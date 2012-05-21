/*
 * Copyright (c) 2007-2011 Atheros Communications Inc.
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
#include "debug.h"
#include "hif-ops.h"

#define HTC_PACKET_CONTAINER_ALLOCATION 32
#define HTC_CONTROL_BUFFER_SIZE (HTC_MAX_CTRL_MSG_LEN + HTC_HDR_LENGTH)

static int ath6kl_htc_pipe_tx(struct htc_target *handle,
			      struct htc_packet *packet);
static void ath6kl_htc_pipe_cleanup(struct htc_target *handle);

/* htc pipe tx path */
static inline void restore_tx_packet(struct htc_packet *packet)
{
	if (packet->info.tx.flags & HTC_FLAGS_TX_FIXUP_NETBUF) {
		skb_pull(packet->skb, sizeof(struct htc_frame_hdr));
		packet->info.tx.flags &= ~HTC_FLAGS_TX_FIXUP_NETBUF;
	}
}

static void do_send_completion(struct htc_endpoint *ep,
			       struct list_head *queue_to_indicate)
{
	struct htc_packet *packet;

	if (list_empty(queue_to_indicate)) {
		/* nothing to indicate */
		return;
	}

	if (ep->ep_cb.tx_comp_multi != NULL) {
		ath6kl_dbg(ATH6KL_DBG_HTC,
			   "%s: calling ep %d, send complete multiple callback (%d pkts)\n",
			   __func__, ep->eid,
			   get_queue_depth(queue_to_indicate));
		/*
		 * a multiple send complete handler is being used,
		 * pass the queue to the handler
		 */
		ep->ep_cb.tx_comp_multi(ep->target, queue_to_indicate);
		/*
		 * all packets are now owned by the callback,
		 * reset queue to be safe
		 */
		INIT_LIST_HEAD(queue_to_indicate);
	} else {
		/* using legacy EpTxComplete */
		do {
			packet = list_first_entry(queue_to_indicate,
						  struct htc_packet, list);

			list_del(&packet->list);
			ath6kl_dbg(ATH6KL_DBG_HTC,
				   "%s: calling ep %d send complete callback on packet 0x%p\n",
				   __func__, ep->eid, packet);
			ep->ep_cb.tx_complete(ep->target, packet);
		} while (!list_empty(queue_to_indicate));
	}
}

static void send_packet_completion(struct htc_target *target,
				   struct htc_packet *packet)
{
	struct htc_endpoint *ep = &target->endpoint[packet->endpoint];
	struct list_head container;

	restore_tx_packet(packet);
	INIT_LIST_HEAD(&container);
	list_add_tail(&packet->list, &container);

	/* do completion */
	do_send_completion(ep, &container);
}

static void get_htc_packet_credit_based(struct htc_target *target,
					struct htc_endpoint *ep,
					struct list_head *queue)
{
	int credits_required;
	int remainder;
	u8 send_flags;
	struct htc_packet *packet;
	unsigned int transfer_len;

	/* NOTE : the TX lock is held when this function is called */

	/* loop until we can grab as many packets out of the queue as we can */
	while (true) {
		send_flags = 0;
		if (list_empty(&ep->txq))
			break;

		/* get packet at head, but don't remove it */
		packet = list_first_entry(&ep->txq, struct htc_packet, list);
		if (packet == NULL)
			break;

		ath6kl_dbg(ATH6KL_DBG_HTC,
			   "%s: got head packet:0x%p , queue depth: %d\n",
			   __func__, packet, get_queue_depth(&ep->txq));

		transfer_len = packet->act_len + HTC_HDR_LENGTH;

		if (transfer_len <= target->tgt_cred_sz) {
			credits_required = 1;
		} else {
			/* figure out how many credits this message requires */
			credits_required = transfer_len / target->tgt_cred_sz;
			remainder = transfer_len % target->tgt_cred_sz;

			if (remainder)
				credits_required++;
		}

		ath6kl_dbg(ATH6KL_DBG_HTC, "%s: creds required:%d got:%d\n",
			   __func__, credits_required, ep->cred_dist.credits);

		if (ep->eid == ENDPOINT_0) {
			/*
			 * endpoint 0 is special, it always has a credit and
			 * does not require credit based flow control
			 */
			credits_required = 0;

		} else {

			if (ep->cred_dist.credits < credits_required)
				break;

			ep->cred_dist.credits -= credits_required;
			ep->ep_st.cred_cosumd += credits_required;

			/* check if we need credits back from the target */
			if (ep->cred_dist.credits <
					ep->cred_dist.cred_per_msg) {
				/* tell the target we need credits ASAP! */
				send_flags |= HTC_FLAGS_NEED_CREDIT_UPDATE;
				ep->ep_st.cred_low_indicate += 1;
				ath6kl_dbg(ATH6KL_DBG_HTC,
					   "%s: host needs credits\n",
					   __func__);
			}
		}

		/* now we can fully dequeue */
		packet = list_first_entry(&ep->txq, struct htc_packet, list);

		list_del(&packet->list);
		/* save the number of credits this packet consumed */
		packet->info.tx.cred_used = credits_required;
		/* save send flags */
		packet->info.tx.flags = send_flags;
		packet->info.tx.seqno = ep->seqno;
		ep->seqno++;
		/* queue this packet into the caller's queue */
		list_add_tail(&packet->list, queue);
	}

}

static void get_htc_packet(struct htc_target *target,
			   struct htc_endpoint *ep,
			   struct list_head *queue, int resources)
{
	struct htc_packet *packet;

	/* NOTE : the TX lock is held when this function is called */

	/* loop until we can grab as many packets out of the queue as we can */
	while (resources) {
		if (list_empty(&ep->txq))
			break;

		packet = list_first_entry(&ep->txq, struct htc_packet, list);
		list_del(&packet->list);

		ath6kl_dbg(ATH6KL_DBG_HTC,
			   "%s: got packet:0x%p , new queue depth: %d\n",
			   __func__, packet, get_queue_depth(&ep->txq));
		packet->info.tx.seqno = ep->seqno;
		packet->info.tx.flags = 0;
		packet->info.tx.cred_used = 0;
		ep->seqno++;

		/* queue this packet into the caller's queue */
		list_add_tail(&packet->list, queue);
		resources--;
	}
}

static int htc_issue_packets(struct htc_target *target,
			     struct htc_endpoint *ep,
			     struct list_head *pkt_queue)
{
	int status = 0;
	u16 payload_len;
	struct sk_buff *skb;
	struct htc_frame_hdr *htc_hdr;
	struct htc_packet *packet;

	ath6kl_dbg(ATH6KL_DBG_HTC,
		   "%s: queue: 0x%p, pkts %d\n", __func__,
		   pkt_queue, get_queue_depth(pkt_queue));

