// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019 MediaTek Inc.
 *
 * Author: Felix Fietkau <nbd@nbd.name>
 *	   Lorenzo Bianconi <lorenzo@kernel.org>
 *	   Sean Wang <sean.wang@mediatek.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include "mt7615.h"
#include "mac.h"
#include "regs.h"

static int mt7663u_dma_sched_init(struct mt7615_dev *dev)
{
	int i;

	mt76_rmw(dev, MT_DMA_SHDL(MT_DMASHDL_PKT_MAX_SIZE),
		 MT_DMASHDL_PKT_MAX_SIZE_PLE | MT_DMASHDL_PKT_MAX_SIZE_PSE,
		 FIELD_PREP(MT_DMASHDL_PKT_MAX_SIZE_PLE, 1) |
		 FIELD_PREP(MT_DMASHDL_PKT_MAX_SIZE_PSE, 8));

	/* disable refill group 5 - group 15 and raise group 2
	 * and 3 as high priority.
	 */
	mt76_wr(dev, MT_DMA_SHDL(MT_DMASHDL_REFILL), 0xffe00006);
	mt76_clear(dev, MT_DMA_SHDL(MT_DMASHDL_PAGE), BIT(16));

	for (i = 0; i < 5; i++)
		mt76_wr(dev, MT_DMA_SHDL(MT_DMASHDL_GROUP_QUOTA(i)),
			FIELD_PREP(MT_DMASHDL_GROUP_QUOTA_MIN, 0x3) |
			FIELD_PREP(MT_DMASHDL_GROUP_QUOTA_MAX, 0x1ff));

	mt76_wr(dev, MT_DMA_SHDL(MT_DMASHDL_Q_MAP(0)), 0x42104210);
	mt76_wr(dev, MT_DMA_SHDL(MT_DMASHDL_Q_MAP(1)), 0x42104210);

	mt76_wr(dev, MT_DMA_SHDL(MT_DMASHDL_Q_MAP(2)), 0x4444);

	/* group pririority from high to low:
	 * 15 (cmd groups) > 4 > 3 > 2 > 1 > 0.
	 */
	mt76_wr(dev, MT_DMA_SHDL(MT_DMASHDL_SCHED_SET0), 0x6501234f);
	mt76_wr(dev, MT_DMA_SHDL(MT_DMASHDL_SCHED_SET1), 0xedcba987);
	mt76_wr(dev, MT_DMA_SHDL(MT_DMASHDL_OPTIONAL), 0x7004801c);

	mt76_wr(dev, MT_UDMA_WLCFG_1,
		FIELD_PREP(MT_WL_TX_TMOUT_LMT, 80000) |
		FIELD_PREP(MT_WL_RX_AGG_PKT_LMT, 1));

	/* setup UDMA Rx Flush */
	mt76_clear(dev, MT_UDMA_WLCFG_0, MT_WL_RX_FLUSH);
	/* hif reset */
	mt76_set(dev, MT_HIF_RST, MT_HIF_LOGIC_RST_N);

	mt76_set(dev, MT_UDMA_WLCFG_0,
		 MT_WL_RX_AGG_EN | MT_WL_RX_EN | MT_WL_TX_EN |
		 MT_WL_RX_MPSZ_PAD0 | MT_TICK_1US_EN |
		 MT_WL_TX_TMOUT_FUNC_EN);
	mt76_rmw(dev, MT_UDMA_WLCFG_0, MT_WL_RX_AGG_LMT | MT_WL_RX_AGG_TO,
		 FIELD_PREP(MT_WL_RX_AGG_LMT, 32) |
		 FIELD_PREP(MT_WL_RX_AGG_TO, 100));

	return 0;
}

static int mt7663u_init_hardware(struct mt7615_dev *dev)
{
	int ret, idx;

	ret = mt7615_eeprom_init(dev, MT_EFUSE_BASE);
	if (ret < 0)
		return ret;

	ret = mt7663u_dma_sched_init(dev);
	if (ret)
		return ret;

	set_bit(MT76_STATE_INITIALIZED, &dev->mphy.state);

	/* Beacon and mgmt frames should occupy wcid 0 */
	idx = mt76_wcid_alloc(dev->mt76.wcid_mask, MT7615_WTBL_STA - 1);
	if (idx)
		return -ENOSPC;

	dev->mt76.global_wcid.idx = idx;
	dev->mt76.global_wcid.hw_key_idx = -1;
	rcu_assign_pointer(dev->mt76.wcid[idx], &dev->mt76.global_wcid);

	return 0;
}

static void mt7663u_init_work(struct work_struct *work)
{
	struct mt7615_dev *dev;

	dev = container_of(work, struct mt7615_dev, mcu_work);
	if (mt7663u_mcu_init(dev))
		return;

	mt7615_mcu_set_eeprom(dev);
	mt7615_mac_init(dev);
	mt7615_phy_init(dev);
	mt7615_mcu_del_wtbl_all(dev);
	mt7615_check_offload_capability(dev);
}

int mt7663u_register_device(struct mt7615_dev *dev)
{
	struct ieee80211_hw *hw = mt76_hw(dev);
	int err;

	INIT_WORK(&dev->wtbl_work, mt7663u_wtbl_work);
	INIT_WORK(&dev->mcu_work, mt7663u_init_work);
	INIT_LIST_HEAD(&dev->wd_head);
	mt7615_init_device(dev);

	err = mt7663u_init_hardware(dev);
	if (err)
		return err;

	hw->extra_tx_headroom += MT_USB_HDR_SIZE + MT_USB_TXD_SIZE;
	/* check hw sg support in order to enable AMSDU */
	hw->max_tx_fragments = dev->mt76.usb.sg_en ? MT_HW_TXP_MAX_BUF_NUM : 1;

	err = mt76_register_device(&dev->mt76, true, mt7615_rates,
				   ARRAY_SIZE(mt7615_rates));
	if (err < 0)
		return err;

	if (!dev->mt76.usb.sg_en) {
		struct ieee80211_sta_vht_cap *vht_cap;

		/* decrease max A-MSDU size if SG is not supported */
		vht_cap = &dev->mphy.sband_5g.sband.vht_cap;
		vht_cap->cap &= ~IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454;
	}

	ieee80211_queue_work(hw, &dev->mcu_work);
	mt7615_init_txpower(dev, &dev->mphy.sband_2g.sband);
	mt7615_init_txpower(dev, &dev->mphy.sband_5g.sband);

	return mt7615_init_debugfs(dev);
}
