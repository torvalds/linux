#
# s390/Makefile
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

LD_BFD		:= elf64-s390
LDFLAGS		:= -m elf64_s390
KBUILD_AFLAGS_MODULE += -fPIC
KBUILD_CFLAGS_MODULE += -fPIC
KBUILD_CFLAGS	+= -m64
KBUILD_AFLAGS	+= -m64
UTS_MACHINE	:= s390x
STACK_SIZE	:= 16384
CHECKFLAGS	+= -D__s390__ -D__s390x__

export LD_BFD

mflags-$(CONFIG_MARCH_Z900)   := -march=z900
mflags-$(CONFIG_MARCH_Z990)   := -march=z990
mflags-$(CONFIG_MARCH_Z9_109) := -march=z9-109
mflags-$(CONFIG_MARCH_Z10)    := -march=z10
mflags-$(CONFIG_MARCH_Z196)   := -march=z196
mflags-$(CONFIG_MARCH_ZEC12)  := -march=zEC12
mflags-$(CONFIG_MARCH_Z13)   := -march=z13

export CC_FLAGS_MARCH := $(mflags-y)

aflags-y += $(mflags-y)
cflags-y += $(mflags-y)

cflags-$(CONFIG_MARCH_Z900_TUNE)	+= -mtune=z900
cflags-$(CONFIG_MARCH_Z990_TUNE)	+= -mtune=z990
cflags-$(CONFIG_MARCH_Z9_109_TUNE)	+= -mtune=z9-109
cflags-$(CONFIG_MARCH_Z10_TUNE)		+= -mtune=z10
cflags-$(CONFIG_MARCH_Z196_TUNE)	+= -mtune=z196
cflags-$(CONFIG_MARCH_ZEC12_TUNE)	+= -mtune=zEC12
cflags-$(CONFIG_MARCH_Z13_TUNE)	+= -mtune=z13

cflags-y += -Wa,-I$(srctree)/arch/$(ARCH)/include

#KBUILD_IMAGE is necessary for make rpm
KBUILD_IMAGE	:=arch/s390/boot/image

#
# Prevent tail-call optimizations, to get clearer backtraces:
#
cflags-$(CONFIG_FRAME_POINTER) += -fno-optimize-sibling-calls

# old style option for packed stacks
ifeq ($(call cc-option-yn,-mkernel-backchain),y)
cflags-$(CONFIG_PACK_STACK)  += -mkernel-backchain -D__PACK_STACK
aflags-$(CONFIG_PACK_STACK)  += -D__PACK_STACK
endif

# new style option for packed stacks
ifeq ($(call cc-option-yn,-mpacked-stack),y)
cflags-$(CONFIG_PACK_STACK)  += -mpacked-stack -D__PACK_STACK
aflags-$(CONFIG_PACK_STACK)  += -D__PACK_STACK
endif

ifeq ($(call cc-option-yn,-mstack-size=8192 -mstack-guard=128),y)
cflags-$(CONFIG_CHECK_STACK) += -mstack-size=$(STACK_SIZE)
ifneq ($(call cc-option-yn,-mstack-size=8192),y)
cflags-$(CONFIG_CHECK_STACK) += -mstack-guard=$(CONFIG_STACK_GUARD)
endif
endif

ifeq ($(call cc-option-yn,-mwarn-dynamicstack),y)
cflags-$(CONFIG_WARN_DYNAMIC_STACK) += -mwarn-dynamicstack
endif

ifdef CONFIG_FUNCTION_TRACER
# make use of hotpatch feature if the compiler supports it
cc_hotpatch	:= -mhotpatch=0,3
ifeq ($(call cc-option-yn,$(cc_hotpatch)),y)
CC_FLAGS_FTRACE := $(cc_hotpatch)
KBUILD_AFLAGS	+= -DCC_USING_HOTPATCH
KBUILD_CFLAGS	+= -DCC_USING_HOTPATCH
endif
endif

KBUILD_CFLAGS	+= -mbackchain -msoft-float $(cflags-y)
KBUILD_CFLAGS	+= -pipe -fno-strength-reduce -Wno-sign-compare
KBUILD_AFLAGS	+= $(aflags-y)

OBJCOPYFLAGS	:= -O binary

head-y		:= arch/s390/kernel/head.o
head-y		+= arch/s390/kernel/head64.o

# See arch/s390/Kbuild for content of core part of the kernel
core-y		+= arch/s390/

libs-y		+= arch/s390/lib/
drivers-y	+= drivers/s390/

# must be linked after kernel
drivers-$(CONFIG_OPROFILE)	+= arch/s390/oprofile/

boot		:= arch/s390/boot
tools		:= arch/s390/tools

all: image bzImage

install: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $@

image bzImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

zfcpdump:
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

vdso_install:
	$(Q)$(MAKE) $(build)=arch/$(ARCH)/kernel/vdso64 $@
	$(Q)$(MAKE) $(build)=arch/$(ARCH)/kernel/vdso32 $@

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)
	$(Q)$(MAKE) $(clean)=$(tools)

archprepare:
	$(Q)$(MAKE) $(build)=$(tools) include/generated/facilities.h

# Don't use tabs in echo arguments
define archhelp
  echo  '* image           - Kernel image for IPL ($(boot)/image)'
  echo	'* bzImage         - Compressed kernel image for IPL ($(boot)/bzImage)'
  echo	'  install         - Install kernel using'
  echo	'                    (your) ~/bin/$(INSTALLKERNEL) or'
  echo	'                    (distribution) /sbin/$(INSTALLKERNEL) or'
  echo	'                    install to $$(INSTALL_PATH)'
endef
