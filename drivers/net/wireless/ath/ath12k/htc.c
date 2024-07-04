// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/skbuff.h>
#include <linux/ctype.h>

#include "debug.h"
#include "hif.h"

struct sk_buff *ath12k_htc_alloc_skb(struct ath12k_base *ab, int size)
{
	struct sk_buff *skb;

	skb = dev_alloc_skb(size + sizeof(struct ath12k_htc_hdr));
	if (!skb)
		return NULL;

	skb_reserve(skb, sizeof(struct ath12k_htc_hdr));

	/* FW/HTC requires 4-byte aligned streams */
	if (!IS_ALIGNED((unsigned long)skb->data, 4))
		ath12k_warn(ab, "Unaligned HTC tx skb\n");

	return skb;
}

static void ath12k_htc_control_tx_complete(struct ath12k_base *ab,
					   struct sk_buff *skb)
{
	kfree_skb(skb);
}

static struct sk_buff *ath12k_htc_build_tx_ctrl_skb(void)
{
	struct sk_buff *skb;
	struct ath12k_skb_cb *skb_cb;

	skb = dev_alloc_skb(ATH12K_HTC_CONTROL_BUFFER_SIZE);
	if (!skb)
		return NULL;

	skb_reserve(skb, sizeof(struct ath12k_htc_hdr));
	WARN_ON_ONCE(!IS_ALIGNED((unsigned long)skb->data, 4));

	skb_cb = ATH12K_SKB_CB(skb);
	memset(skb_cb, 0, sizeof(*skb_cb));

	return skb;
}

static void ath12k_htc_prepare_tx_skb(struct ath12k_htc_ep *ep,
				      struct sk_buff *skb)
{
	struct ath12k_htc_hdr *hdr;

	hdr = (struct ath12k_htc_hdr *)skb->data;

	memset(hdr, 0, sizeof(*hdr));
	hdr->htc_info = le32_encode_bits(ep->eid, HTC_HDR_ENDPOINTID) |
			le32_encode_bits((skb->len - sizeof(*hdr)),
					 HTC_HDR_PAYLOADLEN);

	if (ep->tx_credit_flow_enabled)
		hdr->htc_info |= le32_encode_bits(ATH12K_HTC_FLAG_NEED_CREDIT_UPDATE,
						  HTC_HDR_FLAGS);

	spin_lock_bh(&ep->htc->tx_lock);
	hdr->ctrl_info = le32_encode_bits(ep->seq_no++, HTC_HDR_CONTROLBYTES1);
	spin_unlock_bh(&ep->htc->tx_lock);
}

int ath12k_htc_send(struct ath12k_htc *htc,
		    enum ath12k_htc_ep_id eid,
		    struct sk_buff *skb)
{
	struct ath12k_htc_ep *ep = &htc->endpoint[eid];
	struct ath12k_skb_cb *skb_cb = ATH12K_SKB_CB(skb);
	struct device *dev = htc->ab->dev;
	struct ath12k_base *ab = htc->ab;
	int credits = 0;
	int ret;

	if (eid >= ATH12K_HTC_EP_COUNT) {
		ath12k_warn(ab, "Invalid endpoint id: %d\n", eid);
		return -ENOENT;
	}

	skb_push(skb, sizeof(struct ath12k_htc_hdr));

	if (ep->tx_credit_flow_enabled) {
		credits = DIV_ROUND_UP(skb->len, htc->target_credit_size);
		spin_lock_bh(&htc->tx_lock);
		if (ep->tx_credits < credits) {
			ath12k_dbg(ab, ATH12K_DBG_HTC,
				   "htc insufficient credits ep %d required %d available %d\n",
				   eid, credits, ep->tx_credits);
			spin_unlock_bh(&htc->tx_lock);
			ret = -EAGAIN;
			goto err_pull;
		}
		ep->tx_credits -= credits;
		ath12k_dbg(ab, ATH12K_DBG_HTC,
			   "htc ep %d consumed %d credits (total %d)\n",
			   eid, credits, ep->tx_credits);
		spin_unlock_bh(&htc->tx_lock);
	}

	ath12k_htc_prepare_tx_skb(ep, skb);

