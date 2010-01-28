#
# linux/arch/sh/boot/romimage/Makefile
#
# create an image suitable for burning to flash from zImage
#

targets		:= vmlinux head.o zeropage.bin piggy.o

OBJECTS = $(obj)/head.o
LDFLAGS_vmlinux := --oformat $(ld-bfd) -Ttext 0 -e romstart \
		   -T $(obj)/../../kernel/vmlinux.lds

$(obj)/vmlinux: $(OBJECTS) $(obj)/piggy.o FORCE
	$(call if_changed,ld)
	@:

OBJCOPYFLAGS += -j .empty_zero_page

$(obj)/zeropage.bin: vmlinux FORCE
	$(call if_changed,objcopy)

LDFLAGS_piggy.o := -r --format binary --oformat $(ld-bfd) -T

$(obj)/piggy.o: $(obj)/vmlinux.scr $(obj)/zeropage.bin arch/sh/boot/zImage FORCE
	$(call if_changed,ld)
