// SPDX-License-Identifier: ISC
/* Copyright (C) 2023 MediaTek Inc. */

#include "mt7925.h"
#include "../dma.h"
#include "mac.h"

int mt7925e_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
			   enum mt76_txq_id qid, struct mt76_wcid *wcid,
			   struct ieee80211_sta *sta,
			   struct mt76_tx_info *tx_info)
{
	struct mt792x_dev *dev = container_of(mdev, struct mt792x_dev, mt76);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx_info->skb);
	struct ieee80211_key_conf *key = info->control.hw_key;
	struct mt76_connac_hw_txp *txp;
	struct mt76_txwi_cache *t;
	int id, pid;
	u8 *txwi = (u8 *)txwi_ptr;

	if (unlikely(tx_info->skb->len <= ETH_HLEN))
		return -EINVAL;

	if (!wcid)
		wcid = &dev->mt76.global_wcid;

	t = (struct mt76_txwi_cache *)(txwi + mdev->drv->txwi_size);
	t->skb = tx_info->skb;

	id = mt76_token_consume(mdev, &t);
	if (id < 0)
		return id;

	if (sta) {
		struct mt792x_sta *msta = (struct mt792x_sta *)sta->drv_priv;

		if (time_after(jiffies, msta->last_txs + HZ / 4)) {
			info->flags |= IEEE80211_TX_CTL_REQ_TX_STATUS;
			msta->last_txs = jiffies;
		}
	}

	pid = mt76_tx_status_skb_add(mdev, wcid, tx_info->skb);
	mt7925_mac_write_txwi(mdev, txwi_ptr, tx_info->skb, wcid, key,
			      pid, qid, 0);

	txp = (struct mt76_connac_hw_txp *)(txwi + MT_TXD_SIZE);
	memset(txp, 0, sizeof(struct mt76_connac_hw_txp));
	mt76_connac_write_hw_txp(mdev, tx_info, txp, id);

	tx_info->skb = NULL;

	return 0;
}

void mt7925_tx_token_put(struct mt792x_dev *dev)
{
	struct mt76_txwi_cache *txwi;
	int id;

	spin_lock_bh(&dev->mt76.token_lock);
	idr_for_each_entry(&dev->mt76.token, txwi, id) {
		mt7925_txwi_free(dev, txwi, NULL, false, NULL);
		dev->mt76.token_count--;
	}
	spin_unlock_bh(&dev->mt76.token_lock);
	idr_destroy(&dev->mt76.token);
}

int mt7925e_mac_reset(struct mt792x_dev *dev)
{
	const struct mt792x_irq_map *irq_map = dev->irq_map;
	int i, err;

	mt792xe_mcu_drv_pmctrl(dev);

	mt76_connac_free_pending_tx_skbs(&dev->pm, NULL);

	mt76_wr(dev, dev->irq_map->host_irq_enable, 0);
	mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0x0);

	set_bit(MT76_RESET, &dev->mphy.state);
	set_bit(MT76_MCU_RESET, &dev->mphy.state);
	wake_up(&dev->mt76.mcu.wait);
	skb_queue_purge(&dev->mt76.mcu.res_q);

	mt76_txq_schedule_all(&dev->mphy);

	mt76_worker_disable(&dev->mt76.tx_worker);
	if (irq_map->rx.data_complete_mask)
		napi_disable(&dev->mt76.napi[MT_RXQ_MAIN]);
	if (irq_map->rx.wm_complete_mask)
		napi_disable(&dev->mt76.napi[MT_RXQ_MCU]);
	if (irq_map->rx.wm2_complete_mask)
		napi_disable(&dev->mt76.napi[MT_RXQ_MCU_WA]);
	if (irq_map->tx.all_complete_mask)
		napi_disable(&dev->mt76.tx_napi);

	mt7925_tx_token_put(dev);
	idr_init(&dev->mt76.token);

	mt792x_wpdma_reset(dev, true);

	local_bh_disable();
	mt76_for_each_q_rx(&dev->mt76, i) {
		napi_enable(&dev->mt76.napi[i]);
		napi_schedule(&dev->mt76.napi[i]);
	}
	napi_enable(&dev->mt76.tx_napi);
	napi_schedule(&dev->mt76.tx_napi);
	local_bh_enable();

	dev->fw_assert = false;
	clear_bit(MT76_MCU_RESET, &dev->mphy.state);

	mt76_wr(dev, dev->irq_map->host_irq_enable,
		dev->irq_map->tx.all_complete_mask |
		MT_INT_RX_DONE_ALL | MT_INT_MCU_CMD);
	mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0xff);

	err = mt792xe_mcu_fw_pmctrl(dev);
	if (err)
		return err;

	err = __mt792xe_mcu_drv_pmctrl(dev);
	if (err)
		goto out;

	err = mt7925_run_firmware(dev);
	if (err)
		goto out;

	err = mt7925_mcu_set_eeprom(dev);
	if (err)
		goto out;

	err = mt7925_mac_init(dev);
	if (err)
		goto out;

	err = __mt7925_start(&dev->phy);
out:
	clear_bit(MT76_RESET, &dev->mphy.state);

	mt76_worker_enable(&dev->mt76.tx_worker);

	return err;
}