	while (!list_empty(pkt_queue)) {
		packet = list_first_entry(pkt_queue, struct htc_packet, list);
		list_del(&packet->list);

		skb = packet->skb;
		if (!skb) {
			WARN_ON_ONCE(1);
			status = -EINVAL;
			break;
		}

		payload_len = packet->act_len;

		/* setup HTC frame header */
		htc_hdr = (struct htc_frame_hdr *) skb_push(skb,
							    sizeof(*htc_hdr));
		if (!htc_hdr) {
			WARN_ON_ONCE(1);
			status = -EINVAL;
			break;
		}

		packet->info.tx.flags |= HTC_FLAGS_TX_FIXUP_NETBUF;

		/* Endianess? */
		put_unaligned((u16) payload_len, &htc_hdr->payld_len);
		htc_hdr->flags = packet->info.tx.flags;
		htc_hdr->eid = (u8) packet->endpoint;
		htc_hdr->ctrl[0] = 0;
		htc_hdr->ctrl[1] = (u8) packet->info.tx.seqno;

		spin_lock_bh(&target->tx_lock);

		/* store in look up queue to match completions */
		list_add_tail(&packet->list, &ep->pipe.tx_lookup_queue);
		ep->ep_st.tx_issued += 1;
		spin_unlock_bh(&target->tx_lock);

		status = ath6kl_hif_pipe_send(target->dev->ar,
					      ep->pipe.pipeid_ul, NULL, skb);

		if (status != 0) {
			if (status != -ENOMEM) {
				/* TODO: if more than 1 endpoint maps to the
				 * same PipeID, it is possible to run out of
				 * resources in the HIF layer.
				 * Don't emit the error
				 */
				ath6kl_dbg(ATH6KL_DBG_HTC,
					   "%s: failed status:%d\n",
					   __func__, status);
			}
			spin_lock_bh(&target->tx_lock);
			list_del(&packet->list);

			/* reclaim credits */
			ep->cred_dist.credits += packet->info.tx.cred_used;
			spin_unlock_bh(&target->tx_lock);

			/* put it back into the callers queue */
			list_add(&packet->list, pkt_queue);
			break;
		}

	}

	if (status != 0) {
		while (!list_empty(pkt_queue)) {
			if (status != -ENOMEM) {
				ath6kl_dbg(ATH6KL_DBG_HTC,
					   "%s: failed pkt:0x%p status:%d\n",
					   __func__, packet, status);
			}

			packet = list_first_entry(pkt_queue,
						  struct htc_packet, list);
			list_del(&packet->list);
			packet->status = status;
			send_packet_completion(target, packet);
		}
	}

	return status;
}

static enum htc_send_queue_result htc_try_send(struct htc_target *target,
					       struct htc_endpoint *ep,
					       struct list_head *txq)
{
	struct list_head send_queue;	/* temp queue to hold packets */
	struct htc_packet *packet, *tmp_pkt;
	struct ath6kl *ar = target->dev->ar;
	enum htc_send_full_action action;
	int tx_resources, overflow, txqueue_depth, i, good_pkts;
	u8 pipeid;

	ath6kl_dbg(ATH6KL_DBG_HTC, "%s: (queue:0x%p depth:%d)\n",
		   __func__, txq,
		   (txq == NULL) ? 0 : get_queue_depth(txq));

	/* init the local send queue */
	INIT_LIST_HEAD(&send_queue);

	/*
	 * txq equals to NULL means
	 * caller didn't provide a queue, just wants us to
	 * check queues and send
	 */
	if (txq != NULL) {
		if (list_empty(txq)) {
			/* empty queue */
			return HTC_SEND_QUEUE_DROP;
		}

		spin_lock_bh(&target->tx_lock);
		txqueue_depth = get_queue_depth(&ep->txq);
		spin_unlock_bh(&target->tx_lock);

		if (txqueue_depth >= ep->max_txq_depth) {
			/* we've already overflowed */
			overflow = get_queue_depth(txq);
		} else {
			/* get how much we will overflow by */
			overflow = txqueue_depth;
			overflow += get_queue_depth(txq);
			/* get how much we will overflow the TX queue by */
			overflow -= ep->max_txq_depth;
		}

		/* if overflow is negative or zero, we are okay */
		if (overflow > 0) {
			ath6kl_dbg(ATH6KL_DBG_HTC,
				   "%s: Endpoint %d, TX queue will overflow :%d, Tx Depth:%d, Max:%d\n",
				   __func__, ep->eid, overflow, txqueue_depth,
				   ep->max_txq_depth);
		}
		if ((overflow <= 0) ||
		    (ep->ep_cb.tx_full == NULL)) {
			/*
			 * all packets will fit or caller did not provide send
			 * full indication handler -- just move all of them
			 * to the local send_queue object
			 */
			list_splice_tail_init(txq, &send_queue);
		} else {
			good_pkts = get_queue_depth(txq) - overflow;
			if (good_pkts < 0) {
				WARN_ON_ONCE(1);
				return HTC_SEND_QUEUE_DROP;
			}

			/* we have overflowed, and a callback is provided */
			/* dequeue all non-overflow packets to the sendqueue */
			for (i = 0; i < good_pkts; i++) {
				/* pop off caller's queue */
				packet = list_first_entry(txq,
							  struct htc_packet,
							  list);
				list_del(&packet->list);
				/* insert into local queue */
				list_add_tail(&packet->list, &send_queue);
			}

			/*
			 * the caller's queue has all the packets that won't fit
			 * walk through the caller's queue and indicate each to
			 * the send full handler
			 */
			list_for_each_entry_safe(packet, tmp_pkt,
						 txq, list) {

				ath6kl_dbg(ATH6KL_DBG_HTC,
					   "%s: Indicat overflowed TX pkts: %p\n",
					   __func__, packet);
				action = ep->ep_cb.tx_full(ep->target, packet);
				if (action == HTC_SEND_FULL_DROP) {
					/* callback wants the packet dropped */
					ep->ep_st.tx_dropped += 1;

					/* leave this one in the caller's queue
					 * for cleanup */
				} else {
					/* callback wants to keep this packet,
					 * remove from caller's queue */
					list_del(&packet->list);
					/* put it in the send queue */
					list_add_tail(&packet->list,
						      &send_queue);
				}

			}

			if (list_empty(&send_queue)) {
				/* no packets made it in, caller will cleanup */
				return HTC_SEND_QUEUE_DROP;
			}
		}
	}

	if (!ep->pipe.tx_credit_flow_enabled) {
		tx_resources =
		    ath6kl_hif_pipe_get_free_queue_number(ar,
							  ep->pipe.pipeid_ul);
	} else {
		tx_resources = 0;
	}

