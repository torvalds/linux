/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
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

/********/
/* Send */
/********/

static inline void ath10k_htc_send_complete_check(struct ath10k_htc_ep *ep,
						  int force)
{
	/*
	 * Check whether HIF has any prior sends that have finished,
	 * have not had the post-processing done.
	 */
	ath10k_hif_send_complete_check(ep->htc->ar, ep->ul_pipe_id, force);
}

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
	if (!skb) {
		ath10k_warn("Unable to allocate ctrl skb\n");
		return NULL;
	}

	skb_reserve(skb, 20); /* FIXME: why 20 bytes? */
	WARN_ONCE((unsigned long)skb->data & 3, "unaligned skb");

	skb_cb = ATH10K_SKB_CB(skb);
	memset(skb_cb, 0, sizeof(*skb_cb));

	ath10k_dbg(ATH10K_DBG_HTC, "%s: skb %p\n", __func__, skb);
	return skb;
}

static inline void ath10k_htc_restore_tx_skb(struct ath10k_htc *htc,
					     struct sk_buff *skb)
{
	ath10k_skb_unmap(htc->ar->dev, skb);
	skb_pull(skb, sizeof(struct ath10k_htc_hdr));
}

static void ath10k_htc_notify_tx_completion(struct ath10k_htc_ep *ep,
					    struct sk_buff *skb)
{
	ath10k_dbg(ATH10K_DBG_HTC, "%s: ep %d skb %p\n", __func__,
		   ep->eid, skb);

	ath10k_htc_restore_tx_skb(ep->htc, skb);

	if (!ep->ep_ops.ep_tx_complete) {
		ath10k_warn("no tx handler for eid %d\n", ep->eid);
		dev_kfree_skb_any(skb);
		return;
	}

	ep->ep_ops.ep_tx_complete(ep->htc->ar, skb);
}

/* assumes tx_lock is held */
static bool ath10k_htc_ep_need_credit_update(struct ath10k_htc_ep *ep)
{
	if (!ep->tx_credit_flow_enabled)
		return false;
	if (ep->tx_credits >= ep->tx_credits_per_max_message)
		return false;

	ath10k_dbg(ATH10K_DBG_HTC, "HTC: endpoint %d needs credit update\n",
		   ep->eid);
	return true;
}

static void ath10k_htc_prepare_tx_skb(struct ath10k_htc_ep *ep,
				      struct sk_buff *skb)
{
	struct ath10k_htc_hdr *hdr;

	hdr = (struct ath10k_htc_hdr *)skb->data;
	memset(hdr, 0, sizeof(*hdr));

	hdr->eid = ep->eid;
	hdr->len = __cpu_to_le16(skb->len - sizeof(*hdr));

	spin_lock_bh(&ep->htc->tx_lock);
	hdr->seq_no = ep->seq_no++;

	if (ath10k_htc_ep_need_credit_update(ep))
		hdr->flags |= ATH10K_HTC_FLAG_NEED_CREDIT_UPDATE;

	spin_unlock_bh(&ep->htc->tx_lock);
}

static int ath10k_htc_issue_skb(struct ath10k_htc *htc,
				struct ath10k_htc_ep *ep,
				struct sk_buff *skb,
				u8 credits)
{
	struct ath10k_skb_cb *skb_cb = ATH10K_SKB_CB(skb);
	int ret;

	ath10k_dbg(ATH10K_DBG_HTC, "%s: ep %d skb %p\n", __func__,
		   ep->eid, skb);

	ath10k_htc_prepare_tx_skb(ep, skb);

	ret = ath10k_skb_map(htc->ar->dev, skb);
	if (ret)
		goto err;

	ret = ath10k_hif_send_head(htc->ar,
				   ep->ul_pipe_id,
				   ep->eid,
				   skb->len,
				   skb);
	if (unlikely(ret))
		goto err;

	return 0;
err:
	ath10k_warn("HTC issue failed: %d\n", ret);

	spin_lock_bh(&htc->tx_lock);
	ep->tx_credits += credits;
	spin_unlock_bh(&htc->tx_lock);

	/* this is the simplest way to handle out-of-resources for non-credit
	 * based endpoints. credit based endpoints can still get -ENOSR, but
	 * this is highly unlikely as credit reservation should prevent that */
	if (ret == -ENOSR) {
		spin_lock_bh(&htc->tx_lock);
		__skb_queue_head(&ep->tx_queue, skb);
		spin_unlock_bh(&htc->tx_lock);

		return ret;
	}

	skb_cb->is_aborted = true;
	ath10k_htc_notify_tx_completion(ep, skb);

	return ret;
}

