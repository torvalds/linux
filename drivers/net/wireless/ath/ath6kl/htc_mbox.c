/*
 * Copyright (c) 2007-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2012 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "core.h"
#include "hif.h"
#include "debug.h"
#include "hif-ops.h"
#include "trace.h"

#include <asm/unaligned.h>

#define CALC_TXRX_PADDED_LEN(dev, len)  (__ALIGN_MASK((len), (dev)->block_mask))

static void ath6kl_htc_mbox_cleanup(struct htc_target *target);
static void ath6kl_htc_mbox_stop(struct htc_target *target);
static int ath6kl_htc_mbox_add_rxbuf_multiple(struct htc_target *target,
					      struct list_head *pkt_queue);
static void ath6kl_htc_set_credit_dist(struct htc_target *target,
				       struct ath6kl_htc_credit_info *cred_info,
				       u16 svc_pri_order[], int len);

/* threshold to re-enable Tx bundling for an AC*/
#define TX_RESUME_BUNDLE_THRESHOLD	1500

/* Functions for Tx credit handling */
static void ath6kl_credit_deposit(struct ath6kl_htc_credit_info *cred_info,
				  struct htc_endpoint_credit_dist *ep_dist,
				  int credits)
{
	ath6kl_dbg(ATH6KL_DBG_CREDIT, "credit deposit ep %d credits %d\n",
		   ep_dist->endpoint, credits);

	ep_dist->credits += credits;
	ep_dist->cred_assngd += credits;
	cred_info->cur_free_credits -= credits;
}

static void ath6kl_credit_init(struct ath6kl_htc_credit_info *cred_info,
			       struct list_head *ep_list,
			       int tot_credits)
{
	struct htc_endpoint_credit_dist *cur_ep_dist;
	int count;

	ath6kl_dbg(ATH6KL_DBG_CREDIT, "credit init total %d\n", tot_credits);

	cred_info->cur_free_credits = tot_credits;
	cred_info->total_avail_credits = tot_credits;

	list_for_each_entry(cur_ep_dist, ep_list, list) {
		if (cur_ep_dist->endpoint == ENDPOINT_0)
			continue;

		cur_ep_dist->cred_min = cur_ep_dist->cred_per_msg;

		if (tot_credits > 4) {
			if ((cur_ep_dist->svc_id == WMI_DATA_BK_SVC) ||
			    (cur_ep_dist->svc_id == WMI_DATA_BE_SVC)) {
				ath6kl_credit_deposit(cred_info,
						      cur_ep_dist,
						      cur_ep_dist->cred_min);
				cur_ep_dist->dist_flags |= HTC_EP_ACTIVE;
			}
		}

		if (cur_ep_dist->svc_id == WMI_CONTROL_SVC) {
			ath6kl_credit_deposit(cred_info, cur_ep_dist,
					      cur_ep_dist->cred_min);
			/*
			 * Control service is always marked active, it
			 * never goes inactive EVER.
			 */
			cur_ep_dist->dist_flags |= HTC_EP_ACTIVE;
		}

		/*
		 * Streams have to be created (explicit | implicit) for all
		 * kinds of traffic. BE endpoints are also inactive in the
		 * beginning. When BE traffic starts it creates implicit
		 * streams that redistributes credits.
		 *
		 * Note: all other endpoints have minimums set but are
		 * initially given NO credits. credits will be distributed
		 * as traffic activity demands
		 */
	}

	/*
	 * For ath6kl_credit_seek function,
	 * it use list_for_each_entry_reverse to walk around the whole ep list.
	 * Therefore assign this lowestpri_ep_dist after walk around the ep_list
	 */
	cred_info->lowestpri_ep_dist = cur_ep_dist->list;

	WARN_ON(cred_info->cur_free_credits <= 0);

	list_for_each_entry(cur_ep_dist, ep_list, list) {
		if (cur_ep_dist->endpoint == ENDPOINT_0)
			continue;

		if (cur_ep_dist->svc_id == WMI_CONTROL_SVC) {
			cur_ep_dist->cred_norm = cur_ep_dist->cred_per_msg;
		} else {
			/*
			 * For the remaining data endpoints, we assume that
			 * each cred_per_msg are the same. We use a simple
			 * calculation here, we take the remaining credits
			 * and determine how many max messages this can
			 * cover and then set each endpoint's normal value
			 * equal to 3/4 this amount.
			 */
			count = (cred_info->cur_free_credits /
				 cur_ep_dist->cred_per_msg)
				* cur_ep_dist->cred_per_msg;
			count = (count * 3) >> 2;
			count = max(count, cur_ep_dist->cred_per_msg);
			cur_ep_dist->cred_norm = count;
		}

		ath6kl_dbg(ATH6KL_DBG_CREDIT,
			   "credit ep %d svc_id %d credits %d per_msg %d norm %d min %d\n",
			   cur_ep_dist->endpoint,
			   cur_ep_dist->svc_id,
			   cur_ep_dist->credits,
			   cur_ep_dist->cred_per_msg,
			   cur_ep_dist->cred_norm,
			   cur_ep_dist->cred_min);
	}
}

/* initialize and setup credit distribution */
static int ath6kl_htc_mbox_credit_setup(struct htc_target *htc_target,
			       struct ath6kl_htc_credit_info *cred_info)
{
	u16 servicepriority[5];

	memset(cred_info, 0, sizeof(struct ath6kl_htc_credit_info));

	servicepriority[0] = WMI_CONTROL_SVC;  /* highest */
	servicepriority[1] = WMI_DATA_VO_SVC;
	servicepriority[2] = WMI_DATA_VI_SVC;
	servicepriority[3] = WMI_DATA_BE_SVC;
	servicepriority[4] = WMI_DATA_BK_SVC; /* lowest */

	/* set priority list */
	ath6kl_htc_set_credit_dist(htc_target, cred_info, servicepriority, 5);

	return 0;
}

/* reduce an ep's credits back to a set limit */
static void ath6kl_credit_reduce(struct ath6kl_htc_credit_info *cred_info,
				 struct htc_endpoint_credit_dist *ep_dist,
				 int limit)
{
	int credits;

	ath6kl_dbg(ATH6KL_DBG_CREDIT, "credit reduce ep %d limit %d\n",
		   ep_dist->endpoint, limit);

	ep_dist->cred_assngd = limit;

	if (ep_dist->credits <= limit)
		return;

	credits = ep_dist->credits - limit;
	ep_dist->credits -= credits;
	cred_info->cur_free_credits += credits;
}

static void ath6kl_credit_update(struct ath6kl_htc_credit_info *cred_info,
				 struct list_head *epdist_list)
{
	struct htc_endpoint_credit_dist *cur_list;

	list_for_each_entry(cur_list, epdist_list, list) {
		if (cur_list->endpoint == ENDPOINT_0)
			continue;

		if (cur_list->cred_to_dist > 0) {
			cur_list->credits += cur_list->cred_to_dist;
			cur_list->cred_to_dist = 0;

			if (cur_list->credits > cur_list->cred_assngd)
				ath6kl_credit_reduce(cred_info,
						     cur_list,
						     cur_list->cred_assngd);

			if (cur_list->credits > cur_list->cred_norm)
				ath6kl_credit_reduce(cred_info, cur_list,
						     cur_list->cred_norm);

			if (!(cur_list->dist_flags & HTC_EP_ACTIVE)) {
				if (cur_list->txq_depth == 0)
					ath6kl_credit_reduce(cred_info,
							     cur_list, 0);
			}
		}
	}
}

/*
 * HTC has an endpoint that needs credits, ep_dist is the endpoint in
 * question.
 */
static void ath6kl_credit_seek(struct ath6kl_htc_credit_info *cred_info,
				struct htc_endpoint_credit_dist *ep_dist)
{
	struct htc_endpoint_credit_dist *curdist_list;
	int credits = 0;
	int need;

	if (ep_dist->svc_id == WMI_CONTROL_SVC)
		goto out;

	if ((ep_dist->svc_id == WMI_DATA_VI_SVC) ||
	    (ep_dist->svc_id == WMI_DATA_VO_SVC))
		if ((ep_dist->cred_assngd >= ep_dist->cred_norm))
			goto out;

	/*
	 * For all other services, we follow a simple algorithm of:
	 *
	 * 1. checking the free pool for credits
	 * 2. checking lower priority endpoints for credits to take
	 */

	credits = min(cred_info->cur_free_credits, ep_dist->seek_cred);

	if (credits >= ep_dist->seek_cred)
		goto out;

	/*
	 * We don't have enough in the free pool, try taking away from
	 * lower priority services The rule for taking away credits:
	 *
	 *   1. Only take from lower priority endpoints
	 *   2. Only take what is allocated above the minimum (never
	 *      starve an endpoint completely)
	 *   3. Only take what you need.
	 */

	list_for_each_entry_reverse(curdist_list,
				    &cred_info->lowestpri_ep_dist,
				    list) {
		if (curdist_list == ep_dist)
			break;

		need = ep_dist->seek_cred - cred_info->cur_free_credits;

		if ((curdist_list->cred_assngd - need) >=
		     curdist_list->cred_min) {
			/*
			 * The current one has been allocated more than
			 * it's minimum and it has enough credits assigned
			 * above it's minimum to fulfill our need try to
			 * take away just enough to fulfill our need.
			 */
			ath6kl_credit_reduce(cred_info, curdist_list,
					     curdist_list->cred_assngd - need);

			if (cred_info->cur_free_credits >=
			    ep_dist->seek_cred)
				break;
		}

		if (curdist_list->endpoint == ENDPOINT_0)
			break;
	}

	credits = min(cred_info->cur_free_credits, ep_dist->seek_cred);

out:
	/* did we find some credits? */
	if (credits)
		ath6kl_credit_deposit(cred_info, ep_dist, credits);

	ep_dist->seek_cred = 0;
}

/* redistribute credits based on activity change */
static void ath6kl_credit_redistribute(struct ath6kl_htc_credit_info *info,
				       struct list_head *ep_dist_list)
{
	struct htc_endpoint_credit_dist *curdist_list;

	list_for_each_entry(curdist_list, ep_dist_list, list) {
		if (curdist_list->endpoint == ENDPOINT_0)
			continue;

		if ((curdist_list->svc_id == WMI_DATA_BK_SVC)  ||
		    (curdist_list->svc_id == WMI_DATA_BE_SVC))
			curdist_list->dist_flags |= HTC_EP_ACTIVE;

		if ((curdist_list->svc_id != WMI_CONTROL_SVC) &&
		    !(curdist_list->dist_flags & HTC_EP_ACTIVE)) {
			if (curdist_list->txq_depth == 0)
				ath6kl_credit_reduce(info, curdist_list, 0);
			else
				ath6kl_credit_reduce(info,
						     curdist_list,
						     curdist_list->cred_min);
		}
	}
}

/*
 *
 * This function is invoked whenever endpoints require credit
 * distributions. A lock is held while this function is invoked, this
 * function shall NOT block. The ep_dist_list is a list of distribution
 * structures in prioritized order as defined by the call to the
 * htc_set_credit_dist() api.
 */
static void ath6kl_credit_distribute(struct ath6kl_htc_credit_info *cred_info,
				     struct list_head *ep_dist_list,
			      enum htc_credit_dist_reason reason)
{
	switch (reason) {
	case HTC_CREDIT_DIST_SEND_COMPLETE:
		ath6kl_credit_update(cred_info, ep_dist_list);
		break;
	case HTC_CREDIT_DIST_ACTIVITY_CHANGE:
		ath6kl_credit_redistribute(cred_info, ep_dist_list);
		break;
	default:
		break;
	}

	WARN_ON(cred_info->cur_free_credits > cred_info->total_avail_credits);
	WARN_ON(cred_info->cur_free_credits < 0);
}

static void ath6kl_htc_tx_buf_align(u8 **buf, unsigned long len)
{
	u8 *align_addr;

	if (!IS_ALIGNED((unsigned long) *buf, 4)) {
		align_addr = PTR_ALIGN(*buf - 4, 4);
		memmove(align_addr, *buf, len);
		*buf = align_addr;
	}
}

