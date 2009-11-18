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

obj-y				+=  init.o
obj-y				+= time.o
obj-y				+= clock.o
obj-y				+= pwm-clock.o
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

# devices

obj-$(CONFIG_S3C_DEV_HSMMC)	+= dev-hsmmc.o
obj-$(CONFIG_S3C_DEV_HSMMC1)	+= dev-hsmmc1.o
obj-y				+= dev-i2c0.o
obj-$(CONFIG_S3C_DEV_I2C1)	+= dev-i2c1.o
obj-$(CONFIG_S3C_DEV_FB)	+= dev-fb.o
obj-$(CONFIG_S3C_DEV_USB_HOST)	+= dev-usb.o
obj-$(CONFIG_S3C_DEV_USB_HSOTG)	+= dev-usb-hsotg.o
obj-$(CONFIG_S3C_DEV_NAND)	+= dev-nand.o