static struct sk_buff *ath10k_htc_get_skb_credit_based(struct ath10k_htc *htc,
						       struct ath10k_htc_ep *ep,
						       u8 *credits)
{
	struct sk_buff *skb;
	struct ath10k_skb_cb *skb_cb;
	int credits_required;
	int remainder;
	unsigned int transfer_len;

	lockdep_assert_held(&htc->tx_lock);

	skb = __skb_dequeue(&ep->tx_queue);
	if (!skb)
		return NULL;

	skb_cb = ATH10K_SKB_CB(skb);
	transfer_len = skb->len;

	if (likely(transfer_len <= htc->target_credit_size)) {
		credits_required = 1;
	} else {
		/* figure out how many credits this message requires */
		credits_required = transfer_len / htc->target_credit_size;
		remainder = transfer_len % htc->target_credit_size;

		if (remainder)
			credits_required++;
	}

	ath10k_dbg(ATH10K_DBG_HTC, "Credits required %d got %d\n",
		   credits_required, ep->tx_credits);

	if (ep->tx_credits < credits_required) {
		__skb_queue_head(&ep->tx_queue, skb);
		return NULL;
	}

	ep->tx_credits -= credits_required;
	*credits = credits_required;
	return skb;
}

static void ath10k_htc_send_work(struct work_struct *work)
{
	struct ath10k_htc_ep *ep = container_of(work,
					struct ath10k_htc_ep, send_work);
	struct ath10k_htc *htc = ep->htc;
	struct sk_buff *skb;
	u8 credits = 0;
	int ret;

	while (true) {
		if (ep->ul_is_polled)
			ath10k_htc_send_complete_check(ep, 0);

		spin_lock_bh(&htc->tx_lock);
		if (ep->tx_credit_flow_enabled)
			skb = ath10k_htc_get_skb_credit_based(htc, ep,
							      &credits);
		else
			skb = __skb_dequeue(&ep->tx_queue);
		spin_unlock_bh(&htc->tx_lock);

		if (!skb)
			break;

		ret = ath10k_htc_issue_skb(htc, ep, skb, credits);
		if (ret == -ENOSR)
			break;
	}
}

int ath10k_htc_send(struct ath10k_htc *htc,
		    enum ath10k_htc_ep_id eid,
		    struct sk_buff *skb)
{
	struct ath10k_htc_ep *ep = &htc->endpoint[eid];

	if (htc->ar->state == ATH10K_STATE_WEDGED)
		return -ECOMM;

	if (eid >= ATH10K_HTC_EP_COUNT) {
		ath10k_warn("Invalid endpoint id: %d\n", eid);
		return -ENOENT;
	}

	spin_lock_bh(&htc->tx_lock);
	if (htc->stopped) {
		spin_unlock_bh(&htc->tx_lock);
		return -ESHUTDOWN;
	}

	__skb_queue_tail(&ep->tx_queue, skb);
	skb_push(skb, sizeof(struct ath10k_htc_hdr));
	spin_unlock_bh(&htc->tx_lock);

	queue_work(htc->ar->workqueue, &ep->send_work);
	return 0;
}

static int ath10k_htc_tx_completion_handler(struct ath10k *ar,
					    struct sk_buff *skb,
					    unsigned int eid)
{
	struct ath10k_htc *htc = &ar->htc;
	struct ath10k_htc_ep *ep = &htc->endpoint[eid];

	ath10k_htc_notify_tx_completion(ep, skb);
	/* the skb now belongs to the completion handler */

	/* note: when using TX credit flow, the re-checking of queues happens
	 * when credits flow back from the target.  in the non-TX credit case,
	 * we recheck after the packet completes */
	spin_lock_bh(&htc->tx_lock);
	if (!ep->tx_credit_flow_enabled && !htc->stopped)
		queue_work(ar->workqueue, &ep->send_work);
	spin_unlock_bh(&htc->tx_lock);

	return 0;
}

/* flush endpoint TX queue */
static void ath10k_htc_flush_endpoint_tx(struct ath10k_htc *htc,
					 struct ath10k_htc_ep *ep)
{
	struct sk_buff *skb;
	struct ath10k_skb_cb *skb_cb;

