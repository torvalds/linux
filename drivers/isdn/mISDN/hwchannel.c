/*
 *
 * Author	Karsten Keil <kkeil@novell.com>
 *
 * Copyright 2008  by Karsten Keil <kkeil@novell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/gfp.h>
#include <linux/module.h>
#include <linux/mISDNhw.h>

static void
dchannel_bh(struct work_struct *ws)
{
	struct dchannel	*dch  = container_of(ws, struct dchannel, workq);
	struct sk_buff	*skb;
	int		err;

	if (test_and_clear_bit(FLG_RECVQUEUE, &dch->Flags)) {
		while ((skb = skb_dequeue(&dch->rqueue))) {
			if (likely(dch->dev.D.peer)) {
				err = dch->dev.D.recv(dch->dev.D.peer, skb);
				if (err)
					dev_kfree_skb(skb);
			} else
				dev_kfree_skb(skb);
		}
	}
	if (test_and_clear_bit(FLG_PHCHANGE, &dch->Flags)) {
		if (dch->phfunc)
			dch->phfunc(dch);
	}
}

static void
bchannel_bh(struct work_struct *ws)
{
	struct bchannel	*bch  = container_of(ws, struct bchannel, workq);
	struct sk_buff	*skb;
	int		err;

	if (test_and_clear_bit(FLG_RECVQUEUE, &bch->Flags)) {
		while ((skb = skb_dequeue(&bch->rqueue))) {
			bch->rcount--;
			if (likely(bch->ch.peer)) {
				err = bch->ch.recv(bch->ch.peer, skb);
				if (err)
					dev_kfree_skb(skb);
			} else
				dev_kfree_skb(skb);
		}
	}
}

int
mISDN_initdchannel(struct dchannel *ch, int maxlen, void *phf)
{
	test_and_set_bit(FLG_HDLC, &ch->Flags);
	ch->maxlen = maxlen;
	ch->hw = NULL;
	ch->rx_skb = NULL;
	ch->tx_skb = NULL;
	ch->tx_idx = 0;
	ch->phfunc = phf;
	skb_queue_head_init(&ch->squeue);
	skb_queue_head_init(&ch->rqueue);
	INIT_LIST_HEAD(&ch->dev.bchannels);
	INIT_WORK(&ch->workq, dchannel_bh);
	return 0;
}
EXPORT_SYMBOL(mISDN_initdchannel);

int
mISDN_initbchannel(struct bchannel *ch, int maxlen)
{
	ch->Flags = 0;
	ch->maxlen = maxlen;
	ch->hw = NULL;
	ch->rx_skb = NULL;
	ch->tx_skb = NULL;
	ch->tx_idx = 0;
	skb_queue_head_init(&ch->rqueue);
	ch->rcount = 0;
	ch->next_skb = NULL;
	INIT_WORK(&ch->workq, bchannel_bh);
	return 0;
}
EXPORT_SYMBOL(mISDN_initbchannel);

int
mISDN_freedchannel(struct dchannel *ch)
{
	if (ch->tx_skb) {
		dev_kfree_skb(ch->tx_skb);
		ch->tx_skb = NULL;
	}
	if (ch->rx_skb) {
		dev_kfree_skb(ch->rx_skb);
		ch->rx_skb = NULL;
	}
	skb_queue_purge(&ch->squeue);
	skb_queue_purge(&ch->rqueue);
	flush_scheduled_work();
	return 0;
}
EXPORT_SYMBOL(mISDN_freedchannel);

void
mISDN_clear_bchannel(struct bchannel *ch)
{
	if (ch->tx_skb) {
		dev_kfree_skb(ch->tx_skb);
		ch->tx_skb = NULL;
	}
	ch->tx_idx = 0;
	if (ch->rx_skb) {
		dev_kfree_skb(ch->rx_skb);
		ch->rx_skb = NULL;
	}
	if (ch->next_skb) {
		dev_kfree_skb(ch->next_skb);
		ch->next_skb = NULL;
	}
	test_and_clear_bit(FLG_TX_BUSY, &ch->Flags);
	test_and_clear_bit(FLG_TX_NEXT, &ch->Flags);
	test_and_clear_bit(FLG_ACTIVE, &ch->Flags);
}
EXPORT_SYMBOL(mISDN_clear_bchannel);

int
mISDN_freebchannel(struct bchannel *ch)
{
	mISDN_clear_bchannel(ch);
	skb_queue_purge(&ch->rqueue);
	ch->rcount = 0;
	flush_scheduled_work();
	return 0;
}
EXPORT_SYMBOL(mISDN_freebchannel);

static inline u_int
get_sapi_tei(u_char *p)
{
	u_int	sapi, tei;

	sapi = *p >> 2;
	tei = p[1] >> 1;
	return sapi | (tei << 8);
}

void
recv_Dchannel(struct dchannel *dch)
{
	struct mISDNhead *hh;

	if (dch->rx_skb->len < 2) { /* at least 2 for sapi / tei */
		dev_kfree_skb(dch->rx_skb);
		dch->rx_skb = NULL;
		return;
	}
	hh = mISDN_HEAD_P(dch->rx_skb);
	hh->prim = PH_DATA_IND;
	hh->id = get_sapi_tei(dch->rx_skb->data);
	skb_queue_tail(&dch->rqueue, dch->rx_skb);
	dch->rx_skb = NULL;
	schedule_event(dch, FLG_RECVQUEUE);
}
EXPORT_SYMBOL(recv_Dchannel);