static void ath6kl_htc_tx_prep_pkt(struct htc_packet *packet, u8 flags,
				   int ctrl0, int ctrl1)
{
	struct htc_frame_hdr *hdr;

	packet->buf -= HTC_HDR_LENGTH;
	hdr =  (struct htc_frame_hdr *)packet->buf;

	/* Endianess? */
	put_unaligned((u16)packet->act_len, &hdr->payld_len);
	hdr->flags = flags;
	hdr->eid = packet->endpoint;
	hdr->ctrl[0] = ctrl0;
	hdr->ctrl[1] = ctrl1;
}

static void htc_reclaim_txctrl_buf(struct htc_target *target,
				   struct htc_packet *pkt)
{
	spin_lock_bh(&target->htc_lock);
	list_add_tail(&pkt->list, &target->free_ctrl_txbuf);
	spin_unlock_bh(&target->htc_lock);
}

static struct htc_packet *htc_get_control_buf(struct htc_target *target,
					      bool tx)
{
	struct htc_packet *packet = NULL;
	struct list_head *buf_list;

	buf_list = tx ? &target->free_ctrl_txbuf : &target->free_ctrl_rxbuf;

	spin_lock_bh(&target->htc_lock);

	if (list_empty(buf_list)) {
		spin_unlock_bh(&target->htc_lock);
		return NULL;
	}

	packet = list_first_entry(buf_list, struct htc_packet, list);
	list_del(&packet->list);
	spin_unlock_bh(&target->htc_lock);

	if (tx)
		packet->buf = packet->buf_start + HTC_HDR_LENGTH;

	return packet;
}

static void htc_tx_comp_update(struct htc_target *target,
			       struct htc_endpoint *endpoint,
			       struct htc_packet *packet)
{
	packet->completion = NULL;
	packet->buf += HTC_HDR_LENGTH;

	if (!packet->status)
		return;

	ath6kl_err("req failed (status:%d, ep:%d, len:%d creds:%d)\n",
		   packet->status, packet->endpoint, packet->act_len,
		   packet->info.tx.cred_used);

	/* on failure to submit, reclaim credits for this packet */
	spin_lock_bh(&target->tx_lock);
	endpoint->cred_dist.cred_to_dist +=
				packet->info.tx.cred_used;
	endpoint->cred_dist.txq_depth = get_queue_depth(&endpoint->txq);

	ath6kl_dbg(ATH6KL_DBG_HTC, "htc tx ctxt 0x%p dist 0x%p\n",
		   target->credit_info, &target->cred_dist_list);

	ath6kl_credit_distribute(target->credit_info,
				 &target->cred_dist_list,
				 HTC_CREDIT_DIST_SEND_COMPLETE);

	spin_unlock_bh(&target->tx_lock);
}

static void htc_tx_complete(struct htc_endpoint *endpoint,
			    struct list_head *txq)
{
	if (list_empty(txq))
		return;

	ath6kl_dbg(ATH6KL_DBG_HTC,
		   "htc tx complete ep %d pkts %d\n",
		   endpoint->eid, get_queue_depth(txq));

	ath6kl_tx_complete(endpoint->target, txq);
}

static void htc_tx_comp_handler(struct htc_target *target,
				struct htc_packet *packet)
{
	struct htc_endpoint *endpoint = &target->endpoint[packet->endpoint];
	struct list_head container;

	ath6kl_dbg(ATH6KL_DBG_HTC, "htc tx complete seqno %d\n",
		   packet->info.tx.seqno);

	htc_tx_comp_update(target, endpoint, packet);
	INIT_LIST_HEAD(&container);
	list_add_tail(&packet->list, &container);
	/* do completion */
	htc_tx_complete(endpoint, &container);
}

static void htc_async_tx_scat_complete(struct htc_target *target,
				       struct hif_scatter_req *scat_req)
{
	struct htc_endpoint *endpoint;
	struct htc_packet *packet;
	struct list_head tx_compq;
	int i;

	INIT_LIST_HEAD(&tx_compq);

	ath6kl_dbg(ATH6KL_DBG_HTC,
		   "htc tx scat complete len %d entries %d\n",
		   scat_req->len, scat_req->scat_entries);

	if (scat_req->status)
		ath6kl_err("send scatter req failed: %d\n", scat_req->status);

	packet = scat_req->scat_list[0].packet;
	endpoint = &target->endpoint[packet->endpoint];

	/* walk through the scatter list and process */
	for (i = 0; i < scat_req->scat_entries; i++) {
		packet = scat_req->scat_list[i].packet;
		if (!packet) {
			WARN_ON(1);
			return;
		}

		packet->status = scat_req->status;
		htc_tx_comp_update(target, endpoint, packet);
		list_add_tail(&packet->list, &tx_compq);
	}

	/* free scatter request */
	hif_scatter_req_add(target->dev->ar, scat_req);

	/* complete all packets */
	htc_tx_complete(endpoint, &tx_compq);
}

static int ath6kl_htc_tx_issue(struct htc_target *target,
			       struct htc_packet *packet)
{
	int status;
	bool sync = false;
	u32 padded_len, send_len;

	if (!packet->completion)
		sync = true;

	send_len = packet->act_len + HTC_HDR_LENGTH;

	padded_len = CALC_TXRX_PADDED_LEN(target, send_len);

	ath6kl_dbg(ATH6KL_DBG_HTC,
		   "htc tx issue len %d seqno %d padded_len %d mbox 0x%X %s\n",
		   send_len, packet->info.tx.seqno, padded_len,
		   target->dev->ar->mbox_info.htc_addr,
		   sync ? "sync" : "async");

	if (sync) {
		status = hif_read_write_sync(target->dev->ar,
				target->dev->ar->mbox_info.htc_addr,
				 packet->buf, padded_len,
				 HIF_WR_SYNC_BLOCK_INC);

		packet->status = status;
		packet->buf += HTC_HDR_LENGTH;
	} else
		status = hif_write_async(target->dev->ar,
				target->dev->ar->mbox_info.htc_addr,
				packet->buf, padded_len,
				HIF_WR_ASYNC_BLOCK_INC, packet);

	trace_ath6kl_htc_tx(status, packet->endpoint, packet->buf, send_len);

	return status;
}

static int htc_check_credits(struct htc_target *target,
			     struct htc_endpoint *ep, u8 *flags,
			     enum htc_endpoint_id eid, unsigned int len,
			     int *req_cred)
{
	*req_cred = (len > target->tgt_cred_sz) ?
		     DIV_ROUND_UP(len, target->tgt_cred_sz) : 1;

	ath6kl_dbg(ATH6KL_DBG_CREDIT, "credit check need %d got %d\n",
		   *req_cred, ep->cred_dist.credits);

	if (ep->cred_dist.credits < *req_cred) {
		if (eid == ENDPOINT_0)
			return -EINVAL;

		/* Seek more credits */
		ep->cred_dist.seek_cred = *req_cred - ep->cred_dist.credits;

		ath6kl_credit_seek(target->credit_info, &ep->cred_dist);

		ep->cred_dist.seek_cred = 0;

		if (ep->cred_dist.credits < *req_cred) {
			ath6kl_dbg(ATH6KL_DBG_CREDIT,
				   "credit not found for ep %d\n",
				   eid);
			return -EINVAL;
		}
	}

	ep->cred_dist.credits -= *req_cred;
	ep->ep_st.cred_cosumd += *req_cred;

	 /* When we are getting low on credits, ask for more */
	if (ep->cred_dist.credits < ep->cred_dist.cred_per_msg) {
		ep->cred_dist.seek_cred =
		ep->cred_dist.cred_per_msg - ep->cred_dist.credits;

		ath6kl_credit_seek(target->credit_info, &ep->cred_dist);

		/* see if we were successful in getting more */
		if (ep->cred_dist.credits < ep->cred_dist.cred_per_msg) {
			/* tell the target we need credits ASAP! */
			*flags |= HTC_FLAGS_NEED_CREDIT_UPDATE;
			ep->ep_st.cred_low_indicate += 1;
			ath6kl_dbg(ATH6KL_DBG_CREDIT,
				   "credit we need credits asap\n");
		}
	}

	return 0;
}

static void ath6kl_htc_tx_pkts_get(struct htc_target *target,
				   struct htc_endpoint *endpoint,
				   struct list_head *queue)
{
	int req_cred;
	u8 flags;
	struct htc_packet *packet;
	unsigned int len;

	while (true) {
		flags = 0;

		if (list_empty(&endpoint->txq))
			break;
		packet = list_first_entry(&endpoint->txq, struct htc_packet,
					  list);

		ath6kl_dbg(ATH6KL_DBG_HTC,
			   "htc tx got packet 0x%p queue depth %d\n",
			   packet, get_queue_depth(&endpoint->txq));

		len = CALC_TXRX_PADDED_LEN(target,
					   packet->act_len + HTC_HDR_LENGTH);

		if (htc_check_credits(target, endpoint, &flags,
				      packet->endpoint, len, &req_cred))
			break;

		/* now we can fully move onto caller's queue */
		packet = list_first_entry(&endpoint->txq, struct htc_packet,
					  list);
		list_move_tail(&packet->list, queue);

		/* save the number of credits this packet consumed */
		packet->info.tx.cred_used = req_cred;

		/* all TX packets are handled asynchronously */
		packet->completion = htc_tx_comp_handler;
		packet->context = target;
		endpoint->ep_st.tx_issued += 1;

		/* save send flags */
		packet->info.tx.flags = flags;
		packet->info.tx.seqno = endpoint->seqno;
		endpoint->seqno++;
	}
}

/* See if the padded tx length falls on a credit boundary */
static int htc_get_credit_padding(unsigned int cred_sz, int *len,
				  struct htc_endpoint *ep)
{
	int rem_cred, cred_pad;

	rem_cred = *len % cred_sz;

	/* No padding needed */
	if  (!rem_cred)
		return 0;

	if (!(ep->conn_flags & HTC_FLGS_TX_BNDL_PAD_EN))
		return -1;

	/*
	 * The transfer consumes a "partial" credit, this
	 * packet cannot be bundled unless we add
	 * additional "dummy" padding (max 255 bytes) to
	 * consume the entire credit.
	 */
	cred_pad = *len < cred_sz ? (cred_sz - *len) : rem_cred;

	if ((cred_pad > 0) && (cred_pad <= 255))
		*len += cred_pad;
	else
		/* The amount of padding is too large, send as non-bundled */
		return -1;

	return cred_pad;
}

static int ath6kl_htc_tx_setup_scat_list(struct htc_target *target,
					 struct htc_endpoint *endpoint,
					 struct hif_scatter_req *scat_req,
					 int n_scat,
					 struct list_head *queue)
{
	struct htc_packet *packet;
	int i, len, rem_scat, cred_pad;
	int status = 0;
	u8 flags;

	rem_scat = target->max_tx_bndl_sz;

	for (i = 0; i < n_scat; i++) {
		scat_req->scat_list[i].packet = NULL;

		if (list_empty(queue))
			break;

		packet = list_first_entry(queue, struct htc_packet, list);
		len = CALC_TXRX_PADDED_LEN(target,
					   packet->act_len + HTC_HDR_LENGTH);

		cred_pad = htc_get_credit_padding(target->tgt_cred_sz,
						  &len, endpoint);
		if (cred_pad < 0 || rem_scat < len) {
			status = -ENOSPC;
			break;
		}

		rem_scat -= len;
		/* now remove it from the queue */
		list_del(&packet->list);

		scat_req->scat_list[i].packet = packet;
		/* prepare packet and flag message as part of a send bundle */
		flags = packet->info.tx.flags | HTC_FLAGS_SEND_BUNDLE;
		ath6kl_htc_tx_prep_pkt(packet, flags,
				       cred_pad, packet->info.tx.seqno);
		/* Make sure the buffer is 4-byte aligned */
		ath6kl_htc_tx_buf_align(&packet->buf,
					packet->act_len + HTC_HDR_LENGTH);
		scat_req->scat_list[i].buf = packet->buf;
		scat_req->scat_list[i].len = len;

		scat_req->len += len;
		scat_req->scat_entries++;
		ath6kl_dbg(ATH6KL_DBG_HTC,
			   "htc tx adding (%d) pkt 0x%p seqno %d len %d remaining %d\n",
			   i, packet, packet->info.tx.seqno, len, rem_scat);
	}

	/* Roll back scatter setup in case of any failure */
	if (scat_req->scat_entries < HTC_MIN_HTC_MSGS_TO_BUNDLE) {
		for (i = scat_req->scat_entries - 1; i >= 0; i--) {
			packet = scat_req->scat_list[i].packet;
			if (packet) {
				packet->buf += HTC_HDR_LENGTH;
				list_add(&packet->list, queue);
			}
		}
		return -EAGAIN;
	}

