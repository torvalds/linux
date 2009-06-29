#
#  Copyright 2000, 2008 MontaVista Software Inc.
#  Author: MontaVista Software, Inc. <source@mvista.com>
#
# Makefile for the Alchemy Au1xx0 CPUs, generic files.
#

obj-y += prom.o irq.o puts.o time.o reset.o \
	clocks.o platform.o power.o setup.o \
	sleeper.o dma.o dbdma.o

# optional gpiolib support
ifeq ($(CONFIG_ALCHEMY_GPIO_INDIRECT),)
 ifeq ($(CONFIG_GPIOLIB),y)
  obj-$(CONFIG_ALCHEMY_GPIO_AU1000) += gpiolib-au1000.o
 endif
endif

obj-$(CONFIG_PCI)		+= pci.o

EXTRA_CFLAGS += -Werror
