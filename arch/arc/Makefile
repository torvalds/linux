#
# Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#

UTS_MACHINE := arc

KBUILD_DEFCONFIG := fpga_defconfig

cflags-y	+= -mA7 -fno-common -pipe -fno-builtin -D__linux__

LINUXINCLUDE	+=  -include ${src}/arch/arc/include/asm/defines.h

ifdef CONFIG_ARC_CURR_IN_REG
# For a global register defintion, make sure it gets passed to every file
# We had a customer reported bug where some code built in kernel was NOT using
# any kernel headers, and missing the r25 global register
# Can't do unconditionally (like above) because of recursive include issues
# due to <linux/thread_info.h>
LINUXINCLUDE	+=  -include ${src}/arch/arc/include/asm/current.h
endif

atleast_gcc44 :=  $(call cc-ifversion, -gt, 0402, y)
cflags-$(atleast_gcc44)			+= -fsection-anchors

cflags-$(CONFIG_ARC_HAS_LLSC)		+= -mlock
cflags-$(CONFIG_ARC_HAS_SWAPE)		+= -mswape
cflags-$(CONFIG_ARC_HAS_RTSC)		+= -mrtsc
cflags-$(CONFIG_ARC_DW2_UNWIND)		+= -fasynchronous-unwind-tables

ifndef CONFIG_CC_OPTIMIZE_FOR_SIZE
# Generic build system uses -O2, we want -O3
cflags-y  += -O3
endif

# small data is default for elf32 tool-chain. If not usable, disable it
# This also allows repurposing GP as scratch reg to gcc reg allocator
disable_small_data := y
cflags-$(disable_small_data)		+= -mno-sdata -fcall-used-gp

cflags-$(CONFIG_CPU_BIG_ENDIAN)		+= -mbig-endian
ldflags-$(CONFIG_CPU_BIG_ENDIAN)	+= -EB

# STAR 9000518362:
# arc-linux-uclibc-ld (buildroot) or arceb-elf32-ld (EZChip) don't accept
# --build-id w/o "-marclinux".
# Default arc-elf32-ld is OK
ldflags-y				+= -marclinux

ARC_LIBGCC				:= -mA7
cflags-$(CONFIG_ARC_HAS_HW_MPY)		+= -multcost=16

ifndef CONFIG_ARC_HAS_HW_MPY
	cflags-y	+= -mno-mpy

# newlib for ARC700 assumes MPY to be always present, which is generally true
# However, if someone really doesn't want MPY, we need to use the 600 ver
# which coupled with -mno-mpy will use mpy emulation
# With gcc 4.4.7, -mno-mpy is enough to make any other related adjustments,
# e.g. increased cost of MPY. With gcc 4.2.1 this had to be explicitly hinted

	ARC_LIBGCC		:= -marc600
	ifneq ($(atleast_gcc44),y)
		cflags-y	+= -multcost=30
	endif
endif

LIBGCC	:= $(shell $(CC) $(ARC_LIBGCC) $(cflags-y) --print-libgcc-file-name)

# Modules with short calls might break for calls into builtin-kernel
KBUILD_CFLAGS_MODULE	+= -mlong-calls

# Finally dump eveything into kernel build system
KBUILD_CFLAGS	+= $(cflags-y)
KBUILD_AFLAGS	+= $(KBUILD_CFLAGS)
LDFLAGS		+= $(ldflags-y)

head-y		:= arch/arc/kernel/head.o

# See arch/arc/Kbuild for content of core part of the kernel
core-y		+= arch/arc/

# w/o this dtb won't embed into kernel binary
core-y		+= arch/arc/boot/dts/

core-$(CONFIG_ARC_PLAT_FPGA_LEGACY)	+= arch/arc/plat-arcfpga/

drivers-$(CONFIG_OPROFILE)	+= arch/arc/oprofile/

libs-y		+= arch/arc/lib/ $(LIBGCC)

#default target for make without any arguements.
KBUILD_IMAGE := bootpImage

all:	$(KBUILD_IMAGE)
boot	:= arch/arc/boot

bootpImage: vmlinux

uImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

%.dtb %.dtb.S %.dtb.o: scripts
	$(Q)$(MAKE) $(build)=$(boot)/dts $(boot)/dts/$@

dtbs: scripts
	$(Q)$(MAKE) $(build)=$(boot)/dts dtbs

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

# Hacks to enable final link due to absence of link-time branch relexation
# and gcc choosing optimal(shorter) branches at -O3
#
# vineetg Feb 2010: -mlong-calls switched off for overall kernel build
# However lib/decompress_inflate.o (.init.text) calls
# zlib_inflate_workspacesize (.text) causing relocation errors.
# Thus forcing all exten calls in this file to be long calls
export CFLAGS_decompress_inflate.o = -mmedium-calls
export CFLAGS_initramfs.o = -mmedium-calls
ifdef CONFIG_SMP
export CFLAGS_core.o = -mmedium-calls
endif
