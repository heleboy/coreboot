##
## This file is part of the coreboot project.
##
## Copyright (C) 2014 Google Inc
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; version 2 of the License.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##

# ccache integration
ifeq ($(CONFIG_CCACHE),y)

CCACHE:=$(word 1,$(wildcard $(addsuffix /ccache,$(subst :, ,$(PATH)))))
ifeq ($(CCACHE),)
$(error ccache selected, but not found in PATH)
endif

export CCACHE_COMPILERCHECK=content
export CCACHE_BASEDIR=$(top)

$(foreach arch,$(ARCH_SUPPORTED), \
	$(eval CC_$(arch):=$(CCACHE) $(CC_$(arch))))

HOSTCC:=$(CCACHE) $(HOSTCC)
HOSTCXX:=$(CCACHE) $(HOSTCXX)
ROMCC=$(CCACHE) $(ROMCC_BIN)
endif

# scan-build integration
ifneq ($(CCC_ANALYZER_OUTPUT_FORMAT),)

ifeq ($(CCC_ANALYZER_ANALYSIS),)
export CCC_ANALYZER_ANALYSIS := -analyzer-opt-analyze-headers
endif

$(foreach arch,$(ARCH_SUPPORTED), \
	$(eval CC_$(arch):=CCC_CC="$(CC_$(arch))" $(CC) ))

HOSTCC:=CCC_CC="$(HOSTCC)" $(CC)
HOSTCXX:=CCC_CXX="$(HOSTCXX)" $(CXX)
ROMCC=CCC_CC="$(ROMCC_BIN)" $(CC)
endif

COREBOOT_STANDARD_STAGES := bootblock libverstage verstage romstage ramstage
MAP-libverstage := verstage

ARCHDIR-i386	:= x86
ARCHDIR-x86_32	:= x86
ARCHDIR-x86_64	:= x86
ARCHDIR-arm	:= arm
ARCHDIR-arm64	:= arm64
ARCHDIR-riscv	:= riscv
ARCHDIR-mips	:= mips
ARCHDIR-power8	:= power8

CFLAGS_arm	+=
CFLAGS_arm64	+= -mgeneral-regs-only
CFLAGS_mips	+= -mips32r2 -G 0 -mno-abicalls -fno-pic
CFLAGS_riscv	+=
CFLAGS_x86_32	+=
CFLAGS_x86_64	+= -mcmodel=large -mno-red-zone
CFLAGS_power8	+=

# Some boards only provide 2K stacks, so storing lots of data there leads to
# problems. Since C rules don't allow us to statically determine the maximum
# stack use, we use 1.5K as heuristic, assuming that we typically have lots
# of tiny stack frames and the odd large one.
#
# Store larger buffers in BSS, use MAYBE_STATIC to share code with __PRE_RAM__
# on x86.
# Since GCCs detection of dynamic array bounds unfortunately seems to be
# very basic, you'll sometimes have to use a static upper bound for the
# size and an assert() to make sure it's honored (see gpio_base3_value()
# for an example).
# (If you absolutely need a larger stack frame and are 100% sure it cannot
# cause problems, you can whitelist it with #pragma diagnostic.)
CFLAGS_arm	+= -Wstack-usage=1536
CFLAGS_arm64	+= -Wstack-usage=1536
CFLAGS_mips	+= -Wstack-usage=1536
CFLAGS_riscv	+= -Wstack-usage=1536
CFLAGS_power8	+= -Wstack-usage=1536

toolchain_to_dir = \
	$(foreach arch,$(ARCH_SUPPORTED),\
	$(eval CPPFLAGS_$(arch) += \
	-Isrc/arch/$(ARCHDIR-$(arch))/include))

# set_stage_toolchain: Decides the toolchain to be used by every stage
# E.g.: If bootblock is x86_32, it sets ARCH-BOOTBLOCK-y = x86_32, whereas
# ARCH-BOOTBLOCK-n = armv7. Then, ARCH-BOOTBLOCK-y can be used anywhere to
# decide the compiler toolchain for bootblock stage
# This step is essential for initializing the toolchain for coreboot standard
# stages i.e. bootblock, romstage and ramstage, since it acts as the second
# parameter to create_class_compiler below in init_standard_toolchain
map_stage = $(strip $(if $(MAP-$(1)),$(MAP-$(1)),$(1)))
set_stage_toolchain= \
	$(foreach arch,$(ARCH_SUPPORTED), \
	$(eval ARCH-$(1)-$($(shell \
		echo CONFIG_ARCH_$(call map_stage,$(1))_$(arch) | \
		tr '[:lower:]' '[:upper:]')) := $(arch)))

