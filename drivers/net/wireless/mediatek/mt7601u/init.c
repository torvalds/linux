/*
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 * Copyright (C) 2014 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "mt7601u.h"
#include "eeprom.h"
#include "trace.h"
#include "mcu.h"

#include "initvals.h"

static void
mt7601u_set_wlan_state(struct mt7601u_dev *dev, u32 val, bool enable)
{
	int i;

	/* Note: we don't turn off WLAN_CLK because that makes the device
	 *	 not respond properly on the probe path.
	 *	 In case anyone (PSM?) wants to use this function we can
	 *	 bring the clock stuff back and fixup the probe path.
	 */

	if (enable)
		val |= (MT_WLAN_FUN_CTRL_WLAN_EN |
			MT_WLAN_FUN_CTRL_WLAN_CLK_EN);
	else
		val &= ~(MT_WLAN_FUN_CTRL_WLAN_EN);

	mt7601u_wr(dev, MT_WLAN_FUN_CTRL, val);
	udelay(20);

	if (enable) {
		set_bit(MT7601U_STATE_WLAN_RUNNING, &dev->state);
	} else {
		clear_bit(MT7601U_STATE_WLAN_RUNNING, &dev->state);
		return;
	}

	for (i = 200; i; i--) {
		val = mt7601u_rr(dev, MT_CMB_CTRL);

		if (val & MT_CMB_CTRL_XTAL_RDY && val & MT_CMB_CTRL_PLL_LD)
			break;

		udelay(20);
	}

	/* Note: vendor driver tries to disable/enable wlan here and retry
	 *       but the code which does it is so buggy it must have never
	 *       triggered, so don't bother.
	 */
	if (!i)
		dev_err(dev->dev, "Error: PLL and XTAL check failed!\n");
}

static void mt7601u_chip_onoff(struct mt7601u_dev *dev, bool enable, bool reset)
{
	u32 val;

	mutex_lock(&dev->hw_atomic_mutex);

	val = mt7601u_rr(dev, MT_WLAN_FUN_CTRL);

	if (reset) {
		val |= MT_WLAN_FUN_CTRL_GPIO_OUT_EN;
		val &= ~MT_WLAN_FUN_CTRL_FRC_WL_ANT_SEL;

		if (val & MT_WLAN_FUN_CTRL_WLAN_EN) {
			val |= (MT_WLAN_FUN_CTRL_WLAN_RESET |
				MT_WLAN_FUN_CTRL_WLAN_RESET_RF);
			mt7601u_wr(dev, MT_WLAN_FUN_CTRL, val);
			udelay(20);

			val &= ~(MT_WLAN_FUN_CTRL_WLAN_RESET |
				 MT_WLAN_FUN_CTRL_WLAN_RESET_RF);
		}
	}

	mt7601u_wr(dev, MT_WLAN_FUN_CTRL, val);
	udelay(20);

	mt7601u_set_wlan_state(dev, val, enable);

	mutex_unlock(&dev->hw_atomic_mutex);
}

static void mt7601u_reset_csr_bbp(struct mt7601u_dev *dev)
{
	mt7601u_wr(dev, MT_MAC_SYS_CTRL, (MT_MAC_SYS_CTRL_RESET_CSR |
					  MT_MAC_SYS_CTRL_RESET_BBP));
	mt7601u_wr(dev, MT_USB_DMA_CFG, 0);
	msleep(1);
	mt7601u_wr(dev, MT_MAC_SYS_CTRL, 0);
}

static void mt7601u_init_usb_dma(struct mt7601u_dev *dev)
{
	u32 val;

	val = MT76_SET(MT_USB_DMA_CFG_RX_BULK_AGG_TOUT, MT_USB_AGGR_TIMEOUT) |
	      MT76_SET(MT_USB_DMA_CFG_RX_BULK_AGG_LMT, MT_USB_AGGR_SIZE_LIMIT) |
	      MT_USB_DMA_CFG_RX_BULK_EN |
	      MT_USB_DMA_CFG_TX_BULK_EN;
	if (dev->in_max_packet == 512)
		val |= MT_USB_DMA_CFG_RX_BULK_AGG_EN;
	mt7601u_wr(dev, MT_USB_DMA_CFG, val);

	val |= MT_USB_DMA_CFG_UDMA_RX_WL_DROP;
	mt7601u_wr(dev, MT_USB_DMA_CFG, val);
	val &= ~MT_USB_DMA_CFG_UDMA_RX_WL_DROP;
	mt7601u_wr(dev, MT_USB_DMA_CFG, val);
}

