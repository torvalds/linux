// SPDX-License-Identifier: GPL-2.0-only
/*
 * nxpwifi: commands and events
 *
 * Copyright 2011-2024 NXP
 */

#include "cfg.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "cmdevt.h"
#include "wmm.h"
#include "11n.h"

static void nxpwifi_cancel_pending_ioctl(struct nxpwifi_adapter *adapter);

/* Initialize command node; set defaults; buffers are supplied by caller. */
static void
nxpwifi_init_cmd_node(struct nxpwifi_private *priv,
		      struct cmd_ctrl_node *cmd_node,
		      u32 cmd_no, void *data_buf, bool sync)
{
	cmd_node->priv = priv;
	cmd_node->cmd_no = cmd_no;

	if (sync) {
		cmd_node->wait_q_enabled = true;
		cmd_node->cmd_wait_q_woken = false;
		cmd_node->condition = &cmd_node->cmd_wait_q_woken;
	}
	cmd_node->data_buf = data_buf;
	cmd_node->cmd_skb = cmd_node->skb;
	cmd_node->cmd_resp = NULL;
}

/* Get a free command node from cmd_free_q; return NULL if none. */
static struct cmd_ctrl_node *
nxpwifi_get_cmd_node(struct nxpwifi_adapter *adapter)
{
	struct cmd_ctrl_node *cmd_node;

	spin_lock_bh(&adapter->cmd_free_q_lock);
	if (list_empty(&adapter->cmd_free_q)) {
		nxpwifi_dbg(adapter, ERROR,
			    "GET_CMD_NODE: cmd node not available\n");
		spin_unlock_bh(&adapter->cmd_free_q_lock);
		return NULL;
	}
	cmd_node = list_first_entry(&adapter->cmd_free_q,
				    struct cmd_ctrl_node, list);
	list_del(&cmd_node->list);
	spin_unlock_bh(&adapter->cmd_free_q_lock);

	return cmd_node;
}

/* Reset cmd node state; trim cmd skb; complete and clear resp_skb if present. */
static void
nxpwifi_clean_cmd_node(struct nxpwifi_adapter *adapter,
		       struct cmd_ctrl_node *cmd_node)
{
	cmd_node->cmd_no = 0;
	cmd_node->cmd_flag = 0;
	cmd_node->data_buf = NULL;
	cmd_node->wait_q_enabled = false;

	if (cmd_node->cmd_skb)
		skb_trim(cmd_node->cmd_skb, 0);

	if (cmd_node->resp_skb) {
		adapter->if_ops.cmdrsp_complete(adapter, cmd_node->resp_skb);
		cmd_node->resp_skb = NULL;
	}
}

/* Optionally complete waiters, clean the node, and add it back to cmd_free_q. */
static void
nxpwifi_insert_cmd_to_free_q(struct nxpwifi_adapter *adapter,
			     struct cmd_ctrl_node *cmd_node)
{
	if (!cmd_node)
		return;

	if (cmd_node->wait_q_enabled)
		nxpwifi_complete_cmd(adapter, cmd_node);
	/* Clean the node */
	nxpwifi_clean_cmd_node(adapter, cmd_node);

	/* Insert node into cmd_free_q */
	spin_lock_bh(&adapter->cmd_free_q_lock);
	list_add_tail(&cmd_node->list, &adapter->cmd_free_q);
	spin_unlock_bh(&adapter->cmd_free_q_lock);
}

/* Reuse a command node. */
void nxpwifi_recycle_cmd_node(struct nxpwifi_adapter *adapter,
			      struct cmd_ctrl_node *cmd_node)
{
	struct host_cmd_ds_command *host_cmd = (void *)cmd_node->cmd_skb->data;

	nxpwifi_insert_cmd_to_free_q(adapter, cmd_node);

	atomic_dec(&adapter->cmd_pending);
	nxpwifi_dbg(adapter, CMD,
		    "cmd: FREE_CMD: cmd=%#x, cmd_pending=%d\n",
		le16_to_cpu(host_cmd->command),
		atomic_read(&adapter->cmd_pending));
}

/* Copy host command (userspace-provided) into the driver cmd buffer. */
static int nxpwifi_cmd_host_cmd(struct nxpwifi_private *priv,
				struct cmd_ctrl_node *cmd_node)
{
	struct host_cmd_ds_command *cmd;
	struct nxpwifi_ds_misc_cmd *pcmd_ptr;

	cmd = (struct host_cmd_ds_command *)cmd_node->skb->data;
	pcmd_ptr = (struct nxpwifi_ds_misc_cmd *)cmd_node->data_buf;

	/* Copy the HOST command to command buffer */
	memcpy(cmd, pcmd_ptr->cmd, pcmd_ptr->len);
	nxpwifi_dbg(priv->adapter, CMD,
		    "cmd: host cmd size = %d\n", pcmd_ptr->len);
	return 0;
}

