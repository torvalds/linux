# arch/arm/mach-s3c2410/Makefile
#
# Copyright 2007 Simtec Electronics
#
# Licensed under GPLv2

obj-y				:=
obj-m				:=
obj-n				:=
obj-				:=

obj-$(CONFIG_CPU_S3C2410)	+= s3c2410.o
obj-$(CONFIG_CPU_S3C2410_DMA)	+= dma.o
obj-$(CONFIG_CPU_S3C2410_DMA)	+= dma.o
obj-$(CONFIG_S3C2410_PM)	+= pm.o sleep.o
obj-$(CONFIG_S3C2410_GPIO)	+= gpio.o
obj-$(CONFIG_S3C2410_CPUFREQ)	+= cpu-freq.o
obj-$(CONFIG_S3C2410_PLLTABLE)	+= pll.o

# Machine support

obj-$(CONFIG_ARCH_SMDK2410)	+= mach-smdk2410.o
obj-$(CONFIG_ARCH_H1940)	+= mach-h1940.o
obj-$(CONFIG_H1940BT)		+= h1940-bluetooth.o
obj-$(CONFIG_PM_H1940)		+= pm-h1940.o
obj-$(CONFIG_MACH_N30)		+= mach-n30.o
obj-$(CONFIG_ARCH_BAST)		+= mach-bast.o usb-simtec.o
obj-$(CONFIG_MACH_OTOM)		+= mach-otom.o
obj-$(CONFIG_MACH_AML_M5900)	+= mach-amlm5900.o
obj-$(CONFIG_BAST_PC104_IRQ)	+= bast-irq.o
obj-$(CONFIG_MACH_TCT_HAMMER)	+= mach-tct_hammer.o
obj-$(CONFIG_MACH_VR1000)	+= mach-vr1000.o usb-simtec.o
obj-$(CONFIG_MACH_QT2410)	+= mach-qt2410.o

# Common bits of machine support

obj-$(CONFIG_SIMTEC_NOR)	+= nor-simtec.o

# machine additions

obj-$(CONFIG_MACH_BAST_IDE)	+= bast-ide.o
