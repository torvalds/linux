# arch/arm/plat-s3c24xx/Makefile
#
# Copyright 2007 Simtec Electronics
#
# Licensed under GPLv2

obj-y				:=
obj-m				:=
obj-n				:=
obj-				:=


# Core files

obj-y				+= cpu.o
obj-y				+= irq.o
obj-y				+= devs.o
obj-y				+= gpio.o
obj-y				+= gpiolib.o
obj-y				+= clock.o
obj-$(CONFIG_S3C24XX_DCLK)	+= clock-dclk.o

# Architecture dependant builds

obj-$(CONFIG_CPU_S3C244X)	+= s3c244x.o
obj-$(CONFIG_CPU_S3C244X)	+= s3c244x-irq.o
obj-$(CONFIG_CPU_S3C244X)	+= s3c244x-clock.o
obj-$(CONFIG_PM_SIMTEC)		+= pm-simtec.o
obj-$(CONFIG_PM)		+= pm.o
obj-$(CONFIG_PM)		+= sleep.o
obj-$(CONFIG_HAVE_PWM)		+= pwm.o
obj-$(CONFIG_S3C2410_CLOCK)	+= s3c2410-clock.o
obj-$(CONFIG_S3C2410_DMA)	+= dma.o
obj-$(CONFIG_S3C24XX_ADC)	+= adc.o

# device specific setup and/or initialisation
obj-$(CONFIG_ARCH_S3C2410)	+= setup-i2c.o

# SPI gpio central GPIO functions

obj-$(CONFIG_S3C24XX_SPI_BUS0_GPE11_GPE12_GPE13) += spi-bus0-gpe11_12_13.o
obj-$(CONFIG_S3C24XX_SPI_BUS1_GPG5_GPG6_GPG7)    += spi-bus1-gpg5_6_7.o

# machine common support

obj-$(CONFIG_MACH_SMDK)		+= common-smdk.o