/* Send prepared command to FW: set seq no, adjust skb length, log, start timer. */
static int nxpwifi_dnld_cmd_to_fw(struct nxpwifi_private *priv,
				  struct cmd_ctrl_node *cmd_node)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	int ret;
	struct host_cmd_ds_command *host_cmd;
	u16 cmd_code;
	u16 cmd_size;

	if (!adapter || !cmd_node)
		return -EINVAL;

	host_cmd = (struct host_cmd_ds_command *)(cmd_node->cmd_skb->data);

	/* Sanity test */
	if (host_cmd->size == 0) {
		nxpwifi_dbg(adapter, ERROR,
			    "DNLD_CMD: host_cmd is null\t"
			    "or cmd size is 0, not sending\n");
		if (cmd_node->wait_q_enabled)
			adapter->cmd_wait_q.status = -1;
		nxpwifi_recycle_cmd_node(adapter, cmd_node);
		return -EINVAL;
	}

	cmd_code = le16_to_cpu(host_cmd->command);
	cmd_node->cmd_no = cmd_code;
	cmd_size = le16_to_cpu(host_cmd->size);

	if (adapter->hw_status == NXPWIFI_HW_STATUS_RESET &&
	    cmd_code != HOST_CMD_FUNC_SHUTDOWN &&
	    cmd_code != HOST_CMD_FUNC_INIT) {
		nxpwifi_dbg(adapter, ERROR,
			    "DNLD_CMD: FW in reset state, ignore cmd %#x\n",
			cmd_code);
		nxpwifi_recycle_cmd_node(adapter, cmd_node);
		nxpwifi_queue_work(adapter, &adapter->main_work);
		return -EPERM;
	}

	/* Set command sequence number */
	adapter->seq_num++;
	host_cmd->seq_num = cpu_to_le16(HOST_SET_SEQ_NO_BSS_INFO
					(adapter->seq_num,
					 cmd_node->priv->bss_num,
					 cmd_node->priv->bss_type));

	spin_lock_bh(&adapter->nxpwifi_cmd_lock);
	adapter->curr_cmd = cmd_node;
	spin_unlock_bh(&adapter->nxpwifi_cmd_lock);

	/* Adjust skb length */
	if (cmd_node->cmd_skb->len > cmd_size)
		/*
		 * cmd_size is less than sizeof(struct host_cmd_ds_command).
		 * Trim off the unused portion.
		 */
		skb_trim(cmd_node->cmd_skb, cmd_size);
	else if (cmd_node->cmd_skb->len < cmd_size)
		/*
		 * cmd_size is larger than sizeof(struct host_cmd_ds_command)
		 * because we have appended custom element TLV. Increase skb length
		 * accordingly.
		 */
		skb_put(cmd_node->cmd_skb, cmd_size - cmd_node->cmd_skb->len);

	nxpwifi_dbg(adapter, CMD,
		    "cmd: DNLD_CMD: %#x, act %#x, len %d, seqno %#x\n",
		    cmd_code,
		    get_unaligned_le16((u8 *)host_cmd + S_DS_GEN),
		    cmd_size, le16_to_cpu(host_cmd->seq_num));
	nxpwifi_dbg_dump(adapter, CMD_D, "cmd buffer:", host_cmd, cmd_size);

	skb_push(cmd_node->cmd_skb, adapter->intf_hdr_len);
	ret = adapter->if_ops.host_to_card(adapter, NXPWIFI_TYPE_CMD,
					   cmd_node->cmd_skb, NULL);
	skb_pull(cmd_node->cmd_skb, adapter->intf_hdr_len);

	if (ret) {
		nxpwifi_dbg(adapter, ERROR,
			    "DNLD_CMD: host to card failed\n");
		if (cmd_node->wait_q_enabled)
			adapter->cmd_wait_q.status = -1;
		nxpwifi_recycle_cmd_node(adapter, adapter->curr_cmd);

		spin_lock_bh(&adapter->nxpwifi_cmd_lock);
		adapter->curr_cmd = NULL;
		spin_unlock_bh(&adapter->nxpwifi_cmd_lock);

		adapter->dbg.num_cmd_host_to_card_failure++;
		return ret;
	}

	/* Save the last command id and action to debug log */
	adapter->dbg.last_cmd_index =
			(adapter->dbg.last_cmd_index + 1) % DBG_CMD_NUM;
	adapter->dbg.last_cmd_id[adapter->dbg.last_cmd_index] = cmd_code;
	adapter->dbg.last_cmd_act[adapter->dbg.last_cmd_index] =
			get_unaligned_le16((u8 *)host_cmd + S_DS_GEN);

	/*
	 * Setup the timer after transmit command, except that specific
	 * command might not have command response.
	 */
	if (cmd_code != HOST_CMD_FW_DUMP_EVENT)
		mod_timer(&adapter->cmd_timer,
			  jiffies + msecs_to_jiffies(NXPWIFI_TIMER_10S));

	/* Clear BSS_NO_BITS from HOST */
	cmd_code &= HOST_CMD_ID_MASK;

	return 0;
}

/* Send sleep-confirm command to FW; set seq no; resp may be skipped when resp_ctrl=0. */
static int nxpwifi_dnld_sleep_confirm_cmd(struct nxpwifi_adapter *adapter)
{
	int ret;
	struct nxpwifi_private *priv;
	struct nxpwifi_opt_sleep_confirm *sleep_cfm_buf =
				(struct nxpwifi_opt_sleep_confirm *)
						adapter->sleep_cfm->data;

	priv = nxpwifi_get_priv(adapter, NXPWIFI_BSS_ROLE_ANY);

	adapter->seq_num++;
	sleep_cfm_buf->seq_num =
		cpu_to_le16(HOST_SET_SEQ_NO_BSS_INFO
					(adapter->seq_num, priv->bss_num,
					 priv->bss_type));

	nxpwifi_dbg(adapter, CMD,
		    "cmd: DNLD_CMD: %#x, act %#x, len %d, seqno %#x\n",
		le16_to_cpu(sleep_cfm_buf->command),
		le16_to_cpu(sleep_cfm_buf->action),
		le16_to_cpu(sleep_cfm_buf->size),
		le16_to_cpu(sleep_cfm_buf->seq_num));
	nxpwifi_dbg_dump(adapter, CMD_D, "SLEEP_CFM buffer: ", sleep_cfm_buf,
			 le16_to_cpu(sleep_cfm_buf->size));

	skb_push(adapter->sleep_cfm, adapter->intf_hdr_len);
	ret = adapter->if_ops.host_to_card(adapter, NXPWIFI_TYPE_CMD,
					   adapter->sleep_cfm, NULL);
	skb_pull(adapter->sleep_cfm, adapter->intf_hdr_len);

	if (ret) {
		nxpwifi_dbg(adapter, ERROR, "SLEEP_CFM: failed\n");
		adapter->dbg.num_cmd_sleep_cfm_host_to_card_failure++;
		return ret;
	}

	if (!le16_to_cpu(sleep_cfm_buf->resp_ctrl))
		/* Response is not needed for sleep confirm command */
		adapter->ps_state = PS_STATE_SLEEP;
	else
		adapter->ps_state = PS_STATE_SLEEP_CFM;

	if (!le16_to_cpu(sleep_cfm_buf->resp_ctrl) &&
	    (test_bit(NXPWIFI_IS_HS_CONFIGURED, &adapter->work_flags) &&
	     !adapter->sleep_period.period)) {
		adapter->pm_wakeup_card_req = true;
		nxpwifi_hs_activated_event(nxpwifi_get_priv
				(adapter, NXPWIFI_BSS_ROLE_ANY), true);
	}

	return ret;
}

/* Allocate cmd pool and link all nodes to cmd_free_q (used/returned by cmds). */
int nxpwifi_alloc_cmd_buffer(struct nxpwifi_adapter *adapter)
{
	struct cmd_ctrl_node *cmd_array;
	u32 i;

	/* Allocate and initialize struct cmd_ctrl_node */
	cmd_array = kzalloc_objs(struct cmd_ctrl_node,
				 NXPWIFI_NUM_OF_CMD_BUFFER);
	if (!cmd_array)
		return -ENOMEM;

	adapter->cmd_pool = cmd_array;

	/* Allocate and initialize command buffers */
	for (i = 0; i < NXPWIFI_NUM_OF_CMD_BUFFER; i++) {
		cmd_array[i].skb = dev_alloc_skb(NXPWIFI_SIZE_OF_CMD_BUFFER);
		if (!cmd_array[i].skb)
			return -ENOMEM;
	}

	for (i = 0; i < NXPWIFI_NUM_OF_CMD_BUFFER; i++)
		nxpwifi_insert_cmd_to_free_q(adapter, &cmd_array[i]);

	return 0;
}

