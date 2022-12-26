# SPDX-License-Identifier: GPL-2.0
#
# The stub may be linked into the kernel proper or into a separate boot binary,
# but in either case, it executes before the kernel does (with MMU disabled) so
# things like ftrace and stack-protector are likely to cause trouble if left
# enabled, even if doing so doesn't break the build.
#

# non-x86 reuses KBUILD_CFLAGS, x86 does not
cflags-y			:= $(KBUILD_CFLAGS)

cflags-$(CONFIG_X86_32)		:= -march=i386
cflags-$(CONFIG_X86_64)		:= -mcmodel=small
cflags-$(CONFIG_X86)		+= -m$(BITS) -D__KERNEL__ \
				   -fPIC -fno-strict-aliasing -mno-red-zone \
				   -mno-mmx -mno-sse -fshort-wchar \
				   -Wno-pointer-sign \
				   $(call cc-disable-warning, address-of-packed-member) \
				   $(call cc-disable-warning, gnu) \
				   -fno-asynchronous-unwind-tables \
				   $(CLANG_FLAGS)

# arm64 uses the full KBUILD_CFLAGS so it's necessary to explicitly
# disable the stackleak plugin
cflags-$(CONFIG_ARM64)		+= -fpie $(DISABLE_STACKLEAK_PLUGIN) \
				   -fno-unwind-tables -fno-asynchronous-unwind-tables \
				   $(call cc-option,-mbranch-protection=none)
cflags-$(CONFIG_ARM)		+= -DEFI_HAVE_STRLEN -DEFI_HAVE_STRNLEN \
				   -DEFI_HAVE_MEMCHR -DEFI_HAVE_STRRCHR \
				   -DEFI_HAVE_STRCMP -fno-builtin -fpic \
				   $(call cc-option,-mno-single-pic-base)
cflags-$(CONFIG_RISCV)		+= -fpic
cflags-$(CONFIG_LOONGARCH)	+= -fpie

cflags-$(CONFIG_EFI_PARAMS_FROM_FDT)	+= -I$(srctree)/scripts/dtc/libfdt

KBUILD_CFLAGS			:= $(subst $(CC_FLAGS_FTRACE),,$(cflags-y)) \
				   -Os -DDISABLE_BRANCH_PROFILING \
				   -include $(srctree)/include/linux/hidden.h \
				   -D__NO_FORTIFY \
				   -ffreestanding \
				   -fno-stack-protector \
				   $(call cc-option,-fno-addrsig) \
				   -D__DISABLE_EXPORTS

#
# struct randomization only makes sense for Linux internal types, which the EFI
# stub code never touches, so let's turn off struct randomization for the stub
# altogether
#
KBUILD_CFLAGS := $(filter-out $(RANDSTRUCT_CFLAGS), $(KBUILD_CFLAGS))

# remove SCS flags from all objects in this directory
KBUILD_CFLAGS := $(filter-out $(CC_FLAGS_SCS), $(KBUILD_CFLAGS))
# disable CFI
KBUILD_CFLAGS := $(filter-out $(CC_FLAGS_CFI), $(KBUILD_CFLAGS))
# disable LTO
KBUILD_CFLAGS := $(filter-out $(CC_FLAGS_LTO), $(KBUILD_CFLAGS))

GCOV_PROFILE			:= n
# Sanitizer runtimes are unavailable and cannot be linked here.
KASAN_SANITIZE			:= n
KCSAN_SANITIZE			:= n
KMSAN_SANITIZE			:= n
UBSAN_SANITIZE			:= n
OBJECT_FILES_NON_STANDARD	:= y

# Prevents link failures: __sanitizer_cov_trace_pc() is not linked in.
KCOV_INSTRUMENT			:= n

lib-y				:= efi-stub-helper.o gop.o secureboot.o tpm.o \
				   file.o mem.o random.o randomalloc.o pci.o \
				   skip_spaces.o lib-cmdline.o lib-ctype.o \
				   alignedmem.o relocate.o printk.o vsprintf.o

# include the stub's libfdt dependencies from lib/ when needed
libfdt-deps			:= fdt_rw.c fdt_ro.c fdt_wip.c fdt.c \
				   fdt_empty_tree.c fdt_sw.c

lib-$(CONFIG_EFI_PARAMS_FROM_FDT) += fdt.o \
				     $(patsubst %.c,lib-%.o,$(libfdt-deps))

$(obj)/lib-%.o: $(srctree)/lib/%.c FORCE
	$(call if_changed_rule,cc_o_c)

lib-$(CONFIG_EFI_GENERIC_STUB)	+= efi-stub.o string.o intrinsics.o systable.o \
				   screen_info.o efi-stub-entry.o