static int mt7601u_init_bbp(struct mt7601u_dev *dev)
{
	int ret;

	ret = mt7601u_wait_bbp_ready(dev);
	if (ret)
		return ret;

	ret = mt7601u_write_reg_pairs(dev, MT_MCU_MEMMAP_BBP, bbp_common_vals,
				      ARRAY_SIZE(bbp_common_vals));
	if (ret)
		return ret;

	return mt7601u_write_reg_pairs(dev, MT_MCU_MEMMAP_BBP, bbp_chip_vals,
				       ARRAY_SIZE(bbp_chip_vals));
}

static void
mt76_init_beacon_offsets(struct mt7601u_dev *dev)
{
	u16 base = MT_BEACON_BASE;
	u32 regs[4] = {};
	int i;

	for (i = 0; i < 16; i++) {
		u16 addr = dev->beacon_offsets[i];

		regs[i / 4] |= ((addr - base) / 64) << (8 * (i % 4));
	}

	for (i = 0; i < 4; i++)
		mt7601u_wr(dev, MT_BCN_OFFSET(i), regs[i]);
}

static int mt7601u_write_mac_initvals(struct mt7601u_dev *dev)
{
	int ret;

	ret = mt7601u_write_reg_pairs(dev, MT_MCU_MEMMAP_WLAN, mac_common_vals,
				      ARRAY_SIZE(mac_common_vals));
	if (ret)
		return ret;
	ret = mt7601u_write_reg_pairs(dev, MT_MCU_MEMMAP_WLAN,
				      mac_chip_vals, ARRAY_SIZE(mac_chip_vals));
	if (ret)
		return ret;

	mt76_init_beacon_offsets(dev);

	mt7601u_wr(dev, MT_AUX_CLK_CFG, 0);

	return 0;
}

static int mt7601u_init_wcid_mem(struct mt7601u_dev *dev)
{
	u32 *vals;
	int i, ret;

	vals = kmalloc(sizeof(*vals) * N_WCIDS * 2, GFP_KERNEL);
	if (!vals)
		return -ENOMEM;

	for (i = 0; i < N_WCIDS; i++)  {
		vals[i * 2] = 0xffffffff;
		vals[i * 2 + 1] = 0x00ffffff;
	}

	ret = mt7601u_burst_write_regs(dev, MT_WCID_ADDR_BASE,
				       vals, N_WCIDS * 2);
	kfree(vals);

	return ret;
}

static int mt7601u_init_key_mem(struct mt7601u_dev *dev)
{
	u32 vals[4] = {};

	return mt7601u_burst_write_regs(dev, MT_SKEY_MODE_BASE_0,
					vals, ARRAY_SIZE(vals));
}

static int mt7601u_init_wcid_attr_mem(struct mt7601u_dev *dev)
{
	u32 *vals;
	int i, ret;

	vals = kmalloc(sizeof(*vals) * N_WCIDS * 2, GFP_KERNEL);
	if (!vals)
		return -ENOMEM;

	for (i = 0; i < N_WCIDS * 2; i++)
		vals[i] = 1;

	ret = mt7601u_burst_write_regs(dev, MT_WCID_ATTR_BASE,
				       vals, N_WCIDS * 2);
	kfree(vals);

	return ret;
}

static void mt7601u_reset_counters(struct mt7601u_dev *dev)
{
	mt7601u_rr(dev, MT_RX_STA_CNT0);
	mt7601u_rr(dev, MT_RX_STA_CNT1);
	mt7601u_rr(dev, MT_RX_STA_CNT2);
	mt7601u_rr(dev, MT_TX_STA_CNT0);
	mt7601u_rr(dev, MT_TX_STA_CNT1);
	mt7601u_rr(dev, MT_TX_STA_CNT2);
}

