// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */
#include <linux/skbuff.h>
#include <linux/ctype.h>

#include "debug.h"
#include "hif.h"

struct sk_buff *ath11k_htc_alloc_skb(struct ath11k_base *ab, int size)
{
	struct sk_buff *skb;

	skb = dev_alloc_skb(size + sizeof(struct ath11k_htc_hdr));
	if (!skb)
		return NULL;

	skb_reserve(skb, sizeof(struct ath11k_htc_hdr));

	/* FW/HTC requires 4-byte aligned streams */
	if (!IS_ALIGNED((unsigned long)skb->data, 4))
		ath11k_warn(ab, "Unaligned HTC tx skb\n");

	return skb;
}

static void ath11k_htc_control_tx_complete(struct ath11k_base *ab,
					   struct sk_buff *skb)
{
	kfree_skb(skb);
}

static struct sk_buff *ath11k_htc_build_tx_ctrl_skb(void *ab)
{
	struct sk_buff *skb;
	struct ath11k_skb_cb *skb_cb;

	skb = dev_alloc_skb(ATH11K_HTC_CONTROL_BUFFER_SIZE);
	if (!skb)
		return NULL;

	skb_reserve(skb, sizeof(struct ath11k_htc_hdr));
	WARN_ON_ONCE(!IS_ALIGNED((unsigned long)skb->data, 4));

	skb_cb = ATH11K_SKB_CB(skb);
	memset(skb_cb, 0, sizeof(*skb_cb));

	ath11k_dbg(ab, ATH11K_DBG_HTC, "%s: skb %pK\n", __func__, skb);
	return skb;
}

static void ath11k_htc_prepare_tx_skb(struct ath11k_htc_ep *ep,
				      struct sk_buff *skb)
{
	struct ath11k_htc_hdr *hdr;

	hdr = (struct ath11k_htc_hdr *)skb->data;

	memset(hdr, 0, sizeof(*hdr));
	hdr->htc_info = FIELD_PREP(HTC_HDR_ENDPOINTID, ep->eid) |
			FIELD_PREP(HTC_HDR_PAYLOADLEN,
				   (skb->len - sizeof(*hdr)));

	if (ep->tx_credit_flow_enabled)
		hdr->htc_info |= FIELD_PREP(HTC_HDR_FLAGS,
					    ATH11K_HTC_FLAG_NEED_CREDIT_UPDATE);

	spin_lock_bh(&ep->htc->tx_lock);
	hdr->ctrl_info = FIELD_PREP(HTC_HDR_CONTROLBYTES1, ep->seq_no++);
	spin_unlock_bh(&ep->htc->tx_lock);
}

int ath11k_htc_send(struct ath11k_htc *htc,
		    enum ath11k_htc_ep_id eid,
		    struct sk_buff *skb)
{
	struct ath11k_htc_ep *ep = &htc->endpoint[eid];
	struct ath11k_skb_cb *skb_cb = ATH11K_SKB_CB(skb);
	struct device *dev = htc->ab->dev;
	struct ath11k_base *ab = htc->ab;
	int credits = 0;
	int ret;
	bool credit_flow_enabled = (ab->hw_params.credit_flow &&
				    ep->tx_credit_flow_enabled);

	if (eid >= ATH11K_HTC_EP_COUNT) {
		ath11k_warn(ab, "Invalid endpoint id: %d\n", eid);
		return -ENOENT;
	}

	skb_push(skb, sizeof(struct ath11k_htc_hdr));

	if (credit_flow_enabled) {
		credits = DIV_ROUND_UP(skb->len, htc->target_credit_size);
		spin_lock_bh(&htc->tx_lock);
		if (ep->tx_credits < credits) {
			ath11k_dbg(ab, ATH11K_DBG_HTC,
				   "htc insufficient credits ep %d required %d available %d\n",
				   eid, credits, ep->tx_credits);
			spin_unlock_bh(&htc->tx_lock);
			ret = -EAGAIN;
			goto err_pull;
		}
		ep->tx_credits -= credits;
		ath11k_dbg(ab, ATH11K_DBG_HTC,
			   "htc ep %d consumed %d credits (total %d)\n",
			   eid, credits, ep->tx_credits);
		spin_unlock_bh(&htc->tx_lock);
	}

