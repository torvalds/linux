#
# linux/arch/x86_64/boot/compressed/Makefile
#
# create a compressed vmlinux image from the original vmlinux
#
# Note all the files here are compiled/linked as 32bit executables.
#

targets		:= vmlinux vmlinux.bin vmlinux.bin.gz head.o misc.o piggy.o
EXTRA_AFLAGS	:= -traditional

# cannot use EXTRA_CFLAGS because base CFLAGS contains -mkernel which conflicts with
# -m32
CFLAGS := -m64 -D__KERNEL__ -Iinclude -O2  -fno-strict-aliasing -fPIC -mcmodel=small -fno-builtin
LDFLAGS := -m elf_x86_64

LDFLAGS_vmlinux := -T
$(obj)/vmlinux: $(src)/vmlinux.lds $(obj)/head.o $(obj)/misc.o $(obj)/piggy.o FORCE
	$(call if_changed,ld)
	@:

$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/vmlinux.bin.gz: $(obj)/vmlinux.bin FORCE
	$(call if_changed,gzip)

LDFLAGS_piggy.o := -r --format binary --oformat elf64-x86-64 -T

$(obj)/piggy.o: $(obj)/vmlinux.scr $(obj)/vmlinux.bin.gz FORCE
	$(call if_changed,ld)
