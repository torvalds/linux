#
# Makefile for the Cobalt micro systems family specific parts of the kernel
#

obj-y := buttons.o irq.o led.o reset.o rtc.o serial.o setup.o

obj-$(CONFIG_PCI)		+= pci.o
obj-$(CONFIG_EARLY_PRINTK)	+= console.o
obj-$(CONFIG_MTD_PHYSMAP)	+= mtd.o

EXTRA_CFLAGS += -Werror
