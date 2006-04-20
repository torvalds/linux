#
# Makefile for TOSHIBA JMR-TX3927 board
#

obj-y	 			+= init.o irq.o setup.o
obj-$(CONFIG_RUNTIME_DEBUG) 	+= debug.o
obj-$(CONFIG_KGDB)		+= kgdb_io.o

EXTRA_AFLAGS := $(CFLAGS)
