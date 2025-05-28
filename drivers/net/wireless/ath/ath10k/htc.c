// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "core.h"
#include "hif.h"
#include "debug.h"

/********/
/* Send */
/********/

static void ath10k_htc_control_tx_complete(struct ath10k *ar,
					   struct sk_buff *skb)
{
	kfree_skb(skb);
}

static struct sk_buff *ath10k_htc_build_tx_ctrl_skb(void *ar)
{
	struct sk_buff *skb;
	struct ath10k_skb_cb *skb_cb;

	skb = dev_alloc_skb(ATH10K_HTC_CONTROL_BUFFER_SIZE);
	if (!skb)
		return NULL;

	skb_reserve(skb, 20); /* FIXME: why 20 bytes? */
	WARN_ONCE((unsigned long)skb->data & 3, "unaligned skb");

	skb_cb = ATH10K_SKB_CB(skb);
	memset(skb_cb, 0, sizeof(*skb_cb));

	ath10k_dbg(ar, ATH10K_DBG_HTC, "%s: skb %p\n", __func__, skb);
	return skb;
}

static inline void ath10k_htc_restore_tx_skb(struct ath10k_htc *htc,
					     struct sk_buff *skb)
{
	struct ath10k_skb_cb *skb_cb = ATH10K_SKB_CB(skb);

	if (htc->ar->bus_param.dev_type != ATH10K_DEV_TYPE_HL)
		dma_unmap_single(htc->ar->dev, skb_cb->paddr, skb->len, DMA_TO_DEVICE);
	skb_pull(skb, sizeof(struct ath10k_htc_hdr));
}

void ath10k_htc_notify_tx_completion(struct ath10k_htc_ep *ep,
				     struct sk_buff *skb)
{
	struct ath10k *ar = ep->htc->ar;
	struct ath10k_htc_hdr *hdr;

	ath10k_dbg(ar, ATH10K_DBG_HTC, "%s: ep %d skb %p\n", __func__,
		   ep->eid, skb);

	/* A corner case where the copy completion is reaching to host but still
	 * copy engine is processing it due to which host unmaps corresponding
	 * memory and causes SMMU fault, hence as workaround adding delay
	 * the unmapping memory to avoid SMMU faults.
	 */
	if (ar->hw_params.delay_unmap_buffer &&
	    ep->ul_pipe_id == 3)
		mdelay(2);

	hdr = (struct ath10k_htc_hdr *)skb->data;
	ath10k_htc_restore_tx_skb(ep->htc, skb);

	if (!ep->ep_ops.ep_tx_complete) {
		ath10k_warn(ar, "no tx handler for eid %d\n", ep->eid);
		dev_kfree_skb_any(skb);
		return;
	}

	if (hdr->flags & ATH10K_HTC_FLAG_SEND_BUNDLE) {
		dev_kfree_skb_any(skb);
		return;
	}

	ep->ep_ops.ep_tx_complete(ep->htc->ar, skb);
}
EXPORT_SYMBOL(ath10k_htc_notify_tx_completion);

static void ath10k_htc_prepare_tx_skb(struct ath10k_htc_ep *ep,
				      struct sk_buff *skb)
{
	struct ath10k_htc_hdr *hdr;

	hdr = (struct ath10k_htc_hdr *)skb->data;
	memset(hdr, 0, sizeof(struct ath10k_htc_hdr));

	hdr->eid = ep->eid;
	hdr->len = __cpu_to_le16(skb->len - sizeof(*hdr));
	hdr->flags = 0;
	if (ep->tx_credit_flow_enabled && !ep->bundle_tx)
		hdr->flags |= ATH10K_HTC_FLAG_NEED_CREDIT_UPDATE;

	spin_lock_bh(&ep->htc->tx_lock);
	hdr->seq_no = ep->seq_no++;
	spin_unlock_bh(&ep->htc->tx_lock);
}

static int ath10k_htc_consume_credit(struct ath10k_htc_ep *ep,
				     unsigned int len,
				     bool consume)
{
	struct ath10k_htc *htc = ep->htc;
	struct ath10k *ar = htc->ar;
	enum ath10k_htc_ep_id eid = ep->eid;
	int credits, ret = 0;

	if (!ep->tx_credit_flow_enabled)
		return 0;

	credits = DIV_ROUND_UP(len, ep->tx_credit_size);
	spin_lock_bh(&htc->tx_lock);

	if (ep->tx_credits < credits) {
		ath10k_dbg(ar, ATH10K_DBG_HTC,
			   "htc insufficient credits ep %d required %d available %d consume %d\n",
			   eid, credits, ep->tx_credits, consume);
		ret = -EAGAIN;
		goto unlock;
	}

	if (consume) {
		ep->tx_credits -= credits;
		ath10k_dbg(ar, ATH10K_DBG_HTC,
			   "htc ep %d consumed %d credits total %d\n",
			   eid, credits, ep->tx_credits);
	}

unlock:
	spin_unlock_bh(&htc->tx_lock);
	return ret;
}

static void ath10k_htc_release_credit(struct ath10k_htc_ep *ep, unsigned int len)
{
	struct ath10k_htc *htc = ep->htc;
	struct ath10k *ar = htc->ar;
	enum ath10k_htc_ep_id eid = ep->eid;
	int credits;

	if (!ep->tx_credit_flow_enabled)
		return;

	credits = DIV_ROUND_UP(len, ep->tx_credit_size);
	spin_lock_bh(&htc->tx_lock);
	ep->tx_credits += credits;
	ath10k_dbg(ar, ATH10K_DBG_HTC,
		   "htc ep %d reverted %d credits back total %d\n",
		   eid, credits, ep->tx_credits);
	spin_unlock_bh(&htc->tx_lock);

	if (ep->ep_ops.ep_tx_credits)
		ep->ep_ops.ep_tx_credits(htc->ar);
}

int ath10k_htc_send(struct ath10k_htc *htc,
		    enum ath10k_htc_ep_id eid,
		    struct sk_buff *skb)
{
	struct ath10k *ar = htc->ar;
	struct ath10k_htc_ep *ep = &htc->endpoint[eid];
	struct ath10k_skb_cb *skb_cb = ATH10K_SKB_CB(skb);
	struct ath10k_hif_sg_item sg_item;
	struct device *dev = htc->ar->dev;
	int ret;
	unsigned int skb_len;

	if (htc->ar->state == ATH10K_STATE_WEDGED)
		return -ECOMM;

	if (eid >= ATH10K_HTC_EP_COUNT) {
		ath10k_warn(ar, "Invalid endpoint id: %d\n", eid);
		return -ENOENT;
	}

	skb_push(skb, sizeof(struct ath10k_htc_hdr));

	skb_len = skb->len;
	ret = ath10k_htc_consume_credit(ep, skb_len, true);
	if (ret)
		goto err_pull;

	ath10k_htc_prepare_tx_skb(ep, skb);

	skb_cb->eid = eid;
	if (ar->bus_param.dev_type != ATH10K_DEV_TYPE_HL) {
		skb_cb->paddr = dma_map_single(dev, skb->data, skb->len,
					       DMA_TO_DEVICE);
		ret = dma_mapping_error(dev, skb_cb->paddr);
		if (ret) {
			ret = -EIO;
			goto err_credits;
		}
	}

	sg_item.transfer_id = ep->eid;
	sg_item.transfer_context = skb;
	sg_item.vaddr = skb->data;
	sg_item.paddr = skb_cb->paddr;
	sg_item.len = skb->len;

	ret = ath10k_hif_tx_sg(htc->ar, ep->ul_pipe_id, &sg_item, 1);
	if (ret)
		goto err_unmap;

	return 0;

err_unmap:
	if (ar->bus_param.dev_type != ATH10K_DEV_TYPE_HL)
		dma_unmap_single(dev, skb_cb->paddr, skb->len, DMA_TO_DEVICE);
err_credits:
	ath10k_htc_release_credit(ep, skb_len);
err_pull:
	skb_pull(skb, sizeof(struct ath10k_htc_hdr));
	return ret;
}

void ath10k_htc_tx_completion_handler(struct ath10k *ar, struct sk_buff *skb)
{
	struct ath10k_htc *htc = &ar->htc;
	struct ath10k_skb_cb *skb_cb;
	struct ath10k_htc_ep *ep;

	if (WARN_ON_ONCE(!skb))
		return;

	skb_cb = ATH10K_SKB_CB(skb);
	ep = &htc->endpoint[skb_cb->eid];

	ath10k_htc_notify_tx_completion(ep, skb);
	/* the skb now belongs to the completion handler */
}
EXPORT_SYMBOL(ath10k_htc_tx_completion_handler);

/***********/
/* Receive */
/***********/

static void
ath10k_htc_process_credit_report(struct ath10k_htc *htc,
				 const struct ath10k_htc_credit_report *report,
				 int len,
				 enum ath10k_htc_ep_id eid)
{
	struct ath10k *ar = htc->ar;
	struct ath10k_htc_ep *ep;
	int i, n_reports;

	if (len % sizeof(*report))
		ath10k_warn(ar, "Uneven credit report len %d", len);

	n_reports = len / sizeof(*report);

	spin_lock_bh(&htc->tx_lock);
	for (i = 0; i < n_reports; i++, report++) {
		if (report->eid >= ATH10K_HTC_EP_COUNT)
			break;

		ep = &htc->endpoint[report->eid];
		ep->tx_credits += report->credits;

		ath10k_dbg(ar, ATH10K_DBG_HTC, "htc ep %d got %d credits (total %d)\n",
			   report->eid, report->credits, ep->tx_credits);

		if (ep->ep_ops.ep_tx_credits) {
			spin_unlock_bh(&htc->tx_lock);
			ep->ep_ops.ep_tx_credits(htc->ar);
			spin_lock_bh(&htc->tx_lock);
		}
	}
	spin_unlock_bh(&htc->tx_lock);
}

static int
ath10k_htc_process_lookahead(struct ath10k_htc *htc,
			     const struct ath10k_htc_lookahead_report *report,
			     int len,
			     enum ath10k_htc_ep_id eid,
			     void *next_lookaheads,
			     int *next_lookaheads_len)
{
	struct ath10k *ar = htc->ar;

	/* Invalid lookahead flags are actually transmitted by
	 * the target in the HTC control message.
	 * Since this will happen at every boot we silently ignore
	 * the lookahead in this case
	 */
	if (report->pre_valid != ((~report->post_valid) & 0xFF))
		return 0;

	if (next_lookaheads && next_lookaheads_len) {
		ath10k_dbg(ar, ATH10K_DBG_HTC,
			   "htc rx lookahead found pre_valid 0x%x post_valid 0x%x\n",
			   report->pre_valid, report->post_valid);

		/* look ahead bytes are valid, copy them over */
		memcpy((u8 *)next_lookaheads, report->lookahead, 4);

		*next_lookaheads_len = 1;
	}

	return 0;
}

static int
ath10k_htc_process_lookahead_bundle(struct ath10k_htc *htc,
				    const struct ath10k_htc_lookahead_bundle *report,
				    int len,
				    enum ath10k_htc_ep_id eid,
				    void *next_lookaheads,
				    int *next_lookaheads_len)
{
	struct ath10k *ar = htc->ar;
	int bundle_cnt = len / sizeof(*report);

	if (!bundle_cnt || (bundle_cnt > htc->max_msgs_per_htc_bundle)) {
		ath10k_warn(ar, "Invalid lookahead bundle count: %d\n",
			    bundle_cnt);
		return -EINVAL;
	}

	if (next_lookaheads && next_lookaheads_len) {
		int i;

		for (i = 0; i < bundle_cnt; i++) {
			memcpy(((u8 *)next_lookaheads) + 4 * i,
			       report->lookahead, 4);
			report++;
		}

		*next_lookaheads_len = bundle_cnt;
	}

	return 0;
}

