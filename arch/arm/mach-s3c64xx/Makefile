# arch/arm/mach-s3c64xx/Makefile
#
# Copyright 2008 Openmoko, Inc.
# Copyright 2008 Simtec Electronics
#
# Licensed under GPLv2

obj-y				:=
obj-m				:=
obj-n				:=
obj-				:=

# Core files
obj-y				+= cpu.o
obj-y				+= clock.o
obj-y				+= gpiolib.o

# Core support for S3C6400 system

obj-$(CONFIG_CPU_S3C6400)	+= s3c6400.o
obj-$(CONFIG_CPU_S3C6410)	+= s3c6410.o

obj-y				+= irq.o
obj-y				+= irq-eint.o

# CPU frequency scaling

obj-$(CONFIG_CPU_FREQ_S3C64XX)  += cpufreq.o

# DMA support

obj-$(CONFIG_S3C64XX_DMA)	+= dma.o

# Device setup

obj-$(CONFIG_S3C64XX_SETUP_I2C0) += setup-i2c0.o
obj-$(CONFIG_S3C64XX_SETUP_I2C1) += setup-i2c1.o
obj-$(CONFIG_S3C64XX_SETUP_IDE) += setup-ide.o
obj-$(CONFIG_S3C64XX_SETUP_KEYPAD) += setup-keypad.o
obj-$(CONFIG_S3C64XX_SETUP_SDHCI) += setup-sdhci.o
obj-$(CONFIG_S3C64XX_SETUP_FB_24BPP) += setup-fb-24bpp.o
obj-$(CONFIG_S3C64XX_SETUP_SDHCI_GPIO) += setup-sdhci-gpio.o

# PM

obj-$(CONFIG_PM)		+= pm.o
obj-$(CONFIG_PM)		+= sleep.o
obj-$(CONFIG_PM)		+= irq-pm.o

# Machine support

obj-$(CONFIG_MACH_ANW6410)	+= mach-anw6410.o
obj-$(CONFIG_MACH_SMDK6400)	+= mach-smdk6400.o
obj-$(CONFIG_MACH_SMDK6410)	+= mach-smdk6410.o
obj-$(CONFIG_MACH_REAL6410)     += mach-real6410.o
obj-$(CONFIG_MACH_NCP)		+= mach-ncp.o
obj-$(CONFIG_MACH_HMT)		+= mach-hmt.o
obj-$(CONFIG_MACH_SMARTQ)	+= mach-smartq.o
obj-$(CONFIG_MACH_SMARTQ5)	+= mach-smartq5.o
obj-$(CONFIG_MACH_SMARTQ7)	+= mach-smartq7.o

# device support

obj-y				+= dev-uart.o
obj-y				+= dev-audio.o
obj-$(CONFIG_S3C64XX_DEV_SPI)	+= dev-spi.o
obj-$(CONFIG_S3C64XX_DEV_TS)	+= dev-ts.o
obj-$(CONFIG_S3C64XX_DEV_ONENAND1)	+= dev-onenand1.o
