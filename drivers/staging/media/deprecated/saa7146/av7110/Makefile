# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the AV7110 DVB device driver
#

dvb-ttpci-objs := av7110_hw.o av7110_v4l.o av7110_av.o av7110_ca.o av7110.o \
		  av7110_ipack.o dvb_filter.o

ifdef CONFIG_DVB_AV7110_IR
dvb-ttpci-objs += av7110_ir.o
endif

obj-$(CONFIG_DVB_BUDGET_PATCH) += budget-patch.o

obj-$(CONFIG_DVB_AV7110) += dvb-ttpci.o

obj-$(CONFIG_DVB_SP8870) += sp8870.o

ccflags-y += -I $(srctree)/drivers/media/dvb-frontends
ccflags-y += -I $(srctree)/drivers/media/tuners
ccflags-y += -I $(srctree)/drivers/media/common
ccflags-y += -I $(srctree)/drivers/staging/media/deprecated/saa7146/ttpci
ccflags-y += -I $(srctree)/drivers/staging/media/deprecated/saa7146/common
