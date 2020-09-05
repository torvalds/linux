// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc.
 *
 * Author: Felix Fietkau <nbd@nbd.name>
 *	   Lorenzo Bianconi <lorenzo@kernel.org>
 *	   Sean Wang <sean.wang@mediatek.com>
 */

#include <linux/kernel.h>
#include <linux/iopoll.h>
#include <linux/module.h>

#include <linux/mmc/host.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio_func.h>

#include "../trace.h"
#include "mt7615.h"
#include "sdio.h"
#include "mac.h"

static int mt7663s_refill_sched_quota(struct mt76_dev *dev, u32 *data)
{
	u32 ple_ac_data_quota[] = {
		FIELD_GET(TXQ_CNT_L, data[4]), /* VO */
		FIELD_GET(TXQ_CNT_H, data[3]), /* VI */
		FIELD_GET(TXQ_CNT_L, data[3]), /* BE */
		FIELD_GET(TXQ_CNT_H, data[2]), /* BK */
	};
	u32 pse_ac_data_quota[] = {
		FIELD_GET(TXQ_CNT_H, data[1]), /* VO */
		FIELD_GET(TXQ_CNT_L, data[1]), /* VI */
		FIELD_GET(TXQ_CNT_H, data[0]), /* BE */
		FIELD_GET(TXQ_CNT_L, data[0]), /* BK */
	};
	u32 pse_mcu_quota = FIELD_GET(TXQ_CNT_L, data[2]);
	u32 pse_data_quota = 0, ple_data_quota = 0;
	struct mt76_sdio *sdio = &dev->sdio;
	int i;

	for (i = 0; i < ARRAY_SIZE(pse_ac_data_quota); i++) {
		pse_data_quota += pse_ac_data_quota[i];
		ple_data_quota += ple_ac_data_quota[i];
	}

	if (!pse_data_quota && !ple_data_quota && !pse_mcu_quota)
		return 0;

	mutex_lock(&sdio->sched.lock);
	sdio->sched.pse_mcu_quota += pse_mcu_quota;
	sdio->sched.pse_data_quota += pse_data_quota;
	sdio->sched.ple_data_quota += ple_data_quota;
	mutex_unlock(&sdio->sched.lock);

	return pse_data_quota + ple_data_quota + pse_mcu_quota;
}

static struct sk_buff *mt7663s_build_rx_skb(void *data, int data_len,
					    int buf_len)
{
	int len = min_t(int, data_len, MT_SKB_HEAD_LEN);
	struct sk_buff *skb;

	skb = alloc_skb(len, GFP_KERNEL);
	if (!skb)
		return NULL;

	skb_put_data(skb, data, len);
	if (data_len > len) {
		struct page *page;

		data += len;
		page = virt_to_head_page(data);
		skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags,
				page, data - page_address(page),
				data_len - len, buf_len);
		get_page(page);
	}

	return skb;
}

static int mt7663s_rx_run_queue(struct mt76_dev *dev, enum mt76_rxq_id qid,
				struct mt76s_intr *intr)
{
	struct mt76_queue *q = &dev->q_rx[qid];
	struct mt76_sdio *sdio = &dev->sdio;
	int len = 0, err, i, order;
	struct page *page;
	u8 *buf;

	for (i = 0; i < intr->rx.num[qid]; i++)
		len += round_up(intr->rx.len[qid][i] + 4, 4);

	if (!len)
		return 0;

	if (len > sdio->func->cur_blksize)
		len = roundup(len, sdio->func->cur_blksize);

	order = get_order(len);
	page = __dev_alloc_pages(GFP_KERNEL, order);
	if (!page)
		return -ENOMEM;

	buf = page_address(page);

	sdio_claim_host(sdio->func);
	err = sdio_readsb(sdio->func, buf, MCR_WRDR(qid), len);
	sdio_release_host(sdio->func);

	if (err < 0) {
		dev_err(dev->dev, "sdio read data failed:%d\n", err);
		__free_pages(page, order);
		return err;
	}

	for (i = 0; i < intr->rx.num[qid]; i++) {
		int index = (q->head + i) % q->ndesc;
		struct mt76_queue_entry *e = &q->entry[index];

		len = intr->rx.len[qid][i];
		e->skb = mt7663s_build_rx_skb(buf, len, round_up(len + 4, 4));
		if (!e->skb)
			break;

		buf += round_up(len + 4, 4);
		if (q->queued + i + 1 == q->ndesc)
			break;
	}
	__free_pages(page, order);

	spin_lock_bh(&q->lock);
	q->head = (q->head + i) % q->ndesc;
	q->queued += i;
	spin_unlock_bh(&q->lock);

	return i;
}

static int mt7663s_tx_pick_quota(struct mt76_sdio *sdio, enum mt76_txq_id qid,
				 int buf_sz, int *pse_size, int *ple_size)
{
	int pse_sz;

	pse_sz = DIV_ROUND_UP(buf_sz + sdio->sched.deficit, MT_PSE_PAGE_SZ);

	if (qid == MT_TXQ_MCU) {
		if (sdio->sched.pse_mcu_quota < *pse_size + pse_sz)
			return -EBUSY;
	} else {
		if (sdio->sched.pse_data_quota < *pse_size + pse_sz ||
		    sdio->sched.ple_data_quota < *ple_size)
			return -EBUSY;

		*ple_size = *ple_size + 1;
	}
	*pse_size = *pse_size + pse_sz;

	return 0;
}

