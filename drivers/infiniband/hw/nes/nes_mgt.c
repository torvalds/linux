/*
 * Copyright (c) 2006 - 2011 Intel-NE, Inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/kthread.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <net/tcp.h>
#include "nes.h"
#include "nes_mgt.h"

atomic_t pau_qps_created;
atomic_t pau_qps_destroyed;

static void nes_replenish_mgt_rq(struct nes_vnic_mgt *mgtvnic)
{
	unsigned long flags;
	dma_addr_t bus_address;
	struct sk_buff *skb;
	struct nes_hw_nic_rq_wqe *nic_rqe;
	struct nes_hw_mgt *nesmgt;
	struct nes_device *nesdev;
	struct nes_rskb_cb *cb;
	u32 rx_wqes_posted = 0;

	nesmgt = &mgtvnic->mgt;
	nesdev = mgtvnic->nesvnic->nesdev;
	spin_lock_irqsave(&nesmgt->rq_lock, flags);
	if (nesmgt->replenishing_rq != 0) {
		if (((nesmgt->rq_size - 1) == atomic_read(&mgtvnic->rx_skbs_needed)) &&
		    (atomic_read(&mgtvnic->rx_skb_timer_running) == 0)) {
			atomic_set(&mgtvnic->rx_skb_timer_running, 1);
			spin_unlock_irqrestore(&nesmgt->rq_lock, flags);
			mgtvnic->rq_wqes_timer.expires = jiffies + (HZ / 2);      /* 1/2 second */
			add_timer(&mgtvnic->rq_wqes_timer);
		} else {
			spin_unlock_irqrestore(&nesmgt->rq_lock, flags);
		}
		return;
	}
	nesmgt->replenishing_rq = 1;
	spin_unlock_irqrestore(&nesmgt->rq_lock, flags);
	do {
		skb = dev_alloc_skb(mgtvnic->nesvnic->max_frame_size);
		if (skb) {
			skb->dev = mgtvnic->nesvnic->netdev;

			bus_address = pci_map_single(nesdev->pcidev,
						     skb->data, mgtvnic->nesvnic->max_frame_size, PCI_DMA_FROMDEVICE);
			cb = (struct nes_rskb_cb *)&skb->cb[0];
			cb->busaddr = bus_address;
			cb->maplen = mgtvnic->nesvnic->max_frame_size;

			nic_rqe = &nesmgt->rq_vbase[mgtvnic->mgt.rq_head];
			nic_rqe->wqe_words[NES_NIC_RQ_WQE_LENGTH_1_0_IDX] =
				cpu_to_le32(mgtvnic->nesvnic->max_frame_size);
			nic_rqe->wqe_words[NES_NIC_RQ_WQE_LENGTH_3_2_IDX] = 0;
			nic_rqe->wqe_words[NES_NIC_RQ_WQE_FRAG0_LOW_IDX] =
				cpu_to_le32((u32)bus_address);
			nic_rqe->wqe_words[NES_NIC_RQ_WQE_FRAG0_HIGH_IDX] =
				cpu_to_le32((u32)((u64)bus_address >> 32));
			nesmgt->rx_skb[nesmgt->rq_head] = skb;
			nesmgt->rq_head++;
			nesmgt->rq_head &= nesmgt->rq_size - 1;
			atomic_dec(&mgtvnic->rx_skbs_needed);
			barrier();
			if (++rx_wqes_posted == 255) {
				nes_write32(nesdev->regs + NES_WQE_ALLOC, (rx_wqes_posted << 24) | nesmgt->qp_id);
				rx_wqes_posted = 0;
			}
		} else {
			spin_lock_irqsave(&nesmgt->rq_lock, flags);
			if (((nesmgt->rq_size - 1) == atomic_read(&mgtvnic->rx_skbs_needed)) &&
			    (atomic_read(&mgtvnic->rx_skb_timer_running) == 0)) {
				atomic_set(&mgtvnic->rx_skb_timer_running, 1);
				spin_unlock_irqrestore(&nesmgt->rq_lock, flags);
				mgtvnic->rq_wqes_timer.expires = jiffies + (HZ / 2);      /* 1/2 second */
				add_timer(&mgtvnic->rq_wqes_timer);
			} else {
				spin_unlock_irqrestore(&nesmgt->rq_lock, flags);
			}
			break;
		}
	} while (atomic_read(&mgtvnic->rx_skbs_needed));
	barrier();
	if (rx_wqes_posted)
		nes_write32(nesdev->regs + NES_WQE_ALLOC, (rx_wqes_posted << 24) | nesmgt->qp_id);
	nesmgt->replenishing_rq = 0;
}

/**
 * nes_mgt_rq_wqes_timeout
 */
static void nes_mgt_rq_wqes_timeout(struct timer_list *t)
{
	struct nes_vnic_mgt *mgtvnic = from_timer(mgtvnic, t,
						       rq_wqes_timer);

	atomic_set(&mgtvnic->rx_skb_timer_running, 0);
	if (atomic_read(&mgtvnic->rx_skbs_needed))
		nes_replenish_mgt_rq(mgtvnic);
}

/**
 * nes_mgt_free_skb - unmap and free skb
 */
static void nes_mgt_free_skb(struct nes_device *nesdev, struct sk_buff *skb, u32 dir)
{
	struct nes_rskb_cb *cb;

	cb = (struct nes_rskb_cb *)&skb->cb[0];
	pci_unmap_single(nesdev->pcidev, cb->busaddr, cb->maplen, dir);
	cb->busaddr = 0;
	dev_kfree_skb_any(skb);
}

/**
 * nes_download_callback - handle download completions
 */
static void nes_download_callback(struct nes_device *nesdev, struct nes_cqp_request *cqp_request)
{
	struct pau_fpdu_info *fpdu_info = cqp_request->cqp_callback_pointer;
	struct nes_qp *nesqp = fpdu_info->nesqp;
	struct sk_buff *skb;
	int i;

	for (i = 0; i < fpdu_info->frag_cnt; i++) {
		skb = fpdu_info->frags[i].skb;
		if (fpdu_info->frags[i].cmplt) {
			nes_mgt_free_skb(nesdev, skb, PCI_DMA_TODEVICE);
			nes_rem_ref_cm_node(nesqp->cm_node);
		}
	}

	if (fpdu_info->hdr_vbase)
		pci_free_consistent(nesdev->pcidev, fpdu_info->hdr_len,
				    fpdu_info->hdr_vbase, fpdu_info->hdr_pbase);
	kfree(fpdu_info);
}

/**
 * nes_get_seq - Get the seq, ack_seq and window from the packet
 */
