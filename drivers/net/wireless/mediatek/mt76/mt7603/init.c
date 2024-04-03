// SPDX-License-Identifier: ISC

#include <linux/etherdevice.h>
#include "mt7603.h"
#include "mac.h"
#include "eeprom.h"

const struct mt76_driver_ops mt7603_drv_ops = {
	.txwi_size = MT_TXD_SIZE,
	.drv_flags = MT_DRV_SW_RX_AIRTIME,
	.survey_flags = SURVEY_INFO_TIME_TX,
	.tx_prepare_skb = mt7603_tx_prepare_skb,
	.tx_complete_skb = mt7603_tx_complete_skb,
	.rx_skb = mt7603_queue_rx_skb,
	.rx_poll_complete = mt7603_rx_poll_complete,
	.sta_ps = mt7603_sta_ps,
	.sta_add = mt7603_sta_add,
	.sta_assoc = mt7603_sta_assoc,
	.sta_remove = mt7603_sta_remove,
	.update_survey = mt7603_update_channel,
};

static void
mt7603_set_tmac_template(struct mt7603_dev *dev)
{
	u32 desc[5] = {
		[1] = FIELD_PREP(MT_TXD3_REM_TX_COUNT, 0xf),
		[3] = MT_TXD5_SW_POWER_MGMT
	};
	u32 addr;
	int i;

	addr = mt7603_reg_map(dev, MT_CLIENT_BASE_PHYS_ADDR);
	addr += MT_CLIENT_TMAC_INFO_TEMPLATE;
	for (i = 0; i < ARRAY_SIZE(desc); i++)
		mt76_wr(dev, addr + 4 * i, desc[i]);
}

static void
mt7603_dma_sched_init(struct mt7603_dev *dev)
{
	int page_size = 128;
	int page_count;
	int max_len = 1792;
	int max_amsdu_pages = 4096 / page_size;
	int max_mcu_len = 4096;
	int max_beacon_len = 512 * 4 + max_len;
	int max_mcast_pages = 4 * max_len / page_size;
	int reserved_count = 0;
	int beacon_pages;
	int mcu_pages;
	int i;

	page_count = mt76_get_field(dev, MT_PSE_FC_P0,
				    MT_PSE_FC_P0_MAX_QUOTA);
	beacon_pages = 4 * (max_beacon_len / page_size);
	mcu_pages = max_mcu_len / page_size;

	mt76_wr(dev, MT_PSE_FRP,
		FIELD_PREP(MT_PSE_FRP_P0, 7) |
		FIELD_PREP(MT_PSE_FRP_P1, 6) |
		FIELD_PREP(MT_PSE_FRP_P2_RQ2, 4));

	mt76_wr(dev, MT_HIGH_PRIORITY_1, 0x55555553);
	mt76_wr(dev, MT_HIGH_PRIORITY_2, 0x78555555);

	mt76_wr(dev, MT_QUEUE_PRIORITY_1, 0x2b1a096e);
	mt76_wr(dev, MT_QUEUE_PRIORITY_2, 0x785f4d3c);

	mt76_wr(dev, MT_PRIORITY_MASK, 0xffffffff);

	mt76_wr(dev, MT_SCH_1, page_count | (2 << 28));
	mt76_wr(dev, MT_SCH_2, max_amsdu_pages);

	for (i = 0; i <= 4; i++)
		mt76_wr(dev, MT_PAGE_COUNT(i), max_amsdu_pages);
	reserved_count += 5 * max_amsdu_pages;

	mt76_wr(dev, MT_PAGE_COUNT(5), mcu_pages);
	reserved_count += mcu_pages;

	mt76_wr(dev, MT_PAGE_COUNT(7), beacon_pages);
	reserved_count += beacon_pages;

	mt76_wr(dev, MT_PAGE_COUNT(8), max_mcast_pages);
	reserved_count += max_mcast_pages;

	if (is_mt7603(dev))
		reserved_count = 0;

	mt76_wr(dev, MT_RSV_MAX_THRESH, page_count - reserved_count);

	if (is_mt7603(dev) && mt76xx_rev(dev) >= MT7603_REV_E2) {
		mt76_wr(dev, MT_GROUP_THRESH(0),
			page_count - beacon_pages - mcu_pages);
		mt76_wr(dev, MT_GROUP_THRESH(1), beacon_pages);
		mt76_wr(dev, MT_BMAP_0, 0x0080ff5f);
		mt76_wr(dev, MT_GROUP_THRESH(2), mcu_pages);
		mt76_wr(dev, MT_BMAP_1, 0x00000020);
	} else {
		mt76_wr(dev, MT_GROUP_THRESH(0), page_count);
		mt76_wr(dev, MT_BMAP_0, 0xffff);
	}

	mt76_wr(dev, MT_SCH_4, 0);

	for (i = 0; i <= 15; i++)
		mt76_wr(dev, MT_TXTIME_THRESH(i), 0xfffff);

	mt76_set(dev, MT_SCH_4, BIT(6));
}

