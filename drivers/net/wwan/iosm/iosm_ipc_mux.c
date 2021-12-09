// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-21 Intel Corporation.
 */

#include "iosm_ipc_mux_codec.h"

/* At the begin of the runtime phase the IP MUX channel shall created. */
static int ipc_mux_channel_create(struct iosm_mux *ipc_mux)
{
	int channel_id;

	channel_id = ipc_imem_channel_alloc(ipc_mux->imem, ipc_mux->instance_id,
					    IPC_CTYPE_WWAN);

	if (channel_id < 0) {
		dev_err(ipc_mux->dev,
			"allocation of the MUX channel id failed");
		ipc_mux->state = MUX_S_ERROR;
		ipc_mux->event = MUX_E_NOT_APPLICABLE;
		goto no_channel;
	}

	/* Establish the MUX channel in blocking mode. */
	ipc_mux->channel = ipc_imem_channel_open(ipc_mux->imem, channel_id,
						 IPC_HP_NET_CHANNEL_INIT);

	if (!ipc_mux->channel) {
		dev_err(ipc_mux->dev, "ipc_imem_channel_open failed");
		ipc_mux->state = MUX_S_ERROR;
		ipc_mux->event = MUX_E_NOT_APPLICABLE;
		return -ENODEV; /* MUX channel is not available. */
	}

	/* Define the MUX active state properties. */
	ipc_mux->state = MUX_S_ACTIVE;
	ipc_mux->event = MUX_E_NO_ORDERS;

no_channel:
	return channel_id;
}

/* Reset the session/if id state. */
static void ipc_mux_session_free(struct iosm_mux *ipc_mux, int if_id)
{
	struct mux_session *if_entry;

	if_entry = &ipc_mux->session[if_id];
	/* Reset the session state. */
	if_entry->wwan = NULL;
}

/* Create and send the session open command. */
static struct mux_cmd_open_session_resp *
ipc_mux_session_open_send(struct iosm_mux *ipc_mux, int if_id)
{
	struct mux_cmd_open_session_resp *open_session_resp;
	struct mux_acb *acb = &ipc_mux->acb;
	union mux_cmd_param param;

	/* open_session commands to one ACB and start transmission. */
	param.open_session.flow_ctrl = 0;
	param.open_session.ipv4v6_hints = 0;
	param.open_session.reserved2 = 0;
	param.open_session.dl_head_pad_len = cpu_to_le32(IPC_MEM_DL_ETH_OFFSET);

	/* Finish and transfer ACB. The user thread is suspended.
	 * It is a blocking function call, until CP responds or timeout.
	 */
	acb->wanted_response = MUX_CMD_OPEN_SESSION_RESP;
	if (ipc_mux_dl_acb_send_cmds(ipc_mux, MUX_CMD_OPEN_SESSION, if_id, 0,
				     &param, sizeof(param.open_session), true,
				 false) ||
	    acb->got_response != MUX_CMD_OPEN_SESSION_RESP) {
		dev_err(ipc_mux->dev, "if_id %d: OPEN_SESSION send failed",
			if_id);
		return NULL;
	}

	open_session_resp = &ipc_mux->acb.got_param.open_session_resp;
	if (open_session_resp->response != cpu_to_le32(MUX_CMD_RESP_SUCCESS)) {
		dev_err(ipc_mux->dev,
			"if_id %d,session open failed,response=%d", if_id,
			open_session_resp->response);
		return NULL;
	}

	return open_session_resp;
}

/* Open the first IP session. */
static bool ipc_mux_session_open(struct iosm_mux *ipc_mux,
				 struct mux_session_open *session_open)
{
	struct mux_cmd_open_session_resp *open_session_resp;
	int if_id;

	/* Search for a free session interface id. */
	if_id = le32_to_cpu(session_open->if_id);
	if (if_id < 0 || if_id >= IPC_MEM_MUX_IP_SESSION_ENTRIES) {
		dev_err(ipc_mux->dev, "invalid interface id=%d", if_id);
		return false;
	}

	/* Create and send the session open command.
	 * It is a blocking function call, until CP responds or timeout.
	 */
	open_session_resp = ipc_mux_session_open_send(ipc_mux, if_id);
	if (!open_session_resp) {
		ipc_mux_session_free(ipc_mux, if_id);
		session_open->if_id = cpu_to_le32(-1);
		return false;
	}

