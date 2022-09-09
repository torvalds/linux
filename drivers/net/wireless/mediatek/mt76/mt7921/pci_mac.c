// SPDX-License-Identifier: ISC
/* Copyright (C) 2021 MediaTek Inc. */

#include "mt7921.h"
#include "../dma.h"
#include "mac.h"

int mt7921e_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
			   enum mt76_txq_id qid, struct mt76_wcid *wcid,
			   struct ieee80211_sta *sta,
			   struct mt76_tx_info *tx_info)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);
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
		struct mt7921_sta *msta = (struct mt7921_sta *)sta->drv_priv;

		if (time_after(jiffies, msta->last_txs + HZ / 4)) {
			info->flags |= IEEE80211_TX_CTL_REQ_TX_STATUS;
			msta->last_txs = jiffies;
		}
	}

	pid = mt76_tx_status_skb_add(mdev, wcid, tx_info->skb);
	mt76_connac2_mac_write_txwi(mdev, txwi_ptr, tx_info->skb, wcid, key,
				    pid, qid, 0);

	txp = (struct mt76_connac_hw_txp *)(txwi + MT_TXD_SIZE);
	memset(txp, 0, sizeof(struct mt76_connac_hw_txp));
	mt76_connac_write_hw_txp(mdev, tx_info, txp, id);

	tx_info->skb = DMA_DUMMY_DATA;

	return 0;
}

static void
mt7921_txwi_free(struct mt7921_dev *dev, struct mt76_txwi_cache *t,
		 struct ieee80211_sta *sta, bool clear_status,
		 struct list_head *free_list)
{
	struct mt76_dev *mdev = &dev->mt76;
	__le32 *txwi;
	u16 wcid_idx;

	mt76_connac_txp_skb_unmap(mdev, t);
	if (!t->skb)
		goto out;

	txwi = (__le32 *)mt76_get_txwi_ptr(mdev, t);
	if (sta) {
		struct mt76_wcid *wcid = (struct mt76_wcid *)sta->drv_priv;

		if (likely(t->skb->protocol != cpu_to_be16(ETH_P_PAE)))
			mt7921_tx_check_aggr(sta, txwi);

		wcid_idx = wcid->idx;
	} else {
		wcid_idx = le32_get_bits(txwi[1], MT_TXD1_WLAN_IDX);
	}

	__mt76_tx_complete_skb(mdev, wcid_idx, t->skb, free_list);

out:
	t->skb = NULL;
	mt76_put_txwi(mdev, t);
}

static void
mt7921e_mac_tx_free(struct mt7921_dev *dev, void *data, int len)
{
	struct mt76_connac_tx_free *free = data;
	__le32 *tx_info = (__le32 *)(data + sizeof(*free));
	struct mt76_dev *mdev = &dev->mt76;
	struct mt76_txwi_cache *txwi;
	struct ieee80211_sta *sta = NULL;
	struct sk_buff *skb, *tmp;
	void *end = data + len;
	LIST_HEAD(free_list);
	bool wake = false;
	u8 i, count;

	/* clean DMA queues and unmap buffers first */
	mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[MT_TXQ_PSD], false);
	mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[MT_TXQ_BE], false);

	count = le16_get_bits(free->ctrl, MT_TX_FREE_MSDU_CNT);
	if (WARN_ON_ONCE((void *)&tx_info[count] > end))
		return;

	for (i = 0; i < count; i++) {
		u32 msdu, info = le32_to_cpu(tx_info[i]);
		u8 stat;

		/* 1'b1: new wcid pair.
		 * 1'b0: msdu_id with the same 'wcid pair' as above.
		 */
		if (info & MT_TX_FREE_PAIR) {
			struct mt7921_sta *msta;
			struct mt76_wcid *wcid;
			u16 idx;

			count++;
			idx = FIELD_GET(MT_TX_FREE_WLAN_ID, info);
			wcid = rcu_dereference(dev->mt76.wcid[idx]);
			sta = wcid_to_sta(wcid);
			if (!sta)
				continue;

			msta = container_of(wcid, struct mt7921_sta, wcid);
			spin_lock_bh(&dev->sta_poll_lock);
			if (list_empty(&msta->poll_list))
				list_add_tail(&msta->poll_list, &dev->sta_poll_list);
			spin_unlock_bh(&dev->sta_poll_lock);
			continue;
		}

		msdu = FIELD_GET(MT_TX_FREE_MSDU_ID, info);
		stat = FIELD_GET(MT_TX_FREE_STATUS, info);

		txwi = mt76_token_release(mdev, msdu, &wake);
		if (!txwi)
			continue;

		mt7921_txwi_free(dev, txwi, sta, stat, &free_list);
	}

	if (wake)
		mt76_set_tx_blocked(&dev->mt76, false);

	list_for_each_entry_safe(skb, tmp, &free_list, list) {
		skb_list_del_init(skb);
		napi_consume_skb(skb, 1);
	}

	rcu_read_lock();
	mt7921_mac_sta_poll(dev);
	rcu_read_unlock();

	mt76_worker_schedule(&dev->mt76.tx_worker);
}

