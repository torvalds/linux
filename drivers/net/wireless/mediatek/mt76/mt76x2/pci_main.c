/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
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

#include "mt76x2.h"

static int
mt76x2_start(struct ieee80211_hw *hw)
{
	struct mt76x02_dev *dev = hw->priv;
	int ret;

	mutex_lock(&dev->mt76.mutex);

	ret = mt76x2_mac_start(dev);
	if (ret)
		goto out;

	ret = mt76x2_phy_start(dev);
	if (ret)
		goto out;

	ieee80211_queue_delayed_work(mt76_hw(dev), &dev->mac_work,
				     MT_CALIBRATE_INTERVAL);

	set_bit(MT76_STATE_RUNNING, &dev->mt76.state);

out:
	mutex_unlock(&dev->mt76.mutex);
	return ret;
}

static void
mt76x2_stop(struct ieee80211_hw *hw)
{
	struct mt76x02_dev *dev = hw->priv;

	mutex_lock(&dev->mt76.mutex);
	clear_bit(MT76_STATE_RUNNING, &dev->mt76.state);
	mt76x2_stop_hardware(dev);
	mutex_unlock(&dev->mt76.mutex);
}

static int
mt76x2_set_channel(struct mt76x02_dev *dev, struct cfg80211_chan_def *chandef)
{
	int ret;

	cancel_delayed_work_sync(&dev->cal_work);

	set_bit(MT76_RESET, &dev->mt76.state);

	mt76_set_channel(&dev->mt76);

	tasklet_disable(&dev->pre_tbtt_tasklet);
	tasklet_disable(&dev->dfs_pd.dfs_tasklet);

	mt76x2_mac_stop(dev, true);
	ret = mt76x2_phy_set_channel(dev, chandef);

	/* channel cycle counters read-and-clear */
	mt76_rr(dev, MT_CH_IDLE);
	mt76_rr(dev, MT_CH_BUSY);

	mt76x02_dfs_init_params(dev);

	mt76x2_mac_resume(dev);
	tasklet_enable(&dev->dfs_pd.dfs_tasklet);
	tasklet_enable(&dev->pre_tbtt_tasklet);

	clear_bit(MT76_RESET, &dev->mt76.state);

	mt76_txq_schedule_all(&dev->mt76);

	return ret;
}

static int
mt76x2_config(struct ieee80211_hw *hw, u32 changed)
{
	struct mt76x02_dev *dev = hw->priv;
	int ret = 0;

	mutex_lock(&dev->mt76.mutex);

	if (changed & IEEE80211_CONF_CHANGE_MONITOR) {
		if (!(hw->conf.flags & IEEE80211_CONF_MONITOR))
			dev->mt76.rxfilter |= MT_RX_FILTR_CFG_PROMISC;
		else
			dev->mt76.rxfilter &= ~MT_RX_FILTR_CFG_PROMISC;

		mt76_wr(dev, MT_RX_FILTR_CFG, dev->mt76.rxfilter);
	}

	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		dev->mt76.txpower_conf = hw->conf.power_level * 2;

		/* convert to per-chain power for 2x2 devices */
		dev->mt76.txpower_conf -= 6;

		if (test_bit(MT76_STATE_RUNNING, &dev->mt76.state)) {
			mt76x2_phy_set_txpower(dev);
			mt76x02_tx_set_txpwr_auto(dev, dev->mt76.txpower_conf);
		}
	}

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		ieee80211_stop_queues(hw);
		ret = mt76x2_set_channel(dev, &hw->conf.chandef);
		ieee80211_wake_queues(hw);
	}

	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static void
mt76x2_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	     u32 queues, bool drop)
{
}

static int
mt76x2_set_tim(struct ieee80211_hw *hw, struct ieee80211_sta *sta, bool set)
{
	return 0;
}

static int mt76x2_set_antenna(struct ieee80211_hw *hw, u32 tx_ant,
			      u32 rx_ant)
{
	struct mt76x02_dev *dev = hw->priv;

	if (!tx_ant || tx_ant > 3 || tx_ant != rx_ant)
		return -EINVAL;

	mutex_lock(&dev->mt76.mutex);

	dev->mt76.chainmask = (tx_ant == 3) ? 0x202 : 0x101;
	dev->mt76.antenna_mask = tx_ant;

	mt76_set_stream_caps(&dev->mt76, true);
	mt76x2_phy_set_antenna(dev);

	mutex_unlock(&dev->mt76.mutex);

	return 0;
}

static int mt76x2_get_antenna(struct ieee80211_hw *hw, u32 *tx_ant,
			      u32 *rx_ant)
{
	struct mt76x02_dev *dev = hw->priv;

	mutex_lock(&dev->mt76.mutex);
	*tx_ant = dev->mt76.antenna_mask;
	*rx_ant = dev->mt76.antenna_mask;
	mutex_unlock(&dev->mt76.mutex);

	return 0;
}

const struct ieee80211_ops mt76x2_ops = {
	.tx = mt76x02_tx,
	.start = mt76x2_start,
	.stop = mt76x2_stop,
	.add_interface = mt76x02_add_interface,
	.remove_interface = mt76x02_remove_interface,
	.config = mt76x2_config,
	.configure_filter = mt76x02_configure_filter,
	.bss_info_changed = mt76x02_bss_info_changed,
	.sta_state = mt76_sta_state,
	.set_key = mt76x02_set_key,
	.conf_tx = mt76x02_conf_tx,
	.sw_scan_start = mt76x02_sw_scan,
	.sw_scan_complete = mt76x02_sw_scan_complete,
	.flush = mt76x2_flush,
	.ampdu_action = mt76x02_ampdu_action,
	.get_txpower = mt76x02_get_txpower,
	.wake_tx_queue = mt76_wake_tx_queue,
	.sta_rate_tbl_update = mt76x02_sta_rate_tbl_update,
	.release_buffered_frames = mt76_release_buffered_frames,
	.set_coverage_class = mt76x02_set_coverage_class,
	.get_survey = mt76_get_survey,
	.set_tim = mt76x2_set_tim,
	.set_antenna = mt76x2_set_antenna,
	.get_antenna = mt76x2_get_antenna,
	.set_rts_threshold = mt76x02_set_rts_threshold,
};