	return status;
}

/*
 * Drain a queue and send as bundles this function may return without fully
 * draining the queue when
 *
 *    1. scatter resources are exhausted
 *    2. a message that will consume a partial credit will stop the
 *    bundling process early
 *    3. we drop below the minimum number of messages for a bundle
 */
static void ath6kl_htc_tx_bundle(struct htc_endpoint *endpoint,
				 struct list_head *queue,
				 int *sent_bundle, int *n_bundle_pkts)
{
	struct htc_target *target = endpoint->target;
	struct hif_scatter_req *scat_req = NULL;
	int n_scat, n_sent_bundle = 0, tot_pkts_bundle = 0, i;
	struct htc_packet *packet;
	int status;
	u32 txb_mask;
	u8 ac = WMM_NUM_AC;

	if ((HTC_CTRL_RSVD_SVC != endpoint->svc_id) &&
	    (WMI_CONTROL_SVC != endpoint->svc_id))
		ac = target->dev->ar->ep2ac_map[endpoint->eid];

	while (true) {
		status = 0;
		n_scat = get_queue_depth(queue);
		n_scat = min(n_scat, target->msg_per_bndl_max);

		if (n_scat < HTC_MIN_HTC_MSGS_TO_BUNDLE)
			/* not enough to bundle */
			break;

		scat_req = hif_scatter_req_get(target->dev->ar);

		if (!scat_req) {
			/* no scatter resources  */
			ath6kl_dbg(ATH6KL_DBG_HTC,
				   "htc tx no more scatter resources\n");
			break;
		}

		if ((ac < WMM_NUM_AC) && (ac != WMM_AC_BK)) {
			if (WMM_AC_BE == ac)
				/*
				 * BE, BK have priorities and bit
				 * positions reversed
				 */
				txb_mask = (1 << WMM_AC_BK);
			else
				/*
				 * any AC with priority lower than
				 * itself
				 */
				txb_mask = ((1 << ac) - 1);

			/*
			 * when the scatter request resources drop below a
			 * certain threshold, disable Tx bundling for all
			 * AC's with priority lower than the current requesting
			 * AC. Otherwise re-enable Tx bundling for them
			 */
			if (scat_req->scat_q_depth < ATH6KL_SCATTER_REQS)
				target->tx_bndl_mask &= ~txb_mask;
			else
				target->tx_bndl_mask |= txb_mask;
		}

		ath6kl_dbg(ATH6KL_DBG_HTC, "htc tx pkts to scatter: %d\n",
			   n_scat);

		scat_req->len = 0;
		scat_req->scat_entries = 0;

		status = ath6kl_htc_tx_setup_scat_list(target, endpoint,
						       scat_req, n_scat,
						       queue);
		if (status == -EAGAIN) {
			hif_scatter_req_add(target->dev->ar, scat_req);
			break;
		}

		/* send path is always asynchronous */
		scat_req->complete = htc_async_tx_scat_complete;
		n_sent_bundle++;
		tot_pkts_bundle += scat_req->scat_entries;

		ath6kl_dbg(ATH6KL_DBG_HTC,
			   "htc tx scatter bytes %d entries %d\n",
			   scat_req->len, scat_req->scat_entries);

		for (i = 0; i < scat_req->scat_entries; i++) {
			packet = scat_req->scat_list[i].packet;
			trace_ath6kl_htc_tx(packet->status, packet->endpoint,
					    packet->buf, packet->act_len);
		}

		ath6kl_hif_submit_scat_req(target->dev, scat_req, false);

		if (status)
			break;
	}

	*sent_bundle = n_sent_bundle;
	*n_bundle_pkts = tot_pkts_bundle;
	ath6kl_dbg(ATH6KL_DBG_HTC, "htc tx bundle sent %d pkts\n",
		   n_sent_bundle);

	return;
}

static void ath6kl_htc_tx_from_queue(struct htc_target *target,
				     struct htc_endpoint *endpoint)
{
	struct list_head txq;
	struct htc_packet *packet;
	int bundle_sent;
	int n_pkts_bundle;
	u8 ac = WMM_NUM_AC;
	int status;

	spin_lock_bh(&target->tx_lock);

	endpoint->tx_proc_cnt++;
	if (endpoint->tx_proc_cnt > 1) {
		endpoint->tx_proc_cnt--;
		spin_unlock_bh(&target->tx_lock);
		ath6kl_dbg(ATH6KL_DBG_HTC, "htc tx busy\n");
		return;
	}

	/*
	 * drain the endpoint TX queue for transmission as long
	 * as we have enough credits.
	 */
	INIT_LIST_HEAD(&txq);

	if ((HTC_CTRL_RSVD_SVC != endpoint->svc_id) &&
	    (WMI_CONTROL_SVC != endpoint->svc_id))
		ac = target->dev->ar->ep2ac_map[endpoint->eid];

	while (true) {
		if (list_empty(&endpoint->txq))
			break;

		ath6kl_htc_tx_pkts_get(target, endpoint, &txq);

		if (list_empty(&txq))
			break;

		spin_unlock_bh(&target->tx_lock);

		bundle_sent = 0;
		n_pkts_bundle = 0;

		while (true) {
			/* try to send a bundle on each pass */
			if ((target->tx_bndl_mask) &&
			    (get_queue_depth(&txq) >=
			    HTC_MIN_HTC_MSGS_TO_BUNDLE)) {
				int temp1 = 0, temp2 = 0;

				/* check if bundling is enabled for an AC */
				if (target->tx_bndl_mask & (1 << ac)) {
					ath6kl_htc_tx_bundle(endpoint, &txq,
							     &temp1, &temp2);
					bundle_sent += temp1;
					n_pkts_bundle += temp2;
				}
			}

			if (list_empty(&txq))
				break;

			packet = list_first_entry(&txq, struct htc_packet,
						  list);
			list_del(&packet->list);

			ath6kl_htc_tx_prep_pkt(packet, packet->info.tx.flags,
					       0, packet->info.tx.seqno);
			status = ath6kl_htc_tx_issue(target, packet);

			if (status) {
				packet->status = status;
				packet->completion(packet->context, packet);
			}
		}

		spin_lock_bh(&target->tx_lock);

		endpoint->ep_st.tx_bundles += bundle_sent;
		endpoint->ep_st.tx_pkt_bundled += n_pkts_bundle;

		/*
		 * if an AC has bundling disabled and no tx bundling
		 * has occured continously for a certain number of TX,
		 * enable tx bundling for this AC
		 */
		if (!bundle_sent) {
			if (!(target->tx_bndl_mask & (1 << ac)) &&
			    (ac < WMM_NUM_AC)) {
				if (++target->ac_tx_count[ac] >=
					TX_RESUME_BUNDLE_THRESHOLD) {
					target->ac_tx_count[ac] = 0;
					target->tx_bndl_mask |= (1 << ac);
				}
			}
		} else {
			/* tx bundling will reset the counter */
			if (ac < WMM_NUM_AC)
				target->ac_tx_count[ac] = 0;
		}
	}

	endpoint->tx_proc_cnt = 0;
	spin_unlock_bh(&target->tx_lock);
}

static bool ath6kl_htc_tx_try(struct htc_target *target,
			      struct htc_endpoint *endpoint,
			      struct htc_packet *tx_pkt)
{
	struct htc_ep_callbacks ep_cb;
	int txq_depth;
	bool overflow = false;

	ep_cb = endpoint->ep_cb;

	spin_lock_bh(&target->tx_lock);
	txq_depth = get_queue_depth(&endpoint->txq);
	spin_unlock_bh(&target->tx_lock);

	if (txq_depth >= endpoint->max_txq_depth)
		overflow = true;

	if (overflow)
		ath6kl_dbg(ATH6KL_DBG_HTC,
			   "htc tx overflow ep %d depth %d max %d\n",
			   endpoint->eid, txq_depth,
			   endpoint->max_txq_depth);

	if (overflow && ep_cb.tx_full) {
		if (ep_cb.tx_full(endpoint->target, tx_pkt) ==
		    HTC_SEND_FULL_DROP) {
			endpoint->ep_st.tx_dropped += 1;
			return false;
		}
	}

	spin_lock_bh(&target->tx_lock);
	list_add_tail(&tx_pkt->list, &endpoint->txq);
	spin_unlock_bh(&target->tx_lock);

	ath6kl_htc_tx_from_queue(target, endpoint);

	return true;
}

static void htc_chk_ep_txq(struct htc_target *target)
{
	struct htc_endpoint *endpoint;
	struct htc_endpoint_credit_dist *cred_dist;

	/*
	 * Run through the credit distribution list to see if there are
	 * packets queued. NOTE: no locks need to be taken since the
	 * distribution list is not dynamic (cannot be re-ordered) and we
	 * are not modifying any state.
	 */
	list_for_each_entry(cred_dist, &target->cred_dist_list, list) {
		endpoint = cred_dist->htc_ep;

		spin_lock_bh(&target->tx_lock);
		if (!list_empty(&endpoint->txq)) {
			ath6kl_dbg(ATH6KL_DBG_HTC,
				   "htc creds ep %d credits %d pkts %d\n",
				   cred_dist->endpoint,
				   endpoint->cred_dist.credits,
				   get_queue_depth(&endpoint->txq));
			spin_unlock_bh(&target->tx_lock);
			/*
			 * Try to start the stalled queue, this list is
			 * ordered by priority. If there are credits
			 * available the highest priority queue will get a
			 * chance to reclaim credits from lower priority
			 * ones.
			 */
			ath6kl_htc_tx_from_queue(target, endpoint);
			spin_lock_bh(&target->tx_lock);
		}
		spin_unlock_bh(&target->tx_lock);
	}
}

static int htc_setup_tx_complete(struct htc_target *target)
{
	struct htc_packet *send_pkt = NULL;
	int status;

	send_pkt = htc_get_control_buf(target, true);

	if (!send_pkt)
		return -ENOMEM;

	if (target->htc_tgt_ver >= HTC_VERSION_2P1) {
		struct htc_setup_comp_ext_msg *setup_comp_ext;
		u32 flags = 0;

		setup_comp_ext =
		    (struct htc_setup_comp_ext_msg *)send_pkt->buf;
		memset(setup_comp_ext, 0, sizeof(*setup_comp_ext));
		setup_comp_ext->msg_id =
			cpu_to_le16(HTC_MSG_SETUP_COMPLETE_EX_ID);

		if (target->msg_per_bndl_max > 0) {
			/* Indicate HTC bundling to the target */
			flags |= HTC_SETUP_COMP_FLG_RX_BNDL_EN;
			setup_comp_ext->msg_per_rxbndl =
						target->msg_per_bndl_max;
		}

		memcpy(&setup_comp_ext->flags, &flags,
		       sizeof(setup_comp_ext->flags));
		set_htc_pkt_info(send_pkt, NULL, (u8 *) setup_comp_ext,
				 sizeof(struct htc_setup_comp_ext_msg),
				 ENDPOINT_0, HTC_SERVICE_TX_PACKET_TAG);

	} else {
		struct htc_setup_comp_msg *setup_comp;
		setup_comp = (struct htc_setup_comp_msg *)send_pkt->buf;
		memset(setup_comp, 0, sizeof(struct htc_setup_comp_msg));
		setup_comp->msg_id = cpu_to_le16(HTC_MSG_SETUP_COMPLETE_ID);
		set_htc_pkt_info(send_pkt, NULL, (u8 *) setup_comp,
				 sizeof(struct htc_setup_comp_msg),
				 ENDPOINT_0, HTC_SERVICE_TX_PACKET_TAG);
	}

	/* we want synchronous operation */
	send_pkt->completion = NULL;
	ath6kl_htc_tx_prep_pkt(send_pkt, 0, 0, 0);
	status = ath6kl_htc_tx_issue(target, send_pkt);
	htc_reclaim_txctrl_buf(target, send_pkt);

	return status;
}