void
recv_Echannel(struct dchannel *ech, struct dchannel *dch)
{
	struct mISDNhead *hh;

	if (ech->rx_skb->len < 2) { /* at least 2 for sapi / tei */
		dev_kfree_skb(ech->rx_skb);
		ech->rx_skb = NULL;
		return;
	}
	hh = mISDN_HEAD_P(ech->rx_skb);
	hh->prim = PH_DATA_E_IND;
	hh->id = get_sapi_tei(ech->rx_skb->data);
	skb_queue_tail(&dch->rqueue, ech->rx_skb);
	ech->rx_skb = NULL;
	schedule_event(dch, FLG_RECVQUEUE);
}
EXPORT_SYMBOL(recv_Echannel);

void
recv_Bchannel(struct bchannel *bch, unsigned int id)
{
	struct mISDNhead *hh;

	hh = mISDN_HEAD_P(bch->rx_skb);
	hh->prim = PH_DATA_IND;
	hh->id = id;
	if (bch->rcount >= 64) {
		printk(KERN_WARNING "B-channel %p receive queue overflow, "
			"fushing!\n", bch);
		skb_queue_purge(&bch->rqueue);
		bch->rcount = 0;
		return;
	}
	bch->rcount++;
	skb_queue_tail(&bch->rqueue, bch->rx_skb);
	bch->rx_skb = NULL;
	schedule_event(bch, FLG_RECVQUEUE);
}
EXPORT_SYMBOL(recv_Bchannel);

void
recv_Dchannel_skb(struct dchannel *dch, struct sk_buff *skb)
{
	skb_queue_tail(&dch->rqueue, skb);
	schedule_event(dch, FLG_RECVQUEUE);
}
EXPORT_SYMBOL(recv_Dchannel_skb);

void
recv_Bchannel_skb(struct bchannel *bch, struct sk_buff *skb)
{
	if (bch->rcount >= 64) {
		printk(KERN_WARNING "B-channel %p receive queue overflow, "
			"fushing!\n", bch);
		skb_queue_purge(&bch->rqueue);
		bch->rcount = 0;
	}
	bch->rcount++;
	skb_queue_tail(&bch->rqueue, skb);
	schedule_event(bch, FLG_RECVQUEUE);
}
EXPORT_SYMBOL(recv_Bchannel_skb);

static void
confirm_Dsend(struct dchannel *dch)
{
	struct sk_buff	*skb;

	skb = _alloc_mISDN_skb(PH_DATA_CNF, mISDN_HEAD_ID(dch->tx_skb),
	    0, NULL, GFP_ATOMIC);
	if (!skb) {
		printk(KERN_ERR "%s: no skb id %x\n", __func__,
		    mISDN_HEAD_ID(dch->tx_skb));
		return;
	}
	skb_queue_tail(&dch->rqueue, skb);
	schedule_event(dch, FLG_RECVQUEUE);
}

int
get_next_dframe(struct dchannel *dch)
{
	dch->tx_idx = 0;
	dch->tx_skb = skb_dequeue(&dch->squeue);
	if (dch->tx_skb) {
		confirm_Dsend(dch);
		return 1;
	}
	dch->tx_skb = NULL;
	test_and_clear_bit(FLG_TX_BUSY, &dch->Flags);
	return 0;
}
EXPORT_SYMBOL(get_next_dframe);