int ath10k_htc_process_trailer(struct ath10k_htc *htc,
			       u8 *buffer,
			       int length,
			       enum ath10k_htc_ep_id src_eid,
			       void *next_lookaheads,
			       int *next_lookaheads_len)
{
	struct ath10k_htc_lookahead_bundle *bundle;
	struct ath10k *ar = htc->ar;
	int status = 0;
	struct ath10k_htc_record *record;
	u8 *orig_buffer;
	int orig_length;
	size_t len;

	orig_buffer = buffer;
	orig_length = length;

	while (length > 0) {
		record = (struct ath10k_htc_record *)buffer;

		if (length < sizeof(record->hdr)) {
			status = -EINVAL;
			break;
		}

		if (record->hdr.len > length) {
			/* no room left in buffer for record */
			ath10k_warn(ar, "Invalid record length: %d\n",
				    record->hdr.len);
			status = -EINVAL;
			break;
		}

		switch (record->hdr.id) {
		case ATH10K_HTC_RECORD_CREDITS:
			len = sizeof(struct ath10k_htc_credit_report);
			if (record->hdr.len < len) {
				ath10k_warn(ar, "Credit report too long\n");
				status = -EINVAL;
				break;
			}
			ath10k_htc_process_credit_report(htc,
							 record->credit_report,
							 record->hdr.len,
							 src_eid);
			break;
		case ATH10K_HTC_RECORD_LOOKAHEAD:
			len = sizeof(struct ath10k_htc_lookahead_report);
			if (record->hdr.len < len) {
				ath10k_warn(ar, "Lookahead report too long\n");
				status = -EINVAL;
				break;
			}
			status = ath10k_htc_process_lookahead(htc,
							      record->lookahead_report,
							      record->hdr.len,
							      src_eid,
							      next_lookaheads,
							      next_lookaheads_len);
			break;
		case ATH10K_HTC_RECORD_LOOKAHEAD_BUNDLE:
			bundle = record->lookahead_bundle;
			status = ath10k_htc_process_lookahead_bundle(htc,
								     bundle,
								     record->hdr.len,
								     src_eid,
								     next_lookaheads,
								     next_lookaheads_len);
			break;
		default:
			ath10k_warn(ar, "Unhandled record: id:%d length:%d\n",
				    record->hdr.id, record->hdr.len);
			break;
		}

		if (status)
			break;

		/* multiple records may be present in a trailer */
		buffer += sizeof(record->hdr) + record->hdr.len;
		length -= sizeof(record->hdr) + record->hdr.len;
	}

	if (status)
		ath10k_dbg_dump(ar, ATH10K_DBG_HTC, "htc rx bad trailer", "",
				orig_buffer, orig_length);

	return status;
}
EXPORT_SYMBOL(ath10k_htc_process_trailer);

void ath10k_htc_rx_completion_handler(struct ath10k *ar, struct sk_buff *skb)
{
	int status = 0;
	struct ath10k_htc *htc = &ar->htc;
	struct ath10k_htc_hdr *hdr;
	struct ath10k_htc_ep *ep;
	u16 payload_len;
	u32 trailer_len = 0;
	size_t min_len;
	u8 eid;
	bool trailer_present;

	hdr = (struct ath10k_htc_hdr *)skb->data;
	skb_pull(skb, sizeof(*hdr));

	eid = hdr->eid;

	if (eid >= ATH10K_HTC_EP_COUNT) {
		ath10k_warn(ar, "HTC Rx: invalid eid %d\n", eid);
		ath10k_dbg_dump(ar, ATH10K_DBG_HTC, "htc bad header", "",
				hdr, sizeof(*hdr));
		goto out;
	}

	ep = &htc->endpoint[eid];
	if (ep->service_id == ATH10K_HTC_SVC_ID_UNUSED) {
		ath10k_warn(ar, "htc rx endpoint %d is not connected\n", eid);
		goto out;
	}

	payload_len = __le16_to_cpu(hdr->len);

	if (payload_len + sizeof(*hdr) > ATH10K_HTC_MAX_LEN) {
		ath10k_warn(ar, "HTC rx frame too long, len: %zu\n",
			    payload_len + sizeof(*hdr));
		ath10k_dbg_dump(ar, ATH10K_DBG_HTC, "htc bad rx pkt len", "",
				hdr, sizeof(*hdr));
		goto out;
	}

	if (skb->len < payload_len) {
		ath10k_dbg(ar, ATH10K_DBG_HTC,
			   "HTC Rx: insufficient length, got %d, expected %d\n",
			   skb->len, payload_len);
		ath10k_dbg_dump(ar, ATH10K_DBG_HTC, "htc bad rx pkt len",
				"", hdr, sizeof(*hdr));
		goto out;
	}

	/* get flags to check for trailer */
	trailer_present = hdr->flags & ATH10K_HTC_FLAG_TRAILER_PRESENT;
	if (trailer_present) {
		u8 *trailer;

		trailer_len = hdr->trailer_len;
		min_len = sizeof(struct ath10k_ath10k_htc_record_hdr);

		if ((trailer_len < min_len) ||
		    (trailer_len > payload_len)) {
			ath10k_warn(ar, "Invalid trailer length: %d\n",
				    trailer_len);
			goto out;
		}

		trailer = (u8 *)hdr;
		trailer += sizeof(*hdr);
		trailer += payload_len;
		trailer -= trailer_len;
		status = ath10k_htc_process_trailer(htc, trailer,
						    trailer_len, hdr->eid,
						    NULL, NULL);
		if (status)
			goto out;

		skb_trim(skb, skb->len - trailer_len);
	}

	if (((int)payload_len - (int)trailer_len) <= 0)
		/* zero length packet with trailer data, just drop these */
		goto out;

	ath10k_dbg(ar, ATH10K_DBG_HTC, "htc rx completion ep %d skb %p\n",
		   eid, skb);
	ep->ep_ops.ep_rx_complete(ar, skb);

	/* skb is now owned by the rx completion handler */
	skb = NULL;
out:
	kfree_skb(skb);
}
EXPORT_SYMBOL(ath10k_htc_rx_completion_handler);

