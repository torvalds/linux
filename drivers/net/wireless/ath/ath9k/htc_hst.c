/*
 * Copyright (c) 2010-2011 Atheros Communications Inc.
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "htc.h"

static int htc_issue_send(struct htc_target *target, struct sk_buff* skb,
			  u16 len, u8 flags, u8 epid)

{
	struct htc_frame_hdr *hdr;
	struct htc_endpoint *endpoint = &target->endpoint[epid];
	int status;

	hdr = skb_push(skb, sizeof(struct htc_frame_hdr));
	hdr->endpoint_id = epid;
	hdr->flags = flags;
	hdr->payload_len = cpu_to_be16(len);

	status = target->hif->send(target->hif_dev, endpoint->ul_pipeid, skb);

	return status;
}

static struct htc_endpoint *get_next_avail_ep(struct htc_endpoint *endpoint)
{
	enum htc_endpoint_id avail_epid;

	for (avail_epid = (ENDPOINT_MAX - 1); avail_epid > ENDPOINT0; avail_epid--)
		if (endpoint[avail_epid].service_id == 0)
			return &endpoint[avail_epid];
	return NULL;
}

static u8 service_to_ulpipe(u16 service_id)
{
	switch (service_id) {
	case WMI_CONTROL_SVC:
		return 4;
	case WMI_BEACON_SVC:
	case WMI_CAB_SVC:
	case WMI_UAPSD_SVC:
	case WMI_MGMT_SVC:
	case WMI_DATA_VO_SVC:
	case WMI_DATA_VI_SVC:
	case WMI_DATA_BE_SVC:
	case WMI_DATA_BK_SVC:
		return 1;
	default:
		return 0;
	}
}

static u8 service_to_dlpipe(u16 service_id)
{
	switch (service_id) {
	case WMI_CONTROL_SVC:
		return 3;
	case WMI_BEACON_SVC:
	case WMI_CAB_SVC:
	case WMI_UAPSD_SVC:
	case WMI_MGMT_SVC:
	case WMI_DATA_VO_SVC:
	case WMI_DATA_VI_SVC:
	case WMI_DATA_BE_SVC:
	case WMI_DATA_BK_SVC:
		return 2;
	default:
		return 0;
	}
}

static void htc_process_target_rdy(struct htc_target *target,
				   void *buf)
{
	struct htc_endpoint *endpoint;
	struct htc_ready_msg *htc_ready_msg = (struct htc_ready_msg *) buf;

	target->credit_size = be16_to_cpu(htc_ready_msg->credit_size);

	endpoint = &target->endpoint[ENDPOINT0];
	endpoint->service_id = HTC_CTRL_RSVD_SVC;
	endpoint->max_msglen = HTC_MAX_CONTROL_MESSAGE_LENGTH;
	atomic_inc(&target->tgt_ready);
	complete(&target->target_wait);
}

static void htc_process_conn_rsp(struct htc_target *target,
				 struct htc_frame_hdr *htc_hdr)
{
	struct htc_conn_svc_rspmsg *svc_rspmsg;
	struct htc_endpoint *endpoint, *tmp_endpoint = NULL;
	u16 service_id;
	u16 max_msglen;
	enum htc_endpoint_id epid, tepid;

	svc_rspmsg = (struct htc_conn_svc_rspmsg *)
		((void *) htc_hdr + sizeof(struct htc_frame_hdr));

	if (svc_rspmsg->status == HTC_SERVICE_SUCCESS) {
		epid = svc_rspmsg->endpoint_id;
		if (epid < 0 || epid >= ENDPOINT_MAX)
			return;

		service_id = be16_to_cpu(svc_rspmsg->service_id);
		max_msglen = be16_to_cpu(svc_rspmsg->max_msg_len);
		endpoint = &target->endpoint[epid];

		for (tepid = (ENDPOINT_MAX - 1); tepid > ENDPOINT0; tepid--) {
			tmp_endpoint = &target->endpoint[tepid];
			if (tmp_endpoint->service_id == service_id) {
				tmp_endpoint->service_id = 0;
				break;
			}
		}

		if (tepid == ENDPOINT0)
			return;

		endpoint->service_id = service_id;
		endpoint->max_txqdepth = tmp_endpoint->max_txqdepth;
		endpoint->ep_callbacks = tmp_endpoint->ep_callbacks;
		endpoint->ul_pipeid = tmp_endpoint->ul_pipeid;
		endpoint->dl_pipeid = tmp_endpoint->dl_pipeid;
		endpoint->max_msglen = max_msglen;
		target->conn_rsp_epid = epid;
		complete(&target->cmd_wait);
	} else {
		target->conn_rsp_epid = ENDPOINT_UNUSED;
	}
}

static int htc_config_pipe_credits(struct htc_target *target)
{
	struct sk_buff *skb;
	struct htc_config_pipe_msg *cp_msg;
	int ret;
	unsigned long time_left;

	skb = alloc_skb(50 + sizeof(struct htc_frame_hdr), GFP_ATOMIC);
	if (!skb) {
		dev_err(target->dev, "failed to allocate send buffer\n");
		return -ENOMEM;
	}
	skb_reserve(skb, sizeof(struct htc_frame_hdr));

	cp_msg = skb_put(skb, sizeof(struct htc_config_pipe_msg));

	cp_msg->message_id = cpu_to_be16(HTC_MSG_CONFIG_PIPE_ID);
	cp_msg->pipe_id = USB_WLAN_TX_PIPE;
	cp_msg->credits = target->credits;

	target->htc_flags |= HTC_OP_CONFIG_PIPE_CREDITS;

	ret = htc_issue_send(target, skb, skb->len, 0, ENDPOINT0);
	if (ret)
		goto err;

	time_left = wait_for_completion_timeout(&target->cmd_wait, HZ);
	if (!time_left) {
		dev_err(target->dev, "HTC credit config timeout\n");
		kfree_skb(skb);
		return -ETIMEDOUT;
	}

	return 0;
err:
	kfree_skb(skb);
	return -EINVAL;
}

static int htc_setup_complete(struct htc_target *target)
{
	struct sk_buff *skb;
	struct htc_comp_msg *comp_msg;
	int ret = 0;
	unsigned long time_left;

	skb = alloc_skb(50 + sizeof(struct htc_frame_hdr), GFP_ATOMIC);
	if (!skb) {
		dev_err(target->dev, "failed to allocate send buffer\n");
		return -ENOMEM;
	}
	skb_reserve(skb, sizeof(struct htc_frame_hdr));

	comp_msg = skb_put(skb, sizeof(struct htc_comp_msg));
	comp_msg->msg_id = cpu_to_be16(HTC_MSG_SETUP_COMPLETE_ID);

	target->htc_flags |= HTC_OP_START_WAIT;

	ret = htc_issue_send(target, skb, skb->len, 0, ENDPOINT0);
	if (ret)
		goto err;

	time_left = wait_for_completion_timeout(&target->cmd_wait, HZ);
	if (!time_left) {
		dev_err(target->dev, "HTC start timeout\n");
		kfree_skb(skb);
		return -ETIMEDOUT;
	}

	return 0;

err:
	kfree_skb(skb);
	return -EINVAL;
}

/* HTC APIs */

