#
# Makefile for Momentum's Ocelot board.
#

obj-y	 		+= int-handler.o irq.o prom.o reset.o setup.o

obj-$(CONFIG_KGDB)	+= dbg_io.o

EXTRA_AFLAGS := $(CFLAGS)
