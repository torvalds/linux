# SPDX-License-Identifier: BSD-3-Clause-Clear
obj-$(CONFIG_ATH11K) += ath11k.o
ath11k-y += core.o \
	    hal.o \
	    hal_tx.o \
	    hal_rx.o \
	    ahb.o \
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
	    peer.o

ath11k-$(CONFIG_ATH11K_DEBUGFS) += debug_htt_stats.o debugfs_sta.o
ath11k-$(CONFIG_NL80211_TESTMODE) += testmode.o
ath11k-$(CONFIG_ATH11K_TRACING) += trace.o

# for tracing framework to find trace.h
CFLAGS_trace.o := -I$(src)