int htc_init(struct htc_target *target)
{
	int ret;

	ret = htc_config_pipe_credits(target);
	if (ret)
		return ret;

	return htc_setup_complete(target);
}

int htc_connect_service(struct htc_target *target,
		     struct htc_service_connreq *service_connreq,
		     enum htc_endpoint_id *conn_rsp_epid)
{
	struct sk_buff *skb;
	struct htc_endpoint *endpoint;
	struct htc_conn_svc_msg *conn_msg;
	int ret;
	unsigned long time_left;

	/* Find an available endpoint */
	endpoint = get_next_avail_ep(target->endpoint);
	if (!endpoint) {
		dev_err(target->dev, "Endpoint is not available for service %d\n",
			service_connreq->service_id);
		return -EINVAL;
	}

	endpoint->service_id = service_connreq->service_id;
	endpoint->max_txqdepth = service_connreq->max_send_qdepth;
	endpoint->ul_pipeid = service_to_ulpipe(service_connreq->service_id);
	endpoint->dl_pipeid = service_to_dlpipe(service_connreq->service_id);
	endpoint->ep_callbacks = service_connreq->ep_callbacks;

	skb = alloc_skb(sizeof(struct htc_conn_svc_msg) +
			    sizeof(struct htc_frame_hdr), GFP_ATOMIC);
	if (!skb) {
		dev_err(target->dev, "Failed to allocate buf to send"
			"service connect req\n");
		return -ENOMEM;
	}

