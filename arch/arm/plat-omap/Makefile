# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the linux kernel.
#

ccflags-$(CONFIG_ARCH_MULTIPLATFORM) := -I$(srctree)/arch/arm/plat-omap/include

# Common support
obj-y := sram.o dma.o counter_32k.o

# omap_device support (OMAP2+ only at the moment)

obj-$(CONFIG_OMAP_DEBUG_LEDS) += debug-leds.o
