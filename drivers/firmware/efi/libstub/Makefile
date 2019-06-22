# SPDX-License-Identifier: GPL-2.0
#
# The stub may be linked into the kernel proper or into a separate boot binary,
# but in either case, it executes before the kernel does (with MMU disabled) so
# things like ftrace and stack-protector are likely to cause trouble if left
# enabled, even if doing so doesn't break the build.
#
cflags-$(CONFIG_X86_32)		:= -march=i386
cflags-$(CONFIG_X86_64)		:= -mcmodel=small
cflags-$(CONFIG_X86)		+= -m$(BITS) -D__KERNEL__ -O2 \
				   -fPIC -fno-strict-aliasing -mno-red-zone \
				   -mno-mmx -mno-sse -fshort-wchar \
				   -Wno-pointer-sign \
				   $(call cc-disable-warning, address-of-packed-member) \
				   $(call cc-disable-warning, gnu)

# arm64 uses the full KBUILD_CFLAGS so it's necessary to explicitly
# disable the stackleak plugin
cflags-$(CONFIG_ARM64)		:= $(subst -pg,,$(KBUILD_CFLAGS)) -fpie \
				   $(DISABLE_STACKLEAK_PLUGIN)
cflags-$(CONFIG_ARM)		:= $(subst -pg,,$(KBUILD_CFLAGS)) \
				   -fno-builtin -fpic \
				   $(call cc-option,-mno-single-pic-base)

cflags-$(CONFIG_EFI_ARMSTUB)	+= -I$(srctree)/scripts/dtc/libfdt

KBUILD_CFLAGS			:= $(cflags-y) -DDISABLE_BRANCH_PROFILING \
				   -D__NO_FORTIFY \
				   $(call cc-option,-ffreestanding) \
				   $(call cc-option,-fno-stack-protector) \
				   -D__DISABLE_EXPORTS \
				   $(DISABLE_LTO)

GCOV_PROFILE			:= n
KASAN_SANITIZE			:= n
UBSAN_SANITIZE			:= n
OBJECT_FILES_NON_STANDARD	:= y

# Prevents link failures: __sanitizer_cov_trace_pc() is not linked in.
KCOV_INSTRUMENT			:= n

lib-y				:= efi-stub-helper.o gop.o secureboot.o tpm.o

# include the stub's generic dependencies from lib/ when building for ARM/arm64
arm-deps-y := fdt_rw.c fdt_ro.c fdt_wip.c fdt.c fdt_empty_tree.c fdt_sw.c
arm-deps-$(CONFIG_ARM64) += sort.c

$(obj)/lib-%.o: $(srctree)/lib/%.c FORCE
	$(call if_changed_rule,cc_o_c)

lib-$(CONFIG_EFI_ARMSTUB)	+= arm-stub.o fdt.o string.o random.o \
				   $(patsubst %.c,lib-%.o,$(arm-deps-y))

lib-$(CONFIG_ARM)		+= arm32-stub.o
lib-$(CONFIG_ARM64)		+= arm64-stub.o
CFLAGS_arm64-stub.o 		:= -DTEXT_OFFSET=$(TEXT_OFFSET)

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
extra-$(CONFIG_EFI_ARMSTUB)	:= $(lib-y)
lib-$(CONFIG_EFI_ARMSTUB)	:= $(patsubst %.o,%.stub.o,$(lib-y))

STUBCOPY_RM-y			:= -R *ksymtab* -R *kcrctab*
STUBCOPY_FLAGS-$(CONFIG_ARM64)	+= --prefix-alloc-sections=.init \
				   --prefix-symbols=__efistub_
STUBCOPY_RELOC-$(CONFIG_ARM64)	:= R_AARCH64_ABS

$(obj)/%.stub.o: $(obj)/%.o FORCE
	$(call if_changed,stubcopy)

#
# Strip debug sections and some other sections that may legally contain
# absolute relocations, so that we can inspect the remaining sections for
# such relocations. If none are found, regenerate the output object, but
# this time, use objcopy and leave all sections in place.
#
quiet_cmd_stubcopy = STUBCPY $@
      cmd_stubcopy = if $(STRIP) --strip-debug $(STUBCOPY_RM-y) -o $@ $<; \
		     then if $(OBJDUMP) -r $@ | grep $(STUBCOPY_RELOC-y); \
		     then (echo >&2 "$@: absolute symbol references not allowed in the EFI stub"; \
			   rm -f $@; /bin/false); 			  \
		     else $(OBJCOPY) $(STUBCOPY_FLAGS-y) $< $@; fi	  \
		     else /bin/false; fi

#
# ARM discards the .data section because it disallows r/w data in the
# decompressor. So move our .data to .data.efistub, which is preserved
# explicitly by the decompressor linker script.
#
STUBCOPY_FLAGS-$(CONFIG_ARM)	+= --rename-section .data=.data.efistub
STUBCOPY_RELOC-$(CONFIG_ARM)	:= R_ARM_ABS
