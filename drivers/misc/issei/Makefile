# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2023-2026 Intel Corporation
ccflags-y += -DDEFAULT_SYMBOL_NAMESPACE='"INTEL_SSEI"'

obj-$(CONFIG_INTEL_SSEI) += issei.o
issei-objs += cdev.o
issei-objs += dma.o
issei-objs += fw_client.o
issei-objs += host_client.o
issei-objs += ham.o
issei-objs += main.o

obj-$(CONFIG_INTEL_SSEI_HW_HECI) += issei-heci.o
issei-heci-objs := pci_heci.o
issei-heci-objs += hw_heci.o