static void
mt7603_phy_init(struct mt7603_dev *dev)
{
	int rx_chains = dev->mphy.antenna_mask;
	int tx_chains = hweight8(rx_chains) - 1;

	mt76_rmw(dev, MT_WF_RMAC_RMCR,
		 (MT_WF_RMAC_RMCR_SMPS_MODE |
		  MT_WF_RMAC_RMCR_RX_STREAMS),
		 (FIELD_PREP(MT_WF_RMAC_RMCR_SMPS_MODE, 3) |
		  FIELD_PREP(MT_WF_RMAC_RMCR_RX_STREAMS, rx_chains)));

	mt76_rmw_field(dev, MT_TMAC_TCR, MT_TMAC_TCR_TX_STREAMS,
		       tx_chains);

	dev->agc0 = mt76_rr(dev, MT_AGC(0));
	dev->agc3 = mt76_rr(dev, MT_AGC(3));
}

static void
mt7603_mac_init(struct mt7603_dev *dev)
{
	u8 bc_addr[ETH_ALEN];
	u32 addr;
	int i;

	mt76_wr(dev, MT_AGG_BA_SIZE_LIMIT_0,
		(MT_AGG_SIZE_LIMIT(0) << 0 * MT_AGG_BA_SIZE_LIMIT_SHIFT) |
		(MT_AGG_SIZE_LIMIT(1) << 1 * MT_AGG_BA_SIZE_LIMIT_SHIFT) |
		(MT_AGG_SIZE_LIMIT(2) << 2 * MT_AGG_BA_SIZE_LIMIT_SHIFT) |
		(MT_AGG_SIZE_LIMIT(3) << 3 * MT_AGG_BA_SIZE_LIMIT_SHIFT));

	mt76_wr(dev, MT_AGG_BA_SIZE_LIMIT_1,
		(MT_AGG_SIZE_LIMIT(4) << 0 * MT_AGG_BA_SIZE_LIMIT_SHIFT) |
		(MT_AGG_SIZE_LIMIT(5) << 1 * MT_AGG_BA_SIZE_LIMIT_SHIFT) |
		(MT_AGG_SIZE_LIMIT(6) << 2 * MT_AGG_BA_SIZE_LIMIT_SHIFT) |
		(MT_AGG_SIZE_LIMIT(7) << 3 * MT_AGG_BA_SIZE_LIMIT_SHIFT));

	mt76_wr(dev, MT_AGG_LIMIT,
		FIELD_PREP(MT_AGG_LIMIT_AC(0), 24) |
		FIELD_PREP(MT_AGG_LIMIT_AC(1), 24) |
		FIELD_PREP(MT_AGG_LIMIT_AC(2), 24) |
		FIELD_PREP(MT_AGG_LIMIT_AC(3), 24));

	mt76_wr(dev, MT_AGG_LIMIT_1,
		FIELD_PREP(MT_AGG_LIMIT_AC(0), 24) |
		FIELD_PREP(MT_AGG_LIMIT_AC(1), 24) |
		FIELD_PREP(MT_AGG_LIMIT_AC(2), 24) |
		FIELD_PREP(MT_AGG_LIMIT_AC(3), 24));

	mt76_wr(dev, MT_AGG_CONTROL,
		FIELD_PREP(MT_AGG_CONTROL_BAR_RATE, 0x4b) |
		FIELD_PREP(MT_AGG_CONTROL_CFEND_RATE, 0x69) |
		MT_AGG_CONTROL_NO_BA_AR_RULE);

	mt76_wr(dev, MT_AGG_RETRY_CONTROL,
		FIELD_PREP(MT_AGG_RETRY_CONTROL_BAR_LIMIT, 1) |
		FIELD_PREP(MT_AGG_RETRY_CONTROL_RTS_LIMIT, 15));

	mt76_wr(dev, MT_DMA_DCR0, MT_DMA_DCR0_RX_VEC_DROP |
		FIELD_PREP(MT_DMA_DCR0_MAX_RX_LEN, 4096));

	mt76_rmw(dev, MT_DMA_VCFR0, BIT(0), BIT(13));
	mt76_rmw(dev, MT_DMA_TMCFR0, BIT(0) | BIT(1), BIT(13));

	mt76_clear(dev, MT_WF_RMAC_TMR_PA, BIT(31));

	mt76_set(dev, MT_WF_RMACDR, MT_WF_RMACDR_MAXLEN_20BIT);
	mt76_rmw(dev, MT_WF_RMAC_MAXMINLEN, 0xffffff, 0x19000);

	mt76_wr(dev, MT_WF_RFCR1, 0);

	mt76_set(dev, MT_TMAC_TCR, MT_TMAC_TCR_RX_RIFS_MODE);

	if (is_mt7628(dev)) {
		mt76_set(dev, MT_TMAC_TCR,
			 MT_TMAC_TCR_TXOP_BURST_STOP | BIT(1) | BIT(0));
		mt76_set(dev, MT_TXREQ, BIT(27));
		mt76_set(dev, MT_AGG_TMP, GENMASK(4, 2));
	}

	mt7603_set_tmac_template(dev);

	/* Enable RX group to HIF */
	addr = mt7603_reg_map(dev, MT_CLIENT_BASE_PHYS_ADDR);
	mt76_set(dev, addr + MT_CLIENT_RXINF, MT_CLIENT_RXINF_RXSH_GROUPS);

	/* Enable RX group to MCU */
	mt76_set(dev, MT_DMA_DCR1, GENMASK(13, 11));

	mt76_rmw_field(dev, MT_AGG_PCR_RTS, MT_AGG_PCR_RTS_PKT_THR, 3);
	mt76_set(dev, MT_TMAC_PCR, MT_TMAC_PCR_SPE_EN);

	/* include preamble detection in CCA trigger signal */
	mt76_rmw_field(dev, MT_TXREQ, MT_TXREQ_CCA_SRC_SEL, 2);

	mt76_wr(dev, MT_RXREQ, 4);

	/* Configure all rx packets to HIF */
	mt76_wr(dev, MT_DMA_RCFR0, 0xc0000000);

	/* Configure MCU txs selection with aggregation */
	mt76_wr(dev, MT_DMA_TCFR0,
		FIELD_PREP(MT_DMA_TCFR_TXS_AGGR_TIMEOUT, 1) | /* 32 us */
		MT_DMA_TCFR_TXS_AGGR_COUNT);

	/* Configure HIF txs selection with aggregation */
	mt76_wr(dev, MT_DMA_TCFR1,
		FIELD_PREP(MT_DMA_TCFR_TXS_AGGR_TIMEOUT, 1) | /* 32 us */
		MT_DMA_TCFR_TXS_AGGR_COUNT | /* Maximum count */
		MT_DMA_TCFR_TXS_BIT_MAP);

	mt76_wr(dev, MT_MCU_PCIE_REMAP_1, MT_PSE_WTBL_2_PHYS_ADDR);

	for (i = 0; i < MT7603_WTBL_SIZE; i++)
		mt7603_wtbl_clear(dev, i);

	eth_broadcast_addr(bc_addr);
	mt7603_wtbl_init(dev, MT7603_WTBL_RESERVED, -1, bc_addr);
	dev->global_sta.wcid.idx = MT7603_WTBL_RESERVED;
	rcu_assign_pointer(dev->mt76.wcid[MT7603_WTBL_RESERVED],
			   &dev->global_sta.wcid);

	mt76_rmw_field(dev, MT_LPON_BTEIR, MT_LPON_BTEIR_MBSS_MODE, 2);
	mt76_rmw_field(dev, MT_WF_RMACDR, MT_WF_RMACDR_MBSSID_MASK, 2);

	mt76_wr(dev, MT_AGG_ARUCR,
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(0), 7) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(1), 2) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(2), 2) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(3), 2) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(4), 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(5), 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(6), 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(7), 1));

	mt76_wr(dev, MT_AGG_ARDCR,
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(0), MT7603_RATE_RETRY - 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(1), MT7603_RATE_RETRY - 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(2), MT7603_RATE_RETRY - 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(3), MT7603_RATE_RETRY - 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(4), MT7603_RATE_RETRY - 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(5), MT7603_RATE_RETRY - 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(6), MT7603_RATE_RETRY - 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(7), MT7603_RATE_RETRY - 1));

	mt76_wr(dev, MT_AGG_ARCR,
		(FIELD_PREP(MT_AGG_ARCR_RTS_RATE_THR, 2) |
		 MT_AGG_ARCR_RATE_DOWN_RATIO_EN |
		 FIELD_PREP(MT_AGG_ARCR_RATE_DOWN_RATIO, 1) |
		 FIELD_PREP(MT_AGG_ARCR_RATE_UP_EXTRA_TH, 4)));

	mt76_set(dev, MT_WTBL_RMVTCR, MT_WTBL_RMVTCR_RX_MV_MODE);

	mt76_clear(dev, MT_SEC_SCR, MT_SEC_SCR_MASK_ORDER);
	mt76_clear(dev, MT_SEC_SCR, BIT(18));

	/* Set secondary beacon time offsets */
	for (i = 0; i <= 4; i++)
		mt76_rmw_field(dev, MT_LPON_SBTOR(i), MT_LPON_SBTOR_TIME_OFFSET,
			       (i + 1) * (20 + 4096));
}

static int
mt7603_init_hardware(struct mt7603_dev *dev)
{
	int i, ret;

	mt76_wr(dev, MT_INT_SOURCE_CSR, ~0);

	ret = mt7603_eeprom_init(dev);
	if (ret < 0)
		return ret;

	ret = mt7603_dma_init(dev);
	if (ret)
		return ret;

	mt76_wr(dev, MT_WPDMA_GLO_CFG, 0x52000850);
	mt7603_mac_dma_start(dev);
	dev->rxfilter = mt76_rr(dev, MT_WF_RFCR);
	set_bit(MT76_STATE_INITIALIZED, &dev->mphy.state);

	for (i = 0; i < MT7603_WTBL_SIZE; i++) {
		mt76_wr(dev, MT_PSE_RTA, MT_PSE_RTA_BUSY | MT_PSE_RTA_WRITE |
			FIELD_PREP(MT_PSE_RTA_TAG_ID, i));
		mt76_poll(dev, MT_PSE_RTA, MT_PSE_RTA_BUSY, 0, 5000);
	}

	ret = mt7603_mcu_init(dev);
	if (ret)
		return ret;

	mt7603_dma_sched_init(dev);
	mt7603_mcu_set_eeprom(dev);
	mt7603_phy_init(dev);
	mt7603_mac_init(dev);

	return 0;
}

