// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-21 Intel Corporation.
 */

#include <linux/nospec.h>

#include "iosm_ipc_imem_ops.h"
#include "iosm_ipc_mux_codec.h"
#include "iosm_ipc_task_queue.h"

/* Test the link power state and send a MUX command in blocking mode. */
static int ipc_mux_tq_cmd_send(struct iosm_imem *ipc_imem, int arg, void *msg,
			       size_t size)
{
	struct iosm_mux *ipc_mux = ipc_imem->mux;
	const struct mux_acb *acb = msg;

	skb_queue_tail(&ipc_mux->channel->ul_list, acb->skb);
	ipc_imem_ul_send(ipc_mux->imem);

	return 0;
}

static int ipc_mux_acb_send(struct iosm_mux *ipc_mux, bool blocking)
{
	struct completion *completion = &ipc_mux->channel->ul_sem;
	int ret = ipc_task_queue_send_task(ipc_mux->imem, ipc_mux_tq_cmd_send,
					   0, &ipc_mux->acb,
					   sizeof(ipc_mux->acb), false);
	if (ret) {
		dev_err(ipc_mux->dev, "unable to send mux command");
		return ret;
	}

	/* if blocking, suspend the app and wait for irq in the flash or
	 * crash phase. return false on timeout to indicate failure.
	 */
	if (blocking) {
		u32 wait_time_milliseconds = IPC_MUX_CMD_RUN_DEFAULT_TIMEOUT;

		reinit_completion(completion);

		if (wait_for_completion_interruptible_timeout
		   (completion, msecs_to_jiffies(wait_time_milliseconds)) ==
		   0) {
			dev_err(ipc_mux->dev, "ch[%d] timeout",
				ipc_mux->channel_id);
			ipc_uevent_send(ipc_mux->imem->dev, UEVENT_MDM_TIMEOUT);
			return -ETIMEDOUT;
		}
	}

	return 0;
}

/* Prepare mux Command */
static struct mux_lite_cmdh *ipc_mux_lite_add_cmd(struct iosm_mux *ipc_mux,
						  u32 cmd, struct mux_acb *acb,
						  void *param, u32 param_size)
{
	struct mux_lite_cmdh *cmdh = (struct mux_lite_cmdh *)acb->skb->data;

	cmdh->signature = cpu_to_le32(MUX_SIG_CMDH);
	cmdh->command_type = cpu_to_le32(cmd);
	cmdh->if_id = acb->if_id;

	acb->cmd = cmd;

	cmdh->cmd_len = cpu_to_le16(offsetof(struct mux_lite_cmdh, param) +
				    param_size);
	cmdh->transaction_id = cpu_to_le32(ipc_mux->tx_transaction_id++);

	if (param)
		memcpy(&cmdh->param, param, param_size);

	skb_put(acb->skb, le16_to_cpu(cmdh->cmd_len));

	return cmdh;
}

static int ipc_mux_acb_alloc(struct iosm_mux *ipc_mux)
{
	struct mux_acb *acb = &ipc_mux->acb;
	struct sk_buff *skb;
	dma_addr_t mapping;

	/* Allocate skb memory for the uplink buffer. */
	skb = ipc_pcie_alloc_skb(ipc_mux->pcie, MUX_MAX_UL_ACB_BUF_SIZE,
				 GFP_ATOMIC, &mapping, DMA_TO_DEVICE, 0);
	if (!skb)
		return -ENOMEM;

	/* Save the skb address. */
	acb->skb = skb;

	memset(skb->data, 0, MUX_MAX_UL_ACB_BUF_SIZE);

	return 0;
}

int ipc_mux_dl_acb_send_cmds(struct iosm_mux *ipc_mux, u32 cmd_type, u8 if_id,
			     u32 transaction_id, union mux_cmd_param *param,
			     size_t res_size, bool blocking, bool respond)
{
	struct mux_acb *acb = &ipc_mux->acb;
	struct mux_lite_cmdh *ack_lite;
	int ret = 0;

	acb->if_id = if_id;
	ret = ipc_mux_acb_alloc(ipc_mux);
	if (ret)
		return ret;

	ack_lite = ipc_mux_lite_add_cmd(ipc_mux, cmd_type, acb, param,
					res_size);
	if (respond)
		ack_lite->transaction_id = cpu_to_le32(transaction_id);

	ret = ipc_mux_acb_send(ipc_mux, blocking);

	return ret;
}