static void ath6kl_htc_set_credit_dist(struct htc_target *target,
				struct ath6kl_htc_credit_info *credit_info,
				u16 srvc_pri_order[], int list_len)
{
	struct htc_endpoint *endpoint;
	int i, ep;

	target->credit_info = credit_info;

	list_add_tail(&target->endpoint[ENDPOINT_0].cred_dist.list,
		      &target->cred_dist_list);

	for (i = 0; i < list_len; i++) {
		for (ep = ENDPOINT_1; ep < ENDPOINT_MAX; ep++) {
			endpoint = &target->endpoint[ep];
			if (endpoint->svc_id == srvc_pri_order[i]) {
				list_add_tail(&endpoint->cred_dist.list,
					      &target->cred_dist_list);
				break;
			}
		}
		if (ep >= ENDPOINT_MAX) {
			WARN_ON(1);
			return;
		}
	}
}

static int ath6kl_htc_mbox_tx(struct htc_target *target,
			      struct htc_packet *packet)
{
	struct htc_endpoint *endpoint;
	struct list_head queue;

	ath6kl_dbg(ATH6KL_DBG_HTC,
		   "htc tx ep id %d buf 0x%p len %d\n",
		   packet->endpoint, packet->buf, packet->act_len);

	if (packet->endpoint >= ENDPOINT_MAX) {
		WARN_ON(1);
		return -EINVAL;
	}

	endpoint = &target->endpoint[packet->endpoint];

	if (!ath6kl_htc_tx_try(target, endpoint, packet)) {
		packet->status = (target->htc_flags & HTC_OP_STATE_STOPPING) ?
				 -ECANCELED : -ENOSPC;
		INIT_LIST_HEAD(&queue);
		list_add(&packet->list, &queue);
		htc_tx_complete(endpoint, &queue);
	}

	return 0;
}

/* flush endpoint TX queue */
static void ath6kl_htc_mbox_flush_txep(struct htc_target *target,
			   enum htc_endpoint_id eid, u16 tag)
{
	struct htc_packet *packet, *tmp_pkt;
	struct list_head discard_q, container;
	struct htc_endpoint *endpoint = &target->endpoint[eid];

	if (!endpoint->svc_id) {
		WARN_ON(1);
		return;
	}

	/* initialize the discard queue */
	INIT_LIST_HEAD(&discard_q);

	spin_lock_bh(&target->tx_lock);

	list_for_each_entry_safe(packet, tmp_pkt, &endpoint->txq, list) {
		if ((tag == HTC_TX_PACKET_TAG_ALL) ||
		    (tag == packet->info.tx.tag))
			list_move_tail(&packet->list, &discard_q);
	}

	spin_unlock_bh(&target->tx_lock);

	list_for_each_entry_safe(packet, tmp_pkt, &discard_q, list) {
		packet->status = -ECANCELED;
		list_del(&packet->list);
		ath6kl_dbg(ATH6KL_DBG_HTC,
			   "htc tx flushing pkt 0x%p len %d  ep %d tag 0x%x\n",
			   packet, packet->act_len,
			   packet->endpoint, packet->info.tx.tag);

		INIT_LIST_HEAD(&container);
		list_add_tail(&packet->list, &container);
		htc_tx_complete(endpoint, &container);
	}
}

static void ath6kl_htc_flush_txep_all(struct htc_target *target)
{
	struct htc_endpoint *endpoint;
	int i;

	dump_cred_dist_stats(target);

	for (i = ENDPOINT_0; i < ENDPOINT_MAX; i++) {
		endpoint = &target->endpoint[i];
		if (endpoint->svc_id == 0)
			/* not in use.. */
			continue;
		ath6kl_htc_mbox_flush_txep(target, i, HTC_TX_PACKET_TAG_ALL);
	}
}

static void ath6kl_htc_mbox_activity_changed(struct htc_target *target,
					     enum htc_endpoint_id eid,
					     bool active)
{
	struct htc_endpoint *endpoint = &target->endpoint[eid];
	bool dist = false;

	if (endpoint->svc_id == 0) {
		WARN_ON(1);
		return;
	}

	spin_lock_bh(&target->tx_lock);

	if (active) {
		if (!(endpoint->cred_dist.dist_flags & HTC_EP_ACTIVE)) {
			endpoint->cred_dist.dist_flags |= HTC_EP_ACTIVE;
			dist = true;
		}
	} else {
		if (endpoint->cred_dist.dist_flags & HTC_EP_ACTIVE) {
			endpoint->cred_dist.dist_flags &= ~HTC_EP_ACTIVE;
			dist = true;
		}
	}

	if (dist) {
		endpoint->cred_dist.txq_depth =
			get_queue_depth(&endpoint->txq);

		ath6kl_dbg(ATH6KL_DBG_HTC,
			   "htc tx activity ctxt 0x%p dist 0x%p\n",
			   target->credit_info, &target->cred_dist_list);

		ath6kl_credit_distribute(target->credit_info,
					 &target->cred_dist_list,
					 HTC_CREDIT_DIST_ACTIVITY_CHANGE);
	}

	spin_unlock_bh(&target->tx_lock);

	if (dist && !active)
		htc_chk_ep_txq(target);
}

/* HTC Rx */

static inline void ath6kl_htc_rx_update_stats(struct htc_endpoint *endpoint,
					      int n_look_ahds)
{
	endpoint->ep_st.rx_pkts++;
	if (n_look_ahds == 1)
		endpoint->ep_st.rx_lkahds++;
	else if (n_look_ahds > 1)
		endpoint->ep_st.rx_bundle_lkahd++;
}

static inline bool htc_valid_rx_frame_len(struct htc_target *target,
					  enum htc_endpoint_id eid, int len)
{
	return (eid == target->dev->ar->ctrl_ep) ?
		len <= ATH6KL_BUFFER_SIZE : len <= ATH6KL_AMSDU_BUFFER_SIZE;
}

static int htc_add_rxbuf(struct htc_target *target, struct htc_packet *packet)
{
	struct list_head queue;

	INIT_LIST_HEAD(&queue);
	list_add_tail(&packet->list, &queue);
	return ath6kl_htc_mbox_add_rxbuf_multiple(target, &queue);
}

static void htc_reclaim_rxbuf(struct htc_target *target,
			      struct htc_packet *packet,
			      struct htc_endpoint *ep)
{
	if (packet->info.rx.rx_flags & HTC_RX_PKT_NO_RECYCLE) {
		htc_rxpkt_reset(packet);
		packet->status = -ECANCELED;
		ep->ep_cb.rx(ep->target, packet);
	} else {
		htc_rxpkt_reset(packet);
		htc_add_rxbuf((void *)(target), packet);
	}
}

static void reclaim_rx_ctrl_buf(struct htc_target *target,
				struct htc_packet *packet)
{
	spin_lock_bh(&target->htc_lock);
	list_add_tail(&packet->list, &target->free_ctrl_rxbuf);
	spin_unlock_bh(&target->htc_lock);
}

static int ath6kl_htc_rx_packet(struct htc_target *target,
				struct htc_packet *packet,
				u32 rx_len)
{
	struct ath6kl_device *dev = target->dev;
	u32 padded_len;
	int status;

	padded_len = CALC_TXRX_PADDED_LEN(target, rx_len);

	if (padded_len > packet->buf_len) {
		ath6kl_err("not enough receive space for packet - padlen %d recvlen %d bufferlen %d\n",
			   padded_len, rx_len, packet->buf_len);
		return -ENOMEM;
	}

	ath6kl_dbg(ATH6KL_DBG_HTC,
		   "htc rx 0x%p hdr 0x%x len %d mbox 0x%x\n",
		   packet, packet->info.rx.exp_hdr,
		   padded_len, dev->ar->mbox_info.htc_addr);

	status = hif_read_write_sync(dev->ar,
				     dev->ar->mbox_info.htc_addr,
				     packet->buf, padded_len,
				     HIF_RD_SYNC_BLOCK_FIX);

	packet->status = status;

	return status;
}

/*
 * optimization for recv packets, we can indicate a
 * "hint" that there are more  single-packets to fetch
 * on this endpoint.
 */
static void ath6kl_htc_rx_set_indicate(u32 lk_ahd,
				       struct htc_endpoint *endpoint,
				       struct htc_packet *packet)
{
	struct htc_frame_hdr *htc_hdr = (struct htc_frame_hdr *)&lk_ahd;

	if (htc_hdr->eid == packet->endpoint) {
		if (!list_empty(&endpoint->rx_bufq))
			packet->info.rx.indicat_flags |=
					HTC_RX_FLAGS_INDICATE_MORE_PKTS;
	}
}

static void ath6kl_htc_rx_chk_water_mark(struct htc_endpoint *endpoint)
{
	struct htc_ep_callbacks ep_cb = endpoint->ep_cb;

	if (ep_cb.rx_refill_thresh > 0) {
		spin_lock_bh(&endpoint->target->rx_lock);
		if (get_queue_depth(&endpoint->rx_bufq)
		    < ep_cb.rx_refill_thresh) {
			spin_unlock_bh(&endpoint->target->rx_lock);
			ep_cb.rx_refill(endpoint->target, endpoint->eid);
			return;
		}
		spin_unlock_bh(&endpoint->target->rx_lock);
	}
}

/* This function is called with rx_lock held */
static int ath6kl_htc_rx_setup(struct htc_target *target,
			       struct htc_endpoint *ep,
			       u32 *lk_ahds, struct list_head *queue, int n_msg)
{
	struct htc_packet *packet;
	/* FIXME: type of lk_ahds can't be right */
	struct htc_frame_hdr *htc_hdr = (struct htc_frame_hdr *)lk_ahds;
	struct htc_ep_callbacks ep_cb;
	int status = 0, j, full_len;
	bool no_recycle;

	full_len = CALC_TXRX_PADDED_LEN(target,
					le16_to_cpu(htc_hdr->payld_len) +
					sizeof(*htc_hdr));

	if (!htc_valid_rx_frame_len(target, ep->eid, full_len)) {
		ath6kl_warn("Rx buffer requested with invalid length htc_hdr:eid %d, flags 0x%x, len %d\n",
			    htc_hdr->eid, htc_hdr->flags,
			    le16_to_cpu(htc_hdr->payld_len));
		return -EINVAL;
	}

	ep_cb = ep->ep_cb;
	for (j = 0; j < n_msg; j++) {
		/*
		 * Reset flag, any packets allocated using the
		 * rx_alloc() API cannot be recycled on
		 * cleanup,they must be explicitly returned.
		 */
		no_recycle = false;

		if (ep_cb.rx_allocthresh &&
		    (full_len > ep_cb.rx_alloc_thresh)) {
			ep->ep_st.rx_alloc_thresh_hit += 1;
			ep->ep_st.rxalloc_thresh_byte +=
				le16_to_cpu(htc_hdr->payld_len);

			spin_unlock_bh(&target->rx_lock);
			no_recycle = true;

			packet = ep_cb.rx_allocthresh(ep->target, ep->eid,
						      full_len);
			spin_lock_bh(&target->rx_lock);
		} else {
			/* refill handler is being used */
			if (list_empty(&ep->rx_bufq)) {
				if (ep_cb.rx_refill) {
					spin_unlock_bh(&target->rx_lock);
					ep_cb.rx_refill(ep->target, ep->eid);
					spin_lock_bh(&target->rx_lock);
				}
			}

			if (list_empty(&ep->rx_bufq)) {
				packet = NULL;
			} else {
				packet = list_first_entry(&ep->rx_bufq,
						struct htc_packet, list);
				list_del(&packet->list);
			}
		}

		if (!packet) {
			target->rx_st_flags |= HTC_RECV_WAIT_BUFFERS;
			target->ep_waiting = ep->eid;
			return -ENOSPC;
		}

		/* clear flags */
		packet->info.rx.rx_flags = 0;
		packet->info.rx.indicat_flags = 0;
		packet->status = 0;

		if (no_recycle)
			/*
			 * flag that these packets cannot be
			 * recycled, they have to be returned to
			 * the user
			 */
			packet->info.rx.rx_flags |= HTC_RX_PKT_NO_RECYCLE;

		/* Caller needs to free this upon any failure */
		list_add_tail(&packet->list, queue);

		if (target->htc_flags & HTC_OP_STATE_STOPPING) {
			status = -ECANCELED;
			break;
		}

		if (j) {
			packet->info.rx.rx_flags |= HTC_RX_PKT_REFRESH_HDR;
			packet->info.rx.exp_hdr = 0xFFFFFFFF;
		} else
			/* set expected look ahead */
			packet->info.rx.exp_hdr = *lk_ahds;

		packet->act_len = le16_to_cpu(htc_hdr->payld_len) +
			HTC_HDR_LENGTH;
	}

	return status;
}

