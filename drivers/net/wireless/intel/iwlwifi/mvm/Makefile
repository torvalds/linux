# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_IWLMVM)   += iwlmvm.o
iwlmvm-y += fw.o mac80211.o nvm.o ops.o phy-ctxt.o mac-ctxt.o
iwlmvm-y += utils.o rx.o rxmq.o tx.o binding.o quota.o sta.o sf.o
iwlmvm-y += scan.o time-event.o rs.o rs-fw.o
iwlmvm-y += power.o coex.o
iwlmvm-y += tt.o offloading.o tdls.o
iwlmvm-$(CONFIG_IWLWIFI_DEBUGFS) += debugfs.o debugfs-vif.o
iwlmvm-$(CONFIG_IWLWIFI_LEDS) += led.o
iwlmvm-$(CONFIG_PM) += d3.o

ccflags-y += -I $(srctree)/$(src)/../
