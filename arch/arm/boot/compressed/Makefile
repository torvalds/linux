#
# linux/arch/arm/boot/compressed/Makefile
#
# create a compressed vmlinuz image from the original vmlinux
#

OBJS		=

AFLAGS_head.o += -DTEXT_OFFSET=$(TEXT_OFFSET)
HEAD	= head.o
OBJS	+= misc.o decompress.o
ifeq ($(CONFIG_DEBUG_UNCOMPRESS),y)
OBJS	+= debug.o
endif
FONTC	= $(srctree)/lib/fonts/font_acorn_8x8.c

# string library code (-Os is enforced to keep it much smaller)
OBJS		+= string.o
CFLAGS_string.o	:= -Os

ifeq ($(CONFIG_ARM_VIRT_EXT),y)
OBJS		+= hyp-stub.o
endif

GCOV_PROFILE		:= n

#
# Architecture dependencies
#
ifeq ($(CONFIG_ARCH_ACORN),y)
OBJS		+= ll_char_wr.o font.o
endif

ifeq ($(CONFIG_ARCH_SA1100),y)
OBJS		+= head-sa1100.o
endif

ifeq ($(CONFIG_CPU_XSCALE),y)
OBJS		+= head-xscale.o
endif

ifeq ($(CONFIG_PXA_SHARPSL_DETECT_MACH_ID),y)
OBJS		+= head-sharpsl.o
endif

ifeq ($(CONFIG_CPU_ENDIAN_BE32),y)
ifeq ($(CONFIG_CPU_CP15),y)
OBJS		+= big-endian.o
else
# The endian should be set by h/w design.
endif
endif

#
# We now have a PIC decompressor implementation.  Decompressors running
# from RAM should not define ZTEXTADDR.  Decompressors running directly
# from ROM or Flash must define ZTEXTADDR (preferably via the config)
# FIXME: Previous assignment to ztextaddr-y is lost here. See SHARK
ifeq ($(CONFIG_ZBOOT_ROM),y)
ZTEXTADDR	:= $(CONFIG_ZBOOT_ROM_TEXT)
ZBSSADDR	:= $(CONFIG_ZBOOT_ROM_BSS)
else
ZTEXTADDR	:= 0
ZBSSADDR	:= ALIGN(8)
endif

CPPFLAGS_vmlinux.lds := -DTEXT_START="$(ZTEXTADDR)" -DBSS_START="$(ZBSSADDR)"

suffix_$(CONFIG_KERNEL_GZIP) = gzip
suffix_$(CONFIG_KERNEL_LZO)  = lzo
suffix_$(CONFIG_KERNEL_LZMA) = lzma
suffix_$(CONFIG_KERNEL_XZ)   = xzkern
suffix_$(CONFIG_KERNEL_LZ4)  = lz4

# Borrowed libfdt files for the ATAG compatibility mode

libfdt		:= fdt_rw.c fdt_ro.c fdt_wip.c fdt.c
libfdt_hdrs	:= fdt.h libfdt.h libfdt_internal.h

libfdt_objs	:= $(addsuffix .o, $(basename $(libfdt)))

$(addprefix $(obj)/,$(libfdt) $(libfdt_hdrs)): $(obj)/%: $(srctree)/scripts/dtc/libfdt/%
	$(call cmd,shipped)

$(addprefix $(obj)/,$(libfdt_objs) atags_to_fdt.o): \
	$(addprefix $(obj)/,$(libfdt_hdrs))

ifeq ($(CONFIG_ARM_ATAG_DTB_COMPAT),y)
OBJS	+= $(libfdt_objs) atags_to_fdt.o
endif

targets       := vmlinux vmlinux.lds \
		 piggy.$(suffix_y) piggy.$(suffix_y).o \
		 lib1funcs.o lib1funcs.S ashldi3.o ashldi3.S bswapsdi2.o \
		 bswapsdi2.S font.o font.c head.o misc.o $(OBJS)

# Make sure files are removed during clean
extra-y       += piggy.gzip piggy.lzo piggy.lzma piggy.xzkern piggy.lz4 \
		 lib1funcs.S ashldi3.S bswapsdi2.S $(libfdt) $(libfdt_hdrs) \
		 hyp-stub.S

KBUILD_CFLAGS += -DDISABLE_BRANCH_PROFILING

ifeq ($(CONFIG_FUNCTION_TRACER),y)
ORIG_CFLAGS := $(KBUILD_CFLAGS)
KBUILD_CFLAGS = $(subst -pg, , $(ORIG_CFLAGS))
endif