	/* Initialize the uplink skb accumulator. */
	skb_queue_head_init(&ipc_mux->session[if_id].ul_list);

	ipc_mux->session[if_id].dl_head_pad_len = IPC_MEM_DL_ETH_OFFSET;
	ipc_mux->session[if_id].ul_head_pad_len =
		le32_to_cpu(open_session_resp->ul_head_pad_len);
	ipc_mux->session[if_id].wwan = ipc_mux->wwan;

	/* Reset the flow ctrl stats of the session */
	ipc_mux->session[if_id].flow_ctl_en_cnt = 0;
	ipc_mux->session[if_id].flow_ctl_dis_cnt = 0;
	ipc_mux->session[if_id].ul_flow_credits = 0;
	ipc_mux->session[if_id].net_tx_stop = false;
	ipc_mux->session[if_id].flow_ctl_mask = 0;

	/* Save and return the assigned if id. */
	session_open->if_id = cpu_to_le32(if_id);
	ipc_mux->nr_sessions++;

	return true;
}

/* Free pending session UL packet. */
static void ipc_mux_session_reset(struct iosm_mux *ipc_mux, int if_id)
{
	/* Reset the session/if id state. */
	ipc_mux_session_free(ipc_mux, if_id);

	/* Empty the uplink skb accumulator. */
	skb_queue_purge(&ipc_mux->session[if_id].ul_list);
}

static void ipc_mux_session_close(struct iosm_mux *ipc_mux,
				  struct mux_session_close *msg)
{
	int if_id;

	/* Copy the session interface id. */
	if_id = le32_to_cpu(msg->if_id);

	if (if_id < 0 || if_id >= IPC_MEM_MUX_IP_SESSION_ENTRIES) {
		dev_err(ipc_mux->dev, "invalid session id %d", if_id);
		return;
	}

	/* Create and send the session close command.
	 * It is a blocking function call, until CP responds or timeout.
	 */
	if (ipc_mux_dl_acb_send_cmds(ipc_mux, MUX_CMD_CLOSE_SESSION, if_id, 0,
				     NULL, 0, true, false))
		dev_err(ipc_mux->dev, "if_id %d: CLOSE_SESSION send failed",
			if_id);

	/* Reset the flow ctrl stats of the session */
	ipc_mux->session[if_id].flow_ctl_en_cnt = 0;
	ipc_mux->session[if_id].flow_ctl_dis_cnt = 0;
	ipc_mux->session[if_id].flow_ctl_mask = 0;

	ipc_mux_session_reset(ipc_mux, if_id);
	ipc_mux->nr_sessions--;
}

static void ipc_mux_channel_close(struct iosm_mux *ipc_mux,
				  struct mux_channel_close *channel_close_p)
{
	int i;

	/* Free pending session UL packet. */
	for (i = 0; i < IPC_MEM_MUX_IP_SESSION_ENTRIES; i++)
		if (ipc_mux->session[i].wwan)
			ipc_mux_session_reset(ipc_mux, i);

	ipc_imem_channel_close(ipc_mux->imem, ipc_mux->channel_id);

	/* Reset the MUX object. */
	ipc_mux->state = MUX_S_INACTIVE;
	ipc_mux->event = MUX_E_INACTIVE;
}

