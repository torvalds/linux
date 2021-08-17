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

	sdio->sched.pse_mcu_quota += pse_mcu_quota;
	sdio->sched.pse_data_quota += pse_data_quota;
	sdio->sched.ple_data_quota += ple_data_quota;

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
	int len = 0, err, i;
	struct page *page;
	u8 *buf;

	for (i = 0; i < intr->rx.num[qid]; i++)
		len += round_up(intr->rx.len[qid][i] + 4, 4);

	if (!len)
		return 0;

	if (len > sdio->func->cur_blksize)
		len = roundup(len, sdio->func->cur_blksize);

	page = __dev_alloc_pages(GFP_KERNEL, get_order(len));
	if (!page)
		return -ENOMEM;

	buf = page_address(page);

	err = sdio_readsb(sdio->func, buf, MCR_WRDR(qid), len);
	if (err < 0) {
		dev_err(dev->dev, "sdio read data failed:%d\n", err);
		put_page(page);
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
	put_page(page);

	spin_lock_bh(&q->lock);
	q->head = (q->head + i) % q->ndesc;
	q->queued += i;
	spin_unlock_bh(&q->lock);

	return i;
}

static int mt7663s_rx_handler(struct mt76_dev *dev)
{
	struct mt76_sdio *sdio = &dev->sdio;
	struct mt76s_intr *intr = sdio->intr_data;
	int nframes = 0, ret;

	ret = sdio_readsb(sdio->func, intr, MCR_WHISR, sizeof(*intr));
	if (ret < 0)
		return ret;

	trace_dev_irq(dev, intr->isr, 0);

	if (intr->isr & WHIER_RX0_DONE_INT_EN) {
		ret = mt7663s_rx_run_queue(dev, 0, intr);
		if (ret > 0) {
			mt76_worker_schedule(&sdio->net_worker);
			nframes += ret;
		}
	}

	if (intr->isr & WHIER_RX1_DONE_INT_EN) {
		ret = mt7663s_rx_run_queue(dev, 1, intr);
		if (ret > 0) {
			mt76_worker_schedule(&sdio->net_worker);
			nframes += ret;
		}
	}

	nframes += !!mt7663s_refill_sched_quota(dev, intr->tx.wtqcr);

	return nframes;
}

static int mt7663s_tx_pick_quota(struct mt76_sdio *sdio, bool mcu, int buf_sz,
				 int *pse_size, int *ple_size)
{
	int pse_sz;

	pse_sz = DIV_ROUND_UP(buf_sz + sdio->sched.deficit, MT_PSE_PAGE_SZ);

	if (mcu) {
		if (sdio->sched.pse_mcu_quota < *pse_size + pse_sz)
			return -EBUSY;
	} else {
		if (sdio->sched.pse_data_quota < *pse_size + pse_sz ||
		    sdio->sched.ple_data_quota < *ple_size + 1)
			return -EBUSY;

		*ple_size = *ple_size + 1;
	}
	*pse_size = *pse_size + pse_sz;

	return 0;
}

static void mt7663s_tx_update_quota(struct mt76_sdio *sdio, bool mcu,
				    int pse_size, int ple_size)
{
	if (mcu) {
		sdio->sched.pse_mcu_quota -= pse_size;
	} else {
		sdio->sched.pse_data_quota -= pse_size;
		sdio->sched.ple_data_quota -= ple_size;
	}
}

static int __mt7663s_xmit_queue(struct mt76_dev *dev, u8 *data, int len)
{
	struct mt76_sdio *sdio = &dev->sdio;
	int err;

	if (len > sdio->func->cur_blksize)
		len = roundup(len, sdio->func->cur_blksize);

	err = sdio_writesb(sdio->func, MCR_WTDR1, data, len);
	if (err)
		dev_err(dev->dev, "sdio write failed: %d\n", err);

	return err;
}

static int mt7663s_tx_run_queue(struct mt76_dev *dev, struct mt76_queue *q)
{
	int qid, err, nframes = 0, len = 0, pse_sz = 0, ple_sz = 0;
	bool mcu = q == dev->q_mcu[MT_MCUQ_WM];
	struct mt76_sdio *sdio = &dev->sdio;
	u8 pad;

	qid = mcu ? ARRAY_SIZE(sdio->xmit_buf) - 1 : q->qid;
	while (q->first != q->head) {
		struct mt76_queue_entry *e = &q->entry[q->first];
		struct sk_buff *iter;

		smp_rmb();

		if (!test_bit(MT76_STATE_MCU_RUNNING, &dev->phy.state)) {
			__skb_put_zero(e->skb, 4);
			err = __mt7663s_xmit_queue(dev, e->skb->data,
						   e->skb->len);
			if (err)
				return err;

			goto next;
		}

		pad = roundup(e->skb->len, 4) - e->skb->len;
		if (len + e->skb->len + pad + 4 > MT76S_XMIT_BUF_SZ)
			break;

		if (mt7663s_tx_pick_quota(sdio, mcu, e->buf_sz, &pse_sz,
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

		if (unlikely(pad)) {
			memset(sdio->xmit_buf[qid] + len, 0, pad);
			len += pad;
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
	mt7663s_tx_update_quota(sdio, mcu, pse_sz, ple_sz);

	mt76_worker_schedule(&sdio->status_worker);

	return nframes;
}

void mt7663s_txrx_worker(struct mt76_worker *w)
{
	struct mt76_sdio *sdio = container_of(w, struct mt76_sdio,
					      txrx_worker);
	struct mt76_dev *mdev = container_of(sdio, struct mt76_dev, sdio);
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);
	int i, nframes, ret;

	if (!mt76_connac_pm_ref(&dev->mphy, &dev->pm)) {
		queue_work(mdev->wq, &dev->pm.wake_work);
		return;
	}

	/* disable interrupt */
	sdio_claim_host(sdio->func);
	sdio_writel(sdio->func, WHLPCR_INT_EN_CLR, MCR_WHLPCR, NULL);

	do {
		nframes = 0;

		/* tx */
		for (i = 0; i <= MT_TXQ_PSD; i++) {
			ret = mt7663s_tx_run_queue(mdev, mdev->phy.q_tx[i]);
			if (ret > 0)
				nframes += ret;
		}
		ret = mt7663s_tx_run_queue(mdev, mdev->q_mcu[MT_MCUQ_WM]);
		if (ret > 0)
			nframes += ret;

		/* rx */
		ret = mt7663s_rx_handler(mdev);
		if (ret > 0)
			nframes += ret;
	} while (nframes > 0);

	/* enable interrupt */
	sdio_writel(sdio->func, WHLPCR_INT_EN_SET, MCR_WHLPCR, NULL);
	sdio_release_host(sdio->func);

	mt76_connac_pm_unref(&dev->mphy, &dev->pm);
}

void mt7663s_sdio_irq(struct sdio_func *func)
{
	struct mt7615_dev *dev = sdio_get_drvdata(func);
	struct mt76_sdio *sdio = &dev->mt76.sdio;

	if (!test_bit(MT76_STATE_INITIALIZED, &dev->mt76.phy.state))
		return;

	mt76_worker_schedule(&sdio->txrx_worker);
}
