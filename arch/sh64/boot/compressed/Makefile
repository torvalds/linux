#
# linux/arch/sh64/boot/compressed/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2002 Stuart Menefy
# Copyright (C) 2004 Paul Mundt
#
# create a compressed vmlinux image from the original vmlinux
#

targets		:= vmlinux vmlinux.bin vmlinux.bin.gz \
		   head.o misc.o cache.o piggy.o vmlinux.lds

EXTRA_AFLAGS	:= -traditional

OBJECTS		:= $(obj)/head.o $(obj)/misc.o $(obj)/cache.o

#
# ZIMAGE_OFFSET is the load offset of the compression loader
# (4M for the kernel plus 64K for this loader)
#
ZIMAGE_OFFSET = $(shell printf "0x%8x" $$[$(CONFIG_MEMORY_START)+0x400000+0x10000])

LDFLAGS_vmlinux := -Ttext $(ZIMAGE_OFFSET) -e startup \
		    -T $(obj)/../../kernel/vmlinux.lds \
		    --no-warn-mismatch

$(obj)/vmlinux: $(OBJECTS) $(obj)/piggy.o FORCE
	$(call if_changed,ld)
	@:

$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/vmlinux.bin.gz: $(obj)/vmlinux.bin FORCE
	$(call if_changed,gzip)

LDFLAGS_piggy.o := -r --format binary --oformat elf32-sh64-linux -T
OBJCOPYFLAGS += -R .empty_zero_page

$(obj)/piggy.o: $(obj)/vmlinux.lds $(obj)/vmlinux.bin.gz FORCE
	$(call if_changed,ld)

