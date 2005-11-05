#
#  Copyright 2000 MontaVista Software Inc.
#  Author: MontaVista Software, Inc.
#     	ppopov@mvista.com or source@mvista.com
#
# Makefile for the Alchemy Au1000 CPU, generic files.
#

obj-y += prom.o int-handler.o irq.o puts.o time.o reset.o \
	au1xxx_irqmap.o clocks.o platform.o power.o setup.o \
	sleeper.o cputable.o dma.o dbdma.o gpio.o

obj-$(CONFIG_AU1X00_USB_DEVICE)	+= usbdev.o
obj-$(CONFIG_KGDB)		+= dbg_io.o
obj-$(CONFIG_PCI)		+= pci.o