bool mt7921e_rx_check(struct mt76_dev *mdev, void *data, int len)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);
	__le32 *rxd = (__le32 *)data;
	__le32 *end = (__le32 *)&rxd[len / 4];
	enum rx_pkt_type type;

	type = le32_get_bits(rxd[0], MT_RXD0_PKT_TYPE);

	switch (type) {
	case PKT_TYPE_TXRX_NOTIFY:
		mt7921e_mac_tx_free(dev, data, len);
		return false;
	case PKT_TYPE_TXS:
		for (rxd += 2; rxd + 8 <= end; rxd += 8)
			mt7921_mac_add_txs(dev, rxd);
		return false;
	default:
		return true;
	}
}

void mt7921e_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			  struct sk_buff *skb)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);
	__le32 *rxd = (__le32 *)skb->data;
	enum rx_pkt_type type;

	type = le32_get_bits(rxd[0], MT_RXD0_PKT_TYPE);

	switch (type) {
	case PKT_TYPE_TXRX_NOTIFY:
		mt7921e_mac_tx_free(dev, skb->data, skb->len);
		napi_consume_skb(skb, 1);
		break;
	default:
		mt7921_queue_rx_skb(mdev, q, skb);
		break;
	}
}

void mt7921_tx_token_put(struct mt7921_dev *dev)
{
	struct mt76_txwi_cache *txwi;
	int id;

	spin_lock_bh(&dev->mt76.token_lock);
	idr_for_each_entry(&dev->mt76.token, txwi, id) {
		mt7921_txwi_free(dev, txwi, NULL, false, NULL);
		dev->mt76.token_count--;
	}
	spin_unlock_bh(&dev->mt76.token_lock);
	idr_destroy(&dev->mt76.token);
}

int mt7921e_mac_reset(struct mt7921_dev *dev)
{
	int i, err;

	mt7921e_mcu_drv_pmctrl(dev);

	mt76_connac_free_pending_tx_skbs(&dev->pm, NULL);

	mt76_wr(dev, MT_WFDMA0_HOST_INT_ENA, 0);
	mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0x0);

	set_bit(MT76_RESET, &dev->mphy.state);
	set_bit(MT76_MCU_RESET, &dev->mphy.state);
	wake_up(&dev->mt76.mcu.wait);
	skb_queue_purge(&dev->mt76.mcu.res_q);

	mt76_txq_schedule_all(&dev->mphy);

	mt76_worker_disable(&dev->mt76.tx_worker);
	napi_disable(&dev->mt76.napi[MT_RXQ_MAIN]);
	napi_disable(&dev->mt76.napi[MT_RXQ_MCU]);
	napi_disable(&dev->mt76.napi[MT_RXQ_MCU_WA]);
	napi_disable(&dev->mt76.tx_napi);

	mt7921_tx_token_put(dev);
	idr_init(&dev->mt76.token);

	mt7921_wpdma_reset(dev, true);

	local_bh_disable();
	mt76_for_each_q_rx(&dev->mt76, i) {
		napi_enable(&dev->mt76.napi[i]);
		napi_schedule(&dev->mt76.napi[i]);
	}
	local_bh_enable();

	dev->fw_assert = false;
	clear_bit(MT76_MCU_RESET, &dev->mphy.state);

	mt76_wr(dev, MT_WFDMA0_HOST_INT_ENA,
		MT_INT_RX_DONE_ALL | MT_INT_TX_DONE_ALL |
		MT_INT_MCU_CMD);
	mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0xff);

	err = mt7921e_driver_own(dev);
	if (err)
		goto out;

	err = mt7921_run_firmware(dev);
	if (err)
		goto out;

	err = mt7921_mcu_set_eeprom(dev);
	if (err)
		goto out;

	err = mt7921_mac_init(dev);
	if (err)
		goto out;

	err = __mt7921_start(&dev->phy);
out:
	clear_bit(MT76_RESET, &dev->mphy.state);

	local_bh_disable();
	napi_enable(&dev->mt76.tx_napi);
	napi_schedule(&dev->mt76.tx_napi);
	local_bh_enable();

	mt76_worker_enable(&dev->mt76.tx_worker);

	return err;
}