/* Free cmd pool; release any remaining resp skbs. */
void nxpwifi_free_cmd_buffer(struct nxpwifi_adapter *adapter)
{
	struct cmd_ctrl_node *cmd_array;
	u32 i;

	/* Need to check if cmd pool is allocated or not */
	if (!adapter->cmd_pool) {
		nxpwifi_dbg(adapter, FATAL,
			    "info: FREE_CMD_BUF: cmd_pool is null\n");
		return;
	}

	cmd_array = adapter->cmd_pool;

	/* Release shared memory buffers */
	for (i = 0; i < NXPWIFI_NUM_OF_CMD_BUFFER; i++) {
		if (cmd_array[i].skb) {
			nxpwifi_dbg(adapter, CMD,
				    "cmd: free cmd buffer %d\n", i);
			dev_kfree_skb_any(cmd_array[i].skb);
		}
		if (!cmd_array[i].resp_skb)
			continue;

		dev_kfree_skb_any(cmd_array[i].resp_skb);
	}
	/* Release struct cmd_ctrl_node */
	if (adapter->cmd_pool) {
		nxpwifi_dbg(adapter, CMD,
			    "cmd: free cmd pool\n");
		kfree(adapter->cmd_pool);
		adapter->cmd_pool = NULL;
	}
}

/*
 * Handle FW event: select per-BSS priv, fill rxinfo, dispatch to STA/UAP handler, complete.
 */
int nxpwifi_process_event(struct nxpwifi_adapter *adapter)
{
	int ret, i;
	struct nxpwifi_private *priv =
		nxpwifi_get_priv(adapter, NXPWIFI_BSS_ROLE_ANY);
	struct sk_buff *skb = adapter->event_skb;
	u32 eventcause;
	struct nxpwifi_rxinfo *rx_info;

	if ((adapter->event_cause & EVENT_ID_MASK) == EVENT_RADAR_DETECTED) {
		for (i = 0; i < adapter->priv_num; i++) {
			priv = adapter->priv[i];
			if (nxpwifi_is_11h_active(priv)) {
				adapter->event_cause |=
					((priv->bss_num & 0xff) << 16) |
					((priv->bss_type & 0xff) << 24);
				break;
			}
		}
	}

	eventcause = adapter->event_cause;

	/* Save the last event to debug log */
	adapter->dbg.last_event_index =
		(adapter->dbg.last_event_index + 1) % DBG_CMD_NUM;
	adapter->dbg.last_event[adapter->dbg.last_event_index] =
		(u16)eventcause;

	/* Get BSS number and corresponding priv */
	priv = nxpwifi_get_priv_by_id(adapter, EVENT_GET_BSS_NUM(eventcause),
				      EVENT_GET_BSS_TYPE(eventcause));
	if (!priv)
		priv = nxpwifi_get_priv(adapter, NXPWIFI_BSS_ROLE_ANY);

	/* Clear BSS_NO_BITS from event */
	eventcause &= EVENT_ID_MASK;
	adapter->event_cause = eventcause;

	if (skb) {
		rx_info = NXPWIFI_SKB_RXCB(skb);
		memset(rx_info, 0, sizeof(*rx_info));
		rx_info->bss_num = priv->bss_num;
		rx_info->bss_type = priv->bss_type;
		nxpwifi_dbg_dump(adapter, EVT_D, "Event Buf:",
				 skb->data, skb->len);
	}

	nxpwifi_dbg(adapter, EVENT, "EVENT: cause: %#x\n", eventcause);

	if (priv->bss_role == NXPWIFI_BSS_ROLE_UAP)
		ret = nxpwifi_process_uap_event(priv);
	else
		ret = nxpwifi_process_sta_event(priv);

	adapter->event_cause = 0;
	adapter->event_skb = NULL;
	adapter->if_ops.event_complete(adapter, skb);

	return ret;
}

/*
 * Prepare and queue a command: sanity checks, get node, init, fill, and enqueue/dispatch.
 */
int nxpwifi_send_cmd(struct nxpwifi_private *priv, u16 cmd_no,
		     u16 cmd_action, u32 cmd_oid, void *data_buf, bool sync)
{
	int ret;
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct cmd_ctrl_node *cmd_node;

	if (!adapter) {
		pr_err("PREP_CMD: adapter is NULL\n");
		return -EINVAL;
	}

	if (test_bit(NXPWIFI_IS_SUSPENDED, &adapter->work_flags)) {
		nxpwifi_dbg(adapter, ERROR,
			    "PREP_CMD: device in suspended state\n");
		return -EPERM;
	}

	if (test_bit(NXPWIFI_IS_HS_ENABLING, &adapter->work_flags) &&
	    cmd_no != HOST_CMD_802_11_HS_CFG_ENH) {
		nxpwifi_dbg(adapter, ERROR,
			    "PREP_CMD: host entering sleep state\n");
		return -EPERM;
	}

	if (test_bit(NXPWIFI_SURPRISE_REMOVED, &adapter->work_flags)) {
		nxpwifi_dbg(adapter, ERROR,
			    "PREP_CMD: card is removed\n");
		return -EPERM;
	}

	if (test_bit(NXPWIFI_IS_CMD_TIMEDOUT, &adapter->work_flags)) {
		nxpwifi_dbg(adapter, ERROR,
			    "PREP_CMD: FW is in bad state\n");
		return -EPERM;
	}

	if (adapter->hw_status == NXPWIFI_HW_STATUS_RESET) {
		if (cmd_no != HOST_CMD_FUNC_INIT) {
			nxpwifi_dbg(adapter, ERROR,
				    "PREP_CMD: FW in reset state\n");
			return -EPERM;
		}
	}

	if (priv->adapter->hs_activated_manually &&
	    cmd_no != HOST_CMD_802_11_HS_CFG_ENH) {
		nxpwifi_cancel_hs(priv, NXPWIFI_ASYNC_CMD);
		priv->adapter->hs_activated_manually = false;
	}

	/* Get a new command node */
	cmd_node = nxpwifi_get_cmd_node(adapter);

	if (!cmd_node) {
		nxpwifi_dbg(adapter, ERROR,
			    "PREP_CMD: no free cmd node\n");
		return -ENOMEM;
	}

	/* Initialize the command node */
	nxpwifi_init_cmd_node(priv, cmd_node, cmd_no, data_buf, sync);

	if (!cmd_node->cmd_skb) {
		nxpwifi_dbg(adapter, ERROR,
			    "PREP_CMD: no free cmd buf\n");
		return -ENOMEM;
	}

	skb_put_zero(cmd_node->cmd_skb, sizeof(struct host_cmd_ds_command));

	/* Prepare command */
	if (cmd_no) {
		switch (cmd_no) {
		case HOST_CMD_UAP_SYS_CONFIG:
		case HOST_CMD_UAP_BSS_START:
		case HOST_CMD_UAP_BSS_STOP:
		case HOST_CMD_UAP_STA_DEAUTH:
		case HOST_CMD_APCMD_SYS_RESET:
		case HOST_CMD_APCMD_STA_LIST:
		case HOST_CMD_CHAN_REPORT_REQUEST:
		case HOST_CMD_ADD_NEW_STATION:
			ret = nxpwifi_uap_prepare_cmd(priv, cmd_node,
						      cmd_action, cmd_oid);
			break;
		default:
			ret = nxpwifi_sta_prepare_cmd(priv, cmd_node,
						      cmd_action, cmd_oid);
			break;
		}
	} else {
		ret = nxpwifi_cmd_host_cmd(priv, cmd_node);
		cmd_node->cmd_flag |= CMD_F_HOSTCMD;
	}

	/* Return error, since the command preparation failed */
	if (ret) {
		nxpwifi_dbg(adapter, ERROR,
			    "PREP_CMD: cmd %#x preparation failed\n",
			    cmd_no);
		nxpwifi_insert_cmd_to_free_q(adapter, cmd_node);
		return ret;
	}

	/* Send command */
	if (cmd_no == HOST_CMD_802_11_SCAN ||
	    cmd_no == HOST_CMD_802_11_SCAN_EXT) {
		nxpwifi_queue_scan_cmd(priv, cmd_node);
	} else {
		nxpwifi_insert_cmd_to_pending_q(adapter, cmd_node);
		nxpwifi_queue_work(adapter, &adapter->main_work);
		if (cmd_node->wait_q_enabled)
			ret = nxpwifi_wait_queue_complete(adapter, cmd_node);
	}

	return ret;
}

