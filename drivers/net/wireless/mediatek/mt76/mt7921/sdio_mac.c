// SPDX-License-Identifier: ISC
/* Copyright (C) 2021 MediaTek Inc. */

#include <linux/iopoll.h>
#include <linux/mmc/sdio_func.h>
#include "mt7921.h"
#include "mac.h"

static void
mt7921s_write_txwi(struct mt7921_dev *dev, struct mt76_wcid *wcid,
		   enum mt76_txq_id qid, struct ieee80211_sta *sta,
		   struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_key_conf *key = info->control.hw_key;
	__le32 *txwi;
	int pid;

	pid = mt76_tx_status_skb_add(&dev->mt76, wcid, skb);
	txwi = (__le32 *)(skb->data - MT_SDIO_TXD_SIZE);
	memset(txwi, 0, MT_SDIO_TXD_SIZE);
	mt7921_mac_write_txwi(dev, txwi, skb, wcid, key, pid, false);
	skb_push(skb, MT_SDIO_TXD_SIZE);
}

int mt7921s_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
			   enum mt76_txq_id qid, struct mt76_wcid *wcid,
			   struct ieee80211_sta *sta,
			   struct mt76_tx_info *tx_info)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx_info->skb);
	struct sk_buff *skb = tx_info->skb;
	int pad;

	if (unlikely(tx_info->skb->len <= ETH_HLEN))
		return -EINVAL;

	if (!wcid)
		wcid = &dev->mt76.global_wcid;

	if (sta) {
		struct mt7921_sta *msta = (struct mt7921_sta *)sta->drv_priv;

		if (time_after(jiffies, msta->last_txs + HZ / 4)) {
			info->flags |= IEEE80211_TX_CTL_REQ_TX_STATUS;
			msta->last_txs = jiffies;
		}
	}

	mt7921s_write_txwi(dev, wcid, qid, sta, skb);

	mt7921_skb_add_sdio_hdr(skb, MT7921_SDIO_DATA);
	pad = round_up(skb->len, 4) - skb->len;

	return mt76_skb_adjust_pad(skb, pad);
}

void mt7921s_tx_complete_skb(struct mt76_dev *mdev, struct mt76_queue_entry *e)
{
	__le32 *txwi = (__le32 *)(e->skb->data + MT_SDIO_HDR_SIZE);
	unsigned int headroom = MT_SDIO_TXD_SIZE + MT_SDIO_HDR_SIZE;
	struct ieee80211_sta *sta;
	struct mt76_wcid *wcid;
	u16 idx;

	idx = FIELD_GET(MT_TXD1_WLAN_IDX, le32_to_cpu(txwi[1]));
	wcid = rcu_dereference(mdev->wcid[idx]);
	sta = wcid_to_sta(wcid);

	if (sta && likely(e->skb->protocol != cpu_to_be16(ETH_P_PAE)))
		mt7921_tx_check_aggr(sta, txwi);

	skb_pull(e->skb, headroom);
	mt76_tx_complete_skb(mdev, e->wcid, e->skb);
}

bool mt7921s_tx_status_data(struct mt76_dev *mdev, u8 *update)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);

	mt7921_mutex_acquire(dev);
	mt7921_mac_sta_poll(dev);
	mt7921_mutex_release(dev);

	return 0;
}
