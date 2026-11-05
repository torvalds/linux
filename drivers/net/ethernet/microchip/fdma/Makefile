# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for Microchip FDMA
#

obj-$(CONFIG_FDMA) += fdma.o
fdma-y += fdma_api.o

ifneq ($(CONFIG_MCHP_LAN966X_PCI),)
fdma-y += fdma_pci.o
endif
