#
# Makefile for rescue code
#
target = $(target_rescue_dir)
src    = $(src_rescue_dir)

CC = gcc-cris -mlinux -march=v32 $(LINUXINCLUDE)
CFLAGS = -O2
LD = gcc-cris -mlinux -march=v32 -nostdlib
OBJCOPY = objcopy-cris
OBJCOPYFLAGS = -O binary --remove-section=.bss

all: $(target)/rescue.bin

rescue: rescue.bin
	# do nothing

$(target)/rescue.bin: $(target) $(target)/head.o
	$(LD) -T $(src)/rescue.ld -o $(target)/rescue.o $(target)/head.o
	$(OBJCOPY) $(OBJCOPYFLAGS) $(target)/rescue.o $(target)/rescue.bin
	cp -p $(target)/rescue.bin $(objtree)

$(target):
	mkdir -p $(target)

$(target)/head.o: $(src)/head.S
	$(CC) -D__ASSEMBLY__ -c $< -o $*.o

clean:
	rm -f $(target)/*.o $(target)/*.bin

fastdep:

modules:

modules-install:
