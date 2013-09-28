# arch/arm/mach-s5pv210/Makefile
#
# Copyright (c) 2010 Samsung Electronics Co., Ltd.
# 		http://www.samsung.com/
#
# Licensed under GPLv2

obj-y				:=
obj-m				:=
obj-n				:=
obj-				:=

# Core

obj-y				+= common.o
obj-$(CONFIG_PM)		+= pm.o

obj-y				+= dma.o

# machine support

obj-$(CONFIG_MACH_AQUILA)	+= mach-aquila.o
obj-$(CONFIG_MACH_GONI)		+= mach-goni.o
obj-$(CONFIG_MACH_SMDKC110)	+= mach-smdkc110.o
obj-$(CONFIG_MACH_SMDKV210)	+= mach-smdkv210.o
obj-$(CONFIG_MACH_TORBRECK)	+= mach-torbreck.o
obj-$(CONFIG_MACH_S5PV210_DT)	+= s5pv210.o
# device support

obj-y				+= dev-audio.o

obj-y					+= setup-i2c0.o
obj-$(CONFIG_S5PV210_SETUP_FB_24BPP)	+= setup-fb-24bpp.o
obj-$(CONFIG_S5PV210_SETUP_FIMC)	+= setup-fimc.o
obj-$(CONFIG_S5PV210_SETUP_I2C1)	+= setup-i2c1.o
obj-$(CONFIG_S5PV210_SETUP_I2C2)	+= setup-i2c2.o
obj-$(CONFIG_S5PV210_SETUP_IDE)		+= setup-ide.o
obj-$(CONFIG_S5PV210_SETUP_KEYPAD)	+= setup-keypad.o
obj-$(CONFIG_S5PV210_SETUP_SDHCI_GPIO)	+= setup-sdhci-gpio.o
obj-$(CONFIG_S5PV210_SETUP_SPI)		+= setup-spi.o
obj-$(CONFIG_S5PV210_SETUP_USB_PHY) += setup-usb-phy.o
