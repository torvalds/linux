# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright 2011-2020 NXP
#


nxpwifi-y += main.o
nxpwifi-y += init.o
nxpwifi-y += cfp.o
nxpwifi-y += cmdevt.o
nxpwifi-y += util.o
nxpwifi-y += txrx.o
nxpwifi-y += wmm.o
nxpwifi-y += 11n.o
nxpwifi-y += 11ac.o
nxpwifi-y += 11ax.o
nxpwifi-y += 11n_aggr.o
nxpwifi-y += 11n_rxreorder.o
nxpwifi-y += scan.o
nxpwifi-y += join.o
nxpwifi-y += sta_cfg.o
nxpwifi-y += sta_cmd.o
nxpwifi-y += uap_cmd.o
nxpwifi-y += ie.o
nxpwifi-y += sta_event.o
nxpwifi-y += uap_event.o
nxpwifi-y += sta_tx.o
nxpwifi-y += sta_rx.o
nxpwifi-y += uap_txrx.o
nxpwifi-y += cfg80211.o
nxpwifi-y += ethtool.o
nxpwifi-y += 11h.o
nxpwifi-$(CONFIG_DEBUG_FS) += debugfs.o
obj-$(CONFIG_NXPWIFI) += nxpwifi.o

nxpwifi_sdio-y += sdio.o
obj-$(CONFIG_NXPWIFI_SDIO) += nxpwifi_sdio.o

ccflags-y += -D__CHECK_ENDIAN
