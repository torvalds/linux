# SPDX-License-Identifier: GPL-2.0
#
# s390/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture
#
# Copyright (C) 1994 by Linus Torvalds
#

LD_BFD		:= elf64-s390
KBUILD_LDFLAGS	:= -m elf64_s390
KBUILD_AFLAGS_MODULE += -fPIC
KBUILD_CFLAGS_MODULE += -fPIC
KBUILD_AFLAGS	+= -m64
KBUILD_CFLAGS	+= -m64
ifeq ($(CONFIG_RELOCATABLE),y)
KBUILD_CFLAGS	+= -fPIE
LDFLAGS_vmlinux	:= -pie
endif
aflags_dwarf	:= -Wa,-gdwarf-2
KBUILD_AFLAGS_DECOMPRESSOR := $(CLANG_FLAGS) -m64 -D__ASSEMBLY__
KBUILD_AFLAGS_DECOMPRESSOR += $(if $(CONFIG_DEBUG_INFO),$(aflags_dwarf))
KBUILD_CFLAGS_DECOMPRESSOR := $(CLANG_FLAGS) -m64 -O2
KBUILD_CFLAGS_DECOMPRESSOR += -DDISABLE_BRANCH_PROFILING -D__NO_FORTIFY
KBUILD_CFLAGS_DECOMPRESSOR += -fno-delete-null-pointer-checks -msoft-float -mbackchain
KBUILD_CFLAGS_DECOMPRESSOR += -fno-asynchronous-unwind-tables
KBUILD_CFLAGS_DECOMPRESSOR += -ffreestanding
KBUILD_CFLAGS_DECOMPRESSOR += -fno-stack-protector
KBUILD_CFLAGS_DECOMPRESSOR += $(call cc-disable-warning, address-of-packed-member)
KBUILD_CFLAGS_DECOMPRESSOR += $(if $(CONFIG_DEBUG_INFO),-g)
KBUILD_CFLAGS_DECOMPRESSOR += $(if $(CONFIG_DEBUG_INFO_DWARF4), $(call cc-option, -gdwarf-4,))
UTS_MACHINE	:= s390x
STACK_SIZE	:= $(if $(CONFIG_KASAN),65536,16384)
CHECKFLAGS	+= -D__s390__ -D__s390x__

export LD_BFD

mflags-$(CONFIG_MARCH_Z900)   := -march=z900
mflags-$(CONFIG_MARCH_Z990)   := -march=z990
mflags-$(CONFIG_MARCH_Z9_109) := -march=z9-109
mflags-$(CONFIG_MARCH_Z10)    := -march=z10
mflags-$(CONFIG_MARCH_Z196)   := -march=z196
mflags-$(CONFIG_MARCH_ZEC12)  := -march=zEC12
mflags-$(CONFIG_MARCH_Z13)    := -march=z13
mflags-$(CONFIG_MARCH_Z14)    := -march=z14
mflags-$(CONFIG_MARCH_Z15)    := -march=z15

export CC_FLAGS_MARCH := $(mflags-y)

aflags-y += $(mflags-y)
cflags-y += $(mflags-y)

cflags-$(CONFIG_MARCH_Z900_TUNE)	+= -mtune=z900
cflags-$(CONFIG_MARCH_Z990_TUNE)	+= -mtune=z990
cflags-$(CONFIG_MARCH_Z9_109_TUNE)	+= -mtune=z9-109
cflags-$(CONFIG_MARCH_Z10_TUNE)		+= -mtune=z10
cflags-$(CONFIG_MARCH_Z196_TUNE)	+= -mtune=z196
cflags-$(CONFIG_MARCH_ZEC12_TUNE)	+= -mtune=zEC12
cflags-$(CONFIG_MARCH_Z13_TUNE)		+= -mtune=z13
cflags-$(CONFIG_MARCH_Z14_TUNE)		+= -mtune=z14
cflags-$(CONFIG_MARCH_Z15_TUNE)		+= -mtune=z15

cflags-y += -Wa,-I$(srctree)/arch/$(ARCH)/include

#
# Prevent tail-call optimizations, to get clearer backtraces:
#
cflags-$(CONFIG_FRAME_POINTER) += -fno-optimize-sibling-calls

ifneq ($(call cc-option,-mpacked-stack -mbackchain -msoft-float),)
cflags-$(CONFIG_PACK_STACK)  += -mpacked-stack -D__PACK_STACK
aflags-$(CONFIG_PACK_STACK)  += -D__PACK_STACK
endif

KBUILD_AFLAGS_DECOMPRESSOR += $(aflags-y)
KBUILD_CFLAGS_DECOMPRESSOR += $(cflags-y)

