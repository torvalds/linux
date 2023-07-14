# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#
obj-y			:= core.o clock.o timer-ep93xx.o

obj-$(CONFIG_EP93XX_DMA)	+= dma.o

obj-$(CONFIG_MACH_EDB93XX)	+= edb93xx.o
obj-$(CONFIG_MACH_TS72XX)	+= ts72xx.o
obj-$(CONFIG_MACH_VISION_EP9307)+= vision_ep9307.o