	spin_lock_bh(&target->tx_lock);
	if (!list_empty(&send_queue)) {
		/* transfer packets to tail */
		list_splice_tail_init(&send_queue, &ep->txq);
		if (!list_empty(&send_queue)) {
			WARN_ON_ONCE(1);
			spin_unlock_bh(&target->tx_lock);
			return HTC_SEND_QUEUE_DROP;
		}
		INIT_LIST_HEAD(&send_queue);
	}

	/* increment tx processing count on entry */
	ep->tx_proc_cnt++;

	if (ep->tx_proc_cnt > 1) {
		/*
		 * Another thread or task is draining the TX queues on this
		 * endpoint that thread will reset the tx processing count
		 * when the queue is drained.
		 */
		ep->tx_proc_cnt--;
		spin_unlock_bh(&target->tx_lock);
		return HTC_SEND_QUEUE_OK;
	}

	/***** beyond this point only 1 thread may enter ******/

	/*
	 * Now drain the endpoint TX queue for transmission as long as we have
	 * enough transmit resources.
	 */
	while (true) {

		if (get_queue_depth(&ep->txq) == 0)
			break;

		if (ep->pipe.tx_credit_flow_enabled) {
			/*
			 * Credit based mechanism provides flow control
			 * based on target transmit resource availability,
			 * we assume that the HIF layer will always have
			 * bus resources greater than target transmit
			 * resources.
			 */
			get_htc_packet_credit_based(target, ep, &send_queue);
		} else {
			/*
			 * Get all packets for this endpoint that we can
			 * for this pass.
			 */
			get_htc_packet(target, ep, &send_queue, tx_resources);
		}

		if (get_queue_depth(&send_queue) == 0) {
			/*
			 * Didn't get packets due to out of resources or TX
			 * queue was drained.
			 */
			break;
		}

		spin_unlock_bh(&target->tx_lock);

		/* send what we can */
		htc_issue_packets(target, ep, &send_queue);

		if (!ep->pipe.tx_credit_flow_enabled) {
			pipeid = ep->pipe.pipeid_ul;
			tx_resources =
			    ath6kl_hif_pipe_get_free_queue_number(ar, pipeid);
		}

		spin_lock_bh(&target->tx_lock);

	}
	/* done with this endpoint, we can clear the count */
	ep->tx_proc_cnt = 0;
	spin_unlock_bh(&target->tx_lock);

	return HTC_SEND_QUEUE_OK;
}

/* htc control packet manipulation */
static void destroy_htc_txctrl_packet(struct htc_packet *packet)
{
	struct sk_buff *skb;
	skb = packet->skb;
	if (skb != NULL)
		dev_kfree_skb(skb);

	kfree(packet);
}

static struct htc_packet *build_htc_txctrl_packet(void)
{
	struct htc_packet *packet = NULL;
	struct sk_buff *skb;

	packet = kzalloc(sizeof(struct htc_packet), GFP_KERNEL);
	if (packet == NULL)
		return NULL;

	skb = __dev_alloc_skb(HTC_CONTROL_BUFFER_SIZE, GFP_KERNEL);

	if (skb == NULL) {
		kfree(packet);
		return NULL;
	}
	packet->skb = skb;

	return packet;
}

static void htc_free_txctrl_packet(struct htc_target *target,
				   struct htc_packet *packet)
{
	destroy_htc_txctrl_packet(packet);
}

static struct htc_packet *htc_alloc_txctrl_packet(struct htc_target *target)
{
	return build_htc_txctrl_packet();
}

static void htc_txctrl_complete(struct htc_target *target,
				struct htc_packet *packet)
{
	htc_free_txctrl_packet(target, packet);
}

#define MAX_MESSAGE_SIZE 1536

static int htc_setup_target_buffer_assignments(struct htc_target *target)
{
	int status, credits, credit_per_maxmsg, i;
	struct htc_pipe_txcredit_alloc *entry;
	unsigned int hif_usbaudioclass = 0;

	credit_per_maxmsg = MAX_MESSAGE_SIZE / target->tgt_cred_sz;
	if (MAX_MESSAGE_SIZE % target->tgt_cred_sz)
		credit_per_maxmsg++;

	/* TODO, this should be configured by the caller! */

	credits = target->tgt_creds;
	entry = &target->pipe.txcredit_alloc[0];

	status = -ENOMEM;

	/* FIXME: hif_usbaudioclass is always zero */
	if (hif_usbaudioclass) {
		ath6kl_dbg(ATH6KL_DBG_HTC,
			   "%s: For USB Audio Class- Total:%d\n",
			   __func__, credits);
		entry++;
		entry++;
		/* Setup VO Service To have Max Credits */
		entry->service_id = WMI_DATA_VO_SVC;
		entry->credit_alloc = (credits - 6);
		if (entry->credit_alloc == 0)
			entry->credit_alloc++;

		credits -= (int) entry->credit_alloc;
		if (credits <= 0)
			return status;

		entry++;
		entry->service_id = WMI_CONTROL_SVC;
		entry->credit_alloc = credit_per_maxmsg;
		credits -= (int) entry->credit_alloc;
		if (credits <= 0)
			return status;

		/* leftovers go to best effort */
		entry++;
		entry++;
		entry->service_id = WMI_DATA_BE_SVC;
		entry->credit_alloc = (u8) credits;
		status = 0;
	} else {
		entry++;
		entry->service_id = WMI_DATA_VI_SVC;
		entry->credit_alloc = credits / 4;
		if (entry->credit_alloc == 0)
			entry->credit_alloc++;

		credits -= (int) entry->credit_alloc;
		if (credits <= 0)
			return status;

		entry++;
		entry->service_id = WMI_DATA_VO_SVC;
		entry->credit_alloc = credits / 4;
		if (entry->credit_alloc == 0)
			entry->credit_alloc++;

		credits -= (int) entry->credit_alloc;
		if (credits <= 0)
			return status;

		entry++;
		entry->service_id = WMI_CONTROL_SVC;
		entry->credit_alloc = credit_per_maxmsg;
		credits -= (int) entry->credit_alloc;
		if (credits <= 0)
			return status;

		entry++;
		entry->service_id = WMI_DATA_BK_SVC;
		entry->credit_alloc = credit_per_maxmsg;
		credits -= (int) entry->credit_alloc;
		if (credits <= 0)
			return status;

		/* leftovers go to best effort */
		entry++;
		entry->service_id = WMI_DATA_BE_SVC;
		entry->credit_alloc = (u8) credits;
		status = 0;
	}

	if (status == 0) {
		for (i = 0; i < ENDPOINT_MAX; i++) {
			if (target->pipe.txcredit_alloc[i].service_id != 0) {
				ath6kl_dbg(ATH6KL_DBG_HTC,
					   "HTC Service Index : %d TX : 0x%2.2X : alloc:%d\n",
					   i,
					   target->pipe.txcredit_alloc[i].
					   service_id,
					   target->pipe.txcredit_alloc[i].
					   credit_alloc);
			}
		}
	}
	return status;
}