static void ath10k_htc_control_rx_complete(struct ath10k *ar,
					   struct sk_buff *skb)
{
	struct ath10k_htc *htc = &ar->htc;
	struct ath10k_htc_msg *msg = (struct ath10k_htc_msg *)skb->data;

	switch (__le16_to_cpu(msg->hdr.message_id)) {
	case ATH10K_HTC_MSG_READY_ID:
	case ATH10K_HTC_MSG_CONNECT_SERVICE_RESP_ID:
		/* handle HTC control message */
		if (completion_done(&htc->ctl_resp)) {
			/* this is a fatal error, target should not be
			 * sending unsolicited messages on the ep 0
			 */
			ath10k_warn(ar, "HTC rx ctrl still processing\n");
			complete(&htc->ctl_resp);
			goto out;
		}

		htc->control_resp_len =
			min_t(int, skb->len,
			      ATH10K_HTC_MAX_CTRL_MSG_LEN);

		memcpy(htc->control_resp_buffer, skb->data,
		       htc->control_resp_len);

		complete(&htc->ctl_resp);
		break;
	case ATH10K_HTC_MSG_SEND_SUSPEND_COMPLETE:
		htc->htc_ops.target_send_suspend_complete(ar);
		break;
	default:
		ath10k_warn(ar, "ignoring unsolicited htc ep0 event\n");
		break;
	}

out:
	kfree_skb(skb);
}

/***************/
/* Init/Deinit */
/***************/

static const char *htc_service_name(enum ath10k_htc_svc_id id)
{
	switch (id) {
	case ATH10K_HTC_SVC_ID_RESERVED:
		return "Reserved";
	case ATH10K_HTC_SVC_ID_RSVD_CTRL:
		return "Control";
	case ATH10K_HTC_SVC_ID_WMI_CONTROL:
		return "WMI";
	case ATH10K_HTC_SVC_ID_WMI_DATA_BE:
		return "DATA BE";
	case ATH10K_HTC_SVC_ID_WMI_DATA_BK:
		return "DATA BK";
	case ATH10K_HTC_SVC_ID_WMI_DATA_VI:
		return "DATA VI";
	case ATH10K_HTC_SVC_ID_WMI_DATA_VO:
		return "DATA VO";
	case ATH10K_HTC_SVC_ID_NMI_CONTROL:
		return "NMI Control";
	case ATH10K_HTC_SVC_ID_NMI_DATA:
		return "NMI Data";
	case ATH10K_HTC_SVC_ID_HTT_DATA_MSG:
		return "HTT Data";
	case ATH10K_HTC_SVC_ID_HTT_DATA2_MSG:
		return "HTT Data";
	case ATH10K_HTC_SVC_ID_HTT_DATA3_MSG:
		return "HTT Data";
	case ATH10K_HTC_SVC_ID_TEST_RAW_STREAMS:
		return "RAW";
	case ATH10K_HTC_SVC_ID_HTT_LOG_MSG:
		return "PKTLOG";
	}

	return "Unknown";
}

static void ath10k_htc_reset_endpoint_states(struct ath10k_htc *htc)
{
	struct ath10k_htc_ep *ep;
	int i;

	for (i = ATH10K_HTC_EP_0; i < ATH10K_HTC_EP_COUNT; i++) {
		ep = &htc->endpoint[i];
		ep->service_id = ATH10K_HTC_SVC_ID_UNUSED;
		ep->max_ep_message_len = 0;
		ep->max_tx_queue_depth = 0;
		ep->eid = i;
		ep->htc = htc;
		ep->tx_credit_flow_enabled = true;
	}
}

static u8 ath10k_htc_get_credit_allocation(struct ath10k_htc *htc,
					   u16 service_id)
{
	u8 allocation = 0;

	/* The WMI control service is the only service with flow control.
	 * Let it have all transmit credits.
	 */
	if (service_id == ATH10K_HTC_SVC_ID_WMI_CONTROL)
		allocation = htc->total_transmit_credits;

	return allocation;
}

static int ath10k_htc_send_bundle(struct ath10k_htc_ep *ep,
				  struct sk_buff *bundle_skb,
				  struct sk_buff_head *tx_save_head)
{
	struct ath10k_hif_sg_item sg_item;
	struct ath10k_htc *htc = ep->htc;
	struct ath10k *ar = htc->ar;
	struct sk_buff *skb;
	int ret, cn = 0;
	unsigned int skb_len;

	ath10k_dbg(ar, ATH10K_DBG_HTC, "bundle skb len %d\n", bundle_skb->len);
	skb_len = bundle_skb->len;
	ret = ath10k_htc_consume_credit(ep, skb_len, true);

	if (!ret) {
		sg_item.transfer_id = ep->eid;
		sg_item.transfer_context = bundle_skb;
		sg_item.vaddr = bundle_skb->data;
		sg_item.len = bundle_skb->len;

		ret = ath10k_hif_tx_sg(htc->ar, ep->ul_pipe_id, &sg_item, 1);
		if (ret)
			ath10k_htc_release_credit(ep, skb_len);
	}

	if (ret)
		dev_kfree_skb_any(bundle_skb);

	for (cn = 0; (skb = skb_dequeue_tail(tx_save_head)); cn++) {
		if (ret) {
			skb_pull(skb, sizeof(struct ath10k_htc_hdr));
			skb_queue_head(&ep->tx_req_head, skb);
		} else {
			skb_queue_tail(&ep->tx_complete_head, skb);
		}
	}

	if (!ret)
		queue_work(ar->workqueue_tx_complete, &ar->tx_complete_work);

	ath10k_dbg(ar, ATH10K_DBG_HTC,
		   "bundle tx status %d eid %d req count %d count %d len %d\n",
		   ret, ep->eid, skb_queue_len(&ep->tx_req_head), cn, skb_len);
	return ret;
}

static void ath10k_htc_send_one_skb(struct ath10k_htc_ep *ep, struct sk_buff *skb)
{
	struct ath10k_htc *htc = ep->htc;
	struct ath10k *ar = htc->ar;
	int ret;

	ret = ath10k_htc_send(htc, ep->eid, skb);

	if (ret)
		skb_queue_head(&ep->tx_req_head, skb);

	ath10k_dbg(ar, ATH10K_DBG_HTC, "tx one status %d eid %d len %d pending count %d\n",
		   ret, ep->eid, skb->len, skb_queue_len(&ep->tx_req_head));
}

