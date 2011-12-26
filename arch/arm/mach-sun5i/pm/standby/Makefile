#makefile for standby.bin

INCLUDE   = -I. \
	-I$(KDIR)/include \
	-I$(KDIR)/arch/arm/mach-sun5i/include

OBJ = common.o standby.o stack.o standby_clock.o dram/dram.o dram/dram_init.o   \
      standby_int.o standby_ir.o standby_key.o standby_power.o  \
      standby_tmr.o standby_twi.o standby_usb.o standby_delay.o


OUPUTFILE = standby

LD_FILE = standby.xn
CC = $(CROSS_COMPILE)gcc

CFLAGS = -Os -g -march=armv7-a -fno-unwind-tables -fno-asynchronous-unwind-tables -mlittle-endian  -c

all:
	$(CC) $(INCLUDE) $(CFLAGS) common.c           -o common.o
	$(CC) $(INCLUDE) $(CFLAGS) standby.c          -o standby.o
	$(CC) $(INCLUDE) $(CFLAGS) standby_clock.c    -o standby_clock.o
	$(CC) $(INCLUDE) $(CFLAGS) standby_int.c      -o standby_int.o
	$(CC) $(INCLUDE) $(CFLAGS) standby_ir.c       -o standby_ir.o
	$(CC) $(INCLUDE) $(CFLAGS) standby_key.c      -o standby_key.o
	$(CC) $(INCLUDE) $(CFLAGS) standby_power.c    -o standby_power.o
	$(CC) $(INCLUDE) $(CFLAGS) standby_tmr.c      -o standby_tmr.o
	$(CC) $(INCLUDE) $(CFLAGS) standby_twi.c      -o standby_twi.o
	$(CC) $(INCLUDE) $(CFLAGS) standby_usb.c      -o standby_usb.o
	$(CC) $(INCLUDE) $(CFLAGS) standby_delay.S    -o standby_delay.o
	$(CC) $(INCLUDE) $(CFLAGS) dram/dram.c        -o dram/dram.o
	$(CC) $(INCLUDE) $(CFLAGS) dram/dram_init.c   -o dram/dram_init.o
	$(CC) $(INCLUDE) $(CFLAGS) stack.S            -o stack.o

	$(CROSS_COMPILE)ld -T $(LD_FILE)  -EL  $(OBJ) -o $(OUPUTFILE).elf  -Map $(OUPUTFILE).map
	$(CROSS_COMPILE)objdump -D $(OUPUTFILE).elf > $(OUPUTFILE).lst
	$(CROSS_COMPILE)objcopy -O binary $(OUPUTFILE).elf $(OUPUTFILE).bin

	rm -rf *.o ../*.o

	cp standby.bin standby.code

	@echo ----------------------------------------
	@echo well done!
clean:
	-rm *.o *.map *.lst *.bin *.elf *.exe dram/*.o