/* Queue command to cmd_pending_q; EXIT_PS and HS_ACTIVATE go to the head. */
void
nxpwifi_insert_cmd_to_pending_q(struct nxpwifi_adapter *adapter,
				struct cmd_ctrl_node *cmd_node)
{
	struct host_cmd_ds_command *host_cmd = NULL;
	u16 command;
	bool add_tail = true;

	host_cmd = (struct host_cmd_ds_command *)(cmd_node->cmd_skb->data);
	if (!host_cmd) {
		nxpwifi_dbg(adapter, ERROR, "QUEUE_CMD: host_cmd is NULL\n");
		return;
	}

	command = le16_to_cpu(host_cmd->command);

	/* Exit_PS command needs to be queued in the header always. */
	if (command == HOST_CMD_802_11_PS_MODE_ENH) {
		struct host_cmd_ds_802_11_ps_mode_enh *pm =
						&host_cmd->params.psmode_enh;
		if ((le16_to_cpu(pm->action) == DIS_PS) ||
		    (le16_to_cpu(pm->action) == DIS_AUTO_PS)) {
			if (adapter->ps_state != PS_STATE_AWAKE)
				add_tail = false;
		}
	}

	/* Same with exit host sleep cmd, luckily that can't happen at the same time as EXIT_PS */
	if (command == HOST_CMD_802_11_HS_CFG_ENH) {
		struct host_cmd_ds_802_11_hs_cfg_enh *hs_cfg =
			&host_cmd->params.opt_hs_cfg;

		if (le16_to_cpu(hs_cfg->action) == HS_ACTIVATE)
			add_tail = false;
	}

	spin_lock_bh(&adapter->cmd_pending_q_lock);
	if (add_tail)
		list_add_tail(&cmd_node->list, &adapter->cmd_pending_q);
	else
		list_add(&cmd_node->list, &adapter->cmd_pending_q);
	spin_unlock_bh(&adapter->cmd_pending_q_lock);

	atomic_inc(&adapter->cmd_pending);
	nxpwifi_dbg(adapter, CMD,
		    "cmd: QUEUE_CMD: cmd=%#x, cmd_pending=%d\n",
		    command, atomic_read(&adapter->cmd_pending));
}

/* Dequeue next cmd and download to FW; if HS active (except HS_CFG), deactivate it. */
int nxpwifi_exec_next_cmd(struct nxpwifi_adapter *adapter)
{
	struct nxpwifi_private *priv;
	struct cmd_ctrl_node *cmd_node;
	int ret = 0;
	struct host_cmd_ds_command *host_cmd;

	/* Check if already in processing */
	if (adapter->curr_cmd) {
		nxpwifi_dbg(adapter, FATAL,
			    "EXEC_NEXT_CMD: cmd in processing\n");
		return -EBUSY;
	}

	spin_lock_bh(&adapter->nxpwifi_cmd_lock);
	/* Check if any command is pending */
	spin_lock_bh(&adapter->cmd_pending_q_lock);
	if (list_empty(&adapter->cmd_pending_q)) {
		spin_unlock_bh(&adapter->cmd_pending_q_lock);
		spin_unlock_bh(&adapter->nxpwifi_cmd_lock);
		return -ENODATA;
	}
	cmd_node = list_first_entry(&adapter->cmd_pending_q,
				    struct cmd_ctrl_node, list);

	host_cmd = (struct host_cmd_ds_command *)(cmd_node->cmd_skb->data);
	priv = cmd_node->priv;

	if (adapter->ps_state != PS_STATE_AWAKE) {
		nxpwifi_dbg(adapter, ERROR,
			    "%s: cannot send cmd in sleep state,\t"
			    "this should not happen\n", __func__);
		spin_unlock_bh(&adapter->cmd_pending_q_lock);
		spin_unlock_bh(&adapter->nxpwifi_cmd_lock);
		return ret;
	}

	list_del(&cmd_node->list);
	spin_unlock_bh(&adapter->cmd_pending_q_lock);

	spin_unlock_bh(&adapter->nxpwifi_cmd_lock);
	ret = nxpwifi_dnld_cmd_to_fw(priv, cmd_node);
	priv = nxpwifi_get_priv(adapter, NXPWIFI_BSS_ROLE_ANY);
	/*
	 * Any command sent to the firmware when host is in sleep
	 * mode should de-configure host sleep. We should skip the
	 * host sleep configuration command itself though
	 */
	if (priv && host_cmd->command !=
	     cpu_to_le16(HOST_CMD_802_11_HS_CFG_ENH)) {
		if (adapter->hs_activated) {
			clear_bit(NXPWIFI_IS_HS_CONFIGURED,
				  &adapter->work_flags);
			nxpwifi_hs_activated_event(priv, false);
		}
	}

	return ret;
}

static void
nxpwifi_process_cmdresp_error(struct nxpwifi_private *priv,
			      struct host_cmd_ds_command *resp)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct host_cmd_ds_802_11_ps_mode_enh *pm;

	nxpwifi_dbg(adapter, ERROR,
		    "CMD_RESP: cmd %#x error, result=%#x\n",
		    resp->command, resp->result);

	if (adapter->curr_cmd->wait_q_enabled)
		adapter->cmd_wait_q.status = -1;

	switch (le16_to_cpu(resp->command)) {
	case HOST_CMD_802_11_PS_MODE_ENH:
		pm = &resp->params.psmode_enh;
		nxpwifi_dbg(adapter, ERROR,
			    "PS_MODE_ENH cmd failed: result=0x%x action=0x%X\n",
			    resp->result, le16_to_cpu(pm->action));
		break;
	case HOST_CMD_802_11_SCAN:
	case HOST_CMD_802_11_SCAN_EXT:
		nxpwifi_cancel_scan(adapter);
		break;

	case HOST_CMD_MAC_CONTROL:
		break;

	case HOST_CMD_SDIO_SP_RX_AGGR_CFG:
		nxpwifi_dbg(adapter, MSG,
			    "SDIO RX single-port aggregation Not support\n");
		break;

	default:
		break;
	}
	/* Handling errors here */
	nxpwifi_recycle_cmd_node(adapter, adapter->curr_cmd);

	spin_lock_bh(&adapter->nxpwifi_cmd_lock);
	adapter->curr_cmd = NULL;
	spin_unlock_bh(&adapter->nxpwifi_cmd_lock);
}

