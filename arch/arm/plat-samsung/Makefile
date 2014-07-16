# arch/arm/plat-samsung/Makefile
#
# Copyright 2009 Simtec Electronics
#
# Licensed under GPLv2

ccflags-$(CONFIG_ARCH_MULTI_V7) += -I$(srctree)/$(src)/include
ccflags-$(CONFIG_ARCH_EXYNOS)	+= -I$(srctree)/arch/arm/mach-exynos/include

obj-y				:=
obj-m				:=
obj-n				:= dummy.o
obj-				:=

# Objects we always build independent of SoC choice

obj-y				+= init.o cpu.o

obj-$(CONFIG_SAMSUNG_CLOCK)	+= clock.o

obj-$(CONFIG_SAMSUNG_CLKSRC)	+= clock-clksrc.o
obj-$(CONFIG_S5P_CLOCK)		+= s5p-clock.o

obj-$(CONFIG_S5P_IRQ)		+= s5p-irq.o
obj-$(CONFIG_S5P_EXT_INT)	+= s5p-irq-eint.o
obj-$(CONFIG_S5P_GPIO_INT)	+= s5p-irq-gpioint.o

# ADC

obj-$(CONFIG_S3C_ADC)	+= adc.o

# devices

obj-$(CONFIG_SAMSUNG_ATAGS)	+= platformdata.o

obj-$(CONFIG_SAMSUNG_ATAGS)	+= devs.o
obj-$(CONFIG_SAMSUNG_ATAGS)	+= dev-uart.o
obj-$(CONFIG_S5P_DEV_MFC)	+= s5p-dev-mfc.o
obj-$(CONFIG_S5P_DEV_UART)	+= s5p-dev-uart.o

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

obj-$(CONFIG_S5P_PM)		+= s5p-pm.o
obj-$(CONFIG_S5P_IRQ_PM)	+= s5p-irq-pm.o
obj-$(CONFIG_S5P_SLEEP)		+= s5p-sleep.o
