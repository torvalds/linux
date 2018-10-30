# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_ATH10K) += ath10k_core.o
ath10k_core-y += mac.o \
		 debug.o \
		 core.o \
		 htc.o \
		 htt.o \
		 htt_rx.o \
		 htt_tx.o \
		 txrx.o \
		 wmi.o \
		 wmi-tlv.o \
		 bmi.o \
		 hw.o \
		 p2p.o \
		 swap.o

ath10k_core-$(CONFIG_ATH10K_SPECTRAL) += spectral.o
ath10k_core-$(CONFIG_NL80211_TESTMODE) += testmode.o
ath10k_core-$(CONFIG_ATH10K_TRACING) += trace.o
ath10k_core-$(CONFIG_THERMAL) += thermal.o
ath10k_core-$(CONFIG_MAC80211_DEBUGFS) += debugfs_sta.o
ath10k_core-$(CONFIG_PM) += wow.o
ath10k_core-$(CONFIG_DEV_COREDUMP) += coredump.o
ath10k_core-$(CONFIG_ATH10K_CE) += ce.o

obj-$(CONFIG_ATH10K_PCI) += ath10k_pci.o
ath10k_pci-y += pci.o

ath10k_pci-$(CONFIG_ATH10K_AHB) += ahb.o

obj-$(CONFIG_ATH10K_SDIO) += ath10k_sdio.o
ath10k_sdio-y += sdio.o

obj-$(CONFIG_ATH10K_USB) += ath10k_usb.o
ath10k_usb-y += usb.o

obj-$(CONFIG_ATH10K_SNOC) += ath10k_snoc.o
ath10k_snoc-y += qmi.o \
		 qmi_wlfw_v01.o \
		 snoc.o

# for tracing framework to find trace.h
CFLAGS_trace.o := -I$(src)