	skb_cb->paddr = dma_map_single(dev, skb->data, skb->len, DMA_TO_DEVICE);
	ret = dma_mapping_error(dev, skb_cb->paddr);
	if (ret) {
		ret = -EIO;
		goto err_credits;
	}

	ret = ath12k_ce_send(htc->ab, skb, ep->ul_pipe_id, ep->eid);
	if (ret)
		goto err_unmap;

	return 0;

err_unmap:
	dma_unmap_single(dev, skb_cb->paddr, skb->len, DMA_TO_DEVICE);
err_credits:
	if (ep->tx_credit_flow_enabled) {
		spin_lock_bh(&htc->tx_lock);
		ep->tx_credits += credits;
		ath12k_dbg(ab, ATH12K_DBG_HTC,
			   "htc ep %d reverted %d credits back (total %d)\n",
			   eid, credits, ep->tx_credits);
		spin_unlock_bh(&htc->tx_lock);

		if (ep->ep_ops.ep_tx_credits)
			ep->ep_ops.ep_tx_credits(htc->ab);
	}
err_pull:
	skb_pull(skb, sizeof(struct ath12k_htc_hdr));
	return ret;
}

static void
ath12k_htc_process_credit_report(struct ath12k_htc *htc,
				 const struct ath12k_htc_credit_report *report,
				 int len,
				 enum ath12k_htc_ep_id eid)
{
	struct ath12k_base *ab = htc->ab;
	struct ath12k_htc_ep *ep;
	int i, n_reports;

	if (len % sizeof(*report))
		ath12k_warn(ab, "Uneven credit report len %d", len);

	n_reports = len / sizeof(*report);

	spin_lock_bh(&htc->tx_lock);
	for (i = 0; i < n_reports; i++, report++) {
		if (report->eid >= ATH12K_HTC_EP_COUNT)
			break;

		ep = &htc->endpoint[report->eid];
		ep->tx_credits += report->credits;

		ath12k_dbg(ab, ATH12K_DBG_HTC, "htc ep %d got %d credits (total %d)\n",
			   report->eid, report->credits, ep->tx_credits);

		if (ep->ep_ops.ep_tx_credits) {
			spin_unlock_bh(&htc->tx_lock);
			ep->ep_ops.ep_tx_credits(htc->ab);
			spin_lock_bh(&htc->tx_lock);
		}
	}
	spin_unlock_bh(&htc->tx_lock);
}

static int ath12k_htc_process_trailer(struct ath12k_htc *htc,
				      u8 *buffer,
				      int length,
				      enum ath12k_htc_ep_id src_eid)
{
	struct ath12k_base *ab = htc->ab;
	int status = 0;
	struct ath12k_htc_record *record;
	size_t len;

	while (length > 0) {
		record = (struct ath12k_htc_record *)buffer;

		if (length < sizeof(record->hdr)) {
			status = -EINVAL;
			break;
		}

		if (record->hdr.len > length) {
			/* no room left in buffer for record */
			ath12k_warn(ab, "Invalid record length: %d\n",
				    record->hdr.len);
			status = -EINVAL;
			break;
		}

		switch (record->hdr.id) {
		case ATH12K_HTC_RECORD_CREDITS:
			len = sizeof(struct ath12k_htc_credit_report);
			if (record->hdr.len < len) {
				ath12k_warn(ab, "Credit report too long\n");
				status = -EINVAL;
				break;
			}
			ath12k_htc_process_credit_report(htc,
							 record->credit_report,
							 record->hdr.len,
							 src_eid);
			break;
		default:
			ath12k_warn(ab, "Unhandled record: id:%d length:%d\n",
				    record->hdr.id, record->hdr.len);
			break;
		}

		if (status)
			break;

		/* multiple records may be present in a trailer */
		buffer += sizeof(record->hdr) + record->hdr.len;
		length -= sizeof(record->hdr) + record->hdr.len;
	}

	return status;
}

