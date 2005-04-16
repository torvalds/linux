#
# linux/arch/sh/boot/compressed/Makefile
#
# create a compressed vmlinux image from the original vmlinux
#

targets		:= vmlinux vmlinux.bin vmlinux.bin.gz head.o misc.o \
		   piggy.o vmlinux.lds
EXTRA_AFLAGS	:= -traditional

OBJECTS = $(obj)/head.o $(obj)/misc.o

#
# IMAGE_OFFSET is the load offset of the compression loader
#
#IMAGE_OFFSET := $(shell printf "0x%08x" $$[$(CONFIG_MEMORY_START)+0x2000])
#IMAGE_OFFSET := $(shell printf "0x%08x" $$[$(CONFIG_MEMORY_START)+0x00400000])

LDFLAGS_vmlinux := -T

$(obj)/vmlinux: $(obj)/vmlinux.lds $(OBJECTS) $(obj)/piggy.o FORCE
	$(call if_changed,ld)
	@:

$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/vmlinux.bin.gz: $(obj)/vmlinux.bin FORCE
	$(call if_changed,gzip)

CFLAGS_misc.o += -fpic

ifdef CONFIG_MMU
LDFLAGS_piggy.o := -r --format binary --oformat elf32-m32r-linux -T
else
LDFLAGS_piggy.o := -r --format binary --oformat elf32-m32r -T
endif

OBJCOPYFLAGS += -R .empty_zero_page

$(obj)/piggy.o: $(obj)/vmlinux.scr $(obj)/vmlinux.bin.gz FORCE
	$(call if_changed,ld)