	ath11k_htc_prepare_tx_skb(ep, skb);

	skb_cb->eid = eid;
	skb_cb->paddr = dma_map_single(dev, skb->data, skb->len, DMA_TO_DEVICE);
	ret = dma_mapping_error(dev, skb_cb->paddr);
	if (ret) {
		ret = -EIO;
		goto err_credits;
	}

	ret = ath11k_ce_send(htc->ab, skb, ep->ul_pipe_id, ep->eid);
	if (ret)
		goto err_unmap;

	return 0;

err_unmap:
	dma_unmap_single(dev, skb_cb->paddr, skb->len, DMA_TO_DEVICE);
err_credits:
	if (credit_flow_enabled) {
		spin_lock_bh(&htc->tx_lock);
		ep->tx_credits += credits;
		ath11k_dbg(ab, ATH11K_DBG_HTC,
			   "htc ep %d reverted %d credits back (total %d)\n",
			   eid, credits, ep->tx_credits);
		spin_unlock_bh(&htc->tx_lock);

		if (ep->ep_ops.ep_tx_credits)
			ep->ep_ops.ep_tx_credits(htc->ab);
	}
err_pull:
	skb_pull(skb, sizeof(struct ath11k_htc_hdr));
	return ret;
}

static void
ath11k_htc_process_credit_report(struct ath11k_htc *htc,
				 const struct ath11k_htc_credit_report *report,
				 int len,
				 enum ath11k_htc_ep_id eid)
{
	struct ath11k_base *ab = htc->ab;
	struct ath11k_htc_ep *ep;
	int i, n_reports;

	if (len % sizeof(*report))
		ath11k_warn(ab, "Uneven credit report len %d", len);

	n_reports = len / sizeof(*report);

	spin_lock_bh(&htc->tx_lock);
	for (i = 0; i < n_reports; i++, report++) {
		if (report->eid >= ATH11K_HTC_EP_COUNT)
			break;

		ep = &htc->endpoint[report->eid];
		ep->tx_credits += report->credits;

		ath11k_dbg(ab, ATH11K_DBG_HTC, "htc ep %d got %d credits (total %d)\n",
			   report->eid, report->credits, ep->tx_credits);

		if (ep->ep_ops.ep_tx_credits) {
			spin_unlock_bh(&htc->tx_lock);
			ep->ep_ops.ep_tx_credits(htc->ab);
			spin_lock_bh(&htc->tx_lock);
		}
	}
	spin_unlock_bh(&htc->tx_lock);
}

static int ath11k_htc_process_trailer(struct ath11k_htc *htc,
				      u8 *buffer,
				      int length,
				      enum ath11k_htc_ep_id src_eid)
{
	struct ath11k_base *ab = htc->ab;
	int status = 0;
	struct ath11k_htc_record *record;
	size_t len;

	while (length > 0) {
		record = (struct ath11k_htc_record *)buffer;

		if (length < sizeof(record->hdr)) {
			status = -EINVAL;
			break;
		}

		if (record->hdr.len > length) {
			/* no room left in buffer for record */
			ath11k_warn(ab, "Invalid record length: %d\n",
				    record->hdr.len);
			status = -EINVAL;
			break;
		}

		if (ab->hw_params.credit_flow) {
			switch (record->hdr.id) {
			case ATH11K_HTC_RECORD_CREDITS:
				len = sizeof(struct ath11k_htc_credit_report);
				if (record->hdr.len < len) {
					ath11k_warn(ab, "Credit report too long\n");
					status = -EINVAL;
					break;
				}
				ath11k_htc_process_credit_report(htc,
								 record->credit_report,
								 record->hdr.len,
								 src_eid);
				break;
			default:
				ath11k_warn(ab, "Unhandled record: id:%d length:%d\n",
					    record->hdr.id, record->hdr.len);
				break;
			}
		}

		if (status)
			break;

		/* multiple records may be present in a trailer */
		buffer += sizeof(record->hdr) + record->hdr.len;
		length -= sizeof(record->hdr) + record->hdr.len;
	}

