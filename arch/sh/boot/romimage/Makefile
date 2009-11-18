#
# linux/arch/sh/boot/romimage/Makefile
#
# create an image suitable for burning to flash from zImage
#

targets		:= vmlinux head.o

OBJECTS = $(obj)/head.o
LDFLAGS_vmlinux := --oformat $(ld-bfd) -Ttext 0 -e romstart

$(obj)/vmlinux: $(OBJECTS) $(obj)/piggy.o FORCE
	$(call if_changed,ld)
	@:

LDFLAGS_piggy.o := -r --format binary --oformat $(ld-bfd) -T

$(obj)/piggy.o: $(obj)/vmlinux.scr arch/sh/boot/zImage FORCE
	$(call if_changed,ld)
