#
# i386/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" cleaning up for this architecture.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
#
# 19990713  Artur Skawina <skawina@geocities.com>
#           Added '-march' and '-mpreferred-stack-boundary' support
#
# 20050320  Kianusch Sayah Karadji <kianusch@sk-tech.net>
#           Added support for GEODE CPU

# Fill in SRCARCH
SRCARCH	:= x86

HAS_BIARCH      := $(call cc-option-yn, -m32)
ifeq ($(HAS_BIARCH),y)
AS              := $(AS) --32
LD              := $(LD) -m elf_i386
CC              := $(CC) -m32
endif

LDFLAGS		:= -m elf_i386
OBJCOPYFLAGS	:= -O binary -R .note -R .comment -S
ifdef CONFIG_RELOCATABLE
LDFLAGS_vmlinux := --emit-relocs
endif
CHECKFLAGS	+= -D__i386__

KBUILD_CFLAGS += -pipe -msoft-float -mregparm=3 -freg-struct-return

# prevent gcc from keeping the stack 16 byte aligned
KBUILD_CFLAGS += $(call cc-option,-mpreferred-stack-boundary=2)

# CPU-specific tuning. Anything which can be shared with UML should go here.
include $(srctree)/arch/i386/Makefile.cpu

# temporary until string.h is fixed
cflags-y += -ffreestanding

# this works around some issues with generating unwind tables in older gccs
# newer gccs do it by default
cflags-y += -maccumulate-outgoing-args

# Disable unit-at-a-time mode on pre-gcc-4.0 compilers, it makes gcc use
# a lot more stack due to the lack of sharing of stacklots:
KBUILD_CFLAGS += $(shell if [ $(call cc-version) -lt 0400 ] ; then echo $(call cc-option,-fno-unit-at-a-time); fi ;)

# do binutils support CFI?
cflags-y += $(call as-instr,.cfi_startproc\n.cfi_rel_offset esp${comma}0\n.cfi_endproc,-DCONFIG_AS_CFI=1,)
KBUILD_AFLAGS += $(call as-instr,.cfi_startproc\n.cfi_rel_offset esp${comma}0\n.cfi_endproc,-DCONFIG_AS_CFI=1,)

# is .cfi_signal_frame supported too?
cflags-y += $(call as-instr,.cfi_startproc\n.cfi_signal_frame\n.cfi_endproc,-DCONFIG_AS_CFI_SIGNAL_FRAME=1,)
KBUILD_AFLAGS += $(call as-instr,.cfi_startproc\n.cfi_signal_frame\n.cfi_endproc,-DCONFIG_AS_CFI_SIGNAL_FRAME=1,)

KBUILD_CFLAGS += $(cflags-y)

# Default subarch .c files
mcore-y  := arch/x86/mach-default

# Voyager subarch support
mflags-$(CONFIG_X86_VOYAGER)	:= -Iinclude/asm-x86/mach-voyager
mcore-$(CONFIG_X86_VOYAGER)	:= arch/x86/mach-voyager

# VISWS subarch support
mflags-$(CONFIG_X86_VISWS)	:= -Iinclude/asm-x86/mach-visws
mcore-$(CONFIG_X86_VISWS)	:= arch/x86/mach-visws

# NUMAQ subarch support
mflags-$(CONFIG_X86_NUMAQ)	:= -Iinclude/asm-x86/mach-numaq
mcore-$(CONFIG_X86_NUMAQ)	:= arch/x86/mach-default

# BIGSMP subarch support
mflags-$(CONFIG_X86_BIGSMP)	:= -Iinclude/asm-x86/mach-bigsmp
mcore-$(CONFIG_X86_BIGSMP)	:= arch/x86/mach-default

#Summit subarch support
mflags-$(CONFIG_X86_SUMMIT) := -Iinclude/asm-x86/mach-summit
mcore-$(CONFIG_X86_SUMMIT)  := arch/x86/mach-default

# generic subarchitecture
mflags-$(CONFIG_X86_GENERICARCH) := -Iinclude/asm-x86/mach-generic
mcore-$(CONFIG_X86_GENERICARCH) := arch/x86/mach-default
core-$(CONFIG_X86_GENERICARCH) += arch/x86/mach-generic/

# ES7000 subarch support
mflags-$(CONFIG_X86_ES7000)	:= -Iinclude/asm-x86/mach-es7000
mcore-$(CONFIG_X86_ES7000)	:= arch/x86/mach-default
core-$(CONFIG_X86_ES7000)	:= arch/x86/mach-es7000/

# Xen paravirtualization support
core-$(CONFIG_XEN)		+= arch/x86/xen/

# lguest paravirtualization support
core-$(CONFIG_LGUEST_GUEST)	+= arch/x86/lguest/

# default subarch .h files
mflags-y += -Iinclude/asm-x86/mach-default

head-y := arch/x86/kernel/head_32.o arch/x86/kernel/init_task.o

libs-y 					+= arch/x86/lib/
core-y					+= arch/x86/kernel/ \
					   arch/x86/mm/ \
					   $(mcore-y)/ \
					   arch/x86/crypto/
drivers-$(CONFIG_MATH_EMULATION)	+= arch/x86/math-emu/
drivers-$(CONFIG_PCI)			+= arch/x86/pci/
# must be linked after kernel/
drivers-$(CONFIG_OPROFILE)		+= arch/x86/oprofile/
drivers-$(CONFIG_PM)			+= arch/x86/power/
drivers-$(CONFIG_FB)                    += arch/x86/video/

KBUILD_CFLAGS += $(mflags-y)
KBUILD_AFLAGS += $(mflags-y)

boot := arch/x86/boot

PHONY += zImage bzImage compressed zlilo bzlilo \
         zdisk bzdisk fdimage fdimage144 fdimage288 isoimage install

all: bzImage

# KBUILD_IMAGE specify target image being built
                    KBUILD_IMAGE := $(boot)/bzImage
zImage zlilo zdisk: KBUILD_IMAGE := arch/x86/boot/zImage

zImage bzImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(KBUILD_IMAGE)
	$(Q)mkdir -p $(objtree)/arch/i386/boot
	$(Q)ln -fsn ../../x86/boot/bzImage $(objtree)/arch/i386/boot/bzImage

compressed: zImage

zlilo bzlilo: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) BOOTIMAGE=$(KBUILD_IMAGE) zlilo

zdisk bzdisk: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) BOOTIMAGE=$(KBUILD_IMAGE) zdisk

fdimage fdimage144 fdimage288 isoimage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) BOOTIMAGE=$(KBUILD_IMAGE) $@

install:
	$(Q)$(MAKE) $(build)=$(boot) BOOTIMAGE=$(KBUILD_IMAGE) install

archclean:
	$(Q)rm -rf $(objtree)/arch/i386/boot
	$(Q)$(MAKE) $(clean)=arch/x86/boot

define archhelp
  echo  '* bzImage	- Compressed kernel image (arch/$(ARCH)/boot/bzImage)'
  echo  '  install	- Install kernel using'
  echo  '		   (your) ~/bin/installkernel or'
  echo  '		   (distribution) /sbin/installkernel or'
  echo  '		   install to $$(INSTALL_PATH) and run lilo'
  echo  '  bzdisk       - Create a boot floppy in /dev/fd0'
  echo  '  fdimage      - Create a boot floppy image'
  echo  '  isoimage     - Create a boot CD-ROM image'
endef

CLEAN_FILES += arch/$(ARCH)/boot/fdimage \
	       arch/$(ARCH)/boot/image.iso \
	       arch/$(ARCH)/boot/mtools.conf