static int ath10k_htc_send_bundle_skbs(struct ath10k_htc_ep *ep)
{
	struct ath10k_htc *htc = ep->htc;
	struct sk_buff *bundle_skb, *skb;
	struct sk_buff_head tx_save_head;
	struct ath10k_htc_hdr *hdr;
	u8 *bundle_buf;
	int ret = 0, credit_pad, credit_remainder, trans_len, bundles_left = 0;

	if (htc->ar->state == ATH10K_STATE_WEDGED)
		return -ECOMM;

	if (ep->tx_credit_flow_enabled &&
	    ep->tx_credits < ATH10K_MIN_CREDIT_PER_HTC_TX_BUNDLE)
		return 0;

	bundles_left = ATH10K_MAX_MSG_PER_HTC_TX_BUNDLE * ep->tx_credit_size;
	bundle_skb = dev_alloc_skb(bundles_left);

	if (!bundle_skb)
		return -ENOMEM;

	bundle_buf = bundle_skb->data;
	skb_queue_head_init(&tx_save_head);

	while (true) {
		skb = skb_dequeue(&ep->tx_req_head);
		if (!skb)
			break;

		credit_pad = 0;
		trans_len = skb->len + sizeof(*hdr);
		credit_remainder = trans_len % ep->tx_credit_size;

		if (credit_remainder != 0) {
			credit_pad = ep->tx_credit_size - credit_remainder;
			trans_len += credit_pad;
		}

		ret = ath10k_htc_consume_credit(ep,
						bundle_buf + trans_len - bundle_skb->data,
						false);
		if (ret) {
			skb_queue_head(&ep->tx_req_head, skb);
			break;
		}

		if (bundles_left < trans_len) {
			bundle_skb->len = bundle_buf - bundle_skb->data;
			ret = ath10k_htc_send_bundle(ep, bundle_skb, &tx_save_head);

			if (ret) {
				skb_queue_head(&ep->tx_req_head, skb);
				return ret;
			}

			if (skb_queue_len(&ep->tx_req_head) == 0) {
				ath10k_htc_send_one_skb(ep, skb);
				return ret;
			}

			if (ep->tx_credit_flow_enabled &&
			    ep->tx_credits < ATH10K_MIN_CREDIT_PER_HTC_TX_BUNDLE) {
				skb_queue_head(&ep->tx_req_head, skb);
				return 0;
			}

			bundles_left =
				ATH10K_MAX_MSG_PER_HTC_TX_BUNDLE * ep->tx_credit_size;
			bundle_skb = dev_alloc_skb(bundles_left);

			if (!bundle_skb) {
				skb_queue_head(&ep->tx_req_head, skb);
				return -ENOMEM;
			}
			bundle_buf = bundle_skb->data;
			skb_queue_head_init(&tx_save_head);
		}

		skb_push(skb, sizeof(struct ath10k_htc_hdr));
		ath10k_htc_prepare_tx_skb(ep, skb);

		memcpy(bundle_buf, skb->data, skb->len);
		hdr = (struct ath10k_htc_hdr *)bundle_buf;
		hdr->flags |= ATH10K_HTC_FLAG_SEND_BUNDLE;
		hdr->pad_len = __cpu_to_le16(credit_pad);
		bundle_buf += trans_len;
		bundles_left -= trans_len;
		skb_queue_tail(&tx_save_head, skb);
	}

	if (bundle_buf != bundle_skb->data) {
		bundle_skb->len = bundle_buf - bundle_skb->data;
		ret = ath10k_htc_send_bundle(ep, bundle_skb, &tx_save_head);
	} else {
		dev_kfree_skb_any(bundle_skb);
	}

	return ret;
}

static void ath10k_htc_bundle_tx_work(struct work_struct *work)
{
	struct ath10k *ar = container_of(work, struct ath10k, bundle_tx_work);
	struct ath10k_htc_ep *ep;
	struct sk_buff *skb;
	int i;

	for (i = 0; i < ARRAY_SIZE(ar->htc.endpoint); i++) {
		ep = &ar->htc.endpoint[i];

		if (!ep->bundle_tx)
			continue;

		ath10k_dbg(ar, ATH10K_DBG_HTC, "bundle tx work eid %d count %d\n",
			   ep->eid, skb_queue_len(&ep->tx_req_head));

		if (skb_queue_len(&ep->tx_req_head) >=
		    ATH10K_MIN_MSG_PER_HTC_TX_BUNDLE) {
			ath10k_htc_send_bundle_skbs(ep);
		} else {
			skb = skb_dequeue(&ep->tx_req_head);

			if (!skb)
				continue;
			ath10k_htc_send_one_skb(ep, skb);
		}
	}
}

static void ath10k_htc_tx_complete_work(struct work_struct *work)
{
	struct ath10k *ar = container_of(work, struct ath10k, tx_complete_work);
	struct ath10k_htc_ep *ep;
	enum ath10k_htc_ep_id eid;
	struct sk_buff *skb;
	int i;

	for (i = 0; i < ARRAY_SIZE(ar->htc.endpoint); i++) {
		ep = &ar->htc.endpoint[i];
		eid = ep->eid;
		if (ep->bundle_tx && eid == ar->htt.eid) {
			ath10k_dbg(ar, ATH10K_DBG_HTC, "bundle tx complete eid %d pending complete count%d\n",
				   ep->eid, skb_queue_len(&ep->tx_complete_head));

			while (true) {
				skb = skb_dequeue(&ep->tx_complete_head);
				if (!skb)
					break;
				ath10k_htc_notify_tx_completion(ep, skb);
			}
		}
	}
}

int ath10k_htc_send_hl(struct ath10k_htc *htc,
		       enum ath10k_htc_ep_id eid,
		       struct sk_buff *skb)
{
	struct ath10k_htc_ep *ep = &htc->endpoint[eid];
	struct ath10k *ar = htc->ar;

