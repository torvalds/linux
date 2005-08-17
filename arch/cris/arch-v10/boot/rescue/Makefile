#
# Makefile for rescue code
#
target = $(target_rescue_dir)
src    = $(src_rescue_dir)

CC = gcc-cris -mlinux $(LINUXINCLUDE)
CFLAGS = -O2
LD = gcc-cris -mlinux -nostdlib
OBJCOPY = objcopy-cris
OBJCOPYFLAGS = -O binary --remove-section=.bss

all: $(target)/rescue.bin $(target)/testrescue.bin $(target)/kimagerescue.bin

$(target)/rescue.bin: $(target) $(target)/head.o
	$(LD) -T $(src)/rescue.ld -o $(target)/rescue.o $(target)/head.o
	$(OBJCOPY) $(OBJCOPYFLAGS) $(target)/rescue.o $(target)/rescue.bin
# Place a copy in top-level build directory
	cp -p $(target)/rescue.bin $(objtree)

$(target)/testrescue.bin: $(target) $(target)/testrescue.o
	$(OBJCOPY) $(OBJCOPYFLAGS) $(target)/testrescue.o tr.bin
# Pad it to 784 bytes
	dd if=/dev/zero of=tmp2423 bs=1 count=784
	cat tr.bin tmp2423 >testrescue_tmp.bin
	dd if=testrescue_tmp.bin of=$(target)/testrescue.bin bs=1 count=784
	rm tr.bin tmp2423 testrescue_tmp.bin

$(target)/kimagerescue.bin: $(target) $(target)/kimagerescue.o
	$(OBJCOPY) $(OBJCOPYFLAGS) $(target)/kimagerescue.o ktr.bin
# Pad it to 784 bytes, that's what the rescue loader expects
	dd if=/dev/zero of=tmp2423 bs=1 count=784
	cat ktr.bin tmp2423 >kimagerescue_tmp.bin
	dd if=kimagerescue_tmp.bin of=$(target)/kimagerescue.bin bs=1 count=784
	rm ktr.bin tmp2423 kimagerescue_tmp.bin

$(target):
	mkdir -p $(target)

$(target)/head.o: $(src)/head.S
	$(CC) -D__ASSEMBLY__ -traditional -c $< -o $*.o

$(target)/testrescue.o: $(src)/testrescue.S
	$(CC) -D__ASSEMBLY__ -traditional -c $< -o $*.o

$(target)/kimagerescue.o: $(src)/kimagerescue.S
	$(CC) -D__ASSEMBLY__ -traditional -c $< -o $*.o

clean:
	rm -f $(target)/*.o $(target)/*.bin

fastdep:

modules:

modules-install:
