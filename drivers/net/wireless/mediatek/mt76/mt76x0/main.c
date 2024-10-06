// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 * Copyright (C) 2018 Stanislaw Gruszka <stf_xl@wp.pl>
 */

#include <linux/etherdevice.h>
#include "mt76x0.h"

int mt76x0_set_channel(struct mt76_phy *mphy)
{
	struct mt76x02_dev *dev = container_of(mphy->dev, struct mt76x02_dev, mt76);

	mt76x02_pre_tbtt_enable(dev, false);
	if (mt76_is_mmio(&dev->mt76))
		tasklet_disable(&dev->dfs_pd.dfs_tasklet);

	mt76x0_phy_set_channel(dev, &mphy->chandef);

	mt76x02_mac_cc_reset(dev);
	mt76x02_edcca_init(dev);

	if (mt76_is_mmio(&dev->mt76)) {
		mt76x02_dfs_init_params(dev);
		tasklet_enable(&dev->dfs_pd.dfs_tasklet);
	}
	mt76x02_pre_tbtt_enable(dev, true);

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x0_set_channel);

int mt76x0_set_sar_specs(struct ieee80211_hw *hw,
			 const struct cfg80211_sar_specs *sar)
{
	int err = -EINVAL, power = hw->conf.power_level * 2;
	struct mt76x02_dev *dev = hw->priv;
	struct mt76_phy *mphy = &dev->mphy;

	mutex_lock(&dev->mt76.mutex);
	if (!cfg80211_chandef_valid(&mphy->chandef))
		goto out;

	err = mt76_init_sar_power(hw, sar);
	if (err)
		goto out;

	dev->txpower_conf = mt76_get_sar_power(mphy, mphy->chandef.chan,
					       power);
	if (test_bit(MT76_STATE_RUNNING, &mphy->state))
		mt76x0_phy_set_txpower(dev);
out:
	mutex_unlock(&dev->mt76.mutex);

	return err;
}
EXPORT_SYMBOL_GPL(mt76x0_set_sar_specs);

int mt76x0_config(struct ieee80211_hw *hw, u32 changed)
{
	struct mt76x02_dev *dev = hw->priv;

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL)
		mt76_update_channel(&dev->mphy);

	mutex_lock(&dev->mt76.mutex);

	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		struct mt76_phy *mphy = &dev->mphy;

		dev->txpower_conf = hw->conf.power_level * 2;
		dev->txpower_conf = mt76_get_sar_power(mphy,
						       mphy->chandef.chan,
						       dev->txpower_conf);
		if (test_bit(MT76_STATE_RUNNING, &mphy->state))
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

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x0_config);
