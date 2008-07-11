#
#  Copyright 2000, 2008 MontaVista Software Inc.
#  Author: MontaVista Software, Inc. <source@mvista.com>
#
# Makefile for the Alchemy Au1xx0 CPUs, generic files.
#

obj-y += prom.o irq.o puts.o time.o reset.o \
	au1xxx_irqmap.o clocks.o platform.o power.o setup.o \
	sleeper.o cputable.o dma.o dbdma.o gpio.o

obj-$(CONFIG_KGDB)		+= dbg_io.o
obj-$(CONFIG_PCI)		+= pci.o

EXTRA_CFLAGS += -Werror
