#
# Makefile for the Cobalt micro systems family specific parts of the kernel
#

obj-y	 := irq.o int-handler.o reset.o setup.o

obj-$(CONFIG_EARLY_PRINTK)	+= console.o

EXTRA_AFLAGS := $(CFLAGS)
