// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 * Copyright (C) 2018 Stanislaw Gruszka <stf_xl@wp.pl>
 */

#include <linux/etherdevice.h>
#include "mt76x0.h"

static int
mt76x0_set_channel(struct mt76x02_dev *dev, struct cfg80211_chan_def *chandef)
{
	int ret;

	cancel_delayed_work_sync(&dev->cal_work);
	dev->beacon_ops->pre_tbtt_enable(dev, false);
	if (mt76_is_mmio(dev))
		tasklet_disable(&dev->dfs_pd.dfs_tasklet);

	mt76_set_channel(&dev->mt76);
	ret = mt76x0_phy_set_channel(dev, chandef);

	/* channel cycle counters read-and-clear */
	mt76_rr(dev, MT_CH_IDLE);
	mt76_rr(dev, MT_CH_BUSY);

	mt76x02_edcca_init(dev, true);

	if (mt76_is_mmio(dev)) {
		mt76x02_dfs_init_params(dev);
		tasklet_enable(&dev->dfs_pd.dfs_tasklet);
	}
	dev->beacon_ops->pre_tbtt_enable(dev, true);

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
