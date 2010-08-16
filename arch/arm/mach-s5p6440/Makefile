# arch/arm/mach-s5p6440/Makefile
#
# Copyright (c) 2009 Samsung Electronics Co., Ltd.
# 		http://www.samsung.com/
#
# Licensed under GPLv2

obj-y				:=
obj-m				:=
obj-n				:=
obj-				:=

# Core support for S5P6440 system

obj-$(CONFIG_CPU_S5P6440)	+= cpu.o init.o clock.o gpio.o dma.o
obj-$(CONFIG_CPU_S5P6440)	+= setup-i2c0.o

# machine support

obj-$(CONFIG_MACH_SMDK6440)	+= mach-smdk6440.o

# device support
obj-y				+= dev-audio.o
obj-$(CONFIG_S3C64XX_DEV_SPI)	+= dev-spi.o
obj-$(CONFIG_S5P6440_SETUP_I2C1)	+= setup-i2c1.o