/* Handle command response: validate, cancel timer, dispatch, set status, recycle node. */
int nxpwifi_process_cmdresp(struct nxpwifi_adapter *adapter)
{
	struct host_cmd_ds_command *resp;
	struct nxpwifi_private *priv =
		nxpwifi_get_priv(adapter, NXPWIFI_BSS_ROLE_ANY);
	int ret = 0;
	u16 orig_cmdresp_no;
	u16 cmdresp_no;
	u16 cmdresp_result;

	if (!adapter->curr_cmd || !adapter->curr_cmd->resp_skb) {
		resp = (struct host_cmd_ds_command *)adapter->upld_buf;
		nxpwifi_dbg(adapter, ERROR,
			    "CMD_RESP: NULL curr_cmd, %#x\n",
			    le16_to_cpu(resp->command));
		return -EINVAL;
	}

	resp = (struct host_cmd_ds_command *)adapter->curr_cmd->resp_skb->data;
	orig_cmdresp_no = le16_to_cpu(resp->command);
	cmdresp_no = (orig_cmdresp_no & HOST_CMD_ID_MASK);

	if (adapter->curr_cmd->cmd_no != cmdresp_no) {
		nxpwifi_dbg(adapter, ERROR,
			    "cmdresp error: cmd=0x%x cmd_resp=0x%x\n",
			    adapter->curr_cmd->cmd_no, cmdresp_no);
		return -EINVAL;
	}
	/* Now we got response from FW, cancel the command timer */
	timer_delete_sync(&adapter->cmd_timer);
	clear_bit(NXPWIFI_IS_CMD_TIMEDOUT, &adapter->work_flags);

	if (adapter->curr_cmd->cmd_flag & CMD_F_HOSTCMD) {
		/* Copy original response back to response buffer */
		struct nxpwifi_ds_misc_cmd *hostcmd;
		u16 size = le16_to_cpu(resp->size);

		nxpwifi_dbg(adapter, INFO,
			    "info: host cmd resp size = %d\n", size);
		size = min_t(u16, size, NXPWIFI_SIZE_OF_CMD_BUFFER);
		if (adapter->curr_cmd->data_buf) {
			hostcmd = adapter->curr_cmd->data_buf;
			hostcmd->len = size;
			memcpy(hostcmd->cmd, resp, size);
		}
	}

	/* Get BSS number and corresponding priv */
	priv = nxpwifi_get_priv_by_id
	       (adapter, HOST_GET_BSS_NO(le16_to_cpu(resp->seq_num)),
		HOST_GET_BSS_TYPE(le16_to_cpu(resp->seq_num)));
	if (!priv)
		priv = nxpwifi_get_priv(adapter, NXPWIFI_BSS_ROLE_ANY);
	/* Clear RET_BIT from HOST */
	resp->command = cpu_to_le16(orig_cmdresp_no & HOST_CMD_ID_MASK);

	cmdresp_no = le16_to_cpu(resp->command);
	cmdresp_result = le16_to_cpu(resp->result);

	/* Save the last command response to debug log */
	adapter->dbg.last_cmd_resp_index =
			(adapter->dbg.last_cmd_resp_index + 1) % DBG_CMD_NUM;
	adapter->dbg.last_cmd_resp_id[adapter->dbg.last_cmd_resp_index] =
								orig_cmdresp_no;

	nxpwifi_dbg(adapter, CMD,
		    "cmd: CMD_RESP: 0x%x, result %d, len %d, seqno 0x%x\n",
		    orig_cmdresp_no, cmdresp_result,
		    le16_to_cpu(resp->size), le16_to_cpu(resp->seq_num));
	nxpwifi_dbg_dump(adapter, CMD_D, "CMD_RESP buffer:", resp,
			 le16_to_cpu(resp->size));

	if (!(orig_cmdresp_no & HOST_RET_BIT)) {
		nxpwifi_dbg(adapter, ERROR, "CMD_RESP: invalid cmd resp\n");
		if (adapter->curr_cmd->wait_q_enabled)
			adapter->cmd_wait_q.status = -1;

		nxpwifi_recycle_cmd_node(adapter, adapter->curr_cmd);
		spin_lock_bh(&adapter->nxpwifi_cmd_lock);
		adapter->curr_cmd = NULL;
		spin_unlock_bh(&adapter->nxpwifi_cmd_lock);
		return -EINVAL;
	}

	if (adapter->curr_cmd->cmd_flag & CMD_F_HOSTCMD) {
		adapter->curr_cmd->cmd_flag &= ~CMD_F_HOSTCMD;
		if (cmdresp_result == HOST_RESULT_OK &&
		    cmdresp_no == HOST_CMD_802_11_HS_CFG_ENH)
			ret = nxpwifi_ret_802_11_hs_cfg(priv, resp);
	} else {
		if (resp->result != HOST_RESULT_OK) {
			nxpwifi_process_cmdresp_error(priv, resp);
			return -EFAULT;
		}
		if (adapter->curr_cmd->cmd_resp) {
			void *data_buf = adapter->curr_cmd->data_buf;

			ret = adapter->curr_cmd->cmd_resp(priv, resp,
							  cmdresp_no,
							  data_buf);
		}
	}

	if (adapter->curr_cmd) {
		if (adapter->curr_cmd->wait_q_enabled)
			adapter->cmd_wait_q.status = ret;

		nxpwifi_recycle_cmd_node(adapter, adapter->curr_cmd);

		spin_lock_bh(&adapter->nxpwifi_cmd_lock);
		adapter->curr_cmd = NULL;
		spin_unlock_bh(&adapter->nxpwifi_cmd_lock);
	}

	return ret;
}

void nxpwifi_process_assoc_resp(struct nxpwifi_adapter *adapter)
{
	struct cfg80211_rx_assoc_resp_data assoc_resp = {
		.uapsd_queues = -1,
	};
	struct nxpwifi_private *priv =
		nxpwifi_get_priv(adapter, NXPWIFI_BSS_ROLE_STA);

	if (priv->assoc_rsp_size) {
		assoc_resp.links[0].bss = priv->req_bss;
		assoc_resp.buf = priv->assoc_rsp_buf;
		assoc_resp.len = priv->assoc_rsp_size;
		cfg80211_rx_assoc_resp(priv->netdev,
				       &assoc_resp);
		priv->assoc_rsp_size = 0;
	}
}

