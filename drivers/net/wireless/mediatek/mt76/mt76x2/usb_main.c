// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 */

#include "mt76x2u.h"
#include "../mt76x02_usb.h"

static int mt76x2u_start(struct ieee80211_hw *hw)
{
	struct mt76x02_dev *dev = hw->priv;
	int ret;

	ret = mt76x02u_mac_start(dev);
	if (ret)
		return ret;

	ieee80211_queue_delayed_work(mt76_hw(dev), &dev->mphy.mac_work,
				     MT_MAC_WORK_INTERVAL);
	set_bit(MT76_STATE_RUNNING, &dev->mphy.state);

	return 0;
}

static void mt76x2u_stop(struct ieee80211_hw *hw, bool suspend)
{
	struct mt76x02_dev *dev = hw->priv;

	clear_bit(MT76_STATE_RUNNING, &dev->mphy.state);
	mt76u_stop_tx(&dev->mt76);
	mt76x2u_stop_hw(dev);
}

int mt76x2u_set_channel(struct mt76_phy *mphy)
{
	struct mt76x02_dev *dev = container_of(mphy->dev, struct mt76x02_dev, mt76);
	int err;

	mt76x02_pre_tbtt_enable(dev, false);
	mt76x2_mac_stop(dev, false);

	err = mt76x2u_phy_set_channel(dev, &mphy->chandef);

	mt76x02_mac_cc_reset(dev);
	mt76x2_mac_resume(dev);

	mt76x02_pre_tbtt_enable(dev, true);

	return err;
}

static int
mt76x2u_config(struct ieee80211_hw *hw, u32 changed)
{
	struct mt76x02_dev *dev = hw->priv;
	int err = 0;

	mutex_lock(&dev->mt76.mutex);

	if (changed & IEEE80211_CONF_CHANGE_MONITOR) {
		if (!(hw->conf.flags & IEEE80211_CONF_MONITOR))
			dev->mt76.rxfilter |= MT_RX_FILTR_CFG_PROMISC;
		else
			dev->mt76.rxfilter &= ~MT_RX_FILTR_CFG_PROMISC;
		mt76_wr(dev, MT_RX_FILTR_CFG, dev->mt76.rxfilter);
	}

	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		struct mt76_phy *mphy = &dev->mphy;

		dev->txpower_conf = hw->conf.power_level * 2;
		dev->txpower_conf = mt76_get_sar_power(mphy,
						       mphy->chandef.chan,
						       dev->txpower_conf);
		/* convert to per-chain power for 2x2 devices */
		dev->txpower_conf -= 6;

		if (test_bit(MT76_STATE_RUNNING, &mphy->state))
			mt76x2_phy_set_txpower(dev);
	}

	mutex_unlock(&dev->mt76.mutex);

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL)
		mt76_update_channel(&dev->mphy);

	return err;
}

const struct ieee80211_ops mt76x2u_ops = {
	.add_chanctx = ieee80211_emulate_add_chanctx,
	.remove_chanctx = ieee80211_emulate_remove_chanctx,
	.change_chanctx = ieee80211_emulate_change_chanctx,
	.switch_vif_chanctx = ieee80211_emulate_switch_vif_chanctx,
	.tx = mt76x02_tx,
	.start = mt76x2u_start,
	.stop = mt76x2u_stop,
	.add_interface = mt76x02_add_interface,
	.remove_interface = mt76x02_remove_interface,
	.sta_state = mt76_sta_state,
	.sta_pre_rcu_remove = mt76_sta_pre_rcu_remove,
	.set_key = mt76x02_set_key,
	.ampdu_action = mt76x02_ampdu_action,
	.config = mt76x2u_config,
	.wake_tx_queue = mt76_wake_tx_queue,
	.bss_info_changed = mt76x02_bss_info_changed,
	.configure_filter = mt76x02_configure_filter,
	.conf_tx = mt76x02_conf_tx,
	.sw_scan_start = mt76_sw_scan,
	.sw_scan_complete = mt76x02_sw_scan_complete,
	.sta_rate_tbl_update = mt76x02_sta_rate_tbl_update,
	.get_txpower = mt76_get_txpower,
	.get_survey = mt76_get_survey,
	.set_tim = mt76_set_tim,
	.release_buffered_frames = mt76_release_buffered_frames,
	.get_antenna = mt76_get_antenna,
	.set_sar_specs = mt76x2_set_sar_specs,
};
