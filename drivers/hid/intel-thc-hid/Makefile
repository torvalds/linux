# SPDX-License-Identifier: GPL-2.0
#
# Makefile - Intel Touch Host Controller (THC) drivers
# Copyright (c) 2024, Intel Corporation.
#
#

obj-$(CONFIG_INTEL_THC_HID) += intel-thc.o
intel-thc-objs += intel-thc/intel-thc-dev.o
intel-thc-objs += intel-thc/intel-thc-dma.o

obj-$(CONFIG_INTEL_QUICKSPI) += intel-quickspi.o
intel-quickspi-objs += intel-quickspi/pci-quickspi.o
intel-quickspi-objs += intel-quickspi/quickspi-hid.o
intel-quickspi-objs += intel-quickspi/quickspi-protocol.o

obj-$(CONFIG_INTEL_QUICKI2C) += intel-quicki2c.o
intel-quicki2c-objs += intel-quicki2c/pci-quicki2c.o
intel-quicki2c-objs += intel-quicki2c/quicki2c-hid.o
intel-quicki2c-objs += intel-quicki2c/quicki2c-protocol.o

ccflags-y += -I $(src)/intel-thc
