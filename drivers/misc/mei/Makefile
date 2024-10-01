# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2010-2019, Intel Corporation. All rights reserved.
# Makefile - Intel Management Engine Interface (Intel MEI) Linux driver
#
obj-$(CONFIG_INTEL_MEI) += mei.o
mei-objs := init.o
mei-objs += hbm.o
mei-objs += interrupt.o
mei-objs += client.o
mei-objs += main.o
mei-objs += dma-ring.o
mei-objs += bus.o
mei-objs += bus-fixup.o
mei-$(CONFIG_DEBUG_FS) += debugfs.o

obj-$(CONFIG_INTEL_MEI_ME) += mei-me.o
mei-me-objs := pci-me.o
mei-me-objs += hw-me.o

obj-$(CONFIG_INTEL_MEI_GSC) += mei-gsc.o
mei-gsc-objs := gsc-me.o

obj-$(CONFIG_INTEL_MEI_TXE) += mei-txe.o
mei-txe-objs := pci-txe.o
mei-txe-objs += hw-txe.o

mei-$(CONFIG_EVENT_TRACING) += mei-trace.o
CFLAGS_mei-trace.o = -I$(src)

obj-$(CONFIG_INTEL_MEI_HDCP) += hdcp/
obj-$(CONFIG_INTEL_MEI_PXP) += pxp/
obj-$(CONFIG_INTEL_MEI_GSC_PROXY) += gsc_proxy/

obj-$(CONFIG_INTEL_MEI_VSC_HW) += mei-vsc-hw.o
mei-vsc-hw-y := vsc-tp.o
mei-vsc-hw-y += vsc-fw-loader.o

obj-$(CONFIG_INTEL_MEI_VSC) += mei-vsc.o
mei-vsc-y := platform-vsc.o