void ipc_mux_netif_tx_flowctrl(struct mux_session *session, int idx, bool on)
{
	/* Inform the network interface to start/stop flow ctrl */
	ipc_wwan_tx_flowctrl(session->wwan, idx, on);
}

static int ipc_mux_dl_cmdresps_decode_process(struct iosm_mux *ipc_mux,
					      struct mux_lite_cmdh *cmdh)
{
	struct mux_acb *acb = &ipc_mux->acb;

	switch (le32_to_cpu(cmdh->command_type)) {
	case MUX_CMD_OPEN_SESSION_RESP:
	case MUX_CMD_CLOSE_SESSION_RESP:
		/* Resume the control application. */
		acb->got_param = cmdh->param;
		break;

	case MUX_LITE_CMD_FLOW_CTL_ACK:
		/* This command type is not expected as response for
		 * Aggregation version of the protocol. So return non-zero.
		 */
		if (ipc_mux->protocol != MUX_LITE)
			return -EINVAL;

		dev_dbg(ipc_mux->dev, "if %u FLOW_CTL_ACK %u received",
			cmdh->if_id, le32_to_cpu(cmdh->transaction_id));
		break;

	default:
		return -EINVAL;
	}

	acb->wanted_response = MUX_CMD_INVALID;
	acb->got_response = le32_to_cpu(cmdh->command_type);
	complete(&ipc_mux->channel->ul_sem);

	return 0;
}

static int ipc_mux_dl_dlcmds_decode_process(struct iosm_mux *ipc_mux,
					    struct mux_lite_cmdh *cmdh)
{
	union mux_cmd_param *param = &cmdh->param;
	struct mux_session *session;
	int new_size;

	dev_dbg(ipc_mux->dev, "if_id[%d]: dlcmds decode process %d",
		cmdh->if_id, le32_to_cpu(cmdh->command_type));

