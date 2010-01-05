# arch/arm/plat-s3c64xx/Makefile
#
# Copyright 2009 Simtec Electronics
#
# Licensed under GPLv2

obj-y				:=
obj-m				:=
obj-n				:= dummy.o
obj-				:=

# Objects we always build independent of SoC choice

obj-y				+= clock.o
obj-y				+= pwm-clock.o

obj-$(CONFIG_SAMSUNG_CLKSRC)	+= clock-clksrc.o

# devices

obj-$(CONFIG_S3C_DEV_HSMMC)	+= dev-hsmmc.o
obj-$(CONFIG_S3C_DEV_HSMMC1)	+= dev-hsmmc1.o
obj-$(CONFIG_S3C_DEV_HSMMC2)	+= dev-hsmmc2.o
obj-y				+= dev-i2c0.o
obj-$(CONFIG_S3C_DEV_I2C1)	+= dev-i2c1.o
obj-$(CONFIG_S3C_DEV_FB)	+= dev-fb.o
obj-$(CONFIG_S3C_DEV_USB_HOST)	+= dev-usb.o
obj-$(CONFIG_S3C_DEV_USB_HSOTG)	+= dev-usb-hsotg.o
obj-$(CONFIG_S3C_DEV_NAND)	+= dev-nand.o