	spin_lock_bh(&htc->tx_lock);
	for (;;) {
		skb = __skb_dequeue(&ep->tx_queue);
		if (!skb)
			break;

		skb_cb = ATH10K_SKB_CB(skb);
		skb_cb->is_aborted = true;
		ath10k_htc_notify_tx_completion(ep, skb);
	}
	spin_unlock_bh(&htc->tx_lock);

	cancel_work_sync(&ep->send_work);
}

/***********/
/* Receive */
/***********/

static void
ath10k_htc_process_credit_report(struct ath10k_htc *htc,
				 const struct ath10k_htc_credit_report *report,
				 int len,
				 enum ath10k_htc_ep_id eid)
{
	struct ath10k_htc_ep *ep;
	int i, n_reports;

	if (len % sizeof(*report))
		ath10k_warn("Uneven credit report len %d", len);

	n_reports = len / sizeof(*report);

	spin_lock_bh(&htc->tx_lock);
	for (i = 0; i < n_reports; i++, report++) {
		if (report->eid >= ATH10K_HTC_EP_COUNT)
			break;

		ath10k_dbg(ATH10K_DBG_HTC, "ep %d got %d credits\n",
			   report->eid, report->credits);

		ep = &htc->endpoint[report->eid];
		ep->tx_credits += report->credits;

		if (ep->tx_credits && !skb_queue_empty(&ep->tx_queue))
			queue_work(htc->ar->workqueue, &ep->send_work);
	}
	spin_unlock_bh(&htc->tx_lock);
}

static int ath10k_htc_process_trailer(struct ath10k_htc *htc,
				      u8 *buffer,
				      int length,
				      enum ath10k_htc_ep_id src_eid)
{
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
			ath10k_warn("Invalid record length: %d\n",
				    record->hdr.len);
			status = -EINVAL;
			break;
		}

		switch (record->hdr.id) {
		case ATH10K_HTC_RECORD_CREDITS:
			len = sizeof(struct ath10k_htc_credit_report);
			if (record->hdr.len < len) {
				ath10k_warn("Credit report too long\n");
				status = -EINVAL;
				break;
			}
			ath10k_htc_process_credit_report(htc,
							 record->credit_report,
							 record->hdr.len,
							 src_eid);
			break;
		default:
			ath10k_warn("Unhandled record: id:%d length:%d\n",
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
		ath10k_dbg_dump(ATH10K_DBG_HTC, "htc rx bad trailer", "",
				orig_buffer, orig_length);

	return status;
}

static int ath10k_htc_rx_completion_handler(struct ath10k *ar,
					    struct sk_buff *skb,
					    u8 pipe_id)
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
		ath10k_warn("HTC Rx: invalid eid %d\n", eid);
		ath10k_dbg_dump(ATH10K_DBG_HTC, "htc bad header", "",
				hdr, sizeof(*hdr));
		status = -EINVAL;
		goto out;
	}

	ep = &htc->endpoint[eid];

	/*
	 * If this endpoint that received a message from the target has
	 * a to-target HIF pipe whose send completions are polled rather
	 * than interrupt-driven, this is a good point to ask HIF to check
	 * whether it has any completed sends to handle.
	 */
	if (ep->ul_is_polled)
		ath10k_htc_send_complete_check(ep, 1);

	payload_len = __le16_to_cpu(hdr->len);

	if (payload_len + sizeof(*hdr) > ATH10K_HTC_MAX_LEN) {
		ath10k_warn("HTC rx frame too long, len: %zu\n",
			    payload_len + sizeof(*hdr));
		ath10k_dbg_dump(ATH10K_DBG_HTC, "htc bad rx pkt len", "",
				hdr, sizeof(*hdr));
		status = -EINVAL;
		goto out;
	}

	if (skb->len < payload_len) {
		ath10k_dbg(ATH10K_DBG_HTC,
			   "HTC Rx: insufficient length, got %d, expected %d\n",
			   skb->len, payload_len);
		ath10k_dbg_dump(ATH10K_DBG_HTC, "htc bad rx pkt len",
				"", hdr, sizeof(*hdr));
		status = -EINVAL;
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
			ath10k_warn("Invalid trailer length: %d\n",
				    trailer_len);
			status = -EPROTO;
			goto out;
		}

		trailer = (u8 *)hdr;
		trailer += sizeof(*hdr);
		trailer += payload_len;
		trailer -= trailer_len;
		status = ath10k_htc_process_trailer(htc, trailer,
						    trailer_len, hdr->eid);
		if (status)
			goto out;

		skb_trim(skb, skb->len - trailer_len);
	}

	if (((int)payload_len - (int)trailer_len) <= 0)
		/* zero length packet with trailer data, just drop these */
		goto out;

	if (eid == ATH10K_HTC_EP_0) {
		struct ath10k_htc_msg *msg = (struct ath10k_htc_msg *)skb->data;

		switch (__le16_to_cpu(msg->hdr.message_id)) {
		default:
			/* handle HTC control message */
			if (completion_done(&htc->ctl_resp)) {
				/*
				 * this is a fatal error, target should not be
				 * sending unsolicited messages on the ep 0
				 */
				ath10k_warn("HTC rx ctrl still processing\n");
				status = -EINVAL;
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
		}
		goto out;
	}

	ath10k_dbg(ATH10K_DBG_HTC, "htc rx completion ep %d skb %p\n",
		   eid, skb);
	ep->ep_ops.ep_rx_complete(ar, skb);

	/* skb is now owned by the rx completion handler */
	skb = NULL;
out:
	kfree_skb(skb);

	return status;
}

