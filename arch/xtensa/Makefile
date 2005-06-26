#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2001 - 2005  Tensilica Inc.
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture

# Core configuration.
# (Use CPU=<xtensa_config> to use another default compiler.)

cpu-$(CONFIG_XTENSA_CPU_LINUX_BE)	:= linux_be
cpu-$(CONFIG_XTENSA_CPU_LINUX_CUSTOM)	:= linux_custom

CPU = $(cpu-y)
export CPU

# Platform configuration

platform-y					:= common
platform-$(CONFIG_XTENSA_PLATFORM_XT2000)	:= xt2000
platform-$(CONFIG_XTENSA_PLATFORM_ISS)		:= iss

PLATFORM = $(platform-y)
export PLATFORM

#LDFLAGS_vmlinux		:= -T$(word 1,$(LINKSCRIPT))
AFLAGS_vmlinux.lds.o	:= -Uxtensa
CPPFLAGS		+= -Iarch/xtensa -Iinclude/asm  -mlongcalls  -g
AFLAGS			+= -Iarch/xtensa -Iinclude/asm
CPP			= $(CC) -E $(CFLAGS)

cflags-y	+= -Iarch/xtensa -pipe -mlongcalls


KBUILD_DEFCONFIG := common_defconfig

# ramdisk/initrd support
# You need a compressed ramdisk image, named ramdisk.gz in
# arch/xtensa/boot/ramdisk

core-$(CONFIG_EMBEDDED_RAMDISK)	+= arch/xtensa/boot/ramdisk/

# Test for cross compiling

ifneq ($(CPU),)
  COMPILE_ARCH = $(shell uname -m)

  ifneq ($(COMPILE_ARCH), xtensa)
    ifndef CROSS_COMPILE
      CROSS_COMPILE = xtensa_$(CPU)-
    endif
  endif
endif

#

LIBGCC := $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)

head-y		:= arch/xtensa/kernel/head.o
core-y		+= arch/xtensa/kernel/ \
		   arch/xtensa/mm/ arch/xtensa/platform-$(PLATFORM)/
libs-y		+= arch/xtensa/lib/ $(LIBGCC)

boot	:= arch/xtensa/boot

arch/xtensa/kernel/asm-offsets.s: \
	arch/xtensa/kernel/asm-offsets.c \
	include/asm-xtensa/.platform

include/asm-xtensa/offsets.h: arch/xtensa/kernel/asm-offsets.s
	$(call filechk,gen-asm-offsets)

prepare: include/asm-xtensa/.platform include/asm-xtensa/offsets.h

# Update machine cpu and platform symlinks if something which affects
# them changed.

include/asm-xtensa/.platform: $(wildcard include/config/arch/*.h)
	@echo '  Setting up cpu ($(CPU)) and platform ($(PLATFORM)) symlinks'
	$(Q)rm -f include/asm-xtensa/platform
	$(Q)rm -f include/asm-xtensa/xtensa/config
	$(Q)(cd include/asm-xtensa/; ln -sf platform-$(PLATFORM) platform)
	$(Q)(cd include/asm-xtensa/xtensa; ln -sf config-$(CPU) config)

all: zImage

bzImage : zImage

zImage zImage.initrd: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $@

CLEAN_FILES	+= arch/xtensa/vmlinux.lds include/asm-xtensa/offset.h

define archhelp
  @echo '* zImage      - Compressed kernel image (arch/xtensa/boot/images/zImage.*)'
endef

