# SPDX-License-Identifier: GPL-2.0
#
# Copyright 2009 Simtec Electronics

include $(src)/Makefile.s3c64xx

# Objects we always build independent of SoC choice

obj-y				+= init.o cpu.o

# devices

obj-$(CONFIG_SAMSUNG_ATAGS)	+= platformdata.o
obj-$(CONFIG_SAMSUNG_ATAGS)	+= devs.o
obj-$(CONFIG_SAMSUNG_ATAGS)	+= dev-uart.o

obj-$(CONFIG_GPIO_SAMSUNG)     += gpio-samsung.o

# PM support

obj-$(CONFIG_SAMSUNG_PM)	+= pm.o pm-common.o
obj-$(CONFIG_SAMSUNG_PM_GPIO)	+= pm-gpio.o
obj-$(CONFIG_SAMSUNG_WAKEMASK)	+= wakeup-mask.o