	return status;
}

static void ath11k_htc_suspend_complete(struct ath11k_base *ab, bool ack)
{
	ath11k_dbg(ab, ATH11K_DBG_BOOT, "boot suspend complete %d\n", ack);

	if (ack)
		set_bit(ATH11K_FLAG_HTC_SUSPEND_COMPLETE, &ab->dev_flags);
	else
		clear_bit(ATH11K_FLAG_HTC_SUSPEND_COMPLETE, &ab->dev_flags);

	complete(&ab->htc_suspend);
}

void ath11k_htc_tx_completion_handler(struct ath11k_base *ab,
				      struct sk_buff *skb)
{
	struct ath11k_htc *htc = &ab->htc;
	struct ath11k_htc_ep *ep;
	void (*ep_tx_complete)(struct ath11k_base *, struct sk_buff *);
	u8 eid;

	eid = ATH11K_SKB_CB(skb)->eid;
	if (eid >= ATH11K_HTC_EP_COUNT) {
		dev_kfree_skb_any(skb);
		return;
	}

	ep = &htc->endpoint[eid];
	spin_lock_bh(&htc->tx_lock);
	ep_tx_complete = ep->ep_ops.ep_tx_complete;
	spin_unlock_bh(&htc->tx_lock);
	if (!ep_tx_complete) {
		dev_kfree_skb_any(skb);
		return;
	}
	ep_tx_complete(htc->ab, skb);
}

static void ath11k_htc_wakeup_from_suspend(struct ath11k_base *ab)
{
	ath11k_dbg(ab, ATH11K_DBG_BOOT, "boot wakeup from suspend is received\n");
}

void ath11k_htc_rx_completion_handler(struct ath11k_base *ab,
				      struct sk_buff *skb)
{
	int status = 0;
	struct ath11k_htc *htc = &ab->htc;
	struct ath11k_htc_hdr *hdr;
	struct ath11k_htc_ep *ep;
	u16 payload_len;
	u32 trailer_len = 0;
	size_t min_len;
	u8 eid;
	bool trailer_present;

	hdr = (struct ath11k_htc_hdr *)skb->data;
	skb_pull(skb, sizeof(*hdr));

	eid = FIELD_GET(HTC_HDR_ENDPOINTID, hdr->htc_info);

	if (eid >= ATH11K_HTC_EP_COUNT) {
		ath11k_warn(ab, "HTC Rx: invalid eid %d\n", eid);
		goto out;
	}

	ep = &htc->endpoint[eid];

	payload_len = FIELD_GET(HTC_HDR_PAYLOADLEN, hdr->htc_info);

	if (payload_len + sizeof(*hdr) > ATH11K_HTC_MAX_LEN) {
		ath11k_warn(ab, "HTC rx frame too long, len: %zu\n",
			    payload_len + sizeof(*hdr));
		goto out;
	}

	if (skb->len < payload_len) {
		ath11k_warn(ab, "HTC Rx: insufficient length, got %d, expected %d\n",
			    skb->len, payload_len);
		goto out;
	}

	/* get flags to check for trailer */
	trailer_present = (FIELD_GET(HTC_HDR_FLAGS, hdr->htc_info)) &
			  ATH11K_HTC_FLAG_TRAILER_PRESENT;

	if (trailer_present) {
		u8 *trailer;

		trailer_len = FIELD_GET(HTC_HDR_CONTROLBYTES0, hdr->ctrl_info);
		min_len = sizeof(struct ath11k_htc_record_hdr);

		if ((trailer_len < min_len) ||
		    (trailer_len > payload_len)) {
			ath11k_warn(ab, "Invalid trailer length: %d\n",
				    trailer_len);
			goto out;
		}

		trailer = (u8 *)hdr;
		trailer += sizeof(*hdr);
		trailer += payload_len;
		trailer -= trailer_len;
		status = ath11k_htc_process_trailer(htc, trailer,
						    trailer_len, eid);
		if (status)
			goto out;

		skb_trim(skb, skb->len - trailer_len);
	}

