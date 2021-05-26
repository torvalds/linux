# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
#

KBUILD_DEFCONFIG := haps_hs_smp_defconfig

ifeq ($(CROSS_COMPILE),)
CROSS_COMPILE := $(call cc-cross-prefix, arc-linux- arceb-linux-)
endif

cflags-y	+= -fno-common -pipe -fno-builtin -mmedium-calls -D__linux__

tune-mcpu-def-$(CONFIG_ISA_ARCOMPACT)	:= -mcpu=arc700
tune-mcpu-def-$(CONFIG_ISA_ARCV2)	:= -mcpu=hs38

ifeq ($(CONFIG_ARC_TUNE_MCPU),"")
cflags-y				+= $(tune-mcpu-def-y)
else
tune-mcpu				:= $(shell echo $(CONFIG_ARC_TUNE_MCPU))
tune-mcpu-ok 				:= $(call cc-option-yn, $(tune-mcpu))
ifeq ($(tune-mcpu-ok),y)
cflags-y				+= $(tune-mcpu)
else
# The flag provided by 'CONFIG_ARC_TUNE_MCPU' option isn't known by this compiler
# (probably the compiler is too old). Use ISA default mcpu flag instead as a safe option.
$(warning ** WARNING ** CONFIG_ARC_TUNE_MCPU flag '$(tune-mcpu)' is unknown, fallback to '$(tune-mcpu-def-y)')
cflags-y				+= $(tune-mcpu-def-y)
endif
endif


ifdef CONFIG_ARC_CURR_IN_REG
# For a global register definition, make sure it gets passed to every file
# We had a customer reported bug where some code built in kernel was NOT using
# any kernel headers, and missing the r25 global register
# Can't do unconditionally because of recursive include issues
# due to <linux/thread_info.h>
LINUXINCLUDE	+=  -include $(srctree)/arch/arc/include/asm/current.h
endif

cflags-y				+= -fsection-anchors

cflags-$(CONFIG_ARC_HAS_LLSC)		+= -mlock
cflags-$(CONFIG_ARC_HAS_SWAPE)		+= -mswape

ifdef CONFIG_ISA_ARCV2

ifdef CONFIG_ARC_USE_UNALIGNED_MEM_ACCESS
cflags-y				+= -munaligned-access
else
cflags-y				+= -mno-unaligned-access
endif

ifndef CONFIG_ARC_HAS_LL64
cflags-y				+= -mno-ll64
endif

ifndef CONFIG_ARC_HAS_DIV_REM
cflags-y				+= -mno-div-rem
endif

endif

cfi := $(call as-instr,.cfi_startproc\n.cfi_endproc,-DARC_DW2_UNWIND_AS_CFI)
cflags-$(CONFIG_ARC_DW2_UNWIND)		+= -fasynchronous-unwind-tables $(cfi)

# small data is default for elf32 tool-chain. If not usable, disable it
# This also allows repurposing GP as scratch reg to gcc reg allocator
disable_small_data := y
cflags-$(disable_small_data)		+= -mno-sdata -fcall-used-gp

cflags-$(CONFIG_CPU_BIG_ENDIAN)		+= -mbig-endian
ldflags-$(CONFIG_CPU_BIG_ENDIAN)	+= -EB

LIBGCC	= $(shell $(CC) $(cflags-y) --print-libgcc-file-name)

# Modules with short calls might break for calls into builtin-kernel
KBUILD_CFLAGS_MODULE	+= -mlong-calls -mno-millicode

# Finally dump eveything into kernel build system
KBUILD_CFLAGS	+= $(cflags-y)
KBUILD_AFLAGS	+= $(KBUILD_CFLAGS)
KBUILD_LDFLAGS	+= $(ldflags-y)

head-y		:= arch/arc/kernel/head.o

# See arch/arc/Kbuild for content of core part of the kernel
core-y		+= arch/arc/

# w/o this dtb won't embed into kernel binary
core-y		+= arch/arc/boot/dts/

core-y				+= arch/arc/plat-sim/
core-$(CONFIG_ARC_PLAT_TB10X)	+= arch/arc/plat-tb10x/
core-$(CONFIG_ARC_PLAT_AXS10X)	+= arch/arc/plat-axs10x/
core-$(CONFIG_ARC_SOC_HSDK)	+= arch/arc/plat-hsdk/

libs-y		+= arch/arc/lib/ $(LIBGCC)

boot		:= arch/arc/boot

boot_targets := uImage.bin uImage.gz uImage.lzma

PHONY += $(boot_targets)
$(boot_targets): vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

uimage-default-y			:= uImage.bin
uimage-default-$(CONFIG_KERNEL_GZIP)	:= uImage.gz
uimage-default-$(CONFIG_KERNEL_LZMA)	:= uImage.lzma

PHONY += uImage
uImage: $(uimage-default-y)
	@ln -sf $< $(boot)/uImage
	@$(kecho) '  Image $(boot)/uImage is ready'

CLEAN_FILES += $(boot)/uImage

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)
