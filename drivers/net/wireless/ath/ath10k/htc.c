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

	ath10k_dbg(ar, ATH10K_DBG_HTC, "%s: skb %pK\n", __func__, skb);
	return skb;
}

static inline void ath10k_htc_restore_tx_skb(struct ath10k_htc *htc,
					     struct sk_buff *skb)
{
	struct ath10k_skb_cb *skb_cb = ATH10K_SKB_CB(skb);

	dma_unmap_single(htc->ar->dev, skb_cb->paddr, skb->len, DMA_TO_DEVICE);
	skb_pull(skb, sizeof(struct ath10k_htc_hdr));
}

static void ath10k_htc_notify_tx_completion(struct ath10k_htc_ep *ep,
					    struct sk_buff *skb)
{
	struct ath10k *ar = ep->htc->ar;

	ath10k_dbg(ar, ATH10K_DBG_HTC, "%s: ep %d skb %pK\n", __func__,
		   ep->eid, skb);

	ath10k_htc_restore_tx_skb(ep->htc, skb);

	if (!ep->ep_ops.ep_tx_complete) {
		ath10k_warn(ar, "no tx handler for eid %d\n", ep->eid);
		dev_kfree_skb_any(skb);
		return;
	}

	ep->ep_ops.ep_tx_complete(ep->htc->ar, skb);
}

static void ath10k_htc_prepare_tx_skb(struct ath10k_htc_ep *ep,
				      struct sk_buff *skb)
{
	struct ath10k_htc_hdr *hdr;

	hdr = (struct ath10k_htc_hdr *)skb->data;

	hdr->eid = ep->eid;
	hdr->len = __cpu_to_le16(skb->len - sizeof(*hdr));
	hdr->flags = 0;
	hdr->flags |= ATH10K_HTC_FLAG_NEED_CREDIT_UPDATE;

	spin_lock_bh(&ep->htc->tx_lock);
	hdr->seq_no = ep->seq_no++;
	spin_unlock_bh(&ep->htc->tx_lock);
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
	int credits = 0;
	int ret;

	if (htc->ar->state == ATH10K_STATE_WEDGED)
		return -ECOMM;

	if (eid >= ATH10K_HTC_EP_COUNT) {
		ath10k_warn(ar, "Invalid endpoint id: %d\n", eid);
		return -ENOENT;
	}

	skb_push(skb, sizeof(struct ath10k_htc_hdr));

	if (ep->tx_credit_flow_enabled) {
		credits = DIV_ROUND_UP(skb->len, htc->target_credit_size);
		spin_lock_bh(&htc->tx_lock);
		if (ep->tx_credits < credits) {
			spin_unlock_bh(&htc->tx_lock);
			ret = -EAGAIN;
			goto err_pull;
		}
		ep->tx_credits -= credits;
		ath10k_dbg(ar, ATH10K_DBG_HTC,
			   "htc ep %d consumed %d credits (total %d)\n",
			   eid, credits, ep->tx_credits);
		spin_unlock_bh(&htc->tx_lock);
	}

	ath10k_htc_prepare_tx_skb(ep, skb);

	skb_cb->eid = eid;
	skb_cb->paddr = dma_map_single(dev, skb->data, skb->len, DMA_TO_DEVICE);
	ret = dma_mapping_error(dev, skb_cb->paddr);
	if (ret) {
		ret = -EIO;
		goto err_credits;
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
	dma_unmap_single(dev, skb_cb->paddr, skb->len, DMA_TO_DEVICE);
err_credits:
	if (ep->tx_credit_flow_enabled) {
		spin_lock_bh(&htc->tx_lock);
		ep->tx_credits += credits;
		ath10k_dbg(ar, ATH10K_DBG_HTC,
			   "htc ep %d reverted %d credits back (total %d)\n",
			   eid, credits, ep->tx_credits);
		spin_unlock_bh(&htc->tx_lock);

		if (ep->ep_ops.ep_tx_credits)
			ep->ep_ops.ep_tx_credits(htc->ar);
	}
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

static int ath10k_htc_process_trailer(struct ath10k_htc *htc,
				      u8 *buffer,
				      int length,
				      enum ath10k_htc_ep_id src_eid)
{
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
		case ATH10K_HTC_MSG_READY_ID:
		case ATH10K_HTC_MSG_CONNECT_SERVICE_RESP_ID:
			/* handle HTC control message */
			if (completion_done(&htc->ctl_resp)) {
				/*
				 * this is a fatal error, target should not be
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
		goto out;
	}

	ath10k_dbg(ar, ATH10K_DBG_HTC, "htc rx completion ep %d skb %pK\n",
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
	/* This is unexpected. FW is not supposed to send regular rx on this
	 * endpoint. */
	ath10k_warn(ar, "unexpected htc rx\n");
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

int ath10k_htc_wait_target(struct ath10k_htc *htc)
{
	struct ath10k *ar = htc->ar;
	int i, status = 0;
	unsigned long time_left;
	struct ath10k_htc_svc_conn_req conn_req;
	struct ath10k_htc_svc_conn_resp conn_resp;
	struct ath10k_htc_msg *msg;
	u16 message_id;
	u16 credit_count;
	u16 credit_size;

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
	credit_count = __le16_to_cpu(msg->ready.credit_count);
	credit_size  = __le16_to_cpu(msg->ready.credit_size);

	if (message_id != ATH10K_HTC_MSG_READY_ID) {
		ath10k_err(ar, "Invalid HTC ready msg: 0x%x\n", message_id);
		return -ECOMM;
	}

	htc->total_transmit_credits = credit_count;
	htc->target_credit_size = credit_size;

	ath10k_dbg(ar, ATH10K_DBG_HTC,
		   "Target ready! transmit resources: %d size:%d\n",
		   htc->total_transmit_credits,
		   htc->target_credit_size);

	if ((htc->total_transmit_credits == 0) ||
	    (htc->target_credit_size == 0)) {
		ath10k_err(ar, "Invalid credit size received\n");
		return -ECOMM;
	}

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

	return 0;
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

	/* copy all the callbacks */
	ep->ep_ops = conn_req->ep_ops;

	status = ath10k_hif_map_service_to_pipe(htc->ar,
						ep->service_id,
						&ep->ul_pipe_id,
						&ep->dl_pipe_id);
	if (status)
		return status;

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

	ath10k_dbg(ar, ATH10K_DBG_HTC, "HTC is using TX credit flow control\n");

	status = ath10k_htc_send(htc, ATH10K_HTC_EP_0, skb);
	if (status) {
		kfree_skb(skb);
		return status;
	}

	return 0;
}

/* registered target arrival callback from the HIF layer */
int ath10k_htc_init(struct ath10k *ar)
{
	struct ath10k_htc_ep *ep = NULL;
	struct ath10k_htc *htc = &ar->htc;

	spin_lock_init(&htc->tx_lock);

	ath10k_htc_reset_endpoint_states(htc);

	htc->ar = ar;

	/* Get HIF default pipe for HTC message exchange */
	ep = &htc->endpoint[ATH10K_HTC_EP_0];

	ath10k_hif_get_default_pipe(ar, &ep->ul_pipe_id, &ep->dl_pipe_id);

	init_completion(&htc->ctl_resp);

	return 0;
}