static void ath12k_htc_suspend_complete(struct ath12k_base *ab, bool ack)
{
	ath12k_dbg(ab, ATH12K_DBG_BOOT, "boot suspend complete %d\n", ack);

	if (ack)
		set_bit(ATH12K_FLAG_HTC_SUSPEND_COMPLETE, &ab->dev_flags);
	else
		clear_bit(ATH12K_FLAG_HTC_SUSPEND_COMPLETE, &ab->dev_flags);

	complete(&ab->htc_suspend);
}

void ath12k_htc_rx_completion_handler(struct ath12k_base *ab,
				      struct sk_buff *skb)
{
	int status = 0;
	struct ath12k_htc *htc = &ab->htc;
	struct ath12k_htc_hdr *hdr;
	struct ath12k_htc_ep *ep;
	u16 payload_len;
	u32 trailer_len = 0;
	size_t min_len;
	u8 eid;
	bool trailer_present;

	hdr = (struct ath12k_htc_hdr *)skb->data;
	skb_pull(skb, sizeof(*hdr));

	eid = le32_get_bits(hdr->htc_info, HTC_HDR_ENDPOINTID);

	if (eid >= ATH12K_HTC_EP_COUNT) {
		ath12k_warn(ab, "HTC Rx: invalid eid %d\n", eid);
		goto out;
	}

	ep = &htc->endpoint[eid];

	payload_len = le32_get_bits(hdr->htc_info, HTC_HDR_PAYLOADLEN);

	if (payload_len + sizeof(*hdr) > ATH12K_HTC_MAX_LEN) {
		ath12k_warn(ab, "HTC rx frame too long, len: %zu\n",
			    payload_len + sizeof(*hdr));
		goto out;
	}

	if (skb->len < payload_len) {
		ath12k_warn(ab, "HTC Rx: insufficient length, got %d, expected %d\n",
			    skb->len, payload_len);
		goto out;
	}

	/* get flags to check for trailer */
	trailer_present = le32_get_bits(hdr->htc_info, HTC_HDR_FLAGS) &
			  ATH12K_HTC_FLAG_TRAILER_PRESENT;

	if (trailer_present) {
		u8 *trailer;

		trailer_len = le32_get_bits(hdr->ctrl_info,
					    HTC_HDR_CONTROLBYTES0);
		min_len = sizeof(struct ath12k_htc_record_hdr);

		if ((trailer_len < min_len) ||
		    (trailer_len > payload_len)) {
			ath12k_warn(ab, "Invalid trailer length: %d\n",
				    trailer_len);
			goto out;
		}

		trailer = (u8 *)hdr;
		trailer += sizeof(*hdr);
		trailer += payload_len;
		trailer -= trailer_len;
		status = ath12k_htc_process_trailer(htc, trailer,
						    trailer_len, eid);
		if (status)
			goto out;

		skb_trim(skb, skb->len - trailer_len);
	}

	if (trailer_len >= payload_len)
		/* zero length packet with trailer data, just drop these */
		goto out;

	if (eid == ATH12K_HTC_EP_0) {
		struct ath12k_htc_msg *msg = (struct ath12k_htc_msg *)skb->data;

		switch (le32_get_bits(msg->msg_svc_id, HTC_MSG_MESSAGEID)) {
		case ATH12K_HTC_MSG_READY_ID:
		case ATH12K_HTC_MSG_CONNECT_SERVICE_RESP_ID:
			/* handle HTC control message */
			if (completion_done(&htc->ctl_resp)) {
				/* this is a fatal error, target should not be
				 * sending unsolicited messages on the ep 0
				 */
				ath12k_warn(ab, "HTC rx ctrl still processing\n");
				complete(&htc->ctl_resp);
				goto out;
			}

			htc->control_resp_len =
				min_t(int, skb->len,
				      ATH12K_HTC_MAX_CTRL_MSG_LEN);

			memcpy(htc->control_resp_buffer, skb->data,
			       htc->control_resp_len);

			complete(&htc->ctl_resp);
			break;
		case ATH12K_HTC_MSG_SEND_SUSPEND_COMPLETE:
			ath12k_htc_suspend_complete(ab, true);
			break;
		case ATH12K_HTC_MSG_NACK_SUSPEND:
			ath12k_htc_suspend_complete(ab, false);
			break;
		case ATH12K_HTC_MSG_WAKEUP_FROM_SUSPEND_ID:
			break;
		default:
			ath12k_warn(ab, "ignoring unsolicited htc ep0 event %u\n",
				    le32_get_bits(msg->msg_svc_id, HTC_MSG_MESSAGEID));
			break;
		}
		goto out;
	}