/*
 * Command timeout handler: mark timed out, cancel pending IOCTL, dump/reset device if provided.
 */
void
nxpwifi_cmd_timeout_func(struct timer_list *t)
{
	struct nxpwifi_adapter *adapter = timer_container_of(adapter, t, cmd_timer);
	struct cmd_ctrl_node *cmd_node;

	set_bit(NXPWIFI_IS_CMD_TIMEDOUT, &adapter->work_flags);
	if (!adapter->curr_cmd) {
		nxpwifi_dbg(adapter, ERROR,
			    "cmd: empty curr_cmd\n");
		return;
	}
	cmd_node = adapter->curr_cmd;
	if (cmd_node) {
		adapter->dbg.timeout_cmd_id =
			adapter->dbg.last_cmd_id[adapter->dbg.last_cmd_index];
		adapter->dbg.timeout_cmd_act =
			adapter->dbg.last_cmd_act[adapter->dbg.last_cmd_index];
		nxpwifi_dbg(adapter, MSG,
			    "%s: Timeout cmd id = %#x, act = %#x\n", __func__,
			    adapter->dbg.timeout_cmd_id,
			    adapter->dbg.timeout_cmd_act);

		nxpwifi_dbg(adapter, MSG,
			    "num_data_h2c_failure = %d\n",
			    adapter->dbg.num_tx_host_to_card_failure);
		nxpwifi_dbg(adapter, MSG,
			    "num_cmd_h2c_failure = %d\n",
			    adapter->dbg.num_cmd_host_to_card_failure);

		nxpwifi_dbg(adapter, MSG,
			    "is_cmd_timedout = %d\n",
			    test_bit(NXPWIFI_IS_CMD_TIMEDOUT,
				     &adapter->work_flags));
		nxpwifi_dbg(adapter, MSG,
			    "num_tx_timeout = %d\n",
			    adapter->dbg.num_tx_timeout);

		nxpwifi_dbg(adapter, MSG,
			    "last_cmd_index = %d\n",
			    adapter->dbg.last_cmd_index);
		nxpwifi_dbg(adapter, MSG,
			    "last_cmd_id: %*ph\n",
			    (int)sizeof(adapter->dbg.last_cmd_id),
			    adapter->dbg.last_cmd_id);
		nxpwifi_dbg(adapter, MSG,
			    "last_cmd_act: %*ph\n",
			    (int)sizeof(adapter->dbg.last_cmd_act),
			    adapter->dbg.last_cmd_act);

		nxpwifi_dbg(adapter, MSG,
			    "last_cmd_resp_index = %d\n",
			    adapter->dbg.last_cmd_resp_index);
		nxpwifi_dbg(adapter, MSG,
			    "last_cmd_resp_id: %*ph\n",
			    (int)sizeof(adapter->dbg.last_cmd_resp_id),
			    adapter->dbg.last_cmd_resp_id);

		nxpwifi_dbg(adapter, MSG,
			    "last_event_index = %d\n",
			    adapter->dbg.last_event_index);
		nxpwifi_dbg(adapter, MSG,
			    "last_event: %*ph\n",
			    (int)sizeof(adapter->dbg.last_event),
			    adapter->dbg.last_event);

		nxpwifi_dbg(adapter, MSG,
			    "data_sent=%d cmd_sent=%d\n",
			    adapter->data_sent, adapter->cmd_sent);

		nxpwifi_dbg(adapter, MSG,
			    "ps_mode=%d ps_state=%d\n",
			    adapter->ps_mode, adapter->ps_state);

		if (cmd_node->wait_q_enabled) {
			adapter->cmd_wait_q.status = -ETIMEDOUT;
			nxpwifi_cancel_pending_ioctl(adapter);
		}
	}

	if (adapter->if_ops.device_dump)
		adapter->if_ops.device_dump(adapter);

	if (adapter->if_ops.card_reset)
		adapter->if_ops.card_reset(adapter);
}

void
nxpwifi_cancel_pending_scan_cmd(struct nxpwifi_adapter *adapter)
{
	struct cmd_ctrl_node *cmd_node = NULL, *tmp_node;

	/* Cancel all pending scan command */
	spin_lock_bh(&adapter->scan_pending_q_lock);
	list_for_each_entry_safe(cmd_node, tmp_node,
				 &adapter->scan_pending_q, list) {
		list_del(&cmd_node->list);
		cmd_node->wait_q_enabled = false;
		nxpwifi_insert_cmd_to_free_q(adapter, cmd_node);
	}
	spin_unlock_bh(&adapter->scan_pending_q_lock);
}

/*
 * Cancel current cmd (if waiting), all pending cmds, and pending scan cmds; complete with error.
 */
void
nxpwifi_cancel_all_pending_cmd(struct nxpwifi_adapter *adapter)
{
	struct cmd_ctrl_node *cmd_node = NULL, *tmp_node;

	spin_lock_bh(&adapter->nxpwifi_cmd_lock);
	/* Cancel current cmd */
	if (adapter->curr_cmd && adapter->curr_cmd->wait_q_enabled) {
		adapter->cmd_wait_q.status = -1;
		nxpwifi_complete_cmd(adapter, adapter->curr_cmd);
		adapter->curr_cmd->wait_q_enabled = false;
		/* no recycle probably wait for response */
	}
	/* Cancel all pending command */
	spin_lock_bh(&adapter->cmd_pending_q_lock);
	list_for_each_entry_safe(cmd_node, tmp_node,
				 &adapter->cmd_pending_q, list) {
		list_del(&cmd_node->list);

		if (cmd_node->wait_q_enabled)
			adapter->cmd_wait_q.status = -1;
		nxpwifi_recycle_cmd_node(adapter, cmd_node);
	}
	spin_unlock_bh(&adapter->cmd_pending_q_lock);
	spin_unlock_bh(&adapter->nxpwifi_cmd_lock);

	nxpwifi_cancel_scan(adapter);
}

/* Cancel current/pending commands for the pending IOCTL; also cancel scan cmds. */
static void
nxpwifi_cancel_pending_ioctl(struct nxpwifi_adapter *adapter)
{
	struct cmd_ctrl_node *cmd_node = NULL;

	if (adapter->curr_cmd &&
	    adapter->curr_cmd->wait_q_enabled) {
		spin_lock_bh(&adapter->nxpwifi_cmd_lock);
		cmd_node = adapter->curr_cmd;
		/*
		 * Be careful when setting curr_cmd = NULL:
		 * nxpwifi_process_cmdresp expects a non-NULL pointer.
		 * This is safe here because only cmd_timeout calls this path
		 * and no response is expected at that point.
		 */
		adapter->curr_cmd = NULL;
		spin_unlock_bh(&adapter->nxpwifi_cmd_lock);

		nxpwifi_recycle_cmd_node(adapter, cmd_node);
	}

	nxpwifi_cancel_scan(adapter);
}

