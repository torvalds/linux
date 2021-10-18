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
#include "sdio.h"

static u32 mt76s_read_whisr(struct mt76_dev *dev)
{
	return sdio_readl(dev->sdio.func, MCR_WHISR, NULL);
}

u32 mt76s_read_pcr(struct mt76_dev *dev)
{
	struct mt76_sdio *sdio = &dev->sdio;

	return sdio_readl(sdio->func, MCR_WHLPCR, NULL);
}
EXPORT_SYMBOL_GPL(mt76s_read_pcr);

static u32 mt76s_read_mailbox(struct mt76_dev *dev, u32 offset)
{
	struct sdio_func *func = dev->sdio.func;
	u32 val = ~0, status;
	int err;

	sdio_claim_host(func);

	sdio_writel(func, offset, MCR_H2DSM0R, &err);
	if (err < 0) {
		dev_err(dev->dev, "failed setting address [err=%d]\n", err);
		goto out;
	}

	sdio_writel(func, H2D_SW_INT_READ, MCR_WSICR, &err);
	if (err < 0) {
		dev_err(dev->dev, "failed setting read mode [err=%d]\n", err);
		goto out;
	}

	err = readx_poll_timeout(mt76s_read_whisr, dev, status,
				 status & H2D_SW_INT_READ, 0, 1000000);
	if (err < 0) {
		dev_err(dev->dev, "query whisr timeout\n");
		goto out;
	}

	sdio_writel(func, H2D_SW_INT_READ, MCR_WHISR, &err);
	if (err < 0) {
		dev_err(dev->dev, "failed setting read mode [err=%d]\n", err);
		goto out;
	}

	val = sdio_readl(func, MCR_H2DSM0R, &err);
	if (err < 0) {
		dev_err(dev->dev, "failed reading h2dsm0r [err=%d]\n", err);
		goto out;
	}

	if (val != offset) {
		dev_err(dev->dev, "register mismatch\n");
		val = ~0;
		goto out;
	}

	val = sdio_readl(func, MCR_D2HRM1R, &err);
	if (err < 0)
		dev_err(dev->dev, "failed reading d2hrm1r [err=%d]\n", err);

out:
	sdio_release_host(func);

	return val;
}

static void mt76s_write_mailbox(struct mt76_dev *dev, u32 offset, u32 val)
{
	struct sdio_func *func = dev->sdio.func;
	u32 status;
	int err;

	sdio_claim_host(func);

	sdio_writel(func, offset, MCR_H2DSM0R, &err);
	if (err < 0) {
		dev_err(dev->dev, "failed setting address [err=%d]\n", err);
		goto out;
	}

	sdio_writel(func, val, MCR_H2DSM1R, &err);
	if (err < 0) {
		dev_err(dev->dev,
			"failed setting write value [err=%d]\n", err);
		goto out;
	}

	sdio_writel(func, H2D_SW_INT_WRITE, MCR_WSICR, &err);
	if (err < 0) {
		dev_err(dev->dev, "failed setting write mode [err=%d]\n", err);
		goto out;
	}

	err = readx_poll_timeout(mt76s_read_whisr, dev, status,
				 status & H2D_SW_INT_WRITE, 0, 1000000);
	if (err < 0) {
		dev_err(dev->dev, "query whisr timeout\n");
		goto out;
	}

	sdio_writel(func, H2D_SW_INT_WRITE, MCR_WHISR, &err);
	if (err < 0) {
		dev_err(dev->dev, "failed setting write mode [err=%d]\n", err);
		goto out;
	}

	val = sdio_readl(func, MCR_H2DSM0R, &err);
	if (err < 0) {
		dev_err(dev->dev, "failed reading h2dsm0r [err=%d]\n", err);
		goto out;
	}

	if (val != offset)
		dev_err(dev->dev, "register mismatch\n");

out:
	sdio_release_host(func);
}

u32 mt76s_rr(struct mt76_dev *dev, u32 offset)
{
	if (test_bit(MT76_STATE_MCU_RUNNING, &dev->phy.state))
		return dev->mcu_ops->mcu_rr(dev, offset);
	else
		return mt76s_read_mailbox(dev, offset);
}
EXPORT_SYMBOL_GPL(mt76s_rr);

