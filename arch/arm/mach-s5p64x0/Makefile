# arch/arm/mach-s5p64x0/Makefile
#
# Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
# 		http://www.samsung.com
#
# Licensed under GPLv2

obj-y				:=
obj-m				:=
obj-n				:=
obj-				:=

# Core

obj-y				+= common.o clock.o
obj-$(CONFIG_CPU_S5P6440)	+= clock-s5p6440.o
obj-$(CONFIG_CPU_S5P6450)	+= clock-s5p6450.o

obj-$(CONFIG_PM)		+= pm.o irq-pm.o

obj-y				+= dma.o

# machine support

obj-$(CONFIG_MACH_SMDK6440)	+= mach-smdk6440.o
obj-$(CONFIG_MACH_SMDK6450)	+= mach-smdk6450.o

# device support

obj-y				+= dev-audio.o

obj-y					+= setup-i2c0.o
obj-$(CONFIG_S5P64X0_SETUP_I2C1)	+= setup-i2c1.o
obj-$(CONFIG_S5P64X0_SETUP_FB_24BPP)	+= setup-fb-24bpp.o
obj-$(CONFIG_S5P64X0_SETUP_SPI)		+= setup-spi.o
obj-$(CONFIG_S5P64X0_SETUP_SDHCI_GPIO)	+= setup-sdhci-gpio.o