int mt7601u_mac_start(struct mt7601u_dev *dev)
{
	mt7601u_wr(dev, MT_MAC_SYS_CTRL, MT_MAC_SYS_CTRL_ENABLE_TX);

	if (!mt76_poll(dev, MT_WPDMA_GLO_CFG, MT_WPDMA_GLO_CFG_TX_DMA_BUSY |
		       MT_WPDMA_GLO_CFG_RX_DMA_BUSY, 0, 200000))
		return -ETIMEDOUT;

	dev->rxfilter = MT_RX_FILTR_CFG_CRC_ERR |
		MT_RX_FILTR_CFG_PHY_ERR | MT_RX_FILTR_CFG_PROMISC |
		MT_RX_FILTR_CFG_VER_ERR | MT_RX_FILTR_CFG_DUP |
		MT_RX_FILTR_CFG_CFACK | MT_RX_FILTR_CFG_CFEND |
		MT_RX_FILTR_CFG_ACK | MT_RX_FILTR_CFG_CTS |
		MT_RX_FILTR_CFG_RTS | MT_RX_FILTR_CFG_PSPOLL |
		MT_RX_FILTR_CFG_BA | MT_RX_FILTR_CFG_CTRL_RSV;
	mt7601u_wr(dev, MT_RX_FILTR_CFG, dev->rxfilter);

	mt7601u_wr(dev, MT_MAC_SYS_CTRL,
		   MT_MAC_SYS_CTRL_ENABLE_TX | MT_MAC_SYS_CTRL_ENABLE_RX);

	if (!mt76_poll(dev, MT_WPDMA_GLO_CFG, MT_WPDMA_GLO_CFG_TX_DMA_BUSY |
		       MT_WPDMA_GLO_CFG_RX_DMA_BUSY, 0, 50))
		return -ETIMEDOUT;

	return 0;
}

static void mt7601u_mac_stop_hw(struct mt7601u_dev *dev)
{
	int i, ok;

	if (test_bit(MT7601U_STATE_REMOVED, &dev->state))
		return;

	mt76_clear(dev, MT_BEACON_TIME_CFG, MT_BEACON_TIME_CFG_TIMER_EN |
		   MT_BEACON_TIME_CFG_SYNC_MODE | MT_BEACON_TIME_CFG_TBTT_EN |
		   MT_BEACON_TIME_CFG_BEACON_TX);

	if (!mt76_poll(dev, MT_USB_DMA_CFG, MT_USB_DMA_CFG_TX_BUSY, 0, 1000))
		dev_warn(dev->dev, "Warning: TX DMA did not stop!\n");

	/* Page count on TxQ */
	i = 200;
	while (i-- && ((mt76_rr(dev, 0x0438) & 0xffffffff) ||
		       (mt76_rr(dev, 0x0a30) & 0x000000ff) ||
		       (mt76_rr(dev, 0x0a34) & 0x00ff00ff)))
		msleep(10);

	if (!mt76_poll(dev, MT_MAC_STATUS, MT_MAC_STATUS_TX, 0, 1000))
		dev_warn(dev->dev, "Warning: MAC TX did not stop!\n");

	mt76_clear(dev, MT_MAC_SYS_CTRL, MT_MAC_SYS_CTRL_ENABLE_RX |
					 MT_MAC_SYS_CTRL_ENABLE_TX);

	/* Page count on RxQ */
	ok = 0;
	i = 200;
	while (i--) {
		if ((mt76_rr(dev, 0x0430) & 0x00ff0000) ||
		    (mt76_rr(dev, 0x0a30) & 0xffffffff) ||
		    (mt76_rr(dev, 0x0a34) & 0xffffffff))
			ok++;
		if (ok > 6)
			break;

		msleep(1);
	}

	if (!mt76_poll(dev, MT_MAC_STATUS, MT_MAC_STATUS_RX, 0, 1000))
		dev_warn(dev->dev, "Warning: MAC RX did not stop!\n");

	if (!mt76_poll(dev, MT_USB_DMA_CFG, MT_USB_DMA_CFG_RX_BUSY, 0, 1000))
		dev_warn(dev->dev, "Warning: RX DMA did not stop!\n");
}

