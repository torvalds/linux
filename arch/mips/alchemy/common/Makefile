#
#  Copyright 2000, 2008 MontaVista Software Inc.
#  Author: MontaVista Software, Inc. <source@mvista.com>
#
# Makefile for the Alchemy Au1xx0 CPUs, generic files.
#

obj-y += prom.o time.o clocks.o platform.o power.o setup.o \
	sleeper.o dma.o dbdma.o

obj-$(CONFIG_ALCHEMY_GPIOINT_AU1000) += irq.o

# optional gpiolib support
ifeq ($(CONFIG_ALCHEMY_GPIO_INDIRECT),)
 obj-$(CONFIG_GPIOLIB) += gpiolib.o
endif
