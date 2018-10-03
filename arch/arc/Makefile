#
# Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#

UTS_MACHINE := arc

ifeq ($(CROSS_COMPILE),)
CROSS_COMPILE := arc-linux-
endif

KBUILD_DEFCONFIG := nsim_700_defconfig

cflags-y	+= -fno-common -pipe -fno-builtin -mmedium-calls -D__linux__
cflags-$(CONFIG_ISA_ARCOMPACT)	+= -mA7
cflags-$(CONFIG_ISA_ARCV2)	+= -mcpu=archs

is_700 = $(shell $(CC) -dM -E - < /dev/null | grep -q "ARC700" && echo 1 || echo 0)

ifdef CONFIG_ISA_ARCOMPACT
ifeq ($(is_700), 0)
    $(error Toolchain not configured for ARCompact builds)
endif
endif

ifdef CONFIG_ISA_ARCV2
ifeq ($(is_700), 1)
    $(error Toolchain not configured for ARCv2 builds)
endif
endif

ifdef CONFIG_ARC_CURR_IN_REG
# For a global register defintion, make sure it gets passed to every file
# We had a customer reported bug where some code built in kernel was NOT using
# any kernel headers, and missing the r25 global register
# Can't do unconditionally because of recursive include issues
# due to <linux/thread_info.h>
LINUXINCLUDE	+=  -include ${src}/arch/arc/include/asm/current.h
endif

upto_gcc44    :=  $(call cc-ifversion, -le, 0404, y)
atleast_gcc44 :=  $(call cc-ifversion, -ge, 0404, y)
atleast_gcc48 :=  $(call cc-ifversion, -ge, 0408, y)

cflags-$(atleast_gcc44)			+= -fsection-anchors

cflags-$(CONFIG_ARC_HAS_LLSC)		+= -mlock
cflags-$(CONFIG_ARC_HAS_SWAPE)		+= -mswape

ifdef CONFIG_ISA_ARCV2

ifndef CONFIG_ARC_HAS_LL64
cflags-y				+= -mno-ll64
endif

ifndef CONFIG_ARC_HAS_DIV_REM
cflags-y				+= -mno-div-rem
endif

endif

# By default gcc 4.8 generates dwarf4 which kernel unwinder can't grok
ifeq ($(atleast_gcc48),y)
cflags-$(CONFIG_ARC_DW2_UNWIND)		+= -gdwarf-2
endif

ifndef CONFIG_CC_OPTIMIZE_FOR_SIZE
# Generic build system uses -O2, we want -O3
# Note: No need to add to cflags-y as that happens anyways
ARCH_CFLAGS += -O3
endif

# small data is default for elf32 tool-chain. If not usable, disable it
# This also allows repurposing GP as scratch reg to gcc reg allocator
disable_small_data := y
cflags-$(disable_small_data)		+= -mno-sdata -fcall-used-gp

cflags-$(CONFIG_CPU_BIG_ENDIAN)		+= -mbig-endian
ldflags-$(CONFIG_CPU_BIG_ENDIAN)	+= -EB

# STAR 9000518362: (fixed with binutils shipping with gcc 4.8)
# arc-linux-uclibc-ld (buildroot) or arceb-elf32-ld (EZChip) don't accept
# --build-id w/o "-marclinux". Default arc-elf32-ld is OK
ldflags-$(upto_gcc44)			+= -marclinux

ifndef CONFIG_ARC_HAS_HW_MPY
	cflags-y	+= -mno-mpy
endif

LIBGCC	:= $(shell $(CC) $(cflags-y) --print-libgcc-file-name)

# Modules with short calls might break for calls into builtin-kernel
KBUILD_CFLAGS_MODULE	+= -mlong-calls -mno-millicode

# Finally dump eveything into kernel build system
KBUILD_CFLAGS	+= $(cflags-y)
KBUILD_AFLAGS	+= $(KBUILD_CFLAGS)
LDFLAGS		+= $(ldflags-y)

head-y		:= arch/arc/kernel/head.o

# See arch/arc/Kbuild for content of core part of the kernel
core-y		+= arch/arc/

# w/o this dtb won't embed into kernel binary
core-y		+= arch/arc/boot/dts/

core-$(CONFIG_ARC_PLAT_SIM)	+= arch/arc/plat-sim/
core-$(CONFIG_ARC_PLAT_TB10X)	+= arch/arc/plat-tb10x/
core-$(CONFIG_ARC_PLAT_AXS10X)	+= arch/arc/plat-axs10x/

drivers-$(CONFIG_OPROFILE)	+= arch/arc/oprofile/

libs-y		+= arch/arc/lib/ $(LIBGCC)

boot		:= arch/arc/boot

#default target for make without any arguements.
KBUILD_IMAGE	:= bootpImage

all:	$(KBUILD_IMAGE)
bootpImage: vmlinux

boot_targets += uImage uImage.bin uImage.gz

$(boot_targets): vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

%.dtb %.dtb.S %.dtb.o: scripts
	$(Q)$(MAKE) $(build)=$(boot)/dts $(boot)/dts/$@

dtbs: scripts
	$(Q)$(MAKE) $(build)=$(boot)/dts

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)
