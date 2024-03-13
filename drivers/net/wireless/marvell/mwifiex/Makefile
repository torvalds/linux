# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright 2011-2020 NXP
#


mwifiex-y += main.o
mwifiex-y += init.o
mwifiex-y += cfp.o
mwifiex-y += cmdevt.o
mwifiex-y += util.o
mwifiex-y += txrx.o
mwifiex-y += wmm.o
mwifiex-y += 11n.o
mwifiex-y += 11ac.o
mwifiex-y += 11n_aggr.o
mwifiex-y += 11n_rxreorder.o
mwifiex-y += scan.o
mwifiex-y += join.o
mwifiex-y += sta_ioctl.o
mwifiex-y += sta_cmd.o
mwifiex-y += uap_cmd.o
mwifiex-y += ie.o
mwifiex-y += sta_cmdresp.o
mwifiex-y += sta_event.o
mwifiex-y += uap_event.o
mwifiex-y += sta_tx.o
mwifiex-y += sta_rx.o
mwifiex-y += uap_txrx.o
mwifiex-y += cfg80211.o
mwifiex-y += ethtool.o
mwifiex-y += 11h.o
mwifiex-y += tdls.o
mwifiex-$(CONFIG_DEBUG_FS) += debugfs.o
obj-$(CONFIG_MWIFIEX) += mwifiex.o

mwifiex_sdio-y += sdio.o
obj-$(CONFIG_MWIFIEX_SDIO) += mwifiex_sdio.o

mwifiex_pcie-y += pcie.o
mwifiex_pcie-y += pcie_quirks.o
obj-$(CONFIG_MWIFIEX_PCIE) += mwifiex_pcie.o

mwifiex_usb-y += usb.o
obj-$(CONFIG_MWIFIEX_USB) += mwifiex_usb.o

ccflags-y += -D__CHECK_ENDIAN
