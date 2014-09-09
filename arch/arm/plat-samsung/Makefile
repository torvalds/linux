# arch/arm/plat-samsung/Makefile
#
# Copyright 2009 Simtec Electronics
#
# Licensed under GPLv2

ccflags-$(CONFIG_ARCH_MULTI_V7) += -I$(srctree)/$(src)/include

obj-y				:=
obj-m				:=
obj-				:=

# Objects we always build independent of SoC choice

obj-y				+= init.o cpu.o

# ADC

obj-$(CONFIG_S3C_ADC)	+= adc.o

# devices

obj-$(CONFIG_SAMSUNG_ATAGS)	+= platformdata.o

obj-$(CONFIG_SAMSUNG_ATAGS)	+= devs.o
obj-$(CONFIG_SAMSUNG_ATAGS)	+= dev-uart.o
obj-$(CONFIG_S5P_DEV_MFC)	+= s5p-dev-mfc.o

obj-$(CONFIG_SAMSUNG_DEV_BACKLIGHT)	+= dev-backlight.o

obj-$(CONFIG_S3C_SETUP_CAMIF)	+= setup-camif.o

# DMA support

obj-$(CONFIG_S3C_DMA)		+= dma.o s3c-dma-ops.o

obj-$(CONFIG_SAMSUNG_DMADEV)	+= dma-ops.o

# PM support

obj-$(CONFIG_PM_SLEEP)		+= pm-common.o
obj-$(CONFIG_SAMSUNG_PM)	+= pm.o
obj-$(CONFIG_SAMSUNG_PM_GPIO)	+= pm-gpio.o
obj-$(CONFIG_SAMSUNG_PM_CHECK)	+= pm-check.o
obj-$(CONFIG_SAMSUNG_PM_DEBUG)	+= pm-debug.o

obj-$(CONFIG_SAMSUNG_WAKEMASK)	+= wakeup-mask.o
obj-$(CONFIG_SAMSUNG_WDT_RESET)	+= watchdog-reset.o