/* If no cmd/event/tx is pending, send sleep-confirm to FW; otherwise defer. */
void
nxpwifi_check_ps_cond(struct nxpwifi_adapter *adapter)
{
	if (!adapter->cmd_sent && !atomic_read(&adapter->tx_hw_pending) &&
	    !adapter->curr_cmd && !IS_CARD_RX_RCVD(adapter))
		nxpwifi_dnld_sleep_confirm_cmd(adapter);
	else
		nxpwifi_dbg(adapter, CMD,
			    "cmd: Delay Sleep Confirm (%s%s%s%s)\n",
			    (adapter->cmd_sent) ? "D" : "",
			    atomic_read(&adapter->tx_hw_pending) ? "T" : "",
			    (adapter->curr_cmd) ? "C" : "",
			    (IS_CARD_RX_RCVD(adapter)) ? "R" : "");
}

/* Generate HS activated/deactivated event for userspace; update flags and wake waiters. */
void
nxpwifi_hs_activated_event(struct nxpwifi_private *priv, u8 activated)
{
	if (activated) {
		if (test_bit(NXPWIFI_IS_HS_CONFIGURED,
			     &priv->adapter->work_flags)) {
			priv->adapter->hs_activated = true;
			nxpwifi_update_rxreor_flags(priv->adapter,
						    RXREOR_FORCE_NO_DROP);
			nxpwifi_dbg(priv->adapter, EVENT,
				    "event: hs_activated\n");
			priv->adapter->hs_activate_wait_q_woken = true;
			wake_up_interruptible(&priv->adapter->hs_activate_wait_q);
		} else {
			nxpwifi_dbg(priv->adapter, EVENT,
				    "event: HS not configured\n");
		}
	} else {
		nxpwifi_dbg(priv->adapter, EVENT,
			    "event: hs_deactivated\n");
		priv->adapter->hs_activated = false;
	}
}

/* Handle HS_CFG response: update HS configured/activated flags and emit HS events. */
int nxpwifi_ret_802_11_hs_cfg(struct nxpwifi_private *priv,
			      struct host_cmd_ds_command *resp)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct host_cmd_ds_802_11_hs_cfg_enh *phs_cfg =
		&resp->params.opt_hs_cfg;
	u32 conditions = le32_to_cpu(phs_cfg->params.hs_config.conditions);

	if (phs_cfg->action == cpu_to_le16(HS_ACTIVATE)) {
		nxpwifi_hs_activated_event(priv, true);
		goto done;
	} else {
		nxpwifi_dbg(adapter, CMD,
			    "cmd: CMD_RESP: HS_CFG cmd reply\t"
			    " result=%#x, conditions=0x%x gpio=0x%x gap=0x%x\n",
			    resp->result, conditions,
			    phs_cfg->params.hs_config.gpio,
			    phs_cfg->params.hs_config.gap);
	}
	if (conditions != HS_CFG_CANCEL) {
		set_bit(NXPWIFI_IS_HS_CONFIGURED, &adapter->work_flags);
	} else {
		clear_bit(NXPWIFI_IS_HS_CONFIGURED, &adapter->work_flags);
		if (adapter->hs_activated)
			nxpwifi_hs_activated_event(priv, false);
	}

done:
	return 0;
}

/* On power-up interrupt, wake device and cancel HS if armed; clear flags and notify. */
void
nxpwifi_process_hs_config(struct nxpwifi_adapter *adapter)
{
	nxpwifi_dbg(adapter, INFO,
		    "info: %s: auto cancelling host sleep\t"
		    "since there is interrupt from the firmware\n",
		    __func__);

	adapter->if_ops.wakeup(adapter);

	if (adapter->hs_activated_manually) {
		nxpwifi_cancel_hs(nxpwifi_get_priv(adapter, NXPWIFI_BSS_ROLE_ANY),
				  NXPWIFI_ASYNC_CMD);
		adapter->hs_activated_manually = false;
	}

	adapter->hs_activated = false;
	clear_bit(NXPWIFI_IS_HS_CONFIGURED, &adapter->work_flags);
	clear_bit(NXPWIFI_IS_SUSPENDED, &adapter->work_flags);
	nxpwifi_hs_activated_event(nxpwifi_get_priv(adapter,
						    NXPWIFI_BSS_ROLE_ANY),
				   false);
}
EXPORT_SYMBOL_GPL(nxpwifi_process_hs_config);

/* Handle sleep-confirm response; set ps_state and hs activation accordingly. */
void
nxpwifi_process_sleep_confirm_resp(struct nxpwifi_adapter *adapter,
				   u8 *pbuf, u32 upld_len)
{
	struct host_cmd_ds_command *cmd = (struct host_cmd_ds_command *)pbuf;
	u16 result = le16_to_cpu(cmd->result);
	u16 command = le16_to_cpu(cmd->command);
	u16 seq_num = le16_to_cpu(cmd->seq_num);

	if (!upld_len) {
		nxpwifi_dbg(adapter, ERROR,
			    "%s: cmd size is 0\n", __func__);
		return;
	}

	nxpwifi_dbg(adapter, CMD,
		    "cmd: CMD_RESP: 0x%x, result %d, len %d, seqno 0x%x\n",
		    command, result, le16_to_cpu(cmd->size), seq_num);

	/* Update sequence number */
	seq_num = HOST_GET_SEQ_NO(seq_num);
	/* Clear RET_BIT from HOST */
	command &= HOST_CMD_ID_MASK;

	if (command != HOST_CMD_802_11_PS_MODE_ENH) {
		nxpwifi_dbg(adapter, ERROR,
			    "%s: rcvd unexpected resp for cmd %#x, result = %x\n",
			    __func__, command, result);
		return;
	}

	if (result) {
		nxpwifi_dbg(adapter, ERROR,
			    "%s: sleep confirm cmd failed\n",
			    __func__);
		adapter->pm_wakeup_card_req = false;
		adapter->ps_state = PS_STATE_AWAKE;
		return;
	}
	adapter->pm_wakeup_card_req = true;
	if (test_bit(NXPWIFI_IS_HS_CONFIGURED, &adapter->work_flags))
		nxpwifi_hs_activated_event(nxpwifi_get_priv
						(adapter, NXPWIFI_BSS_ROLE_ANY),
					   true);
	adapter->ps_state = PS_STATE_SLEEP;
	cmd->command = cpu_to_le16(command);
	cmd->seq_num = cpu_to_le16(seq_num);
}
EXPORT_SYMBOL_GPL(nxpwifi_process_sleep_confirm_resp);

int nxpwifi_mgmt_frame_reg(struct nxpwifi_private *priv, u32 mask)
{
	return nxpwifi_send_cmd(priv, HOST_CMD_MGMT_FRAME_REG, HOST_ACT_GEN_SET,
				0, &mask, false);
}