void mt7601u_mac_stop(struct mt7601u_dev *dev)
{
	mt7601u_mac_stop_hw(dev);
	flush_delayed_work(&dev->stat_work);
	cancel_delayed_work_sync(&dev->stat_work);
}

static void mt7601u_stop_hardware(struct mt7601u_dev *dev)
{
	mt7601u_chip_onoff(dev, false, false);
}

int mt7601u_init_hardware(struct mt7601u_dev *dev)
{
	static const u16 beacon_offsets[16] = {
		/* 512 byte per beacon */
		0xc000,	0xc200,	0xc400,	0xc600,
		0xc800,	0xca00,	0xcc00,	0xce00,
		0xd000,	0xd200,	0xd400,	0xd600,
		0xd800,	0xda00,	0xdc00,	0xde00
	};
	int ret;

	dev->beacon_offsets = beacon_offsets;

	mt7601u_chip_onoff(dev, true, false);

	ret = mt7601u_wait_asic_ready(dev);
	if (ret)
		goto err;
	ret = mt7601u_mcu_init(dev);
	if (ret)
		goto err;

	if (!mt76_poll_msec(dev, MT_WPDMA_GLO_CFG,
			    MT_WPDMA_GLO_CFG_TX_DMA_BUSY |
			    MT_WPDMA_GLO_CFG_RX_DMA_BUSY, 0, 100)) {
		ret = -EIO;
		goto err;
	}

	/* Wait for ASIC ready after FW load. */
	ret = mt7601u_wait_asic_ready(dev);
	if (ret)
		goto err;

	mt7601u_reset_csr_bbp(dev);
	mt7601u_init_usb_dma(dev);

	ret = mt7601u_mcu_cmd_init(dev);
	if (ret)
		goto err;
	ret = mt7601u_dma_init(dev);
	if (ret)
		goto err_mcu;
	ret = mt7601u_write_mac_initvals(dev);
	if (ret)
		goto err_rx;

	if (!mt76_poll_msec(dev, MT_MAC_STATUS,
			    MT_MAC_STATUS_TX | MT_MAC_STATUS_RX, 0, 100)) {
		ret = -EIO;
		goto err_rx;
	}

	ret = mt7601u_init_bbp(dev);
	if (ret)
		goto err_rx;
	ret = mt7601u_init_wcid_mem(dev);
	if (ret)
		goto err_rx;
	ret = mt7601u_init_key_mem(dev);
	if (ret)
		goto err_rx;
	ret = mt7601u_init_wcid_attr_mem(dev);
	if (ret)
		goto err_rx;

	mt76_clear(dev, MT_BEACON_TIME_CFG, (MT_BEACON_TIME_CFG_TIMER_EN |
					     MT_BEACON_TIME_CFG_SYNC_MODE |
					     MT_BEACON_TIME_CFG_TBTT_EN |
					     MT_BEACON_TIME_CFG_BEACON_TX));

	mt7601u_reset_counters(dev);

	mt7601u_rmw(dev, MT_US_CYC_CFG, MT_US_CYC_CNT, 0x1e);

	mt7601u_wr(dev, MT_TXOP_CTRL_CFG, MT76_SET(MT_TXOP_TRUN_EN, 0x3f) |
					  MT76_SET(MT_TXOP_EXT_CCA_DLY, 0x58));

	ret = mt7601u_eeprom_init(dev);
	if (ret)
		goto err_rx;

	ret = mt7601u_phy_init(dev);
	if (ret)
		goto err_rx;

	mt7601u_set_rx_path(dev, 0);
	mt7601u_set_tx_dac(dev, 0);

	mt7601u_mac_set_ctrlch(dev, false);
	mt7601u_bbp_set_ctrlch(dev, false);
	mt7601u_bbp_set_bw(dev, MT_BW_20);

	return 0;

err_rx:
	mt7601u_dma_cleanup(dev);
err_mcu:
	mt7601u_mcu_cmd_deinit(dev);
err:
	mt7601u_chip_onoff(dev, false, false);
	return ret;
}

