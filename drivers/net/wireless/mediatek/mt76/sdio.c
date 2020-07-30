// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc.
 *
 * This file is written based on mt76/usb.c.
 *
 * Author: Felix Fietkau <nbd@nbd.name>
 *	   Lorenzo Bianconi <lorenzo@kernel.org>
 *	   Sean Wang <sean.wang@mediatek.com>
 */

#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mmc/sdio_func.h>
#include <linux/sched.h>
#include <linux/kthread.h>

#include "mt76.h"

static int
mt76s_alloc_rx_queue(struct mt76_dev *dev, enum mt76_rxq_id qid)
{
	struct mt76_queue *q = &dev->q_rx[qid];

	spin_lock_init(&q->lock);
	q->entry = devm_kcalloc(dev->dev,
				MT_NUM_RX_ENTRIES, sizeof(*q->entry),
				GFP_KERNEL);
	if (!q->entry)
		return -ENOMEM;

	q->ndesc = MT_NUM_RX_ENTRIES;
	q->head = q->tail = 0;
	q->queued = 0;

	return 0;
}

static int mt76s_alloc_tx(struct mt76_dev *dev)
{
	struct mt76_queue *q;
	int i;

	for (i = 0; i < MT_TXQ_MCU_WA; i++) {
		INIT_LIST_HEAD(&dev->q_tx[i].swq);

		q = devm_kzalloc(dev->dev, sizeof(*q), GFP_KERNEL);
		if (!q)
			return -ENOMEM;

		spin_lock_init(&q->lock);
		q->hw_idx = i;
		dev->q_tx[i].q = q;

		q->entry = devm_kcalloc(dev->dev,
					MT_NUM_TX_ENTRIES, sizeof(*q->entry),
					GFP_KERNEL);
		if (!q->entry)
			return -ENOMEM;

		q->ndesc = MT_NUM_TX_ENTRIES;
	}

	return 0;
}

void mt76s_stop_txrx(struct mt76_dev *dev)
{
	struct mt76_sdio *sdio = &dev->sdio;

	cancel_work_sync(&sdio->tx_work);
	cancel_work_sync(&sdio->rx_work);
	cancel_work_sync(&sdio->work);
	cancel_work_sync(&sdio->stat_work);
	clear_bit(MT76_READING_STATS, &dev->phy.state);

	mt76_tx_status_check(dev, NULL, true);
}
EXPORT_SYMBOL_GPL(mt76s_stop_txrx);

int mt76s_alloc_queues(struct mt76_dev *dev)
{
	int err;

	err = mt76s_alloc_rx_queue(dev, MT_RXQ_MAIN);
	if (err < 0)
		return err;

	return mt76s_alloc_tx(dev);
}
EXPORT_SYMBOL_GPL(mt76s_alloc_queues);

static struct mt76_queue_entry *
mt76s_get_next_rx_entry(struct mt76_queue *q)
{
	struct mt76_queue_entry *e = NULL;

	spin_lock_bh(&q->lock);
	if (q->queued > 0) {
		e = &q->entry[q->head];
		q->head = (q->head + 1) % q->ndesc;
		q->queued--;
	}
	spin_unlock_bh(&q->lock);

	return e;
}

static int
mt76s_process_rx_queue(struct mt76_dev *dev, struct mt76_queue *q)
{
	int qid = q - &dev->q_rx[MT_RXQ_MAIN];
	int nframes = 0;

	while (true) {
		struct mt76_queue_entry *e;

		if (!test_bit(MT76_STATE_INITIALIZED, &dev->phy.state))
			break;

		e = mt76s_get_next_rx_entry(q);
		if (!e || !e->skb)
			break;

		dev->drv->rx_skb(dev, MT_RXQ_MAIN, e->skb);
		e->skb = NULL;
		nframes++;
	}
	if (qid == MT_RXQ_MAIN)
		mt76_rx_poll_complete(dev, MT_RXQ_MAIN, NULL);

	return nframes;
}

static int mt76s_process_tx_queue(struct mt76_dev *dev, enum mt76_txq_id qid)
{
	struct mt76_sw_queue *sq = &dev->q_tx[qid];
	u32 n_dequeued = 0, n_sw_dequeued = 0;
	struct mt76_queue_entry entry;
	struct mt76_queue *q = sq->q;
	bool wake;

	while (q->queued > n_dequeued) {
		if (!q->entry[q->head].done)
			break;

		if (q->entry[q->head].schedule) {
			q->entry[q->head].schedule = false;
			n_sw_dequeued++;
		}

		entry = q->entry[q->head];
		q->entry[q->head].done = false;
		q->head = (q->head + 1) % q->ndesc;
		n_dequeued++;

		if (qid == MT_TXQ_MCU)
			dev_kfree_skb(entry.skb);
		else
			dev->drv->tx_complete_skb(dev, qid, &entry);
	}

	spin_lock_bh(&q->lock);

	sq->swq_queued -= n_sw_dequeued;
	q->queued -= n_dequeued;

	wake = q->stopped && q->queued < q->ndesc - 8;
	if (wake)
		q->stopped = false;

	if (!q->queued)
		wake_up(&dev->tx_wait);

	spin_unlock_bh(&q->lock);

	if (qid == MT_TXQ_MCU)
		goto out;

	mt76_txq_schedule(&dev->phy, qid);

	if (wake)
		ieee80211_wake_queue(dev->hw, qid);

out:
	return n_dequeued;
}

