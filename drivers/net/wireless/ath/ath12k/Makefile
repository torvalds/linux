# SPDX-License-Identifier: BSD-3-Clause-Clear
obj-$(CONFIG_ATH12K) += ath12k.o
ath12k-y += core.o \
	    hal.o \
	    wmi.o \
	    mac.o \
	    reg.o \
	    htc.o \
	    qmi.o \
	    dp.o  \
	    dp_tx.o \
	    dp_rx.o \
	    dp_htt.o \
	    debug.o \
	    ce.o \
	    peer.o \
	    dbring.o \
	    mhi.o \
	    pci.o \
	    dp_mon.o \
	    fw.o \
	    p2p.o

ath12k-$(CONFIG_ATH12K_AHB) += ahb.o

ath12k-y += wifi7/hal_tx.o \
	    wifi7/hal_rx.o \
	    wifi7/dp_rx.o \
	    wifi7/dp_tx.o \
	    wifi7/dp.o \
	    wifi7/hal.o \
	    wifi7/hal_qcn9274.o \
	    wifi7/hal_wcn7850.o

obj-$(CONFIG_ATH12K) += wifi7/

ath12k-$(CONFIG_ATH12K_DEBUGFS) += debugfs.o debugfs_htt_stats.o debugfs_sta.o
ath12k-$(CONFIG_ACPI) += acpi.o
ath12k-$(CONFIG_ATH12K_TRACING) += trace.o
ath12k-$(CONFIG_PM) += wow.o
ath12k-$(CONFIG_ATH12K_COREDUMP) += coredump.o
ath12k-$(CONFIG_NL80211_TESTMODE) += testmode.o

# for tracing framework to find trace.h
CFLAGS_trace.o := -I$(src)