static int ath6kl_htc_rx_alloc(struct htc_target *target,
			       u32 lk_ahds[], int msg,
			       struct htc_endpoint *endpoint,
			       struct list_head *queue)
{
	int status = 0;
	struct htc_packet *packet, *tmp_pkt;
	struct htc_frame_hdr *htc_hdr;
	int i, n_msg;

	spin_lock_bh(&target->rx_lock);

	for (i = 0; i < msg; i++) {
		htc_hdr = (struct htc_frame_hdr *)&lk_ahds[i];

		if (htc_hdr->eid >= ENDPOINT_MAX) {
			ath6kl_err("invalid ep in look-ahead: %d\n",
				   htc_hdr->eid);
			status = -ENOMEM;
			break;
		}

		if (htc_hdr->eid != endpoint->eid) {
			ath6kl_err("invalid ep in look-ahead: %d should be : %d (index:%d)\n",
				   htc_hdr->eid, endpoint->eid, i);
			status = -ENOMEM;
			break;
		}

		if (le16_to_cpu(htc_hdr->payld_len) > HTC_MAX_PAYLOAD_LENGTH) {
			ath6kl_err("payload len %d exceeds max htc : %d !\n",
				   htc_hdr->payld_len,
				   (u32) HTC_MAX_PAYLOAD_LENGTH);
			status = -ENOMEM;
			break;
		}

		if (endpoint->svc_id == 0) {
			ath6kl_err("ep %d is not connected !\n", htc_hdr->eid);
			status = -ENOMEM;
			break;
		}

		if (htc_hdr->flags & HTC_FLG_RX_BNDL_CNT) {
			/*
			 * HTC header indicates that every packet to follow
			 * has the same padded length so that it can be
			 * optimally fetched as a full bundle.
			 */
			n_msg = (htc_hdr->flags & HTC_FLG_RX_BNDL_CNT) >>
				HTC_FLG_RX_BNDL_CNT_S;

			/* the count doesn't include the starter frame */
			n_msg++;
			if (n_msg > target->msg_per_bndl_max) {
				status = -ENOMEM;
				break;
			}

			endpoint->ep_st.rx_bundle_from_hdr += 1;
			ath6kl_dbg(ATH6KL_DBG_HTC,
				   "htc rx bundle pkts %d\n",
				   n_msg);
		} else
			/* HTC header only indicates 1 message to fetch */
			n_msg = 1;

		/* Setup packet buffers for each message */
		status = ath6kl_htc_rx_setup(target, endpoint, &lk_ahds[i],
					     queue, n_msg);

		/*
		 * This is due to unavailabilty of buffers to rx entire data.
		 * Return no error so that free buffers from queue can be used
		 * to receive partial data.
		 */
		if (status == -ENOSPC) {
			spin_unlock_bh(&target->rx_lock);
			return 0;
		}

		if (status)
			break;
	}

	spin_unlock_bh(&target->rx_lock);

	if (status) {
		list_for_each_entry_safe(packet, tmp_pkt, queue, list) {
			list_del(&packet->list);
			htc_reclaim_rxbuf(target, packet,
					  &target->endpoint[packet->endpoint]);
		}
	}

	return status;
}

static void htc_ctrl_rx(struct htc_target *context, struct htc_packet *packets)
{
	if (packets->endpoint != ENDPOINT_0) {
		WARN_ON(1);
		return;
	}

	if (packets->status == -ECANCELED) {
		reclaim_rx_ctrl_buf(context, packets);
		return;
	}

	if (packets->act_len > 0) {
		ath6kl_err("htc_ctrl_rx, got message with len:%zu\n",
			   packets->act_len + HTC_HDR_LENGTH);

		ath6kl_dbg_dump(ATH6KL_DBG_HTC,
				"htc rx unexpected endpoint 0 message", "",
				packets->buf - HTC_HDR_LENGTH,
				packets->act_len + HTC_HDR_LENGTH);
	}

	htc_reclaim_rxbuf(context, packets, &context->endpoint[0]);
}

static void htc_proc_cred_rpt(struct htc_target *target,
			      struct htc_credit_report *rpt,
			      int n_entries,
			      enum htc_endpoint_id from_ep)
{
	struct htc_endpoint *endpoint;
	int tot_credits = 0, i;
	bool dist = false;

	spin_lock_bh(&target->tx_lock);

	for (i = 0; i < n_entries; i++, rpt++) {
		if (rpt->eid >= ENDPOINT_MAX) {
			WARN_ON(1);
			spin_unlock_bh(&target->tx_lock);
			return;
		}

		endpoint = &target->endpoint[rpt->eid];

		ath6kl_dbg(ATH6KL_DBG_CREDIT,
			   "credit report ep %d credits %d\n",
			   rpt->eid, rpt->credits);

		endpoint->ep_st.tx_cred_rpt += 1;
		endpoint->ep_st.cred_retnd += rpt->credits;

		if (from_ep == rpt->eid) {
			/*
			 * This credit report arrived on the same endpoint
			 * indicating it arrived in an RX packet.
			 */
			endpoint->ep_st.cred_from_rx += rpt->credits;
			endpoint->ep_st.cred_rpt_from_rx += 1;
		} else if (from_ep == ENDPOINT_0) {
			/* credit arrived on endpoint 0 as a NULL message */
			endpoint->ep_st.cred_from_ep0 += rpt->credits;
			endpoint->ep_st.cred_rpt_ep0 += 1;
		} else {
			endpoint->ep_st.cred_from_other += rpt->credits;
			endpoint->ep_st.cred_rpt_from_other += 1;
		}

		if (rpt->eid == ENDPOINT_0)
			/* always give endpoint 0 credits back */
			endpoint->cred_dist.credits += rpt->credits;
		else {
			endpoint->cred_dist.cred_to_dist += rpt->credits;
			dist = true;
		}

		/*
		 * Refresh tx depth for distribution function that will
		 * recover these credits NOTE: this is only valid when
		 * there are credits to recover!
		 */
		endpoint->cred_dist.txq_depth =
			get_queue_depth(&endpoint->txq);

		tot_credits += rpt->credits;
	}

	if (dist) {
		/*
		 * This was a credit return based on a completed send
		 * operations note, this is done with the lock held
		 */
		ath6kl_credit_distribute(target->credit_info,
					 &target->cred_dist_list,
					 HTC_CREDIT_DIST_SEND_COMPLETE);
	}

	spin_unlock_bh(&target->tx_lock);

	if (tot_credits)
		htc_chk_ep_txq(target);
}

static int htc_parse_trailer(struct htc_target *target,
			     struct htc_record_hdr *record,
			     u8 *record_buf, u32 *next_lk_ahds,
			     enum htc_endpoint_id endpoint,
			     int *n_lk_ahds)
{
	struct htc_bundle_lkahd_rpt *bundle_lkahd_rpt;
	struct htc_lookahead_report *lk_ahd;
	int len;

	switch (record->rec_id) {
	case HTC_RECORD_CREDITS:
		len = record->len / sizeof(struct htc_credit_report);
		if (!len) {
			WARN_ON(1);
			return -EINVAL;
		}

		htc_proc_cred_rpt(target,
				  (struct htc_credit_report *) record_buf,
				  len, endpoint);
		break;
	case HTC_RECORD_LOOKAHEAD:
		len = record->len / sizeof(*lk_ahd);
		if (!len) {
			WARN_ON(1);
			return -EINVAL;
		}

		lk_ahd = (struct htc_lookahead_report *) record_buf;
		if ((lk_ahd->pre_valid == ((~lk_ahd->post_valid) & 0xFF)) &&
		    next_lk_ahds) {
			ath6kl_dbg(ATH6KL_DBG_HTC,
				   "htc rx lk_ahd found pre_valid 0x%x post_valid 0x%x\n",
				   lk_ahd->pre_valid, lk_ahd->post_valid);

			/* look ahead bytes are valid, copy them over */
			memcpy((u8 *)&next_lk_ahds[0], lk_ahd->lk_ahd, 4);

			ath6kl_dbg_dump(ATH6KL_DBG_HTC,
					"htc rx next look ahead",
					"", next_lk_ahds, 4);

			*n_lk_ahds = 1;
		}
		break;
	case HTC_RECORD_LOOKAHEAD_BUNDLE:
		len = record->len / sizeof(*bundle_lkahd_rpt);
		if (!len || (len > HTC_HOST_MAX_MSG_PER_BUNDLE)) {
			WARN_ON(1);
			return -EINVAL;
		}

		if (next_lk_ahds) {
			int i;

			bundle_lkahd_rpt =
				(struct htc_bundle_lkahd_rpt *) record_buf;

			ath6kl_dbg_dump(ATH6KL_DBG_HTC, "htc rx bundle lk_ahd",
					"", record_buf, record->len);

			for (i = 0; i < len; i++) {
				memcpy((u8 *)&next_lk_ahds[i],
				       bundle_lkahd_rpt->lk_ahd, 4);
				bundle_lkahd_rpt++;
			}

			*n_lk_ahds = i;
		}
		break;
	default:
		ath6kl_err("unhandled record: id:%d len:%d\n",
			   record->rec_id, record->len);
		break;
	}

	return 0;
}

static int htc_proc_trailer(struct htc_target *target,
			    u8 *buf, int len, u32 *next_lk_ahds,
			    int *n_lk_ahds, enum htc_endpoint_id endpoint)
{
	struct htc_record_hdr *record;
	int orig_len;
	int status;
	u8 *record_buf;
	u8 *orig_buf;

	ath6kl_dbg(ATH6KL_DBG_HTC, "htc rx trailer len %d\n", len);
	ath6kl_dbg_dump(ATH6KL_DBG_HTC, NULL, "", buf, len);

	orig_buf = buf;
	orig_len = len;
	status = 0;

	while (len > 0) {
		if (len < sizeof(struct htc_record_hdr)) {
			status = -ENOMEM;
			break;
		}
		/* these are byte aligned structs */
		record = (struct htc_record_hdr *) buf;
		len -= sizeof(struct htc_record_hdr);
		buf += sizeof(struct htc_record_hdr);

		if (record->len > len) {
			ath6kl_err("invalid record len: %d (id:%d) buf has: %d bytes left\n",
				   record->len, record->rec_id, len);
			status = -ENOMEM;
			break;
		}
		record_buf = buf;

		status = htc_parse_trailer(target, record, record_buf,
					   next_lk_ahds, endpoint, n_lk_ahds);

		if (status)
			break;

		/* advance buffer past this record for next time around */
		buf += record->len;
		len -= record->len;
	}

	if (status)
		ath6kl_dbg_dump(ATH6KL_DBG_HTC, "htc rx bad trailer",
				"", orig_buf, orig_len);

	return status;
}

