# arch/arm/plat-s3c/Makefile
#
# Copyright 2008 Simtec Electronics
#
# Licensed under GPLv2

obj-y				:=
obj-m				:=
obj-n				:=
obj-				:=

# Core support for all Samsung SoCs

obj-y				+= init.o
obj-y				+= time.o
obj-y				+= gpio.o
obj-y				+= gpio-config.o

# DMA support

obj-$(CONFIG_S3C_DMA)		+= dma.o

# PM support

obj-$(CONFIG_PM)		+= pm.o
obj-$(CONFIG_PM)		+= pm-gpio.o
obj-$(CONFIG_S3C2410_PM_CHECK)	+= pm-check.o

# PWM support

obj-$(CONFIG_HAVE_PWM)		+= pwm.o
