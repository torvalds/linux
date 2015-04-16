#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2013 Altera Corporation
# Copyright (C) 1994, 95, 96, 2003 by Wind River Systems
# Written by Fredrik Markstrom
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" cleaning up for this architecture.
#
# Nios2 port by Wind River Systems Inc trough:
#   fredrik.markstrom@gmail.com and ivarholmqvist@gmail.com

KBUILD_DEFCONFIG := 3c120_defconfig

UTS_SYSNAME = Linux

export MMU

LIBGCC		:= $(shell $(CC) $(KBUILD_CFLAGS) -print-libgcc-file-name)

KBUILD_CFLAGS += -pipe -D__linux__ -D__ELF__
KBUILD_CFLAGS += $(if $(CONFIG_NIOS2_HW_MUL_SUPPORT),-mhw-mul,-mno-hw-mul)
KBUILD_CFLAGS += $(if $(CONFIG_NIOS2_HW_MULX_SUPPORT),-mhw-mulx,-mno-hw-mulx)
KBUILD_CFLAGS += $(if $(CONFIG_NIOS2_HW_DIV_SUPPORT),-mhw-div,-mno-hw-div)
KBUILD_CFLAGS += $(if $(CONFIG_NIOS2_FPU_SUPPORT),-mcustom-fpu-cfg=60-1,)

KBUILD_CFLAGS += -fno-optimize-sibling-calls
KBUILD_CFLAGS += -DUTS_SYSNAME=\"$(UTS_SYSNAME)\"
KBUILD_CFLAGS += -fno-builtin
KBUILD_CFLAGS += -G 0

head-y		:= arch/nios2/kernel/head.o
libs-y		+= arch/nios2/lib/ $(LIBGCC)
core-y		+= arch/nios2/kernel/ arch/nios2/mm/
core-y		+= arch/nios2/platform/

INSTALL_PATH ?= /tftpboot
nios2-boot := arch/$(ARCH)/boot
BOOT_TARGETS = vmImage zImage
PHONY += $(BOOT_TARGETS) install
KBUILD_IMAGE := $(nios2-boot)/vmImage

ifneq ($(CONFIG_NIOS2_DTB_SOURCE),"")
	core-y	+= $(nios2-boot)/
endif

all: vmImage

archclean:
	$(Q)$(MAKE) $(clean)=$(nios2-boot)

%.dtb:
	$(Q)$(MAKE) $(build)=$(nios2-boot) $(nios2-boot)/$@

dtbs:
	$(Q)$(MAKE) $(build)=$(nios2-boot) $(nios2-boot)/$@

$(BOOT_TARGETS): vmlinux
	$(Q)$(MAKE) $(build)=$(nios2-boot) $(nios2-boot)/$@

install:
	$(Q)$(MAKE) $(build)=$(nios2-boot) BOOTIMAGE=$(KBUILD_IMAGE) install

define archhelp
  echo  '* vmImage         - Kernel-only image for U-Boot ($(KBUILD_IMAGE))'
  echo  '  install         - Install kernel using'
  echo  '                     (your) ~/bin/$(INSTALLKERNEL) or'
  echo  '                     (distribution) /sbin/$(INSTALLKERNEL) or'
  echo  '                     install to $$(INSTALL_PATH)'
  echo  '  dtbs            - Build device tree blobs for enabled boards'
endef
