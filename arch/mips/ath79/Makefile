#
# Makefile for the Atheros AR71XX/AR724X/AR913X specific parts of the kernel
#
# Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
# Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as published
# by the Free Software Foundation.

obj-y	:= prom.o setup.o irq.o common.o clock.o gpio.o

obj-$(CONFIG_EARLY_PRINTK)		+= early_printk.o
obj-$(CONFIG_PCI)			+= pci.o

#
# Devices
#
obj-y					+= dev-common.o
obj-$(CONFIG_ATH79_DEV_GPIO_BUTTONS)	+= dev-gpio-buttons.o
obj-$(CONFIG_ATH79_DEV_LEDS_GPIO)	+= dev-leds-gpio.o
obj-$(CONFIG_ATH79_DEV_SPI)		+= dev-spi.o
obj-$(CONFIG_ATH79_DEV_USB)		+= dev-usb.o
obj-$(CONFIG_ATH79_DEV_WMAC)		+= dev-wmac.o

#
# Machines
#
obj-$(CONFIG_ATH79_MACH_AP121)		+= mach-ap121.o
obj-$(CONFIG_ATH79_MACH_AP81)		+= mach-ap81.o
obj-$(CONFIG_ATH79_MACH_DB120)		+= mach-db120.o
obj-$(CONFIG_ATH79_MACH_PB44)		+= mach-pb44.o
obj-$(CONFIG_ATH79_MACH_UBNT_XM)	+= mach-ubnt-xm.o
