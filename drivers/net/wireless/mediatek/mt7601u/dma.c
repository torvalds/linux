// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 */

#include "mt7601u.h"
#include "dma.h"
#include "usb.h"
#include "trace.h"

static int mt7601u_submit_rx_buf(struct mt7601u_dev *dev,
				 struct mt7601u_dma_buf_rx *e, gfp_t gfp);

static unsigned int ieee80211_get_hdrlen_from_buf(const u8 *data, unsigned len)
{
	const struct ieee80211_hdr *hdr = (const struct ieee80211_hdr *)data;
	unsigned int hdrlen;

	if (unlikely(len < 10))
		return 0;
	hdrlen = ieee80211_hdrlen(hdr->frame_control);
	if (unlikely(hdrlen > len))
		return 0;
	return hdrlen;
}

static struct sk_buff *
mt7601u_rx_skb_from_seg(struct mt7601u_dev *dev, struct mt7601u_rxwi *rxwi,
			void *data, u32 seg_len, u32 truesize, struct page *p)
{
	struct sk_buff *skb;
	u32 true_len, hdr_len = 0, copy, frag;

	skb = alloc_skb(p ? 128 : seg_len, GFP_ATOMIC);
	if (!skb)
		return NULL;

	true_len = mt76_mac_process_rx(dev, skb, data, rxwi);
	if (!true_len || true_len > seg_len)
		goto bad_frame;

	hdr_len = ieee80211_get_hdrlen_from_buf(data, true_len);
	if (!hdr_len)
		goto bad_frame;

	if (rxwi->rxinfo & cpu_to_le32(MT_RXINFO_L2PAD)) {
		skb_put_data(skb, data, hdr_len);

		data += hdr_len + 2;
		true_len -= hdr_len;
		hdr_len = 0;
	}

	/* If not doing paged RX allocated skb will always have enough space */
	copy = (true_len <= skb_tailroom(skb)) ? true_len : hdr_len + 8;
	frag = true_len - copy;

	skb_put_data(skb, data, copy);
	data += copy;

	if (frag) {
		skb_add_rx_frag(skb, 0, p, data - page_address(p),
				frag, truesize);
		get_page(p);
	}

	return skb;

bad_frame:
	dev_err_ratelimited(dev->dev, "Error: incorrect frame len:%u hdr:%u\n",
			    true_len, hdr_len);
	dev_kfree_skb(skb);
	return NULL;
}

static void mt7601u_rx_process_seg(struct mt7601u_dev *dev, u8 *data,
				   u32 seg_len, struct page *p)
{
	struct sk_buff *skb;
	struct mt7601u_rxwi *rxwi;
	u32 fce_info, truesize = seg_len;

	/* DMA_INFO field at the beginning of the segment contains only some of
	 * the information, we need to read the FCE descriptor from the end.
	 */
	fce_info = get_unaligned_le32(data + seg_len - MT_FCE_INFO_LEN);
	seg_len -= MT_FCE_INFO_LEN;

	data += MT_DMA_HDR_LEN;
	seg_len -= MT_DMA_HDR_LEN;

	rxwi = (struct mt7601u_rxwi *) data;
	data += sizeof(struct mt7601u_rxwi);
	seg_len -= sizeof(struct mt7601u_rxwi);

	if (unlikely(rxwi->zero[0] || rxwi->zero[1] || rxwi->zero[2]))
		dev_err_once(dev->dev, "Error: RXWI zero fields are set\n");
	if (unlikely(FIELD_GET(MT_RXD_INFO_TYPE, fce_info)))
		dev_err_once(dev->dev, "Error: RX path seen a non-pkt urb\n");

	trace_mt_rx(dev, rxwi, fce_info);

	skb = mt7601u_rx_skb_from_seg(dev, rxwi, data, seg_len, truesize, p);
	if (!skb)
		return;

	spin_lock(&dev->mac_lock);
	ieee80211_rx(dev->hw, skb);
	spin_unlock(&dev->mac_lock);
}

static u16 mt7601u_rx_next_seg_len(u8 *data, u32 data_len)
{
	u32 min_seg_len = MT_DMA_HDR_LEN + MT_RX_INFO_LEN +
		sizeof(struct mt7601u_rxwi) + MT_FCE_INFO_LEN;
	u16 dma_len = get_unaligned_le16(data);

	if (data_len < min_seg_len ||
	    WARN_ON_ONCE(!dma_len) ||
	    WARN_ON_ONCE(dma_len + MT_DMA_HDRS > data_len) ||
	    WARN_ON_ONCE(dma_len & 0x3))
		return 0;

	return MT_DMA_HDRS + dma_len;
}