static u32 nes_get_seq(struct sk_buff *skb, u32 *ack, u16 *wnd, u32 *fin_rcvd, u32 *rst_rcvd)
{
	struct nes_rskb_cb *cb = (struct nes_rskb_cb *)&skb->cb[0];
	struct iphdr *iph = (struct iphdr *)(cb->data_start + ETH_HLEN);
	struct tcphdr *tcph = (struct tcphdr *)(((char *)iph) + (4 * iph->ihl));

	*ack = be32_to_cpu(tcph->ack_seq);
	*wnd = be16_to_cpu(tcph->window);
	*fin_rcvd = tcph->fin;
	*rst_rcvd = tcph->rst;
	return be32_to_cpu(tcph->seq);
}

/**
 * nes_get_next_skb - Get the next skb based on where current skb is in the queue
 */
static struct sk_buff *nes_get_next_skb(struct nes_device *nesdev, struct nes_qp *nesqp,
					struct sk_buff *skb, u32 nextseq, u32 *ack,
					u16 *wnd, u32 *fin_rcvd, u32 *rst_rcvd)
{
	u32 seq;
	bool processacks;
	struct sk_buff *old_skb;

	if (skb) {
		/* Continue processing fpdu */
		skb = skb_peek_next(skb, &nesqp->pau_list);
		if (!skb)
			goto out;
		processacks = false;
	} else {
		/* Starting a new one */
		if (skb_queue_empty(&nesqp->pau_list))
			goto out;
		skb = skb_peek(&nesqp->pau_list);
		processacks = true;
	}

	while (1) {
		if (skb_queue_empty(&nesqp->pau_list))
			goto out;

		seq = nes_get_seq(skb, ack, wnd, fin_rcvd, rst_rcvd);
		if (seq == nextseq) {
			if (skb->len || processacks)
				break;
		} else if (after(seq, nextseq)) {
			goto out;
		}

		old_skb = skb;
		skb = skb_peek_next(skb, &nesqp->pau_list);
		skb_unlink(old_skb, &nesqp->pau_list);
		nes_mgt_free_skb(nesdev, old_skb, PCI_DMA_TODEVICE);
		nes_rem_ref_cm_node(nesqp->cm_node);
		if (!skb)
			goto out;
	}
	return skb;

out:
	return NULL;
}

/**
 * get_fpdu_info - Find the next complete fpdu and return its fragments.
 */
static int get_fpdu_info(struct nes_device *nesdev, struct nes_qp *nesqp,
			 struct pau_fpdu_info **pau_fpdu_info)
{
	struct sk_buff *skb;
	struct iphdr *iph;
	struct tcphdr *tcph;
	struct nes_rskb_cb *cb;
	struct pau_fpdu_info *fpdu_info = NULL;
	struct pau_fpdu_frag frags[MAX_FPDU_FRAGS];
	u32 fpdu_len = 0;
	u32 tmp_len;
	int frag_cnt = 0;
	u32 tot_len;
	u32 frag_tot;
	u32 ack;
	u32 fin_rcvd;
	u32 rst_rcvd;
	u16 wnd;
	int i;
	int rc = 0;

	*pau_fpdu_info = NULL;

	skb = nes_get_next_skb(nesdev, nesqp, NULL, nesqp->pau_rcv_nxt, &ack, &wnd, &fin_rcvd, &rst_rcvd);
	if (!skb)
		goto out;

	cb = (struct nes_rskb_cb *)&skb->cb[0];
	if (skb->len) {
		fpdu_len = be16_to_cpu(*(__be16 *) skb->data) + MPA_FRAMING;
		fpdu_len = (fpdu_len + 3) & 0xfffffffc;
		tmp_len = fpdu_len;

		/* See if we have all of the fpdu */
		frag_tot = 0;
		memset(&frags, 0, sizeof frags);
		for (i = 0; i < MAX_FPDU_FRAGS; i++) {
			frags[i].physaddr = cb->busaddr;
			frags[i].physaddr += skb->data - cb->data_start;
			frags[i].frag_len = min(tmp_len, skb->len);
			frags[i].skb = skb;
			frags[i].cmplt = (skb->len == frags[i].frag_len);
			frag_tot += frags[i].frag_len;
			frag_cnt++;

			tmp_len -= frags[i].frag_len;
			if (tmp_len == 0)
				break;

			skb = nes_get_next_skb(nesdev, nesqp, skb,
					       nesqp->pau_rcv_nxt + frag_tot, &ack, &wnd, &fin_rcvd, &rst_rcvd);
			if (!skb)
				goto out;
			if (rst_rcvd) {
				/* rst received in the middle of fpdu */
				for (; i >= 0; i--) {
					skb_unlink(frags[i].skb, &nesqp->pau_list);
					nes_mgt_free_skb(nesdev, frags[i].skb, PCI_DMA_TODEVICE);
				}
				cb = (struct nes_rskb_cb *)&skb->cb[0];
				frags[0].physaddr = cb->busaddr;
				frags[0].physaddr += skb->data - cb->data_start;
				frags[0].frag_len = skb->len;
				frags[0].skb = skb;
				frags[0].cmplt = true;
				frag_cnt = 1;
				break;
			}

			cb = (struct nes_rskb_cb *)&skb->cb[0];
		}
	} else {
		/* no data */
		frags[0].physaddr = cb->busaddr;
		frags[0].frag_len = 0;
		frags[0].skb = skb;
		frags[0].cmplt = true;
		frag_cnt = 1;
	}

	/* Found one */
	fpdu_info = kzalloc(sizeof(*fpdu_info), GFP_ATOMIC);
	if (!fpdu_info) {
		rc = -ENOMEM;
		goto out;
	}

	fpdu_info->cqp_request = nes_get_cqp_request(nesdev);
	if (fpdu_info->cqp_request == NULL) {
		nes_debug(NES_DBG_PAU, "Failed to get a cqp_request.\n");
		rc = -ENOMEM;
		goto out;
	}

	cb = (struct nes_rskb_cb *)&frags[0].skb->cb[0];
	iph = (struct iphdr *)(cb->data_start + ETH_HLEN);
	tcph = (struct tcphdr *)(((char *)iph) + (4 * iph->ihl));
	fpdu_info->hdr_len = (((unsigned char *)tcph) + 4 * (tcph->doff)) - cb->data_start;
	fpdu_info->data_len = fpdu_len;
	tot_len = fpdu_info->hdr_len + fpdu_len - ETH_HLEN;

