/*
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "mt76x0.h"
#include "eeprom.h"
#include "trace.h"
#include "mcu.h"
#include "../mt76x02_util.h"
#include "../mt76x02_dma.h"

#include "initvals.h"

static void mt76x0_vht_cap_mask(struct ieee80211_supported_band *sband)
{
	struct ieee80211_sta_vht_cap *vht_cap = &sband->vht_cap;
	u16 mcs_map = 0;
	int i;

	vht_cap->cap &= ~IEEE80211_VHT_CAP_RXLDPC;
	for (i = 0; i < 8; i++) {
		if (!i)
			mcs_map |= (IEEE80211_VHT_MCS_SUPPORT_0_7 << (i * 2));
		else
			mcs_map |=
				(IEEE80211_VHT_MCS_NOT_SUPPORTED << (i * 2));
	}
	vht_cap->vht_mcs.rx_mcs_map = cpu_to_le16(mcs_map);
	vht_cap->vht_mcs.tx_mcs_map = cpu_to_le16(mcs_map);
}

static void
mt76x0_set_wlan_state(struct mt76x0_dev *dev, u32 val, bool enable)
{
	u32 mask = MT_CMB_CTRL_XTAL_RDY | MT_CMB_CTRL_PLL_LD;

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

	mt76_wr(dev, MT_WLAN_FUN_CTRL, val);
	udelay(20);

	/* Note: vendor driver tries to disable/enable wlan here and retry
	 *       but the code which does it is so buggy it must have never
	 *       triggered, so don't bother.
	 */
	if (enable && !mt76_poll(dev, MT_CMB_CTRL, mask, mask, 2000))
		dev_err(dev->mt76.dev, "PLL and XTAL check failed\n");
}

void mt76x0_chip_onoff(struct mt76x0_dev *dev, bool enable, bool reset)
{
	u32 val;

	mutex_lock(&dev->hw_atomic_mutex);

	val = mt76_rr(dev, MT_WLAN_FUN_CTRL);

	if (reset) {
		val |= MT_WLAN_FUN_CTRL_GPIO_OUT_EN;
		val &= ~MT_WLAN_FUN_CTRL_FRC_WL_ANT_SEL;

		if (val & MT_WLAN_FUN_CTRL_WLAN_EN) {
			val |= (MT_WLAN_FUN_CTRL_WLAN_RESET |
				MT_WLAN_FUN_CTRL_WLAN_RESET_RF);
			mt76_wr(dev, MT_WLAN_FUN_CTRL, val);
			udelay(20);

			val &= ~(MT_WLAN_FUN_CTRL_WLAN_RESET |
				 MT_WLAN_FUN_CTRL_WLAN_RESET_RF);
		}
	}

	mt76_wr(dev, MT_WLAN_FUN_CTRL, val);
	udelay(20);

	mt76x0_set_wlan_state(dev, val, enable);

	mutex_unlock(&dev->hw_atomic_mutex);
}
EXPORT_SYMBOL_GPL(mt76x0_chip_onoff);

static void mt76x0_reset_csr_bbp(struct mt76x0_dev *dev)
{
	u32 val;

	val = mt76_rr(dev, MT_PBF_SYS_CTRL);
	val &= ~0x2000;
	mt76_wr(dev, MT_PBF_SYS_CTRL, val);

	mt76_wr(dev, MT_MAC_SYS_CTRL, MT_MAC_SYS_CTRL_RESET_CSR |
					 MT_MAC_SYS_CTRL_RESET_BBP);

	msleep(200);
}

static void mt76x0_init_usb_dma(struct mt76x0_dev *dev)
{
	u32 val;

	val = mt76_rr(dev, MT_USB_DMA_CFG);

	val |= MT_USB_DMA_CFG_RX_BULK_EN |
	       MT_USB_DMA_CFG_TX_BULK_EN;

	/* disable AGGR_BULK_RX in order to receive one
	 * frame in each rx urb and avoid copies
	 */
	val &= ~MT_USB_DMA_CFG_RX_BULK_AGG_EN;
	mt76_wr(dev, MT_USB_DMA_CFG, val);

	val = mt76_rr(dev, MT_COM_REG0);
	if (val & 1)
		dev_dbg(dev->mt76.dev, "MCU not ready\n");

	val = mt76_rr(dev, MT_USB_DMA_CFG);

	val |= MT_USB_DMA_CFG_RX_DROP_OR_PAD;
	mt76_wr(dev, MT_USB_DMA_CFG, val);
	val &= ~MT_USB_DMA_CFG_RX_DROP_OR_PAD;
	mt76_wr(dev, MT_USB_DMA_CFG, val);
}

#define RANDOM_WRITE(dev, tab)			\
	mt76_wr_rp(dev, MT_MCU_MEMMAP_WLAN,	\
		   tab, ARRAY_SIZE(tab))

