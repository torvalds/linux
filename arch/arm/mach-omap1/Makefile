# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

# Common support
obj-y := io.o id.o sram-init.o sram.o time.o irq.o mux.o flash.o \
	 serial.o devices.o dma.o omap-dma.o fb.o
obj-y += clock.o clock_data.o opp_data.o reset.o pm_bus.o timer.o

ifneq ($(CONFIG_SND_SOC_OMAP_MCBSP),)
obj-y += mcbsp.o
endif

obj-$(CONFIG_OMAP_32K_TIMER)	+= timer32k.o

# OCPI interconnect support for 1710, 1610 and 5912
obj-$(CONFIG_ARCH_OMAP16XX) += ocpi.o

# Power Management
obj-$(CONFIG_PM) += pm.o sleep.o

i2c-omap-$(CONFIG_I2C_OMAP)		:= i2c.o
obj-y					+= $(i2c-omap-m) $(i2c-omap-y)

led-y := leds.o

usb-fs-$(CONFIG_USB_SUPPORT)		:= usb.o
obj-y					+= $(usb-fs-m) $(usb-fs-y)

# Specific board support
obj-$(CONFIG_MACH_OMAP_OSK)		+= board-osk.o
obj-$(CONFIG_MACH_OMAP_PALMTE)		+= board-palmte.o
obj-$(CONFIG_MACH_NOKIA770)		+= board-nokia770.o
obj-$(CONFIG_MACH_AMS_DELTA)		+= board-ams-delta.o ams-delta-fiq.o \
					   ams-delta-fiq-handler.o
obj-$(CONFIG_MACH_SX1)			+= board-sx1.o board-sx1-mmc.o

# GPIO
obj-$(CONFIG_ARCH_OMAP15XX)		+= gpio15xx.o
obj-$(CONFIG_ARCH_OMAP16XX)		+= gpio16xx.o
