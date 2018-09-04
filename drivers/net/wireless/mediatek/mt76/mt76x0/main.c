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

#include "mt76x0.h"
#include "mac.h"
#include "../mt76x02_util.h"
#include <linux/etherdevice.h>

static int mt76x0_start(struct ieee80211_hw *hw)
{
	struct mt76x0_dev *dev = hw->priv;
	int ret;

	mutex_lock(&dev->mt76.mutex);

	ret = mt76x0_mac_start(dev);
	if (ret)
		goto out;

	ieee80211_queue_delayed_work(dev->mt76.hw, &dev->mac_work,
				     MT_CALIBRATE_INTERVAL);
	ieee80211_queue_delayed_work(dev->mt76.hw, &dev->cal_work,
				     MT_CALIBRATE_INTERVAL);
out:
	mutex_unlock(&dev->mt76.mutex);
	return ret;
}

static void mt76x0_stop(struct ieee80211_hw *hw)
{
	struct mt76x0_dev *dev = hw->priv;

	mutex_lock(&dev->mt76.mutex);

	cancel_delayed_work_sync(&dev->cal_work);
	cancel_delayed_work_sync(&dev->mac_work);
	mt76x0_mac_stop(dev);

	mutex_unlock(&dev->mt76.mutex);
}


static int mt76x0_add_interface(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif)
{
	struct mt76x0_dev *dev = hw->priv;
	unsigned int idx;

	idx = ffs(~dev->vif_mask);
	if (!idx || idx > 8)
		return -ENOSPC;

	idx--;
	dev->vif_mask |= BIT(idx);

	mt76x02_vif_init(&dev->mt76, vif, idx);
	return 0;
}

static void mt76x0_remove_interface(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif)
{
	struct mt76x0_dev *dev = hw->priv;
	struct mt76x02_vif *mvif = (struct mt76x02_vif *) vif->drv_priv;

	dev->vif_mask &= ~BIT(mvif->idx);
	mt76_txq_remove(&dev->mt76, vif->txq);
}

static int mt76x0_config(struct ieee80211_hw *hw, u32 changed)
{
	struct mt76x0_dev *dev = hw->priv;
	int ret = 0;

	mutex_lock(&dev->mt76.mutex);

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		ieee80211_stop_queues(hw);
		ret = mt76x0_phy_set_channel(dev, &hw->conf.chandef);
		ieee80211_wake_queues(hw);
	}

	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static void
mt76x0_bss_info_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			 struct ieee80211_bss_conf *info, u32 changed)
{
	struct mt76x0_dev *dev = hw->priv;

	mutex_lock(&dev->mt76.mutex);

	if (changed & BSS_CHANGED_ASSOC)
		mt76x0_phy_con_cal_onoff(dev, info);

	if (changed & BSS_CHANGED_BSSID) {
		mt76x0_addr_wr(dev, MT_MAC_BSSID_DW0, info->bssid);

		/* Note: this is a hack because beacon_int is not changed
		 *	 on leave nor is any more appropriate event generated.
		 *	 rt2x00 doesn't seem to be bothered though.
		 */
		if (is_zero_ether_addr(info->bssid))
			mt76x0_mac_config_tsf(dev, false, 0);
	}

	if (changed & BSS_CHANGED_BASIC_RATES) {
		mt76_wr(dev, MT_LEGACY_BASIC_RATE, info->basic_rates);
		mt76_wr(dev, MT_VHT_HT_FBK_CFG0, 0x65432100);
		mt76_wr(dev, MT_VHT_HT_FBK_CFG1, 0xedcba980);
		mt76_wr(dev, MT_LG_FBK_CFG0, 0xedcba988);
		mt76_wr(dev, MT_LG_FBK_CFG1, 0x00002100);
	}

	if (changed & BSS_CHANGED_BEACON_INT)
		mt76x0_mac_config_tsf(dev, true, info->beacon_int);

	if (changed & BSS_CHANGED_HT || changed & BSS_CHANGED_ERP_CTS_PROT)
		mt76x0_mac_set_protection(dev, info->use_cts_prot,
					   info->ht_operation_mode);

	if (changed & BSS_CHANGED_ERP_PREAMBLE)
		mt76x0_mac_set_short_preamble(dev, info->use_short_preamble);

	if (changed & BSS_CHANGED_ERP_SLOT) {
		int slottime = info->use_short_slot ? 9 : 20;

		mt76_rmw_field(dev, MT_BKOFF_SLOT_CFG,
			       MT_BKOFF_SLOT_CFG_SLOTTIME, slottime);
	}

	if (changed & BSS_CHANGED_ASSOC)
		mt76x0_phy_recalibrate_after_assoc(dev);

	mutex_unlock(&dev->mt76.mutex);
}