	if (trailer_len >= payload_len)
		/* zero length packet with trailer data, just drop these */
		goto out;

	if (eid == ATH11K_HTC_EP_0) {
		struct ath11k_htc_msg *msg = (struct ath11k_htc_msg *)skb->data;

		switch (FIELD_GET(HTC_MSG_MESSAGEID, msg->msg_svc_id)) {
		case ATH11K_HTC_MSG_READY_ID:
		case ATH11K_HTC_MSG_CONNECT_SERVICE_RESP_ID:
			/* handle HTC control message */
			if (completion_done(&htc->ctl_resp)) {
				/* this is a fatal error, target should not be
				 * sending unsolicited messages on the ep 0
				 */
				ath11k_warn(ab, "HTC rx ctrl still processing\n");
				complete(&htc->ctl_resp);
				goto out;
			}

			htc->control_resp_len =
				min_t(int, skb->len,
				      ATH11K_HTC_MAX_CTRL_MSG_LEN);

			memcpy(htc->control_resp_buffer, skb->data,
			       htc->control_resp_len);

			complete(&htc->ctl_resp);
			break;
		case ATH11K_HTC_MSG_SEND_SUSPEND_COMPLETE:
			ath11k_htc_suspend_complete(ab, true);
			break;
		case ATH11K_HTC_MSG_NACK_SUSPEND:
			ath11k_htc_suspend_complete(ab, false);
			break;
		case ATH11K_HTC_MSG_WAKEUP_FROM_SUSPEND_ID:
			ath11k_htc_wakeup_from_suspend(ab);
			break;
		default:
			ath11k_warn(ab, "ignoring unsolicited htc ep0 event %ld\n",
				    FIELD_GET(HTC_MSG_MESSAGEID, msg->msg_svc_id));
			break;
		}
		goto out;
	}

	ath11k_dbg(ab, ATH11K_DBG_HTC, "htc rx completion ep %d skb %pK\n",
		   eid, skb);
	ep->ep_ops.ep_rx_complete(ab, skb);

	/* poll tx completion for interrupt disabled CE's */
	ath11k_ce_poll_send_completed(ab, ep->ul_pipe_id);

	/* skb is now owned by the rx completion handler */
	skb = NULL;
out:
	kfree_skb(skb);
}

static void ath11k_htc_control_rx_complete(struct ath11k_base *ab,
					   struct sk_buff *skb)
{
	/* This is unexpected. FW is not supposed to send regular rx on this
	 * endpoint.
	 */
	ath11k_warn(ab, "unexpected htc rx\n");
	kfree_skb(skb);
}

static const char *htc_service_name(enum ath11k_htc_svc_id id)
{
	switch (id) {
	case ATH11K_HTC_SVC_ID_RESERVED:
		return "Reserved";
	case ATH11K_HTC_SVC_ID_RSVD_CTRL:
		return "Control";
	case ATH11K_HTC_SVC_ID_WMI_CONTROL:
		return "WMI";
	case ATH11K_HTC_SVC_ID_WMI_DATA_BE:
		return "DATA BE";
	case ATH11K_HTC_SVC_ID_WMI_DATA_BK:
		return "DATA BK";
	case ATH11K_HTC_SVC_ID_WMI_DATA_VI:
		return "DATA VI";
	case ATH11K_HTC_SVC_ID_WMI_DATA_VO:
		return "DATA VO";
	case ATH11K_HTC_SVC_ID_WMI_CONTROL_MAC1:
		return "WMI MAC1";
	case ATH11K_HTC_SVC_ID_WMI_CONTROL_MAC2:
		return "WMI MAC2";
	case ATH11K_HTC_SVC_ID_NMI_CONTROL:
		return "NMI Control";
	case ATH11K_HTC_SVC_ID_NMI_DATA:
		return "NMI Data";
	case ATH11K_HTC_SVC_ID_HTT_DATA_MSG:
		return "HTT Data";
	case ATH11K_HTC_SVC_ID_TEST_RAW_STREAMS:
		return "RAW";
	case ATH11K_HTC_SVC_ID_IPA_TX:
		return "IPA TX";
	case ATH11K_HTC_SVC_ID_PKT_LOG:
		return "PKT LOG";
	}

	return "Unknown";
}

