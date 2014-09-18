#
# linux/arch/unicore32/boot/compressed/Makefile
#
# create a compressed vmlinuz image from the original vmlinux
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2001~2010 GUAN Xue-tao
#

ccflags-y	:= -fpic -fno-builtin
asflags-y	:= -Wa,-march=all

OBJS		:= misc.o

# font.c and font.o
CFLAGS_font.o	:= -Dstatic=
$(obj)/font.c: $(srctree)/lib/fonts/font_8x8.c
	$(call cmd,shipped)

# piggy.S and piggy.o
suffix_$(CONFIG_KERNEL_GZIP)	:= gzip
suffix_$(CONFIG_KERNEL_BZIP2)	:= bz2
suffix_$(CONFIG_KERNEL_LZO)	:= lzo
suffix_$(CONFIG_KERNEL_LZMA)	:= lzma

$(obj)/piggy.$(suffix_y): $(obj)/../Image FORCE
	$(call if_changed,$(suffix_y))

SEDFLAGS_piggy	= s/DECOMP_SUFFIX/$(suffix_y)/
$(obj)/piggy.S: $(obj)/piggy.S.in
	@sed "$(SEDFLAGS_piggy)" < $< > $@

$(obj)/piggy.o:  $(obj)/piggy.$(suffix_y) $(obj)/piggy.S FORCE

targets		:= vmlinux vmlinux.lds font.o font.c head.o misc.o \
			piggy.$(suffix_y) piggy.o piggy.S \

# Make sure files are removed during clean
extra-y		+= piggy.gzip piggy.bz2 piggy.lzo piggy.lzma

# ?
LDFLAGS_vmlinux += -p
# Report unresolved symbol references
LDFLAGS_vmlinux += --no-undefined
# Delete all temporary local symbols
LDFLAGS_vmlinux += -X
# Next argument is a linker script
LDFLAGS_vmlinux += -T

# For uidivmod
$(obj)/vmlinux: $(obj)/vmlinux.lds $(obj)/head.o $(obj)/piggy.o \
		$(obj)/misc.o FORCE
	$(call if_changed,ld)
	@:

# We now have a PIC decompressor implementation.  Decompressors running
# from RAM should not define ZTEXTADDR.  Decompressors running directly
# from ROM or Flash must define ZTEXTADDR (preferably via the config)
ZTEXTADDR	:= 0x03000000
ZBSSADDR	:= ALIGN(4)

SEDFLAGS_lds	= s/TEXT_START/$(ZTEXTADDR)/;s/BSS_START/$(ZBSSADDR)/
$(obj)/vmlinux.lds: $(obj)/vmlinux.lds.in arch/unicore32/boot/Makefile $(KCONFIG_CONFIG)
	@sed "$(SEDFLAGS_lds)" < $< > $@