static int ath6kl_htc_rx_process_hdr(struct htc_target *target,
				     struct htc_packet *packet,
				     u32 *next_lkahds, int *n_lkahds)
{
	int status = 0;
	u16 payload_len;
	u32 lk_ahd;
	struct htc_frame_hdr *htc_hdr = (struct htc_frame_hdr *)packet->buf;

	if (n_lkahds != NULL)
		*n_lkahds = 0;

	/*
	 * NOTE: we cannot assume the alignment of buf, so we use the safe
	 * macros to retrieve 16 bit fields.
	 */
	payload_len = le16_to_cpu(get_unaligned(&htc_hdr->payld_len));

	memcpy((u8 *)&lk_ahd, packet->buf, sizeof(lk_ahd));

	if (packet->info.rx.rx_flags & HTC_RX_PKT_REFRESH_HDR) {
		/*
		 * Refresh the expected header and the actual length as it
		 * was unknown when this packet was grabbed as part of the
		 * bundle.
		 */
		packet->info.rx.exp_hdr = lk_ahd;
		packet->act_len = payload_len + HTC_HDR_LENGTH;

		/* validate the actual header that was refreshed  */
		if (packet->act_len > packet->buf_len) {
			ath6kl_err("refreshed hdr payload len (%d) in bundled recv is invalid (hdr: 0x%X)\n",
				   payload_len, lk_ahd);
			/*
			 * Limit this to max buffer just to print out some
			 * of the buffer.
			 */
			packet->act_len = min(packet->act_len, packet->buf_len);
			status = -ENOMEM;
			goto fail_rx;
		}

		if (packet->endpoint != htc_hdr->eid) {
			ath6kl_err("refreshed hdr ep (%d) does not match expected ep (%d)\n",
				   htc_hdr->eid, packet->endpoint);
			status = -ENOMEM;
			goto fail_rx;
		}
	}

	if (lk_ahd != packet->info.rx.exp_hdr) {
		ath6kl_err("%s(): lk_ahd mismatch! (pPkt:0x%p flags:0x%X)\n",
			   __func__, packet, packet->info.rx.rx_flags);
		ath6kl_dbg_dump(ATH6KL_DBG_HTC, "htc rx expected lk_ahd",
				"", &packet->info.rx.exp_hdr, 4);
		ath6kl_dbg_dump(ATH6KL_DBG_HTC, "htc rx current header",
				"", (u8 *)&lk_ahd, sizeof(lk_ahd));
		status = -ENOMEM;
		goto fail_rx;
	}

	if (htc_hdr->flags & HTC_FLG_RX_TRAILER) {
		if (htc_hdr->ctrl[0] < sizeof(struct htc_record_hdr) ||
		    htc_hdr->ctrl[0] > payload_len) {
			ath6kl_err("%s(): invalid hdr (payload len should be :%d, CB[0] is:%d)\n",
				   __func__, payload_len, htc_hdr->ctrl[0]);
			status = -ENOMEM;
			goto fail_rx;
		}

		if (packet->info.rx.rx_flags & HTC_RX_PKT_IGNORE_LOOKAHEAD) {
			next_lkahds = NULL;
			n_lkahds = NULL;
		}

		status = htc_proc_trailer(target, packet->buf + HTC_HDR_LENGTH
					  + payload_len - htc_hdr->ctrl[0],
					  htc_hdr->ctrl[0], next_lkahds,
					   n_lkahds, packet->endpoint);

		if (status)
			goto fail_rx;

		packet->act_len -= htc_hdr->ctrl[0];
	}

	packet->buf += HTC_HDR_LENGTH;
	packet->act_len -= HTC_HDR_LENGTH;

fail_rx:
	if (status)
		ath6kl_dbg_dump(ATH6KL_DBG_HTC, "htc rx bad packet",
				"", packet->buf, packet->act_len);

	return status;
}

static void ath6kl_htc_rx_complete(struct htc_endpoint *endpoint,
				   struct htc_packet *packet)
{
		ath6kl_dbg(ATH6KL_DBG_HTC,
			   "htc rx complete ep %d packet 0x%p\n",
			   endpoint->eid, packet);

		endpoint->ep_cb.rx(endpoint->target, packet);
}

static int ath6kl_htc_rx_bundle(struct htc_target *target,
				struct list_head *rxq,
				struct list_head *sync_compq,
				int *n_pkt_fetched, bool part_bundle)
{
	struct hif_scatter_req *scat_req;
	struct htc_packet *packet;
	int rem_space = target->max_rx_bndl_sz;
	int n_scat_pkt, status = 0, i, len;

	n_scat_pkt = get_queue_depth(rxq);
	n_scat_pkt = min(n_scat_pkt, target->msg_per_bndl_max);

	if ((get_queue_depth(rxq) - n_scat_pkt) > 0) {
		/*
		 * We were forced to split this bundle receive operation
		 * all packets in this partial bundle must have their
		 * lookaheads ignored.
		 */
		part_bundle = true;

		/*
		 * This would only happen if the target ignored our max
		 * bundle limit.
		 */
		ath6kl_warn("%s(): partial bundle detected num:%d , %d\n",
			    __func__, get_queue_depth(rxq), n_scat_pkt);
	}

	len = 0;

	ath6kl_dbg(ATH6KL_DBG_HTC,
		   "htc rx bundle depth %d pkts %d\n",
		   get_queue_depth(rxq), n_scat_pkt);

	scat_req = hif_scatter_req_get(target->dev->ar);

	if (scat_req == NULL)
		goto fail_rx_pkt;

	for (i = 0; i < n_scat_pkt; i++) {
		int pad_len;

		packet = list_first_entry(rxq, struct htc_packet, list);
		list_del(&packet->list);

		pad_len = CALC_TXRX_PADDED_LEN(target,
						   packet->act_len);

		if ((rem_space - pad_len) < 0) {
			list_add(&packet->list, rxq);
			break;
		}

		rem_space -= pad_len;

		if (part_bundle || (i < (n_scat_pkt - 1)))
			/*
			 * Packet 0..n-1 cannot be checked for look-aheads
			 * since we are fetching a bundle the last packet
			 * however can have it's lookahead used
			 */
			packet->info.rx.rx_flags |=
			    HTC_RX_PKT_IGNORE_LOOKAHEAD;

		/* NOTE: 1 HTC packet per scatter entry */
		scat_req->scat_list[i].buf = packet->buf;
		scat_req->scat_list[i].len = pad_len;

		packet->info.rx.rx_flags |= HTC_RX_PKT_PART_OF_BUNDLE;

		list_add_tail(&packet->list, sync_compq);

		WARN_ON(!scat_req->scat_list[i].len);
		len += scat_req->scat_list[i].len;
	}

	scat_req->len = len;
	scat_req->scat_entries = i;

	status = ath6kl_hif_submit_scat_req(target->dev, scat_req, true);

	if (!status)
		*n_pkt_fetched = i;

	/* free scatter request */
	hif_scatter_req_add(target->dev->ar, scat_req);

fail_rx_pkt:

	return status;
}

static int ath6kl_htc_rx_process_packets(struct htc_target *target,
					 struct list_head *comp_pktq,
					 u32 lk_ahds[],
					 int *n_lk_ahd)
{
	struct htc_packet *packet, *tmp_pkt;
	struct htc_endpoint *ep;
	int status = 0;

	list_for_each_entry_safe(packet, tmp_pkt, comp_pktq, list) {
		ep = &target->endpoint[packet->endpoint];

		trace_ath6kl_htc_rx(packet->status, packet->endpoint,
				    packet->buf, packet->act_len);

		/* process header for each of the recv packet */
		status = ath6kl_htc_rx_process_hdr(target, packet, lk_ahds,
						   n_lk_ahd);
		if (status)
			return status;

		list_del(&packet->list);

		if (list_empty(comp_pktq)) {
			/*
			 * Last packet's more packet flag is set
			 * based on the lookahead.
			 */
			if (*n_lk_ahd > 0)
				ath6kl_htc_rx_set_indicate(lk_ahds[0],
							   ep, packet);
		} else
			/*
			 * Packets in a bundle automatically have
			 * this flag set.
			 */
			packet->info.rx.indicat_flags |=
				HTC_RX_FLAGS_INDICATE_MORE_PKTS;

		ath6kl_htc_rx_update_stats(ep, *n_lk_ahd);

		if (packet->info.rx.rx_flags & HTC_RX_PKT_PART_OF_BUNDLE)
			ep->ep_st.rx_bundl += 1;

		ath6kl_htc_rx_complete(ep, packet);
	}

	return status;
}

static int ath6kl_htc_rx_fetch(struct htc_target *target,
			       struct list_head *rx_pktq,
			       struct list_head *comp_pktq)
{
	int fetched_pkts;
	bool part_bundle = false;
	int status = 0;
	struct list_head tmp_rxq;
	struct htc_packet *packet, *tmp_pkt;

	/* now go fetch the list of HTC packets */
	while (!list_empty(rx_pktq)) {
		fetched_pkts = 0;

		INIT_LIST_HEAD(&tmp_rxq);

		if (target->rx_bndl_enable && (get_queue_depth(rx_pktq) > 1)) {
			/*
			 * There are enough packets to attempt a
			 * bundle transfer and recv bundling is
			 * allowed.
			 */
			status = ath6kl_htc_rx_bundle(target, rx_pktq,
						      &tmp_rxq,
						      &fetched_pkts,
						      part_bundle);
			if (status)
				goto fail_rx;

			if (!list_empty(rx_pktq))
				part_bundle = true;

			list_splice_tail_init(&tmp_rxq, comp_pktq);
		}

		if (!fetched_pkts) {
			packet = list_first_entry(rx_pktq, struct htc_packet,
						   list);

			/* fully synchronous */
			packet->completion = NULL;

			if (!list_is_singular(rx_pktq))
				/*
				 * look_aheads in all packet
				 * except the last one in the
				 * bundle must be ignored
				 */
				packet->info.rx.rx_flags |=
					HTC_RX_PKT_IGNORE_LOOKAHEAD;

			/* go fetch the packet */
			status = ath6kl_htc_rx_packet(target, packet,
						      packet->act_len);

			list_move_tail(&packet->list, &tmp_rxq);

			if (status)
				goto fail_rx;

			list_splice_tail_init(&tmp_rxq, comp_pktq);
		}
	}

	return 0;

fail_rx:

	/*
	 * Cleanup any packets we allocated but didn't use to
	 * actually fetch any packets.
	 */

	list_for_each_entry_safe(packet, tmp_pkt, rx_pktq, list) {
		list_del(&packet->list);
		htc_reclaim_rxbuf(target, packet,
				  &target->endpoint[packet->endpoint]);
	}

	list_for_each_entry_safe(packet, tmp_pkt, &tmp_rxq, list) {
		list_del(&packet->list);
		htc_reclaim_rxbuf(target, packet,
				  &target->endpoint[packet->endpoint]);
	}

	return status;
}

int ath6kl_htc_rxmsg_pending_handler(struct htc_target *target,
				     u32 msg_look_ahead, int *num_pkts)
{
	struct htc_packet *packets, *tmp_pkt;
	struct htc_endpoint *endpoint;
	struct list_head rx_pktq, comp_pktq;
	int status = 0;
	u32 look_aheads[HTC_HOST_MAX_MSG_PER_BUNDLE];
	int num_look_ahead = 1;
	enum htc_endpoint_id id;
	int n_fetched = 0;

	INIT_LIST_HEAD(&comp_pktq);
	*num_pkts = 0;

	/*
	 * On first entry copy the look_aheads into our temp array for
	 * processing
	 */
	look_aheads[0] = msg_look_ahead;

	while (true) {
		/*
		 * First lookahead sets the expected endpoint IDs for all
		 * packets in a bundle.
		 */
		id = ((struct htc_frame_hdr *)&look_aheads[0])->eid;
		endpoint = &target->endpoint[id];

		if (id >= ENDPOINT_MAX) {
			ath6kl_err("MsgPend, invalid endpoint in look-ahead: %d\n",
				   id);
			status = -ENOMEM;
			break;
		}

		INIT_LIST_HEAD(&rx_pktq);
		INIT_LIST_HEAD(&comp_pktq);

		/*
		 * Try to allocate as many HTC RX packets indicated by the
		 * look_aheads.
		 */
		status = ath6kl_htc_rx_alloc(target, look_aheads,
					     num_look_ahead, endpoint,
					     &rx_pktq);
		if (status)
			break;

		if (get_queue_depth(&rx_pktq) >= 2)
			/*
			 * A recv bundle was detected, force IRQ status
			 * re-check again
			 */
			target->chk_irq_status_cnt = 1;

		n_fetched += get_queue_depth(&rx_pktq);

		num_look_ahead = 0;

		status = ath6kl_htc_rx_fetch(target, &rx_pktq, &comp_pktq);

		if (!status)
			ath6kl_htc_rx_chk_water_mark(endpoint);

		/* Process fetched packets */
		status = ath6kl_htc_rx_process_packets(target, &comp_pktq,
						       look_aheads,
						       &num_look_ahead);

		if (!num_look_ahead || status)
			break;

		/*
		 * For SYNCH processing, if we get here, we are running
		 * through the loop again due to a detected lookahead. Set
		 * flag that we should re-check IRQ status registers again
		 * before leaving IRQ processing, this can net better
		 * performance in high throughput situations.
		 */
		target->chk_irq_status_cnt = 1;
	}

	if (status) {
		if (status != -ECANCELED)
			ath6kl_err("failed to get pending recv messages: %d\n",
				   status);

		/* cleanup any packets in sync completion queue */
		list_for_each_entry_safe(packets, tmp_pkt, &comp_pktq, list) {
			list_del(&packets->list);
			htc_reclaim_rxbuf(target, packets,
					  &target->endpoint[packets->endpoint]);
		}

		if (target->htc_flags & HTC_OP_STATE_STOPPING) {
			ath6kl_warn("host is going to stop blocking receiver for htc_stop\n");
			ath6kl_hif_rx_control(target->dev, false);
		}
	}

	/*
	 * Before leaving, check to see if host ran out of buffers and
	 * needs to stop the receiver.
	 */
	if (target->rx_st_flags & HTC_RECV_WAIT_BUFFERS) {
		ath6kl_warn("host has no rx buffers blocking receiver to prevent overrun\n");
		ath6kl_hif_rx_control(target->dev, false);
	}
	*num_pkts = n_fetched;

	return status;
}

