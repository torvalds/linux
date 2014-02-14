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

UTS_SYSNAME = Linux

export MMU

cflags-y :=
LDFLAGS		:=
LDFLAGS_vmlinux	:=

LIBGCC		:= $(shell $(CC) $(KBUILD_CFLAGS) -print-libgcc-file-name)

KBUILD_AFLAGS += $(cflags-y)
KBUILD_CFLAGS += -pipe -D__linux__ -D__ELF__ $(cflags-y)
KBUILD_CFLAGS += $(if $(CONFIG_NIOS2_HW_MUL_SUPPORT),-mhw-mul,-mno-hw-mul)
KBUILD_CFLAGS += $(if $(CONFIG_NIOS2_HW_MULX_SUPPORT),-mhw-mulx,-mno-hw-mulx)
KBUILD_CFLAGS += $(if $(CONFIG_NIOS2_HW_DIV_SUPPORT),-mhw-div,-mno-hw-div)
KBUILD_CFLAGS += $(if $(CONFIG_NIOS2_FPU_SUPPORT),-mcustom-fpu-cfg=60-1,)

KBUILD_CFLAGS += -fno-optimize-sibling-calls
KBUILD_CFLAGS += -DUTS_SYSNAME=\"$(UTS_SYSNAME)\"
KBUILD_CFLAGS += -fno-builtin
KBUILD_CFLAGS += -G 0

LDFLAGS += -mnios2elf

head-y		:= arch/nios2/kernel/head.o
libs-y		+= arch/nios2/lib/ $(LIBGCC)
core-y		+= arch/nios2/kernel/ arch/nios2/mm/
core-y		+= arch/nios2/platform/

drivers-$(CONFIG_OPROFILE) += arch/$(ARCH)/oprofile/

INSTALL_PATH ?= /tftpboot
boot := arch/$(ARCH)/boot
BOOT_TARGETS = vmImage zImage
PHONY += $(BOOT_TARGETS) install
KBUILD_IMAGE := $(boot)/vmImage

ifneq ($(CONFIG_DTB_SOURCE),"")
	core-y	+= $(boot)/
endif

all: vmImage

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

%.dtb:
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

dtbs:
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

$(BOOT_TARGETS): vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

install:
	$(Q)$(MAKE) $(build)=$(boot) BOOTIMAGE=$(KBUILD_IMAGE) install

define archhelp
  echo  '* vmImage         - Kernel-only image for U-Boot (arch/$(ARCH)/boot/vmImage)'
  echo  '  install         - Install kernel using'
  echo  '                     (your) ~/bin/$(CROSS_COMPILE)installkernel or'
  echo  '                     (distribution) PATH: $(CROSS_COMPILE)installkernel or'
  echo  '                     install to $$(INSTALL_PATH)'
  echo  '  dtbs            - Build device tree blobs for enabled boards'
endef
