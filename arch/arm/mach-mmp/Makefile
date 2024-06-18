# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Marvell's PXA168 processors line
#
obj-y				+= common.o time.o

ifeq ($(CONFIG_SMP),y)
obj-$(CONFIG_MACH_MMP3_DT)	+= platsmp.o
endif

# board support
obj-$(CONFIG_MACH_MMP_DT)	+= mmp-dt.o
obj-$(CONFIG_MACH_MMP2_DT)	+= mmp2-dt.o
obj-$(CONFIG_MACH_MMP3_DT)	+= mmp3.o