static void ath10k_htc_control_rx_complete(struct ath10k *ar,
					   struct sk_buff *skb)
{
	/* This is unexpected. FW is not supposed to send regular rx on this
	 * endpoint. */
	ath10k_warn("unexpected htc rx\n");
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
	case ATH10K_HTC_SVC_ID_TEST_RAW_STREAMS:
		return "RAW";
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
		skb_queue_head_init(&ep->tx_queue);
		ep->htc = htc;
		ep->tx_credit_flow_enabled = true;
		INIT_WORK(&ep->send_work, ath10k_htc_send_work);
	}
}

static void ath10k_htc_setup_target_buffer_assignments(struct ath10k_htc *htc)
{
	struct ath10k_htc_svc_tx_credits *entry;

	entry = &htc->service_tx_alloc[0];

	/*
	 * for PCIE allocate all credists/HTC buffers to WMI.
	 * no buffers are used/required for data. data always
	 * remains on host.
	 */
	entry++;
	entry->service_id = ATH10K_HTC_SVC_ID_WMI_CONTROL;
	entry->credit_allocation = htc->total_transmit_credits;
}

static u8 ath10k_htc_get_credit_allocation(struct ath10k_htc *htc,
					   u16 service_id)
{
	u8 allocation = 0;
	int i;

	for (i = 0; i < ATH10K_HTC_EP_COUNT; i++) {
		if (htc->service_tx_alloc[i].service_id == service_id)
			allocation =
			    htc->service_tx_alloc[i].credit_allocation;
	}

	return allocation;
}

int ath10k_htc_wait_target(struct ath10k_htc *htc)
{
	int status = 0;
	struct ath10k_htc_svc_conn_req conn_req;
	struct ath10k_htc_svc_conn_resp conn_resp;
	struct ath10k_htc_msg *msg;
	u16 message_id;
	u16 credit_count;
	u16 credit_size;

	INIT_COMPLETION(htc->ctl_resp);

	status = ath10k_hif_start(htc->ar);
	if (status) {
		ath10k_err("could not start HIF (%d)\n", status);
		goto err_start;
	}

	status = wait_for_completion_timeout(&htc->ctl_resp,
					     ATH10K_HTC_WAIT_TIMEOUT_HZ);
	if (status <= 0) {
		if (status == 0)
			status = -ETIMEDOUT;

		ath10k_err("ctl_resp never came in (%d)\n", status);
		goto err_target;
	}

	if (htc->control_resp_len < sizeof(msg->hdr) + sizeof(msg->ready)) {
		ath10k_err("Invalid HTC ready msg len:%d\n",
			   htc->control_resp_len);

		status = -ECOMM;
		goto err_target;
	}

	msg = (struct ath10k_htc_msg *)htc->control_resp_buffer;
	message_id   = __le16_to_cpu(msg->hdr.message_id);
	credit_count = __le16_to_cpu(msg->ready.credit_count);
	credit_size  = __le16_to_cpu(msg->ready.credit_size);

	if (message_id != ATH10K_HTC_MSG_READY_ID) {
		ath10k_err("Invalid HTC ready msg: 0x%x\n", message_id);
		status = -ECOMM;
		goto err_target;
	}

	htc->total_transmit_credits = credit_count;
	htc->target_credit_size = credit_size;

	ath10k_dbg(ATH10K_DBG_HTC,
		   "Target ready! transmit resources: %d size:%d\n",
		   htc->total_transmit_credits,
		   htc->target_credit_size);

	if ((htc->total_transmit_credits == 0) ||
	    (htc->target_credit_size == 0)) {
		status = -ECOMM;
		ath10k_err("Invalid credit size received\n");
		goto err_target;
	}

	ath10k_htc_setup_target_buffer_assignments(htc);

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
		ath10k_err("could not connect to htc service (%d)\n", status);
		goto err_target;
	}

	return 0;