static int mt76x0_init_bbp(struct mt76x0_dev *dev)
{
	int ret, i;

	ret = mt76x0_wait_bbp_ready(dev);
	if (ret)
		return ret;

	RANDOM_WRITE(dev, mt76x0_bbp_init_tab);

	for (i = 0; i < ARRAY_SIZE(mt76x0_bbp_switch_tab); i++) {
		const struct mt76x0_bbp_switch_item *item = &mt76x0_bbp_switch_tab[i];
		const struct mt76_reg_pair *pair = &item->reg_pair;

		if (((RF_G_BAND | RF_BW_20) & item->bw_band) == (RF_G_BAND | RF_BW_20))
			mt76_wr(dev, pair->reg, pair->value);
	}

	RANDOM_WRITE(dev, mt76x0_dcoc_tab);

	return 0;
}

static void mt76x0_init_mac_registers(struct mt76x0_dev *dev)
{
	u32 reg;

	RANDOM_WRITE(dev, common_mac_reg_table);

	mt76x02_set_beacon_offsets(&dev->mt76);

	/* Enable PBF and MAC clock SYS_CTRL[11:10] = 0x3 */
	RANDOM_WRITE(dev, mt76x0_mac_reg_table);

	/* Release BBP and MAC reset MAC_SYS_CTRL[1:0] = 0x0 */
	reg = mt76_rr(dev, MT_MAC_SYS_CTRL);
	reg &= ~0x3;
	mt76_wr(dev, MT_MAC_SYS_CTRL, reg);

	if (is_mt7610e(dev)) {
		/* Disable COEX_EN */
		reg = mt76_rr(dev, MT_COEXCFG0);
		reg &= 0xFFFFFFFE;
		mt76_wr(dev, MT_COEXCFG0, reg);
	}

	/* Set 0x141C[15:12]=0xF */
	reg = mt76_rr(dev, MT_EXT_CCA_CFG);
	reg |= 0x0000F000;
	mt76_wr(dev, MT_EXT_CCA_CFG, reg);

	mt76_clear(dev, MT_FCE_L2_STUFF, MT_FCE_L2_STUFF_WR_MPDU_LEN_EN);

	/*
		TxRing 9 is for Mgmt frame.
		TxRing 8 is for In-band command frame.
		WMM_RG0_TXQMA: This register setting is for FCE to define the rule of TxRing 9.
		WMM_RG1_TXQMA: This register setting is for FCE to define the rule of TxRing 8.
	*/
	reg = mt76_rr(dev, MT_WMM_CTRL);
	reg &= ~0x000003FF;
	reg |= 0x00000201;
	mt76_wr(dev, MT_WMM_CTRL, reg);

	/* TODO: Probably not needed */
	mt76_wr(dev, 0x7028, 0);
	mt76_wr(dev, 0x7010, 0);
	mt76_wr(dev, 0x7024, 0);
	msleep(10);
}

static int mt76x0_init_wcid_mem(struct mt76x0_dev *dev)
{
	u32 *vals;
	int i;

	vals = kmalloc(sizeof(*vals) * MT76_N_WCIDS * 2, GFP_KERNEL);
	if (!vals)
		return -ENOMEM;

	for (i = 0; i < MT76_N_WCIDS; i++)  {
		vals[i * 2] = 0xffffffff;
		vals[i * 2 + 1] = 0x00ffffff;
	}

	mt76_wr_copy(dev, MT_WCID_ADDR_BASE, vals, MT76_N_WCIDS * 2);
	kfree(vals);
	return 0;
}

static void mt76x0_init_key_mem(struct mt76x0_dev *dev)
{
	u32 vals[4] = {};

	mt76_wr_copy(dev, MT_SKEY_MODE_BASE_0, vals, ARRAY_SIZE(vals));
}

static int mt76x0_init_wcid_attr_mem(struct mt76x0_dev *dev)
{
	u32 *vals;
	int i;

	vals = kmalloc(sizeof(*vals) * MT76_N_WCIDS * 2, GFP_KERNEL);
	if (!vals)
		return -ENOMEM;

	for (i = 0; i < MT76_N_WCIDS * 2; i++)
		vals[i] = 1;

	mt76_wr_copy(dev, MT_WCID_ATTR_BASE, vals, MT76_N_WCIDS * 2);
	kfree(vals);
	return 0;
}

static void mt76x0_reset_counters(struct mt76x0_dev *dev)
{
	mt76_rr(dev, MT_RX_STAT_0);
	mt76_rr(dev, MT_RX_STAT_1);
	mt76_rr(dev, MT_RX_STAT_2);
	mt76_rr(dev, MT_TX_STA_0);
	mt76_rr(dev, MT_TX_STA_1);
	mt76_rr(dev, MT_TX_STA_2);
}

