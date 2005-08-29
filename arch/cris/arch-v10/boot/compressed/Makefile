#
# create a compressed vmlinuz image from the binary vmlinux.bin file
#
target = $(target_compressed_dir)
src    = $(src_compressed_dir)

CC = gcc-cris -melf $(LINUXINCLUDE)
CFLAGS = -O2
LD = ld-cris
OBJCOPY = objcopy-cris
OBJCOPYFLAGS = -O binary --remove-section=.bss
OBJECTS = $(target)/head.o $(target)/misc.o

# files to compress
SYSTEM = $(objtree)/vmlinux.bin

all: $(target_compressed_dir)/vmlinuz

$(target)/decompress.bin: $(OBJECTS)
	$(LD) -T $(src)/decompress.ld -o $(target)/decompress.o $(OBJECTS)
	$(OBJCOPY) $(OBJCOPYFLAGS) $(target)/decompress.o $(target)/decompress.bin

# Create vmlinuz image in top-level build directory
$(target_compressed_dir)/vmlinuz: $(target) piggy.img $(target)/decompress.bin
	@echo "  COMPR   vmlinux.bin --> vmlinuz"
	@cat $(target)/decompress.bin piggy.img > $(target_compressed_dir)/vmlinuz
	@rm -f piggy.img

$(target)/head.o: $(src)/head.S
	$(CC) -D__ASSEMBLY__ -traditional -c $< -o $@

$(target)/misc.o: $(src)/misc.c
	$(CC) -D__KERNEL__ -c $< -o $@

# gzip the kernel image

piggy.img: $(SYSTEM)
	@cat $(SYSTEM) | gzip -f -9 > piggy.img

$(target):
	mkdir -p $(target)

clean:
	rm -f piggy.img $(objtree)/vmlinuz

