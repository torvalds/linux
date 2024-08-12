# SPDX-License-Identifier: GPL-2.0
#
# Makefile - Intel ISH HID drivers
# Copyright (c) 2014-2016, Intel Corporation.
#
#
obj-$(CONFIG_INTEL_ISH_HID) += intel-ishtp.o
intel-ishtp-objs := ishtp/init.o
intel-ishtp-objs += ishtp/hbm.o
intel-ishtp-objs += ishtp/client.o
intel-ishtp-objs += ishtp/bus.o
intel-ishtp-objs += ishtp/dma-if.o
intel-ishtp-objs += ishtp/client-buffers.o
intel-ishtp-objs += ishtp/loader.o

obj-$(CONFIG_INTEL_ISH_HID) += intel-ish-ipc.o
intel-ish-ipc-objs := ipc/ipc.o
intel-ish-ipc-objs += ipc/pci-ish.o

obj-$(CONFIG_INTEL_ISH_HID) += intel-ishtp-hid.o
intel-ishtp-hid-objs := ishtp-hid.o
intel-ishtp-hid-objs += ishtp-hid-client.o

obj-$(CONFIG_INTEL_ISH_FIRMWARE_DOWNLOADER) += intel-ishtp-loader.o
intel-ishtp-loader-objs += ishtp-fw-loader.o

ccflags-y += -I $(src)/ishtp