void mt76s_wr(struct mt76_dev *dev, u32 offset, u32 val)
{
	if (test_bit(MT76_STATE_MCU_RUNNING, &dev->phy.state))
		dev->mcu_ops->mcu_wr(dev, offset, val);
	else
		mt76s_write_mailbox(dev, offset, val);
}
EXPORT_SYMBOL_GPL(mt76s_wr);

u32 mt76s_rmw(struct mt76_dev *dev, u32 offset, u32 mask, u32 val)
{
	val |= mt76s_rr(dev, offset) & ~mask;
	mt76s_wr(dev, offset, val);

	return val;
}
EXPORT_SYMBOL_GPL(mt76s_rmw);

void mt76s_write_copy(struct mt76_dev *dev, u32 offset,
		      const void *data, int len)
{
	const u32 *val = data;
	int i;

	for (i = 0; i < len / sizeof(u32); i++) {
		mt76s_wr(dev, offset, val[i]);
		offset += sizeof(u32);
	}
}
EXPORT_SYMBOL_GPL(mt76s_write_copy);

void mt76s_read_copy(struct mt76_dev *dev, u32 offset,
		     void *data, int len)
{
	u32 *val = data;
	int i;

	for (i = 0; i < len / sizeof(u32); i++) {
		val[i] = mt76s_rr(dev, offset);
		offset += sizeof(u32);
	}
}
EXPORT_SYMBOL_GPL(mt76s_read_copy);

int mt76s_wr_rp(struct mt76_dev *dev, u32 base,
		const struct mt76_reg_pair *data,
		int len)
{
	int i;

	for (i = 0; i < len; i++) {
		mt76s_wr(dev, data->reg, data->value);
		data++;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mt76s_wr_rp);

int mt76s_rd_rp(struct mt76_dev *dev, u32 base,
		struct mt76_reg_pair *data, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		data->value = mt76s_rr(dev, data->reg);
		data++;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mt76s_rd_rp);

int mt76s_hw_init(struct mt76_dev *dev, struct sdio_func *func, int hw_ver)
{
	u32 status, ctrl;
	int ret;

	dev->sdio.hw_ver = hw_ver;

	sdio_claim_host(func);

	ret = sdio_enable_func(func);
	if (ret < 0)
		goto release;

	/* Get ownership from the device */
	sdio_writel(func, WHLPCR_INT_EN_CLR | WHLPCR_FW_OWN_REQ_CLR,
		    MCR_WHLPCR, &ret);
	if (ret < 0)
		goto disable_func;

	ret = readx_poll_timeout(mt76s_read_pcr, dev, status,
				 status & WHLPCR_IS_DRIVER_OWN, 2000, 1000000);
	if (ret < 0) {
		dev_err(dev->dev, "Cannot get ownership from device");
		goto disable_func;
	}

	ret = sdio_set_block_size(func, 512);
	if (ret < 0)
		goto disable_func;

	/* Enable interrupt */
	sdio_writel(func, WHLPCR_INT_EN_SET, MCR_WHLPCR, &ret);
	if (ret < 0)
		goto disable_func;

	ctrl = WHIER_RX0_DONE_INT_EN | WHIER_TX_DONE_INT_EN;
	if (hw_ver == MT76_CONNAC2_SDIO)
		ctrl |= WHIER_RX1_DONE_INT_EN;
	sdio_writel(func, ctrl, MCR_WHIER, &ret);
	if (ret < 0)
		goto disable_func;

	switch (hw_ver) {
	case MT76_CONNAC_SDIO:
		/* set WHISR as read clear and Rx aggregation number as 16 */
		ctrl = FIELD_PREP(MAX_HIF_RX_LEN_NUM, 16);
		break;
	default:
		ctrl = sdio_readl(func, MCR_WHCR, &ret);
		if (ret < 0)
			goto disable_func;
		ctrl &= ~MAX_HIF_RX_LEN_NUM_CONNAC2;
		ctrl &= ~W_INT_CLR_CTRL; /* read clear */
		ctrl |= FIELD_PREP(MAX_HIF_RX_LEN_NUM_CONNAC2, 0);
		break;
	}

	sdio_writel(func, ctrl, MCR_WHCR, &ret);
	if (ret < 0)
		goto disable_func;

	ret = sdio_claim_irq(func, mt76s_sdio_irq);
	if (ret < 0)
		goto disable_func;

	sdio_release_host(func);

	return 0;

disable_func:
	sdio_disable_func(func);
release:
	sdio_release_host(func);

	return ret;
}
EXPORT_SYMBOL_GPL(mt76s_hw_init);

int mt76s_alloc_rx_queue(struct mt76_dev *dev, enum mt76_rxq_id qid)
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
EXPORT_SYMBOL_GPL(mt76s_alloc_rx_queue);

static struct mt76_queue *mt76s_alloc_tx_queue(struct mt76_dev *dev)
{
	struct mt76_queue *q;

	q = devm_kzalloc(dev->dev, sizeof(*q), GFP_KERNEL);
	if (!q)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&q->lock);
	q->entry = devm_kcalloc(dev->dev,
				MT_NUM_TX_ENTRIES, sizeof(*q->entry),
				GFP_KERNEL);
	if (!q->entry)
		return ERR_PTR(-ENOMEM);

	q->ndesc = MT_NUM_TX_ENTRIES;

	return q;
}

