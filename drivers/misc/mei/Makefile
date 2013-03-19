#
# Makefile - Intel Management Engine Interface (Intel MEI) Linux driver
# Copyright (c) 2010-2011, Intel Corporation.
#
obj-$(CONFIG_INTEL_MEI) += mei.o
mei-objs := init.o
mei-objs += hbm.o
mei-objs += interrupt.o
mei-objs += client.o
mei-objs += main.o
mei-objs += amthif.o
mei-objs += wd.o
mei-$(CONFIG_INTEL_MEI_ME) += pci-me.o
mei-$(CONFIG_INTEL_MEI_ME) += hw-me.o
