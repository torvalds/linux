#
# x86_64/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
#
# 19990713  Artur Skawina <skawina@geocities.com>
#           Added '-march' and '-mpreferred-stack-boundary' support
# 20000913  Pavel Machek <pavel@suse.cz>
#	    Converted for x86_64 architecture
# 20010105  Andi Kleen, add IA32 compiler.
#           ....and later removed it again....
#
# $Id: Makefile,v 1.31 2002/03/22 15:56:07 ak Exp $

#
# early bootup linking needs 32bit. You can either use real 32bit tools
# here or 64bit tools in 32bit mode.
#
IA32_CC := $(CC) $(CPPFLAGS) -m32 -O2 -fomit-frame-pointer
IA32_LD := $(LD) -m elf_i386
IA32_AS := $(CC) $(AFLAGS) -m32 -Wa,--32 -traditional -c
IA32_OBJCOPY := $(CROSS_COMPILE)objcopy
IA32_CPP := $(CROSS_COMPILE)gcc -m32 -E
export IA32_CC IA32_LD IA32_AS IA32_OBJCOPY IA32_CPP


LDFLAGS		:= -m elf_x86_64
OBJCOPYFLAGS	:= -O binary -R .note -R .comment -S
LDFLAGS_vmlinux := -e stext

CHECKFLAGS      += -D__x86_64__ -m64

cflags-$(CONFIG_MK8) += $(call cc-option,-march=k8)
cflags-$(CONFIG_MPSC) += $(call cc-option,-march=nocona)
CFLAGS += $(cflags-y)

CFLAGS += -mno-red-zone
CFLAGS += -mcmodel=kernel
CFLAGS += -pipe
# this makes reading assembly source easier, but produces worse code
# actually it makes the kernel smaller too.
CFLAGS += -fno-reorder-blocks	
CFLAGS += -Wno-sign-compare
ifneq ($(CONFIG_DEBUG_INFO),y)
CFLAGS += -fno-asynchronous-unwind-tables
# -fweb shrinks the kernel a bit, but the difference is very small
# it also messes up debugging, so don't use it for now.
#CFLAGS += $(call cc-option,-fweb)
endif
# -funit-at-a-time shrinks the kernel .text considerably
# unfortunately it makes reading oopses harder.
CFLAGS += $(call cc-option,-funit-at-a-time)
# prevent gcc from generating any FP code by mistake
CFLAGS += $(call cc-option,-mno-sse -mno-mmx -mno-sse2 -mno-3dnow,)

head-y := arch/x86_64/kernel/head.o arch/x86_64/kernel/head64.o arch/x86_64/kernel/init_task.o

libs-y 					+= arch/x86_64/lib/
core-y					+= arch/x86_64/kernel/ arch/x86_64/mm/
core-$(CONFIG_IA32_EMULATION)		+= arch/x86_64/ia32/
drivers-$(CONFIG_PCI)			+= arch/x86_64/pci/
drivers-$(CONFIG_OPROFILE)		+= arch/x86_64/oprofile/

boot := arch/x86_64/boot

.PHONY: bzImage bzlilo install archmrproper \
	fdimage fdimage144 fdimage288 archclean

#Default target when executing "make"
all: bzImage

BOOTIMAGE                     := arch/x86_64/boot/bzImage
KBUILD_IMAGE                  := $(BOOTIMAGE)

bzImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(BOOTIMAGE)

bzlilo: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) BOOTIMAGE=$(BOOTIMAGE) zlilo

bzdisk: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) BOOTIMAGE=$(BOOTIMAGE) zdisk

install fdimage fdimage144 fdimage288: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) BOOTIMAGE=$(BOOTIMAGE) $@

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

prepare: include/asm-$(ARCH)/offset.h

arch/$(ARCH)/kernel/asm-offsets.s: include/asm include/linux/version.h \
				   include/config/MARKER

include/asm-$(ARCH)/offset.h: arch/$(ARCH)/kernel/asm-offsets.s
	$(call filechk,gen-asm-offsets)

CLEAN_FILES += include/asm-$(ARCH)/offset.h

define archhelp
  echo  '* bzImage	- Compressed kernel image (arch/$(ARCH)/boot/bzImage)'
  echo  '  install	- Install kernel using'
  echo  '                  (your) ~/bin/installkernel or'
  echo  '                  (distribution) /sbin/installkernel or'
  echo  '        	  install to $$(INSTALL_PATH) and run lilo'
endef

CLEAN_FILES += arch/$(ARCH)/boot/fdimage arch/$(ARCH)/boot/mtools.conf


