#
# linux/arch/arm/boot/compressed/Makefile
#
# create a compressed vmlinuz image from the original vmlinux
#

HEAD	= head.o
OBJS	= misc.o
FONTC	= $(srctree)/drivers/video/console/font_acorn_8x8.c

#
# Architecture dependencies
#
ifeq ($(CONFIG_ARCH_ACORN),y)
OBJS		+= ll_char_wr.o font.o
endif

ifeq ($(CONFIG_ARCH_SHARK),y)
OBJS		+= head-shark.o ofw-shark.o
endif

ifeq ($(CONFIG_ARCH_L7200),y)
OBJS		+= head-l7200.o
endif

ifeq ($(CONFIG_ARCH_P720T),y)
# Borrow this code from SA1100
OBJS		+= head-sa1100.o
endif

ifeq ($(CONFIG_ARCH_SA1100),y)
OBJS		+= head-sa1100.o
endif

ifeq ($(CONFIG_CPU_XSCALE),y)
OBJS		+= head-xscale.o
endif

ifeq ($(CONFIG_PXA_SHARPSL),y)
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
ZBSSADDR	:= ALIGN(4)
endif

SEDFLAGS	= s/TEXT_START/$(ZTEXTADDR)/;s/BSS_START/$(ZBSSADDR)/

suffix_$(CONFIG_KERNEL_GZIP) = gzip
suffix_$(CONFIG_KERNEL_LZO)  = lzo

targets       := vmlinux vmlinux.lds \
		 piggy.$(suffix_y) piggy.$(suffix_y).o \
		 font.o font.c head.o misc.o $(OBJS)

ifeq ($(CONFIG_FUNCTION_TRACER),y)
ORIG_CFLAGS := $(KBUILD_CFLAGS)
KBUILD_CFLAGS = $(subst -pg, , $(ORIG_CFLAGS))
endif

EXTRA_CFLAGS  := -fpic -fno-builtin
EXTRA_AFLAGS  := -Wa,-march=all

# Supply ZRELADDR, INITRD_PHYS and PARAMS_PHYS to the decompressor via
# linker symbols.  We only define initrd_phys and params_phys if the
# machine class defined the corresponding makefile variable.
LDFLAGS_vmlinux := --defsym zreladdr=$(ZRELADDR)
ifeq ($(CONFIG_CPU_ENDIAN_BE8),y)
LDFLAGS_vmlinux += --be8
endif
ifneq ($(INITRD_PHYS),)
LDFLAGS_vmlinux += --defsym initrd_phys=$(INITRD_PHYS)
endif
ifneq ($(PARAMS_PHYS),)
LDFLAGS_vmlinux += --defsym params_phys=$(PARAMS_PHYS)
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

$(obj)/lib1funcs.S: $(srctree)/arch/$(SRCARCH)/lib/lib1funcs.S FORCE
	$(call cmd,shipped)

# Don't allow any static data in misc.o, which
# would otherwise mess up our GOT table
CFLAGS_misc.o := -Dstatic=

$(obj)/vmlinux: $(obj)/vmlinux.lds $(obj)/$(HEAD) $(obj)/piggy.$(suffix_y).o \
	 	$(addprefix $(obj)/, $(OBJS)) $(lib1funcs) FORCE
	$(call if_changed,ld)
	@:

$(obj)/piggy.$(suffix_y): $(obj)/../Image FORCE
	$(call if_changed,$(suffix_y))

$(obj)/piggy.$(suffix_y).o:  $(obj)/piggy.$(suffix_y) FORCE

CFLAGS_font.o := -Dstatic=

$(obj)/font.c: $(FONTC)
	$(call cmd,shipped)

$(obj)/vmlinux.lds: $(obj)/vmlinux.lds.in arch/arm/boot/Makefile .config
	@sed "$(SEDFLAGS)" < $< > $@