/* CP has interrupted AP. If AP is in IP MUX mode, execute the pending ops. */
static int ipc_mux_schedule(struct iosm_mux *ipc_mux, union mux_msg *msg)
{
	enum mux_event order;
	bool success;
	int ret = -EIO;

	if (!ipc_mux->initialized) {
		ret = -EAGAIN;
		goto out;
	}

	order = msg->common.event;

	switch (ipc_mux->state) {
	case MUX_S_INACTIVE:
		if (order != MUX_E_MUX_SESSION_OPEN)
			goto out; /* Wait for the request to open a session */

		if (ipc_mux->event == MUX_E_INACTIVE)
			/* Establish the MUX channel and the new state. */
			ipc_mux->channel_id = ipc_mux_channel_create(ipc_mux);

		if (ipc_mux->state != MUX_S_ACTIVE) {
			ret = ipc_mux->channel_id; /* Missing the MUX channel */
			goto out;
		}

		/* Disable the TD update timer and open the first IP session. */
		ipc_imem_td_update_timer_suspend(ipc_mux->imem, true);
		ipc_mux->event = MUX_E_MUX_SESSION_OPEN;
		success = ipc_mux_session_open(ipc_mux, &msg->session_open);

		ipc_imem_td_update_timer_suspend(ipc_mux->imem, false);
		if (success)
			ret = ipc_mux->channel_id;
		goto out;

	case MUX_S_ACTIVE:
		switch (order) {
		case MUX_E_MUX_SESSION_OPEN:
			/* Disable the TD update timer and open a session */
			ipc_imem_td_update_timer_suspend(ipc_mux->imem, true);
			ipc_mux->event = MUX_E_MUX_SESSION_OPEN;
			success = ipc_mux_session_open(ipc_mux,
						       &msg->session_open);
			ipc_imem_td_update_timer_suspend(ipc_mux->imem, false);
			if (success)
				ret = ipc_mux->channel_id;
			goto out;

		case MUX_E_MUX_SESSION_CLOSE:
			/* Release an IP session. */
			ipc_mux->event = MUX_E_MUX_SESSION_CLOSE;
			ipc_mux_session_close(ipc_mux, &msg->session_close);
			if (!ipc_mux->nr_sessions) {
				ipc_mux->event = MUX_E_MUX_CHANNEL_CLOSE;
				ipc_mux_channel_close(ipc_mux,
						      &msg->channel_close);
			}
			ret = ipc_mux->channel_id;
			goto out;

		case MUX_E_MUX_CHANNEL_CLOSE:
			/* Close the MUX channel pipes. */
			ipc_mux->event = MUX_E_MUX_CHANNEL_CLOSE;
			ipc_mux_channel_close(ipc_mux, &msg->channel_close);
			ret = ipc_mux->channel_id;
			goto out;

		default:
			/* Invalid order. */
			goto out;
		}

	default:
		dev_err(ipc_mux->dev,
			"unexpected MUX transition: state=%d, event=%d",
			ipc_mux->state, ipc_mux->event);
	}
out:
	return ret;
}

struct iosm_mux *ipc_mux_init(struct ipc_mux_config *mux_cfg,
			      struct iosm_imem *imem)
{
	struct iosm_mux *ipc_mux = kzalloc(sizeof(*ipc_mux), GFP_KERNEL);
	int i, ul_tds, ul_td_size;
	struct sk_buff_head *free_list;
	struct sk_buff *skb;

	if (!ipc_mux)
		return NULL;

	ipc_mux->protocol = mux_cfg->protocol;
	ipc_mux->ul_flow = mux_cfg->ul_flow;
	ipc_mux->instance_id = mux_cfg->instance_id;
	ipc_mux->wwan_q_offset = 0;

	ipc_mux->pcie = imem->pcie;
	ipc_mux->imem = imem;
	ipc_mux->ipc_protocol = imem->ipc_protocol;
	ipc_mux->dev = imem->dev;
	ipc_mux->wwan = imem->wwan;

	/* Get the reference to the UL ADB list. */
	free_list = &ipc_mux->ul_adb.free_list;

	/* Initialize the list with free ADB. */
	skb_queue_head_init(free_list);

	ul_td_size = IPC_MEM_MAX_DL_MUX_LITE_BUF_SIZE;

	ul_tds = IPC_MEM_MAX_TDS_MUX_LITE_UL;

	ipc_mux->ul_adb.dest_skb = NULL;

	ipc_mux->initialized = true;
	ipc_mux->adb_prep_ongoing = false;
	ipc_mux->size_needed = 0;
	ipc_mux->ul_data_pend_bytes = 0;
	ipc_mux->state = MUX_S_INACTIVE;
	ipc_mux->ev_mux_net_transmit_pending = false;
	ipc_mux->tx_transaction_id = 0;
	ipc_mux->rr_next_session = 0;
	ipc_mux->event = MUX_E_INACTIVE;
	ipc_mux->channel_id = -1;
	ipc_mux->channel = NULL;

	/* Allocate the list of UL ADB. */
	for (i = 0; i < ul_tds; i++) {
		dma_addr_t mapping;

		skb = ipc_pcie_alloc_skb(ipc_mux->pcie, ul_td_size, GFP_ATOMIC,
					 &mapping, DMA_TO_DEVICE, 0);
		if (!skb) {
			ipc_mux_deinit(ipc_mux);
			return NULL;
		}
		/* Extend the UL ADB list. */
		skb_queue_tail(free_list, skb);
	}