/*
 * Synchronously wait for a control message from the target,
 * This function is used at initialization time ONLY.  At init messages
 * on ENDPOINT 0 are expected.
 */
static struct htc_packet *htc_wait_for_ctrl_msg(struct htc_target *target)
{
	struct htc_packet *packet = NULL;
	struct htc_frame_hdr *htc_hdr;
	u32 look_ahead;

	if (ath6kl_hif_poll_mboxmsg_rx(target->dev, &look_ahead,
				       HTC_TARGET_RESPONSE_TIMEOUT))
		return NULL;

	ath6kl_dbg(ATH6KL_DBG_HTC,
		   "htc rx wait ctrl look_ahead 0x%X\n", look_ahead);

	htc_hdr = (struct htc_frame_hdr *)&look_ahead;

	if (htc_hdr->eid != ENDPOINT_0)
		return NULL;

	packet = htc_get_control_buf(target, false);

	if (!packet)
		return NULL;

	packet->info.rx.rx_flags = 0;
	packet->info.rx.exp_hdr = look_ahead;
	packet->act_len = le16_to_cpu(htc_hdr->payld_len) + HTC_HDR_LENGTH;

	if (packet->act_len > packet->buf_len)
		goto fail_ctrl_rx;

	/* we want synchronous operation */
	packet->completion = NULL;

	/* get the message from the device, this will block */
	if (ath6kl_htc_rx_packet(target, packet, packet->act_len))
		goto fail_ctrl_rx;

	trace_ath6kl_htc_rx(packet->status, packet->endpoint,
			    packet->buf, packet->act_len);

	/* process receive header */
	packet->status = ath6kl_htc_rx_process_hdr(target, packet, NULL, NULL);

	if (packet->status) {
		ath6kl_err("htc_wait_for_ctrl_msg, ath6kl_htc_rx_process_hdr failed (status = %d)\n",
			   packet->status);
		goto fail_ctrl_rx;
	}

	return packet;

fail_ctrl_rx:
	if (packet != NULL) {
		htc_rxpkt_reset(packet);
		reclaim_rx_ctrl_buf(target, packet);
	}

	return NULL;
}

static int ath6kl_htc_mbox_add_rxbuf_multiple(struct htc_target *target,
				  struct list_head *pkt_queue)
{
	struct htc_endpoint *endpoint;
	struct htc_packet *first_pkt;
	bool rx_unblock = false;
	int status = 0, depth;

	if (list_empty(pkt_queue))
		return -ENOMEM;

	first_pkt = list_first_entry(pkt_queue, struct htc_packet, list);

	if (first_pkt->endpoint >= ENDPOINT_MAX)
		return status;

	depth = get_queue_depth(pkt_queue);

	ath6kl_dbg(ATH6KL_DBG_HTC,
		   "htc rx add multiple ep id %d cnt %d len %d\n",
		first_pkt->endpoint, depth, first_pkt->buf_len);

	endpoint = &target->endpoint[first_pkt->endpoint];

	if (target->htc_flags & HTC_OP_STATE_STOPPING) {
		struct htc_packet *packet, *tmp_pkt;

		/* walk through queue and mark each one canceled */
		list_for_each_entry_safe(packet, tmp_pkt, pkt_queue, list) {
			packet->status = -ECANCELED;
			list_del(&packet->list);
			ath6kl_htc_rx_complete(endpoint, packet);
		}

		return status;
	}

	spin_lock_bh(&target->rx_lock);

	list_splice_tail_init(pkt_queue, &endpoint->rx_bufq);

	/* check if we are blocked waiting for a new buffer */
	if (target->rx_st_flags & HTC_RECV_WAIT_BUFFERS) {
		if (target->ep_waiting == first_pkt->endpoint) {
			ath6kl_dbg(ATH6KL_DBG_HTC,
				   "htc rx blocked on ep %d, unblocking\n",
				   target->ep_waiting);
			target->rx_st_flags &= ~HTC_RECV_WAIT_BUFFERS;
			target->ep_waiting = ENDPOINT_MAX;
			rx_unblock = true;
		}
	}

	spin_unlock_bh(&target->rx_lock);

	if (rx_unblock && !(target->htc_flags & HTC_OP_STATE_STOPPING))
		/* TODO : implement a buffer threshold count? */
		ath6kl_hif_rx_control(target->dev, true);

	return status;
}

static void ath6kl_htc_mbox_flush_rx_buf(struct htc_target *target)
{
	struct htc_endpoint *endpoint;
	struct htc_packet *packet, *tmp_pkt;
	int i;

	for (i = ENDPOINT_0; i < ENDPOINT_MAX; i++) {
		endpoint = &target->endpoint[i];
		if (!endpoint->svc_id)
			/* not in use.. */
			continue;

		spin_lock_bh(&target->rx_lock);
		list_for_each_entry_safe(packet, tmp_pkt,
					 &endpoint->rx_bufq, list) {
			list_del(&packet->list);
			spin_unlock_bh(&target->rx_lock);
			ath6kl_dbg(ATH6KL_DBG_HTC,
				   "htc rx flush pkt 0x%p  len %d  ep %d\n",
				   packet, packet->buf_len,
				   packet->endpoint);
			/*
			 * packets in rx_bufq of endpoint 0 have originally
			 * been queued from target->free_ctrl_rxbuf where
			 * packet and packet->buf_start are allocated
			 * separately using kmalloc(). For other endpoint
			 * rx_bufq, it is allocated as skb where packet is
			 * skb->head. Take care of this difference while freeing
			 * the memory.
			 */
			if (packet->endpoint == ENDPOINT_0) {
				kfree(packet->buf_start);
				kfree(packet);
			} else {
				dev_kfree_skb(packet->pkt_cntxt);
			}
			spin_lock_bh(&target->rx_lock);
		}
		spin_unlock_bh(&target->rx_lock);
	}
}

static int ath6kl_htc_mbox_conn_service(struct htc_target *target,
			    struct htc_service_connect_req *conn_req,
			    struct htc_service_connect_resp *conn_resp)
{
	struct htc_packet *rx_pkt = NULL;
	struct htc_packet *tx_pkt = NULL;
	struct htc_conn_service_resp *resp_msg;
	struct htc_conn_service_msg *conn_msg;
	struct htc_endpoint *endpoint;
	enum htc_endpoint_id assigned_ep = ENDPOINT_MAX;
	unsigned int max_msg_sz = 0;
	int status = 0;
	u16 msg_id;

	ath6kl_dbg(ATH6KL_DBG_HTC,
		   "htc connect service target 0x%p service id 0x%x\n",
		   target, conn_req->svc_id);

	if (conn_req->svc_id == HTC_CTRL_RSVD_SVC) {
		/* special case for pseudo control service */
		assigned_ep = ENDPOINT_0;
		max_msg_sz = HTC_MAX_CTRL_MSG_LEN;
	} else {
		/* allocate a packet to send to the target */
		tx_pkt = htc_get_control_buf(target, true);

		if (!tx_pkt)
			return -ENOMEM;

		conn_msg = (struct htc_conn_service_msg *)tx_pkt->buf;
		memset(conn_msg, 0, sizeof(*conn_msg));
		conn_msg->msg_id = cpu_to_le16(HTC_MSG_CONN_SVC_ID);
		conn_msg->svc_id = cpu_to_le16(conn_req->svc_id);
		conn_msg->conn_flags = cpu_to_le16(conn_req->conn_flags);

		set_htc_pkt_info(tx_pkt, NULL, (u8 *) conn_msg,
				 sizeof(*conn_msg) + conn_msg->svc_meta_len,
				 ENDPOINT_0, HTC_SERVICE_TX_PACKET_TAG);

		/* we want synchronous operation */
		tx_pkt->completion = NULL;
		ath6kl_htc_tx_prep_pkt(tx_pkt, 0, 0, 0);
		status = ath6kl_htc_tx_issue(target, tx_pkt);

		if (status)
			goto fail_tx;

		/* wait for response */
		rx_pkt = htc_wait_for_ctrl_msg(target);

		if (!rx_pkt) {
			status = -ENOMEM;
			goto fail_tx;
		}

		resp_msg = (struct htc_conn_service_resp *)rx_pkt->buf;
		msg_id = le16_to_cpu(resp_msg->msg_id);

		if ((msg_id != HTC_MSG_CONN_SVC_RESP_ID) ||
		    (rx_pkt->act_len < sizeof(*resp_msg))) {
			status = -ENOMEM;
			goto fail_tx;
		}

		conn_resp->resp_code = resp_msg->status;
		/* check response status */
		if (resp_msg->status != HTC_SERVICE_SUCCESS) {
			ath6kl_err("target failed service 0x%X connect request (status:%d)\n",
				   resp_msg->svc_id, resp_msg->status);
			status = -ENOMEM;
			goto fail_tx;
		}

		assigned_ep = (enum htc_endpoint_id)resp_msg->eid;
		max_msg_sz = le16_to_cpu(resp_msg->max_msg_sz);
	}

	if (WARN_ON_ONCE(assigned_ep == ENDPOINT_UNUSED ||
			 assigned_ep >= ENDPOINT_MAX || !max_msg_sz)) {
		status = -ENOMEM;
		goto fail_tx;
	}

	endpoint = &target->endpoint[assigned_ep];
	endpoint->eid = assigned_ep;
	if (endpoint->svc_id) {
		status = -ENOMEM;
		goto fail_tx;
	}

	/* return assigned endpoint to caller */
	conn_resp->endpoint = assigned_ep;
	conn_resp->len_max = max_msg_sz;

	/* setup the endpoint */

	/* this marks the endpoint in use */
	endpoint->svc_id = conn_req->svc_id;

	endpoint->max_txq_depth = conn_req->max_txq_depth;
	endpoint->len_max = max_msg_sz;
	endpoint->ep_cb = conn_req->ep_cb;
	endpoint->cred_dist.svc_id = conn_req->svc_id;
	endpoint->cred_dist.htc_ep = endpoint;
	endpoint->cred_dist.endpoint = assigned_ep;
	endpoint->cred_dist.cred_sz = target->tgt_cred_sz;

	switch (endpoint->svc_id) {
	case WMI_DATA_BK_SVC:
		endpoint->tx_drop_packet_threshold = MAX_DEF_COOKIE_NUM / 3;
		break;
	default:
		endpoint->tx_drop_packet_threshold = MAX_HI_COOKIE_NUM;
		break;
	}

	if (conn_req->max_rxmsg_sz) {
		/*
		 * Override cred_per_msg calculation, this optimizes
		 * the credit-low indications since the host will actually
		 * issue smaller messages in the Send path.
		 */
		if (conn_req->max_rxmsg_sz > max_msg_sz) {
			status = -ENOMEM;
			goto fail_tx;
		}
		endpoint->cred_dist.cred_per_msg =
		    conn_req->max_rxmsg_sz / target->tgt_cred_sz;
	} else
		endpoint->cred_dist.cred_per_msg =
		    max_msg_sz / target->tgt_cred_sz;

	if (!endpoint->cred_dist.cred_per_msg)
		endpoint->cred_dist.cred_per_msg = 1;

	/* save local connection flags */
	endpoint->conn_flags = conn_req->flags;

fail_tx:
	if (tx_pkt)
		htc_reclaim_txctrl_buf(target, tx_pkt);

	if (rx_pkt) {
		htc_rxpkt_reset(rx_pkt);
		reclaim_rx_ctrl_buf(target, rx_pkt);
	}

	return status;
}

