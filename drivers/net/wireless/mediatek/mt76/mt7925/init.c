// SPDX-License-Identifier: ISC
/* Copyright (C) 2023 MediaTek Inc. */

#include <linux/etherdevice.h>
#include <linux/firmware.h>
#include "mt7925.h"
#include "mac.h"
#include "mcu.h"

static void
mt7925_regd_notifier(struct wiphy *wiphy,
		     struct regulatory_request *req)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct mt76_dev *mdev = &dev->mt76;

	/* allow world regdom at the first boot only */
	if (!memcmp(req->alpha2, "00", 2) &&
	    mdev->alpha2[0] && mdev->alpha2[1])
		return;

	/* do not need to update the same country twice */
	if (!memcmp(req->alpha2, mdev->alpha2, 2) &&
	    dev->country_ie_env == req->country_ie_env)
		return;

	memcpy(mdev->alpha2, req->alpha2, 2);
	mdev->region = req->dfs_region;
	dev->country_ie_env = req->country_ie_env;

	mt792x_mutex_acquire(dev);
	mt7925_mcu_set_clc(dev, req->alpha2, req->country_ie_env);
	mt7925_mcu_set_channel_domain(hw->priv);
	mt7925_set_tx_sar_pwr(hw, NULL);
	mt792x_mutex_release(dev);
}

static void mt7925_mac_init_basic_rates(struct mt792x_dev *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mt76_rates); i++) {
		u16 rate = mt76_rates[i].hw_value;
		u16 idx = MT792x_BASIC_RATES_TBL + i;

		rate = FIELD_PREP(MT_TX_RATE_MODE, rate >> 8) |
		       FIELD_PREP(MT_TX_RATE_IDX, rate & GENMASK(7, 0));
		mt7925_mac_set_fixed_rate_table(dev, idx, rate);
	}
}

