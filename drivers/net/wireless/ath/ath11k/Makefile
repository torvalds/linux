# SPDX-License-Identifier: BSD-3-Clause-Clear
obj-$(CONFIG_ATH11K) += ath11k.o
ath11k-y += core.o \
	    hal.o \
	    hal_tx.o \
	    hal_rx.o \
	    wmi.o \
	    mac.o \
	    reg.o \
	    htc.o \
	    qmi.o \
	    dp.o  \
	    dp_tx.o \
	    dp_rx.o \
	    debug.o \
	    ce.o \
	    peer.o \
	    dbring.o \
	    hw.o

ath11k-$(CONFIG_ATH11K_DEBUGFS) += debugfs.o debugfs_htt_stats.o debugfs_sta.o
ath11k-$(CONFIG_NL80211_TESTMODE) += testmode.o
ath11k-$(CONFIG_ATH11K_TRACING) += trace.o
ath11k-$(CONFIG_THERMAL) += thermal.o
ath11k-$(CONFIG_ATH11K_SPECTRAL) += spectral.o

obj-$(CONFIG_ATH11K_AHB) += ath11k_ahb.o
ath11k_ahb-y += ahb.o

obj-$(CONFIG_ATH11K_PCI) += ath11k_pci.o
ath11k_pci-y += mhi.o pci.o

# for tracing framework to find trace.h
CFLAGS_trace.o := -I$(src)