void mt7601u_cleanup(struct mt7601u_dev *dev)
{
	if (!test_and_clear_bit(MT7601U_STATE_INITIALIZED, &dev->state))
		return;

	mt7601u_stop_hardware(dev);
	mt7601u_dma_cleanup(dev);
	mt7601u_mcu_cmd_deinit(dev);
}

struct mt7601u_dev *mt7601u_alloc_device(struct device *pdev)
{
	struct ieee80211_hw *hw;
	struct mt7601u_dev *dev;

	hw = ieee80211_alloc_hw(sizeof(*dev), &mt7601u_ops);
	if (!hw)
		return NULL;

	dev = hw->priv;
	dev->dev = pdev;
	dev->hw = hw;
	mutex_init(&dev->vendor_req_mutex);
	mutex_init(&dev->reg_atomic_mutex);
	mutex_init(&dev->hw_atomic_mutex);
	mutex_init(&dev->mutex);
	spin_lock_init(&dev->tx_lock);
	spin_lock_init(&dev->rx_lock);
	spin_lock_init(&dev->lock);
	spin_lock_init(&dev->mac_lock);
	spin_lock_init(&dev->con_mon_lock);
	atomic_set(&dev->avg_ampdu_len, 1);
	skb_queue_head_init(&dev->tx_skb_done);

	dev->stat_wq = alloc_workqueue("mt7601u", WQ_UNBOUND, 0);
	if (!dev->stat_wq) {
		ieee80211_free_hw(hw);
		return NULL;
	}

	return dev;
}

#define CHAN2G(_idx, _freq) {			\
	.band = NL80211_BAND_2GHZ,		\
	.center_freq = (_freq),			\
	.hw_value = (_idx),			\
	.max_power = 30,			\
}

static const struct ieee80211_channel mt76_channels_2ghz[] = {
	CHAN2G(1, 2412),
	CHAN2G(2, 2417),
	CHAN2G(3, 2422),
	CHAN2G(4, 2427),
	CHAN2G(5, 2432),
	CHAN2G(6, 2437),
	CHAN2G(7, 2442),
	CHAN2G(8, 2447),
	CHAN2G(9, 2452),
	CHAN2G(10, 2457),
	CHAN2G(11, 2462),
	CHAN2G(12, 2467),
	CHAN2G(13, 2472),
	CHAN2G(14, 2484),
};

#define CCK_RATE(_idx, _rate) {					\
	.bitrate = _rate,					\
	.flags = IEEE80211_RATE_SHORT_PREAMBLE,			\
	.hw_value = (MT_PHY_TYPE_CCK << 8) | _idx,		\
	.hw_value_short = (MT_PHY_TYPE_CCK << 8) | (8 + _idx),	\
}

#define OFDM_RATE(_idx, _rate) {				\
	.bitrate = _rate,					\
	.hw_value = (MT_PHY_TYPE_OFDM << 8) | _idx,		\
	.hw_value_short = (MT_PHY_TYPE_OFDM << 8) | _idx,	\
}

static struct ieee80211_rate mt76_rates[] = {
	CCK_RATE(0, 10),
	CCK_RATE(1, 20),
	CCK_RATE(2, 55),
	CCK_RATE(3, 110),
	OFDM_RATE(0, 60),
	OFDM_RATE(1, 90),
	OFDM_RATE(2, 120),
	OFDM_RATE(3, 180),
	OFDM_RATE(4, 240),
	OFDM_RATE(5, 360),
	OFDM_RATE(6, 480),
	OFDM_RATE(7, 540),
};