static void
mt7601u_rx_process_entry(struct mt7601u_dev *dev, struct mt7601u_dma_buf_rx *e)
{
	u32 seg_len, data_len = e->urb->actual_length;
	u8 *data = page_address(e->p);
	struct page *new_p = NULL;
	int cnt = 0;

	if (!test_bit(MT7601U_STATE_INITIALIZED, &dev->state))
		return;

	/* Copy if there is very little data in the buffer. */
	if (data_len > 512)
		new_p = dev_alloc_pages(MT_RX_ORDER);

	while ((seg_len = mt7601u_rx_next_seg_len(data, data_len))) {
		mt7601u_rx_process_seg(dev, data, seg_len, new_p ? e->p : NULL);

		data_len -= seg_len;
		data += seg_len;
		cnt++;
	}

	if (cnt > 1)
		trace_mt_rx_dma_aggr(dev, cnt, !!new_p);

	if (new_p) {
		/* we have one extra ref from the allocator */
		put_page(e->p);
		e->p = new_p;
	}
}

static struct mt7601u_dma_buf_rx *
mt7601u_rx_get_pending_entry(struct mt7601u_dev *dev)
{
	struct mt7601u_rx_queue *q = &dev->rx_q;
	struct mt7601u_dma_buf_rx *buf = NULL;
	unsigned long flags;

	spin_lock_irqsave(&dev->rx_lock, flags);

	if (!q->pending)
		goto out;

	buf = &q->e[q->start];
	q->pending--;
	q->start = (q->start + 1) % q->entries;
out:
	spin_unlock_irqrestore(&dev->rx_lock, flags);

	return buf;
}

static void mt7601u_complete_rx(struct urb *urb)
{
	struct mt7601u_dev *dev = urb->context;
	struct mt7601u_rx_queue *q = &dev->rx_q;
	unsigned long flags;

	/* do no schedule rx tasklet if urb has been unlinked
	 * or the device has been removed
	 */
	switch (urb->status) {
	case -ECONNRESET:
	case -ESHUTDOWN:
	case -ENOENT:
		return;
	default:
		dev_err_ratelimited(dev->dev, "rx urb failed: %d\n",
				    urb->status);
		fallthrough;
	case 0:
		break;
	}

	spin_lock_irqsave(&dev->rx_lock, flags);
	if (WARN_ONCE(q->e[q->end].urb != urb, "RX urb mismatch"))
		goto out;

	q->end = (q->end + 1) % q->entries;
	q->pending++;
	tasklet_schedule(&dev->rx_tasklet);
out:
	spin_unlock_irqrestore(&dev->rx_lock, flags);
}

static void mt7601u_rx_tasklet(unsigned long data)
{
	struct mt7601u_dev *dev = (struct mt7601u_dev *) data;
	struct mt7601u_dma_buf_rx *e;

	while ((e = mt7601u_rx_get_pending_entry(dev))) {
		if (e->urb->status)
			continue;

		mt7601u_rx_process_entry(dev, e);
		mt7601u_submit_rx_buf(dev, e, GFP_ATOMIC);
	}
}

static void mt7601u_complete_tx(struct urb *urb)
{
	struct mt7601u_tx_queue *q = urb->context;
	struct mt7601u_dev *dev = q->dev;
	struct sk_buff *skb;
	unsigned long flags;

	switch (urb->status) {
	case -ECONNRESET:
	case -ESHUTDOWN:
	case -ENOENT:
		return;
	default:
		dev_err_ratelimited(dev->dev, "tx urb failed: %d\n",
				    urb->status);
		fallthrough;
	case 0:
		break;
	}

	spin_lock_irqsave(&dev->tx_lock, flags);
	if (WARN_ONCE(q->e[q->start].urb != urb, "TX urb mismatch"))
		goto out;

	skb = q->e[q->start].skb;
	q->e[q->start].skb = NULL;
	trace_mt_tx_dma_done(dev, skb);

	__skb_queue_tail(&dev->tx_skb_done, skb);
	tasklet_schedule(&dev->tx_tasklet);

	if (q->used == q->entries - q->entries / 8)
		ieee80211_wake_queue(dev->hw, skb_get_queue_mapping(skb));

	q->start = (q->start + 1) % q->entries;
	q->used--;
out:
	spin_unlock_irqrestore(&dev->tx_lock, flags);
}

