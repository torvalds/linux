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

# Core support for S5PV210 system

obj-$(CONFIG_CPU_S5PV210)	+= cpu.o init.o clock.o dma.o
obj-$(CONFIG_CPU_S5PV210)	+= setup-i2c0.o
obj-$(CONFIG_S5PV210_PM)	+= pm.o sleep.o

# machine support

obj-$(CONFIG_MACH_AQUILA)	+= mach-aquila.o
obj-$(CONFIG_MACH_SMDKV210)	+= mach-smdkv210.o
obj-$(CONFIG_MACH_SMDKC110)	+= mach-smdkc110.o
obj-$(CONFIG_MACH_GONI)		+= mach-goni.o
obj-$(CONFIG_MACH_TORBRECK)	+= mach-torbreck.o

# device support

obj-y				+= dev-audio.o
obj-$(CONFIG_S3C64XX_DEV_SPI)	+= dev-spi.o

obj-$(CONFIG_S5PV210_SETUP_FB_24BPP)	+= setup-fb-24bpp.o
obj-$(CONFIG_S5PV210_SETUP_FIMC)	+= setup-fimc.o
obj-$(CONFIG_S5PV210_SETUP_I2C1) 	+= setup-i2c1.o
obj-$(CONFIG_S5PV210_SETUP_I2C2) 	+= setup-i2c2.o
obj-$(CONFIG_S5PV210_SETUP_IDE)		+= setup-ide.o
obj-$(CONFIG_S5PV210_SETUP_KEYPAD)	+= setup-keypad.o
obj-$(CONFIG_S5PV210_SETUP_SDHCI)       += setup-sdhci.o
obj-$(CONFIG_S5PV210_SETUP_SDHCI_GPIO)	+= setup-sdhci-gpio.o
