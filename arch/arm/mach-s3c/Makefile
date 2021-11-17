# SPDX-License-Identifier: GPL-2.0
#
# Copyright 2009 Simtec Electronics

ccflags-$(CONFIG_ARCH_MULTIPLATFORM) += -I$(srctree)/$(src)/include

ifdef CONFIG_ARCH_S3C24XX
include $(src)/Makefile.s3c24xx
endif

ifdef CONFIG_ARCH_S3C64XX
include $(src)/Makefile.s3c64xx
endif

# Objects we always build independent of SoC choice

obj-y				+= init.o cpu.o

# ADC

obj-$(CONFIG_S3C_ADC)	+= adc.o

# devices

obj-$(CONFIG_SAMSUNG_ATAGS)	+= platformdata.o

obj-$(CONFIG_SAMSUNG_ATAGS)	+= devs.o
obj-$(CONFIG_SAMSUNG_ATAGS)	+= dev-uart.o

obj-$(CONFIG_GPIO_SAMSUNG)     += gpio-samsung.o

# PM support

obj-$(CONFIG_SAMSUNG_PM)	+= pm.o pm-common.o
obj-$(CONFIG_SAMSUNG_PM_GPIO)	+= pm-gpio.o

obj-$(CONFIG_SAMSUNG_WAKEMASK)	+= wakeup-mask.o