/* process credit reports and call distribution function */
static void htc_process_credit_report(struct htc_target *target,
				      struct htc_credit_report *rpt,
				      int num_entries,
				      enum htc_endpoint_id from_ep)
{
	int total_credits = 0, i;
	struct htc_endpoint *ep;

	/* lock out TX while we update credits */
	spin_lock_bh(&target->tx_lock);

	for (i = 0; i < num_entries; i++, rpt++) {
		if (rpt->eid >= ENDPOINT_MAX) {
			WARN_ON_ONCE(1);
			spin_unlock_bh(&target->tx_lock);
			return;
		}

		ep = &target->endpoint[rpt->eid];
		ep->cred_dist.credits += rpt->credits;

		if (ep->cred_dist.credits && get_queue_depth(&ep->txq)) {
			spin_unlock_bh(&target->tx_lock);
			htc_try_send(target, ep, NULL);
			spin_lock_bh(&target->tx_lock);
		}

		total_credits += rpt->credits;
	}
	ath6kl_dbg(ATH6KL_DBG_HTC,
		   "Report indicated %d credits to distribute\n",
		   total_credits);

	spin_unlock_bh(&target->tx_lock);
}

/* flush endpoint TX queue */
static void htc_flush_tx_endpoint(struct htc_target *target,
				  struct htc_endpoint *ep, u16 tag)
{
	struct htc_packet *packet;

	spin_lock_bh(&target->tx_lock);
	while (get_queue_depth(&ep->txq)) {
		packet = list_first_entry(&ep->txq, struct htc_packet, list);
		list_del(&packet->list);
		packet->status = 0;
		send_packet_completion(target, packet);
	}
	spin_unlock_bh(&target->tx_lock);
}

/*
 * In the adapted HIF layer, struct sk_buff * are passed between HIF and HTC,
 * since upper layers expects struct htc_packet containers we use the completed
 * skb and lookup it's corresponding HTC packet buffer from a lookup list.
 * This is extra overhead that can be fixed by re-aligning HIF interfaces with
 * HTC.
 */
static struct htc_packet *htc_lookup_tx_packet(struct htc_target *target,
					       struct htc_endpoint *ep,
					       struct sk_buff *skb)
{
	struct htc_packet *packet, *tmp_pkt, *found_packet = NULL;

	spin_lock_bh(&target->tx_lock);

	/*
	 * interate from the front of tx lookup queue
	 * this lookup should be fast since lower layers completes in-order and
	 * so the completed packet should be at the head of the list generally
	 */
	list_for_each_entry_safe(packet, tmp_pkt, &ep->pipe.tx_lookup_queue,
				 list) {
		/* check for removal */
		if (skb == packet->skb) {
			/* found it */
			list_del(&packet->list);
			found_packet = packet;
			break;
		}
	}

	spin_unlock_bh(&target->tx_lock);

	return found_packet;
}

static int ath6kl_htc_pipe_tx_complete(struct ath6kl *ar, struct sk_buff *skb)
{
	struct htc_target *target = ar->htc_target;
	struct htc_frame_hdr *htc_hdr;
	struct htc_endpoint *ep;
	struct htc_packet *packet;
	u8 ep_id, *netdata;
	u32 netlen;

	netdata = skb->data;
	netlen = skb->len;

	htc_hdr = (struct htc_frame_hdr *) netdata;

	ep_id = htc_hdr->eid;
	ep = &target->endpoint[ep_id];

	packet = htc_lookup_tx_packet(target, ep, skb);
	if (packet == NULL) {
		/* may have already been flushed and freed */
		ath6kl_err("HTC TX lookup failed!\n");
	} else {
		/* will be giving this buffer back to upper layers */
		packet->status = 0;
		send_packet_completion(target, packet);
	}
	skb = NULL;

	if (!ep->pipe.tx_credit_flow_enabled) {
		/*
		 * note: when using TX credit flow, the re-checking of queues
		 * happens when credits flow back from the target. in the
		 * non-TX credit case, we recheck after the packet completes
		 */
		htc_try_send(target, ep, NULL);
	}

	return 0;
}

static int htc_send_packets_multiple(struct htc_target *target,
				     struct list_head *pkt_queue)
{
	struct htc_endpoint *ep;
	struct htc_packet *packet, *tmp_pkt;

	if (list_empty(pkt_queue))
		return -EINVAL;

	/* get first packet to find out which ep the packets will go into */
	packet = list_first_entry(pkt_queue, struct htc_packet, list);
	if (packet == NULL)
		return -EINVAL;

	if (packet->endpoint >= ENDPOINT_MAX) {
		WARN_ON_ONCE(1);
		return -EINVAL;
	}
	ep = &target->endpoint[packet->endpoint];

	htc_try_send(target, ep, pkt_queue);

	/* do completion on any packets that couldn't get in */
	if (!list_empty(pkt_queue)) {
		list_for_each_entry_safe(packet, tmp_pkt, pkt_queue, list) {
			packet->status = -ENOMEM;
		}

		do_send_completion(ep, pkt_queue);
	}

	return 0;
}

/* htc pipe rx path */
static struct htc_packet *alloc_htc_packet_container(struct htc_target *target)
{
	struct htc_packet *packet;
	spin_lock_bh(&target->rx_lock);

	if (target->pipe.htc_packet_pool == NULL) {
		spin_unlock_bh(&target->rx_lock);
		return NULL;
	}

	packet = target->pipe.htc_packet_pool;
	target->pipe.htc_packet_pool = (struct htc_packet *) packet->list.next;

	spin_unlock_bh(&target->rx_lock);

	packet->list.next = NULL;
	return packet;
}

static void free_htc_packet_container(struct htc_target *target,
				      struct htc_packet *packet)
{
	struct list_head *lh;

	spin_lock_bh(&target->rx_lock);

	if (target->pipe.htc_packet_pool == NULL) {
		target->pipe.htc_packet_pool = packet;
		packet->list.next = NULL;
	} else {
		lh = (struct list_head *) target->pipe.htc_packet_pool;
		packet->list.next = lh;
		target->pipe.htc_packet_pool = packet;
	}

	spin_unlock_bh(&target->rx_lock);
}

