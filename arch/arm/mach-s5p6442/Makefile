# arch/arm/mach-s5p6442/Makefile
#
# Copyright (c) 2010 Samsung Electronics Co., Ltd.
# 		http://www.samsung.com/
#
# Licensed under GPLv2

obj-y				:=
obj-m				:=
obj-n				:=
obj-				:=

# Core support for S5P6442 system

obj-$(CONFIG_CPU_S5P6442)	+= cpu.o init.o clock.o dma.o
obj-$(CONFIG_CPU_S5P6442)	+= setup-i2c0.o

# machine support

obj-$(CONFIG_MACH_SMDK6442)	+= mach-smdk6442.o

# device support
obj-y				+= dev-audio.o
obj-$(CONFIG_S3C64XX_DEV_SPI)	+= dev-spi.o