int nxpwifi_set_uap_sys_cfg(struct nxpwifi_private *priv,
			    struct nxpwifi_uap_bss_param *cfg)
{
	return nxpwifi_send_cmd(priv, HOST_CMD_UAP_SYS_CONFIG, HOST_ACT_GEN_SET,
				UAP_BSS_PARAMS_I, cfg, false);
}

int nxpwifi_set_rts(struct nxpwifi_private *priv, u32 rts_thr)
{
	if (rts_thr < NXPWIFI_RTS_THRESHOLD_MIN || rts_thr > NXPWIFI_RTS_THRESHOLD_MAX)
		rts_thr = NXPWIFI_RTS_THRESHOLD_MAX;

	return nxpwifi_send_cmd(priv, HOST_CMD_802_11_SNMP_MIB,
				HOST_ACT_GEN_SET, RTS_THRESH_I, &rts_thr, true);
}

int nxpwifi_set_frag(struct nxpwifi_private *priv, u32 frag_thr)
{
	if (frag_thr < NXPWIFI_FRAG_THRESHOLD_MIN ||
	    frag_thr > NXPWIFI_FRAG_THRESHOLD_MAX)
		frag_thr = NXPWIFI_FRAG_THRESHOLD_MAX;

	return nxpwifi_send_cmd(priv, HOST_CMD_802_11_SNMP_MIB,
				HOST_ACT_GEN_SET, FRAG_THRESH_I, &frag_thr,
				true);
}

int nxpwifi_set_bss_mode(struct nxpwifi_private *priv)
{
	return nxpwifi_send_cmd(priv, HOST_CMD_SET_BSS_MODE, HOST_ACT_GEN_SET,
				0, NULL, true);
}

int nxpwifi_config_monitor_mode(struct nxpwifi_private *priv,
				struct nxpwifi_802_11_net_monitor *cfg)
{
	return nxpwifi_send_cmd(priv, HOST_CMD_802_11_NET_MONITOR,
				HOST_ACT_GEN_SET, 0, cfg, true);
}

int nxpwifi_get_tx_pwr(struct nxpwifi_private *priv)
{
	return nxpwifi_send_cmd(priv, HOST_CMD_RF_TX_PWR, HOST_ACT_GEN_GET, 0,
				NULL, true);
}

int nxpwifi_apply_regdomain(struct nxpwifi_private *priv)
{
	return nxpwifi_send_cmd(priv, HOST_CMD_802_11D_DOMAIN_INFO, HOST_ACT_GEN_SET,
				0, NULL, false);
}

int nxpwifi_get_rssi_info(struct nxpwifi_private *priv)
{
	return nxpwifi_send_cmd(priv, HOST_CMD_RSSI_INFO, HOST_ACT_GEN_GET, 0,
				NULL, true);
}

int nxpwifi_get_802_11_snmp_mib(struct nxpwifi_private *priv, u16 oid, void *value)
{
	return nxpwifi_send_cmd(priv, HOST_CMD_802_11_SNMP_MIB, HOST_ACT_GEN_GET,
				oid, value, true);
}

int nxpwifi_set_rf_antenna(struct nxpwifi_private *priv, void *antcfg)
{
	return nxpwifi_send_cmd(priv, HOST_CMD_RF_ANTENNA, HOST_ACT_GEN_SET, 0, antcfg,
						    true);
}

int nxpwifi_get_rf_antenna(struct nxpwifi_private *priv, u32 *tx_ant, u32 *rx_ant)
{
	int ret;

	ret = nxpwifi_send_cmd(priv, HOST_CMD_RF_ANTENNA, HOST_ACT_GEN_GET, 0, NULL, true);

	if (!ret) {
		*tx_ant = priv->tx_ant;
		*rx_ant = priv->rx_ant;
	}

	return ret;
}

int nxpwifi_ap_stop_bss(struct nxpwifi_private *priv)
{
	return nxpwifi_send_cmd(priv, HOST_CMD_UAP_BSS_STOP, HOST_ACT_GEN_SET,
				0, NULL, true);
}

int nxpwifi_ap_sys_reset(struct nxpwifi_private *priv)
{
	return nxpwifi_send_cmd(priv, HOST_CMD_APCMD_SYS_RESET,
				HOST_ACT_GEN_SET, 0, NULL, true);
}

int nxpwifi_ap_get_sta_list(struct nxpwifi_private *priv)
{
	return nxpwifi_send_cmd(priv, HOST_CMD_APCMD_STA_LIST, HOST_ACT_GEN_GET,
				0, NULL, true);
}

int nxpwifi_set_tx_rate(struct nxpwifi_private *priv, void *bitmap_rates)
{
	return nxpwifi_send_cmd(priv, HOST_CMD_TX_RATE_CFG, HOST_ACT_GEN_SET, 0,
				bitmap_rates, true);
}

int nxpwifi_802_11_subscribe_event(struct nxpwifi_private *priv,
				   struct nxpwifi_ds_misc_subsc_evt *subsc_evt)
{
	return nxpwifi_send_cmd(priv, HOST_CMD_802_11_SUBSCRIBE_EVENT, 0, 0, subsc_evt,
				true);
}

int nxpwifi_uap_sta_deauth(struct nxpwifi_private *priv, u8 *mac)
{
	return nxpwifi_send_cmd(priv, HOST_CMD_UAP_STA_DEAUTH, HOST_ACT_GEN_SET, 0, mac, true);
}

int nxpwifi_bg_scan_config(struct nxpwifi_private *priv, void *bg_scan_cfg)
{
	return nxpwifi_send_cmd(priv, HOST_CMD_802_11_BG_SCAN_CONFIG, HOST_ACT_GEN_SET,
				0, bg_scan_cfg, true);
}

int nxpwifi_mef_cfg(struct nxpwifi_private *priv, void *mef_cfg)
{
	return nxpwifi_send_cmd(priv, HOST_CMD_MEF_CFG, HOST_ACT_GEN_SET, 0, mef_cfg,
				true);
}

int nxpwifi_coalesce_cfg(struct nxpwifi_private *priv, void *coalesce_cfg)
{
	return nxpwifi_send_cmd(priv, HOST_CMD_COALESCE_CFG, HOST_ACT_GEN_SET, 0,
				coalesce_cfg, true);
}

int nxpwifi_add_new_station(struct nxpwifi_private *priv, void *add_sta)
{
	return nxpwifi_send_cmd(priv, HOST_CMD_ADD_NEW_STATION, HOST_ACT_ADD_STA, 0,
				add_sta, true);
}

int nxpwifi_hostcmd(struct nxpwifi_private *priv, struct nxpwifi_ds_misc_cmd *hostcmd)
{
	return nxpwifi_send_cmd(priv, 0, 0, 0, hostcmd, true);
}

int nxpwifi_chan_report_request(struct nxpwifi_private *priv, void *radar_params)
{
	return nxpwifi_send_cmd(priv, HOST_CMD_CHAN_REPORT_REQUEST, HOST_ACT_GEN_SET, 0,
				radar_params, true);
}
