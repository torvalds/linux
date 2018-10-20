/*
 * Copyright (C) 2014 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 * Copyright (C) 2018 Stanislaw Gruszka <stf_xl@wp.pl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/etherdevice.h>
#include "mt76x0.h"

static int
mt76x0_set_channel(struct mt76x02_dev *dev, struct cfg80211_chan_def *chandef)
{
	int ret;

	cancel_delayed_work_sync(&dev->cal_work);
	tasklet_disable(&dev->pre_tbtt_tasklet);

	mt76_set_channel(&dev->mt76);
	ret = mt76x0_phy_set_channel(dev, chandef);

	/* channel cycle counters read-and-clear */
	mt76_rr(dev, MT_CH_IDLE);
	mt76_rr(dev, MT_CH_BUSY);

	tasklet_enable(&dev->pre_tbtt_tasklet);
	mt76_txq_schedule_all(&dev->mt76);

	return ret;
}

int mt76x0_config(struct ieee80211_hw *hw, u32 changed)
{
	struct mt76x02_dev *dev = hw->priv;
	int ret = 0;

	mutex_lock(&dev->mt76.mutex);

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		ieee80211_stop_queues(hw);
		ret = mt76x0_set_channel(dev, &hw->conf.chandef);
		ieee80211_wake_queues(hw);
	}

	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		dev->mt76.txpower_conf = hw->conf.power_level * 2;

		if (test_bit(MT76_STATE_RUNNING, &dev->mt76.state))
			mt76x0_phy_set_txpower(dev);
	}

	if (changed & IEEE80211_CONF_CHANGE_MONITOR) {
		if (!(hw->conf.flags & IEEE80211_CONF_MONITOR))
			dev->mt76.rxfilter |= MT_RX_FILTR_CFG_PROMISC;
		else
			dev->mt76.rxfilter &= ~MT_RX_FILTR_CFG_PROMISC;

		mt76_wr(dev, MT_RX_FILTR_CFG, dev->mt76.rxfilter);
	}

	mutex_unlock(&dev->mt76.mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(mt76x0_config);

void mt76x0_bss_info_changed(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *info, u32 changed)
{
	struct mt76x02_vif *mvif = (struct mt76x02_vif *)vif->drv_priv;
	struct mt76x02_dev *dev = hw->priv;

	mutex_lock(&dev->mt76.mutex);

	if (changed & BSS_CHANGED_BSSID)
		mt76x02_mac_set_bssid(dev, mvif->idx, info->bssid);

	if (changed & BSS_CHANGED_BEACON_ENABLED) {
		tasklet_disable(&dev->pre_tbtt_tasklet);
		mt76x02_mac_set_beacon_enable(dev, mvif->idx,
					      info->enable_beacon);
		tasklet_enable(&dev->pre_tbtt_tasklet);
	}

	if (changed & BSS_CHANGED_BASIC_RATES) {
		mt76_wr(dev, MT_LEGACY_BASIC_RATE, info->basic_rates);
		mt76_wr(dev, MT_VHT_HT_FBK_CFG0, 0x65432100);
		mt76_wr(dev, MT_VHT_HT_FBK_CFG1, 0xedcba980);
		mt76_wr(dev, MT_LG_FBK_CFG0, 0xedcba988);
		mt76_wr(dev, MT_LG_FBK_CFG1, 0x00002100);
	}

	if (changed & BSS_CHANGED_BEACON_INT) {
		mt76_rmw_field(dev, MT_BEACON_TIME_CFG,
			       MT_BEACON_TIME_CFG_INTVAL,
			       info->beacon_int << 4);
		dev->beacon_int = info->beacon_int;
		dev->tbtt_count = 0;
	}

	if (changed & BSS_CHANGED_HT || changed & BSS_CHANGED_ERP_CTS_PROT)
		mt76x0_mac_set_protection(dev, info->use_cts_prot,
					   info->ht_operation_mode);

	if (changed & BSS_CHANGED_ERP_PREAMBLE)
		mt76x02_mac_set_short_preamble(dev, info->use_short_preamble);

	if (changed & BSS_CHANGED_ERP_SLOT) {
		int slottime = info->use_short_slot ? 9 : 20;

		dev->slottime = slottime;
		mt76x02_set_tx_ackto(dev);
	}

	mutex_unlock(&dev->mt76.mutex);
}
EXPORT_SYMBOL_GPL(mt76x0_bss_info_changed);

int mt76x0_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	struct mt76x02_dev *dev = hw->priv;

	mt76_rmw_field(dev, MT_TX_RTS_CFG, MT_TX_RTS_CFG_THRESH, value);

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x0_set_rts_threshold);
