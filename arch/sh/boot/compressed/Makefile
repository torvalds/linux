#
# linux/arch/sh/boot/compressed/Makefile
#
# create a compressed vmlinux image from the original vmlinux
#

targets		:= vmlinux vmlinux.bin vmlinux.bin.gz \
		   vmlinux.bin.bz2 vmlinux.bin.lzma \
		   head_$(BITS).o misc.o piggy.o

OBJECTS = $(obj)/head_$(BITS).o $(obj)/misc.o $(obj)/cache.o

ifdef CONFIG_SH_STANDARD_BIOS
OBJECTS += $(obj)/../../kernel/sh_bios.o
endif

#
# IMAGE_OFFSET is the load offset of the compression loader
#
IMAGE_OFFSET	:= $(shell /bin/bash -c 'printf "0x%08x" \
		     $$[$(CONFIG_PAGE_OFFSET)  + \
			$(KERNEL_MEMORY) + \
			$(CONFIG_BOOT_LINK_OFFSET)]')

LIBGCC	:= $(shell $(CC) $(KBUILD_CFLAGS) -print-libgcc-file-name)

ifeq ($(CONFIG_MCOUNT),y)
ORIG_CFLAGS := $(KBUILD_CFLAGS)
KBUILD_CFLAGS = $(subst -pg, , $(ORIG_CFLAGS))
endif

LDFLAGS_vmlinux := --oformat $(ld-bfd) -Ttext $(IMAGE_OFFSET) -e startup \
		   -T $(obj)/../../kernel/vmlinux.lds

$(obj)/vmlinux: $(OBJECTS) $(obj)/piggy.o $(LIBGCC) FORCE
	$(call if_changed,ld)
	@:

$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

vmlinux.bin.all-y := $(obj)/vmlinux.bin

$(obj)/vmlinux.bin.gz: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,gzip)
$(obj)/vmlinux.bin.bz2: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,bzip2)
$(obj)/vmlinux.bin.lzma: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,lzma)

OBJCOPYFLAGS += -R .empty_zero_page

LDFLAGS_piggy.o := -r --format binary --oformat $(ld-bfd) -T

$(obj)/piggy.o: $(obj)/vmlinux.scr $(obj)/vmlinux.bin.$(suffix-y) FORCE
	$(call if_changed,ld)