	if (frags[0].cmplt) {
		fpdu_info->hdr_pbase = cb->busaddr;
		fpdu_info->hdr_vbase = NULL;
	} else {
		fpdu_info->hdr_vbase = pci_alloc_consistent(nesdev->pcidev,
							    fpdu_info->hdr_len, &fpdu_info->hdr_pbase);
		if (!fpdu_info->hdr_vbase) {
			nes_debug(NES_DBG_PAU, "Unable to allocate memory for pau first frag\n");
			rc = -ENOMEM;
			goto out;
		}

		/* Copy hdrs, adjusting len and seqnum */
		memcpy(fpdu_info->hdr_vbase, cb->data_start, fpdu_info->hdr_len);
		iph = (struct iphdr *)(fpdu_info->hdr_vbase + ETH_HLEN);
		tcph = (struct tcphdr *)(((char *)iph) + (4 * iph->ihl));
	}

	iph->tot_len = cpu_to_be16(tot_len);
	iph->saddr = cpu_to_be32(0x7f000001);

	tcph->seq = cpu_to_be32(nesqp->pau_rcv_nxt);
	tcph->ack_seq = cpu_to_be32(ack);
	tcph->window = cpu_to_be16(wnd);

	nesqp->pau_rcv_nxt += fpdu_len + fin_rcvd;

	memcpy(fpdu_info->frags, frags, sizeof(fpdu_info->frags));
	fpdu_info->frag_cnt = frag_cnt;
	fpdu_info->nesqp = nesqp;
	*pau_fpdu_info = fpdu_info;

	/* Update skb's for next pass */
	for (i = 0; i < frag_cnt; i++) {
		cb = (struct nes_rskb_cb *)&frags[i].skb->cb[0];
		skb_pull(frags[i].skb, frags[i].frag_len);

		if (frags[i].skb->len == 0) {
			/* Pull skb off the list - it will be freed in the callback */
			if (!skb_queue_empty(&nesqp->pau_list))
				skb_unlink(frags[i].skb, &nesqp->pau_list);
		} else {
			/* Last skb still has data so update the seq */
			iph = (struct iphdr *)(cb->data_start + ETH_HLEN);
			tcph = (struct tcphdr *)(((char *)iph) + (4 * iph->ihl));
			tcph->seq = cpu_to_be32(nesqp->pau_rcv_nxt);
		}
	}

out:
	if (rc) {
		if (fpdu_info) {
			if (fpdu_info->cqp_request)
				nes_put_cqp_request(nesdev, fpdu_info->cqp_request);
			kfree(fpdu_info);
		}
	}
	return rc;
}

/**
 * forward_fpdu - send complete fpdus, one at a time
 */
static int forward_fpdus(struct nes_vnic *nesvnic, struct nes_qp *nesqp)
{
	struct nes_device *nesdev = nesvnic->nesdev;
	struct pau_fpdu_info *fpdu_info;
	struct nes_hw_cqp_wqe *cqp_wqe;
	struct nes_cqp_request *cqp_request;
	unsigned long flags;
	u64 u64tmp;
	u32 u32tmp;
	int rc;

	while (1) {
		spin_lock_irqsave(&nesqp->pau_lock, flags);
		rc = get_fpdu_info(nesdev, nesqp, &fpdu_info);
		if (rc || (fpdu_info == NULL)) {
			spin_unlock_irqrestore(&nesqp->pau_lock, flags);
			return rc;
		}

		cqp_request = fpdu_info->cqp_request;
		cqp_wqe = &cqp_request->cqp_wqe;
		nes_fill_init_cqp_wqe(cqp_wqe, nesdev);
		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_DL_OPCODE_IDX,
				    NES_CQP_DOWNLOAD_SEGMENT |
				    (((u32)nesvnic->logical_port) << NES_CQP_OP_LOGICAL_PORT_SHIFT));

		u32tmp = fpdu_info->hdr_len << 16;
		u32tmp |= fpdu_info->hdr_len + (u32)fpdu_info->data_len;
		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_DL_LENGTH_0_TOTAL_IDX,
				    u32tmp);

		u32tmp = (fpdu_info->frags[1].frag_len << 16) | fpdu_info->frags[0].frag_len;
		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_NIC_SQ_WQE_LENGTH_2_1_IDX,
				    u32tmp);

		u32tmp = (fpdu_info->frags[3].frag_len << 16) | fpdu_info->frags[2].frag_len;
		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_NIC_SQ_WQE_LENGTH_4_3_IDX,
				    u32tmp);

		u64tmp = (u64)fpdu_info->hdr_pbase;
		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_NIC_SQ_WQE_FRAG0_LOW_IDX,
				    lower_32_bits(u64tmp));
		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_NIC_SQ_WQE_FRAG0_HIGH_IDX,
				    upper_32_bits(u64tmp));

		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_NIC_SQ_WQE_FRAG1_LOW_IDX,
				    lower_32_bits(fpdu_info->frags[0].physaddr));
		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_NIC_SQ_WQE_FRAG1_HIGH_IDX,
				    upper_32_bits(fpdu_info->frags[0].physaddr));

		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_NIC_SQ_WQE_FRAG2_LOW_IDX,
				    lower_32_bits(fpdu_info->frags[1].physaddr));
		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_NIC_SQ_WQE_FRAG2_HIGH_IDX,
				    upper_32_bits(fpdu_info->frags[1].physaddr));

		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_NIC_SQ_WQE_FRAG3_LOW_IDX,
				    lower_32_bits(fpdu_info->frags[2].physaddr));
		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_NIC_SQ_WQE_FRAG3_HIGH_IDX,
				    upper_32_bits(fpdu_info->frags[2].physaddr));

		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_NIC_SQ_WQE_FRAG4_LOW_IDX,
				    lower_32_bits(fpdu_info->frags[3].physaddr));
		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_NIC_SQ_WQE_FRAG4_HIGH_IDX,
				    upper_32_bits(fpdu_info->frags[3].physaddr));

		cqp_request->cqp_callback_pointer = fpdu_info;
		cqp_request->callback = 1;
		cqp_request->cqp_callback = nes_download_callback;

		atomic_set(&cqp_request->refcount, 1);
		nes_post_cqp_request(nesdev, cqp_request);
		spin_unlock_irqrestore(&nesqp->pau_lock, flags);
	}

	return 0;
}

static void process_fpdus(struct nes_vnic *nesvnic, struct nes_qp *nesqp)
{
	int again = 1;
	unsigned long flags;

	do {
		/* Ignore rc - if it failed, tcp retries will cause it to try again */
		forward_fpdus(nesvnic, nesqp);

		spin_lock_irqsave(&nesqp->pau_lock, flags);
		if (nesqp->pau_pending) {
			nesqp->pau_pending = 0;
		} else {
			nesqp->pau_busy = 0;
			again = 0;
		}

		spin_unlock_irqrestore(&nesqp->pau_lock, flags);
	} while (again);
}