static void reset_ep_state(struct htc_target *target)
{
	struct htc_endpoint *endpoint;
	int i;

	for (i = ENDPOINT_0; i < ENDPOINT_MAX; i++) {
		endpoint = &target->endpoint[i];
		memset(&endpoint->cred_dist, 0, sizeof(endpoint->cred_dist));
		endpoint->svc_id = 0;
		endpoint->len_max = 0;
		endpoint->max_txq_depth = 0;
		memset(&endpoint->ep_st, 0,
		       sizeof(endpoint->ep_st));
		INIT_LIST_HEAD(&endpoint->rx_bufq);
		INIT_LIST_HEAD(&endpoint->txq);
		endpoint->target = target;
	}

	/* reset distribution list */
	/* FIXME: free existing entries */
	INIT_LIST_HEAD(&target->cred_dist_list);
}

static int ath6kl_htc_mbox_get_rxbuf_num(struct htc_target *target,
			     enum htc_endpoint_id endpoint)
{
	int num;

	spin_lock_bh(&target->rx_lock);
	num = get_queue_depth(&(target->endpoint[endpoint].rx_bufq));
	spin_unlock_bh(&target->rx_lock);
	return num;
}

static void htc_setup_msg_bndl(struct htc_target *target)
{
	/* limit what HTC can handle */
	target->msg_per_bndl_max = min(HTC_HOST_MAX_MSG_PER_BUNDLE,
				       target->msg_per_bndl_max);

	if (ath6kl_hif_enable_scatter(target->dev->ar)) {
		target->msg_per_bndl_max = 0;
		return;
	}

	/* limit bundle what the device layer can handle */
	target->msg_per_bndl_max = min(target->max_scat_entries,
				       target->msg_per_bndl_max);

	ath6kl_dbg(ATH6KL_DBG_BOOT,
		   "htc bundling allowed msg_per_bndl_max %d\n",
		   target->msg_per_bndl_max);

	/* Max rx bundle size is limited by the max tx bundle size */
	target->max_rx_bndl_sz = target->max_xfer_szper_scatreq;
	/* Max tx bundle size if limited by the extended mbox address range */
	target->max_tx_bndl_sz = min(HIF_MBOX0_EXT_WIDTH,
				     target->max_xfer_szper_scatreq);

	ath6kl_dbg(ATH6KL_DBG_BOOT, "htc max_rx_bndl_sz %d max_tx_bndl_sz %d\n",
		   target->max_rx_bndl_sz, target->max_tx_bndl_sz);

	if (target->max_tx_bndl_sz)
		/* tx_bndl_mask is enabled per AC, each has 1 bit */
		target->tx_bndl_mask = (1 << WMM_NUM_AC) - 1;

	if (target->max_rx_bndl_sz)
		target->rx_bndl_enable = true;

	if ((target->tgt_cred_sz % target->block_sz) != 0) {
		ath6kl_warn("credit size: %d is not block aligned! Disabling send bundling\n",
			    target->tgt_cred_sz);

		/*
		 * Disallow send bundling since the credit size is
		 * not aligned to a block size the I/O block
		 * padding will spill into the next credit buffer
		 * which is fatal.
		 */
		target->tx_bndl_mask = 0;
	}
}

static int ath6kl_htc_mbox_wait_target(struct htc_target *target)
{
	struct htc_packet *packet = NULL;
	struct htc_ready_ext_msg *rdy_msg;
	struct htc_service_connect_req connect;
	struct htc_service_connect_resp resp;
	int status;

	/* we should be getting 1 control message that the target is ready */
	packet = htc_wait_for_ctrl_msg(target);

	if (!packet)
		return -ENOMEM;

	/* we controlled the buffer creation so it's properly aligned */
	rdy_msg = (struct htc_ready_ext_msg *)packet->buf;

	if ((le16_to_cpu(rdy_msg->ver2_0_info.msg_id) != HTC_MSG_READY_ID) ||
	    (packet->act_len < sizeof(struct htc_ready_msg))) {
		status = -ENOMEM;
		goto fail_wait_target;
	}

	if (!rdy_msg->ver2_0_info.cred_cnt || !rdy_msg->ver2_0_info.cred_sz) {
		status = -ENOMEM;
		goto fail_wait_target;
	}

	target->tgt_creds = le16_to_cpu(rdy_msg->ver2_0_info.cred_cnt);
	target->tgt_cred_sz = le16_to_cpu(rdy_msg->ver2_0_info.cred_sz);

	ath6kl_dbg(ATH6KL_DBG_BOOT,
		   "htc target ready credits %d size %d\n",
		   target->tgt_creds, target->tgt_cred_sz);

	/* check if this is an extended ready message */
	if (packet->act_len >= sizeof(struct htc_ready_ext_msg)) {
		/* this is an extended message */
		target->htc_tgt_ver = rdy_msg->htc_ver;
		target->msg_per_bndl_max = rdy_msg->msg_per_htc_bndl;
	} else {
		/* legacy */
		target->htc_tgt_ver = HTC_VERSION_2P0;
		target->msg_per_bndl_max = 0;
	}

	ath6kl_dbg(ATH6KL_DBG_BOOT, "htc using protocol %s (%d)\n",
		   (target->htc_tgt_ver == HTC_VERSION_2P0) ? "2.0" : ">= 2.1",
		   target->htc_tgt_ver);

	if (target->msg_per_bndl_max > 0)
		htc_setup_msg_bndl(target);

	/* setup our pseudo HTC control endpoint connection */
	memset(&connect, 0, sizeof(connect));
	memset(&resp, 0, sizeof(resp));
	connect.ep_cb.rx = htc_ctrl_rx;
	connect.ep_cb.rx_refill = NULL;
	connect.ep_cb.tx_full = NULL;
	connect.max_txq_depth = NUM_CONTROL_BUFFERS;
	connect.svc_id = HTC_CTRL_RSVD_SVC;

	/* connect fake service */
	status = ath6kl_htc_mbox_conn_service((void *)target, &connect, &resp);

	if (status)
		/*
		 * FIXME: this call doesn't make sense, the caller should
		 * call ath6kl_htc_mbox_cleanup() when it wants remove htc
		 */
		ath6kl_hif_cleanup_scatter(target->dev->ar);

fail_wait_target:
	if (packet) {
		htc_rxpkt_reset(packet);
		reclaim_rx_ctrl_buf(target, packet);
	}

	return status;
}

/*
 * Start HTC, enable interrupts and let the target know
 * host has finished setup.
 */
static int ath6kl_htc_mbox_start(struct htc_target *target)
{
	struct htc_packet *packet;
	int status;

	memset(&target->dev->irq_proc_reg, 0,
	       sizeof(target->dev->irq_proc_reg));

	/* Disable interrupts at the chip level */
	ath6kl_hif_disable_intrs(target->dev);

	target->htc_flags = 0;
	target->rx_st_flags = 0;

	/* Push control receive buffers into htc control endpoint */
	while ((packet = htc_get_control_buf(target, false)) != NULL) {
		status = htc_add_rxbuf(target, packet);
		if (status)
			return status;
	}

	/* NOTE: the first entry in the distribution list is ENDPOINT_0 */
	ath6kl_credit_init(target->credit_info, &target->cred_dist_list,
			   target->tgt_creds);

	dump_cred_dist_stats(target);

	/* Indicate to the target of the setup completion */
	status = htc_setup_tx_complete(target);

	if (status)
		return status;

	/* unmask interrupts */
	status = ath6kl_hif_unmask_intrs(target->dev);

	if (status)
		ath6kl_htc_mbox_stop(target);

	return status;
}

static int ath6kl_htc_reset(struct htc_target *target)
{
	u32 block_size, ctrl_bufsz;
	struct htc_packet *packet;
	int i;

	reset_ep_state(target);

	block_size = target->dev->ar->mbox_info.block_size;

	ctrl_bufsz = (block_size > HTC_MAX_CTRL_MSG_LEN) ?
		      (block_size + HTC_HDR_LENGTH) :
		      (HTC_MAX_CTRL_MSG_LEN + HTC_HDR_LENGTH);

	for (i = 0; i < NUM_CONTROL_BUFFERS; i++) {
		packet = kzalloc(sizeof(*packet), GFP_KERNEL);
		if (!packet)
			return -ENOMEM;

		packet->buf_start = kzalloc(ctrl_bufsz, GFP_KERNEL);
		if (!packet->buf_start) {
			kfree(packet);
			return -ENOMEM;
		}

		packet->buf_len = ctrl_bufsz;
		if (i < NUM_CONTROL_RX_BUFFERS) {
			packet->act_len = 0;
			packet->buf = packet->buf_start;
			packet->endpoint = ENDPOINT_0;
			list_add_tail(&packet->list, &target->free_ctrl_rxbuf);
		} else {
			list_add_tail(&packet->list, &target->free_ctrl_txbuf);
		}
	}

	return 0;
}

/* htc_stop: stop interrupt reception, and flush all queued buffers */
static void ath6kl_htc_mbox_stop(struct htc_target *target)
{
	spin_lock_bh(&target->htc_lock);
	target->htc_flags |= HTC_OP_STATE_STOPPING;
	spin_unlock_bh(&target->htc_lock);

	/*
	 * Masking interrupts is a synchronous operation, when this
	 * function returns all pending HIF I/O has completed, we can
	 * safely flush the queues.
	 */
	ath6kl_hif_mask_intrs(target->dev);

	ath6kl_htc_flush_txep_all(target);

	ath6kl_htc_mbox_flush_rx_buf(target);

	ath6kl_htc_reset(target);
}

static void *ath6kl_htc_mbox_create(struct ath6kl *ar)
{
	struct htc_target *target = NULL;
	int status = 0;

	target = kzalloc(sizeof(*target), GFP_KERNEL);
	if (!target) {
		ath6kl_err("unable to allocate memory\n");
		return NULL;
	}

	target->dev = kzalloc(sizeof(*target->dev), GFP_KERNEL);
	if (!target->dev) {
		ath6kl_err("unable to allocate memory\n");
		status = -ENOMEM;
		goto err_htc_cleanup;
	}

	spin_lock_init(&target->htc_lock);
	spin_lock_init(&target->rx_lock);
	spin_lock_init(&target->tx_lock);

	INIT_LIST_HEAD(&target->free_ctrl_txbuf);
	INIT_LIST_HEAD(&target->free_ctrl_rxbuf);
	INIT_LIST_HEAD(&target->cred_dist_list);

	target->dev->ar = ar;
	target->dev->htc_cnxt = target;
	target->ep_waiting = ENDPOINT_MAX;

	status = ath6kl_hif_setup(target->dev);
	if (status)
		goto err_htc_cleanup;

	status = ath6kl_htc_reset(target);
	if (status)
		goto err_htc_cleanup;

	return target;

err_htc_cleanup:
	ath6kl_htc_mbox_cleanup(target);

	return NULL;
}

/* cleanup the HTC instance */
static void ath6kl_htc_mbox_cleanup(struct htc_target *target)
{
	struct htc_packet *packet, *tmp_packet;

	ath6kl_hif_cleanup_scatter(target->dev->ar);

	list_for_each_entry_safe(packet, tmp_packet,
				 &target->free_ctrl_txbuf, list) {
		list_del(&packet->list);
		kfree(packet->buf_start);
		kfree(packet);
	}

	list_for_each_entry_safe(packet, tmp_packet,
				 &target->free_ctrl_rxbuf, list) {
		list_del(&packet->list);
		kfree(packet->buf_start);
		kfree(packet);
	}

	kfree(target->dev);
	kfree(target);
}

static const struct ath6kl_htc_ops ath6kl_htc_mbox_ops = {
	.create = ath6kl_htc_mbox_create,
	.wait_target = ath6kl_htc_mbox_wait_target,
	.start = ath6kl_htc_mbox_start,
	.conn_service = ath6kl_htc_mbox_conn_service,
	.tx = ath6kl_htc_mbox_tx,
	.stop = ath6kl_htc_mbox_stop,
	.cleanup = ath6kl_htc_mbox_cleanup,
	.flush_txep = ath6kl_htc_mbox_flush_txep,
	.flush_rx_buf = ath6kl_htc_mbox_flush_rx_buf,
	.activity_changed = ath6kl_htc_mbox_activity_changed,
	.get_rxbuf_num = ath6kl_htc_mbox_get_rxbuf_num,
	.add_rxbuf_multiple = ath6kl_htc_mbox_add_rxbuf_multiple,
	.credit_setup = ath6kl_htc_mbox_credit_setup,
};

void ath6kl_htc_mbox_attach(struct ath6kl *ar)
{
	ar->htc_ops = &ath6kl_htc_mbox_ops;
}
