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

obj-y				+= init.o
obj-y				+= time.o
obj-y				+= clock.o
obj-y				+= pwm-clock.o
obj-y				+= gpio.o
obj-y				+= gpio-config.o

obj-$(CONFIG_SAMSUNG_GPIOLIB_4BIT)	+= gpiolib.o
obj-$(CONFIG_SAMSUNG_CLKSRC)	+= clock-clksrc.o

obj-$(CONFIG_SAMSUNG_IRQ_UART)	+= irq-uart.o
obj-$(CONFIG_SAMSUNG_IRQ_VIC_TIMER) += irq-vic-timer.o

# ADC

obj-$(CONFIG_S3C_ADC)	+= adc.o

# devices

obj-$(CONFIG_S3C_DEV_HSMMC)	+= dev-hsmmc.o
obj-$(CONFIG_S3C_DEV_HSMMC1)	+= dev-hsmmc1.o
obj-$(CONFIG_S3C_DEV_HSMMC2)	+= dev-hsmmc2.o
obj-y				+= dev-i2c0.o
obj-$(CONFIG_S3C_DEV_I2C1)	+= dev-i2c1.o
obj-$(CONFIG_S3C_DEV_FB)	+= dev-fb.o
obj-y				+= dev-uart.o
obj-$(CONFIG_S3C_DEV_USB_HOST)	+= dev-usb.o
obj-$(CONFIG_S3C_DEV_USB_HSOTG)	+= dev-usb-hsotg.o
obj-$(CONFIG_S3C_DEV_NAND)	+= dev-nand.o

# DMA support

obj-$(CONFIG_S3C_DMA)		+= dma.o

# PM support

obj-$(CONFIG_PM)		+= pm.o
obj-$(CONFIG_PM)		+= pm-gpio.o
obj-$(CONFIG_SAMSUNG_PM_CHECK)	+= pm-check.o

# PWM support

obj-$(CONFIG_HAVE_PWM)		+= pwm.o
