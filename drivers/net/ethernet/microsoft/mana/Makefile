# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
#
# Makefile for the Microsoft Azure Network Adapter driver

obj-$(CONFIG_MICROSOFT_GDMA_CORE) += gdma_core.o
gdma_core-y := gdma_main.o shm_channel.o hw_channel.o mana_en.o mana_ethtool.o \
	       mana_bpf.o

obj-$(CONFIG_MICROSOFT_MANA) += mana.o
mana-y := gdma_pci.o

obj-$(CONFIG_MICROSOFT_MANA_CDX) += mana_cdx.o
mana_cdx-y := gdma_cdx.o