# -fstack-protector-strong triggers protection checks in this code,
# but it is being used too early to link to meaningful stack_chk logic.
nossp_flags := $(call cc-option, -fno-stack-protector)
CFLAGS_atags_to_fdt.o := $(nossp_flags)
CFLAGS_fdt.o := $(nossp_flags)
CFLAGS_fdt_ro.o := $(nossp_flags)
CFLAGS_fdt_rw.o := $(nossp_flags)
CFLAGS_fdt_wip.o := $(nossp_flags)

ccflags-y := -fpic -mno-single-pic-base -fno-builtin -I$(obj)
asflags-y := -DZIMAGE

# Supply kernel BSS size to the decompressor via a linker symbol.
KBSS_SZ = $(shell $(CROSS_COMPILE)size $(obj)/../../../../vmlinux | \
		awk 'END{print $$3}')
LDFLAGS_vmlinux = --defsym _kernel_bss_size=$(KBSS_SZ)
# Supply ZRELADDR to the decompressor via a linker symbol.
ifneq ($(CONFIG_AUTO_ZRELADDR),y)
LDFLAGS_vmlinux += --defsym zreladdr=$(ZRELADDR)
endif
ifeq ($(CONFIG_CPU_ENDIAN_BE8),y)
LDFLAGS_vmlinux += --be8
endif
# ?
LDFLAGS_vmlinux += -p
# Report unresolved symbol references
LDFLAGS_vmlinux += --no-undefined
# Delete all temporary local symbols
LDFLAGS_vmlinux += -X
# Next argument is a linker script
LDFLAGS_vmlinux += -T

# For __aeabi_uidivmod
lib1funcs = $(obj)/lib1funcs.o

$(obj)/lib1funcs.S: $(srctree)/arch/$(SRCARCH)/lib/lib1funcs.S
	$(call cmd,shipped)

# For __aeabi_llsl
ashldi3 = $(obj)/ashldi3.o

$(obj)/ashldi3.S: $(srctree)/arch/$(SRCARCH)/lib/ashldi3.S
	$(call cmd,shipped)

# For __bswapsi2, __bswapdi2
bswapsdi2 = $(obj)/bswapsdi2.o

$(obj)/bswapsdi2.S: $(srctree)/arch/$(SRCARCH)/lib/bswapsdi2.S
	$(call cmd,shipped)

# We need to prevent any GOTOFF relocs being used with references
# to symbols in the .bss section since we cannot relocate them
# independently from the rest at run time.  This can be achieved by
# ensuring that no private .bss symbols exist, as global symbols
# always have a GOT entry which is what we need.
# The .data section is already discarded by the linker script so no need
# to bother about it here.
check_for_bad_syms = \
bad_syms=$$($(CROSS_COMPILE)nm $@ | sed -n 's/^.\{8\} [bc] \(.*\)/\1/p') && \
[ -z "$$bad_syms" ] || \
  ( echo "following symbols must have non local/private scope:" >&2; \
    echo "$$bad_syms" >&2; rm -f $@; false )

check_for_multiple_zreladdr = \
if [ $(words $(ZRELADDR)) -gt 1 -a "$(CONFIG_AUTO_ZRELADDR)" = "" ]; then \
	echo 'multiple zreladdrs: $(ZRELADDR)'; \
	echo 'This needs CONFIG_AUTO_ZRELADDR to be set'; \
	false; \
fi

efi-obj-$(CONFIG_EFI_STUB) := $(objtree)/drivers/firmware/efi/libstub/lib.a

$(obj)/vmlinux: $(obj)/vmlinux.lds $(obj)/$(HEAD) $(obj)/piggy.$(suffix_y).o \
		$(addprefix $(obj)/, $(OBJS)) $(lib1funcs) $(ashldi3) \
		$(bswapsdi2) $(efi-obj-y) FORCE
	@$(check_for_multiple_zreladdr)
	$(call if_changed,ld)
	@$(check_for_bad_syms)

$(obj)/piggy.$(suffix_y): $(obj)/../Image FORCE
	$(call if_changed,$(suffix_y))

$(obj)/piggy.$(suffix_y).o:  $(obj)/piggy.$(suffix_y) FORCE

CFLAGS_font.o := -Dstatic=

$(obj)/font.c: $(FONTC)
	$(call cmd,shipped)

AFLAGS_hyp-stub.o := -Wa,-march=armv7-a

$(obj)/hyp-stub.S: $(srctree)/arch/$(SRCARCH)/kernel/hyp-stub.S
	$(call cmd,shipped)