	return ipc_mux;
}

/* Informs the network stack to restart transmission for all opened session if
 * Flow Control is not ON for that session.
 */
static void ipc_mux_restart_tx_for_all_sessions(struct iosm_mux *ipc_mux)
{
	struct mux_session *session;
	int idx;

	for (idx = 0; idx < IPC_MEM_MUX_IP_SESSION_ENTRIES; idx++) {
		session = &ipc_mux->session[idx];

		if (!session->wwan)
			continue;

		/* If flow control of the session is OFF and if there was tx
		 * stop then restart. Inform the network interface to restart
		 * sending data.
		 */
		if (session->flow_ctl_mask == 0) {
			session->net_tx_stop = false;
			ipc_mux_netif_tx_flowctrl(session, idx, false);
		}
	}
}

/* Informs the network stack to stop sending further pkt for all opened
 * sessions
 */
static void ipc_mux_stop_netif_for_all_sessions(struct iosm_mux *ipc_mux)
{
	struct mux_session *session;
	int idx;

	for (idx = 0; idx < IPC_MEM_MUX_IP_SESSION_ENTRIES; idx++) {
		session = &ipc_mux->session[idx];

		if (!session->wwan)
			continue;

		ipc_mux_netif_tx_flowctrl(session, session->if_id, true);
	}
}

void ipc_mux_check_n_restart_tx(struct iosm_mux *ipc_mux)
{
	if (ipc_mux->ul_flow == MUX_UL) {
		int low_thresh = IPC_MEM_MUX_UL_FLOWCTRL_LOW_B;

		if (ipc_mux->ul_data_pend_bytes < low_thresh)
			ipc_mux_restart_tx_for_all_sessions(ipc_mux);
	}
}

int ipc_mux_get_max_sessions(struct iosm_mux *ipc_mux)
{
	return ipc_mux ? IPC_MEM_MUX_IP_SESSION_ENTRIES : -EFAULT;
}

enum ipc_mux_protocol ipc_mux_get_active_protocol(struct iosm_mux *ipc_mux)
{
	return ipc_mux ? ipc_mux->protocol : MUX_UNKNOWN;
}

int ipc_mux_open_session(struct iosm_mux *ipc_mux, int session_nr)
{
	struct mux_session_open *session_open;
	union mux_msg mux_msg;

	session_open = &mux_msg.session_open;
	session_open->event = MUX_E_MUX_SESSION_OPEN;

	session_open->if_id = cpu_to_le32(session_nr);
	ipc_mux->session[session_nr].flags |= IPC_MEM_WWAN_MUX;
	return ipc_mux_schedule(ipc_mux, &mux_msg);
}

int ipc_mux_close_session(struct iosm_mux *ipc_mux, int session_nr)
{
	struct mux_session_close *session_close;
	union mux_msg mux_msg;
	int ret_val;

	session_close = &mux_msg.session_close;
	session_close->event = MUX_E_MUX_SESSION_CLOSE;

	session_close->if_id = cpu_to_le32(session_nr);
	ret_val = ipc_mux_schedule(ipc_mux, &mux_msg);
	ipc_mux->session[session_nr].flags &= ~IPC_MEM_WWAN_MUX;

	return ret_val;
}

void ipc_mux_deinit(struct iosm_mux *ipc_mux)
{
	struct mux_channel_close *channel_close;
	struct sk_buff_head *free_list;
	union mux_msg mux_msg;
	struct sk_buff *skb;

	if (!ipc_mux->initialized)
		return;
	ipc_mux_stop_netif_for_all_sessions(ipc_mux);

	if (ipc_mux->state == MUX_S_ACTIVE) {
		channel_close = &mux_msg.channel_close;
		channel_close->event = MUX_E_MUX_CHANNEL_CLOSE;
		ipc_mux_schedule(ipc_mux, &mux_msg);
	}

	/* Empty the ADB free list. */
	free_list = &ipc_mux->ul_adb.free_list;

	/* Remove from the head of the downlink queue. */
	while ((skb = skb_dequeue(free_list)))
		ipc_pcie_kfree_skb(ipc_mux->pcie, skb);

	if (ipc_mux->channel) {
		ipc_mux->channel->ul_pipe.is_open = false;
		ipc_mux->channel->dl_pipe.is_open = false;
	}

	kfree(ipc_mux);
}