/**
 * queue_fpdus - Handle fpdu's that hw passed up to sw
 */
static void queue_fpdus(struct sk_buff *skb, struct nes_vnic *nesvnic, struct nes_qp *nesqp)
{
	struct sk_buff *tmpskb;
	struct nes_rskb_cb *cb;
	struct iphdr *iph;
	struct tcphdr *tcph;
	unsigned char *tcph_end;
	u32 rcv_nxt;
	u32 rcv_wnd;
	u32 seqnum;
	u32 len;
	bool process_it = false;
	unsigned long flags;

	/* Move data ptr to after tcp header */
	iph = (struct iphdr *)skb->data;
	tcph = (struct tcphdr *)(((char *)iph) + (4 * iph->ihl));
	seqnum = be32_to_cpu(tcph->seq);
	tcph_end = (((char *)tcph) + (4 * tcph->doff));

	len = be16_to_cpu(iph->tot_len);
	if (skb->len > len)
		skb_trim(skb, len);
	skb_pull(skb, tcph_end - skb->data);

	/* Initialize tracking values */
	cb = (struct nes_rskb_cb *)&skb->cb[0];
	cb->seqnum = seqnum;

	/* Make sure data is in the receive window */
	rcv_nxt = nesqp->pau_rcv_nxt;
	rcv_wnd = le32_to_cpu(nesqp->nesqp_context->rcv_wnd);
	if (!between(seqnum, rcv_nxt, (rcv_nxt + rcv_wnd))) {
		nes_mgt_free_skb(nesvnic->nesdev, skb, PCI_DMA_TODEVICE);
		nes_rem_ref_cm_node(nesqp->cm_node);
		return;
	}

	spin_lock_irqsave(&nesqp->pau_lock, flags);

	if (nesqp->pau_busy)
		nesqp->pau_pending = 1;
	else
		nesqp->pau_busy = 1;

	/* Queue skb by sequence number */
	if (skb_queue_len(&nesqp->pau_list) == 0) {
		skb_queue_head(&nesqp->pau_list, skb);
	} else {
		skb_queue_walk(&nesqp->pau_list, tmpskb) {
			cb = (struct nes_rskb_cb *)&tmpskb->cb[0];
			if (before(seqnum, cb->seqnum))
				break;
		}
		skb_insert(tmpskb, skb, &nesqp->pau_list);
	}
	if (nesqp->pau_state == PAU_READY)
		process_it = true;
	spin_unlock_irqrestore(&nesqp->pau_lock, flags);

	if (process_it)
		process_fpdus(nesvnic, nesqp);

	return;
}

/**
 * mgt_thread - Handle mgt skbs in a safe context
 */
static int mgt_thread(void *context)
{
	struct nes_vnic *nesvnic = context;
	struct sk_buff *skb;
	struct nes_rskb_cb *cb;

	while (!kthread_should_stop()) {
		wait_event_interruptible(nesvnic->mgt_wait_queue,
					 skb_queue_len(&nesvnic->mgt_skb_list) || kthread_should_stop());
		while ((skb_queue_len(&nesvnic->mgt_skb_list)) && !kthread_should_stop()) {
			skb = skb_dequeue(&nesvnic->mgt_skb_list);
			cb = (struct nes_rskb_cb *)&skb->cb[0];
			cb->data_start = skb->data - ETH_HLEN;
			cb->busaddr = pci_map_single(nesvnic->nesdev->pcidev, cb->data_start,
						     nesvnic->max_frame_size, PCI_DMA_TODEVICE);
			queue_fpdus(skb, nesvnic, cb->nesqp);
		}
	}

	/* Closing down so delete any entries on the queue */
	while (skb_queue_len(&nesvnic->mgt_skb_list)) {
		skb = skb_dequeue(&nesvnic->mgt_skb_list);
		cb = (struct nes_rskb_cb *)&skb->cb[0];
		nes_rem_ref_cm_node(cb->nesqp->cm_node);
		dev_kfree_skb_any(skb);
	}
	return 0;
}

/**
 * nes_queue_skbs - Queue skb so it can be handled in a thread context
 */
void nes_queue_mgt_skbs(struct sk_buff *skb, struct nes_vnic *nesvnic, struct nes_qp *nesqp)
{
	struct nes_rskb_cb *cb;

	cb = (struct nes_rskb_cb *)&skb->cb[0];
	cb->nesqp = nesqp;
	skb_queue_tail(&nesvnic->mgt_skb_list, skb);
	wake_up_interruptible(&nesvnic->mgt_wait_queue);
}

void nes_destroy_pau_qp(struct nes_device *nesdev, struct nes_qp *nesqp)
{
	struct sk_buff *skb;
	unsigned long flags;
	atomic_inc(&pau_qps_destroyed);

	/* Free packets that have not yet been forwarded */
	/* Lock is acquired by skb_dequeue when removing the skb */
	spin_lock_irqsave(&nesqp->pau_lock, flags);
	while (skb_queue_len(&nesqp->pau_list)) {
		skb = skb_dequeue(&nesqp->pau_list);
		nes_mgt_free_skb(nesdev, skb, PCI_DMA_TODEVICE);
		nes_rem_ref_cm_node(nesqp->cm_node);
	}
	spin_unlock_irqrestore(&nesqp->pau_lock, flags);
}