static int htc_process_trailer(struct htc_target *target, u8 *buffer,
			       int len, enum htc_endpoint_id from_ep)
{
	struct htc_credit_report *report;
	struct htc_record_hdr *record;
	u8 *record_buf, *orig_buf;
	int orig_len, status;

	orig_buf = buffer;
	orig_len = len;
	status = 0;

	while (len > 0) {
		if (len < sizeof(struct htc_record_hdr)) {
			status = -EINVAL;
			break;
		}

		/* these are byte aligned structs */
		record = (struct htc_record_hdr *) buffer;
		len -= sizeof(struct htc_record_hdr);
		buffer += sizeof(struct htc_record_hdr);

		if (record->len > len) {
			/* no room left in buffer for record */
			ath6kl_dbg(ATH6KL_DBG_HTC,
				   "invalid length: %d (id:%d) buffer has: %d bytes left\n",
				   record->len, record->rec_id, len);
			status = -EINVAL;
			break;
		}

		/* start of record follows the header */
		record_buf = buffer;

		switch (record->rec_id) {
		case HTC_RECORD_CREDITS:
			if (record->len < sizeof(struct htc_credit_report)) {
				WARN_ON_ONCE(1);
				return -EINVAL;
			}

			report = (struct htc_credit_report *) record_buf;
			htc_process_credit_report(target, report,
						  record->len / sizeof(*report),
						  from_ep);
			break;
		default:
			ath6kl_dbg(ATH6KL_DBG_HTC,
				   "unhandled record: id:%d length:%d\n",
				   record->rec_id, record->len);
			break;
		}

		if (status != 0)
			break;

		/* advance buffer past this record for next time around */
		buffer += record->len;
		len -= record->len;
	}

	return status;
}

static void do_recv_completion(struct htc_endpoint *ep,
			       struct list_head *queue_to_indicate)
{
	struct htc_packet *packet;

	if (list_empty(queue_to_indicate)) {
		/* nothing to indicate */
		return;
	}

	/* using legacy EpRecv */
	while (!list_empty(queue_to_indicate)) {
		packet = list_first_entry(queue_to_indicate,
					  struct htc_packet, list);
		list_del(&packet->list);
		ep->ep_cb.rx(ep->target, packet);
	}

	return;
}

static void recv_packet_completion(struct htc_target *target,
				   struct htc_endpoint *ep,
				   struct htc_packet *packet)
{
	struct list_head container;
	INIT_LIST_HEAD(&container);
	list_add_tail(&packet->list, &container);

	/* do completion */
	do_recv_completion(ep, &container);
}

static int ath6kl_htc_pipe_rx_complete(struct ath6kl *ar, struct sk_buff *skb,
				       u8 pipeid)
{
	struct htc_target *target = ar->htc_target;
	u8 *netdata, *trailer, hdr_info;
	struct htc_frame_hdr *htc_hdr;
	u32 netlen, trailerlen = 0;
	struct htc_packet *packet;
	struct htc_endpoint *ep;
	u16 payload_len;
	int status = 0;

	netdata = skb->data;
	netlen = skb->len;

	htc_hdr = (struct htc_frame_hdr *) netdata;

	ep = &target->endpoint[htc_hdr->eid];

	if (htc_hdr->eid >= ENDPOINT_MAX) {
		ath6kl_dbg(ATH6KL_DBG_HTC,
			   "HTC Rx: invalid EndpointID=%d\n",
			   htc_hdr->eid);
		status = -EINVAL;
		goto free_skb;
	}

	payload_len = le16_to_cpu(get_unaligned(&htc_hdr->payld_len));

	if (netlen < (payload_len + HTC_HDR_LENGTH)) {
		ath6kl_dbg(ATH6KL_DBG_HTC,
			   "HTC Rx: insufficient length, got:%d expected =%u\n",
			   netlen, payload_len + HTC_HDR_LENGTH);
		status = -EINVAL;
		goto free_skb;
	}

	/* get flags to check for trailer */
	hdr_info = htc_hdr->flags;
	if (hdr_info & HTC_FLG_RX_TRAILER) {
		/* extract the trailer length */
		hdr_info = htc_hdr->ctrl[0];
		if ((hdr_info < sizeof(struct htc_record_hdr)) ||
		    (hdr_info > payload_len)) {
			ath6kl_dbg(ATH6KL_DBG_HTC,
				   "invalid header: payloadlen should be %d, CB[0]: %d\n",
				   payload_len, hdr_info);
			status = -EINVAL;
			goto free_skb;
		}

		trailerlen = hdr_info;
		/* process trailer after hdr/apps payload */
		trailer = (u8 *) htc_hdr + HTC_HDR_LENGTH +
			payload_len - hdr_info;
		status = htc_process_trailer(target, trailer, hdr_info,
					     htc_hdr->eid);
		if (status != 0)
			goto free_skb;
	}

	if (((int) payload_len - (int) trailerlen) <= 0) {
		/* zero length packet with trailer, just drop these */
		goto free_skb;
	}

	if (htc_hdr->eid == ENDPOINT_0) {
		/* handle HTC control message */
		if (target->htc_flags & HTC_OP_STATE_SETUP_COMPLETE) {
			/*
			 * fatal: target should not send unsolicited
			 * messageson the endpoint 0
			 */
			ath6kl_dbg(ATH6KL_DBG_HTC,
				   "HTC ignores Rx Ctrl after setup complete\n");
			status = -EINVAL;
			goto free_skb;
		}

		/* remove HTC header */
		skb_pull(skb, HTC_HDR_LENGTH);

		netdata = skb->data;
		netlen = skb->len;

		spin_lock_bh(&target->rx_lock);

		target->pipe.ctrl_response_valid = true;
		target->pipe.ctrl_response_len = min_t(int, netlen,
						       HTC_MAX_CTRL_MSG_LEN);
		memcpy(target->pipe.ctrl_response_buf, netdata,
		       target->pipe.ctrl_response_len);

		spin_unlock_bh(&target->rx_lock);

		dev_kfree_skb(skb);
		skb = NULL;
		goto free_skb;
	}

	/*
	 * TODO: the message based HIF architecture allocates net bufs
	 * for recv packets since it bridges that HIF to upper layers,
	 * which expects HTC packets, we form the packets here
	 */
	packet = alloc_htc_packet_container(target);
	if (packet == NULL) {
		status = -ENOMEM;
		goto free_skb;
	}

	packet->status = 0;
	packet->endpoint = htc_hdr->eid;
	packet->pkt_cntxt = skb;

	/* TODO: for backwards compatibility */
	packet->buf = skb_push(skb, 0) + HTC_HDR_LENGTH;
	packet->act_len = netlen - HTC_HDR_LENGTH - trailerlen;

	/*
	 * TODO: this is a hack because the driver layer will set the
	 * actual len of the skb again which will just double the len
	 */
	skb_trim(skb, 0);

	recv_packet_completion(target, ep, packet);

	/* recover the packet container */
	free_htc_packet_container(target, packet);
	skb = NULL;

free_skb:
	if (skb != NULL)
		dev_kfree_skb(skb);

	return status;

}

static void htc_flush_rx_queue(struct htc_target *target,
			       struct htc_endpoint *ep)
{
	struct list_head container;
	struct htc_packet *packet;

	spin_lock_bh(&target->rx_lock);