static void ath11k_htc_reset_endpoint_states(struct ath11k_htc *htc)
{
	struct ath11k_htc_ep *ep;
	int i;

	for (i = ATH11K_HTC_EP_0; i < ATH11K_HTC_EP_COUNT; i++) {
		ep = &htc->endpoint[i];
		ep->service_id = ATH11K_HTC_SVC_ID_UNUSED;
		ep->max_ep_message_len = 0;
		ep->max_tx_queue_depth = 0;
		ep->eid = i;
		ep->htc = htc;
		ep->tx_credit_flow_enabled = true;
	}
}

static u8 ath11k_htc_get_credit_allocation(struct ath11k_htc *htc,
					   u16 service_id)
{
	u8 i, allocation = 0;

	for (i = 0; i < ATH11K_HTC_MAX_SERVICE_ALLOC_ENTRIES; i++) {
		if (htc->service_alloc_table[i].service_id == service_id) {
			allocation =
				htc->service_alloc_table[i].credit_allocation;
		}
	}

	return allocation;
}

static int ath11k_htc_setup_target_buffer_assignments(struct ath11k_htc *htc)
{
	struct ath11k_htc_svc_tx_credits *serv_entry;
	u32 svc_id[] = {
		ATH11K_HTC_SVC_ID_WMI_CONTROL,
		ATH11K_HTC_SVC_ID_WMI_CONTROL_MAC1,
		ATH11K_HTC_SVC_ID_WMI_CONTROL_MAC2,
	};
	int i, credits;

	credits =  htc->total_transmit_credits;
	serv_entry = htc->service_alloc_table;

	if ((htc->wmi_ep_count == 0) ||
	    (htc->wmi_ep_count > ARRAY_SIZE(svc_id)))
		return -EINVAL;

	/* Divide credits among number of endpoints for WMI */
	credits = credits / htc->wmi_ep_count;
	for (i = 0; i < htc->wmi_ep_count; i++) {
		serv_entry[i].service_id = svc_id[i];
		serv_entry[i].credit_allocation = credits;
	}

	return 0;
}

int ath11k_htc_wait_target(struct ath11k_htc *htc)
{
	int i, status = 0;
	struct ath11k_base *ab = htc->ab;
	unsigned long time_left;
	struct ath11k_htc_ready *ready;
	u16 message_id;
	u16 credit_count;
	u16 credit_size;

	time_left = wait_for_completion_timeout(&htc->ctl_resp,
						ATH11K_HTC_WAIT_TIMEOUT_HZ);
	if (!time_left) {
		ath11k_warn(ab, "failed to receive control response completion, polling..\n");

		for (i = 0; i < ab->hw_params.ce_count; i++)
			ath11k_ce_per_engine_service(htc->ab, i);

		time_left =
			wait_for_completion_timeout(&htc->ctl_resp,
						    ATH11K_HTC_WAIT_TIMEOUT_HZ);

		if (!time_left)
			status = -ETIMEDOUT;
	}

	if (status < 0) {
		ath11k_warn(ab, "ctl_resp never came in (%d)\n", status);
		return status;
	}

	if (htc->control_resp_len < sizeof(*ready)) {
		ath11k_warn(ab, "Invalid HTC ready msg len:%d\n",
			    htc->control_resp_len);
		return -ECOMM;
	}

	ready = (struct ath11k_htc_ready *)htc->control_resp_buffer;
	message_id   = FIELD_GET(HTC_MSG_MESSAGEID, ready->id_credit_count);
	credit_count = FIELD_GET(HTC_READY_MSG_CREDITCOUNT,
				 ready->id_credit_count);
	credit_size  = FIELD_GET(HTC_READY_MSG_CREDITSIZE, ready->size_ep);

	if (message_id != ATH11K_HTC_MSG_READY_ID) {
		ath11k_warn(ab, "Invalid HTC ready msg: 0x%x\n", message_id);
		return -ECOMM;
	}

	htc->total_transmit_credits = credit_count;
	htc->target_credit_size = credit_size;

	ath11k_dbg(ab, ATH11K_DBG_HTC,
		   "Target ready! transmit resources: %d size:%d\n",
		   htc->total_transmit_credits, htc->target_credit_size);

	if ((htc->total_transmit_credits == 0) ||
	    (htc->target_credit_size == 0)) {
		ath11k_warn(ab, "Invalid credit size received\n");
		return -ECOMM;
	}

	/* For QCA6390, wmi endpoint uses 1 credit to avoid
	 * back-to-back write.
	 */
	if (ab->hw_params.supports_shadow_regs)
		htc->total_transmit_credits = 1;

	ath11k_htc_setup_target_buffer_assignments(htc);

	return 0;
}