static void nes_chg_qh_handler(struct nes_device *nesdev, struct nes_cqp_request *cqp_request)
{
	struct pau_qh_chg *qh_chg = cqp_request->cqp_callback_pointer;
	struct nes_cqp_request *new_request;
	struct nes_hw_cqp_wqe *cqp_wqe;
	struct nes_adapter *nesadapter;
	struct nes_qp *nesqp;
	struct nes_v4_quad nes_quad;
	u32 crc_value;
	u64 u64temp;

	nesadapter = nesdev->nesadapter;
	nesqp = qh_chg->nesqp;

	/* Should we handle the bad completion */
	if (cqp_request->major_code)
		WARN(1, PFX "Invalid cqp_request major_code=0x%x\n",
		       cqp_request->major_code);

	switch (nesqp->pau_state) {
	case PAU_DEL_QH:
		/* Old hash code deleted, now set the new one */
		nesqp->pau_state = PAU_ADD_LB_QH;
		new_request = nes_get_cqp_request(nesdev);
		if (new_request == NULL) {
			nes_debug(NES_DBG_PAU, "Failed to get a new_request.\n");
			WARN_ON(1);
			return;
		}

		memset(&nes_quad, 0, sizeof(nes_quad));
		nes_quad.DstIpAdrIndex =
			cpu_to_le32((u32)PCI_FUNC(nesdev->pcidev->devfn) << 24);
		nes_quad.SrcIpadr = cpu_to_be32(0x7f000001);
		nes_quad.TcpPorts[0] = swab16(nesqp->nesqp_context->tcpPorts[1]);
		nes_quad.TcpPorts[1] = swab16(nesqp->nesqp_context->tcpPorts[0]);

		/* Produce hash key */
		crc_value = get_crc_value(&nes_quad);
		nesqp->hte_index = cpu_to_be32(crc_value ^ 0xffffffff);
		nes_debug(NES_DBG_PAU, "new HTE Index = 0x%08X, CRC = 0x%08X\n",
			  nesqp->hte_index, nesqp->hte_index & nesadapter->hte_index_mask);

		nesqp->hte_index &= nesadapter->hte_index_mask;
		nesqp->nesqp_context->hte_index = cpu_to_le32(nesqp->hte_index);
		nesqp->nesqp_context->ip0 = cpu_to_le32(0x7f000001);
		nesqp->nesqp_context->rcv_nxt = cpu_to_le32(nesqp->pau_rcv_nxt);

		cqp_wqe = &new_request->cqp_wqe;
		nes_fill_init_cqp_wqe(cqp_wqe, nesdev);
		set_wqe_32bit_value(cqp_wqe->wqe_words,
				    NES_CQP_WQE_OPCODE_IDX, NES_CQP_MANAGE_QUAD_HASH |
				    NES_CQP_QP_TYPE_IWARP | NES_CQP_QP_CONTEXT_VALID | NES_CQP_QP_IWARP_STATE_RTS);
		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_ID_IDX, nesqp->hwqp.qp_id);
		u64temp = (u64)nesqp->nesqp_context_pbase;
		set_wqe_64bit_value(cqp_wqe->wqe_words, NES_CQP_QP_WQE_CONTEXT_LOW_IDX, u64temp);

		nes_debug(NES_DBG_PAU, "Waiting for CQP completion for adding the quad hash.\n");

		new_request->cqp_callback_pointer = qh_chg;
		new_request->callback = 1;
		new_request->cqp_callback = nes_chg_qh_handler;
		atomic_set(&new_request->refcount, 1);
		nes_post_cqp_request(nesdev, new_request);
		break;

	case PAU_ADD_LB_QH:
		/* Start processing the queued fpdu's */
		nesqp->pau_state = PAU_READY;
		process_fpdus(qh_chg->nesvnic, qh_chg->nesqp);
		kfree(qh_chg);
		break;
	}
}

/**
 * nes_change_quad_hash
 */
static int nes_change_quad_hash(struct nes_device *nesdev,
				struct nes_vnic *nesvnic, struct nes_qp *nesqp)
{
	struct nes_cqp_request *cqp_request = NULL;
	struct pau_qh_chg *qh_chg = NULL;
	u64 u64temp;
	struct nes_hw_cqp_wqe *cqp_wqe;
	int ret = 0;

	cqp_request = nes_get_cqp_request(nesdev);
	if (cqp_request == NULL) {
		nes_debug(NES_DBG_PAU, "Failed to get a cqp_request.\n");
		ret = -ENOMEM;
		goto chg_qh_err;
	}

	qh_chg = kmalloc(sizeof *qh_chg, GFP_ATOMIC);
	if (!qh_chg) {
		ret = -ENOMEM;
		goto chg_qh_err;
	}
	qh_chg->nesdev = nesdev;
	qh_chg->nesvnic = nesvnic;
	qh_chg->nesqp = nesqp;
	nesqp->pau_state = PAU_DEL_QH;

	cqp_wqe = &cqp_request->cqp_wqe;
	nes_fill_init_cqp_wqe(cqp_wqe, nesdev);
	set_wqe_32bit_value(cqp_wqe->wqe_words,
			    NES_CQP_WQE_OPCODE_IDX, NES_CQP_MANAGE_QUAD_HASH | NES_CQP_QP_DEL_HTE |
			    NES_CQP_QP_TYPE_IWARP | NES_CQP_QP_CONTEXT_VALID | NES_CQP_QP_IWARP_STATE_RTS);
	set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_ID_IDX, nesqp->hwqp.qp_id);
	u64temp = (u64)nesqp->nesqp_context_pbase;
	set_wqe_64bit_value(cqp_wqe->wqe_words, NES_CQP_QP_WQE_CONTEXT_LOW_IDX, u64temp);

	nes_debug(NES_DBG_PAU, "Waiting for CQP completion for deleting the quad hash.\n");

	cqp_request->cqp_callback_pointer = qh_chg;
	cqp_request->callback = 1;
	cqp_request->cqp_callback = nes_chg_qh_handler;
	atomic_set(&cqp_request->refcount, 1);
	nes_post_cqp_request(nesdev, cqp_request);

	return ret;

chg_qh_err:
	kfree(qh_chg);
	if (cqp_request)
		nes_put_cqp_request(nesdev, cqp_request);
	return ret;
}

/**
 * nes_mgt_ce_handler
 * This management code deals with any packed and unaligned (pau) fpdu's
 * that the hardware cannot handle.
 */
