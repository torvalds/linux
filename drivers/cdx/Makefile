# SPDX-License-Identifier: GPL-2.0
#
# Makefile for CDX
#
# Copyright (C) 2022-2023, Advanced Micro Devices, Inc.
#

ccflags-y += -DDEFAULT_SYMBOL_NAMESPACE='"CDX_BUS"'

obj-$(CONFIG_CDX_BUS) += cdx.o controller/

ifdef CONFIG_GENERIC_MSI_IRQ
obj-$(CONFIG_CDX_BUS) += cdx_msi.o
endif