static void mt7601u_tx_tasklet(unsigned long data)
{
	struct mt7601u_dev *dev = (struct mt7601u_dev *) data;
	struct sk_buff_head skbs;
	unsigned long flags;

	__skb_queue_head_init(&skbs);

	spin_lock_irqsave(&dev->tx_lock, flags);

	set_bit(MT7601U_STATE_MORE_STATS, &dev->state);
	if (!test_and_set_bit(MT7601U_STATE_READING_STATS, &dev->state))
		queue_delayed_work(dev->stat_wq, &dev->stat_work,
				   msecs_to_jiffies(10));

	skb_queue_splice_init(&dev->tx_skb_done, &skbs);

	spin_unlock_irqrestore(&dev->tx_lock, flags);

	while (!skb_queue_empty(&skbs)) {
		struct sk_buff *skb = __skb_dequeue(&skbs);

		mt7601u_tx_status(dev, skb);
	}
}

static int mt7601u_dma_submit_tx(struct mt7601u_dev *dev,
				 struct sk_buff *skb, u8 ep)
{
	struct usb_device *usb_dev = mt7601u_to_usb_dev(dev);
	unsigned snd_pipe = usb_sndbulkpipe(usb_dev, dev->out_eps[ep]);
	struct mt7601u_dma_buf_tx *e;
	struct mt7601u_tx_queue *q = &dev->tx_q[ep];
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&dev->tx_lock, flags);

	if (WARN_ON(q->entries <= q->used)) {
		ret = -ENOSPC;
		goto out;
	}

	e = &q->e[q->end];
	usb_fill_bulk_urb(e->urb, usb_dev, snd_pipe, skb->data, skb->len,
			  mt7601u_complete_tx, q);
	ret = usb_submit_urb(e->urb, GFP_ATOMIC);
	if (ret) {
		/* Special-handle ENODEV from TX urb submission because it will
		 * often be the first ENODEV we see after device is removed.
		 */
		if (ret == -ENODEV)
			set_bit(MT7601U_STATE_REMOVED, &dev->state);
		else
			dev_err(dev->dev, "Error: TX urb submit failed:%d\n",
				ret);
		goto out;
	}

	q->end = (q->end + 1) % q->entries;
	q->used++;
	e->skb = skb;

	if (q->used >= q->entries)
		ieee80211_stop_queue(dev->hw, skb_get_queue_mapping(skb));
out:
	spin_unlock_irqrestore(&dev->tx_lock, flags);

	return ret;
}

/* Map hardware Q to USB endpoint number */
static u8 q2ep(u8 qid)
{
	/* TODO: take management packets to queue 5 */
	return qid + 1;
}

/* Map USB endpoint number to Q id in the DMA engine */
static enum mt76_qsel ep2dmaq(u8 ep)
{
	if (ep == 5)
		return MT_QSEL_MGMT;
	return MT_QSEL_EDCA;
}

int mt7601u_dma_enqueue_tx(struct mt7601u_dev *dev, struct sk_buff *skb,
			   struct mt76_wcid *wcid, int hw_q)
{
	u8 ep = q2ep(hw_q);
	u32 dma_flags;
	int ret;

	dma_flags = MT_TXD_PKT_INFO_80211;
	if (wcid->hw_key_idx == 0xff)
		dma_flags |= MT_TXD_PKT_INFO_WIV;

	ret = mt7601u_dma_skb_wrap_pkt(skb, ep2dmaq(ep), dma_flags);
	if (ret)
		return ret;

	ret = mt7601u_dma_submit_tx(dev, skb, ep);
	if (ret) {
		ieee80211_free_txskb(dev->hw, skb);
		return ret;
	}

	return 0;
}

static void mt7601u_kill_rx(struct mt7601u_dev *dev)
{
	int i;

	for (i = 0; i < dev->rx_q.entries; i++)
		usb_poison_urb(dev->rx_q.e[i].urb);
}

static int mt7601u_submit_rx_buf(struct mt7601u_dev *dev,
				 struct mt7601u_dma_buf_rx *e, gfp_t gfp)
{
	struct usb_device *usb_dev = mt7601u_to_usb_dev(dev);
	u8 *buf = page_address(e->p);
	unsigned pipe;
	int ret;