static const struct ieee80211_iface_limit if_limits[] = {
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_ADHOC)
	}, {
		.max = MT7603_MAX_INTERFACES,
		.types = BIT(NL80211_IFTYPE_STATION) |
#ifdef CONFIG_MAC80211_MESH
			 BIT(NL80211_IFTYPE_MESH_POINT) |
#endif
			 BIT(NL80211_IFTYPE_P2P_CLIENT) |
			 BIT(NL80211_IFTYPE_P2P_GO) |
			 BIT(NL80211_IFTYPE_AP)
	 },
};

static const struct ieee80211_iface_combination if_comb[] = {
	{
		.limits = if_limits,
		.n_limits = ARRAY_SIZE(if_limits),
		.max_interfaces = 4,
		.num_different_channels = 1,
		.beacon_int_infra_match = true,
	}
};

static void mt7603_led_set_config(struct mt76_phy *mphy, u8 delay_on,
				  u8 delay_off)
{
	struct mt7603_dev *dev = container_of(mphy->dev, struct mt7603_dev,
					      mt76);
	u32 val, addr;

	val = FIELD_PREP(MT_LED_STATUS_DURATION, 0xffff) |
	      FIELD_PREP(MT_LED_STATUS_OFF, delay_off) |
	      FIELD_PREP(MT_LED_STATUS_ON, delay_on);

	addr = mt7603_reg_map(dev, MT_LED_STATUS_0(mphy->leds.pin));
	mt76_wr(dev, addr, val);
	addr = mt7603_reg_map(dev, MT_LED_STATUS_1(mphy->leds.pin));
	mt76_wr(dev, addr, val);

	val = MT_LED_CTRL_REPLAY(mphy->leds.pin) |
	      MT_LED_CTRL_KICK(mphy->leds.pin);
	if (mphy->leds.al)
		val |= MT_LED_CTRL_POLARITY(mphy->leds.pin);
	addr = mt7603_reg_map(dev, MT_LED_CTRL);
	mt76_wr(dev, addr, val);
}