	while (1) {
		if (list_empty(&ep->rx_bufq))
			break;

		packet = list_first_entry(&ep->rx_bufq,
					  struct htc_packet, list);
		list_del(&packet->list);

		spin_unlock_bh(&target->rx_lock);
		packet->status = -ECANCELED;
		packet->act_len = 0;

		ath6kl_dbg(ATH6KL_DBG_HTC,
			   "Flushing RX packet:0x%p, length:%d, ep:%d\n",
			   packet, packet->buf_len,
			   packet->endpoint);

		INIT_LIST_HEAD(&container);
		list_add_tail(&packet->list, &container);

		/* give the packet back */
		do_recv_completion(ep, &container);
		spin_lock_bh(&target->rx_lock);
	}

	spin_unlock_bh(&target->rx_lock);
}

/* polling routine to wait for a control packet to be received */
static int htc_wait_recv_ctrl_message(struct htc_target *target)
{
	int count = HTC_TARGET_RESPONSE_POLL_COUNT;

	while (count > 0) {
		spin_lock_bh(&target->rx_lock);

		if (target->pipe.ctrl_response_valid) {
			target->pipe.ctrl_response_valid = false;
			spin_unlock_bh(&target->rx_lock);
			break;
		}

		spin_unlock_bh(&target->rx_lock);

		count--;

		msleep_interruptible(HTC_TARGET_RESPONSE_POLL_WAIT);
	}

	if (count <= 0) {
		ath6kl_dbg(ATH6KL_DBG_HTC, "%s: Timeout!\n", __func__);
		return -ECOMM;
	}

	return 0;
}

static void htc_rxctrl_complete(struct htc_target *context,
				struct htc_packet *packet)
{
	/* TODO, can't really receive HTC control messages yet.... */
	ath6kl_dbg(ATH6KL_DBG_HTC, "%s: invalid call function\n", __func__);
}

/* htc pipe initialization */
static void reset_endpoint_states(struct htc_target *target)
{
	struct htc_endpoint *ep;
	int i;

	for (i = ENDPOINT_0; i < ENDPOINT_MAX; i++) {
		ep = &target->endpoint[i];
		ep->svc_id = 0;
		ep->len_max = 0;
		ep->max_txq_depth = 0;
		ep->eid = i;
		INIT_LIST_HEAD(&ep->txq);
		INIT_LIST_HEAD(&ep->pipe.tx_lookup_queue);
		INIT_LIST_HEAD(&ep->rx_bufq);
		ep->target = target;
		ep->pipe.tx_credit_flow_enabled = (bool) 1; /* FIXME */
	}
}

/* start HTC, this is called after all services are connected */
static int htc_config_target_hif_pipe(struct htc_target *target)
{
	return 0;
}

/* htc service functions */
static u8 htc_get_credit_alloc(struct htc_target *target, u16 service_id)
{
	u8 allocation = 0;
	int i;

	for (i = 0; i < ENDPOINT_MAX; i++) {
		if (target->pipe.txcredit_alloc[i].service_id == service_id)
			allocation =
				target->pipe.txcredit_alloc[i].credit_alloc;
	}

	if (allocation == 0) {
		ath6kl_dbg(ATH6KL_DBG_HTC,
			   "HTC Service TX : 0x%2.2X : allocation is zero!\n",
			   service_id);
	}

	return allocation;
}

static int ath6kl_htc_pipe_conn_service(struct htc_target *target,
		     struct htc_service_connect_req *conn_req,
		     struct htc_service_connect_resp *conn_resp)
{
	struct ath6kl *ar = target->dev->ar;
	struct htc_packet *packet = NULL;
	struct htc_conn_service_resp *resp_msg;
	struct htc_conn_service_msg *conn_msg;
	enum htc_endpoint_id assigned_epid = ENDPOINT_MAX;
	bool disable_credit_flowctrl = false;
	unsigned int max_msg_size = 0;
	struct htc_endpoint *ep;
	int length, status = 0;
	struct sk_buff *skb;
	u8 tx_alloc;
	u16 flags;

	if (conn_req->svc_id == 0) {
		WARN_ON_ONCE(1);
		status = -EINVAL;
		goto free_packet;
	}

	if (conn_req->svc_id == HTC_CTRL_RSVD_SVC) {
		/* special case for pseudo control service */
		assigned_epid = ENDPOINT_0;
		max_msg_size = HTC_MAX_CTRL_MSG_LEN;
		tx_alloc = 0;

	} else {

		tx_alloc = htc_get_credit_alloc(target, conn_req->svc_id);
		if (tx_alloc == 0) {
			status = -ENOMEM;
			goto free_packet;
		}

		/* allocate a packet to send to the target */
		packet = htc_alloc_txctrl_packet(target);

		if (packet == NULL) {
			WARN_ON_ONCE(1);
			status = -ENOMEM;
			goto free_packet;
		}

		skb = packet->skb;
		length = sizeof(struct htc_conn_service_msg);

		/* assemble connect service message */
		conn_msg = (struct htc_conn_service_msg *) skb_put(skb,
								   length);
		if (conn_msg == NULL) {
			WARN_ON_ONCE(1);
			status = -EINVAL;
			goto free_packet;
		}

		memset(conn_msg, 0,
		       sizeof(struct htc_conn_service_msg));
		conn_msg->msg_id = cpu_to_le16(HTC_MSG_CONN_SVC_ID);
		conn_msg->svc_id = cpu_to_le16(conn_req->svc_id);
		conn_msg->conn_flags = cpu_to_le16(conn_req->conn_flags &
					~HTC_CONN_FLGS_SET_RECV_ALLOC_MASK);

		/* tell target desired recv alloc for this ep */
		flags = tx_alloc << HTC_CONN_FLGS_SET_RECV_ALLOC_SHIFT;
		conn_msg->conn_flags |= cpu_to_le16(flags);

		if (conn_req->conn_flags &
		    HTC_CONN_FLGS_DISABLE_CRED_FLOW_CTRL) {
			disable_credit_flowctrl = true;
		}

		set_htc_pkt_info(packet, NULL, (u8 *) conn_msg,
				 length,
				 ENDPOINT_0, HTC_SERVICE_TX_PACKET_TAG);

		status = ath6kl_htc_pipe_tx(target, packet);

		/* we don't own it anymore */
		packet = NULL;
		if (status != 0)
			goto free_packet;

		/* wait for response */
		status = htc_wait_recv_ctrl_message(target);
		if (status != 0)
			goto free_packet;

		/* we controlled the buffer creation so it has to be
		 * properly aligned
		 */
		resp_msg = (struct htc_conn_service_resp *)
		    target->pipe.ctrl_response_buf;

		if (resp_msg->msg_id != cpu_to_le16(HTC_MSG_CONN_SVC_RESP_ID) ||
		    (target->pipe.ctrl_response_len < sizeof(*resp_msg))) {
			/* this message is not valid */
			WARN_ON_ONCE(1);
			status = -EINVAL;
			goto free_packet;
		}

		ath6kl_dbg(ATH6KL_DBG_TRC,
			   "%s: service 0x%X conn resp: status: %d ep: %d\n",
			   __func__, resp_msg->svc_id, resp_msg->status,
			   resp_msg->eid);

		conn_resp->resp_code = resp_msg->status;
		/* check response status */
		if (resp_msg->status != HTC_SERVICE_SUCCESS) {
			ath6kl_dbg(ATH6KL_DBG_HTC,
				   "Target failed service 0x%X connect request (status:%d)\n",
				   resp_msg->svc_id, resp_msg->status);
			status = -EINVAL;
			goto free_packet;
		}

		assigned_epid = (enum htc_endpoint_id) resp_msg->eid;
		max_msg_size = le16_to_cpu(resp_msg->max_msg_sz);
	}

