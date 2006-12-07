
#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y			:= cpu.o irq.o time.o gpio.o clock.o devs.o
obj-m			:=
obj-n			:=
obj-			:=
obj-dma-y		:=
obj-dma-n		:=

# DMA
obj-$(CONFIG_S3C2410_DMA)	+= dma.o

# S3C2400 support files
obj-$(CONFIG_CPU_S3C2400)	+= s3c2400-gpio.o

# S3C2410 support files

obj-$(CONFIG_CPU_S3C2410)	+= s3c2410.o
obj-$(CONFIG_CPU_S3C2410)	+= s3c2410-gpio.o
obj-$(CONFIG_CPU_S3C2410)	+= s3c2410-irq.o

obj-$(CONFIG_S3C2410_PM)	+= s3c2410-pm.o s3c2410-sleep.o
obj-$(CONFIG_CPU_S3C2410_DMA)	+= s3c2410-dma.o

# Power Management support

obj-$(CONFIG_PM)		+= pm.o sleep.o
obj-$(CONFIG_PM_SIMTEC)		+= pm-simtec.o
obj-$(CONFIG_PM_H1940)		+= pm-h1940.o

# S3C2412 support
obj-$(CONFIG_CPU_S3C2412)	+= s3c2412.o
obj-$(CONFIG_CPU_S3C2412)	+= s3c2412-irq.o
obj-$(CONFIG_CPU_S3C2412)	+= s3c2412-clock.o
obj-dma-$(CONFIG_CPU_S3C2412)	+= s3c2412-dma.o

obj-$(CONFIG_S3C2412_PM)	+= s3c2412-pm.o

#
# S3C244X support

obj-$(CONFIG_CPU_S3C244X)	+= s3c244x.o
obj-$(CONFIG_CPU_S3C244X)	+= s3c244x-irq.o

# Clock control

obj-$(CONFIG_S3C2410_CLOCK)	+= s3c2410-clock.o

# S3C2440 support

obj-$(CONFIG_CPU_S3C2440)	+= s3c2440.o s3c2440-dsc.o
obj-$(CONFIG_CPU_S3C2440)	+= s3c2440-irq.o
obj-$(CONFIG_CPU_S3C2440)	+= s3c2440-clock.o
obj-$(CONFIG_CPU_S3C2440)	+= s3c2410-gpio.o
obj-dma-$(CONFIG_CPU_S3C2440)	+= s3c2440-dma.o

# S3C2442 support

obj-$(CONFIG_CPU_S3C2442)	+= s3c2442.o
obj-$(CONFIG_CPU_S3C2442)	+= s3c2442-clock.o

# bast extras

obj-$(CONFIG_BAST_PC104_IRQ)	+= bast-irq.o

# merge in dma objects

obj-y				+= $(obj-dma-y)

# machine specific support

obj-$(CONFIG_MACH_AML_M5900)	+= mach-amlm5900.o
obj-$(CONFIG_MACH_ANUBIS)	+= mach-anubis.o
obj-$(CONFIG_MACH_OSIRIS)	+= mach-osiris.o
obj-$(CONFIG_ARCH_BAST)		+= mach-bast.o usb-simtec.o
obj-$(CONFIG_ARCH_H1940)	+= mach-h1940.o
obj-$(CONFIG_MACH_N30)		+= mach-n30.o
obj-$(CONFIG_ARCH_SMDK2410)	+= mach-smdk2410.o
obj-$(CONFIG_MACH_SMDK2413)	+= mach-smdk2413.o
obj-$(CONFIG_ARCH_S3C2440)	+= mach-smdk2440.o
obj-$(CONFIG_MACH_VR1000)	+= mach-vr1000.o usb-simtec.o
obj-$(CONFIG_MACH_RX3715)	+= mach-rx3715.o
obj-$(CONFIG_MACH_OTOM)		+= mach-otom.o
obj-$(CONFIG_MACH_NEXCODER_2440) += mach-nexcoder.o
obj-$(CONFIG_MACH_VSTMS)	+= mach-vstms.o

obj-$(CONFIG_MACH_SMDK)		+= common-smdk.o