static void mt7663s_tx_update_quota(struct mt76_sdio *sdio, enum mt76_txq_id qid,
				    int pse_size, int ple_size)
{
	mutex_lock(&sdio->sched.lock);
	if (qid == MT_TXQ_MCU) {
		sdio->sched.pse_mcu_quota -= pse_size;
	} else {
		sdio->sched.pse_data_quota -= pse_size;
		sdio->sched.ple_data_quota -= ple_size;
	}
	mutex_unlock(&sdio->sched.lock);
}

static int __mt7663s_xmit_queue(struct mt76_dev *dev, u8 *data, int len)
{
	struct mt76_sdio *sdio = &dev->sdio;
	int err;

	if (len > sdio->func->cur_blksize)
		len = roundup(len, sdio->func->cur_blksize);

	sdio_claim_host(sdio->func);
	err = sdio_writesb(sdio->func, MCR_WTDR1, data, len);
	sdio_release_host(sdio->func);

	if (err)
		dev_err(dev->dev, "sdio write failed: %d\n", err);

	return err;
}

static int mt7663s_tx_run_queue(struct mt76_dev *dev, enum mt76_txq_id qid)
{
	int err, nframes = 0, len = 0, pse_sz = 0, ple_sz = 0;
	struct mt76_queue *q = dev->q_tx[qid];
	struct mt76_sdio *sdio = &dev->sdio;

	while (q->first != q->head) {
		struct mt76_queue_entry *e = &q->entry[q->first];
		struct sk_buff *iter;

		if (!test_bit(MT76_STATE_MCU_RUNNING, &dev->phy.state)) {
			__skb_put_zero(e->skb, 4);
			err = __mt7663s_xmit_queue(dev, e->skb->data,
						   e->skb->len);
			if (err)
				return err;

			goto next;
		}

		if (len + e->skb->len + 4 > MT76S_XMIT_BUF_SZ)
			break;

		if (mt7663s_tx_pick_quota(sdio, qid, e->buf_sz, &pse_sz,
					  &ple_sz))
			break;

		memcpy(sdio->xmit_buf[qid] + len, e->skb->data,
		       skb_headlen(e->skb));
		len += skb_headlen(e->skb);
		nframes++;

		skb_walk_frags(e->skb, iter) {
			memcpy(sdio->xmit_buf[qid] + len, iter->data,
			       iter->len);
			len += iter->len;
			nframes++;
		}
next:
		q->first = (q->first + 1) % q->ndesc;
		e->done = true;
	}

	if (nframes) {
		memset(sdio->xmit_buf[qid] + len, 0, 4);
		err = __mt7663s_xmit_queue(dev, sdio->xmit_buf[qid], len + 4);
		if (err)
			return err;
	}
	mt7663s_tx_update_quota(sdio, qid, pse_sz, ple_sz);

	return nframes;
}

void mt7663s_tx_work(struct work_struct *work)
{
	struct mt76_sdio *sdio = container_of(work, struct mt76_sdio,
					      tx.xmit_work);
	struct mt76_dev *dev = container_of(sdio, struct mt76_dev, sdio);
	int i, nframes = 0;

	for (i = 0; i < MT_TXQ_MCU_WA; i++) {
		int ret;

		ret = mt7663s_tx_run_queue(dev, i);
		if (ret < 0)
			break;

		nframes += ret;
	}
	if (nframes)
		queue_work(sdio->txrx_wq, &sdio->tx.xmit_work);

	queue_work(sdio->txrx_wq, &sdio->tx.status_work);
}

void mt7663s_rx_work(struct work_struct *work)
{
	struct mt76_sdio *sdio = container_of(work, struct mt76_sdio,
					      rx.recv_work);
	struct mt76_dev *dev = container_of(sdio, struct mt76_dev, sdio);
	struct mt76s_intr *intr = sdio->intr_data;
	int nframes = 0, ret;

	/* disable interrupt */
	sdio_claim_host(sdio->func);
	sdio_writel(sdio->func, WHLPCR_INT_EN_CLR, MCR_WHLPCR, NULL);
	sdio_readsb(sdio->func, intr, MCR_WHISR, sizeof(struct mt76s_intr));
	sdio_release_host(sdio->func);

	trace_dev_irq(dev, intr->isr, 0);

	if (intr->isr & WHIER_RX0_DONE_INT_EN) {
		ret = mt7663s_rx_run_queue(dev, 0, intr);
		if (ret > 0) {
			queue_work(sdio->txrx_wq, &sdio->rx.net_work);
			nframes += ret;
		}
	}

	if (intr->isr & WHIER_RX1_DONE_INT_EN) {
		ret = mt7663s_rx_run_queue(dev, 1, intr);
		if (ret > 0) {
			queue_work(sdio->txrx_wq, &sdio->rx.net_work);
			nframes += ret;
		}
	}

	if (mt7663s_refill_sched_quota(dev, intr->tx.wtqcr))
		queue_work(sdio->txrx_wq, &sdio->tx.xmit_work);

	if (nframes) {
		queue_work(sdio->txrx_wq, &sdio->rx.recv_work);
		return;
	}

	/* enable interrupt */
	sdio_claim_host(sdio->func);
	sdio_writel(sdio->func, WHLPCR_INT_EN_SET, MCR_WHLPCR, NULL);
	sdio_release_host(sdio->func);
}

void mt7663s_sdio_irq(struct sdio_func *func)
{
	struct mt7615_dev *dev = sdio_get_drvdata(func);
	struct mt76_sdio *sdio = &dev->mt76.sdio;

	if (!test_bit(MT76_STATE_INITIALIZED, &dev->mt76.phy.state))
		return;

	queue_work(sdio->txrx_wq, &sdio->rx.recv_work);
}