	ath12k_dbg(ab, ATH12K_DBG_HTC, "htc rx completion ep %d skb %p\n",
		   eid, skb);
	ep->ep_ops.ep_rx_complete(ab, skb);

	/* poll tx completion for interrupt disabled CE's */
	ath12k_ce_poll_send_completed(ab, ep->ul_pipe_id);

	/* skb is now owned by the rx completion handler */
	skb = NULL;
out:
	kfree_skb(skb);
}

static void ath12k_htc_control_rx_complete(struct ath12k_base *ab,
					   struct sk_buff *skb)
{
	/* This is unexpected. FW is not supposed to send regular rx on this
	 * endpoint.
	 */
	ath12k_warn(ab, "unexpected htc rx\n");
	kfree_skb(skb);
}

static const char *htc_service_name(enum ath12k_htc_svc_id id)
{
	switch (id) {
	case ATH12K_HTC_SVC_ID_RESERVED:
		return "Reserved";
	case ATH12K_HTC_SVC_ID_RSVD_CTRL:
		return "Control";
	case ATH12K_HTC_SVC_ID_WMI_CONTROL:
		return "WMI";
	case ATH12K_HTC_SVC_ID_WMI_DATA_BE:
		return "DATA BE";
	case ATH12K_HTC_SVC_ID_WMI_DATA_BK:
		return "DATA BK";
	case ATH12K_HTC_SVC_ID_WMI_DATA_VI:
		return "DATA VI";
	case ATH12K_HTC_SVC_ID_WMI_DATA_VO:
		return "DATA VO";
	case ATH12K_HTC_SVC_ID_WMI_CONTROL_MAC1:
		return "WMI MAC1";
	case ATH12K_HTC_SVC_ID_WMI_CONTROL_MAC2:
		return "WMI MAC2";
	case ATH12K_HTC_SVC_ID_NMI_CONTROL:
		return "NMI Control";
	case ATH12K_HTC_SVC_ID_NMI_DATA:
		return "NMI Data";
	case ATH12K_HTC_SVC_ID_HTT_DATA_MSG:
		return "HTT Data";
	case ATH12K_HTC_SVC_ID_TEST_RAW_STREAMS:
		return "RAW";
	case ATH12K_HTC_SVC_ID_IPA_TX:
		return "IPA TX";
	case ATH12K_HTC_SVC_ID_PKT_LOG:
		return "PKT LOG";
	case ATH12K_HTC_SVC_ID_WMI_CONTROL_DIAG:
		return "WMI DIAG";
	}

	return "Unknown";
}

static void ath12k_htc_reset_endpoint_states(struct ath12k_htc *htc)
{
	struct ath12k_htc_ep *ep;
	int i;

	for (i = ATH12K_HTC_EP_0; i < ATH12K_HTC_EP_COUNT; i++) {
		ep = &htc->endpoint[i];
		ep->service_id = ATH12K_HTC_SVC_ID_UNUSED;
		ep->max_ep_message_len = 0;
		ep->max_tx_queue_depth = 0;
		ep->eid = i;
		ep->htc = htc;
		ep->tx_credit_flow_enabled = true;
	}
}

static u8 ath12k_htc_get_credit_allocation(struct ath12k_htc *htc,
					   u16 service_id)
{
	struct ath12k_htc_svc_tx_credits *serv_entry;
	u8 i, allocation = 0;

	serv_entry = htc->service_alloc_table;

	for (i = 0; i < ATH12K_HTC_MAX_SERVICE_ALLOC_ENTRIES; i++) {
		if (serv_entry[i].service_id == service_id) {
			allocation = serv_entry[i].credit_allocation;
			break;
		}
	}

	return allocation;
}