	skb_reserve(skb, sizeof(struct htc_frame_hdr));

	conn_msg = skb_put(skb, sizeof(struct htc_conn_svc_msg));
	conn_msg->service_id = cpu_to_be16(service_connreq->service_id);
	conn_msg->msg_id = cpu_to_be16(HTC_MSG_CONNECT_SERVICE_ID);
	conn_msg->con_flags = cpu_to_be16(service_connreq->con_flags);
	conn_msg->dl_pipeid = endpoint->dl_pipeid;
	conn_msg->ul_pipeid = endpoint->ul_pipeid;

	ret = htc_issue_send(target, skb, skb->len, 0, ENDPOINT0);
	if (ret)
		goto err;

	time_left = wait_for_completion_timeout(&target->cmd_wait, HZ);
	if (!time_left) {
		dev_err(target->dev, "Service connection timeout for: %d\n",
			service_connreq->service_id);
		kfree_skb(skb);
		return -ETIMEDOUT;
	}

	*conn_rsp_epid = target->conn_rsp_epid;
	return 0;
err:
	kfree_skb(skb);
	return ret;
}

int htc_send(struct htc_target *target, struct sk_buff *skb)
{
	struct ath9k_htc_tx_ctl *tx_ctl;

	tx_ctl = HTC_SKB_CB(skb);
	return htc_issue_send(target, skb, skb->len, 0, tx_ctl->epid);
}

int htc_send_epid(struct htc_target *target, struct sk_buff *skb,
		  enum htc_endpoint_id epid)
{
	return htc_issue_send(target, skb, skb->len, 0, epid);
}

void htc_stop(struct htc_target *target)
{
	target->hif->stop(target->hif_dev);
}

void htc_start(struct htc_target *target)
{
	target->hif->start(target->hif_dev);
}

void htc_sta_drain(struct htc_target *target, u8 idx)
{
	target->hif->sta_drain(target->hif_dev, idx);
}

void ath9k_htc_txcompletion_cb(struct htc_target *htc_handle,
			       struct sk_buff *skb, bool txok)
{
	struct htc_endpoint *endpoint;
	struct htc_frame_hdr *htc_hdr = NULL;

	if (htc_handle->htc_flags & HTC_OP_CONFIG_PIPE_CREDITS) {
		complete(&htc_handle->cmd_wait);
		htc_handle->htc_flags &= ~HTC_OP_CONFIG_PIPE_CREDITS;
		goto ret;
	}

	if (htc_handle->htc_flags & HTC_OP_START_WAIT) {
		complete(&htc_handle->cmd_wait);
		htc_handle->htc_flags &= ~HTC_OP_START_WAIT;
		goto ret;
	}

	if (skb) {
		htc_hdr = (struct htc_frame_hdr *) skb->data;
		endpoint = &htc_handle->endpoint[htc_hdr->endpoint_id];
		skb_pull(skb, sizeof(struct htc_frame_hdr));

		if (endpoint->ep_callbacks.tx) {
			endpoint->ep_callbacks.tx(endpoint->ep_callbacks.priv,
						  skb, htc_hdr->endpoint_id,
						  txok);
		} else {
			kfree_skb(skb);
		}
	}

	return;
ret:
	kfree_skb(skb);
}

static void ath9k_htc_fw_panic_report(struct htc_target *htc_handle,
				      struct sk_buff *skb)
{
	uint32_t *pattern = (uint32_t *)skb->data;

	switch (*pattern) {
	case 0x33221199:
		{
		struct htc_panic_bad_vaddr *htc_panic;
		htc_panic = (struct htc_panic_bad_vaddr *) skb->data;
		dev_err(htc_handle->dev, "ath: firmware panic! "
			"exccause: 0x%08x; pc: 0x%08x; badvaddr: 0x%08x.\n",
			htc_panic->exccause, htc_panic->pc,
			htc_panic->badvaddr);
		break;
		}
	case 0x33221299:
		{
		struct htc_panic_bad_epid *htc_panic;
		htc_panic = (struct htc_panic_bad_epid *) skb->data;
		dev_err(htc_handle->dev, "ath: firmware panic! "
			"bad epid: 0x%08x\n", htc_panic->epid);
		break;
		}
	default:
		dev_err(htc_handle->dev, "ath: unknown panic pattern!\n");
		break;
	}
}