err_target:
	ath10k_hif_stop(htc->ar);
err_start:
	return status;
}

int ath10k_htc_connect_service(struct ath10k_htc *htc,
			       struct ath10k_htc_svc_conn_req *conn_req,
			       struct ath10k_htc_svc_conn_resp *conn_resp)
{
	struct ath10k_htc_msg *msg;
	struct ath10k_htc_conn_svc *req_msg;
	struct ath10k_htc_conn_svc_response resp_msg_dummy;
	struct ath10k_htc_conn_svc_response *resp_msg = &resp_msg_dummy;
	enum ath10k_htc_ep_id assigned_eid = ATH10K_HTC_EP_COUNT;
	struct ath10k_htc_ep *ep;
	struct sk_buff *skb;
	unsigned int max_msg_size = 0;
	int length, status;
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
		ath10k_dbg(ATH10K_DBG_HTC,
			   "HTC Service %s does not allocate target credits\n",
			   htc_service_name(conn_req->service_id));

	skb = ath10k_htc_build_tx_ctrl_skb(htc->ar);
	if (!skb) {
		ath10k_err("Failed to allocate HTC packet\n");
		return -ENOMEM;
	}

	length = sizeof(msg->hdr) + sizeof(msg->connect_service);
	skb_put(skb, length);
	memset(skb->data, 0, length);

	msg = (struct ath10k_htc_msg *)skb->data;
	msg->hdr.message_id =
		__cpu_to_le16(ATH10K_HTC_MSG_CONNECT_SERVICE_ID);

	flags |= SM(tx_alloc, ATH10K_HTC_CONN_FLAGS_RECV_ALLOC);

	req_msg = &msg->connect_service;
	req_msg->flags = __cpu_to_le16(flags);
	req_msg->service_id = __cpu_to_le16(conn_req->service_id);

	/* Only enable credit flow control for WMI ctrl service */
	if (conn_req->service_id != ATH10K_HTC_SVC_ID_WMI_CONTROL) {
		flags |= ATH10K_HTC_CONN_FLAGS_DISABLE_CREDIT_FLOW_CTRL;
		disable_credit_flow_ctrl = true;
	}

	INIT_COMPLETION(htc->ctl_resp);

	status = ath10k_htc_send(htc, ATH10K_HTC_EP_0, skb);
	if (status) {
		kfree_skb(skb);
		return status;
	}

	/* wait for response */
	status = wait_for_completion_timeout(&htc->ctl_resp,
					     ATH10K_HTC_CONN_SVC_TIMEOUT_HZ);
	if (status <= 0) {
		if (status == 0)
			status = -ETIMEDOUT;
		ath10k_err("Service connect timeout: %d\n", status);
		return status;
	}

	/* we controlled the buffer creation, it's aligned */
	msg = (struct ath10k_htc_msg *)htc->control_resp_buffer;
	resp_msg = &msg->connect_service_response;
	message_id = __le16_to_cpu(msg->hdr.message_id);
	service_id = __le16_to_cpu(resp_msg->service_id);

	if ((message_id != ATH10K_HTC_MSG_CONNECT_SERVICE_RESP_ID) ||
	    (htc->control_resp_len < sizeof(msg->hdr) +
	     sizeof(msg->connect_service_response))) {
		ath10k_err("Invalid resp message ID 0x%x", message_id);
		return -EPROTO;
	}

	ath10k_dbg(ATH10K_DBG_HTC,
		   "HTC Service %s connect response: status: 0x%x, assigned ep: 0x%x\n",
		   htc_service_name(service_id),
		   resp_msg->status, resp_msg->eid);

