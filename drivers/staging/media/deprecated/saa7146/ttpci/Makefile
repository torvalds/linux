# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the kernel SAA7146 FULL TS DVB device driver
#

obj-$(CONFIG_DVB_BUDGET_CORE) += budget-core.o
obj-$(CONFIG_DVB_BUDGET) += budget.o
obj-$(CONFIG_DVB_BUDGET_AV) += budget-av.o
obj-$(CONFIG_DVB_BUDGET_CI) += budget-ci.o

ccflags-y += -I $(srctree)/drivers/media/dvb-frontends/
ccflags-y += -I $(srctree)/drivers/media/tuners
ccflags-y += -I $(srctree)/drivers/media/common
