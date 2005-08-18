#
# lx25/arch/cris/arch-v32/boot/compressed/Makefile
#
# create a compressed vmlinux image from the original vmlinux files and romfs
#

target = $(target_compressed_dir)
src    = $(src_compressed_dir)

CC = gcc-cris -mlinux -march=v32 -I $(TOPDIR)/include
CFLAGS = -O2
LD = gcc-cris -mlinux -march=v32 -nostdlib
OBJCOPY = objcopy-cris
OBJCOPYFLAGS = -O binary --remove-section=.bss
OBJECTS = $(target)/head.o $(target)/misc.o

# files to compress
SYSTEM = $(objtree)/vmlinux.bin

all: vmlinuz

$(target)/decompress.bin: $(OBJECTS)
	$(LD) -T $(src)/decompress.ld -o $(target)/decompress.o $(OBJECTS)
	$(OBJCOPY) $(OBJCOPYFLAGS) $(target)/decompress.o $(target)/decompress.bin

$(objtree)/vmlinuz: $(target) piggy.img $(target)/decompress.bin
	cat $(target)/decompress.bin piggy.img > $(objtree)/vmlinuz
	rm -f piggy.img
	cp $(objtree)/vmlinuz $(src)

$(target)/head.o: $(src)/head.S
	$(CC) -D__ASSEMBLY__ -c $< -o $@

# gzip the kernel image

piggy.img: $(SYSTEM)
	cat $(SYSTEM) | gzip -f -9 > piggy.img

clean:
	rm -f piggy.img $(objtree)/vmlinuz vmlinuz.o decompress.o decompress.bin $(OBJECTS)

