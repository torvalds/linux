# SPDX-License-Identifier: GPL-2.0
#
# Author: Huacai Chen <chenhuacai@loongson.cn>
# Copyright (C) 2020-2022 Loongson Technology Corporation Limited

boot	:= arch/loongarch/boot

KBUILD_DEFCONFIG := loongson3_defconfig

image-name-y			:= vmlinux
image-name-$(CONFIG_EFI_ZBOOT)	:= vmlinuz

ifndef CONFIG_EFI_STUB
KBUILD_IMAGE	:= $(boot)/vmlinux.elf
else
KBUILD_IMAGE	:= $(boot)/$(image-name-y).efi
endif

#
# Select the object file format to substitute into the linker script.
#
64bit-tool-archpref	= loongarch64
32bit-bfd		= elf32-loongarch
64bit-bfd		= elf64-loongarch
32bit-emul		= elf32loongarch
64bit-emul		= elf64loongarch

ifdef CONFIG_DYNAMIC_FTRACE
KBUILD_CPPFLAGS += -DCC_USING_PATCHABLE_FUNCTION_ENTRY
CC_FLAGS_FTRACE := -fpatchable-function-entry=2
endif

ifdef CONFIG_64BIT
tool-archpref		= $(64bit-tool-archpref)
UTS_MACHINE		:= loongarch64
endif

ifneq ($(SUBARCH),$(ARCH))
  ifeq ($(CROSS_COMPILE),)
    CROSS_COMPILE := $(call cc-cross-prefix, $(tool-archpref)-linux-  $(tool-archpref)-linux-gnu-  $(tool-archpref)-unknown-linux-gnu-)
  endif
endif

ifdef CONFIG_64BIT
ld-emul			= $(64bit-emul)
cflags-y		+= -mabi=lp64s
endif

cflags-y			+= -G0 -pipe -msoft-float
LDFLAGS_vmlinux			+= -G0 -static -n -nostdlib

# When the assembler supports explicit relocation hint, we must use it.
# GCC may have -mexplicit-relocs off by default if it was built with an old
# assembler, so we force it via an option.
#
# When the assembler does not supports explicit relocation hint, we can't use
# it.  Disable it if the compiler supports it.
#
# If you've seen "unknown reloc hint" message building the kernel and you are
# now wondering why "-mexplicit-relocs" is not wrapped with cc-option: the
# combination of a "new" assembler and "old" compiler is not supported.  Either
# upgrade the compiler or downgrade the assembler.
ifdef CONFIG_AS_HAS_EXPLICIT_RELOCS
cflags-y			+= -mexplicit-relocs
KBUILD_CFLAGS_KERNEL		+= -mdirect-extern-access
else
cflags-y			+= $(call cc-option,-mno-explicit-relocs)
KBUILD_AFLAGS_KERNEL		+= -Wa,-mla-global-with-pcrel
KBUILD_CFLAGS_KERNEL		+= -Wa,-mla-global-with-pcrel
KBUILD_AFLAGS_MODULE		+= -Wa,-mla-global-with-abs
KBUILD_CFLAGS_MODULE		+= -fplt -Wa,-mla-global-with-abs,-mla-local-with-abs
endif

cflags-y += -ffreestanding
cflags-y += $(call cc-option, -mno-check-zero-division)

ifndef CONFIG_PHYSICAL_START
load-y		= 0x9000000000200000
else
load-y		= $(CONFIG_PHYSICAL_START)
endif
bootvars-y	= VMLINUX_LOAD_ADDRESS=$(load-y)

drivers-$(CONFIG_PCI)		+= arch/loongarch/pci/

KBUILD_AFLAGS	+= $(cflags-y)
KBUILD_CFLAGS	+= $(cflags-y)
KBUILD_CPPFLAGS += -DVMLINUX_LOAD_ADDRESS=$(load-y)

# This is required to get dwarf unwinding tables into .debug_frame
# instead of .eh_frame so we don't discard them.
KBUILD_CFLAGS += -fno-asynchronous-unwind-tables

# Don't emit unaligned accesses.
# Not all LoongArch cores support unaligned access, and as kernel we can't
# rely on others to provide emulation for these accesses.
KBUILD_CFLAGS += $(call cc-option,-mstrict-align)

KBUILD_CFLAGS += -isystem $(shell $(CC) -print-file-name=include)

KBUILD_LDFLAGS	+= -m $(ld-emul)

ifdef CONFIG_LOONGARCH
CHECKFLAGS += $(shell $(CC) $(KBUILD_CFLAGS) -dM -E -x c /dev/null | \
	grep -E -vw '__GNUC_(MINOR_|PATCHLEVEL_)?_' | \
	sed -e "s/^\#define /-D'/" -e "s/ /'='/" -e "s/$$/'/" -e 's/\$$/&&/g')
endif

libs-y += arch/loongarch/lib/
libs-$(CONFIG_EFI_STUB) += $(objtree)/drivers/firmware/efi/libstub/lib.a

# suspend and hibernation support
drivers-$(CONFIG_PM)	+= arch/loongarch/power/

ifeq ($(KBUILD_EXTMOD),)
prepare: vdso_prepare
vdso_prepare: prepare0
	$(Q)$(MAKE) $(build)=arch/loongarch/vdso include/generated/vdso-offsets.h
endif

PHONY += vdso_install
vdso_install:
	$(Q)$(MAKE) $(build)=arch/loongarch/vdso $@

all:	$(notdir $(KBUILD_IMAGE))

vmlinux.elf vmlinux.efi vmlinuz.efi: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(bootvars-y) $(boot)/$@

install:
	$(Q)install -D -m 755 $(KBUILD_IMAGE) $(INSTALL_PATH)/$(image-name-y)-$(KERNELRELEASE)
	$(Q)install -D -m 644 .config $(INSTALL_PATH)/config-$(KERNELRELEASE)
	$(Q)install -D -m 644 System.map $(INSTALL_PATH)/System.map-$(KERNELRELEASE)

define archhelp
	echo '  install              - install kernel into $(INSTALL_PATH)'
	echo
endef
