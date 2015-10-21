# arch/arm/mach-s3c64xx/Makefile
#
# Copyright 2008 Openmoko, Inc.
# Copyright 2008 Simtec Electronics
#
# Licensed under GPLv2

# Core

obj-y				+= common.o

# Core support

obj-$(CONFIG_CPU_S3C6400)	+= s3c6400.o
obj-$(CONFIG_CPU_S3C6410)	+= s3c6410.o

# PM

obj-$(CONFIG_PM)		+= pm.o
obj-$(CONFIG_PM_SLEEP)		+= irq-pm.o sleep.o
obj-$(CONFIG_CPU_IDLE)		+= cpuidle.o

# DMA support

obj-$(CONFIG_S3C64XX_PL080)	+= pl080.o

# Device support

obj-y				+= dev-uart.o
obj-y				+= dev-audio.o

# Device setup

obj-$(CONFIG_S3C64XX_SETUP_FB_24BPP)	+= setup-fb-24bpp.o
obj-$(CONFIG_S3C64XX_SETUP_I2C0)	+= setup-i2c0.o
obj-$(CONFIG_S3C64XX_SETUP_I2C1)	+= setup-i2c1.o
obj-$(CONFIG_S3C64XX_SETUP_IDE)		+= setup-ide.o
obj-$(CONFIG_S3C64XX_SETUP_KEYPAD)	+= setup-keypad.o
obj-$(CONFIG_S3C64XX_SETUP_SDHCI_GPIO)	+= setup-sdhci-gpio.o
obj-$(CONFIG_S3C64XX_SETUP_SPI)		+= setup-spi.o
obj-$(CONFIG_S3C64XX_SETUP_USB_PHY) += setup-usb-phy.o

obj-$(CONFIG_SAMSUNG_DEV_BACKLIGHT)	+= dev-backlight.o

# Machine support

obj-$(CONFIG_MACH_ANW6410)		+= mach-anw6410.o
obj-$(CONFIG_MACH_HMT)			+= mach-hmt.o
obj-$(CONFIG_MACH_MINI6410)		+= mach-mini6410.o
obj-$(CONFIG_MACH_NCP)			+= mach-ncp.o
obj-$(CONFIG_MACH_REAL6410)		+= mach-real6410.o
obj-$(CONFIG_MACH_SMARTQ)		+= mach-smartq.o
obj-$(CONFIG_MACH_SMARTQ5)		+= mach-smartq5.o
obj-$(CONFIG_MACH_SMARTQ7)		+= mach-smartq7.o
obj-$(CONFIG_MACH_SMDK6400)		+= mach-smdk6400.o
obj-$(CONFIG_MACH_SMDK6410)		+= mach-smdk6410.o
obj-$(CONFIG_MACH_WLF_CRAGG_6410)	+= mach-crag6410.o mach-crag6410-module.o
obj-$(CONFIG_MACH_S3C64XX_DT)		+= mach-s3c64xx-dt.o
