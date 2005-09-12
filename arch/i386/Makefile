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

HAS_BIARCH      := $(call cc-option-yn, -m32)
ifeq ($(HAS_BIARCH),y)
AS              := $(AS) --32
LD              := $(LD) -m elf_i386
CC              := $(CC) -m32
endif

LDFLAGS		:= -m elf_i386
OBJCOPYFLAGS	:= -O binary -R .note -R .comment -S
LDFLAGS_vmlinux :=
CHECKFLAGS	+= -D__i386__

CFLAGS += -pipe -msoft-float

# prevent gcc from keeping the stack 16 byte aligned
CFLAGS += $(call cc-option,-mpreferred-stack-boundary=2)

align := $(cc-option-align)
cflags-$(CONFIG_M386)		+= -march=i386
cflags-$(CONFIG_M486)		+= -march=i486
cflags-$(CONFIG_M586)		+= -march=i586
cflags-$(CONFIG_M586TSC)	+= -march=i586
cflags-$(CONFIG_M586MMX)	+= $(call cc-option,-march=pentium-mmx,-march=i586)
cflags-$(CONFIG_M686)		+= -march=i686
cflags-$(CONFIG_MPENTIUMII)	+= -march=i686 $(call cc-option,-mtune=pentium2)
cflags-$(CONFIG_MPENTIUMIII)	+= -march=i686 $(call cc-option,-mtune=pentium3)
cflags-$(CONFIG_MPENTIUMM)	+= -march=i686 $(call cc-option,-mtune=pentium3)
cflags-$(CONFIG_MPENTIUM4)	+= -march=i686 $(call cc-option,-mtune=pentium4)
cflags-$(CONFIG_MK6)		+= -march=k6
# Please note, that patches that add -march=athlon-xp and friends are pointless.
# They make zero difference whatsosever to performance at this time.
cflags-$(CONFIG_MK7)		+= $(call cc-option,-march=athlon,-march=i686 $(align)-functions=4)
cflags-$(CONFIG_MK8)		+= $(call cc-option,-march=k8,$(call cc-option,-march=athlon,-march=i686 $(align)-functions=4))
cflags-$(CONFIG_MCRUSOE)	+= -march=i686 $(align)-functions=0 $(align)-jumps=0 $(align)-loops=0
cflags-$(CONFIG_MEFFICEON)	+= -march=i686 $(call cc-option,-mtune=pentium3) $(align)-functions=0 $(align)-jumps=0 $(align)-loops=0
cflags-$(CONFIG_MWINCHIPC6)	+= $(call cc-option,-march=winchip-c6,-march=i586)
cflags-$(CONFIG_MWINCHIP2)	+= $(call cc-option,-march=winchip2,-march=i586)
cflags-$(CONFIG_MWINCHIP3D)	+= $(call cc-option,-march=winchip2,-march=i586)
cflags-$(CONFIG_MCYRIXIII)	+= $(call cc-option,-march=c3,-march=i486) $(align)-functions=0 $(align)-jumps=0 $(align)-loops=0
cflags-$(CONFIG_MVIAC3_2)	+= $(call cc-option,-march=c3-2,-march=i686)

# AMD Elan support
cflags-$(CONFIG_X86_ELAN)	+= -march=i486

# Geode GX1 support
cflags-$(CONFIG_MGEODEGX1)		+= $(call cc-option,-march=pentium-mmx,-march=i486)

# -mregparm=3 works ok on gcc-3.0 and later
#
GCC_VERSION			:= $(call cc-version)
cflags-$(CONFIG_REGPARM) 	+= $(shell if [ $(GCC_VERSION) -ge 0300 ] ; then echo "-mregparm=3"; fi ;)

# Disable unit-at-a-time mode, it makes gcc use a lot more stack
# due to the lack of sharing of stacklots.
CFLAGS += $(call cc-option,-fno-unit-at-a-time)

CFLAGS += $(cflags-y)

# Default subarch .c files
mcore-y  := mach-default