	/* the rest are parameter checks so set the error status */
	status = -EINVAL;

	if (assigned_epid >= ENDPOINT_MAX) {
		WARN_ON_ONCE(1);
		goto free_packet;
	}

	if (max_msg_size == 0) {
		WARN_ON_ONCE(1);
		goto free_packet;
	}

	ep = &target->endpoint[assigned_epid];
	ep->eid = assigned_epid;
	if (ep->svc_id != 0) {
		/* endpoint already in use! */
		WARN_ON_ONCE(1);
		goto free_packet;
	}

	/* return assigned endpoint to caller */
	conn_resp->endpoint = assigned_epid;
	conn_resp->len_max = max_msg_size;

	/* setup the endpoint */
	ep->svc_id = conn_req->svc_id; /* this marks ep in use */
	ep->max_txq_depth = conn_req->max_txq_depth;
	ep->len_max = max_msg_size;
	ep->cred_dist.credits = tx_alloc;
	ep->cred_dist.cred_sz = target->tgt_cred_sz;
	ep->cred_dist.cred_per_msg = max_msg_size / target->tgt_cred_sz;
	if (max_msg_size % target->tgt_cred_sz)
		ep->cred_dist.cred_per_msg++;

	/* copy all the callbacks */
	ep->ep_cb = conn_req->ep_cb;

	status = ath6kl_hif_pipe_map_service(ar, ep->svc_id,
					     &ep->pipe.pipeid_ul,
					     &ep->pipe.pipeid_dl);
	if (status != 0)
		goto free_packet;

	ath6kl_dbg(ATH6KL_DBG_HTC,
		   "SVC Ready: 0x%4.4X: ULpipe:%d DLpipe:%d id:%d\n",
		   ep->svc_id, ep->pipe.pipeid_ul,
		   ep->pipe.pipeid_dl, ep->eid);

	if (disable_credit_flowctrl && ep->pipe.tx_credit_flow_enabled) {
		ep->pipe.tx_credit_flow_enabled = false;
		ath6kl_dbg(ATH6KL_DBG_HTC,
			   "SVC: 0x%4.4X ep:%d TX flow control off\n",
			   ep->svc_id, assigned_epid);
	}

free_packet:
	if (packet != NULL)
		htc_free_txctrl_packet(target, packet);
	return status;
}

/* htc export functions */
static void *ath6kl_htc_pipe_create(struct ath6kl *ar)
{
	int status = 0;
	struct htc_endpoint *ep = NULL;
	struct htc_target *target = NULL;
	struct htc_packet *packet;
	int i;

	target = kzalloc(sizeof(struct htc_target), GFP_KERNEL);
	if (target == NULL) {
		ath6kl_err("htc create unable to allocate memory\n");
		status = -ENOMEM;
		goto fail_htc_create;
	}

	spin_lock_init(&target->htc_lock);
	spin_lock_init(&target->rx_lock);
	spin_lock_init(&target->tx_lock);

	reset_endpoint_states(target);

	for (i = 0; i < HTC_PACKET_CONTAINER_ALLOCATION; i++) {
		packet = kzalloc(sizeof(struct htc_packet), GFP_KERNEL);

		if (packet != NULL)
			free_htc_packet_container(target, packet);
	}

	target->dev = kzalloc(sizeof(*target->dev), GFP_KERNEL);
	if (!target->dev) {
		ath6kl_err("unable to allocate memory\n");
		status = -ENOMEM;
		goto fail_htc_create;
	}
	target->dev->ar = ar;
	target->dev->htc_cnxt = target;

	/* Get HIF default pipe for HTC message exchange */
	ep = &target->endpoint[ENDPOINT_0];

	ath6kl_hif_pipe_get_default(ar, &ep->pipe.pipeid_ul,
				    &ep->pipe.pipeid_dl);

	return target;

fail_htc_create:
	if (status != 0) {
		if (target != NULL)
			ath6kl_htc_pipe_cleanup(target);

		target = NULL;
	}
	return target;
}

/* cleanup the HTC instance */
static void ath6kl_htc_pipe_cleanup(struct htc_target *target)
{
	struct htc_packet *packet;

	while (true) {
		packet = alloc_htc_packet_container(target);
		if (packet == NULL)
			break;
		kfree(packet);
	}

	kfree(target->dev);

	/* kfree our instance */
	kfree(target);
}

static int ath6kl_htc_pipe_start(struct htc_target *target)
{
	struct sk_buff *skb;
	struct htc_setup_comp_ext_msg *setup;
	struct htc_packet *packet;

	htc_config_target_hif_pipe(target);

	/* allocate a buffer to send */
	packet = htc_alloc_txctrl_packet(target);
	if (packet == NULL) {
		WARN_ON_ONCE(1);
		return -ENOMEM;
	}

	skb = packet->skb;

	/* assemble setup complete message */
	setup = (struct htc_setup_comp_ext_msg *) skb_put(skb,
							  sizeof(*setup));
	memset(setup, 0, sizeof(struct htc_setup_comp_ext_msg));
	setup->msg_id = cpu_to_le16(HTC_MSG_SETUP_COMPLETE_EX_ID);

	ath6kl_dbg(ATH6KL_DBG_HTC, "HTC using TX credit flow control\n");

	set_htc_pkt_info(packet, NULL, (u8 *) setup,
			 sizeof(struct htc_setup_comp_ext_msg),
			 ENDPOINT_0, HTC_SERVICE_TX_PACKET_TAG);

	target->htc_flags |= HTC_OP_STATE_SETUP_COMPLETE;

	return ath6kl_htc_pipe_tx(target, packet);
}

static void ath6kl_htc_pipe_stop(struct htc_target *target)
{
	int i;
	struct htc_endpoint *ep;

	/* cleanup endpoints */
	for (i = 0; i < ENDPOINT_MAX; i++) {
		ep = &target->endpoint[i];
		htc_flush_rx_queue(target, ep);
		htc_flush_tx_endpoint(target, ep, HTC_TX_PACKET_TAG_ALL);
	}

	reset_endpoint_states(target);
	target->htc_flags &= ~HTC_OP_STATE_SETUP_COMPLETE;
}