int ath11k_htc_connect_service(struct ath11k_htc *htc,
			       struct ath11k_htc_svc_conn_req *conn_req,
			       struct ath11k_htc_svc_conn_resp *conn_resp)
{
	struct ath11k_base *ab = htc->ab;
	struct ath11k_htc_conn_svc *req_msg;
	struct ath11k_htc_conn_svc_resp resp_msg_dummy;
	struct ath11k_htc_conn_svc_resp *resp_msg = &resp_msg_dummy;
	enum ath11k_htc_ep_id assigned_eid = ATH11K_HTC_EP_COUNT;
	struct ath11k_htc_ep *ep;
	struct sk_buff *skb;
	unsigned int max_msg_size = 0;
	int length, status;
	unsigned long time_left;
	bool disable_credit_flow_ctrl = false;
	u16 message_id, service_id, flags = 0;
	u8 tx_alloc = 0;

	/* special case for HTC pseudo control service */
	if (conn_req->service_id == ATH11K_HTC_SVC_ID_RSVD_CTRL) {
		disable_credit_flow_ctrl = true;
		assigned_eid = ATH11K_HTC_EP_0;
		max_msg_size = ATH11K_HTC_MAX_CTRL_MSG_LEN;
		memset(&resp_msg_dummy, 0, sizeof(resp_msg_dummy));
		goto setup;
	}

	tx_alloc = ath11k_htc_get_credit_allocation(htc,
						    conn_req->service_id);
	if (!tx_alloc)
		ath11k_dbg(ab, ATH11K_DBG_BOOT,
			   "boot htc service %s does not allocate target credits\n",
			   htc_service_name(conn_req->service_id));

	skb = ath11k_htc_build_tx_ctrl_skb(htc->ab);
	if (!skb) {
		ath11k_warn(ab, "Failed to allocate HTC packet\n");
		return -ENOMEM;
	}

	length = sizeof(*req_msg);
	skb_put(skb, length);
	memset(skb->data, 0, length);

	req_msg = (struct ath11k_htc_conn_svc *)skb->data;
	req_msg->msg_svc_id = FIELD_PREP(HTC_MSG_MESSAGEID,
					 ATH11K_HTC_MSG_CONNECT_SERVICE_ID);

	flags |= FIELD_PREP(ATH11K_HTC_CONN_FLAGS_RECV_ALLOC, tx_alloc);

	/* Only enable credit flow control for WMI ctrl service */
	if (!(conn_req->service_id == ATH11K_HTC_SVC_ID_WMI_CONTROL ||
	      conn_req->service_id == ATH11K_HTC_SVC_ID_WMI_CONTROL_MAC1 ||
	      conn_req->service_id == ATH11K_HTC_SVC_ID_WMI_CONTROL_MAC2)) {
		flags |= ATH11K_HTC_CONN_FLAGS_DISABLE_CREDIT_FLOW_CTRL;
		disable_credit_flow_ctrl = true;
	}