	if (sizeof(struct ath10k_htc_hdr) + skb->len > ep->tx_credit_size) {
		ath10k_dbg(ar, ATH10K_DBG_HTC, "tx exceed max len %d\n", skb->len);
		return -ENOMEM;
	}

	ath10k_dbg(ar, ATH10K_DBG_HTC, "htc send hl eid %d bundle %d tx count %d len %d\n",
		   eid, ep->bundle_tx, skb_queue_len(&ep->tx_req_head), skb->len);

	if (ep->bundle_tx) {
		skb_queue_tail(&ep->tx_req_head, skb);
		queue_work(ar->workqueue, &ar->bundle_tx_work);
		return 0;
	} else {
		return ath10k_htc_send(htc, eid, skb);
	}
}

void ath10k_htc_setup_tx_req(struct ath10k_htc_ep *ep)
{
	if (ep->htc->max_msgs_per_htc_bundle >= ATH10K_MIN_MSG_PER_HTC_TX_BUNDLE &&
	    !ep->bundle_tx) {
		ep->bundle_tx = true;
		skb_queue_head_init(&ep->tx_req_head);
		skb_queue_head_init(&ep->tx_complete_head);
	}
}

void ath10k_htc_stop_hl(struct ath10k *ar)
{
	struct ath10k_htc_ep *ep;
	int i;

	cancel_work_sync(&ar->bundle_tx_work);
	cancel_work_sync(&ar->tx_complete_work);

	for (i = 0; i < ARRAY_SIZE(ar->htc.endpoint); i++) {
		ep = &ar->htc.endpoint[i];

		if (!ep->bundle_tx)
			continue;

		ath10k_dbg(ar, ATH10K_DBG_HTC, "stop tx work eid %d count %d\n",
			   ep->eid, skb_queue_len(&ep->tx_req_head));

		skb_queue_purge(&ep->tx_req_head);
	}
}

int ath10k_htc_wait_target(struct ath10k_htc *htc)
{
	struct ath10k *ar = htc->ar;
	int i, status = 0;
	unsigned long time_left;
	struct ath10k_htc_msg *msg;
	u16 message_id;

	time_left = wait_for_completion_timeout(&htc->ctl_resp,
						ATH10K_HTC_WAIT_TIMEOUT_HZ);
	if (!time_left) {
		/* Workaround: In some cases the PCI HIF doesn't
		 * receive interrupt for the control response message
		 * even if the buffer was completed. It is suspected
		 * iomap writes unmasking PCI CE irqs aren't propagated
		 * properly in KVM PCI-passthrough sometimes.
		 */
		ath10k_warn(ar, "failed to receive control response completion, polling..\n");

		for (i = 0; i < CE_COUNT; i++)
			ath10k_hif_send_complete_check(htc->ar, i, 1);

		time_left =
		wait_for_completion_timeout(&htc->ctl_resp,
					    ATH10K_HTC_WAIT_TIMEOUT_HZ);

		if (!time_left)
			status = -ETIMEDOUT;
	}

	if (status < 0) {
		ath10k_err(ar, "ctl_resp never came in (%d)\n", status);
		return status;
	}

	if (htc->control_resp_len < sizeof(msg->hdr) + sizeof(msg->ready)) {
		ath10k_err(ar, "Invalid HTC ready msg len:%d\n",
			   htc->control_resp_len);
		return -ECOMM;
	}

	msg = (struct ath10k_htc_msg *)htc->control_resp_buffer;
	message_id   = __le16_to_cpu(msg->hdr.message_id);

	if (message_id != ATH10K_HTC_MSG_READY_ID) {
		ath10k_err(ar, "Invalid HTC ready msg: 0x%x\n", message_id);
		return -ECOMM;
	}

	if (ar->hw_params.use_fw_tx_credits)
		htc->total_transmit_credits = __le16_to_cpu(msg->ready.credit_count);
	else
		htc->total_transmit_credits = 1;

	htc->target_credit_size = __le16_to_cpu(msg->ready.credit_size);

	ath10k_dbg(ar, ATH10K_DBG_HTC,
		   "Target ready! transmit resources: %d size:%d actual credits:%d\n",
		   htc->total_transmit_credits,
		   htc->target_credit_size,
		   msg->ready.credit_count);

	if ((htc->total_transmit_credits == 0) ||
	    (htc->target_credit_size == 0)) {
		ath10k_err(ar, "Invalid credit size received\n");
		return -ECOMM;
	}

	/* The only way to determine if the ready message is an extended
	 * message is from the size.
	 */
	if (htc->control_resp_len >=
	    sizeof(msg->hdr) + sizeof(msg->ready_ext)) {
		htc->alt_data_credit_size =
			__le16_to_cpu(msg->ready_ext.reserved) &
			ATH10K_HTC_MSG_READY_EXT_ALT_DATA_MASK;
		htc->max_msgs_per_htc_bundle =
			min_t(u8, msg->ready_ext.max_msgs_per_htc_bundle,
			      HTC_HOST_MAX_MSG_PER_RX_BUNDLE);
		ath10k_dbg(ar, ATH10K_DBG_HTC,
			   "Extended ready message RX bundle size %d alt size %d\n",
			   htc->max_msgs_per_htc_bundle,
			   htc->alt_data_credit_size);
	}

	INIT_WORK(&ar->bundle_tx_work, ath10k_htc_bundle_tx_work);
	INIT_WORK(&ar->tx_complete_work, ath10k_htc_tx_complete_work);

	return 0;
}

void ath10k_htc_change_tx_credit_flow(struct ath10k_htc *htc,
				      enum ath10k_htc_ep_id eid,
				      bool enable)
{
	struct ath10k *ar = htc->ar;
	struct ath10k_htc_ep *ep = &ar->htc.endpoint[eid];

	ep->tx_credit_flow_enabled = enable;
}