static int ath12k_htc_setup_target_buffer_assignments(struct ath12k_htc *htc)
{
	struct ath12k_htc_svc_tx_credits *serv_entry;
	static const u32 svc_id[] = {
					ATH12K_HTC_SVC_ID_WMI_CONTROL,
					ATH12K_HTC_SVC_ID_WMI_CONTROL_MAC1,
					ATH12K_HTC_SVC_ID_WMI_CONTROL_MAC2,
				};
	int i, credits;

	credits = htc->total_transmit_credits;
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

int ath12k_htc_wait_target(struct ath12k_htc *htc)
{
	int i, status = 0;
	struct ath12k_base *ab = htc->ab;
	unsigned long time_left;
	struct ath12k_htc_ready *ready;
	u16 message_id;
	u16 credit_count;
	u16 credit_size;

	time_left = wait_for_completion_timeout(&htc->ctl_resp,
						ATH12K_HTC_WAIT_TIMEOUT_HZ);
	if (!time_left) {
		ath12k_warn(ab, "failed to receive control response completion, polling..\n");

		for (i = 0; i < ab->hw_params->ce_count; i++)
			ath12k_ce_per_engine_service(htc->ab, i);

		time_left =
			wait_for_completion_timeout(&htc->ctl_resp,
						    ATH12K_HTC_WAIT_TIMEOUT_HZ);

		if (!time_left)
			status = -ETIMEDOUT;
	}

	if (status < 0) {
		ath12k_warn(ab, "ctl_resp never came in (%d)\n", status);
		return status;
	}

	if (htc->control_resp_len < sizeof(*ready)) {
		ath12k_warn(ab, "Invalid HTC ready msg len:%d\n",
			    htc->control_resp_len);
		return -ECOMM;
	}

	ready = (struct ath12k_htc_ready *)htc->control_resp_buffer;
	message_id = le32_get_bits(ready->id_credit_count, HTC_MSG_MESSAGEID);
	credit_count = le32_get_bits(ready->id_credit_count,
				     HTC_READY_MSG_CREDITCOUNT);
	credit_size = le32_get_bits(ready->size_ep, HTC_READY_MSG_CREDITSIZE);

	if (message_id != ATH12K_HTC_MSG_READY_ID) {
		ath12k_warn(ab, "Invalid HTC ready msg: 0x%x\n", message_id);
		return -ECOMM;
	}

	htc->total_transmit_credits = credit_count;
	htc->target_credit_size = credit_size;

	ath12k_dbg(ab, ATH12K_DBG_HTC,
		   "Target ready! transmit resources: %d size:%d\n",
		   htc->total_transmit_credits, htc->target_credit_size);

	if ((htc->total_transmit_credits == 0) ||
	    (htc->target_credit_size == 0)) {
		ath12k_warn(ab, "Invalid credit size received\n");
		return -ECOMM;
	}

	ath12k_htc_setup_target_buffer_assignments(htc);

	return 0;
}

int ath12k_htc_connect_service(struct ath12k_htc *htc,
			       struct ath12k_htc_svc_conn_req *conn_req,
			       struct ath12k_htc_svc_conn_resp *conn_resp)
{
	struct ath12k_base *ab = htc->ab;
	struct ath12k_htc_conn_svc *req_msg;
	struct ath12k_htc_conn_svc_resp resp_msg_dummy;
	struct ath12k_htc_conn_svc_resp *resp_msg = &resp_msg_dummy;
	enum ath12k_htc_ep_id assigned_eid = ATH12K_HTC_EP_COUNT;
	struct ath12k_htc_ep *ep;
	struct sk_buff *skb;
	unsigned int max_msg_size = 0;
	int length, status;
	unsigned long time_left;
	bool disable_credit_flow_ctrl = false;
	u16 message_id, service_id, flags = 0;
	u8 tx_alloc = 0;

	/* special case for HTC pseudo control service */
	if (conn_req->service_id == ATH12K_HTC_SVC_ID_RSVD_CTRL) {
		disable_credit_flow_ctrl = true;
		assigned_eid = ATH12K_HTC_EP_0;
		max_msg_size = ATH12K_HTC_MAX_CTRL_MSG_LEN;
		memset(&resp_msg_dummy, 0, sizeof(resp_msg_dummy));
		goto setup;
	}

	tx_alloc = ath12k_htc_get_credit_allocation(htc,
						    conn_req->service_id);
	if (!tx_alloc)
		ath12k_dbg(ab, ATH12K_DBG_BOOT,
			   "boot htc service %s does not allocate target credits\n",
			   htc_service_name(conn_req->service_id));

	skb = ath12k_htc_build_tx_ctrl_skb();
	if (!skb) {
		ath12k_warn(ab, "Failed to allocate HTC packet\n");
		return -ENOMEM;
	}

	length = sizeof(*req_msg);
	skb_put(skb, length);
	memset(skb->data, 0, length);

	req_msg = (struct ath12k_htc_conn_svc *)skb->data;
	req_msg->msg_svc_id = le32_encode_bits(ATH12K_HTC_MSG_CONNECT_SERVICE_ID,
					       HTC_MSG_MESSAGEID);

	flags |= u32_encode_bits(tx_alloc, ATH12K_HTC_CONN_FLAGS_RECV_ALLOC);

	/* Only enable credit flow control for WMI ctrl service */
	if (!(conn_req->service_id == ATH12K_HTC_SVC_ID_WMI_CONTROL ||
	      conn_req->service_id == ATH12K_HTC_SVC_ID_WMI_CONTROL_MAC1 ||
	      conn_req->service_id == ATH12K_HTC_SVC_ID_WMI_CONTROL_MAC2)) {
		flags |= ATH12K_HTC_CONN_FLAGS_DISABLE_CREDIT_FLOW_CTRL;
		disable_credit_flow_ctrl = true;
	}

	req_msg->flags_len = le32_encode_bits(flags, HTC_SVC_MSG_CONNECTIONFLAGS);
	req_msg->msg_svc_id |= le32_encode_bits(conn_req->service_id,
						HTC_SVC_MSG_SERVICE_ID);

	reinit_completion(&htc->ctl_resp);

	status = ath12k_htc_send(htc, ATH12K_HTC_EP_0, skb);
	if (status) {
		kfree_skb(skb);
		return status;
	}

	/* wait for response */
	time_left = wait_for_completion_timeout(&htc->ctl_resp,
						ATH12K_HTC_CONN_SVC_TIMEOUT_HZ);
	if (!time_left) {
		ath12k_err(ab, "Service connect timeout\n");
		return -ETIMEDOUT;
	}

	/* we controlled the buffer creation, it's aligned */
	resp_msg = (struct ath12k_htc_conn_svc_resp *)htc->control_resp_buffer;
	message_id = le32_get_bits(resp_msg->msg_svc_id, HTC_MSG_MESSAGEID);
	service_id = le32_get_bits(resp_msg->msg_svc_id,
				   HTC_SVC_RESP_MSG_SERVICEID);

	if ((message_id != ATH12K_HTC_MSG_CONNECT_SERVICE_RESP_ID) ||
	    (htc->control_resp_len < sizeof(*resp_msg))) {
		ath12k_err(ab, "Invalid resp message ID 0x%x", message_id);
		return -EPROTO;
	}

	ath12k_dbg(ab, ATH12K_DBG_HTC,
		   "HTC Service %s connect response: status: %u, assigned ep: %u\n",
		   htc_service_name(service_id),
		   le32_get_bits(resp_msg->flags_len, HTC_SVC_RESP_MSG_STATUS),
		   le32_get_bits(resp_msg->flags_len, HTC_SVC_RESP_MSG_ENDPOINTID));

	conn_resp->connect_resp_code = le32_get_bits(resp_msg->flags_len,
						     HTC_SVC_RESP_MSG_STATUS);

	/* check response status */
	if (conn_resp->connect_resp_code != ATH12K_HTC_CONN_SVC_STATUS_SUCCESS) {
		ath12k_err(ab, "HTC Service %s connect request failed: 0x%x)\n",
			   htc_service_name(service_id),
		       conn_resp->connect_resp_code);
		return -EPROTO;
	}

	assigned_eid = le32_get_bits(resp_msg->flags_len,
				     HTC_SVC_RESP_MSG_ENDPOINTID);

	max_msg_size = le32_get_bits(resp_msg->flags_len,
				     HTC_SVC_RESP_MSG_MAXMSGSIZE);

setup:

	if (assigned_eid >= ATH12K_HTC_EP_COUNT)
		return -EPROTO;

	if (max_msg_size == 0)
		return -EPROTO;

	ep = &htc->endpoint[assigned_eid];
	ep->eid = assigned_eid;

	if (ep->service_id != ATH12K_HTC_SVC_ID_UNUSED)
		return -EPROTO;

	/* return assigned endpoint to caller */
	conn_resp->eid = assigned_eid;
	conn_resp->max_msg_len = le32_get_bits(resp_msg->flags_len,
					       HTC_SVC_RESP_MSG_MAXMSGSIZE);

	/* setup the endpoint */
	ep->service_id = conn_req->service_id;
	ep->max_tx_queue_depth = conn_req->max_send_queue_depth;
	ep->max_ep_message_len = le32_get_bits(resp_msg->flags_len,
					       HTC_SVC_RESP_MSG_MAXMSGSIZE);
	ep->tx_credits = tx_alloc;

	/* copy all the callbacks */
	ep->ep_ops = conn_req->ep_ops;

	status = ath12k_hif_map_service_to_pipe(htc->ab,
						ep->service_id,
						&ep->ul_pipe_id,
						&ep->dl_pipe_id);
	if (status)
		return status;

	ath12k_dbg(ab, ATH12K_DBG_BOOT,
		   "boot htc service '%s' ul pipe %d dl pipe %d eid %d ready\n",
		   htc_service_name(ep->service_id), ep->ul_pipe_id,
		   ep->dl_pipe_id, ep->eid);

	if (disable_credit_flow_ctrl && ep->tx_credit_flow_enabled) {
		ep->tx_credit_flow_enabled = false;
		ath12k_dbg(ab, ATH12K_DBG_BOOT,
			   "boot htc service '%s' eid %d TX flow control disabled\n",
			   htc_service_name(ep->service_id), assigned_eid);
	}

	return status;
}

int ath12k_htc_start(struct ath12k_htc *htc)
{
	struct sk_buff *skb;
	int status;
	struct ath12k_base *ab = htc->ab;
	struct ath12k_htc_setup_complete_extended *msg;

	skb = ath12k_htc_build_tx_ctrl_skb();
	if (!skb)
		return -ENOMEM;

	skb_put(skb, sizeof(*msg));
	memset(skb->data, 0, skb->len);

	msg = (struct ath12k_htc_setup_complete_extended *)skb->data;
	msg->msg_id = le32_encode_bits(ATH12K_HTC_MSG_SETUP_COMPLETE_EX_ID,
				       HTC_MSG_MESSAGEID);

	ath12k_dbg(ab, ATH12K_DBG_HTC, "HTC is using TX credit flow control\n");

	status = ath12k_htc_send(htc, ATH12K_HTC_EP_0, skb);
	if (status) {
		kfree_skb(skb);
		return status;
	}

	return 0;
}

int ath12k_htc_init(struct ath12k_base *ab)
{
	struct ath12k_htc *htc = &ab->htc;
	struct ath12k_htc_svc_conn_req conn_req = { };
	struct ath12k_htc_svc_conn_resp conn_resp = { };
	int ret;

	spin_lock_init(&htc->tx_lock);

	ath12k_htc_reset_endpoint_states(htc);

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
		htc->wmi_ep_count = ab->hw_params->max_radios;
		break;
	}

	/* setup our pseudo HTC control endpoint connection */
	conn_req.ep_ops.ep_tx_complete = ath12k_htc_control_tx_complete;
	conn_req.ep_ops.ep_rx_complete = ath12k_htc_control_rx_complete;
	conn_req.max_send_queue_depth = ATH12K_NUM_CONTROL_TX_BUFFERS;
	conn_req.service_id = ATH12K_HTC_SVC_ID_RSVD_CTRL;

	/* connect fake service */
	ret = ath12k_htc_connect_service(htc, &conn_req, &conn_resp);
	if (ret) {
		ath12k_err(ab, "could not connect to htc service (%d)\n", ret);
		return ret;
	}

	init_completion(&htc->ctl_resp);

	return 0;
}