	if (!ab->hw_params.credit_flow) {
		flags |= ATH11K_HTC_CONN_FLAGS_DISABLE_CREDIT_FLOW_CTRL;
		disable_credit_flow_ctrl = true;
	}

	req_msg->flags_len = FIELD_PREP(HTC_SVC_MSG_CONNECTIONFLAGS, flags);
	req_msg->msg_svc_id |= FIELD_PREP(HTC_SVC_MSG_SERVICE_ID,
					  conn_req->service_id);

	reinit_completion(&htc->ctl_resp);

	status = ath11k_htc_send(htc, ATH11K_HTC_EP_0, skb);
	if (status) {
		kfree_skb(skb);
		return status;
	}

	/* wait for response */
	time_left = wait_for_completion_timeout(&htc->ctl_resp,
						ATH11K_HTC_CONN_SVC_TIMEOUT_HZ);
	if (!time_left) {
		ath11k_err(ab, "Service connect timeout\n");
		return -ETIMEDOUT;
	}

	/* we controlled the buffer creation, it's aligned */
	resp_msg = (struct ath11k_htc_conn_svc_resp *)htc->control_resp_buffer;
	message_id = FIELD_GET(HTC_MSG_MESSAGEID, resp_msg->msg_svc_id);
	service_id = FIELD_GET(HTC_SVC_RESP_MSG_SERVICEID,
			       resp_msg->msg_svc_id);

	if ((message_id != ATH11K_HTC_MSG_CONNECT_SERVICE_RESP_ID) ||
	    (htc->control_resp_len < sizeof(*resp_msg))) {
		ath11k_err(ab, "Invalid resp message ID 0x%x", message_id);
		return -EPROTO;
	}

	ath11k_dbg(ab, ATH11K_DBG_HTC,
		   "HTC Service %s connect response: status: 0x%lx, assigned ep: 0x%lx\n",
		   htc_service_name(service_id),
		   FIELD_GET(HTC_SVC_RESP_MSG_STATUS, resp_msg->flags_len),
		   FIELD_GET(HTC_SVC_RESP_MSG_ENDPOINTID, resp_msg->flags_len));

	conn_resp->connect_resp_code = FIELD_GET(HTC_SVC_RESP_MSG_STATUS,
						 resp_msg->flags_len);

	/* check response status */
	if (conn_resp->connect_resp_code != ATH11K_HTC_CONN_SVC_STATUS_SUCCESS) {
		ath11k_err(ab, "HTC Service %s connect request failed: 0x%x)\n",
			   htc_service_name(service_id),
		       conn_resp->connect_resp_code);
		return -EPROTO;
	}

	assigned_eid = (enum ath11k_htc_ep_id)FIELD_GET(
						HTC_SVC_RESP_MSG_ENDPOINTID,
						resp_msg->flags_len);

	max_msg_size = FIELD_GET(HTC_SVC_RESP_MSG_MAXMSGSIZE,
				 resp_msg->flags_len);

setup:

	if (assigned_eid >= ATH11K_HTC_EP_COUNT)
		return -EPROTO;

	if (max_msg_size == 0)
		return -EPROTO;

	ep = &htc->endpoint[assigned_eid];
	ep->eid = assigned_eid;

	if (ep->service_id != ATH11K_HTC_SVC_ID_UNUSED)
		return -EPROTO;

	/* return assigned endpoint to caller */
	conn_resp->eid = assigned_eid;
	conn_resp->max_msg_len = FIELD_GET(HTC_SVC_RESP_MSG_MAXMSGSIZE,
					   resp_msg->flags_len);

	/* setup the endpoint */
	ep->service_id = conn_req->service_id;
	ep->max_tx_queue_depth = conn_req->max_send_queue_depth;
	ep->max_ep_message_len = FIELD_GET(HTC_SVC_RESP_MSG_MAXMSGSIZE,
					   resp_msg->flags_len);
	ep->tx_credits = tx_alloc;

