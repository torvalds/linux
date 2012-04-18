# arch/arm/plat-samsung/Makefile
#
# Copyright 2009 Simtec Electronics
#
# Licensed under GPLv2

obj-y				:=
obj-m				:=
obj-n				:= dummy.o
obj-				:=

# Objects we always build independent of SoC choice

obj-y				+= init.o cpu.o
obj-$(CONFIG_ARCH_USES_GETTIMEOFFSET)   += time.o
obj-$(CONFIG_S5P_HRT) 		+= s5p-time.o

obj-y				+= clock.o
obj-y				+= pwm-clock.o

obj-$(CONFIG_SAMSUNG_CLKSRC)	+= clock-clksrc.o
obj-$(CONFIG_S5P_CLOCK)		+= s5p-clock.o

obj-$(CONFIG_SAMSUNG_IRQ_VIC_TIMER) += irq-vic-timer.o
obj-$(CONFIG_S5P_IRQ)		+= s5p-irq.o
obj-$(CONFIG_S5P_EXT_INT)	+= s5p-irq-eint.o
obj-$(CONFIG_S5P_GPIO_INT)	+= s5p-irq-gpioint.o

# ADC

obj-$(CONFIG_S3C_ADC)	+= adc.o

# devices

obj-y				+= platformdata.o

obj-y				+= devs.o
obj-y				+= dev-uart.o
obj-$(CONFIG_S5P_DEV_UART)	+= s5p-dev-uart.o

obj-$(CONFIG_SAMSUNG_DEV_BACKLIGHT)	+= dev-backlight.o

# DMA support

obj-$(CONFIG_S3C_DMA)		+= dma.o s3c-dma-ops.o

obj-$(CONFIG_SAMSUNG_DMADEV)	+= dma-ops.o

# PM support

obj-$(CONFIG_PM)		+= pm.o
obj-$(CONFIG_PM)		+= pm-gpio.o
obj-$(CONFIG_SAMSUNG_PM_CHECK)	+= pm-check.o

obj-$(CONFIG_SAMSUNG_WAKEMASK)	+= wakeup-mask.o

obj-$(CONFIG_S5P_PM)		+= s5p-pm.o s5p-irq-pm.o
obj-$(CONFIG_S5P_SLEEP)		+= s5p-sleep.o

# PD support

obj-$(CONFIG_SAMSUNG_PD)	+= pd.o

# PWM support

obj-$(CONFIG_HAVE_PWM)		+= pwm.o
