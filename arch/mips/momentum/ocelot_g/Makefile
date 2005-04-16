#
# Makefile for Momentum Computer's Ocelot-G board.
#

obj-y	 		+= int-handler.o irq.o gt-irq.o prom.o reset.o setup.o
obj-$(CONFIG_KGDB)	+= dbg_io.o

EXTRA_AFLAGS := $(CFLAGS)