int mt76x0_mac_start(struct mt76x0_dev *dev)
{
	mt76_wr(dev, MT_MAC_SYS_CTRL, MT_MAC_SYS_CTRL_ENABLE_TX);

	if (!mt76x02_wait_for_wpdma(&dev->mt76, 200000))
		return -ETIMEDOUT;

	dev->mt76.rxfilter = MT_RX_FILTR_CFG_CRC_ERR |
		MT_RX_FILTR_CFG_PHY_ERR | MT_RX_FILTR_CFG_PROMISC |
		MT_RX_FILTR_CFG_VER_ERR | MT_RX_FILTR_CFG_DUP |
		MT_RX_FILTR_CFG_CFACK | MT_RX_FILTR_CFG_CFEND |
		MT_RX_FILTR_CFG_ACK | MT_RX_FILTR_CFG_CTS |
		MT_RX_FILTR_CFG_RTS | MT_RX_FILTR_CFG_PSPOLL |
		MT_RX_FILTR_CFG_BA | MT_RX_FILTR_CFG_CTRL_RSV;
	mt76_wr(dev, MT_RX_FILTR_CFG, dev->mt76.rxfilter);

	mt76_wr(dev, MT_MAC_SYS_CTRL,
		MT_MAC_SYS_CTRL_ENABLE_TX | MT_MAC_SYS_CTRL_ENABLE_RX);

	return !mt76x02_wait_for_wpdma(&dev->mt76, 50) ? -ETIMEDOUT : 0;
}

static void mt76x0_mac_stop_hw(struct mt76x0_dev *dev)
{
	int i, ok;

	if (test_bit(MT76_REMOVED, &dev->mt76.state))
		return;

	mt76_clear(dev, MT_BEACON_TIME_CFG, MT_BEACON_TIME_CFG_TIMER_EN |
		   MT_BEACON_TIME_CFG_SYNC_MODE | MT_BEACON_TIME_CFG_TBTT_EN |
		   MT_BEACON_TIME_CFG_BEACON_TX);

	if (!mt76_poll(dev, MT_USB_DMA_CFG, MT_USB_DMA_CFG_TX_BUSY, 0, 1000))
		dev_warn(dev->mt76.dev, "Warning: TX DMA did not stop!\n");

	/* Page count on TxQ */
	i = 200;
	while (i-- && ((mt76_rr(dev, 0x0438) & 0xffffffff) ||
		       (mt76_rr(dev, 0x0a30) & 0x000000ff) ||
		       (mt76_rr(dev, 0x0a34) & 0x00ff00ff)))
		msleep(10);

	if (!mt76_poll(dev, MT_MAC_STATUS, MT_MAC_STATUS_TX, 0, 1000))
		dev_warn(dev->mt76.dev, "Warning: MAC TX did not stop!\n");

	mt76_clear(dev, MT_MAC_SYS_CTRL, MT_MAC_SYS_CTRL_ENABLE_RX |
					 MT_MAC_SYS_CTRL_ENABLE_TX);

	/* Page count on RxQ */
	ok = 0;
	i = 200;
	while (i--) {
		if (!(mt76_rr(dev, MT_RXQ_STA) & 0x00ff0000) &&
		    !mt76_rr(dev, 0x0a30) &&
		    !mt76_rr(dev, 0x0a34)) {
			if (ok++ > 5)
				break;
			continue;
		}
		msleep(1);
	}

	if (!mt76_poll(dev, MT_MAC_STATUS, MT_MAC_STATUS_RX, 0, 1000))
		dev_warn(dev->mt76.dev, "Warning: MAC RX did not stop!\n");

	if (!mt76_poll(dev, MT_USB_DMA_CFG, MT_USB_DMA_CFG_RX_BUSY, 0, 1000))
		dev_warn(dev->mt76.dev, "Warning: RX DMA did not stop!\n");
}

void mt76x0_mac_stop(struct mt76x0_dev *dev)
{
	cancel_delayed_work_sync(&dev->cal_work);
	cancel_delayed_work_sync(&dev->mac_work);
	mt76u_stop_stat_wk(&dev->mt76);
	mt76x0_mac_stop_hw(dev);
}
EXPORT_SYMBOL_GPL(mt76x0_mac_stop);

