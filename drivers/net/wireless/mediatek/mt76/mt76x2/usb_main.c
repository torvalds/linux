/*
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "mt76x2u.h"
#include "../mt76x02_util.h"

static int mt76x2u_start(struct ieee80211_hw *hw)
{
	struct mt76x02_dev *dev = hw->priv;
	int ret;

	mutex_lock(&dev->mt76.mutex);

	ret = mt76x2u_mac_start(dev);
	if (ret)
		goto out;

	set_bit(MT76_STATE_RUNNING, &dev->mt76.state);

out:
	mutex_unlock(&dev->mt76.mutex);
	return ret;
}

static void mt76x2u_stop(struct ieee80211_hw *hw)
{
	struct mt76x02_dev *dev = hw->priv;

	mutex_lock(&dev->mt76.mutex);
	clear_bit(MT76_STATE_RUNNING, &dev->mt76.state);
	mt76x2u_stop_hw(dev);
	mutex_unlock(&dev->mt76.mutex);
}

static int mt76x2u_add_interface(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif)
{
	struct mt76x02_dev *dev = hw->priv;

	if (!ether_addr_equal(dev->mt76.macaddr, vif->addr))
		mt76x02_mac_setaddr(&dev->mt76, vif->addr);

	mt76x02_vif_init(&dev->mt76, vif, 0);
	return 0;
}

static int
mt76x2u_set_channel(struct mt76x02_dev *dev,
		    struct cfg80211_chan_def *chandef)
{
	int err;

	cancel_delayed_work_sync(&dev->cal_work);
	set_bit(MT76_RESET, &dev->mt76.state);

	mt76_set_channel(&dev->mt76);

	mt76_clear(dev, MT_TXOP_CTRL_CFG, BIT(20));
	mt76_clear(dev, MT_TXOP_HLDR_ET, BIT(1));
	mt76x2_mac_stop(dev, false);

	err = mt76x2u_phy_set_channel(dev, chandef);

	mt76x2u_mac_resume(dev);

	clear_bit(MT76_RESET, &dev->mt76.state);
	mt76_txq_schedule_all(&dev->mt76);

	return err;
}

static void
mt76x2u_bss_info_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			 struct ieee80211_bss_conf *info, u32 changed)
{
	struct mt76x02_dev *dev = hw->priv;

	mutex_lock(&dev->mt76.mutex);

	if (changed & BSS_CHANGED_ASSOC) {
		mt76x2u_phy_channel_calibrate(dev);
		mt76x2_apply_gain_adj(dev);
	}

	if (changed & BSS_CHANGED_BSSID) {
		mt76_wr(dev, MT_MAC_BSSID_DW0,
			get_unaligned_le32(info->bssid));
		mt76_wr(dev, MT_MAC_BSSID_DW1,
			get_unaligned_le16(info->bssid + 4));
	}

	mutex_unlock(&dev->mt76.mutex);
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

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		ieee80211_stop_queues(hw);
		err = mt76x2u_set_channel(dev, &hw->conf.chandef);
		ieee80211_wake_queues(hw);
	}

	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		dev->mt76.txpower_conf = hw->conf.power_level * 2;

		/* convert to per-chain power for 2x2 devices */
		dev->mt76.txpower_conf -= 6;

		if (test_bit(MT76_STATE_RUNNING, &dev->mt76.state))
			mt76x2_phy_set_txpower(dev);
	}

	mutex_unlock(&dev->mt76.mutex);

	return err;
}

static void
mt76x2u_sw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		const u8 *mac)
{
	struct mt76x02_dev *dev = hw->priv;

	set_bit(MT76_SCANNING, &dev->mt76.state);
}

static void
mt76x2u_sw_scan_complete(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt76x02_dev *dev = hw->priv;

	clear_bit(MT76_SCANNING, &dev->mt76.state);
}

const struct ieee80211_ops mt76x2u_ops = {
	.tx = mt76x02_tx,
	.start = mt76x2u_start,
	.stop = mt76x2u_stop,
	.add_interface = mt76x2u_add_interface,
	.remove_interface = mt76x02_remove_interface,
	.sta_add = mt76x02_sta_add,
	.sta_remove = mt76x02_sta_remove,
	.set_key = mt76x02_set_key,
	.ampdu_action = mt76x02_ampdu_action,
	.config = mt76x2u_config,
	.wake_tx_queue = mt76_wake_tx_queue,
	.bss_info_changed = mt76x2u_bss_info_changed,
	.configure_filter = mt76x02_configure_filter,
	.conf_tx = mt76x02_conf_tx,
	.sw_scan_start = mt76x2u_sw_scan,
	.sw_scan_complete = mt76x2u_sw_scan_complete,
	.sta_rate_tbl_update = mt76x02_sta_rate_tbl_update,
};
