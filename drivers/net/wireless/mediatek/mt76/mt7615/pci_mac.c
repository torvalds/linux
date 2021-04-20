// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc.
 *
 * Author: Ryder Lee <ryder.lee@mediatek.com>
 *         Roy Luo <royluo@google.com>
 *         Felix Fietkau <nbd@nbd.name>
 *         Lorenzo Bianconi <lorenzo@kernel.org>
 */

#include <linux/etherdevice.h>
#include <linux/timekeeping.h>

#include "mt7615.h"
#include "../dma.h"
#include "mac.h"

void mt7615_tx_complete_skb(struct mt76_dev *mdev, struct mt76_queue_entry *e)
{
	if (!e->txwi) {
		dev_kfree_skb_any(e->skb);
		return;
	}

	/* error path */
	if (e->skb == DMA_DUMMY_DATA) {
		struct mt76_txwi_cache *t;
		struct mt7615_dev *dev;
		struct mt7615_txp_common *txp;
		u16 token;

		dev = container_of(mdev, struct mt7615_dev, mt76);
		txp = mt7615_txwi_to_txp(mdev, e->txwi);

		if (is_mt7615(&dev->mt76))
			token = le16_to_cpu(txp->fw.token);
		else
			token = le16_to_cpu(txp->hw.msdu_id[0]) &
				~MT_MSDU_ID_VALID;

		t = mt76_token_put(mdev, token);
		e->skb = t ? t->skb : NULL;
	}

	if (e->skb)
		mt76_tx_complete_skb(mdev, e->wcid, e->skb);
}

static void
mt7615_write_hw_txp(struct mt7615_dev *dev, struct mt76_tx_info *tx_info,
		    void *txp_ptr, u32 id)
{
	struct mt7615_hw_txp *txp = txp_ptr;
	struct mt7615_txp_ptr *ptr = &txp->ptr[0];
	int i, nbuf = tx_info->nbuf - 1;
	u32 last_mask;

	tx_info->buf[0].len = MT_TXD_SIZE + sizeof(*txp);
	tx_info->nbuf = 1;

	txp->msdu_id[0] = cpu_to_le16(id | MT_MSDU_ID_VALID);

	if (is_mt7663(&dev->mt76))
		last_mask = MT_TXD_LEN_LAST;
	else
		last_mask = MT_TXD_LEN_AMSDU_LAST |
			    MT_TXD_LEN_MSDU_LAST;

	for (i = 0; i < nbuf; i++) {
		u16 len = tx_info->buf[i + 1].len & MT_TXD_LEN_MASK;
		u32 addr = tx_info->buf[i + 1].addr;

		if (i == nbuf - 1)
			len |= last_mask;

		if (i & 1) {
			ptr->buf1 = cpu_to_le32(addr);
			ptr->len1 = cpu_to_le16(len);
			ptr++;
		} else {
			ptr->buf0 = cpu_to_le32(addr);
			ptr->len0 = cpu_to_le16(len);
		}
	}
}

static void
mt7615_write_fw_txp(struct mt7615_dev *dev, struct mt76_tx_info *tx_info,
		    void *txp_ptr, u32 id)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx_info->skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx_info->skb);
	struct ieee80211_key_conf *key = info->control.hw_key;
	struct ieee80211_vif *vif = info->control.vif;
	struct mt7615_fw_txp *txp = txp_ptr;
	int nbuf = tx_info->nbuf - 1;
	int i;

	for (i = 0; i < nbuf; i++) {
		txp->buf[i] = cpu_to_le32(tx_info->buf[i + 1].addr);
		txp->len[i] = cpu_to_le16(tx_info->buf[i + 1].len);
	}
	txp->nbuf = nbuf;

	/* pass partial skb header to fw */
	tx_info->buf[0].len = MT_TXD_SIZE + sizeof(*txp);
	tx_info->buf[1].len = MT_CT_PARSE_LEN;
	tx_info->buf[1].skip_unmap = true;
	tx_info->nbuf = MT_CT_DMA_BUF_NUM;

	txp->flags = cpu_to_le16(MT_CT_INFO_APPLY_TXD);

	if (!key)
		txp->flags |= cpu_to_le16(MT_CT_INFO_NONE_CIPHER_FRAME);

	if (ieee80211_is_mgmt(hdr->frame_control))
		txp->flags |= cpu_to_le16(MT_CT_INFO_MGMT_FRAME);

	if (vif) {
		struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;

		txp->bss_idx = mvif->idx;
	}

	txp->token = cpu_to_le16(id);
	txp->rept_wds_wcid = 0xff;
}

