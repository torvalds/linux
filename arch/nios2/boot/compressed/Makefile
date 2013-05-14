#
# linux/arch/nios2/boot/compressed/Makefile
#
# create a compressed vmlinux image from the original vmlinux
#

targets		:= vmlinux head.o misc.o piggy.o vmlinux.lds
asflags-y	:=

OBJECTS = $(obj)/head.o $(obj)/misc.o

LDFLAGS_vmlinux := -T

$(obj)/vmlinux: $(obj)/vmlinux.lds $(OBJECTS) $(obj)/piggy.o FORCE
	$(call if_changed,ld)
	@:

LDFLAGS_piggy.o := -r --format binary --oformat elf32-littlenios2 -T

$(obj)/piggy.o: $(obj)/vmlinux.scr $(obj)/../vmlinux.gz FORCE
	$(call if_changed,ld)