static int ath6kl_htc_pipe_get_rxbuf_num(struct htc_target *target,
					 enum htc_endpoint_id endpoint)
{
	int num;

	spin_lock_bh(&target->rx_lock);
	num = get_queue_depth(&(target->endpoint[endpoint].rx_bufq));
	spin_unlock_bh(&target->rx_lock);

	return num;
}

static int ath6kl_htc_pipe_tx(struct htc_target *target,
			      struct htc_packet *packet)
{
	struct list_head queue;

	ath6kl_dbg(ATH6KL_DBG_HTC,
		   "%s: endPointId: %d, buffer: 0x%p, length: %d\n",
		   __func__, packet->endpoint, packet->buf,
		   packet->act_len);

	INIT_LIST_HEAD(&queue);
	list_add_tail(&packet->list, &queue);

	return htc_send_packets_multiple(target, &queue);
}

static int ath6kl_htc_pipe_wait_target(struct htc_target *target)
{
	struct htc_ready_ext_msg *ready_msg;
	struct htc_service_connect_req connect;
	struct htc_service_connect_resp resp;
	int status = 0;

	status = htc_wait_recv_ctrl_message(target);

	if (status != 0)
		return status;

	if (target->pipe.ctrl_response_len < sizeof(*ready_msg)) {
		ath6kl_dbg(ATH6KL_DBG_HTC, "invalid htc ready msg len:%d!\n",
			   target->pipe.ctrl_response_len);
		return -ECOMM;
	}

	ready_msg = (struct htc_ready_ext_msg *) target->pipe.ctrl_response_buf;

	if (ready_msg->ver2_0_info.msg_id != cpu_to_le16(HTC_MSG_READY_ID)) {
		ath6kl_dbg(ATH6KL_DBG_HTC, "invalid htc ready msg : 0x%X !\n",
			   ready_msg->ver2_0_info.msg_id);
		return -ECOMM;
	}

	ath6kl_dbg(ATH6KL_DBG_HTC,
		   "Target Ready! : transmit resources : %d size:%d\n",
		   ready_msg->ver2_0_info.cred_cnt,
		   ready_msg->ver2_0_info.cred_sz);

	target->tgt_creds = le16_to_cpu(ready_msg->ver2_0_info.cred_cnt);
	target->tgt_cred_sz = le16_to_cpu(ready_msg->ver2_0_info.cred_sz);

	if ((target->tgt_creds == 0) || (target->tgt_cred_sz == 0))
		return -ECOMM;

	htc_setup_target_buffer_assignments(target);

	/* setup our pseudo HTC control endpoint connection */
	memset(&connect, 0, sizeof(connect));
	memset(&resp, 0, sizeof(resp));
	connect.ep_cb.tx_complete = htc_txctrl_complete;
	connect.ep_cb.rx = htc_rxctrl_complete;
	connect.max_txq_depth = NUM_CONTROL_TX_BUFFERS;
	connect.svc_id = HTC_CTRL_RSVD_SVC;

	/* connect fake service */
	status = ath6kl_htc_pipe_conn_service(target, &connect, &resp);

	return status;
}

static void ath6kl_htc_pipe_flush_txep(struct htc_target *target,
				       enum htc_endpoint_id endpoint, u16 tag)
{
	struct htc_endpoint *ep = &target->endpoint[endpoint];

	if (ep->svc_id == 0) {
		WARN_ON_ONCE(1);
		/* not in use.. */
		return;
	}

	htc_flush_tx_endpoint(target, ep, tag);
}

static int ath6kl_htc_pipe_add_rxbuf_multiple(struct htc_target *target,
					      struct list_head *pkt_queue)
{
	struct htc_packet *packet, *tmp_pkt, *first;
	struct htc_endpoint *ep;
	int status = 0;

	if (list_empty(pkt_queue))
		return -EINVAL;

	first = list_first_entry(pkt_queue, struct htc_packet, list);
	if (first == NULL) {
		WARN_ON_ONCE(1);
		return -EINVAL;
	}

	if (first->endpoint >= ENDPOINT_MAX) {
		WARN_ON_ONCE(1);
		return -EINVAL;
	}

	ath6kl_dbg(ATH6KL_DBG_HTC, "%s: epid: %d, cnt:%d, len: %d\n",
		   __func__, first->endpoint, get_queue_depth(pkt_queue),
		   first->buf_len);

	ep = &target->endpoint[first->endpoint];

	spin_lock_bh(&target->rx_lock);

	/* store receive packets */
	list_splice_tail_init(pkt_queue, &ep->rx_bufq);

	spin_unlock_bh(&target->rx_lock);

	if (status != 0) {
		/* walk through queue and mark each one canceled */
		list_for_each_entry_safe(packet, tmp_pkt, pkt_queue, list) {
			packet->status = -ECANCELED;
		}

		do_recv_completion(ep, pkt_queue);
	}

	return status;
}

static void ath6kl_htc_pipe_activity_changed(struct htc_target *target,
					     enum htc_endpoint_id ep,
					     bool active)
{
	/* TODO */
}

static void ath6kl_htc_pipe_flush_rx_buf(struct htc_target *target)
{
	/* TODO */
}

static int ath6kl_htc_pipe_credit_setup(struct htc_target *target,
					struct ath6kl_htc_credit_info *info)
{
	return 0;
}

static const struct ath6kl_htc_ops ath6kl_htc_pipe_ops = {
	.create = ath6kl_htc_pipe_create,
	.wait_target = ath6kl_htc_pipe_wait_target,
	.start = ath6kl_htc_pipe_start,
	.conn_service = ath6kl_htc_pipe_conn_service,
	.tx = ath6kl_htc_pipe_tx,
	.stop = ath6kl_htc_pipe_stop,
	.cleanup = ath6kl_htc_pipe_cleanup,
	.flush_txep = ath6kl_htc_pipe_flush_txep,
	.flush_rx_buf = ath6kl_htc_pipe_flush_rx_buf,
	.activity_changed = ath6kl_htc_pipe_activity_changed,
	.get_rxbuf_num = ath6kl_htc_pipe_get_rxbuf_num,
	.add_rxbuf_multiple = ath6kl_htc_pipe_add_rxbuf_multiple,
	.credit_setup = ath6kl_htc_pipe_credit_setup,
	.tx_complete = ath6kl_htc_pipe_tx_complete,
	.rx_complete = ath6kl_htc_pipe_rx_complete,
};

void ath6kl_htc_pipe_attach(struct ath6kl *ar)
{
	ar->htc_ops = &ath6kl_htc_pipe_ops;
}
