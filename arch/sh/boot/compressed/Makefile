# SPDX-License-Identifier: GPL-2.0
#
# linux/arch/sh/boot/compressed/Makefile
#
# create a compressed vmlinux image from the original vmlinux
#

OBJECTS := head_32.o misc.o cache.o piggy.o \
           ashiftrt.o ashldi3.o ashrsi3.o ashlsi3.o lshrsi3.o

targets := vmlinux vmlinux.bin vmlinux.bin.gz vmlinux.bin.bz2 \
           vmlinux.bin.lzma vmlinux.bin.xz vmlinux.bin.lzo $(OBJECTS)

GCOV_PROFILE := n
UBSAN_SANITIZE := n

#
# IMAGE_OFFSET is the load offset of the compression loader
#
ifeq ($(CONFIG_32BIT),y)
IMAGE_OFFSET	:= $(shell /bin/bash -c 'printf "0x%08x" \
		     $$[$(CONFIG_MEMORY_START)  + \
			$(CONFIG_BOOT_LINK_OFFSET)]')
else
IMAGE_OFFSET	:= $(shell /bin/bash -c 'printf "0x%08x" \
		     $$[$(CONFIG_PAGE_OFFSET)  + \
			$(KERNEL_MEMORY) + \
			$(CONFIG_BOOT_LINK_OFFSET)]')
endif

ccflags-remove-$(CONFIG_MCOUNT) += -pg

LDFLAGS_vmlinux := --oformat $(ld-bfd) -Ttext $(IMAGE_OFFSET) -e startup \
		   -T $(obj)/../../kernel/vmlinux.lds

KBUILD_CFLAGS += -DDISABLE_BRANCH_PROFILING

$(obj)/vmlinux: $(addprefix $(obj)/, $(OBJECTS)) FORCE
	$(call if_changed,ld)

$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/vmlinux.bin.gz: $(obj)/vmlinux.bin FORCE
	$(call if_changed,gzip)
$(obj)/vmlinux.bin.bz2: $(obj)/vmlinux.bin FORCE
	$(call if_changed,bzip2_with_size)
$(obj)/vmlinux.bin.lzma: $(obj)/vmlinux.bin FORCE
	$(call if_changed,lzma_with_size)
$(obj)/vmlinux.bin.xz: $(obj)/vmlinux.bin FORCE
	$(call if_changed,xzkern_with_size)
$(obj)/vmlinux.bin.lzo: $(obj)/vmlinux.bin FORCE
	$(call if_changed,lzo_with_size)

OBJCOPYFLAGS += -R .empty_zero_page

LDFLAGS_piggy.o := -r --format binary --oformat $(ld-bfd) -T

$(obj)/piggy.o: $(obj)/vmlinux.scr $(obj)/vmlinux.bin.$(suffix_y) FORCE
	$(call if_changed,ld)