static int mt7603_led_set_blink(struct led_classdev *led_cdev,
				unsigned long *delay_on,
				unsigned long *delay_off)
{
	struct mt76_phy *mphy = container_of(led_cdev, struct mt76_phy,
					     leds.cdev);
	u8 delta_on, delta_off;

	delta_off = max_t(u8, *delay_off / 10, 1);
	delta_on = max_t(u8, *delay_on / 10, 1);

	mt7603_led_set_config(mphy, delta_on, delta_off);
	return 0;
}

static void mt7603_led_set_brightness(struct led_classdev *led_cdev,
				      enum led_brightness brightness)
{
	struct mt76_phy *mphy = container_of(led_cdev, struct mt76_phy,
					     leds.cdev);

	if (!brightness)
		mt7603_led_set_config(mphy, 0, 0xff);
	else
		mt7603_led_set_config(mphy, 0xff, 0);
}

static u32 __mt7603_reg_addr(struct mt7603_dev *dev, u32 addr)
{
	if (addr < 0x100000)
		return addr;

	return mt7603_reg_map(dev, addr);
}

static u32 mt7603_rr(struct mt76_dev *mdev, u32 offset)
{
	struct mt7603_dev *dev = container_of(mdev, struct mt7603_dev, mt76);
	u32 addr = __mt7603_reg_addr(dev, offset);

	return dev->bus_ops->rr(mdev, addr);
}

static void mt7603_wr(struct mt76_dev *mdev, u32 offset, u32 val)
{
	struct mt7603_dev *dev = container_of(mdev, struct mt7603_dev, mt76);
	u32 addr = __mt7603_reg_addr(dev, offset);

	dev->bus_ops->wr(mdev, addr, val);
}

static u32 mt7603_rmw(struct mt76_dev *mdev, u32 offset, u32 mask, u32 val)
{
	struct mt7603_dev *dev = container_of(mdev, struct mt7603_dev, mt76);
	u32 addr = __mt7603_reg_addr(dev, offset);

	return dev->bus_ops->rmw(mdev, addr, mask, val);
}

static void
mt7603_regd_notifier(struct wiphy *wiphy,
		     struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt7603_dev *dev = hw->priv;

	dev->mt76.region = request->dfs_region;
	dev->ed_monitor = dev->ed_monitor_enabled &&
			  dev->mt76.region == NL80211_DFS_ETSI;
}

static int
mt7603_txpower_signed(int val)
{
	bool sign = val & BIT(6);

	if (!(val & BIT(7)))
		return 0;

	val &= GENMASK(5, 0);
	if (!sign)
		val = -val;

	return val;
}

static void
mt7603_init_txpower(struct mt7603_dev *dev,
		    struct ieee80211_supported_band *sband)
{
	struct ieee80211_channel *chan;
	u8 *eeprom = (u8 *)dev->mt76.eeprom.data;
	int target_power = eeprom[MT_EE_TX_POWER_0_START_2G + 2] & ~BIT(7);
	u8 *rate_power = &eeprom[MT_EE_TX_POWER_CCK];
	bool ext_pa = eeprom[MT_EE_NIC_CONF_0 + 1] & BIT(1);
	int max_offset, cur_offset;
	int i;

