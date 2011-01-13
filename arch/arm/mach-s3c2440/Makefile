# arch/arm/mach-s3c2440/Makefile
#
# Copyright 2007 Simtec Electronics
#
# Licensed under GPLv2

obj-y				:=
obj-m				:=
obj-n				:=
obj-				:=

obj-$(CONFIG_CPU_S3C2440)	+= s3c2440.o dsc.o
obj-$(CONFIG_CPU_S3C2442)	+= s3c2442.o

obj-$(CONFIG_CPU_S3C2440)	+= irq.o
obj-$(CONFIG_CPU_S3C2440)	+= clock.o
obj-$(CONFIG_S3C2440_DMA)	+= dma.o

obj-$(CONFIG_CPU_S3C244X)	+= s3c244x.o
obj-$(CONFIG_CPU_S3C244X)	+= s3c244x-irq.o
obj-$(CONFIG_CPU_S3C244X)	+= s3c244x-clock.o
obj-$(CONFIG_S3C2440_CPUFREQ)	+= s3c2440-cpufreq.o

obj-$(CONFIG_S3C2440_PLL_12000000) += s3c2440-pll-12000000.o
obj-$(CONFIG_S3C2440_PLL_16934400) += s3c2440-pll-16934400.o

# Machine support

obj-$(CONFIG_MACH_ANUBIS)	+= mach-anubis.o
obj-$(CONFIG_MACH_OSIRIS)	+= mach-osiris.o
obj-$(CONFIG_MACH_RX3715)	+= mach-rx3715.o
obj-$(CONFIG_ARCH_S3C2440)	+= mach-smdk2440.o
obj-$(CONFIG_MACH_NEXCODER_2440) += mach-nexcoder.o
obj-$(CONFIG_MACH_AT2440EVB) += mach-at2440evb.o
obj-$(CONFIG_MACH_MINI2440) += mach-mini2440.o
obj-$(CONFIG_MACH_NEO1973_GTA02) += mach-gta02.o
obj-$(CONFIG_MACH_RX1950) += mach-rx1950.o

# extra machine support

obj-$(CONFIG_MACH_OSIRIS_DVS)	+= mach-osiris-dvs.o
