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

# Core support for S5P64X0 system

obj-$(CONFIG_ARCH_S5P64X0)	+= cpu.o init.o clock.o dma.o gpiolib.o
obj-$(CONFIG_ARCH_S5P64X0)	+= setup-i2c0.o irq-eint.o
obj-$(CONFIG_CPU_S5P6440)	+= clock-s5p6440.o
obj-$(CONFIG_CPU_S5P6450)	+= clock-s5p6450.o

# machine support

obj-$(CONFIG_MACH_SMDK6440)	+= mach-smdk6440.o
obj-$(CONFIG_MACH_SMDK6450)	+= mach-smdk6450.o

# device support

obj-y				+= dev-audio.o
obj-$(CONFIG_S3C64XX_DEV_SPI)	+= dev-spi.o

obj-$(CONFIG_S5P64X0_SETUP_I2C1)	+= setup-i2c1.o