int ath10k_htc_connect_service(struct ath10k_htc *htc,
			       struct ath10k_htc_svc_conn_req *conn_req,
			       struct ath10k_htc_svc_conn_resp *conn_resp)
{
	struct ath10k *ar = htc->ar;
	struct ath10k_htc_msg *msg;
	struct ath10k_htc_conn_svc *req_msg;
	struct ath10k_htc_conn_svc_response resp_msg_dummy;
	struct ath10k_htc_conn_svc_response *resp_msg = &resp_msg_dummy;
	enum ath10k_htc_ep_id assigned_eid = ATH10K_HTC_EP_COUNT;
	struct ath10k_htc_ep *ep;
	struct sk_buff *skb;
	unsigned int max_msg_size = 0;
	int length, status;
	unsigned long time_left;
	bool disable_credit_flow_ctrl = false;
	u16 message_id, service_id, flags = 0;
	u8 tx_alloc = 0;

	/* special case for HTC pseudo control service */
	if (conn_req->service_id == ATH10K_HTC_SVC_ID_RSVD_CTRL) {
		disable_credit_flow_ctrl = true;
		assigned_eid = ATH10K_HTC_EP_0;
		max_msg_size = ATH10K_HTC_MAX_CTRL_MSG_LEN;
		memset(&resp_msg_dummy, 0, sizeof(resp_msg_dummy));
		goto setup;
	}

	tx_alloc = ath10k_htc_get_credit_allocation(htc,
						    conn_req->service_id);
	if (!tx_alloc)
		ath10k_dbg(ar, ATH10K_DBG_BOOT,
			   "boot htc service %s does not allocate target credits\n",
			   htc_service_name(conn_req->service_id));

	skb = ath10k_htc_build_tx_ctrl_skb(htc->ar);
	if (!skb) {
		ath10k_err(ar, "Failed to allocate HTC packet\n");
		return -ENOMEM;
	}

	length = sizeof(msg->hdr) + sizeof(msg->connect_service);
	skb_put(skb, length);
	memset(skb->data, 0, length);

	msg = (struct ath10k_htc_msg *)skb->data;
	msg->hdr.message_id =
		__cpu_to_le16(ATH10K_HTC_MSG_CONNECT_SERVICE_ID);

	flags |= SM(tx_alloc, ATH10K_HTC_CONN_FLAGS_RECV_ALLOC);

	/* Only enable credit flow control for WMI ctrl service */
	if (conn_req->service_id != ATH10K_HTC_SVC_ID_WMI_CONTROL) {
		flags |= ATH10K_HTC_CONN_FLAGS_DISABLE_CREDIT_FLOW_CTRL;
		disable_credit_flow_ctrl = true;
	}

	req_msg = &msg->connect_service;
	req_msg->flags = __cpu_to_le16(flags);
	req_msg->service_id = __cpu_to_le16(conn_req->service_id);

	reinit_completion(&htc->ctl_resp);

	status = ath10k_htc_send(htc, ATH10K_HTC_EP_0, skb);
	if (status) {
		kfree_skb(skb);
		return status;
	}

	/* wait for response */
	time_left = wait_for_completion_timeout(&htc->ctl_resp,
						ATH10K_HTC_CONN_SVC_TIMEOUT_HZ);
	if (!time_left) {
		ath10k_err(ar, "Service connect timeout\n");
		return -ETIMEDOUT;
	}

	/* we controlled the buffer creation, it's aligned */
	msg = (struct ath10k_htc_msg *)htc->control_resp_buffer;
	resp_msg = &msg->connect_service_response;
	message_id = __le16_to_cpu(msg->hdr.message_id);
	service_id = __le16_to_cpu(resp_msg->service_id);

	if ((message_id != ATH10K_HTC_MSG_CONNECT_SERVICE_RESP_ID) ||
	    (htc->control_resp_len < sizeof(msg->hdr) +
	     sizeof(msg->connect_service_response))) {
		ath10k_err(ar, "Invalid resp message ID 0x%x", message_id);
		return -EPROTO;
	}

	ath10k_dbg(ar, ATH10K_DBG_HTC,
		   "HTC Service %s connect response: status: 0x%x, assigned ep: 0x%x\n",
		   htc_service_name(service_id),
		   resp_msg->status, resp_msg->eid);

	conn_resp->connect_resp_code = resp_msg->status;

	/* check response status */
	if (resp_msg->status != ATH10K_HTC_CONN_SVC_STATUS_SUCCESS) {
		ath10k_err(ar, "HTC Service %s connect request failed: 0x%x)\n",
			   htc_service_name(service_id),
			   resp_msg->status);
		return -EPROTO;
	}

	assigned_eid = (enum ath10k_htc_ep_id)resp_msg->eid;
	max_msg_size = __le16_to_cpu(resp_msg->max_msg_size);

setup:

	if (assigned_eid >= ATH10K_HTC_EP_COUNT)
		return -EPROTO;

	if (max_msg_size == 0)
		return -EPROTO;

	ep = &htc->endpoint[assigned_eid];
	ep->eid = assigned_eid;

	if (ep->service_id != ATH10K_HTC_SVC_ID_UNUSED)
		return -EPROTO;

	/* return assigned endpoint to caller */
	conn_resp->eid = assigned_eid;
	conn_resp->max_msg_len = __le16_to_cpu(resp_msg->max_msg_size);

	/* setup the endpoint */
	ep->service_id = conn_req->service_id;
	ep->max_tx_queue_depth = conn_req->max_send_queue_depth;
	ep->max_ep_message_len = __le16_to_cpu(resp_msg->max_msg_size);
	ep->tx_credits = tx_alloc;
	ep->tx_credit_size = htc->target_credit_size;

	if (conn_req->service_id == ATH10K_HTC_SVC_ID_HTT_DATA_MSG &&
	    htc->alt_data_credit_size != 0)
		ep->tx_credit_size = htc->alt_data_credit_size;

	/* copy all the callbacks */
	ep->ep_ops = conn_req->ep_ops;

	status = ath10k_hif_map_service_to_pipe(htc->ar,
						ep->service_id,
						&ep->ul_pipe_id,
						&ep->dl_pipe_id);
	if (status) {
		ath10k_dbg(ar, ATH10K_DBG_BOOT, "unsupported HTC service id: %d\n",
			   ep->service_id);
		return status;
	}

