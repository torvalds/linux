# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Marvell's PXA168 processors line
#
obj-y				+= common.o devices.o time.o

# SoC support
obj-$(CONFIG_CPU_PXA168)	+= pxa168.o
obj-$(CONFIG_CPU_PXA910)	+= pxa910.o
obj-$(CONFIG_CPU_MMP2)		+= mmp2.o
obj-$(CONFIG_MMP_SRAM)		+= sram.o

ifeq ($(CONFIG_PM),y)
obj-$(CONFIG_CPU_PXA910)	+= pm-pxa910.o
obj-$(CONFIG_CPU_MMP2)		+= pm-mmp2.o
endif
ifeq ($(CONFIG_SMP),y)
obj-$(CONFIG_MACH_MMP3_DT)	+= platsmp.o
endif

# board support
obj-$(CONFIG_MACH_MMP_DT)	+= mmp-dt.o
obj-$(CONFIG_MACH_MMP2_DT)	+= mmp2-dt.o
obj-$(CONFIG_MACH_MMP3_DT)	+= mmp3.o
