#makefile for standby test
LINUX_ROOT = ./../../../../..
LINUX_INC = $(LINUX_ROOT)/include/linux
ARCH_INC = $(LINUX_ROOT)/arch/arm/mach-sun3i/include
SRCDIR = .

INCLUDE   = -I$(SRCDIR)\
		-I$(LINUX_INC)/power\
		-I$(ARCH_INC)

INPUTFILE = pm_test.c
OBJ = pm_test.o
OUPUTFILE = pm_test

CROSS_COMPILE = arm-none-linux-gnueabi-
#LD_FILE = standby.xn
CFLAGS = -Os   -g  -fno-unwind-tables -fno-asynchronous-unwind-tables -mlittle-endian  -static

all:$(INPUTFILE)
	$(CROSS_COMPILE)gcc $(INCLUDE) $(CFLAGS) pm_test.c  -o $(OUPUTFILE).app

#	$(CROSS_COMPILE)objdump -D $(OUPUTFILE).app > $(OUPUTFILE).lst
#	$(CROSS_COMPILE)objcopy -O binary $(OUPUTFILE).app $(OUPUTFILE).bin

	@echo ----------------------------------------
	@echo well done!
clean:
	-rm *.o *.exe *.map *.lst