ifneq ($(call cc-option,-mstack-size=8192 -mstack-guard=128),)
cflags-$(CONFIG_CHECK_STACK) += -mstack-size=$(STACK_SIZE)
ifeq ($(call cc-option,-mstack-size=8192),)
cflags-$(CONFIG_CHECK_STACK) += -mstack-guard=$(CONFIG_STACK_GUARD)
endif
endif

ifdef CONFIG_WARN_DYNAMIC_STACK
  ifneq ($(call cc-option,-mwarn-dynamicstack),)
    KBUILD_CFLAGS += -mwarn-dynamicstack
    KBUILD_CFLAGS_DECOMPRESSOR += -mwarn-dynamicstack
  endif
endif

ifdef CONFIG_EXPOLINE
  ifneq ($(call cc-option,$(CC_FLAGS_MARCH) -mindirect-branch=thunk),)
    CC_FLAGS_EXPOLINE := -mindirect-branch=thunk
    CC_FLAGS_EXPOLINE += -mfunction-return=thunk
    CC_FLAGS_EXPOLINE += -mindirect-branch-table
    export CC_FLAGS_EXPOLINE
    cflags-y += $(CC_FLAGS_EXPOLINE) -DCC_USING_EXPOLINE
    aflags-y += -DCC_USING_EXPOLINE
  endif
endif

ifdef CONFIG_FUNCTION_TRACER
  ifeq ($(call cc-option,-mfentry -mnop-mcount),)
    # make use of hotpatch feature if the compiler supports it
    cc_hotpatch	:= -mhotpatch=0,3
    ifneq ($(call cc-option,$(cc_hotpatch)),)
      CC_FLAGS_FTRACE := $(cc_hotpatch)
      KBUILD_AFLAGS	+= -DCC_USING_HOTPATCH
      KBUILD_CFLAGS	+= -DCC_USING_HOTPATCH
    endif
  endif
endif

# Test CFI features of binutils
cfi := $(call as-instr,.cfi_startproc\n.cfi_val_offset 15$(comma)-160\n.cfi_endproc,-DCONFIG_AS_CFI_VAL_OFFSET=1)

KBUILD_CFLAGS	+= -mbackchain -msoft-float $(cflags-y)
KBUILD_CFLAGS	+= -pipe -Wno-sign-compare
KBUILD_CFLAGS	+= -fno-asynchronous-unwind-tables $(cfi)
KBUILD_AFLAGS	+= $(aflags-y) $(cfi)
export KBUILD_AFLAGS_DECOMPRESSOR
export KBUILD_CFLAGS_DECOMPRESSOR

OBJCOPYFLAGS	:= -O binary

head-y		:= arch/s390/kernel/head64.o

libs-y		+= arch/s390/lib/
drivers-y	+= drivers/s390/

boot		:= arch/s390/boot
syscalls	:= arch/s390/kernel/syscalls
tools		:= arch/s390/tools

all: bzImage

#KBUILD_IMAGE is necessary for packaging targets like rpm-pkg, deb-pkg...
KBUILD_IMAGE	:= $(boot)/bzImage

install:
	$(Q)$(MAKE) $(build)=$(boot) $@

bzImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

zfcpdump:
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

vdso_install:
	$(Q)$(MAKE) $(build)=arch/$(ARCH)/kernel/vdso64 $@

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)
	$(Q)$(MAKE) $(clean)=$(tools)

archheaders:
	$(Q)$(MAKE) $(build)=$(syscalls) uapi

archprepare:
	$(Q)$(MAKE) $(build)=$(syscalls) kapi
	$(Q)$(MAKE) $(build)=$(tools) kapi
ifeq ($(KBUILD_EXTMOD),)
# We need to generate vdso-offsets.h before compiling certain files in kernel/.
# In order to do that, we should use the archprepare target, but we can't since
# asm-offsets.h is included in some files used to generate vdso-offsets.h, and
# asm-offsets.h is built in prepare0, for which archprepare is a dependency.
# Therefore we need to generate the header after prepare0 has been made, hence
# this hack.
prepare: vdso_prepare
vdso_prepare: prepare0
	$(Q)$(MAKE) $(build)=arch/s390/kernel/vdso64 include/generated/vdso64-offsets.h
	$(if $(CONFIG_COMPAT),$(Q)$(MAKE) \
		$(build)=arch/s390/kernel/vdso32 include/generated/vdso32-offsets.h)
endif

# Don't use tabs in echo arguments
define archhelp
  echo	'* bzImage         - Kernel image for IPL ($(boot)/bzImage)'
  echo	'  install         - Install kernel using'
  echo	'                    (your) ~/bin/$(INSTALLKERNEL) or'
  echo	'                    (distribution) /sbin/$(INSTALLKERNEL) or'
  echo	'                    install to $$(INSTALL_PATH)'
endef