	switch (le32_to_cpu(cmdh->command_type)) {
	case MUX_LITE_CMD_FLOW_CTL:

		if (cmdh->if_id >= ipc_mux->nr_sessions) {
			dev_err(ipc_mux->dev, "if_id [%d] not valid",
				cmdh->if_id);
			return -EINVAL; /* No session interface id. */
		}

		session = &ipc_mux->session[cmdh->if_id];

		new_size = offsetof(struct mux_lite_cmdh, param) +
			   sizeof(param->flow_ctl);
		if (param->flow_ctl.mask == cpu_to_le32(0xFFFFFFFF)) {
			/* Backward Compatibility */
			if (cmdh->cmd_len == cpu_to_le16(new_size))
				session->flow_ctl_mask =
					le32_to_cpu(param->flow_ctl.mask);
			else
				session->flow_ctl_mask = ~0;
			/* if CP asks for FLOW CTRL Enable
			 * then set our internal flow control Tx flag
			 * to limit uplink session queueing
			 */
			session->net_tx_stop = true;
			/* Update the stats */
			session->flow_ctl_en_cnt++;
		} else if (param->flow_ctl.mask == 0) {
			/* Just reset the Flow control mask and let
			 * mux_flow_ctrl_low_thre_b take control on
			 * our internal Tx flag and enabling kernel
			 * flow control
			 */
			/* Backward Compatibility */
			if (cmdh->cmd_len == cpu_to_le16(new_size))
				session->flow_ctl_mask =
					le32_to_cpu(param->flow_ctl.mask);
			else
				session->flow_ctl_mask = 0;
			/* Update the stats */
			session->flow_ctl_dis_cnt++;
		} else {
			break;
		}

		dev_dbg(ipc_mux->dev, "if[%u] FLOW CTRL 0x%08X", cmdh->if_id,
			le32_to_cpu(param->flow_ctl.mask));
		break;

	case MUX_LITE_CMD_LINK_STATUS_REPORT:
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

/* Decode and Send appropriate response to a command block. */
static void ipc_mux_dl_cmd_decode(struct iosm_mux *ipc_mux, struct sk_buff *skb)
{
	struct mux_lite_cmdh *cmdh = (struct mux_lite_cmdh *)skb->data;
	__le32 trans_id = cmdh->transaction_id;

	if (ipc_mux_dl_cmdresps_decode_process(ipc_mux, cmdh)) {
		/* Unable to decode command response indicates the cmd_type
		 * may be a command instead of response. So try to decoding it.
		 */
		if (!ipc_mux_dl_dlcmds_decode_process(ipc_mux, cmdh)) {
			/* Decoded command may need a response. Give the
			 * response according to the command type.
			 */
			union mux_cmd_param *mux_cmd = NULL;
			size_t size = 0;
			u32 cmd = MUX_LITE_CMD_LINK_STATUS_REPORT_RESP;

			if (cmdh->command_type ==
			    cpu_to_le32(MUX_LITE_CMD_LINK_STATUS_REPORT)) {
				mux_cmd = &cmdh->param;
				mux_cmd->link_status_resp.response =
					cpu_to_le32(MUX_CMD_RESP_SUCCESS);
				/* response field is u32 */
				size = sizeof(u32);
			} else if (cmdh->command_type ==
				   cpu_to_le32(MUX_LITE_CMD_FLOW_CTL)) {
				cmd = MUX_LITE_CMD_FLOW_CTL_ACK;
			} else {
				return;
			}

			if (ipc_mux_dl_acb_send_cmds(ipc_mux, cmd, cmdh->if_id,
						     le32_to_cpu(trans_id),
						     mux_cmd, size, false,
						     true))
				dev_err(ipc_mux->dev,
					"if_id %d: cmd send failed",
					cmdh->if_id);
		}
	}
}

/* Pass the DL packet to the netif layer. */
static int ipc_mux_net_receive(struct iosm_mux *ipc_mux, int if_id,
			       struct iosm_wwan *wwan, u32 offset,
			       u8 service_class, struct sk_buff *skb)
{
	struct sk_buff *dest_skb = skb_clone(skb, GFP_ATOMIC);

	if (!dest_skb)
		return -ENOMEM;

	skb_pull(dest_skb, offset);
	skb_set_tail_pointer(dest_skb, dest_skb->len);
	/* Pass the packet to the netif layer. */
	dest_skb->priority = service_class;

	return ipc_wwan_receive(wwan, dest_skb, false, if_id);
}

/* Decode Flow Credit Table in the block */
static void ipc_mux_dl_fcth_decode(struct iosm_mux *ipc_mux,
				   unsigned char *block)
{
	struct ipc_mem_lite_gen_tbl *fct = (struct ipc_mem_lite_gen_tbl *)block;
	struct iosm_wwan *wwan;
	int ul_credits;
	int if_id;

	if (fct->vfl_length != sizeof(fct->vfl.nr_of_bytes)) {
		dev_err(ipc_mux->dev, "unexpected FCT length: %d",
			fct->vfl_length);
		return;
	}

	if_id = fct->if_id;
	if (if_id >= ipc_mux->nr_sessions) {
		dev_err(ipc_mux->dev, "not supported if_id: %d", if_id);
		return;
	}

	/* Is the session active ? */
	if_id = array_index_nospec(if_id, ipc_mux->nr_sessions);
	wwan = ipc_mux->session[if_id].wwan;
	if (!wwan) {
		dev_err(ipc_mux->dev, "session Net ID is NULL");
		return;
	}

	ul_credits = le32_to_cpu(fct->vfl.nr_of_bytes);

	dev_dbg(ipc_mux->dev, "Flow_Credit:: if_id[%d] Old: %d Grants: %d",
		if_id, ipc_mux->session[if_id].ul_flow_credits, ul_credits);

	/* Update the Flow Credit information from ADB */
	ipc_mux->session[if_id].ul_flow_credits += ul_credits;

	/* Check whether the TX can be started */
	if (ipc_mux->session[if_id].ul_flow_credits > 0) {
		ipc_mux->session[if_id].net_tx_stop = false;
		ipc_mux_netif_tx_flowctrl(&ipc_mux->session[if_id],
					  ipc_mux->session[if_id].if_id, false);
	}
}

/* Decode non-aggregated datagram */
static void ipc_mux_dl_adgh_decode(struct iosm_mux *ipc_mux,
				   struct sk_buff *skb)
{
	u32 pad_len, packet_offset;
	struct iosm_wwan *wwan;
	struct mux_adgh *adgh;
	u8 *block = skb->data;
	int rc = 0;
	u8 if_id;

	adgh = (struct mux_adgh *)block;

	if (adgh->signature != cpu_to_le32(MUX_SIG_ADGH)) {
		dev_err(ipc_mux->dev, "invalid ADGH signature received");
		return;
	}

	if_id = adgh->if_id;
	if (if_id >= ipc_mux->nr_sessions) {
		dev_err(ipc_mux->dev, "invalid if_id while decoding %d", if_id);
		return;
	}

	/* Is the session active ? */
	if_id = array_index_nospec(if_id, ipc_mux->nr_sessions);
	wwan = ipc_mux->session[if_id].wwan;
	if (!wwan) {
		dev_err(ipc_mux->dev, "session Net ID is NULL");
		return;
	}

	/* Store the pad len for the corresponding session
	 * Pad bytes as negotiated in the open session less the header size
	 * (see session management chapter for details).
	 * If resulting padding is zero or less, the additional head padding is
	 * omitted. For e.g., if HEAD_PAD_LEN = 16 or less, this field is
	 * omitted if HEAD_PAD_LEN = 20, then this field will have 4 bytes
	 * set to zero
	 */
	pad_len =
		ipc_mux->session[if_id].dl_head_pad_len - IPC_MEM_DL_ETH_OFFSET;
	packet_offset = sizeof(*adgh) + pad_len;

	if_id += ipc_mux->wwan_q_offset;

	/* Pass the packet to the netif layer */
	rc = ipc_mux_net_receive(ipc_mux, if_id, wwan, packet_offset,
				 adgh->service_class, skb);
	if (rc) {
		dev_err(ipc_mux->dev, "mux adgh decoding error");
		return;
	}
	ipc_mux->session[if_id].flush = 1;
}

void ipc_mux_dl_decode(struct iosm_mux *ipc_mux, struct sk_buff *skb)
{
	u32 signature;

	if (!skb->data)
		return;

	/* Decode the MUX header type. */
	signature = le32_to_cpup((__le32 *)skb->data);

	switch (signature) {
	case MUX_SIG_ADGH:
		ipc_mux_dl_adgh_decode(ipc_mux, skb);
		break;

	case MUX_SIG_FCTH:
		ipc_mux_dl_fcth_decode(ipc_mux, skb->data);
		break;

	case MUX_SIG_CMDH:
		ipc_mux_dl_cmd_decode(ipc_mux, skb);
		break;

	default:
		dev_err(ipc_mux->dev, "invalid ABH signature");
	}

	ipc_pcie_kfree_skb(ipc_mux->pcie, skb);
}

static int ipc_mux_ul_skb_alloc(struct iosm_mux *ipc_mux,
				struct mux_adb *ul_adb, u32 type)
{
	/* Take the first element of the free list. */
	struct sk_buff *skb = skb_dequeue(&ul_adb->free_list);
	int qlt_size;

	if (!skb)
		return -EBUSY; /* Wait for a free ADB skb. */

	/* Mark it as UL ADB to select the right free operation. */
	IPC_CB(skb)->op_type = (u8)UL_MUX_OP_ADB;

	switch (type) {
	case MUX_SIG_ADGH:
		/* Save the ADB memory settings. */
		ul_adb->dest_skb = skb;
		ul_adb->buf = skb->data;
		ul_adb->size = IPC_MEM_MAX_DL_MUX_LITE_BUF_SIZE;
		/* reset statistic counter */
		ul_adb->if_cnt = 0;
		ul_adb->payload_size = 0;
		ul_adb->dg_cnt_total = 0;

		ul_adb->adgh = (struct mux_adgh *)skb->data;
		memset(ul_adb->adgh, 0, sizeof(struct mux_adgh));
		break;

	case MUX_SIG_QLTH:
		qlt_size = offsetof(struct ipc_mem_lite_gen_tbl, vfl) +
			   (MUX_QUEUE_LEVEL * sizeof(struct mux_lite_vfl));

		if (qlt_size > IPC_MEM_MAX_DL_MUX_LITE_BUF_SIZE) {
			dev_err(ipc_mux->dev,
				"can't support. QLT size:%d SKB size: %d",
				qlt_size, IPC_MEM_MAX_DL_MUX_LITE_BUF_SIZE);
			return -ERANGE;
		}

		ul_adb->qlth_skb = skb;
		memset((ul_adb->qlth_skb)->data, 0, qlt_size);
		skb_put(skb, qlt_size);
		break;
	}

	return 0;
}

static void ipc_mux_ul_adgh_finish(struct iosm_mux *ipc_mux)
{
	struct mux_adb *ul_adb = &ipc_mux->ul_adb;
	u16 adgh_len;
	long long bytes;
	char *str;

	if (!ul_adb->dest_skb) {
		dev_err(ipc_mux->dev, "no dest skb");
		return;
	}

	adgh_len = le16_to_cpu(ul_adb->adgh->length);
	skb_put(ul_adb->dest_skb, adgh_len);
	skb_queue_tail(&ipc_mux->channel->ul_list, ul_adb->dest_skb);
	ul_adb->dest_skb = NULL;

	if (ipc_mux->ul_flow == MUX_UL_ON_CREDITS) {
		struct mux_session *session;

		session = &ipc_mux->session[ul_adb->adgh->if_id];
		str = "available_credits";
		bytes = (long long)session->ul_flow_credits;

	} else {
		str = "pend_bytes";
		bytes = ipc_mux->ul_data_pend_bytes;
		ipc_mux->ul_data_pend_bytes = ipc_mux->ul_data_pend_bytes +
					      adgh_len;
	}

	dev_dbg(ipc_mux->dev, "UL ADGH: size=%u, if_id=%d, payload=%d, %s=%lld",
		adgh_len, ul_adb->adgh->if_id, ul_adb->payload_size,
		str, bytes);
}

/* Allocates an ADB from the free list and initializes it with ADBH  */
static bool ipc_mux_ul_adb_allocate(struct iosm_mux *ipc_mux,
				    struct mux_adb *adb, int *size_needed,
				    u32 type)
{
	bool ret_val = false;
	int status;

	if (!adb->dest_skb) {
		/* Allocate memory for the ADB including of the
		 * datagram table header.
		 */
		status = ipc_mux_ul_skb_alloc(ipc_mux, adb, type);
		if (status)
			/* Is a pending ADB available ? */
			ret_val = true; /* None. */

		/* Update size need to zero only for new ADB memory */
		*size_needed = 0;
	}

	return ret_val;
}

/* Informs the network stack to stop sending further packets for all opened
 * sessions
 */
static void ipc_mux_stop_tx_for_all_sessions(struct iosm_mux *ipc_mux)
{
	struct mux_session *session;
	int idx;

	for (idx = 0; idx < ipc_mux->nr_sessions; idx++) {
		session = &ipc_mux->session[idx];

		if (!session->wwan)
			continue;

		session->net_tx_stop = true;
	}
}

/* Sends Queue Level Table of all opened sessions */
static bool ipc_mux_lite_send_qlt(struct iosm_mux *ipc_mux)
{
	struct ipc_mem_lite_gen_tbl *qlt;
	struct mux_session *session;
	bool qlt_updated = false;
	int i;
	int qlt_size;

	if (!ipc_mux->initialized || ipc_mux->state != MUX_S_ACTIVE)
		return qlt_updated;

	qlt_size = offsetof(struct ipc_mem_lite_gen_tbl, vfl) +
		   MUX_QUEUE_LEVEL * sizeof(struct mux_lite_vfl);

	for (i = 0; i < ipc_mux->nr_sessions; i++) {
		session = &ipc_mux->session[i];

		if (!session->wwan || session->flow_ctl_mask)
			continue;

		if (ipc_mux_ul_skb_alloc(ipc_mux, &ipc_mux->ul_adb,
					 MUX_SIG_QLTH)) {
			dev_err(ipc_mux->dev,
				"no reserved mem to send QLT of if_id: %d", i);
			break;
		}

		/* Prepare QLT */
		qlt = (struct ipc_mem_lite_gen_tbl *)(ipc_mux->ul_adb.qlth_skb)
			      ->data;
		qlt->signature = cpu_to_le32(MUX_SIG_QLTH);
		qlt->length = cpu_to_le16(qlt_size);
		qlt->if_id = i;
		qlt->vfl_length = MUX_QUEUE_LEVEL * sizeof(struct mux_lite_vfl);
		qlt->reserved[0] = 0;
		qlt->reserved[1] = 0;

		qlt->vfl.nr_of_bytes = cpu_to_le32(session->ul_list.qlen);

		/* Add QLT to the transfer list. */
		skb_queue_tail(&ipc_mux->channel->ul_list,
			       ipc_mux->ul_adb.qlth_skb);

		qlt_updated = true;
		ipc_mux->ul_adb.qlth_skb = NULL;
	}

	if (qlt_updated)
		/* Updates the TDs with ul_list */
		(void)ipc_imem_ul_write_td(ipc_mux->imem);

	return qlt_updated;
}

/* Checks the available credits for the specified session and returns
 * number of packets for which credits are available.
 */
static int ipc_mux_ul_bytes_credits_check(struct iosm_mux *ipc_mux,
					  struct mux_session *session,
					  struct sk_buff_head *ul_list,
					  int max_nr_of_pkts)
{
	int pkts_to_send = 0;
	struct sk_buff *skb;
	int credits = 0;

	if (ipc_mux->ul_flow == MUX_UL_ON_CREDITS) {
		credits = session->ul_flow_credits;
		if (credits <= 0) {
			dev_dbg(ipc_mux->dev,
				"FC::if_id[%d] Insuff.Credits/Qlen:%d/%u",
				session->if_id, session->ul_flow_credits,
				session->ul_list.qlen); /* nr_of_bytes */
			return 0;
		}
	} else {
		credits = IPC_MEM_MUX_UL_FLOWCTRL_HIGH_B -
			  ipc_mux->ul_data_pend_bytes;
		if (credits <= 0) {
			ipc_mux_stop_tx_for_all_sessions(ipc_mux);

			dev_dbg(ipc_mux->dev,
				"if_id[%d] encod. fail Bytes: %llu, thresh: %d",
				session->if_id, ipc_mux->ul_data_pend_bytes,
				IPC_MEM_MUX_UL_FLOWCTRL_HIGH_B);
			return 0;
		}
	}

	/* Check if there are enough credits/bytes available to send the
	 * requested max_nr_of_pkts. Otherwise restrict the nr_of_pkts
	 * depending on available credits.
	 */
	skb_queue_walk(ul_list, skb)
	{
		if (!(credits >= skb->len && pkts_to_send < max_nr_of_pkts))
			break;
		credits -= skb->len;
		pkts_to_send++;
	}

	return pkts_to_send;
}

/* Encode the UL IP packet according to Lite spec. */
static int ipc_mux_ul_adgh_encode(struct iosm_mux *ipc_mux, int session_id,
				  struct mux_session *session,
				  struct sk_buff_head *ul_list,
				  struct mux_adb *adb, int nr_of_pkts)
{
	int offset = sizeof(struct mux_adgh);
	int adb_updated = -EINVAL;
	struct sk_buff *src_skb;
	int aligned_size = 0;
	int nr_of_skb = 0;
	u32 pad_len = 0;

	/* Re-calculate the number of packets depending on number of bytes to be
	 * processed/available credits.
	 */
	nr_of_pkts = ipc_mux_ul_bytes_credits_check(ipc_mux, session, ul_list,
						    nr_of_pkts);

	/* If calculated nr_of_pkts from available credits is <= 0
	 * then nothing to do.
	 */
	if (nr_of_pkts <= 0)
		return 0;

	/* Read configured UL head_pad_length for session.*/
	if (session->ul_head_pad_len > IPC_MEM_DL_ETH_OFFSET)
		pad_len = session->ul_head_pad_len - IPC_MEM_DL_ETH_OFFSET;

	/* Process all pending UL packets for this session
	 * depending on the allocated datagram table size.
	 */
	while (nr_of_pkts > 0) {
		/* get destination skb allocated */
		if (ipc_mux_ul_adb_allocate(ipc_mux, adb, &ipc_mux->size_needed,
					    MUX_SIG_ADGH)) {
			dev_err(ipc_mux->dev, "no reserved memory for ADGH");
			return -ENOMEM;
		}

		/* Peek at the head of the list. */
		src_skb = skb_peek(ul_list);
		if (!src_skb) {
			dev_err(ipc_mux->dev,
				"skb peek return NULL with count : %d",
				nr_of_pkts);
			break;
		}

		/* Calculate the memory value. */
		aligned_size = ALIGN((pad_len + src_skb->len), 4);

		ipc_mux->size_needed = sizeof(struct mux_adgh) + aligned_size;

		if (ipc_mux->size_needed > adb->size) {
			dev_dbg(ipc_mux->dev, "size needed %d, adgh size %d",
				ipc_mux->size_needed, adb->size);
			/* Return 1 if any IP packet is added to the transfer
			 * list.
			 */
			return nr_of_skb ? 1 : 0;
		}

		/* Add buffer (without head padding to next pending transfer) */
		memcpy(adb->buf + offset + pad_len, src_skb->data,
		       src_skb->len);

		adb->adgh->signature = cpu_to_le32(MUX_SIG_ADGH);
		adb->adgh->if_id = session_id;
		adb->adgh->length =
			cpu_to_le16(sizeof(struct mux_adgh) + pad_len +
				    src_skb->len);
		adb->adgh->service_class = src_skb->priority;
		adb->adgh->next_count = --nr_of_pkts;
		adb->dg_cnt_total++;
		adb->payload_size += src_skb->len;

		if (ipc_mux->ul_flow == MUX_UL_ON_CREDITS)
			/* Decrement the credit value as we are processing the
			 * datagram from the UL list.
			 */
			session->ul_flow_credits -= src_skb->len;

		/* Remove the processed elements and free it. */
		src_skb = skb_dequeue(ul_list);
		dev_kfree_skb(src_skb);
		nr_of_skb++;

		ipc_mux_ul_adgh_finish(ipc_mux);
	}

	if (nr_of_skb) {
		/* Send QLT info to modem if pending bytes > high watermark
		 * in case of mux lite
		 */
		if (ipc_mux->ul_flow == MUX_UL_ON_CREDITS ||
		    ipc_mux->ul_data_pend_bytes >=
			    IPC_MEM_MUX_UL_FLOWCTRL_LOW_B)
			adb_updated = ipc_mux_lite_send_qlt(ipc_mux);
		else
			adb_updated = 1;

		/* Updates the TDs with ul_list */
		(void)ipc_imem_ul_write_td(ipc_mux->imem);
	}

	return adb_updated;
}

bool ipc_mux_ul_data_encode(struct iosm_mux *ipc_mux)
{
	struct sk_buff_head *ul_list;
	struct mux_session *session;
	int updated = 0;
	int session_id;
	int dg_n;
	int i;

	if (!ipc_mux || ipc_mux->state != MUX_S_ACTIVE ||
	    ipc_mux->adb_prep_ongoing)
		return false;

	ipc_mux->adb_prep_ongoing = true;

	for (i = 0; i < ipc_mux->nr_sessions; i++) {
		session_id = ipc_mux->rr_next_session;
		session = &ipc_mux->session[session_id];

		/* Go to next handle rr_next_session overflow */
		ipc_mux->rr_next_session++;
		if (ipc_mux->rr_next_session >= ipc_mux->nr_sessions)
			ipc_mux->rr_next_session = 0;

		if (!session->wwan || session->flow_ctl_mask ||
		    session->net_tx_stop)
			continue;

		ul_list = &session->ul_list;

		/* Is something pending in UL and flow ctrl off */
		dg_n = skb_queue_len(ul_list);
		if (dg_n > MUX_MAX_UL_DG_ENTRIES)
			dg_n = MUX_MAX_UL_DG_ENTRIES;

		if (dg_n == 0)
			/* Nothing to do for ipc_mux session
			 * -> try next session id.
			 */
			continue;

		updated = ipc_mux_ul_adgh_encode(ipc_mux, session_id, session,
						 ul_list, &ipc_mux->ul_adb,
						 dg_n);
	}

	ipc_mux->adb_prep_ongoing = false;
	return updated == 1;
}

void ipc_mux_ul_encoded_process(struct iosm_mux *ipc_mux, struct sk_buff *skb)
{
	struct mux_adgh *adgh;
	u16 adgh_len;

	adgh = (struct mux_adgh *)skb->data;
	adgh_len = le16_to_cpu(adgh->length);

	if (adgh->signature == cpu_to_le32(MUX_SIG_ADGH) &&
	    ipc_mux->ul_flow == MUX_UL)
		ipc_mux->ul_data_pend_bytes = ipc_mux->ul_data_pend_bytes -
					      adgh_len;

	if (ipc_mux->ul_flow == MUX_UL)
		dev_dbg(ipc_mux->dev, "ul_data_pend_bytes: %lld",
			ipc_mux->ul_data_pend_bytes);

	/* Reset the skb settings. */
	skb->tail = 0;
	skb->len = 0;

	/* Add the consumed ADB to the free list. */
	skb_queue_tail((&ipc_mux->ul_adb.free_list), skb);
}

/* Start the NETIF uplink send transfer in MUX mode. */
static int ipc_mux_tq_ul_trigger_encode(struct iosm_imem *ipc_imem, int arg,
					void *msg, size_t size)
{
	struct iosm_mux *ipc_mux = ipc_imem->mux;
	bool ul_data_pend = false;

	/* Add session UL data to a ADB and ADGH */
	ul_data_pend = ipc_mux_ul_data_encode(ipc_mux);
	if (ul_data_pend)
		/* Delay the doorbell irq */
		ipc_imem_td_update_timer_start(ipc_mux->imem);

	/* reset the debounce flag */
	ipc_mux->ev_mux_net_transmit_pending = false;

	return 0;
}

int ipc_mux_ul_trigger_encode(struct iosm_mux *ipc_mux, int if_id,
			      struct sk_buff *skb)
{
	struct mux_session *session = &ipc_mux->session[if_id];
	int ret = -EINVAL;

	if (ipc_mux->channel &&
	    ipc_mux->channel->state != IMEM_CHANNEL_ACTIVE) {
		dev_err(ipc_mux->dev,
			"channel state is not IMEM_CHANNEL_ACTIVE");
		goto out;
	}

	if (!session->wwan) {
		dev_err(ipc_mux->dev, "session net ID is NULL");
		ret = -EFAULT;
		goto out;
	}

	/* Session is under flow control.
	 * Check if packet can be queued in session list, if not
	 * suspend net tx
	 */
	if (skb_queue_len(&session->ul_list) >=
	    (session->net_tx_stop ?
		     IPC_MEM_MUX_UL_SESS_FCON_THRESHOLD :
		     (IPC_MEM_MUX_UL_SESS_FCON_THRESHOLD *
		      IPC_MEM_MUX_UL_SESS_FCOFF_THRESHOLD_FACTOR))) {
		ipc_mux_netif_tx_flowctrl(session, session->if_id, true);
		ret = -EBUSY;
		goto out;
	}

	/* Add skb to the uplink skb accumulator. */
	skb_queue_tail(&session->ul_list, skb);

	/* Inform the IPC kthread to pass uplink IP packets to CP. */
	if (!ipc_mux->ev_mux_net_transmit_pending) {
		ipc_mux->ev_mux_net_transmit_pending = true;
		ret = ipc_task_queue_send_task(ipc_mux->imem,
					       ipc_mux_tq_ul_trigger_encode, 0,
					       NULL, 0, false);
		if (ret)
			goto out;
	}
	dev_dbg(ipc_mux->dev, "mux ul if[%d] qlen=%d/%u, len=%d/%d, prio=%d",
		if_id, skb_queue_len(&session->ul_list), session->ul_list.qlen,
		skb->len, skb->truesize, skb->priority);
	ret = 0;
out:
	return ret;
}