static void nes_mgt_ce_handler(struct nes_device *nesdev, struct nes_hw_nic_cq *cq)
{
	struct nes_vnic_mgt *mgtvnic = container_of(cq, struct nes_vnic_mgt, mgt_cq);
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	u32 head;
	u32 cq_size;
	u32 cqe_count = 0;
	u32 cqe_misc;
	u32 qp_id = 0;
	u32 skbs_needed;
	unsigned long context;
	struct nes_qp *nesqp;
	struct sk_buff *rx_skb;
	struct nes_rskb_cb *cb;

	head = cq->cq_head;
	cq_size = cq->cq_size;

	while (1) {
		cqe_misc = le32_to_cpu(cq->cq_vbase[head].cqe_words[NES_NIC_CQE_MISC_IDX]);
		if (!(cqe_misc & NES_NIC_CQE_VALID))
			break;

		nesqp = NULL;
		if (cqe_misc & NES_NIC_CQE_ACCQP_VALID) {
			qp_id = le32_to_cpu(cq->cq_vbase[head].cqe_words[NES_NIC_CQE_ACCQP_ID_IDX]);
			qp_id &= 0x001fffff;
			if (qp_id < nesadapter->max_qp) {
				context = (unsigned long)nesadapter->qp_table[qp_id - NES_FIRST_QPN];
				nesqp = (struct nes_qp *)context;
			}
		}

		if (nesqp) {
			if (nesqp->pau_mode == false) {
				nesqp->pau_mode = true; /* First time for this qp */
				nesqp->pau_rcv_nxt = le32_to_cpu(
					cq->cq_vbase[head].cqe_words[NES_NIC_CQE_HASH_RCVNXT]);
				skb_queue_head_init(&nesqp->pau_list);
				spin_lock_init(&nesqp->pau_lock);
				atomic_inc(&pau_qps_created);
				nes_change_quad_hash(nesdev, mgtvnic->nesvnic, nesqp);
			}

			rx_skb = mgtvnic->mgt.rx_skb[mgtvnic->mgt.rq_tail];
			rx_skb->len = 0;
			skb_put(rx_skb, cqe_misc & 0x0000ffff);
			rx_skb->protocol = eth_type_trans(rx_skb, mgtvnic->nesvnic->netdev);
			cb = (struct nes_rskb_cb *)&rx_skb->cb[0];
			pci_unmap_single(nesdev->pcidev, cb->busaddr, cb->maplen, PCI_DMA_FROMDEVICE);
			cb->busaddr = 0;
			mgtvnic->mgt.rq_tail++;
			mgtvnic->mgt.rq_tail &= mgtvnic->mgt.rq_size - 1;

			nes_add_ref_cm_node(nesqp->cm_node);
			nes_queue_mgt_skbs(rx_skb, mgtvnic->nesvnic, nesqp);
		} else {
			printk(KERN_ERR PFX "Invalid QP %d for packed/unaligned handling\n", qp_id);
		}

		cq->cq_vbase[head].cqe_words[NES_NIC_CQE_MISC_IDX] = 0;
		cqe_count++;
		if (++head >= cq_size)
			head = 0;

		if (cqe_count == 255) {
			/* Replenish mgt CQ */
			nes_write32(nesdev->regs + NES_CQE_ALLOC, cq->cq_number | (cqe_count << 16));
			nesdev->currcq_count += cqe_count;
			cqe_count = 0;
		}

		skbs_needed = atomic_inc_return(&mgtvnic->rx_skbs_needed);
		if (skbs_needed > (mgtvnic->mgt.rq_size >> 1))
			nes_replenish_mgt_rq(mgtvnic);
	}

	cq->cq_head = head;
	nes_write32(nesdev->regs + NES_CQE_ALLOC, NES_CQE_ALLOC_NOTIFY_NEXT |
		    cq->cq_number | (cqe_count << 16));
	nes_read32(nesdev->regs + NES_CQE_ALLOC);
	nesdev->currcq_count += cqe_count;
}

/**
 * nes_init_mgt_qp
 */
