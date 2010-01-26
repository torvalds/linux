# arch/arm/plat-s3c64xx/Makefile
#
# Copyright 2008 Openmoko, Inc.
# Copyright 2008 Simtec Electronics
#
# Licensed under GPLv2

obj-y				:=
obj-m				:=
obj-n				:= dummy.o
obj-				:=

# Core files
obj-y				+= cpu.o
obj-y				+= clock.o
obj-y				+= gpiolib.o

# CPU support

obj-$(CONFIG_CPU_S3C6400_INIT)	+= s3c6400-init.o
obj-$(CONFIG_CPU_S3C6400_CLOCK)	+= s3c6400-clock.o
obj-$(CONFIG_CPU_FREQ_S3C64XX)  += cpufreq.o

# PM support

obj-$(CONFIG_PM)		+= pm.o
obj-$(CONFIG_PM)		+= sleep.o

# DMA support

obj-$(CONFIG_S3C64XX_DMA)	+= dma.o