static int
mt76_init_sband(struct mt7601u_dev *dev, struct ieee80211_supported_band *sband,
		const struct ieee80211_channel *chan, int n_chan,
		struct ieee80211_rate *rates, int n_rates)
{
	struct ieee80211_sta_ht_cap *ht_cap;
	void *chanlist;
	int size;

	size = n_chan * sizeof(*chan);
	chanlist = devm_kmemdup(dev->dev, chan, size, GFP_KERNEL);
	if (!chanlist)
		return -ENOMEM;

	sband->channels = chanlist;
	sband->n_channels = n_chan;
	sband->bitrates = rates;
	sband->n_bitrates = n_rates;

	ht_cap = &sband->ht_cap;
	ht_cap->ht_supported = true;
	ht_cap->cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
		      IEEE80211_HT_CAP_GRN_FLD |
		      IEEE80211_HT_CAP_SGI_20 |
		      IEEE80211_HT_CAP_SGI_40 |
		      (1 << IEEE80211_HT_CAP_RX_STBC_SHIFT);

	ht_cap->mcs.rx_mask[0] = 0xff;
	ht_cap->mcs.rx_mask[4] = 0x1;
	ht_cap->mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
	ht_cap->ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;
	ht_cap->ampdu_density = IEEE80211_HT_MPDU_DENSITY_2;

	dev->chandef.chan = &sband->channels[0];

	return 0;
}

static int
mt76_init_sband_2g(struct mt7601u_dev *dev)
{
	dev->sband_2g = devm_kzalloc(dev->dev, sizeof(*dev->sband_2g),
				     GFP_KERNEL);
	dev->hw->wiphy->bands[NL80211_BAND_2GHZ] = dev->sband_2g;

	WARN_ON(dev->ee->reg.start - 1 + dev->ee->reg.num >
		ARRAY_SIZE(mt76_channels_2ghz));

	return mt76_init_sband(dev, dev->sband_2g,
			       &mt76_channels_2ghz[dev->ee->reg.start - 1],
			       dev->ee->reg.num,
			       mt76_rates, ARRAY_SIZE(mt76_rates));
}

int mt7601u_register_device(struct mt7601u_dev *dev)
{
	struct ieee80211_hw *hw = dev->hw;
	struct wiphy *wiphy = hw->wiphy;
	int ret;

	/* Reserve WCID 0 for mcast - thanks to this APs WCID will go to
	 * entry no. 1 like it does in the vendor driver.
	 */
	dev->wcid_mask[0] |= 1;

	/* init fake wcid for monitor interfaces */
	dev->mon_wcid = devm_kmalloc(dev->dev, sizeof(*dev->mon_wcid),
				     GFP_KERNEL);
	if (!dev->mon_wcid)
		return -ENOMEM;
	dev->mon_wcid->idx = 0xff;
	dev->mon_wcid->hw_key_idx = -1;

	SET_IEEE80211_DEV(hw, dev->dev);

	hw->queues = 4;
	ieee80211_hw_set(hw, SIGNAL_DBM);
	ieee80211_hw_set(hw, PS_NULLFUNC_STACK);
	ieee80211_hw_set(hw, SUPPORTS_HT_CCK_RATES);
	ieee80211_hw_set(hw, AMPDU_AGGREGATION);
	ieee80211_hw_set(hw, SUPPORTS_RC_TABLE);
	hw->max_rates = 1;
	hw->max_report_rates = 7;
	hw->max_rate_tries = 1;

	hw->sta_data_size = sizeof(struct mt76_sta);
	hw->vif_data_size = sizeof(struct mt76_vif);

	SET_IEEE80211_PERM_ADDR(hw, dev->macaddr);

	wiphy->features |= NL80211_FEATURE_ACTIVE_MONITOR;
	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);

	ret = mt76_init_sband_2g(dev);
	if (ret)
		return ret;

	INIT_DELAYED_WORK(&dev->mac_work, mt7601u_mac_work);
	INIT_DELAYED_WORK(&dev->stat_work, mt7601u_tx_stat);

	ret = ieee80211_register_hw(hw);
	if (ret)
		return ret;

	mt7601u_init_debugfs(dev);

	return 0;
}