	pipe = usb_rcvbulkpipe(usb_dev, dev->in_eps[MT_EP_IN_PKT_RX]);

	usb_fill_bulk_urb(e->urb, usb_dev, pipe, buf, MT_RX_URB_SIZE,
			  mt7601u_complete_rx, dev);

	trace_mt_submit_urb(dev, e->urb);
	ret = usb_submit_urb(e->urb, gfp);
	if (ret)
		dev_err(dev->dev, "Error: submit RX URB failed:%d\n", ret);

	return ret;
}

static int mt7601u_submit_rx(struct mt7601u_dev *dev)
{
	int i, ret;

	for (i = 0; i < dev->rx_q.entries; i++) {
		ret = mt7601u_submit_rx_buf(dev, &dev->rx_q.e[i], GFP_KERNEL);
		if (ret)
			return ret;
	}

	return 0;
}

static void mt7601u_free_rx(struct mt7601u_dev *dev)
{
	int i;

	for (i = 0; i < dev->rx_q.entries; i++) {
		__free_pages(dev->rx_q.e[i].p, MT_RX_ORDER);
		usb_free_urb(dev->rx_q.e[i].urb);
	}
}

static int mt7601u_alloc_rx(struct mt7601u_dev *dev)
{
	int i;

	memset(&dev->rx_q, 0, sizeof(dev->rx_q));
	dev->rx_q.dev = dev;
	dev->rx_q.entries = N_RX_ENTRIES;

	for (i = 0; i < N_RX_ENTRIES; i++) {
		dev->rx_q.e[i].urb = usb_alloc_urb(0, GFP_KERNEL);
		dev->rx_q.e[i].p = dev_alloc_pages(MT_RX_ORDER);

		if (!dev->rx_q.e[i].urb || !dev->rx_q.e[i].p)
			return -ENOMEM;
	}

	return 0;
}

static void mt7601u_free_tx_queue(struct mt7601u_tx_queue *q)
{
	int i;

	for (i = 0; i < q->entries; i++)  {
		usb_poison_urb(q->e[i].urb);
		if (q->e[i].skb)
			mt7601u_tx_status(q->dev, q->e[i].skb);
		usb_free_urb(q->e[i].urb);
	}
}

static void mt7601u_free_tx(struct mt7601u_dev *dev)
{
	int i;

	if (!dev->tx_q)
		return;

	for (i = 0; i < __MT_EP_OUT_MAX; i++)
		mt7601u_free_tx_queue(&dev->tx_q[i]);
}

static int mt7601u_alloc_tx_queue(struct mt7601u_dev *dev,
				  struct mt7601u_tx_queue *q)
{
	int i;

	q->dev = dev;
	q->entries = N_TX_ENTRIES;

	for (i = 0; i < N_TX_ENTRIES; i++) {
		q->e[i].urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!q->e[i].urb)
			return -ENOMEM;
	}

	return 0;
}

static int mt7601u_alloc_tx(struct mt7601u_dev *dev)
{
	int i;

	dev->tx_q = devm_kcalloc(dev->dev, __MT_EP_OUT_MAX,
				 sizeof(*dev->tx_q), GFP_KERNEL);
	if (!dev->tx_q)
		return -ENOMEM;

	for (i = 0; i < __MT_EP_OUT_MAX; i++)
		if (mt7601u_alloc_tx_queue(dev, &dev->tx_q[i]))
			return -ENOMEM;

	return 0;
}

int mt7601u_dma_init(struct mt7601u_dev *dev)
{
	int ret = -ENOMEM;

	tasklet_init(&dev->tx_tasklet, mt7601u_tx_tasklet, (unsigned long) dev);
	tasklet_init(&dev->rx_tasklet, mt7601u_rx_tasklet, (unsigned long) dev);

	ret = mt7601u_alloc_tx(dev);
	if (ret)
		goto err;
	ret = mt7601u_alloc_rx(dev);
	if (ret)
		goto err;

	ret = mt7601u_submit_rx(dev);
	if (ret)
		goto err;

	return 0;
err:
	mt7601u_dma_cleanup(dev);
	return ret;
}

void mt7601u_dma_cleanup(struct mt7601u_dev *dev)
{
	mt7601u_kill_rx(dev);

	tasklet_kill(&dev->rx_tasklet);

	mt7601u_free_rx(dev);
	mt7601u_free_tx(dev);

	tasklet_kill(&dev->tx_tasklet);
}