void
confirm_Bsend(struct bchannel *bch)
{
	struct sk_buff	*skb;

	if (bch->rcount >= 64) {
		printk(KERN_WARNING "B-channel %p receive queue overflow, "
			"fushing!\n", bch);
		skb_queue_purge(&bch->rqueue);
		bch->rcount = 0;
	}
	skb = _alloc_mISDN_skb(PH_DATA_CNF, mISDN_HEAD_ID(bch->tx_skb),
	    0, NULL, GFP_ATOMIC);
	if (!skb) {
		printk(KERN_ERR "%s: no skb id %x\n", __func__,
		    mISDN_HEAD_ID(bch->tx_skb));
		return;
	}
	bch->rcount++;
	skb_queue_tail(&bch->rqueue, skb);
	schedule_event(bch, FLG_RECVQUEUE);
}
EXPORT_SYMBOL(confirm_Bsend);

int
get_next_bframe(struct bchannel *bch)
{
	bch->tx_idx = 0;
	if (test_bit(FLG_TX_NEXT, &bch->Flags)) {
		bch->tx_skb = bch->next_skb;
		if (bch->tx_skb) {
			bch->next_skb = NULL;
			test_and_clear_bit(FLG_TX_NEXT, &bch->Flags);
			if (!test_bit(FLG_TRANSPARENT, &bch->Flags))
				confirm_Bsend(bch); /* not for transparent */
			return 1;
		} else {
			test_and_clear_bit(FLG_TX_NEXT, &bch->Flags);
			printk(KERN_WARNING "B TX_NEXT without skb\n");
		}
	}
	bch->tx_skb = NULL;
	test_and_clear_bit(FLG_TX_BUSY, &bch->Flags);
	return 0;
}
EXPORT_SYMBOL(get_next_bframe);

void
queue_ch_frame(struct mISDNchannel *ch, u_int pr, int id, struct sk_buff *skb)
{
	struct mISDNhead *hh;

	if (!skb) {
		_queue_data(ch, pr, id, 0, NULL, GFP_ATOMIC);
	} else {
		if (ch->peer) {
			hh = mISDN_HEAD_P(skb);
			hh->prim = pr;
			hh->id = id;
			if (!ch->recv(ch->peer, skb))
				return;
		}
		dev_kfree_skb(skb);
	}
}
EXPORT_SYMBOL(queue_ch_frame);

int
dchannel_senddata(struct dchannel *ch, struct sk_buff *skb)
{
	/* check oversize */
	if (skb->len <= 0) {
		printk(KERN_WARNING "%s: skb too small\n", __func__);
		return -EINVAL;
	}
	if (skb->len > ch->maxlen) {
		printk(KERN_WARNING "%s: skb too large(%d/%d)\n",
			__func__, skb->len, ch->maxlen);
		return -EINVAL;
	}
	/* HW lock must be obtained */
	if (test_and_set_bit(FLG_TX_BUSY, &ch->Flags)) {
		skb_queue_tail(&ch->squeue, skb);
		return 0;
	} else {
		/* write to fifo */
		ch->tx_skb = skb;
		ch->tx_idx = 0;
		return 1;
	}
}
EXPORT_SYMBOL(dchannel_senddata);

int
bchannel_senddata(struct bchannel *ch, struct sk_buff *skb)
{

	/* check oversize */
	if (skb->len <= 0) {
		printk(KERN_WARNING "%s: skb too small\n", __func__);
		return -EINVAL;
	}
	if (skb->len > ch->maxlen) {
		printk(KERN_WARNING "%s: skb too large(%d/%d)\n",
			__func__, skb->len, ch->maxlen);
		return -EINVAL;
	}
	/* HW lock must be obtained */
	/* check for pending next_skb */
	if (ch->next_skb) {
		printk(KERN_WARNING
		    "%s: next_skb exist ERROR (skb->len=%d next_skb->len=%d)\n",
		    __func__, skb->len, ch->next_skb->len);
		return -EBUSY;
	}
	if (test_and_set_bit(FLG_TX_BUSY, &ch->Flags)) {
		test_and_set_bit(FLG_TX_NEXT, &ch->Flags);
		ch->next_skb = skb;
		return 0;
	} else {
		/* write to fifo */
		ch->tx_skb = skb;
		ch->tx_idx = 0;
		return 1;
	}
}
EXPORT_SYMBOL(bchannel_senddata);