int nes_init_mgt_qp(struct nes_device *nesdev, struct net_device *netdev, struct nes_vnic *nesvnic)
{
	struct nes_vnic_mgt *mgtvnic;
	u32 counter;
	void *vmem;
	dma_addr_t pmem;
	struct nes_hw_cqp_wqe *cqp_wqe;
	u32 cqp_head;
	unsigned long flags;
	struct nes_hw_nic_qp_context *mgt_context;
	u64 u64temp;
	struct nes_hw_nic_rq_wqe *mgt_rqe;
	struct sk_buff *skb;
	u32 wqe_count;
	struct nes_rskb_cb *cb;
	u32 mgt_mem_size;
	void *mgt_vbase;
	dma_addr_t mgt_pbase;
	int i;
	int ret;

	/* Allocate space the all mgt QPs once */
	mgtvnic = kcalloc(NES_MGT_QP_COUNT, sizeof(struct nes_vnic_mgt),
			  GFP_KERNEL);
	if (!mgtvnic)
		return -ENOMEM;

	/* Allocate fragment, RQ, and CQ; Reuse CEQ based on the PCI function */
	/* We are not sending from this NIC so sq is not allocated */
	mgt_mem_size = 256 +
		       (NES_MGT_WQ_COUNT * sizeof(struct nes_hw_nic_rq_wqe)) +
		       (NES_MGT_WQ_COUNT * sizeof(struct nes_hw_nic_cqe)) +
		       sizeof(struct nes_hw_nic_qp_context);
	mgt_mem_size = (mgt_mem_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	mgt_vbase = pci_alloc_consistent(nesdev->pcidev, NES_MGT_QP_COUNT * mgt_mem_size, &mgt_pbase);
	if (!mgt_vbase) {
		kfree(mgtvnic);
		nes_debug(NES_DBG_INIT, "Unable to allocate memory for mgt host descriptor rings\n");
		return -ENOMEM;
	}

	nesvnic->mgt_mem_size = NES_MGT_QP_COUNT * mgt_mem_size;
	nesvnic->mgt_vbase = mgt_vbase;
	nesvnic->mgt_pbase = mgt_pbase;

	skb_queue_head_init(&nesvnic->mgt_skb_list);
	init_waitqueue_head(&nesvnic->mgt_wait_queue);
	nesvnic->mgt_thread = kthread_run(mgt_thread, nesvnic, "nes_mgt_thread");

	for (i = 0; i < NES_MGT_QP_COUNT; i++) {
		mgtvnic->nesvnic = nesvnic;
		mgtvnic->mgt.qp_id = nesdev->mac_index + NES_MGT_QP_OFFSET + i;
		memset(mgt_vbase, 0, mgt_mem_size);
		nes_debug(NES_DBG_INIT, "Allocated mgt QP structures at %p (phys = %016lX), size = %u.\n",
			  mgt_vbase, (unsigned long)mgt_pbase, mgt_mem_size);

		vmem = (void *)(((unsigned long)mgt_vbase + (256 - 1)) &
				~(unsigned long)(256 - 1));
		pmem = (dma_addr_t)(((unsigned long long)mgt_pbase + (256 - 1)) &
				    ~(unsigned long long)(256 - 1));

		spin_lock_init(&mgtvnic->mgt.rq_lock);

		/* setup the RQ */
		mgtvnic->mgt.rq_vbase = vmem;
		mgtvnic->mgt.rq_pbase = pmem;
		mgtvnic->mgt.rq_head = 0;
		mgtvnic->mgt.rq_tail = 0;
		mgtvnic->mgt.rq_size = NES_MGT_WQ_COUNT;

		/* setup the CQ */
		vmem += (NES_MGT_WQ_COUNT * sizeof(struct nes_hw_nic_rq_wqe));
		pmem += (NES_MGT_WQ_COUNT * sizeof(struct nes_hw_nic_rq_wqe));

		mgtvnic->mgt_cq.cq_number = mgtvnic->mgt.qp_id;
		mgtvnic->mgt_cq.cq_vbase = vmem;
		mgtvnic->mgt_cq.cq_pbase = pmem;
		mgtvnic->mgt_cq.cq_head = 0;
		mgtvnic->mgt_cq.cq_size = NES_MGT_WQ_COUNT;

		mgtvnic->mgt_cq.ce_handler = nes_mgt_ce_handler;

		/* Send CreateCQ request to CQP */
		spin_lock_irqsave(&nesdev->cqp.lock, flags);
		cqp_head = nesdev->cqp.sq_head;

		cqp_wqe = &nesdev->cqp.sq_vbase[cqp_head];
		nes_fill_init_cqp_wqe(cqp_wqe, nesdev);

		cqp_wqe->wqe_words[NES_CQP_WQE_OPCODE_IDX] = cpu_to_le32(
			NES_CQP_CREATE_CQ | NES_CQP_CQ_CEQ_VALID |
			((u32)mgtvnic->mgt_cq.cq_size << 16));
		cqp_wqe->wqe_words[NES_CQP_WQE_ID_IDX] = cpu_to_le32(
			mgtvnic->mgt_cq.cq_number | ((u32)nesdev->ceq_index << 16));
		u64temp = (u64)mgtvnic->mgt_cq.cq_pbase;
		set_wqe_64bit_value(cqp_wqe->wqe_words, NES_CQP_CQ_WQE_PBL_LOW_IDX, u64temp);
		cqp_wqe->wqe_words[NES_CQP_CQ_WQE_CQ_CONTEXT_HIGH_IDX] = 0;
		u64temp = (unsigned long)&mgtvnic->mgt_cq;
		cqp_wqe->wqe_words[NES_CQP_CQ_WQE_CQ_CONTEXT_LOW_IDX] = cpu_to_le32((u32)(u64temp >> 1));
		cqp_wqe->wqe_words[NES_CQP_CQ_WQE_CQ_CONTEXT_HIGH_IDX] =
			cpu_to_le32(((u32)((u64temp) >> 33)) & 0x7FFFFFFF);
		cqp_wqe->wqe_words[NES_CQP_CQ_WQE_DOORBELL_INDEX_HIGH_IDX] = 0;

		if (++cqp_head >= nesdev->cqp.sq_size)
			cqp_head = 0;
		cqp_wqe = &nesdev->cqp.sq_vbase[cqp_head];
		nes_fill_init_cqp_wqe(cqp_wqe, nesdev);

		/* Send CreateQP request to CQP */
		mgt_context = (void *)(&mgtvnic->mgt_cq.cq_vbase[mgtvnic->mgt_cq.cq_size]);
		mgt_context->context_words[NES_NIC_CTX_MISC_IDX] =
			cpu_to_le32((u32)NES_MGT_CTX_SIZE |
				    ((u32)PCI_FUNC(nesdev->pcidev->devfn) << 12));
		nes_debug(NES_DBG_INIT, "RX_WINDOW_BUFFER_PAGE_TABLE_SIZE = 0x%08X, RX_WINDOW_BUFFER_SIZE = 0x%08X\n",
			  nes_read_indexed(nesdev, NES_IDX_RX_WINDOW_BUFFER_PAGE_TABLE_SIZE),
			  nes_read_indexed(nesdev, NES_IDX_RX_WINDOW_BUFFER_SIZE));
		if (nes_read_indexed(nesdev, NES_IDX_RX_WINDOW_BUFFER_SIZE) != 0)
			mgt_context->context_words[NES_NIC_CTX_MISC_IDX] |= cpu_to_le32(NES_NIC_BACK_STORE);

		u64temp = (u64)mgtvnic->mgt.rq_pbase;
		mgt_context->context_words[NES_NIC_CTX_SQ_LOW_IDX] = cpu_to_le32((u32)u64temp);
		mgt_context->context_words[NES_NIC_CTX_SQ_HIGH_IDX] = cpu_to_le32((u32)(u64temp >> 32));
		u64temp = (u64)mgtvnic->mgt.rq_pbase;
		mgt_context->context_words[NES_NIC_CTX_RQ_LOW_IDX] = cpu_to_le32((u32)u64temp);
		mgt_context->context_words[NES_NIC_CTX_RQ_HIGH_IDX] = cpu_to_le32((u32)(u64temp >> 32));

		cqp_wqe->wqe_words[NES_CQP_WQE_OPCODE_IDX] = cpu_to_le32(NES_CQP_CREATE_QP |
									 NES_CQP_QP_TYPE_NIC);
		cqp_wqe->wqe_words[NES_CQP_WQE_ID_IDX] = cpu_to_le32(mgtvnic->mgt.qp_id);
		u64temp = (u64)mgtvnic->mgt_cq.cq_pbase +
			  (mgtvnic->mgt_cq.cq_size * sizeof(struct nes_hw_nic_cqe));
		set_wqe_64bit_value(cqp_wqe->wqe_words, NES_CQP_QP_WQE_CONTEXT_LOW_IDX, u64temp);

		if (++cqp_head >= nesdev->cqp.sq_size)
			cqp_head = 0;
		nesdev->cqp.sq_head = cqp_head;

		barrier();

		/* Ring doorbell (2 WQEs) */
		nes_write32(nesdev->regs + NES_WQE_ALLOC, 0x02800000 | nesdev->cqp.qp_id);

		spin_unlock_irqrestore(&nesdev->cqp.lock, flags);
		nes_debug(NES_DBG_INIT, "Waiting for create MGT QP%u to complete.\n",
			  mgtvnic->mgt.qp_id);

		ret = wait_event_timeout(nesdev->cqp.waitq, (nesdev->cqp.sq_tail == cqp_head),
					 NES_EVENT_TIMEOUT);
		nes_debug(NES_DBG_INIT, "Create MGT QP%u completed, wait_event_timeout ret = %u.\n",
			  mgtvnic->mgt.qp_id, ret);
		if (!ret) {
			nes_debug(NES_DBG_INIT, "MGT QP%u create timeout expired\n", mgtvnic->mgt.qp_id);
			if (i == 0) {
				pci_free_consistent(nesdev->pcidev, nesvnic->mgt_mem_size, nesvnic->mgt_vbase,
						    nesvnic->mgt_pbase);
				kfree(mgtvnic);
			} else {
				nes_destroy_mgt(nesvnic);
			}
			return -EIO;
		}

		/* Populate the RQ */
		for (counter = 0; counter < (NES_MGT_WQ_COUNT - 1); counter++) {
			skb = dev_alloc_skb(nesvnic->max_frame_size);
			if (!skb) {
				nes_debug(NES_DBG_INIT, "%s: out of memory for receive skb\n", netdev->name);
				return -ENOMEM;
			}

			skb->dev = netdev;

			pmem = pci_map_single(nesdev->pcidev, skb->data,
					      nesvnic->max_frame_size, PCI_DMA_FROMDEVICE);
			cb = (struct nes_rskb_cb *)&skb->cb[0];
			cb->busaddr = pmem;
			cb->maplen = nesvnic->max_frame_size;

			mgt_rqe = &mgtvnic->mgt.rq_vbase[counter];
			mgt_rqe->wqe_words[NES_NIC_RQ_WQE_LENGTH_1_0_IDX] = cpu_to_le32((u32)nesvnic->max_frame_size);
			mgt_rqe->wqe_words[NES_NIC_RQ_WQE_LENGTH_3_2_IDX] = 0;
			mgt_rqe->wqe_words[NES_NIC_RQ_WQE_FRAG0_LOW_IDX] = cpu_to_le32((u32)pmem);
			mgt_rqe->wqe_words[NES_NIC_RQ_WQE_FRAG0_HIGH_IDX] = cpu_to_le32((u32)((u64)pmem >> 32));
			mgtvnic->mgt.rx_skb[counter] = skb;
		}

		timer_setup(&mgtvnic->rq_wqes_timer, nes_mgt_rq_wqes_timeout,
			    0);

		wqe_count = NES_MGT_WQ_COUNT - 1;
		mgtvnic->mgt.rq_head = wqe_count;
		barrier();
		do {
			counter = min(wqe_count, ((u32)255));
			wqe_count -= counter;
			nes_write32(nesdev->regs + NES_WQE_ALLOC, (counter << 24) | mgtvnic->mgt.qp_id);
		} while (wqe_count);

		nes_write32(nesdev->regs + NES_CQE_ALLOC, NES_CQE_ALLOC_NOTIFY_NEXT |
			    mgtvnic->mgt_cq.cq_number);
		nes_read32(nesdev->regs + NES_CQE_ALLOC);

		mgt_vbase += mgt_mem_size;
		mgt_pbase += mgt_mem_size;
		nesvnic->mgtvnic[i] = mgtvnic++;
	}
	return 0;
}


void nes_destroy_mgt(struct nes_vnic *nesvnic)
{
	struct nes_device *nesdev = nesvnic->nesdev;
	struct nes_vnic_mgt *mgtvnic;
	struct nes_vnic_mgt *first_mgtvnic;
	unsigned long flags;
	struct nes_hw_cqp_wqe *cqp_wqe;
	u32 cqp_head;
	struct sk_buff *rx_skb;
	int i;
	int ret;

	kthread_stop(nesvnic->mgt_thread);

	/* Free remaining NIC receive buffers */
	first_mgtvnic = nesvnic->mgtvnic[0];
	for (i = 0; i < NES_MGT_QP_COUNT; i++) {
		mgtvnic = nesvnic->mgtvnic[i];
		if (mgtvnic == NULL)
			continue;

		while (mgtvnic->mgt.rq_head != mgtvnic->mgt.rq_tail) {
			rx_skb = mgtvnic->mgt.rx_skb[mgtvnic->mgt.rq_tail];
			nes_mgt_free_skb(nesdev, rx_skb, PCI_DMA_FROMDEVICE);
			mgtvnic->mgt.rq_tail++;
			mgtvnic->mgt.rq_tail &= (mgtvnic->mgt.rq_size - 1);
		}

		spin_lock_irqsave(&nesdev->cqp.lock, flags);

		/* Destroy NIC QP */
		cqp_head = nesdev->cqp.sq_head;
		cqp_wqe = &nesdev->cqp.sq_vbase[cqp_head];
		nes_fill_init_cqp_wqe(cqp_wqe, nesdev);

		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_OPCODE_IDX,
				    (NES_CQP_DESTROY_QP | NES_CQP_QP_TYPE_NIC));
		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_ID_IDX,
				    mgtvnic->mgt.qp_id);

		if (++cqp_head >= nesdev->cqp.sq_size)
			cqp_head = 0;

		cqp_wqe = &nesdev->cqp.sq_vbase[cqp_head];

		/* Destroy NIC CQ */
		nes_fill_init_cqp_wqe(cqp_wqe, nesdev);
		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_OPCODE_IDX,
				    (NES_CQP_DESTROY_CQ | ((u32)mgtvnic->mgt_cq.cq_size << 16)));
		set_wqe_32bit_value(cqp_wqe->wqe_words, NES_CQP_WQE_ID_IDX,
				    (mgtvnic->mgt_cq.cq_number | ((u32)nesdev->ceq_index << 16)));

		if (++cqp_head >= nesdev->cqp.sq_size)
			cqp_head = 0;

		nesdev->cqp.sq_head = cqp_head;
		barrier();

		/* Ring doorbell (2 WQEs) */
		nes_write32(nesdev->regs + NES_WQE_ALLOC, 0x02800000 | nesdev->cqp.qp_id);

		spin_unlock_irqrestore(&nesdev->cqp.lock, flags);
		nes_debug(NES_DBG_SHUTDOWN, "Waiting for CQP, cqp_head=%u, cqp.sq_head=%u,"
			  " cqp.sq_tail=%u, cqp.sq_size=%u\n",
			  cqp_head, nesdev->cqp.sq_head,
			  nesdev->cqp.sq_tail, nesdev->cqp.sq_size);

		ret = wait_event_timeout(nesdev->cqp.waitq, (nesdev->cqp.sq_tail == cqp_head),
					 NES_EVENT_TIMEOUT);

		nes_debug(NES_DBG_SHUTDOWN, "Destroy MGT QP returned, wait_event_timeout ret = %u, cqp_head=%u,"
			  " cqp.sq_head=%u, cqp.sq_tail=%u\n",
			  ret, cqp_head, nesdev->cqp.sq_head, nesdev->cqp.sq_tail);
		if (!ret)
			nes_debug(NES_DBG_SHUTDOWN, "MGT QP%u destroy timeout expired\n",
				  mgtvnic->mgt.qp_id);

		nesvnic->mgtvnic[i] = NULL;
	}

	if (nesvnic->mgt_vbase) {
		pci_free_consistent(nesdev->pcidev, nesvnic->mgt_mem_size, nesvnic->mgt_vbase,
				    nesvnic->mgt_pbase);
		nesvnic->mgt_vbase = NULL;
		nesvnic->mgt_pbase = 0;
	}

	kfree(first_mgtvnic);
}
