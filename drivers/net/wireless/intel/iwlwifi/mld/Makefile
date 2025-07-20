# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
obj-$(CONFIG_IWLMLD)   += iwlmld.o
obj-$(CONFIG_IWLWIFI_KUNIT_TESTS) += tests/

iwlmld-y += mld.o notif.o mac80211.o fw.o power.o iface.o link.o rx.o mcc.o session-protect.o phy.o
iwlmld-y += scan.o sta.o tx.o coex.o tlc.o agg.o key.o regulatory.o ap.o thermal.o roc.o stats.o
iwlmld-y += low_latency.o mlo.o ptp.o time_sync.o ftm-initiator.o
iwlmld-$(CONFIG_IWLWIFI_DEBUGFS) += debugfs.o
iwlmld-$(CONFIG_IWLWIFI_LEDS) += led.o
iwlmld-$(CONFIG_PM_SLEEP) += d3.o

# non-upstream things
iwlmld-$(CONFIG_IWL_VENDOR_CMDS) += vendor-cmd.o
iwlmld-$(CONFIG_IWLMVM_AX_SOFTAP_TESTMODE) += ax-softap-testmode.o

subdir-ccflags-y += -I$(src)/../