# create_class_compiler: Used to create compiler tool set for
# special classes
# @1: special class
# @2: compiler set to be used
# e.g.: smm special class uses i386 as compiler set
define create_class_compiler
$(if $(2),,$(warning *** The toolchain architecture for $(1) is unknown.) \
	$(error Check your .config file for CONFIG_ARCH_$(1)_* settings))
CC_$(1) := $(CC_$(2))
LD_$(1) := $(LD_$(2))
NM_$(1) := $(NM_$(2))
AR_$(1) := $(AR_$(2))
OBJCOPY_$(1) := $(OBJCOPY_$(2))
OBJDUMP_$(1) := $(OBJDUMP_$(2))
STRIP_$(1) := $(STRIP_$(2))
READELF_$(1) := $(READELF_$(2))
CFLAGS_$(1) = $$(CFLAGS_common) $$(CFLAGS_$(2))
CPPFLAGS_$(1) = $$(CPPFLAGS_common) $$(CPPFLAGS_$(2)) -D__ARCH_$(2)__
COMPILER_RT_$(1) := $$(COMPILER_RT_$(2))
COMPILER_RT_FLAGS_$(1) := $$(COMPILER_RT_FLAGS_$(2))
LDFLAGS_$(1) = $$(LDFLAGS_common) $$(LDFLAGS_$(2))
endef

# define_class: Allows defining any program as dynamic class and compiler tool
# set for the same based on the architecture for which the program is to be
# compiled
# @1: program (class name)
# @2: architecture for which the program needs to be compiled
# IMP: Ensure that define_class is called before any .c or .S files are added to
# the class of the program. Check subdirs-y for order of subdirectory inclusions
define define_class
classes-y += $(1)
$(eval $(call create_class_compiler,$(1),$(2)))
endef

# initialize standard toolchain (CC,AS and others) for give stage
# @1 : stage for which the toolchain is to be initialized
init_standard_toolchain = \
	$(eval $(call set_stage_toolchain,$(1))) \
	$(eval $(call create_class_compiler,$(1),$(ARCH-$(1)-y)))

init_stages = \
	$(foreach stage,$(COREBOOT_STANDARD_STAGES), \
	$(eval $(call init_standard_toolchain,$(stage))))

$(eval $(call toolchain_to_dir))

$(call init_stages)

# Test for coreboot toolchain (except when explicitly not requested)
ifneq ($(NOCOMPILE),1)
# Only run if we're doing a build (not for tests, kconfig, ...)
# rationale: gcc versions by Linux distributions tend to be quite messed up
# llvm/clang also needs patches supplied by the coreboot build
COMPILERFAIL:=0
IASLFAIL:=0

ifneq ($(CONFIG_ANY_TOOLCHAIN),y)
# Verify that the coreboot toolchain is being used.
$(foreach arch,$(sort $(foreach stage,\
	$(COREBOOT_STANDARD_STAGES),$(ARCH-$(stage)-y))), \
	$(if $(shell if [ -n "$(CC_$(arch))" ]; then \
		$(CC_$(arch)) -v 2>&1 | grep -q "coreboot toolchain" || \
		echo not-coreboot; else echo not-coreboot; fi), \
		$(eval COMPILERFAIL:=1)\
		$(warning The coreboot toolchain for '$(arch)'\
			architecture was not found.)))
# If iasl doesn't match the current coreboot version, fail the test
# TODO: Figure out if iasl is even needed for the build.
$(if $(shell if [ -n "$(IASL)" ]; then \
	$(IASL) -v 2>&1 | grep -q "coreboot toolchain" || \
	echo not-coreboot; else echo not-coreboot; fi), \
	$(eval COMPILERFAIL:=1)$(eval IASLFAIL:=1)\
	$(warning The coreboot toolchain version of iasl \
		'$(shell util/crossgcc/buildgcc -s iasl)' was not found))