int mt76s_alloc_tx(struct mt76_dev *dev)
{
	struct mt76_queue *q;
	int i;

	for (i = 0; i <= MT_TXQ_PSD; i++) {
		q = mt76s_alloc_tx_queue(dev);
		if (IS_ERR(q))
			return PTR_ERR(q);

		q->qid = i;
		dev->phy.q_tx[i] = q;
	}

	q = mt76s_alloc_tx_queue(dev);
	if (IS_ERR(q))
		return PTR_ERR(q);

	q->qid = MT_MCUQ_WM;
	dev->q_mcu[MT_MCUQ_WM] = q;

	return 0;
}
EXPORT_SYMBOL_GPL(mt76s_alloc_tx);

static struct mt76_queue_entry *
mt76s_get_next_rx_entry(struct mt76_queue *q)
{
	struct mt76_queue_entry *e = NULL;

	spin_lock_bh(&q->lock);
	if (q->queued > 0) {
		e = &q->entry[q->tail];
		q->tail = (q->tail + 1) % q->ndesc;
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

static void mt76s_net_worker(struct mt76_worker *w)
{
	struct mt76_sdio *sdio = container_of(w, struct mt76_sdio,
					      net_worker);
	struct mt76_dev *dev = container_of(sdio, struct mt76_dev, sdio);
	int i, nframes;

	do {
		nframes = 0;

		local_bh_disable();
		rcu_read_lock();

		mt76_for_each_q_rx(dev, i)
			nframes += mt76s_process_rx_queue(dev, &dev->q_rx[i]);

		rcu_read_unlock();
		local_bh_enable();
	} while (nframes > 0);
}

static int mt76s_process_tx_queue(struct mt76_dev *dev, struct mt76_queue *q)
{
	struct mt76_queue_entry entry;
	int nframes = 0;
	bool mcu;

	if (!q)
		return 0;

	mcu = q == dev->q_mcu[MT_MCUQ_WM];
	while (q->queued > 0) {
		if (!q->entry[q->tail].done)
			break;

		entry = q->entry[q->tail];
		q->entry[q->tail].done = false;

		if (mcu) {
			dev_kfree_skb(entry.skb);
			entry.skb = NULL;
		}

		mt76_queue_tx_complete(dev, q, &entry);
		nframes++;
	}

	if (!q->queued)
		wake_up(&dev->tx_wait);

	return nframes;
}

static void mt76s_status_worker(struct mt76_worker *w)
{
	struct mt76_sdio *sdio = container_of(w, struct mt76_sdio,
					      status_worker);
	struct mt76_dev *dev = container_of(sdio, struct mt76_dev, sdio);
	bool resched = false;
	int i, nframes;

	do {
		int ndata_frames = 0;

		nframes = mt76s_process_tx_queue(dev, dev->q_mcu[MT_MCUQ_WM]);

		for (i = 0; i <= MT_TXQ_PSD; i++)
			ndata_frames += mt76s_process_tx_queue(dev,
							       dev->phy.q_tx[i]);
		nframes += ndata_frames;
		if (ndata_frames > 0)
			resched = true;

		if (dev->drv->tx_status_data &&
		    !test_and_set_bit(MT76_READING_STATS, &dev->phy.state))
			queue_work(dev->wq, &dev->sdio.stat_work);
	} while (nframes > 0);

	if (resched)
		mt76_worker_schedule(&dev->sdio.txrx_worker);
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
mt76s_tx_queue_skb(struct mt76_dev *dev, struct mt76_queue *q,
		   struct sk_buff *skb, struct mt76_wcid *wcid,
		   struct ieee80211_sta *sta)
{
	struct mt76_tx_info tx_info = {
		.skb = skb,
	};
	int err, len = skb->len;
	u16 idx = q->head;

	if (q->queued == q->ndesc)
		return -ENOSPC;

	skb->prev = skb->next = NULL;
	err = dev->drv->tx_prepare_skb(dev, NULL, q->qid, wcid, sta, &tx_info);
	if (err < 0)
		return err;

	q->entry[q->head].skb = tx_info.skb;
	q->entry[q->head].buf_sz = len;
	q->entry[q->head].wcid = 0xffff;

	smp_wmb();

	q->head = (q->head + 1) % q->ndesc;
	q->queued++;

	return idx;
}

static int
mt76s_tx_queue_skb_raw(struct mt76_dev *dev, struct mt76_queue *q,
		       struct sk_buff *skb, u32 tx_info)
{
	int ret = -ENOSPC, len = skb->len, pad;

	if (q->queued == q->ndesc)
		goto error;

	pad = round_up(skb->len, 4) - skb->len;
	ret = mt76_skb_adjust_pad(skb, pad);
	if (ret)
		goto error;

	spin_lock_bh(&q->lock);

	q->entry[q->head].buf_sz = len;
	q->entry[q->head].skb = skb;
	q->head = (q->head + 1) % q->ndesc;
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

	mt76_worker_schedule(&sdio->txrx_worker);
}

static const struct mt76_queue_ops sdio_queue_ops = {
	.tx_queue_skb = mt76s_tx_queue_skb,
	.kick = mt76s_tx_kick,
	.tx_queue_skb_raw = mt76s_tx_queue_skb_raw,
};

void mt76s_deinit(struct mt76_dev *dev)
{
	struct mt76_sdio *sdio = &dev->sdio;
	int i;

	mt76_worker_teardown(&sdio->txrx_worker);
	mt76_worker_teardown(&sdio->status_worker);
	mt76_worker_teardown(&sdio->net_worker);

	cancel_work_sync(&sdio->stat_work);
	clear_bit(MT76_READING_STATS, &dev->phy.state);

	mt76_tx_status_check(dev, true);

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
	int err;

	err = mt76_worker_setup(dev->hw, &sdio->status_worker,
				mt76s_status_worker, "sdio-status");
	if (err)
		return err;

	err = mt76_worker_setup(dev->hw, &sdio->net_worker, mt76s_net_worker,
				"sdio-net");
	if (err)
		return err;

	sched_set_fifo_low(sdio->status_worker.task);
	sched_set_fifo_low(sdio->net_worker.task);

	INIT_WORK(&sdio->stat_work, mt76s_tx_status_data);

	dev->queue_ops = &sdio_queue_ops;
	dev->bus = bus_ops;
	dev->sdio.func = func;

	return 0;
}
EXPORT_SYMBOL_GPL(mt76s_init);

MODULE_AUTHOR("Sean Wang <sean.wang@mediatek.com>");
MODULE_AUTHOR("Lorenzo Bianconi <lorenzo@kernel.org>");
MODULE_LICENSE("Dual BSD/GPL");
