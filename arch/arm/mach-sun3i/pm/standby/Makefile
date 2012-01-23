#makefile for standby.bin
LINUX_ROOT = ./../../../../..
LINUX_INC = $(LINUX_ROOT)/include/linux
ARCH_INC = $(LINUX_ROOT)/arch/arm/mach-sun3i/include
SRCDIR = .

INCLUDE   = -I$(SRCDIR)\
		-I$(LINUX_INC)/power\
		-I$(ARCH_INC)

INPUTFILE = sun3i_sram_main.c sun3i_sram_entry.c sun3i_sram_serial.c sun3i_sram_twi.c
OBJ = sun3i_sram_main.o sun3i_sram_entry.o sun3i_sram_serial.o sun3i_sram_twi.o
OUPUTFILE = standby

CROSS_COMPILE = arm-none-linux-gnueabi-
LD_FILE = standby.xn
CFLAGS = -Os   -g  -fno-unwind-tables -fno-asynchronous-unwind-tables -mlittle-endian  -c

all:$(INPUTFILE)
	$(CROSS_COMPILE)gcc $(INCLUDE) $(CFLAGS) sun3i_sram_serial.c  -o sun3i_sram_serial.o
	$(CROSS_COMPILE)gcc $(INCLUDE) $(CFLAGS) sun3i_sram_main.c  -o sun3i_sram_main.o
	$(CROSS_COMPILE)gcc $(INCLUDE) $(CFLAGS) sun3i_sram_entry.c  -o sun3i_sram_entry.o
	$(CROSS_COMPILE)gcc $(INCLUDE) $(CFLAGS) sun3i_sram_twi.c  -o sun3i_sram_twi.o

	$(CROSS_COMPILE)ld -T $(LD_FILE)  -EL  $(OBJ) -o $(OUPUTFILE).exe  -Map $(OUPUTFILE).map
	$(CROSS_COMPILE)objdump -D $(OUPUTFILE).exe > $(OUPUTFILE).lst
	$(CROSS_COMPILE)objcopy -O binary $(OUPUTFILE).exe $(OUPUTFILE).bin

	-rm *.o

	@echo ----------------------------------------
	@echo well done!
clean:
	-rm *.o *.exe *.map *.lst