	conn_resp->connect_resp_code = resp_msg->status;

	/* check response status */
	if (resp_msg->status != ATH10K_HTC_CONN_SVC_STATUS_SUCCESS) {
		ath10k_err("HTC Service %s connect request failed: 0x%x)\n",
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
	ep->tx_credits_per_max_message = ep->max_ep_message_len /
					 htc->target_credit_size;

	if (ep->max_ep_message_len % htc->target_credit_size)
		ep->tx_credits_per_max_message++;

	/* copy all the callbacks */
	ep->ep_ops = conn_req->ep_ops;

	status = ath10k_hif_map_service_to_pipe(htc->ar,
						ep->service_id,
						&ep->ul_pipe_id,
						&ep->dl_pipe_id,
						&ep->ul_is_polled,
						&ep->dl_is_polled);
	if (status)
		return status;

	ath10k_dbg(ATH10K_DBG_HTC,
		   "HTC service: %s UL pipe: %d DL pipe: %d eid: %d ready\n",
		   htc_service_name(ep->service_id), ep->ul_pipe_id,
		   ep->dl_pipe_id, ep->eid);

	ath10k_dbg(ATH10K_DBG_HTC,
		   "EP %d UL polled: %d, DL polled: %d\n",
		   ep->eid, ep->ul_is_polled, ep->dl_is_polled);

	if (disable_credit_flow_ctrl && ep->tx_credit_flow_enabled) {
		ep->tx_credit_flow_enabled = false;
		ath10k_dbg(ATH10K_DBG_HTC,
			   "HTC service: %s eid: %d TX flow control disabled\n",
			   htc_service_name(ep->service_id), assigned_eid);
	}

	return status;
}

struct sk_buff *ath10k_htc_alloc_skb(int size)
{
	struct sk_buff *skb;

	skb = dev_alloc_skb(size + sizeof(struct ath10k_htc_hdr));
	if (!skb) {
		ath10k_warn("could not allocate HTC tx skb\n");
		return NULL;
	}

	skb_reserve(skb, sizeof(struct ath10k_htc_hdr));

	/* FW/HTC requires 4-byte aligned streams */
	if (!IS_ALIGNED((unsigned long)skb->data, 4))
		ath10k_warn("Unaligned HTC tx skb\n");

	return skb;
}

int ath10k_htc_start(struct ath10k_htc *htc)
{
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

	ath10k_dbg(ATH10K_DBG_HTC, "HTC is using TX credit flow control\n");

	status = ath10k_htc_send(htc, ATH10K_HTC_EP_0, skb);
	if (status) {
		kfree_skb(skb);
		return status;
	}

	return 0;
}

/*
 * stop HTC communications, i.e. stop interrupt reception, and flush all
 * queued buffers
 */
void ath10k_htc_stop(struct ath10k_htc *htc)
{
	int i;
	struct ath10k_htc_ep *ep;

	spin_lock_bh(&htc->tx_lock);
	htc->stopped = true;
	spin_unlock_bh(&htc->tx_lock);

	for (i = ATH10K_HTC_EP_0; i < ATH10K_HTC_EP_COUNT; i++) {
		ep = &htc->endpoint[i];
		ath10k_htc_flush_endpoint_tx(htc, ep);
	}

	ath10k_hif_stop(htc->ar);
}

/* registered target arrival callback from the HIF layer */
int ath10k_htc_init(struct ath10k *ar)
{
	struct ath10k_hif_cb htc_callbacks;
	struct ath10k_htc_ep *ep = NULL;
	struct ath10k_htc *htc = &ar->htc;

	spin_lock_init(&htc->tx_lock);

	htc->stopped = false;
	ath10k_htc_reset_endpoint_states(htc);

	/* setup HIF layer callbacks */
	htc_callbacks.rx_completion = ath10k_htc_rx_completion_handler;
	htc_callbacks.tx_completion = ath10k_htc_tx_completion_handler;
	htc->ar = ar;

	/* Get HIF default pipe for HTC message exchange */
	ep = &htc->endpoint[ATH10K_HTC_EP_0];

	ath10k_hif_set_callbacks(ar, &htc_callbacks);
	ath10k_hif_get_default_pipe(ar, &ep->ul_pipe_id, &ep->dl_pipe_id);

	init_completion(&htc->ctl_resp);

	return 0;
}