int mt76x0_init_hardware(struct mt76x0_dev *dev)
{
	int ret;

	if (!mt76x02_wait_for_wpdma(&dev->mt76, 1000))
		return -EIO;

	/* Wait for ASIC ready after FW load. */
	if (!mt76x02_wait_for_mac(&dev->mt76))
		return -ETIMEDOUT;

	mt76x0_reset_csr_bbp(dev);
	mt76x0_init_usb_dma(dev);

	mt76_wr(dev, MT_HEADER_TRANS_CTRL_REG, 0x0);
	mt76_wr(dev, MT_TSO_CTRL, 0x0);

	ret = mt76x02_mcu_function_select(&dev->mt76, Q_SELECT, 1, false);
	if (ret)
		return ret;

	mt76x0_init_mac_registers(dev);

	if (!mt76x02_wait_for_txrx_idle(&dev->mt76))
		return -EIO;

	ret = mt76x0_init_bbp(dev);
	if (ret)
		return ret;

	ret = mt76x0_init_wcid_mem(dev);
	if (ret)
		return ret;

	mt76x0_init_key_mem(dev);

	ret = mt76x0_init_wcid_attr_mem(dev);
	if (ret)
		return ret;

	mt76_clear(dev, MT_BEACON_TIME_CFG, (MT_BEACON_TIME_CFG_TIMER_EN |
					     MT_BEACON_TIME_CFG_SYNC_MODE |
					     MT_BEACON_TIME_CFG_TBTT_EN |
					     MT_BEACON_TIME_CFG_BEACON_TX));

	mt76x0_reset_counters(dev);

	mt76_rmw(dev, MT_US_CYC_CFG, MT_US_CYC_CNT, 0x1e);

	mt76_wr(dev, MT_TXOP_CTRL_CFG,
		   FIELD_PREP(MT_TXOP_TRUN_EN, 0x3f) |
		   FIELD_PREP(MT_TXOP_EXT_CCA_DLY, 0x58));

	ret = mt76x0_eeprom_init(dev);
	if (ret)
		return ret;

	mt76x0_phy_init(dev);

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x0_init_hardware);

void mt76x0_cleanup(struct mt76x0_dev *dev)
{
	clear_bit(MT76_STATE_INITIALIZED, &dev->mt76.state);
	mt76x0_chip_onoff(dev, false, false);
	mt76u_queues_deinit(&dev->mt76);
	mt76u_mcu_deinit(&dev->mt76);
}
EXPORT_SYMBOL_GPL(mt76x0_cleanup);

struct mt76x0_dev *
mt76x0_alloc_device(struct device *pdev, const struct mt76_driver_ops *drv_ops)
{
	struct mt76x0_dev *dev;
	struct mt76_dev *mdev;

	mdev = mt76_alloc_device(sizeof(*dev), &mt76x0_ops);
	if (!mdev)
		return NULL;

	mdev->dev = pdev;
	mdev->drv = drv_ops;

	dev = container_of(mdev, struct mt76x0_dev, mt76);
	mutex_init(&dev->reg_atomic_mutex);
	mutex_init(&dev->hw_atomic_mutex);
	spin_lock_init(&dev->mac_lock);
	spin_lock_init(&dev->con_mon_lock);
	atomic_set(&dev->avg_ampdu_len, 1);

	return dev;
}
EXPORT_SYMBOL_GPL(mt76x0_alloc_device);

int mt76x0_register_device(struct mt76x0_dev *dev)
{
	struct mt76_dev *mdev = &dev->mt76;
	struct ieee80211_hw *hw = mdev->hw;
	struct wiphy *wiphy = hw->wiphy;
	int ret;

	ret = mt76x0_init_hardware(dev);
	if (ret)
		return ret;

	/* Reserve WCID 0 for mcast - thanks to this APs WCID will go to
	 * entry no. 1 like it does in the vendor driver.
	 */
	mdev->wcid_mask[0] |= 1;

	/* init fake wcid for monitor interfaces */
	mdev->global_wcid.idx = 0xff;
	mdev->global_wcid.hw_key_idx = -1;

	/* init antenna configuration */
	mdev->antenna_mask = 1;

	hw->queues = 4;
	hw->max_rates = 1;
	hw->max_report_rates = 7;
	hw->max_rate_tries = 1;
	hw->extra_tx_headroom = sizeof(struct mt76x02_txwi) + 4 + 2;

	hw->sta_data_size = sizeof(struct mt76x02_sta);
	hw->vif_data_size = sizeof(struct mt76x02_vif);

	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);

	INIT_DELAYED_WORK(&dev->mac_work, mt76x0_mac_work);

	ret = mt76_register_device(mdev, true, mt76x02_rates,
				   ARRAY_SIZE(mt76x02_rates));
	if (ret)
		return ret;

	/* overwrite unsupported features */
	if (mdev->cap.has_5ghz)
		mt76x0_vht_cap_mask(&dev->mt76.sband_5g.sband);

	/* check hw sg support in order to enable AMSDU */
	if (mt76u_check_sg(mdev))
		hw->max_tx_fragments = MT_SG_MAX_SIZE;
	else
		hw->max_tx_fragments = 1;

	mt76x0_init_debugfs(dev);

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x0_register_device);