	if (ext_pa && is_mt7603(dev))
		target_power = eeprom[MT_EE_TX_POWER_TSSI_OFF] & ~BIT(7);

	if (target_power & BIT(6))
		target_power = -(target_power & GENMASK(5, 0));

	max_offset = 0;
	for (i = 0; i < 14; i++) {
		cur_offset = mt7603_txpower_signed(rate_power[i]);
		max_offset = max(max_offset, cur_offset);
	}

	target_power += max_offset;

	dev->tx_power_limit = target_power;
	dev->mphy.txpower_cur = target_power;

	target_power = DIV_ROUND_UP(target_power, 2);

	/* add 3 dBm for 2SS devices (combined output) */
	if (dev->mphy.antenna_mask & BIT(1))
		target_power += 3;

	for (i = 0; i < sband->n_channels; i++) {
		chan = &sband->channels[i];
		chan->max_power = min_t(int, chan->max_reg_power, target_power);
		chan->orig_mpwr = target_power;
	}
}

int mt7603_register_device(struct mt7603_dev *dev)
{
	struct mt76_bus_ops *bus_ops;
	struct ieee80211_hw *hw = mt76_hw(dev);
	struct wiphy *wiphy = hw->wiphy;
	int ret;

	dev->bus_ops = dev->mt76.bus;
	bus_ops = devm_kmemdup(dev->mt76.dev, dev->bus_ops, sizeof(*bus_ops),
			       GFP_KERNEL);
	if (!bus_ops)
		return -ENOMEM;

	bus_ops->rr = mt7603_rr;
	bus_ops->wr = mt7603_wr;
	bus_ops->rmw = mt7603_rmw;
	dev->mt76.bus = bus_ops;

	spin_lock_init(&dev->ps_lock);

	INIT_DELAYED_WORK(&dev->mphy.mac_work, mt7603_mac_work);
	tasklet_setup(&dev->mt76.pre_tbtt_tasklet, mt7603_pre_tbtt_tasklet);

	dev->slottime = 9;
	dev->sensitivity_limit = 28;
	dev->dynamic_sensitivity = true;

	ret = mt7603_init_hardware(dev);
	if (ret)
		return ret;

	hw->queues = 4;
	hw->max_rates = 3;
	hw->max_report_rates = 7;
	hw->max_rate_tries = 11;
	hw->max_tx_fragments = 1;

	hw->radiotap_timestamp.units_pos =
		IEEE80211_RADIOTAP_TIMESTAMP_UNIT_US;

	hw->sta_data_size = sizeof(struct mt7603_sta);
	hw->vif_data_size = sizeof(struct mt7603_vif);

	wiphy->iface_combinations = if_comb;
	wiphy->n_iface_combinations = ARRAY_SIZE(if_comb);

	ieee80211_hw_set(hw, TX_STATUS_NO_AMPDU_LEN);
	ieee80211_hw_set(hw, HOST_BROADCAST_PS_BUFFERING);
	ieee80211_hw_set(hw, NEEDS_UNIQUE_STA_ADDR);

	/* init led callbacks */
	if (IS_ENABLED(CONFIG_MT76_LEDS)) {
		dev->mphy.leds.cdev.brightness_set = mt7603_led_set_brightness;
		dev->mphy.leds.cdev.blink_set = mt7603_led_set_blink;
	}

	wiphy->reg_notifier = mt7603_regd_notifier;

	ret = mt76_register_device(&dev->mt76, true, mt76_rates,
				   ARRAY_SIZE(mt76_rates));
	if (ret)
		return ret;

	mt7603_init_debugfs(dev);
	mt7603_init_txpower(dev, &dev->mphy.sband_2g.sband);

	return 0;
}

void mt7603_unregister_device(struct mt7603_dev *dev)
{
	tasklet_disable(&dev->mt76.pre_tbtt_tasklet);
	mt76_unregister_device(&dev->mt76);
	mt7603_mcu_exit(dev);
	mt7603_dma_cleanup(dev);
	mt76_free_device(&dev->mt76);
}