static void mt76s_tx_status_data(struct work_struct *work)
{
	struct mt76_sdio *sdio;
	struct mt76_dev *dev;
	u8 update = 1;
	u16 count = 0;

	sdio = container_of(work, struct mt76_sdio, stat_work);
	dev = container_of(sdio, struct mt76_dev, sdio);

	while (true) {
		if (test_bit(MT76_REMOVED, &dev->phy.state))
			break;

		if (!dev->drv->tx_status_data(dev, &update))
			break;
		count++;
	}

	if (count && test_bit(MT76_STATE_RUNNING, &dev->phy.state))
		queue_work(dev->wq, &sdio->stat_work);
	else
		clear_bit(MT76_READING_STATS, &dev->phy.state);
}

static int
mt76s_tx_queue_skb(struct mt76_dev *dev, enum mt76_txq_id qid,
		   struct sk_buff *skb, struct mt76_wcid *wcid,
		   struct ieee80211_sta *sta)
{
	struct mt76_queue *q = dev->q_tx[qid].q;
	struct mt76_tx_info tx_info = {
		.skb = skb,
	};
	int err, len = skb->len;
	u16 idx = q->tail;

	if (q->queued == q->ndesc)
		return -ENOSPC;

	skb->prev = skb->next = NULL;
	err = dev->drv->tx_prepare_skb(dev, NULL, qid, wcid, sta, &tx_info);
	if (err < 0)
		return err;

	q->entry[q->tail].skb = tx_info.skb;
	q->entry[q->tail].buf_sz = len;
	q->tail = (q->tail + 1) % q->ndesc;
	q->queued++;

	return idx;
}

static int
mt76s_tx_queue_skb_raw(struct mt76_dev *dev, enum mt76_txq_id qid,
		       struct sk_buff *skb, u32 tx_info)
{
	struct mt76_queue *q = dev->q_tx[qid].q;
	int ret = -ENOSPC, len = skb->len;

	if (q->queued == q->ndesc)
		goto error;

	ret = mt76_skb_adjust_pad(skb);
	if (ret)
		goto error;

	spin_lock_bh(&q->lock);

	q->entry[q->tail].buf_sz = len;
	q->entry[q->tail].skb = skb;
	q->tail = (q->tail + 1) % q->ndesc;
	q->queued++;

	spin_unlock_bh(&q->lock);

	return 0;

error:
	dev_kfree_skb(skb);

	return ret;
}

static void mt76s_tx_kick(struct mt76_dev *dev, struct mt76_queue *q)
{
	struct mt76_sdio *sdio = &dev->sdio;

	queue_work(sdio->txrx_wq, &sdio->tx_work);
}

static const struct mt76_queue_ops sdio_queue_ops = {
	.tx_queue_skb = mt76s_tx_queue_skb,
	.kick = mt76s_tx_kick,
	.tx_queue_skb_raw = mt76s_tx_queue_skb_raw,
};

static void mt76s_txrx_work(struct work_struct *work)
{
	struct mt76_sdio *sdio = container_of(work, struct mt76_sdio, work);
	struct mt76_dev *dev = container_of(sdio, struct mt76_dev, sdio);
	int i;

	/* rx processing */
	local_bh_disable();
	rcu_read_lock();

	mt76_for_each_q_rx(dev, i)
		mt76s_process_rx_queue(dev, &dev->q_rx[i]);

	rcu_read_unlock();
	local_bh_enable();

	/* tx processing */
	for (i = 0; i < MT_TXQ_MCU_WA; i++)
		mt76s_process_tx_queue(dev, i);

	if (dev->drv->tx_status_data &&
	    !test_and_set_bit(MT76_READING_STATS, &dev->phy.state))
		queue_work(dev->wq, &dev->sdio.stat_work);
}

void mt76s_deinit(struct mt76_dev *dev)
{
	struct mt76_sdio *sdio = &dev->sdio;
	int i;

	mt76s_stop_txrx(dev);
	if (sdio->txrx_wq) {
		destroy_workqueue(sdio->txrx_wq);
		sdio->txrx_wq = NULL;
	}

	sdio_claim_host(sdio->func);
	sdio_release_irq(sdio->func);
	sdio_release_host(sdio->func);

	mt76_for_each_q_rx(dev, i) {
		struct mt76_queue *q = &dev->q_rx[i];
		int j;

		for (j = 0; j < q->ndesc; j++) {
			struct mt76_queue_entry *e = &q->entry[j];

			if (!e->skb)
				continue;

			dev_kfree_skb(e->skb);
			e->skb = NULL;
		}
	}
}
EXPORT_SYMBOL_GPL(mt76s_deinit);

int mt76s_init(struct mt76_dev *dev, struct sdio_func *func,
	       const struct mt76_bus_ops *bus_ops)
{
	struct mt76_sdio *sdio = &dev->sdio;

	sdio->txrx_wq = alloc_workqueue("mt76s_txrx_wq",
					WQ_UNBOUND | WQ_HIGHPRI,
					WQ_UNBOUND_MAX_ACTIVE);
	if (!sdio->txrx_wq)
		return -ENOMEM;

	INIT_WORK(&sdio->stat_work, mt76s_tx_status_data);
	INIT_WORK(&sdio->work, mt76s_txrx_work);

	mutex_init(&sdio->sched.lock);
	dev->queue_ops = &sdio_queue_ops;
	dev->bus = bus_ops;
	dev->sdio.func = func;

	return 0;
}
EXPORT_SYMBOL_GPL(mt76s_init);

MODULE_AUTHOR("Sean Wang <sean.wang@mediatek.com>");
MODULE_AUTHOR("Lorenzo Bianconi <lorenzo@kernel.org>");
MODULE_LICENSE("Dual BSD/GPL");