else #$(CONFIG_ANY_TOOLCHAIN)
# If the coreboot toolchain isn't being used, verify that there is A toolchain
$(foreach arch,$(sort \
	$(foreach stage,$(COREBOOT_STANDARD_STAGES),$(ARCH-$(stage)-y))), \
	$(if $(CC_$(arch)),, $(eval COMPILERFAIL:=1) \
	$(warning No compiler found for '$(arch)' architecture. \
		Install one or use the coreboot toolchain?)) )
# If iasl isn't present, fail
# TODO: Figure out if iasl is even needed for the build.
$(if $(IASL),, $(eval COMPILERFAIL:=1)$(eval IASLFAIL:=1) \
	$(warning iasl not found. \
		Please install it or use the coreboot toolchain.))
endif

# If the compiler check failed, print out warnings
ifeq ($(COMPILERFAIL),1)
ifneq ($(XGCCPATH),)
$(warning )
$(warning Path to your toolchain is currently set to '$(XGCCPATH)')
endif
$(warning )
$(warning To build the entire coreboot toolchain: run 'make crossgcc')
ifeq ($(IASLFAIL),1)
$(warning To build just IASL: run 'make iasl')
endif #($(IASLFAIL),1)
$(warning For more toolchain build targets: run 'make help_toolchain')
$(warning )
ifneq ($(CONFIG_ANY_TOOLCHAIN),y)
$(warning To try to use any toolchain in your path, \
	run 'make menuconfig', then select)
$(warning the config option: 'General setup', \
	and 'Allow building with any toolchain')
$(warning Note that this is NOT supported.  \
	Using it means you're on your own.)
$(warning )
endif #($(CONFIG_ANY_TOOLCHAIN),y)
$(error Halting the build)
endif #($(COMPILERFAIL),1)

endif #($(NOCOMPILE),1)

# Run the toolchain version checks if the requested target is 'test-toolchain'
# Checks the versions of GCC, binutils, clang, and IASL
ifneq ($(MAKECMDGOALS),)
ifneq ($(filter test-toolchain,$(MAKECMDGOALS)),)
$(foreach arch, $(ARCH_SUPPORTED), \
	$(if $(shell if [ -n "$(GCC_CC_$(arch))" ]; then \
		$(GCC_CC_$(arch)) -v 2>&1 | \
		grep -q "$(shell util/crossgcc/buildgcc -s gcc)" || \
		echo not-current; fi), \
		$(eval COMPILER_OUT_OF_DATE:=1) \
		$(warning The coreboot toolchain version of gcc for '$(arch)' \
			architecture is not the current version.)) \
	$(if $(shell if [ -n "$(CLANG_CC_$(arch))" ]; then \
		$(CLANG_CC_$(arch)) -v 2>&1 | \
		grep -q "$(shell util/crossgcc/buildgcc -s clang)" || \
		echo not-current; fi), \
		$(eval COMPILER_OUT_OF_DATE:=1)\
		$(warning The coreboot toolchain version of clang for \
			'$(arch)' architecture is not the current version.)) \
	$(if $(shell if [ "$(OBJDUMP_$(arch))" != "invalidobjdump" ]; then \
	$(OBJDUMP_$(arch)) -v 2>&1 | \
	grep -q "$(shell util/crossgcc/buildgcc -s binutils)" || \
	echo not-current; fi), \
		$(eval COMPILER_OUT_OF_DATE:=1)\
		$(warning The coreboot toolchain version of binutils for \
			'$(arch)' architecture is not the current version.)) \
)
$(if $(shell if [ -n "$(IASL)" ]; then $(IASL) -v 2>&1 | \
	grep -q "$(shell util/crossgcc/buildgcc -s iasl)" || \
	echo not-coreboot; fi), \
	$(eval COMPILER_OUT_OF_DATE:=1)\
	$(warning The coreboot toolchain version of iasl \
		is not the current version))
endif #($(filter crossgcc_check%,$(MAKECMDGOALS)),)
endif #($(MAKECMDGOALS),)