lib-$(CONFIG_ARM)		+= arm32-stub.o
lib-$(CONFIG_ARM64)		+= arm64.o arm64-stub.o arm64-entry.o smbios.o
lib-$(CONFIG_X86)		+= x86-stub.o
lib-$(CONFIG_RISCV)		+= riscv.o riscv-stub.o
lib-$(CONFIG_LOONGARCH)		+= loongarch.o loongarch-stub.o

CFLAGS_arm32-stub.o		:= -DTEXT_OFFSET=$(TEXT_OFFSET)

zboot-obj-$(CONFIG_RISCV)	:= lib-clz_ctz.o lib-ashldi3.o
lib-$(CONFIG_EFI_ZBOOT)		+= zboot.o $(zboot-obj-y)

extra-y				:= $(lib-y)
lib-y				:= $(patsubst %.o,%.stub.o,$(lib-y))

# Even when -mbranch-protection=none is set, Clang will generate a
# .note.gnu.property for code-less object files (like lib/ctype.c),
# so work around this by explicitly removing the unwanted section.
# https://bugs.llvm.org/show_bug.cgi?id=46480
STUBCOPY_FLAGS-y		+= --remove-section=.note.gnu.property

#
# For x86, bootloaders like systemd-boot or grub-efi do not zero-initialize the
# .bss section, so the .bss section of the EFI stub needs to be included in the
# .data section of the compressed kernel to ensure initialization. Rename the
# .bss section here so it's easy to pick out in the linker script.
#
STUBCOPY_FLAGS-$(CONFIG_X86)	+= --rename-section .bss=.bss.efistub,load,alloc
STUBCOPY_RELOC-$(CONFIG_X86_32)	:= R_386_32
STUBCOPY_RELOC-$(CONFIG_X86_64)	:= R_X86_64_64

#
# ARM discards the .data section because it disallows r/w data in the
# decompressor. So move our .data to .data.efistub and .bss to .bss.efistub,
# which are preserved explicitly by the decompressor linker script.
#
STUBCOPY_FLAGS-$(CONFIG_ARM)	+= --rename-section .data=.data.efistub	\
				   --rename-section .bss=.bss.efistub,load,alloc
STUBCOPY_RELOC-$(CONFIG_ARM)	:= R_ARM_ABS

#
# arm64 puts the stub in the kernel proper, which will unnecessarily retain all
# code indefinitely unless it is annotated as __init/__initdata/__initconst etc.
# So let's apply the __init annotations at the section level, by prefixing
# the section names directly. This will ensure that even all the inline string
# literals are covered.
# The fact that the stub and the kernel proper are essentially the same binary
# also means that we need to be extra careful to make sure that the stub does
# not rely on any absolute symbol references, considering that the virtual
# kernel mapping that the linker uses is not active yet when the stub is
# executing. So build all C dependencies of the EFI stub into libstub, and do
# a verification pass to see if any absolute relocations exist in any of the
# object files.
#
STUBCOPY_FLAGS-$(CONFIG_ARM64)	+= --prefix-alloc-sections=.init \
				   --prefix-symbols=__efistub_
STUBCOPY_RELOC-$(CONFIG_ARM64)	:= R_AARCH64_ABS64

# For RISC-V, we don't need anything special other than arm64. Keep all the
# symbols in .init section and make sure that no absolute symbols references
# doesn't exist.
STUBCOPY_FLAGS-$(CONFIG_RISCV)	+= --prefix-alloc-sections=.init \
				   --prefix-symbols=__efistub_
STUBCOPY_RELOC-$(CONFIG_RISCV)	:= R_RISCV_HI20

# For LoongArch, keep all the symbols in .init section and make sure that no
# absolute symbols references exist.
STUBCOPY_FLAGS-$(CONFIG_LOONGARCH)	+= --prefix-alloc-sections=.init \
					   --prefix-symbols=__efistub_
STUBCOPY_RELOC-$(CONFIG_LOONGARCH)	:= R_LARCH_MARK_LA

$(obj)/%.stub.o: $(obj)/%.o FORCE
	$(call if_changed,stubcopy)

#
# Strip debug sections and some other sections that may legally contain
# absolute relocations, so that we can inspect the remaining sections for
# such relocations. If none are found, regenerate the output object, but
# this time, use objcopy and leave all sections in place.
#
quiet_cmd_stubcopy = STUBCPY $@
      cmd_stubcopy =							\
	$(STRIP) --strip-debug -o $@ $<;				\
	if $(OBJDUMP) -r $@ | grep $(STUBCOPY_RELOC-y); then		\
		echo "$@: absolute symbol references not allowed in the EFI stub" >&2; \
		/bin/false;						\
	fi;								\
	$(OBJCOPY) $(STUBCOPY_FLAGS-y) $< $@