static void
mt76x0_sta_notify(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   enum sta_notify_cmd cmd, struct ieee80211_sta *sta)
{
}

static void
mt76x0_sw_scan(struct ieee80211_hw *hw,
		struct ieee80211_vif *vif,
		const u8 *mac_addr)
{
	struct mt76x0_dev *dev = hw->priv;

	cancel_delayed_work_sync(&dev->cal_work);
	mt76x0_agc_save(dev);
	set_bit(MT76_SCANNING, &dev->mt76.state);
}

static void
mt76x0_sw_scan_complete(struct ieee80211_hw *hw,
			 struct ieee80211_vif *vif)
{
	struct mt76x0_dev *dev = hw->priv;

	mt76x0_agc_restore(dev);
	clear_bit(MT76_SCANNING, &dev->mt76.state);

	ieee80211_queue_delayed_work(dev->mt76.hw, &dev->cal_work,
				     MT_CALIBRATE_INTERVAL);
}

static int
mt76x0_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
		struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		struct ieee80211_key_conf *key)
{
	struct mt76x0_dev *dev = hw->priv;
	struct mt76x02_vif *mvif = (struct mt76x02_vif *) vif->drv_priv;
	struct mt76x02_sta *msta = sta ? (struct mt76x02_sta *) sta->drv_priv : NULL;
	struct mt76_wcid *wcid = msta ? &msta->wcid : &mvif->group_wcid;
	int idx = key->keyidx;
	int ret;

	if (cmd == SET_KEY) {
		key->hw_key_idx = wcid->idx;
		wcid->hw_key_idx = idx;
	} else {
		if (idx == wcid->hw_key_idx)
			wcid->hw_key_idx = -1;

		key = NULL;
	}

	if (!msta) {
		if (key || wcid->hw_key_idx == idx) {
			ret = mt76x02_mac_wcid_set_key(&dev->mt76, wcid->idx, key);
			if (ret)
				return ret;
		}

		return mt76x02_mac_shared_key_setup(&dev->mt76, mvif->idx, idx, key);
	}

	return mt76x02_mac_wcid_set_key(&dev->mt76, msta->wcid.idx, key);
}

static int mt76x0_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	struct mt76x0_dev *dev = hw->priv;

	mt76_rmw_field(dev, MT_TX_RTS_CFG, MT_TX_RTS_CFG_THRESH, value);

	return 0;
}

static void
mt76_sta_rate_tbl_update(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta)
{
	struct mt76x0_dev *dev = hw->priv;
	struct mt76x02_sta *msta = (struct mt76x02_sta *) sta->drv_priv;
	struct ieee80211_sta_rates *rates;
	struct ieee80211_tx_rate rate = {};

	rcu_read_lock();
	rates = rcu_dereference(sta->rates);

	if (!rates)
		goto out;

	rate.idx = rates->rate[0].idx;
	rate.flags = rates->rate[0].flags;
	mt76x0_mac_wcid_set_rate(dev, &msta->wcid, &rate);

out:
	rcu_read_unlock();
}

const struct ieee80211_ops mt76x0_ops = {
	.tx = mt76x0_tx,
	.start = mt76x0_start,
	.stop = mt76x0_stop,
	.add_interface = mt76x0_add_interface,
	.remove_interface = mt76x0_remove_interface,
	.config = mt76x0_config,
	.configure_filter = mt76x02_configure_filter,
	.bss_info_changed = mt76x0_bss_info_changed,
	.sta_add = mt76x02_sta_add,
	.sta_remove = mt76x02_sta_remove,
	.sta_notify = mt76x0_sta_notify,
	.set_key = mt76x0_set_key,
	.conf_tx = mt76x0_conf_tx,
	.sw_scan_start = mt76x0_sw_scan,
	.sw_scan_complete = mt76x0_sw_scan_complete,
	.ampdu_action = mt76x02_ampdu_action,
	.sta_rate_tbl_update = mt76_sta_rate_tbl_update,
	.set_rts_threshold = mt76x0_set_rts_threshold,
};