int mt7925_mac_init(struct mt792x_dev *dev)
{
	int i;

	mt76_rmw_field(dev, MT_MDP_DCR1, MT_MDP_DCR1_MAX_RX_LEN, 1536);
	/* enable hardware de-agg */
	mt76_set(dev, MT_MDP_DCR0, MT_MDP_DCR0_DAMSDU_EN);

	for (i = 0; i < MT792x_WTBL_SIZE; i++)
		mt7925_mac_wtbl_update(dev, i,
				       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
	for (i = 0; i < 2; i++)
		mt792x_mac_init_band(dev, i);

	mt7925_mac_init_basic_rates(dev);

	memzero_explicit(&dev->mt76.alpha2, sizeof(dev->mt76.alpha2));

	return 0;
}
EXPORT_SYMBOL_GPL(mt7925_mac_init);

static int __mt7925_init_hardware(struct mt792x_dev *dev)
{
	int ret;

	ret = mt792x_mcu_init(dev);
	if (ret)
		goto out;

	mt76_eeprom_override(&dev->mphy);

	ret = mt7925_mcu_set_eeprom(dev);
	if (ret)
		goto out;

	ret = mt7925_mac_init(dev);
	if (ret)
		goto out;

out:
	return ret;
}

static int mt7925_init_hardware(struct mt792x_dev *dev)
{
	int ret, i;

	set_bit(MT76_STATE_INITIALIZED, &dev->mphy.state);

	for (i = 0; i < MT792x_MCU_INIT_RETRY_COUNT; i++) {
		ret = __mt7925_init_hardware(dev);
		if (!ret)
			break;

		mt792x_init_reset(dev);
	}

	if (i == MT792x_MCU_INIT_RETRY_COUNT) {
		dev_err(dev->mt76.dev, "hardware init failed\n");
		return ret;
	}

	return 0;
}

static void mt7925_init_work(struct work_struct *work)
{
	struct mt792x_dev *dev = container_of(work, struct mt792x_dev,
					      init_work);
	int ret;

	ret = mt7925_init_hardware(dev);
	if (ret)
		return;

	mt76_set_stream_caps(&dev->mphy, true);
	mt7925_set_stream_he_eht_caps(&dev->phy);

	ret = mt76_register_device(&dev->mt76, true, mt76_rates,
				   ARRAY_SIZE(mt76_rates));
	if (ret) {
		dev_err(dev->mt76.dev, "register device failed\n");
		return;
	}

	ret = mt7925_init_debugfs(dev);
	if (ret) {
		dev_err(dev->mt76.dev, "register debugfs failed\n");
		return;
	}

	/* we support chip reset now */
	dev->hw_init_done = true;

	mt7925_mcu_set_deep_sleep(dev, dev->pm.ds_enable);
}

int mt7925_register_device(struct mt792x_dev *dev)
{
	struct ieee80211_hw *hw = mt76_hw(dev);
	int ret;

	dev->phy.dev = dev;
	dev->phy.mt76 = &dev->mt76.phy;
	dev->mt76.phy.priv = &dev->phy;
	dev->mt76.tx_worker.fn = mt792x_tx_worker;

	INIT_DELAYED_WORK(&dev->pm.ps_work, mt792x_pm_power_save_work);
	INIT_WORK(&dev->pm.wake_work, mt792x_pm_wake_work);
	spin_lock_init(&dev->pm.wake.lock);
	mutex_init(&dev->pm.mutex);
	init_waitqueue_head(&dev->pm.wait);
	spin_lock_init(&dev->pm.txq_lock);
	INIT_DELAYED_WORK(&dev->mphy.mac_work, mt792x_mac_work);
	INIT_DELAYED_WORK(&dev->phy.scan_work, mt7925_scan_work);
	INIT_DELAYED_WORK(&dev->coredump.work, mt7925_coredump_work);
#if IS_ENABLED(CONFIG_IPV6)
	INIT_WORK(&dev->ipv6_ns_work, mt7925_set_ipv6_ns_work);
	skb_queue_head_init(&dev->ipv6_ns_list);
#endif
	skb_queue_head_init(&dev->phy.scan_event_list);
	skb_queue_head_init(&dev->coredump.msg_list);

	INIT_WORK(&dev->reset_work, mt7925_mac_reset_work);
	INIT_WORK(&dev->init_work, mt7925_init_work);

	INIT_WORK(&dev->phy.roc_work, mt7925_roc_work);
	timer_setup(&dev->phy.roc_timer, mt792x_roc_timer, 0);
	init_waitqueue_head(&dev->phy.roc_wait);

	dev->pm.idle_timeout = MT792x_PM_TIMEOUT;
	dev->pm.stats.last_wake_event = jiffies;
	dev->pm.stats.last_doze_event = jiffies;
	if (!mt76_is_usb(&dev->mt76)) {
		dev->pm.enable_user = true;
		dev->pm.enable = true;
		dev->pm.ds_enable_user = true;
		dev->pm.ds_enable = true;
	}

	if (!mt76_is_mmio(&dev->mt76))
		hw->extra_tx_headroom += MT_SDIO_TXD_SIZE + MT_SDIO_HDR_SIZE;

	mt792x_init_acpi_sar(dev);

	ret = mt792x_init_wcid(dev);
	if (ret)
		return ret;

	ret = mt792x_init_wiphy(hw);
	if (ret)
		return ret;

	hw->wiphy->reg_notifier = mt7925_regd_notifier;
	dev->mphy.sband_2g.sband.ht_cap.cap |=
			IEEE80211_HT_CAP_LDPC_CODING |
			IEEE80211_HT_CAP_MAX_AMSDU;
	dev->mphy.sband_2g.sband.ht_cap.ampdu_density =
			IEEE80211_HT_MPDU_DENSITY_2;
	dev->mphy.sband_5g.sband.ht_cap.cap |=
			IEEE80211_HT_CAP_LDPC_CODING |
			IEEE80211_HT_CAP_MAX_AMSDU;
	dev->mphy.sband_2g.sband.ht_cap.ampdu_density =
			IEEE80211_HT_MPDU_DENSITY_1;
	dev->mphy.sband_5g.sband.vht_cap.cap |=
			IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454 |
			IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK |
			IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE |
			IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE |
			(3 << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT);
	dev->mphy.sband_5g.sband.vht_cap.cap |=
			IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ |
			IEEE80211_VHT_CAP_SHORT_GI_160;

	dev->mphy.hw->wiphy->available_antennas_rx = dev->mphy.chainmask;
	dev->mphy.hw->wiphy->available_antennas_tx = dev->mphy.chainmask;

	queue_work(system_wq, &dev->init_work);

	return 0;
}
EXPORT_SYMBOL_GPL(mt7925_register_device);
