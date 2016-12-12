#
# The stub may be linked into the kernel proper or into a separate boot binary,
# but in either case, it executes before the kernel does (with MMU disabled) so
# things like ftrace and stack-protector are likely to cause trouble if left
# enabled, even if doing so doesn't break the build.
#
cflags-$(CONFIG_X86_32)		:= -march=i386
cflags-$(CONFIG_X86_64)		:= -mcmodel=small
cflags-$(CONFIG_X86)		+= -m$(BITS) -D__KERNEL__ $(LINUX_INCLUDE) -O2 \
				   -fPIC -fno-strict-aliasing -mno-red-zone \
				   -mno-mmx -mno-sse

cflags-$(CONFIG_ARM64)		:= $(subst -pg,,$(KBUILD_CFLAGS))
cflags-$(CONFIG_ARM)		:= $(subst -pg,,$(KBUILD_CFLAGS)) -g0 \
				   -fno-builtin -fpic -mno-single-pic-base

cflags-$(CONFIG_EFI_ARMSTUB)	+= -I$(srctree)/scripts/dtc/libfdt

KBUILD_CFLAGS			:= $(cflags-y) -DDISABLE_BRANCH_PROFILING \
				   $(call cc-option,-ffreestanding) \
				   $(call cc-option,-fno-stack-protector)

GCOV_PROFILE			:= n
KASAN_SANITIZE			:= n
UBSAN_SANITIZE			:= n
OBJECT_FILES_NON_STANDARD	:= y

# Prevents link failures: __sanitizer_cov_trace_pc() is not linked in.
KCOV_INSTRUMENT			:= n

lib-y				:= efi-stub-helper.o gop.o

# include the stub's generic dependencies from lib/ when building for ARM/arm64
arm-deps := fdt_rw.c fdt_ro.c fdt_wip.c fdt.c fdt_empty_tree.c fdt_sw.c sort.c

$(obj)/lib-%.o: $(srctree)/lib/%.c FORCE
	$(call if_changed_rule,cc_o_c)

lib-$(CONFIG_EFI_ARMSTUB)	+= arm-stub.o fdt.o string.o random.o \
				   $(patsubst %.c,lib-%.o,$(arm-deps))

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

STUBCOPY_FLAGS-y		:= -R .debug* -R *ksymtab* -R *kcrctab*
STUBCOPY_FLAGS-$(CONFIG_ARM64)	+= --prefix-alloc-sections=.init \
				   --prefix-symbols=__efistub_
STUBCOPY_RELOC-$(CONFIG_ARM64)	:= R_AARCH64_ABS

$(obj)/%.stub.o: $(obj)/%.o FORCE
	$(call if_changed,stubcopy)

quiet_cmd_stubcopy = STUBCPY $@
      cmd_stubcopy = if $(OBJCOPY) $(STUBCOPY_FLAGS-y) $< $@; then	\
		     $(OBJDUMP) -r $@ | grep $(STUBCOPY_RELOC-y)	\
		     && (echo >&2 "$@: absolute symbol references not allowed in the EFI stub"; \
			 rm -f $@; /bin/false); else /bin/false; fi

#
# ARM discards the .data section because it disallows r/w data in the
# decompressor. So move our .data to .data.efistub, which is preserved
# explicitly by the decompressor linker script.
#
STUBCOPY_FLAGS-$(CONFIG_ARM)	+= --rename-section .data=.data.efistub \
				   -R ___ksymtab+sort -R ___kcrctab+sort
STUBCOPY_RELOC-$(CONFIG_ARM)	:= R_ARM_ABS