	/* copy all the callbacks */
	ep->ep_ops = conn_req->ep_ops;

	status = ath11k_hif_map_service_to_pipe(htc->ab,
						ep->service_id,
						&ep->ul_pipe_id,
						&ep->dl_pipe_id);
	if (status)
		return status;

	ath11k_dbg(ab, ATH11K_DBG_BOOT,
		   "boot htc service '%s' ul pipe %d dl pipe %d eid %d ready\n",
		   htc_service_name(ep->service_id), ep->ul_pipe_id,
		   ep->dl_pipe_id, ep->eid);

	if (disable_credit_flow_ctrl && ep->tx_credit_flow_enabled) {
		ep->tx_credit_flow_enabled = false;
		ath11k_dbg(ab, ATH11K_DBG_BOOT,
			   "boot htc service '%s' eid %d TX flow control disabled\n",
			   htc_service_name(ep->service_id), assigned_eid);
	}

	return status;
}

int ath11k_htc_start(struct ath11k_htc *htc)
{
	struct sk_buff *skb;
	int status = 0;
	struct ath11k_base *ab = htc->ab;
	struct ath11k_htc_setup_complete_extended *msg;

	skb = ath11k_htc_build_tx_ctrl_skb(htc->ab);
	if (!skb)
		return -ENOMEM;

	skb_put(skb, sizeof(*msg));
	memset(skb->data, 0, skb->len);

	msg = (struct ath11k_htc_setup_complete_extended *)skb->data;
	msg->msg_id = FIELD_PREP(HTC_MSG_MESSAGEID,
				 ATH11K_HTC_MSG_SETUP_COMPLETE_EX_ID);

	if (ab->hw_params.credit_flow)
		ath11k_dbg(ab, ATH11K_DBG_HTC, "HTC is using TX credit flow control\n");
	else
		msg->flags |= ATH11K_GLOBAL_DISABLE_CREDIT_FLOW;

	status = ath11k_htc_send(htc, ATH11K_HTC_EP_0, skb);
	if (status) {
		kfree_skb(skb);
		return status;
	}

	return 0;
}

int ath11k_htc_init(struct ath11k_base *ab)
{
	struct ath11k_htc *htc = &ab->htc;
	struct ath11k_htc_svc_conn_req conn_req;
	struct ath11k_htc_svc_conn_resp conn_resp;
	int ret;

	spin_lock_init(&htc->tx_lock);

	ath11k_htc_reset_endpoint_states(htc);

	htc->ab = ab;

	switch (ab->wmi_ab.preferred_hw_mode) {
	case WMI_HOST_HW_MODE_SINGLE:
		htc->wmi_ep_count = 1;
		break;
	case WMI_HOST_HW_MODE_DBS:
	case WMI_HOST_HW_MODE_DBS_OR_SBS:
		htc->wmi_ep_count = 2;
		break;
	case WMI_HOST_HW_MODE_DBS_SBS:
		htc->wmi_ep_count = 3;
		break;
	default:
		htc->wmi_ep_count = ab->hw_params.max_radios;
		break;
	}

	/* setup our pseudo HTC control endpoint connection */
	memset(&conn_req, 0, sizeof(conn_req));
	memset(&conn_resp, 0, sizeof(conn_resp));
	conn_req.ep_ops.ep_tx_complete = ath11k_htc_control_tx_complete;
	conn_req.ep_ops.ep_rx_complete = ath11k_htc_control_rx_complete;
	conn_req.max_send_queue_depth = ATH11K_NUM_CONTROL_TX_BUFFERS;
	conn_req.service_id = ATH11K_HTC_SVC_ID_RSVD_CTRL;

	/* connect fake service */
	ret = ath11k_htc_connect_service(htc, &conn_req, &conn_resp);
	if (ret) {
		ath11k_err(ab, "could not connect to htc service (%d)\n", ret);
		return ret;
	}

	init_completion(&htc->ctl_resp);

	return 0;
}