	ath10k_dbg(ar, ATH10K_DBG_BOOT,
		   "boot htc service '%s' ul pipe %d dl pipe %d eid %d ready\n",
		   htc_service_name(ep->service_id), ep->ul_pipe_id,
		   ep->dl_pipe_id, ep->eid);

	if (disable_credit_flow_ctrl && ep->tx_credit_flow_enabled) {
		ep->tx_credit_flow_enabled = false;
		ath10k_dbg(ar, ATH10K_DBG_BOOT,
			   "boot htc service '%s' eid %d TX flow control disabled\n",
			   htc_service_name(ep->service_id), assigned_eid);
	}

	return status;
}

struct sk_buff *ath10k_htc_alloc_skb(struct ath10k *ar, int size)
{
	struct sk_buff *skb;

	skb = dev_alloc_skb(size + sizeof(struct ath10k_htc_hdr));
	if (!skb)
		return NULL;

	skb_reserve(skb, sizeof(struct ath10k_htc_hdr));

	/* FW/HTC requires 4-byte aligned streams */
	if (!IS_ALIGNED((unsigned long)skb->data, 4))
		ath10k_warn(ar, "Unaligned HTC tx skb\n");

	return skb;
}

static void ath10k_htc_pktlog_process_rx(struct ath10k *ar, struct sk_buff *skb)
{
	trace_ath10k_htt_pktlog(ar, skb->data, skb->len);
	dev_kfree_skb_any(skb);
}

static int ath10k_htc_pktlog_connect(struct ath10k *ar)
{
	struct ath10k_htc_svc_conn_resp conn_resp;
	struct ath10k_htc_svc_conn_req conn_req;
	int status;

	memset(&conn_req, 0, sizeof(conn_req));
	memset(&conn_resp, 0, sizeof(conn_resp));

	conn_req.ep_ops.ep_tx_complete = NULL;
	conn_req.ep_ops.ep_rx_complete = ath10k_htc_pktlog_process_rx;
	conn_req.ep_ops.ep_tx_credits = NULL;

	/* connect to control service */
	conn_req.service_id = ATH10K_HTC_SVC_ID_HTT_LOG_MSG;
	status = ath10k_htc_connect_service(&ar->htc, &conn_req, &conn_resp);
	if (status) {
		ath10k_warn(ar, "failed to connect to PKTLOG service: %d\n",
			    status);
		return status;
	}

	return 0;
}

static bool ath10k_htc_pktlog_svc_supported(struct ath10k *ar)
{
	u8 ul_pipe_id;
	u8 dl_pipe_id;
	int status;

	status = ath10k_hif_map_service_to_pipe(ar, ATH10K_HTC_SVC_ID_HTT_LOG_MSG,
						&ul_pipe_id,
						&dl_pipe_id);
	if (status) {
		ath10k_dbg(ar, ATH10K_DBG_BOOT, "unsupported HTC pktlog service id: %d\n",
			   ATH10K_HTC_SVC_ID_HTT_LOG_MSG);

		return false;
	}

	return true;
}

int ath10k_htc_start(struct ath10k_htc *htc)
{
	struct ath10k *ar = htc->ar;
	struct sk_buff *skb;
	int status = 0;
	struct ath10k_htc_msg *msg;

	skb = ath10k_htc_build_tx_ctrl_skb(htc->ar);
	if (!skb)
		return -ENOMEM;

	skb_put(skb, sizeof(msg->hdr) + sizeof(msg->setup_complete_ext));
	memset(skb->data, 0, skb->len);

	msg = (struct ath10k_htc_msg *)skb->data;
	msg->hdr.message_id =
		__cpu_to_le16(ATH10K_HTC_MSG_SETUP_COMPLETE_EX_ID);

	if (ar->hif.bus == ATH10K_BUS_SDIO) {
		/* Extra setup params used by SDIO */
		msg->setup_complete_ext.flags =
			__cpu_to_le32(ATH10K_HTC_SETUP_COMPLETE_FLAGS_RX_BNDL_EN);
		msg->setup_complete_ext.max_msgs_per_bundled_recv =
			htc->max_msgs_per_htc_bundle;
	}
	ath10k_dbg(ar, ATH10K_DBG_HTC, "HTC is using TX credit flow control\n");

	status = ath10k_htc_send(htc, ATH10K_HTC_EP_0, skb);
	if (status) {
		kfree_skb(skb);
		return status;
	}

	if (ath10k_htc_pktlog_svc_supported(ar)) {
		status = ath10k_htc_pktlog_connect(ar);
		if (status) {
			ath10k_err(ar, "failed to connect to pktlog: %d\n", status);
			return status;
		}
	}

	return 0;
}

/* registered target arrival callback from the HIF layer */
int ath10k_htc_init(struct ath10k *ar)
{
	int status;
	struct ath10k_htc *htc = &ar->htc;
	struct ath10k_htc_svc_conn_req conn_req;
	struct ath10k_htc_svc_conn_resp conn_resp;

	spin_lock_init(&htc->tx_lock);

	ath10k_htc_reset_endpoint_states(htc);

	htc->ar = ar;

	/* setup our pseudo HTC control endpoint connection */
	memset(&conn_req, 0, sizeof(conn_req));
	memset(&conn_resp, 0, sizeof(conn_resp));
	conn_req.ep_ops.ep_tx_complete = ath10k_htc_control_tx_complete;
	conn_req.ep_ops.ep_rx_complete = ath10k_htc_control_rx_complete;
	conn_req.max_send_queue_depth = ATH10K_NUM_CONTROL_TX_BUFFERS;
	conn_req.service_id = ATH10K_HTC_SVC_ID_RSVD_CTRL;

	/* connect fake service */
	status = ath10k_htc_connect_service(htc, &conn_req, &conn_resp);
	if (status) {
		ath10k_err(ar, "could not connect to htc service (%d)\n",
			   status);
		return status;
	}

	init_completion(&htc->ctl_resp);

	return 0;
}