int mt7615_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
			  enum mt76_txq_id qid, struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta,
			  struct mt76_tx_info *tx_info)
{
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);
	struct mt7615_sta *msta = container_of(wcid, struct mt7615_sta, wcid);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx_info->skb);
	struct ieee80211_key_conf *key = info->control.hw_key;
	int pid, id;
	u8 *txwi = (u8 *)txwi_ptr;
	struct mt76_txwi_cache *t;
	void *txp;

	if (!wcid)
		wcid = &dev->mt76.global_wcid;

	pid = mt76_tx_status_skb_add(mdev, wcid, tx_info->skb);

	if (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE) {
		struct mt7615_phy *phy = &dev->phy;

		if ((info->hw_queue & MT_TX_HW_QUEUE_EXT_PHY) && mdev->phy2)
			phy = mdev->phy2->priv;

		spin_lock_bh(&dev->mt76.lock);
		mt7615_mac_set_rates(phy, msta, &info->control.rates[0],
				     msta->rates);
		spin_unlock_bh(&dev->mt76.lock);
	}

	t = (struct mt76_txwi_cache *)(txwi + mdev->drv->txwi_size);
	t->skb = tx_info->skb;

	id = mt76_token_get(mdev, &t);
	if (id < 0)
		return id;

	mt7615_mac_write_txwi(dev, txwi_ptr, tx_info->skb, wcid, sta,
			      pid, key, false);

	txp = txwi + MT_TXD_SIZE;
	memset(txp, 0, sizeof(struct mt7615_txp_common));
	if (is_mt7615(&dev->mt76))
		mt7615_write_fw_txp(dev, tx_info, txp, id);
	else
		mt7615_write_hw_txp(dev, tx_info, txp, id);

	tx_info->skb = DMA_DUMMY_DATA;

	return 0;
}

void mt7615_dma_reset(struct mt7615_dev *dev)
{
	int i;

	mt76_clear(dev, MT_WPDMA_GLO_CFG,
		   MT_WPDMA_GLO_CFG_RX_DMA_EN | MT_WPDMA_GLO_CFG_TX_DMA_EN |
		   MT_WPDMA_GLO_CFG_TX_WRITEBACK_DONE);

	usleep_range(1000, 2000);

	for (i = 0; i < __MT_TXQ_MAX; i++)
		mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[i], true);

	for (i = 0; i < __MT_MCUQ_MAX; i++)
		mt76_queue_tx_cleanup(dev, dev->mt76.q_mcu[i], true);

	mt76_for_each_q_rx(&dev->mt76, i)
		mt76_queue_rx_reset(dev, i);

	mt76_tx_status_check(&dev->mt76, NULL, true);

	mt7615_dma_start(dev);
}
EXPORT_SYMBOL_GPL(mt7615_dma_reset);

static void
mt7615_hif_int_event_trigger(struct mt7615_dev *dev, u8 event)
{
	u32 reg = MT_MCU_INT_EVENT;

	if (is_mt7663(&dev->mt76))
		reg = MT7663_MCU_INT_EVENT;

	mt76_wr(dev, reg, event);

	mt7622_trigger_hif_int(dev, true);
	mt7622_trigger_hif_int(dev, false);
}

static bool
mt7615_wait_reset_state(struct mt7615_dev *dev, u32 state)
{
	bool ret;

	ret = wait_event_timeout(dev->reset_wait,
				 (READ_ONCE(dev->reset_state) & state),
				 MT7615_RESET_TIMEOUT);
	WARN(!ret, "Timeout waiting for MCU reset state %x\n", state);
	return ret;
}

static void
mt7615_update_vif_beacon(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct ieee80211_hw *hw = priv;
	struct mt7615_dev *dev = mt7615_hw_dev(hw);

	switch (vif->type) {
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_AP:
		mt7615_mcu_add_beacon(dev, hw, vif,
				      vif->bss_conf.enable_beacon);
		break;
	default:
		break;
	}
}