# Voyager subarch support
mflags-$(CONFIG_X86_VOYAGER)	:= -Iinclude/asm-i386/mach-voyager
mcore-$(CONFIG_X86_VOYAGER)	:= mach-voyager

# VISWS subarch support
mflags-$(CONFIG_X86_VISWS)	:= -Iinclude/asm-i386/mach-visws
mcore-$(CONFIG_X86_VISWS)	:= mach-visws

# NUMAQ subarch support
mflags-$(CONFIG_X86_NUMAQ)	:= -Iinclude/asm-i386/mach-numaq
mcore-$(CONFIG_X86_NUMAQ)	:= mach-default

# BIGSMP subarch support
mflags-$(CONFIG_X86_BIGSMP)	:= -Iinclude/asm-i386/mach-bigsmp
mcore-$(CONFIG_X86_BIGSMP)	:= mach-default

#Summit subarch support
mflags-$(CONFIG_X86_SUMMIT) := -Iinclude/asm-i386/mach-summit
mcore-$(CONFIG_X86_SUMMIT)  := mach-default

# generic subarchitecture
mflags-$(CONFIG_X86_GENERICARCH) := -Iinclude/asm-i386/mach-generic
mcore-$(CONFIG_X86_GENERICARCH) := mach-default
core-$(CONFIG_X86_GENERICARCH) += arch/i386/mach-generic/

# ES7000 subarch support
mflags-$(CONFIG_X86_ES7000)	:= -Iinclude/asm-i386/mach-es7000
mcore-$(CONFIG_X86_ES7000)	:= mach-default
core-$(CONFIG_X86_ES7000)	:= arch/i386/mach-es7000/

# default subarch .h files
mflags-y += -Iinclude/asm-i386/mach-default

head-y := arch/i386/kernel/head.o arch/i386/kernel/init_task.o

libs-y 					+= arch/i386/lib/
core-y					+= arch/i386/kernel/ \
					   arch/i386/mm/ \
					   arch/i386/$(mcore-y)/ \
					   arch/i386/crypto/
drivers-$(CONFIG_MATH_EMULATION)	+= arch/i386/math-emu/
drivers-$(CONFIG_PCI)			+= arch/i386/pci/
# must be linked after kernel/
drivers-$(CONFIG_OPROFILE)		+= arch/i386/oprofile/
drivers-$(CONFIG_PM)			+= arch/i386/power/

CFLAGS += $(mflags-y)
AFLAGS += $(mflags-y)

boot := arch/i386/boot

.PHONY: zImage bzImage compressed zlilo bzlilo \
	zdisk bzdisk fdimage fdimage144 fdimage288 install kernel_install

all: bzImage

# KBUILD_IMAGE specify target image being built
                    KBUILD_IMAGE := $(boot)/bzImage
zImage zlilo zdisk: KBUILD_IMAGE := arch/i386/boot/zImage

zImage bzImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(KBUILD_IMAGE)

compressed: zImage

zlilo bzlilo: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) BOOTIMAGE=$(KBUILD_IMAGE) zlilo

zdisk bzdisk: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) BOOTIMAGE=$(KBUILD_IMAGE) zdisk

fdimage fdimage144 fdimage288: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) BOOTIMAGE=$(KBUILD_IMAGE) $@

install: vmlinux
install kernel_install:
	$(Q)$(MAKE) $(build)=$(boot) BOOTIMAGE=$(KBUILD_IMAGE) install

archclean:
	$(Q)$(MAKE) $(clean)=arch/i386/boot

define archhelp
  echo  '* bzImage	- Compressed kernel image (arch/$(ARCH)/boot/bzImage)'
  echo  '  install	- Install kernel using'
  echo  '		   (your) ~/bin/installkernel or'
  echo  '		   (distribution) /sbin/installkernel or'
  echo  '		   install to $$(INSTALL_PATH) and run lilo'
  echo  '  bzdisk       - Create a boot floppy in /dev/fd0'
  echo  '  fdimage      - Create a boot floppy image'
endef

CLEAN_FILES += arch/$(ARCH)/boot/fdimage arch/$(ARCH)/boot/mtools.conf
