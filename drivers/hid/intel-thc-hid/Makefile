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

ccflags-y += -I $(src)/intel-thc
