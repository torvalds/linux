# arch/arm/mach-s3c24xx/Makefile
#
# Copyright (c) 2012 Samsung Electronics Co., Ltd.
#		http://www.samsung.com/
#
# Copyright 2007 Simtec Electronics
#
# Licensed under GPLv2

obj-y				:=
obj-m				:=
obj-n				:=
obj-				:=

# core

obj-$(CONFIG_CPU_S3C2410)	+= s3c2410.o
obj-$(CONFIG_S3C2410_DMA)	+= dma-s3c2410.o
obj-$(CONFIG_S3C2410_PM)	+= pm-s3c2410.o sleep-s3c2410.o

obj-$(CONFIG_CPU_S3C2412)	+= s3c2412.o irq-s3c2412.o clock-s3c2412.o
obj-$(CONFIG_S3C2412_DMA)	+= dma-s3c2412.o
obj-$(CONFIG_S3C2412_PM)	+= pm-s3c2412.o
obj-$(CONFIG_S3C2412_PM_SLEEP)	+= sleep-s3c2412.o

obj-$(CONFIG_CPU_S3C2416)	+= s3c2416.o irq-s3c2416.o clock-s3c2416.o
obj-$(CONFIG_S3C2416_PM)	+= pm-s3c2416.o

obj-$(CONFIG_CPU_S3C2440)	+= s3c2440.o irq-s3c2440.o clock-s3c2440.o
obj-$(CONFIG_CPU_S3C2442)	+= s3c2442.o
obj-$(CONFIG_CPU_S3C244X)	+= s3c244x.o irq-s3c244x.o clock-s3c244x.o
obj-$(CONFIG_S3C2440_DMA)	+= dma-s3c2440.o

obj-$(CONFIG_CPU_S3C2443)	+= s3c2443.o irq-s3c2443.o clock-s3c2443.o

# common code

obj-$(CONFIG_S3C2443_COMMON)	+= common-s3c2443.o
obj-$(CONFIG_S3C2443_DMA)	+= dma-s3c2443.o

#
# machine support
# following is ordered alphabetically by option text.
#

obj-$(CONFIG_MACH_AML_M5900)		+= mach-amlm5900.o
obj-$(CONFIG_ARCH_BAST)			+= mach-bast.o
obj-$(CONFIG_BAST_PC104_IRQ)		+= bast-irq.o
obj-$(CONFIG_ARCH_H1940)		+= mach-h1940.o
obj-$(CONFIG_H1940BT)			+= h1940-bluetooth.o
obj-$(CONFIG_PM_H1940)			+= pm-h1940.o
obj-$(CONFIG_MACH_N30)			+= mach-n30.o
obj-$(CONFIG_MACH_OTOM)			+= mach-otom.o
obj-$(CONFIG_MACH_QT2410)		+= mach-qt2410.o
obj-$(CONFIG_ARCH_SMDK2410)		+= mach-smdk2410.o
obj-$(CONFIG_MACH_TCT_HAMMER)		+= mach-tct_hammer.o
obj-$(CONFIG_MACH_VR1000)		+= mach-vr1000.o

obj-$(CONFIG_MACH_JIVE)			+= mach-jive.o
obj-$(CONFIG_MACH_SMDK2413)		+= mach-smdk2413.o
obj-$(CONFIG_MACH_VSTMS)		+= mach-vstms.o

obj-$(CONFIG_MACH_SMDK2416)		+= mach-smdk2416.o

obj-$(CONFIG_MACH_ANUBIS)		+= mach-anubis.o
obj-$(CONFIG_MACH_AT2440EVB)		+= mach-at2440evb.o
obj-$(CONFIG_MACH_MINI2440)		+= mach-mini2440.o
obj-$(CONFIG_MACH_NEXCODER_2440)	+= mach-nexcoder.o
obj-$(CONFIG_MACH_OSIRIS)		+= mach-osiris.o
obj-$(CONFIG_MACH_RX3715)		+= mach-rx3715.o
obj-$(CONFIG_ARCH_S3C2440)		+= mach-smdk2440.o

obj-$(CONFIG_MACH_NEO1973_GTA02)	+= mach-gta02.o
obj-$(CONFIG_MACH_RX1950)		+= mach-rx1950.o

obj-$(CONFIG_MACH_SMDK2443)		+= mach-smdk2443.o

# common bits of machine support

obj-$(CONFIG_S3C24XX_SMDK)		+= common-smdk.o
obj-$(CONFIG_S3C24XX_SIMTEC_AUDIO)	+= simtec-audio.o
obj-$(CONFIG_S3C24XX_SIMTEC_NOR)	+= simtec-nor.o
obj-$(CONFIG_S3C24XX_SIMTEC_PM)		+= simtec-pm.o
obj-$(CONFIG_S3C24XX_SIMTEC_USB)	+= simtec-usb.o

# machine additions

obj-$(CONFIG_MACH_BAST_IDE)		+= bast-ide.o
obj-$(CONFIG_MACH_OSIRIS_DVS)		+= mach-osiris-dvs.o

# device setup

obj-$(CONFIG_S3C2416_SETUP_SDHCI_GPIO)	+= setup-sdhci-gpio.o
obj-$(CONFIG_ARCH_S3C24XX)		+= setup-i2c.o
obj-$(CONFIG_S3C24XX_SETUP_TS)		+= setup-ts.o