/*
 * HTC Messages are handled directly here and the obtained SKB
 * is freed.
 *
 * Service messages (Data, WMI) passed to the corresponding
 * endpoint RX handlers, which have to free the SKB.
 */
void ath9k_htc_rx_msg(struct htc_target *htc_handle,
		      struct sk_buff *skb, u32 len, u8 pipe_id)
{
	struct htc_frame_hdr *htc_hdr;
	enum htc_endpoint_id epid;
	struct htc_endpoint *endpoint;
	__be16 *msg_id;

	if (!htc_handle || !skb)
		return;

	htc_hdr = (struct htc_frame_hdr *) skb->data;
	epid = htc_hdr->endpoint_id;

	if (epid == 0x99) {
		ath9k_htc_fw_panic_report(htc_handle, skb);
		kfree_skb(skb);
		return;
	}

	if (epid < 0 || epid >= ENDPOINT_MAX) {
		if (pipe_id != USB_REG_IN_PIPE)
			dev_kfree_skb_any(skb);
		else
			kfree_skb(skb);
		return;
	}

	if (epid == ENDPOINT0) {

		/* Handle trailer */
		if (htc_hdr->flags & HTC_FLAGS_RECV_TRAILER) {
			if (be32_to_cpu(*(__be32 *) skb->data) == 0x00C60000)
				/* Move past the Watchdog pattern */
				htc_hdr = (struct htc_frame_hdr *)(skb->data + 4);
		}

		/* Get the message ID */
		msg_id = (__be16 *) ((void *) htc_hdr +
				     sizeof(struct htc_frame_hdr));

		/* Now process HTC messages */
		switch (be16_to_cpu(*msg_id)) {
		case HTC_MSG_READY_ID:
			htc_process_target_rdy(htc_handle, htc_hdr);
			break;
		case HTC_MSG_CONNECT_SERVICE_RESPONSE_ID:
			htc_process_conn_rsp(htc_handle, htc_hdr);
			break;
		default:
			break;
		}

		kfree_skb(skb);

	} else {
		if (htc_hdr->flags & HTC_FLAGS_RECV_TRAILER)
			skb_trim(skb, len - htc_hdr->control[0]);

		skb_pull(skb, sizeof(struct htc_frame_hdr));

		endpoint = &htc_handle->endpoint[epid];
		if (endpoint->ep_callbacks.rx)
			endpoint->ep_callbacks.rx(endpoint->ep_callbacks.priv,
						  skb, epid);
	}
}

struct htc_target *ath9k_htc_hw_alloc(void *hif_handle,
				      struct ath9k_htc_hif *hif,
				      struct device *dev)
{
	struct htc_endpoint *endpoint;
	struct htc_target *target;

	target = kzalloc(sizeof(struct htc_target), GFP_KERNEL);
	if (!target)
		return NULL;

	init_completion(&target->target_wait);
	init_completion(&target->cmd_wait);

	target->hif = hif;
	target->hif_dev = hif_handle;
	target->dev = dev;

	/* Assign control endpoint pipe IDs */
	endpoint = &target->endpoint[ENDPOINT0];
	endpoint->ul_pipeid = hif->control_ul_pipe;
	endpoint->dl_pipeid = hif->control_dl_pipe;

	atomic_set(&target->tgt_ready, 0);

	return target;
}

void ath9k_htc_hw_free(struct htc_target *htc)
{
	kfree(htc);
}

int ath9k_htc_hw_init(struct htc_target *target,
		      struct device *dev, u16 devid,
		      char *product, u32 drv_info)
{
	if (ath9k_htc_probe_device(target, dev, devid, product, drv_info)) {
		pr_err("Failed to initialize the device\n");
		return -ENODEV;
	}

	return 0;
}

void ath9k_htc_hw_deinit(struct htc_target *target, bool hot_unplug)
{
	if (target)
		ath9k_htc_disconnect_device(target, hot_unplug);
}