static void
mt7615_update_beacons(struct mt7615_dev *dev)
{
	ieee80211_iterate_active_interfaces(dev->mt76.hw,
		IEEE80211_IFACE_ITER_RESUME_ALL,
		mt7615_update_vif_beacon, dev->mt76.hw);

	if (!dev->mt76.phy2)
		return;

	ieee80211_iterate_active_interfaces(dev->mt76.phy2->hw,
		IEEE80211_IFACE_ITER_RESUME_ALL,
		mt7615_update_vif_beacon, dev->mt76.phy2->hw);
}

void mt7615_mac_reset_work(struct work_struct *work)
{
	struct mt7615_phy *phy2;
	struct mt76_phy *ext_phy;
	struct mt7615_dev *dev;

	dev = container_of(work, struct mt7615_dev, reset_work);
	ext_phy = dev->mt76.phy2;
	phy2 = ext_phy ? ext_phy->priv : NULL;

	if (!(READ_ONCE(dev->reset_state) & MT_MCU_CMD_STOP_PDMA))
		return;

	ieee80211_stop_queues(mt76_hw(dev));
	if (ext_phy)
		ieee80211_stop_queues(ext_phy->hw);

	set_bit(MT76_RESET, &dev->mphy.state);
	set_bit(MT76_MCU_RESET, &dev->mphy.state);
	wake_up(&dev->mt76.mcu.wait);
	cancel_delayed_work_sync(&dev->mphy.mac_work);
	del_timer_sync(&dev->phy.roc_timer);
	cancel_work_sync(&dev->phy.roc_work);
	if (phy2) {
		set_bit(MT76_RESET, &phy2->mt76->state);
		cancel_delayed_work_sync(&phy2->mt76->mac_work);
		del_timer_sync(&phy2->roc_timer);
		cancel_work_sync(&phy2->roc_work);
	}

	/* lock/unlock all queues to ensure that no tx is pending */
	mt76_txq_schedule_all(&dev->mphy);
	if (ext_phy)
		mt76_txq_schedule_all(ext_phy);

	mt76_worker_disable(&dev->mt76.tx_worker);
	napi_disable(&dev->mt76.napi[0]);
	napi_disable(&dev->mt76.napi[1]);
	napi_disable(&dev->mt76.tx_napi);

	mt7615_mutex_acquire(dev);

	mt7615_hif_int_event_trigger(dev, MT_MCU_INT_EVENT_PDMA_STOPPED);

	if (mt7615_wait_reset_state(dev, MT_MCU_CMD_RESET_DONE)) {
		mt7615_dma_reset(dev);

		mt7615_tx_token_put(dev);
		idr_init(&dev->mt76.token);

		mt76_wr(dev, MT_WPDMA_MEM_RNG_ERR, 0);

		mt7615_hif_int_event_trigger(dev, MT_MCU_INT_EVENT_PDMA_INIT);
		mt7615_wait_reset_state(dev, MT_MCU_CMD_RECOVERY_DONE);
	}

	clear_bit(MT76_MCU_RESET, &dev->mphy.state);
	clear_bit(MT76_RESET, &dev->mphy.state);
	if (phy2)
		clear_bit(MT76_RESET, &phy2->mt76->state);

	mt76_worker_enable(&dev->mt76.tx_worker);
	napi_enable(&dev->mt76.tx_napi);
	napi_schedule(&dev->mt76.tx_napi);

	napi_enable(&dev->mt76.napi[0]);
	napi_schedule(&dev->mt76.napi[0]);

	napi_enable(&dev->mt76.napi[1]);
	napi_schedule(&dev->mt76.napi[1]);

	ieee80211_wake_queues(mt76_hw(dev));
	if (ext_phy)
		ieee80211_wake_queues(ext_phy->hw);

	mt7615_hif_int_event_trigger(dev, MT_MCU_INT_EVENT_RESET_DONE);
	mt7615_wait_reset_state(dev, MT_MCU_CMD_NORMAL_STATE);

	mt7615_update_beacons(dev);

	mt7615_mutex_release(dev);

	ieee80211_queue_delayed_work(mt76_hw(dev), &dev->mphy.mac_work,
				     MT7615_WATCHDOG_TIME);
	if (phy2)
		ieee80211_queue_delayed_work(ext_phy->hw,
					     &phy2->mt76->mac_work,
					     MT7615_WATCHDOG_TIME);

}
