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

obj-y				+= irq.o
obj-y				+= clock.o
obj-$(CONFIG_S3C24XX_DCLK)	+= clock-dclk.o

obj-$(CONFIG_CPU_FREQ_S3C24XX)	+= cpu-freq.o
obj-$(CONFIG_CPU_FREQ_S3C24XX_DEBUGFS) += cpu-freq-debugfs.o

# Architecture dependent builds

obj-$(CONFIG_S3C2410_CLOCK)	+= s3c2410-clock.o
obj-$(CONFIG_S3C24XX_DMA)	+= dma.o
obj-$(CONFIG_S3C2410_IOTIMING)	+= s3c2410-iotiming.o
obj-$(CONFIG_S3C2412_IOTIMING)	+= s3c2412-iotiming.o
obj-$(CONFIG_S3C2410_CPUFREQ_UTILS) += s3c2410-cpufreq-utils.o
