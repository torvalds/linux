/*
 * Marvell Wireless LAN device driver: commands and events
 *
 * Copyright (C) 2011-2014, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#include "decl.h"
#include "ioctl.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "wmm.h"
#include "11n.h"
#include "11ac.h"

/*
 * This function initializes a command node.
 *
 * The actual allocation of the node is not done by this function. It only
 * initiates a node by filling it with default parameters. Similarly,
 * allocation of the different buffers used (IOCTL buffer, data buffer) are
 * not done by this function either.
 */
static void
mwifiex_init_cmd_node(struct mwifiex_private *priv,
		      struct cmd_ctrl_node *cmd_node,
		      u32 cmd_oid, void *data_buf, bool sync)
{
	cmd_node->priv = priv;
	cmd_node->cmd_oid = cmd_oid;
	if (sync) {
		cmd_node->wait_q_enabled = true;
		cmd_node->cmd_wait_q_woken = false;
		cmd_node->condition = &cmd_node->cmd_wait_q_woken;
	}
	cmd_node->data_buf = data_buf;
	cmd_node->cmd_skb = cmd_node->skb;
}

/*
 * This function returns a command node from the free queue depending upon
 * availability.
 */
static struct cmd_ctrl_node *
mwifiex_get_cmd_node(struct mwifiex_adapter *adapter)
{
	struct cmd_ctrl_node *cmd_node;
	unsigned long flags;

	spin_lock_irqsave(&adapter->cmd_free_q_lock, flags);
	if (list_empty(&adapter->cmd_free_q)) {
		mwifiex_dbg(adapter, ERROR,
			    "GET_CMD_NODE: cmd node not available\n");
		spin_unlock_irqrestore(&adapter->cmd_free_q_lock, flags);
		return NULL;
	}
	cmd_node = list_first_entry(&adapter->cmd_free_q,
				    struct cmd_ctrl_node, list);
	list_del(&cmd_node->list);
	spin_unlock_irqrestore(&adapter->cmd_free_q_lock, flags);

	return cmd_node;
}

/*
 * This function cleans up a command node.
 *
 * The function resets the fields including the buffer pointers.
 * This function does not try to free the buffers. They must be
 * freed before calling this function.
 *
 * This function will however call the receive completion callback
 * in case a response buffer is still available before resetting
 * the pointer.
 */
static void
mwifiex_clean_cmd_node(struct mwifiex_adapter *adapter,
		       struct cmd_ctrl_node *cmd_node)
{
	cmd_node->cmd_oid = 0;
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

/*
 * This function sends a host command to the firmware.
 *
 * The function copies the host command into the driver command
 * buffer, which will be transferred to the firmware later by the
 * main thread.
 */
static int mwifiex_cmd_host_cmd(struct mwifiex_private *priv,
				struct host_cmd_ds_command *cmd,
				struct mwifiex_ds_misc_cmd *pcmd_ptr)
{
	/* Copy the HOST command to command buffer */
	memcpy(cmd, pcmd_ptr->cmd, pcmd_ptr->len);
	mwifiex_dbg(priv->adapter, CMD,
		    "cmd: host cmd size = %d\n", pcmd_ptr->len);
	return 0;
}

/*
 * This function downloads a command to the firmware.
 *
 * The function performs sanity tests, sets the command sequence
 * number and size, converts the header fields to CPU format before
 * sending. Afterwards, it logs the command ID and action for debugging
 * and sets up the command timeout timer.
 */
static int mwifiex_dnld_cmd_to_fw(struct mwifiex_private *priv,
				  struct cmd_ctrl_node *cmd_node)
{

	struct mwifiex_adapter *adapter = priv->adapter;
	int ret;
	struct host_cmd_ds_command *host_cmd;
	uint16_t cmd_code;
	uint16_t cmd_size;
	unsigned long flags;
	__le32 tmp;

	if (!adapter || !cmd_node)
		return -1;

	host_cmd = (struct host_cmd_ds_command *) (cmd_node->cmd_skb->data);

	/* Sanity test */
	if (host_cmd == NULL || host_cmd->size == 0) {
		mwifiex_dbg(adapter, ERROR,
			    "DNLD_CMD: host_cmd is null\t"
			    "or cmd size is 0, not sending\n");
		if (cmd_node->wait_q_enabled)
			adapter->cmd_wait_q.status = -1;
		mwifiex_recycle_cmd_node(adapter, cmd_node);
		return -1;
	}

	cmd_code = le16_to_cpu(host_cmd->command);
	cmd_size = le16_to_cpu(host_cmd->size);

	if (adapter->hw_status == MWIFIEX_HW_STATUS_RESET &&
	    cmd_code != HostCmd_CMD_FUNC_SHUTDOWN &&
	    cmd_code != HostCmd_CMD_FUNC_INIT) {
		mwifiex_dbg(adapter, ERROR,
			    "DNLD_CMD: FW in reset state, ignore cmd %#x\n",
			cmd_code);
		mwifiex_recycle_cmd_node(adapter, cmd_node);
		queue_work(adapter->workqueue, &adapter->main_work);
		return -1;
	}

	/* Set command sequence number */
	adapter->seq_num++;
	host_cmd->seq_num = cpu_to_le16(HostCmd_SET_SEQ_NO_BSS_INFO
					(adapter->seq_num,
					 cmd_node->priv->bss_num,
					 cmd_node->priv->bss_type));

	spin_lock_irqsave(&adapter->mwifiex_cmd_lock, flags);
	adapter->curr_cmd = cmd_node;
	spin_unlock_irqrestore(&adapter->mwifiex_cmd_lock, flags);

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
		 * because we have appended custom IE TLV. Increase skb length
		 * accordingly.
		 */
		skb_put(cmd_node->cmd_skb, cmd_size - cmd_node->cmd_skb->len);

	mwifiex_dbg(adapter, CMD,
		    "cmd: DNLD_CMD: %#x, act %#x, len %d, seqno %#x\n",
		    cmd_code,
		    le16_to_cpu(*(__le16 *)((u8 *)host_cmd + S_DS_GEN)),
		    cmd_size, le16_to_cpu(host_cmd->seq_num));
	mwifiex_dbg_dump(adapter, CMD_D, "cmd buffer:", host_cmd, cmd_size);

	if (adapter->iface_type == MWIFIEX_USB) {
		tmp = cpu_to_le32(MWIFIEX_USB_TYPE_CMD);
		skb_push(cmd_node->cmd_skb, MWIFIEX_TYPE_LEN);
		memcpy(cmd_node->cmd_skb->data, &tmp, MWIFIEX_TYPE_LEN);
		adapter->cmd_sent = true;
		ret = adapter->if_ops.host_to_card(adapter,
						   MWIFIEX_USB_EP_CMD_EVENT,
						   cmd_node->cmd_skb, NULL);
		skb_pull(cmd_node->cmd_skb, MWIFIEX_TYPE_LEN);
		if (ret == -EBUSY)
			cmd_node->cmd_skb = NULL;
	} else {
		skb_push(cmd_node->cmd_skb, INTF_HEADER_LEN);
		ret = adapter->if_ops.host_to_card(adapter, MWIFIEX_TYPE_CMD,
						   cmd_node->cmd_skb, NULL);
		skb_pull(cmd_node->cmd_skb, INTF_HEADER_LEN);
	}

	if (ret == -1) {
		mwifiex_dbg(adapter, ERROR,
			    "DNLD_CMD: host to card failed\n");
		if (adapter->iface_type == MWIFIEX_USB)
			adapter->cmd_sent = false;
		if (cmd_node->wait_q_enabled)
			adapter->cmd_wait_q.status = -1;
		mwifiex_recycle_cmd_node(adapter, adapter->curr_cmd);

		spin_lock_irqsave(&adapter->mwifiex_cmd_lock, flags);
		adapter->curr_cmd = NULL;
		spin_unlock_irqrestore(&adapter->mwifiex_cmd_lock, flags);

		adapter->dbg.num_cmd_host_to_card_failure++;
		return -1;
	}

	/* Save the last command id and action to debug log */
	adapter->dbg.last_cmd_index =
			(adapter->dbg.last_cmd_index + 1) % DBG_CMD_NUM;
	adapter->dbg.last_cmd_id[adapter->dbg.last_cmd_index] = cmd_code;
	adapter->dbg.last_cmd_act[adapter->dbg.last_cmd_index] =
			le16_to_cpu(*(__le16 *) ((u8 *) host_cmd + S_DS_GEN));

	/* Clear BSS_NO_BITS from HostCmd */
	cmd_code &= HostCmd_CMD_ID_MASK;

	/* Setup the timer after transmit command */
	mod_timer(&adapter->cmd_timer,
		  jiffies + msecs_to_jiffies(MWIFIEX_TIMER_10S));

	return 0;
}

/*
 * This function downloads a sleep confirm command to the firmware.
 *
 * The function performs sanity tests, sets the command sequence
 * number and size, converts the header fields to CPU format before
 * sending.
 *
 * No responses are needed for sleep confirm command.
 */
static int mwifiex_dnld_sleep_confirm_cmd(struct mwifiex_adapter *adapter)
{
	int ret;
	struct mwifiex_private *priv;
	struct mwifiex_opt_sleep_confirm *sleep_cfm_buf =
				(struct mwifiex_opt_sleep_confirm *)
						adapter->sleep_cfm->data;
	struct sk_buff *sleep_cfm_tmp;
	__le32 tmp;

	priv = mwifiex_get_priv(adapter, MWIFIEX_BSS_ROLE_ANY);

	adapter->seq_num++;
	sleep_cfm_buf->seq_num =
		cpu_to_le16((HostCmd_SET_SEQ_NO_BSS_INFO
					(adapter->seq_num, priv->bss_num,
					 priv->bss_type)));

	mwifiex_dbg(adapter, CMD,
		    "cmd: DNLD_CMD: %#x, act %#x, len %d, seqno %#x\n",
		le16_to_cpu(sleep_cfm_buf->command),
		le16_to_cpu(sleep_cfm_buf->action),
		le16_to_cpu(sleep_cfm_buf->size),
		le16_to_cpu(sleep_cfm_buf->seq_num));
	mwifiex_dbg_dump(adapter, CMD_D, "SLEEP_CFM buffer: ", sleep_cfm_buf,
			 le16_to_cpu(sleep_cfm_buf->size));

	if (adapter->iface_type == MWIFIEX_USB) {
		sleep_cfm_tmp =
			dev_alloc_skb(sizeof(struct mwifiex_opt_sleep_confirm)
				      + MWIFIEX_TYPE_LEN);
		skb_put(sleep_cfm_tmp, sizeof(struct mwifiex_opt_sleep_confirm)
			+ MWIFIEX_TYPE_LEN);
		tmp = cpu_to_le32(MWIFIEX_USB_TYPE_CMD);
		memcpy(sleep_cfm_tmp->data, &tmp, MWIFIEX_TYPE_LEN);
		memcpy(sleep_cfm_tmp->data + MWIFIEX_TYPE_LEN,
		       adapter->sleep_cfm->data,
		       sizeof(struct mwifiex_opt_sleep_confirm));
		ret = adapter->if_ops.host_to_card(adapter,
						   MWIFIEX_USB_EP_CMD_EVENT,
						   sleep_cfm_tmp, NULL);
		if (ret != -EBUSY)
			dev_kfree_skb_any(sleep_cfm_tmp);
	} else {
		skb_push(adapter->sleep_cfm, INTF_HEADER_LEN);
		ret = adapter->if_ops.host_to_card(adapter, MWIFIEX_TYPE_CMD,
						   adapter->sleep_cfm, NULL);
		skb_pull(adapter->sleep_cfm, INTF_HEADER_LEN);
	}

	if (ret == -1) {
		mwifiex_dbg(adapter, ERROR, "SLEEP_CFM: failed\n");
		adapter->dbg.num_cmd_sleep_cfm_host_to_card_failure++;
		return -1;
	}

	if (!le16_to_cpu(sleep_cfm_buf->resp_ctrl))
		/* Response is not needed for sleep confirm command */
		adapter->ps_state = PS_STATE_SLEEP;
	else
		adapter->ps_state = PS_STATE_SLEEP_CFM;

	if (!le16_to_cpu(sleep_cfm_buf->resp_ctrl) &&
	    (adapter->is_hs_configured &&
	     !adapter->sleep_period.period)) {
		adapter->pm_wakeup_card_req = true;
		mwifiex_hs_activated_event(mwifiex_get_priv
				(adapter, MWIFIEX_BSS_ROLE_ANY), true);
	}

	return ret;
}

/*
 * This function allocates the command buffers and links them to
 * the command free queue.
 *
 * The driver uses a pre allocated number of command buffers, which
 * are created at driver initializations and freed at driver cleanup.
 * Every command needs to obtain a command buffer from this pool before
 * it can be issued. The command free queue lists the command buffers
 * currently free to use, while the command pending queue lists the
 * command buffers already in use and awaiting handling. Command buffers
 * are returned to the free queue after use.
 */
int mwifiex_alloc_cmd_buffer(struct mwifiex_adapter *adapter)
{
	struct cmd_ctrl_node *cmd_array;
	u32 i;

	/* Allocate and initialize struct cmd_ctrl_node */
	cmd_array = kcalloc(MWIFIEX_NUM_OF_CMD_BUFFER,
			    sizeof(struct cmd_ctrl_node), GFP_KERNEL);
	if (!cmd_array)
		return -ENOMEM;

	adapter->cmd_pool = cmd_array;

	/* Allocate and initialize command buffers */
	for (i = 0; i < MWIFIEX_NUM_OF_CMD_BUFFER; i++) {
		cmd_array[i].skb = dev_alloc_skb(MWIFIEX_SIZE_OF_CMD_BUFFER);
		if (!cmd_array[i].skb) {
			mwifiex_dbg(adapter, ERROR,
				    "unable to allocate command buffer\n");
			return -ENOMEM;
		}
	}

	for (i = 0; i < MWIFIEX_NUM_OF_CMD_BUFFER; i++)
		mwifiex_insert_cmd_to_free_q(adapter, &cmd_array[i]);

	return 0;
}

/*
 * This function frees the command buffers.
 *
 * The function calls the completion callback for all the command
 * buffers that still have response buffers associated with them.
 */
int mwifiex_free_cmd_buffer(struct mwifiex_adapter *adapter)
{
	struct cmd_ctrl_node *cmd_array;
	u32 i;

	/* Need to check if cmd pool is allocated or not */
	if (!adapter->cmd_pool) {
		mwifiex_dbg(adapter, FATAL,
			    "info: FREE_CMD_BUF: cmd_pool is null\n");
		return 0;
	}

	cmd_array = adapter->cmd_pool;

	/* Release shared memory buffers */
	for (i = 0; i < MWIFIEX_NUM_OF_CMD_BUFFER; i++) {
		if (cmd_array[i].skb) {
			mwifiex_dbg(adapter, CMD,
				    "cmd: free cmd buffer %d\n", i);
			dev_kfree_skb_any(cmd_array[i].skb);
		}
		if (!cmd_array[i].resp_skb)
			continue;

		if (adapter->iface_type == MWIFIEX_USB)
			adapter->if_ops.cmdrsp_complete(adapter,
							cmd_array[i].resp_skb);
		else
			dev_kfree_skb_any(cmd_array[i].resp_skb);
	}
	/* Release struct cmd_ctrl_node */
	if (adapter->cmd_pool) {
		mwifiex_dbg(adapter, CMD,
			    "cmd: free cmd pool\n");
		kfree(adapter->cmd_pool);
		adapter->cmd_pool = NULL;
	}

	return 0;
}

/*
 * This function handles events generated by firmware.
 *
 * Event body of events received from firmware are not used (though they are
 * saved), only the event ID is used. Some events are re-invoked by
 * the driver, with a new event body.
 *
 * After processing, the function calls the completion callback
 * for cleanup.
 */
int mwifiex_process_event(struct mwifiex_adapter *adapter)
{
	int ret;
	struct mwifiex_private *priv =
		mwifiex_get_priv(adapter, MWIFIEX_BSS_ROLE_ANY);
	struct sk_buff *skb = adapter->event_skb;
	u32 eventcause = adapter->event_cause;
	struct mwifiex_rxinfo *rx_info;

	/* Save the last event to debug log */
	adapter->dbg.last_event_index =
			(adapter->dbg.last_event_index + 1) % DBG_CMD_NUM;
	adapter->dbg.last_event[adapter->dbg.last_event_index] =
							(u16) eventcause;

	/* Get BSS number and corresponding priv */
	priv = mwifiex_get_priv_by_id(adapter, EVENT_GET_BSS_NUM(eventcause),
				      EVENT_GET_BSS_TYPE(eventcause));
	if (!priv)
		priv = mwifiex_get_priv(adapter, MWIFIEX_BSS_ROLE_ANY);

	/* Clear BSS_NO_BITS from event */
	eventcause &= EVENT_ID_MASK;
	adapter->event_cause = eventcause;

	if (skb) {
		rx_info = MWIFIEX_SKB_RXCB(skb);
		memset(rx_info, 0, sizeof(*rx_info));
		rx_info->bss_num = priv->bss_num;
		rx_info->bss_type = priv->bss_type;
		mwifiex_dbg_dump(adapter, EVT_D, "Event Buf:",
				 skb->data, skb->len);
	}

	mwifiex_dbg(adapter, EVENT, "EVENT: cause: %#x\n", eventcause);

	if (priv->bss_role == MWIFIEX_BSS_ROLE_UAP)
		ret = mwifiex_process_uap_event(priv);
	else
		ret = mwifiex_process_sta_event(priv);

	adapter->event_cause = 0;
	adapter->event_skb = NULL;
	adapter->if_ops.event_complete(adapter, skb);

	return ret;
}

/*
 * This function prepares a command and send it to the firmware.
 *
 * Preparation includes -
 *      - Sanity tests to make sure the card is still present or the FW
 *        is not reset
 *      - Getting a new command node from the command free queue
 *      - Initializing the command node for default parameters
 *      - Fill up the non-default parameters and buffer pointers
 *      - Add the command to pending queue
 */
int mwifiex_send_cmd(struct mwifiex_private *priv, u16 cmd_no,
		     u16 cmd_action, u32 cmd_oid, void *data_buf, bool sync)
{
	int ret;
	struct mwifiex_adapter *adapter = priv->adapter;
	struct cmd_ctrl_node *cmd_node;
	struct host_cmd_ds_command *cmd_ptr;

	if (!adapter) {
		pr_err("PREP_CMD: adapter is NULL\n");
		return -1;
	}

	if (adapter->is_suspended) {
		mwifiex_dbg(adapter, ERROR,
			    "PREP_CMD: device in suspended state\n");
		return -1;
	}

	if (adapter->hs_enabling && cmd_no != HostCmd_CMD_802_11_HS_CFG_ENH) {
		mwifiex_dbg(adapter, ERROR,
			    "PREP_CMD: host entering sleep state\n");
		return -1;
	}

	if (adapter->surprise_removed) {
		mwifiex_dbg(adapter, ERROR,
			    "PREP_CMD: card is removed\n");
		return -1;
	}

	if (adapter->is_cmd_timedout) {
		mwifiex_dbg(adapter, ERROR,
			    "PREP_CMD: FW is in bad state\n");
		return -1;
	}

	if (adapter->hw_status == MWIFIEX_HW_STATUS_RESET) {
		if (cmd_no != HostCmd_CMD_FUNC_INIT) {
			mwifiex_dbg(adapter, ERROR,
				    "PREP_CMD: FW in reset state\n");
			return -1;
		}
	}

	/* Get a new command node */
	cmd_node = mwifiex_get_cmd_node(adapter);

	if (!cmd_node) {
		mwifiex_dbg(adapter, ERROR,
			    "PREP_CMD: no free cmd node\n");
		return -1;
	}

	/* Initialize the command node */
	mwifiex_init_cmd_node(priv, cmd_node, cmd_oid, data_buf, sync);

	if (!cmd_node->cmd_skb) {
		mwifiex_dbg(adapter, ERROR,
			    "PREP_CMD: no free cmd buf\n");
		return -1;
	}

	memset(skb_put(cmd_node->cmd_skb, sizeof(struct host_cmd_ds_command)),
	       0, sizeof(struct host_cmd_ds_command));

	cmd_ptr = (struct host_cmd_ds_command *) (cmd_node->cmd_skb->data);
	cmd_ptr->command = cpu_to_le16(cmd_no);
	cmd_ptr->result = 0;

	/* Prepare command */
	if (cmd_no) {
		switch (cmd_no) {
		case HostCmd_CMD_UAP_SYS_CONFIG:
		case HostCmd_CMD_UAP_BSS_START:
		case HostCmd_CMD_UAP_BSS_STOP:
		case HostCmd_CMD_UAP_STA_DEAUTH:
		case HOST_CMD_APCMD_SYS_RESET:
		case HOST_CMD_APCMD_STA_LIST:
			ret = mwifiex_uap_prepare_cmd(priv, cmd_no, cmd_action,
						      cmd_oid, data_buf,
						      cmd_ptr);
			break;
		default:
			ret = mwifiex_sta_prepare_cmd(priv, cmd_no, cmd_action,
						      cmd_oid, data_buf,
						      cmd_ptr);
			break;
		}
	} else {
		ret = mwifiex_cmd_host_cmd(priv, cmd_ptr, data_buf);
		cmd_node->cmd_flag |= CMD_F_HOSTCMD;
	}

	/* Return error, since the command preparation failed */
	if (ret) {
		mwifiex_dbg(adapter, ERROR,
			    "PREP_CMD: cmd %#x preparation failed\n",
			cmd_no);
		mwifiex_insert_cmd_to_free_q(adapter, cmd_node);
		return -1;
	}

	/* Send command */
	if (cmd_no == HostCmd_CMD_802_11_SCAN ||
	    cmd_no == HostCmd_CMD_802_11_SCAN_EXT) {
		mwifiex_queue_scan_cmd(priv, cmd_node);
	} else {
		mwifiex_insert_cmd_to_pending_q(adapter, cmd_node, true);
		queue_work(adapter->workqueue, &adapter->main_work);
		if (cmd_node->wait_q_enabled)
			ret = mwifiex_wait_queue_complete(adapter, cmd_node);
	}

	return ret;
}

/*
 * This function returns a command to the command free queue.
 *
 * The function also calls the completion callback if required, before
 * cleaning the command node and re-inserting it into the free queue.
 */
void
mwifiex_insert_cmd_to_free_q(struct mwifiex_adapter *adapter,
			     struct cmd_ctrl_node *cmd_node)
{
	unsigned long flags;

	if (!cmd_node)
		return;

	if (cmd_node->wait_q_enabled)
		mwifiex_complete_cmd(adapter, cmd_node);
	/* Clean the node */
	mwifiex_clean_cmd_node(adapter, cmd_node);

	/* Insert node into cmd_free_q */
	spin_lock_irqsave(&adapter->cmd_free_q_lock, flags);
	list_add_tail(&cmd_node->list, &adapter->cmd_free_q);
	spin_unlock_irqrestore(&adapter->cmd_free_q_lock, flags);
}

/* This function reuses a command node. */
void mwifiex_recycle_cmd_node(struct mwifiex_adapter *adapter,
			      struct cmd_ctrl_node *cmd_node)
{
	struct host_cmd_ds_command *host_cmd = (void *)cmd_node->cmd_skb->data;

	mwifiex_insert_cmd_to_free_q(adapter, cmd_node);

	atomic_dec(&adapter->cmd_pending);
	mwifiex_dbg(adapter, CMD,
		    "cmd: FREE_CMD: cmd=%#x, cmd_pending=%d\n",
		le16_to_cpu(host_cmd->command),
		atomic_read(&adapter->cmd_pending));
}

/*
 * This function queues a command to the command pending queue.
 *
 * This in effect adds the command to the command list to be executed.
 * Exit PS command is handled specially, by placing it always to the
 * front of the command queue.
 */
void
mwifiex_insert_cmd_to_pending_q(struct mwifiex_adapter *adapter,
				struct cmd_ctrl_node *cmd_node, u32 add_tail)
{
	struct host_cmd_ds_command *host_cmd = NULL;
	u16 command;
	unsigned long flags;

	host_cmd = (struct host_cmd_ds_command *) (cmd_node->cmd_skb->data);
	if (!host_cmd) {
		mwifiex_dbg(adapter, ERROR, "QUEUE_CMD: host_cmd is NULL\n");
		return;
	}

	command = le16_to_cpu(host_cmd->command);

	/* Exit_PS command needs to be queued in the header always. */
	if (command == HostCmd_CMD_802_11_PS_MODE_ENH) {
		struct host_cmd_ds_802_11_ps_mode_enh *pm =
						&host_cmd->params.psmode_enh;
		if ((le16_to_cpu(pm->action) == DIS_PS) ||
		    (le16_to_cpu(pm->action) == DIS_AUTO_PS)) {
			if (adapter->ps_state != PS_STATE_AWAKE)
				add_tail = false;
		}
	}

	spin_lock_irqsave(&adapter->cmd_pending_q_lock, flags);
	if (add_tail)
		list_add_tail(&cmd_node->list, &adapter->cmd_pending_q);
	else
		list_add(&cmd_node->list, &adapter->cmd_pending_q);
	spin_unlock_irqrestore(&adapter->cmd_pending_q_lock, flags);

	atomic_inc(&adapter->cmd_pending);
	mwifiex_dbg(adapter, CMD,
		    "cmd: QUEUE_CMD: cmd=%#x, cmd_pending=%d\n",
		command, atomic_read(&adapter->cmd_pending));
}

/*
 * This function executes the next command in command pending queue.
 *
 * This function will fail if a command is already in processing stage,
 * otherwise it will dequeue the first command from the command pending
 * queue and send to the firmware.
 *
 * If the device is currently in host sleep mode, any commands, except the
 * host sleep configuration command will de-activate the host sleep. For PS
 * mode, the function will put the firmware back to sleep if applicable.
 */
int mwifiex_exec_next_cmd(struct mwifiex_adapter *adapter)
{
	struct mwifiex_private *priv;
	struct cmd_ctrl_node *cmd_node;
	int ret = 0;
	struct host_cmd_ds_command *host_cmd;
	unsigned long cmd_flags;
	unsigned long cmd_pending_q_flags;

	/* Check if already in processing */
	if (adapter->curr_cmd) {
		mwifiex_dbg(adapter, FATAL,
			    "EXEC_NEXT_CMD: cmd in processing\n");
		return -1;
	}

	spin_lock_irqsave(&adapter->mwifiex_cmd_lock, cmd_flags);
	/* Check if any command is pending */
	spin_lock_irqsave(&adapter->cmd_pending_q_lock, cmd_pending_q_flags);
	if (list_empty(&adapter->cmd_pending_q)) {
		spin_unlock_irqrestore(&adapter->cmd_pending_q_lock,
				       cmd_pending_q_flags);
		spin_unlock_irqrestore(&adapter->mwifiex_cmd_lock, cmd_flags);
		return 0;
	}
	cmd_node = list_first_entry(&adapter->cmd_pending_q,
				    struct cmd_ctrl_node, list);
	spin_unlock_irqrestore(&adapter->cmd_pending_q_lock,
			       cmd_pending_q_flags);

	host_cmd = (struct host_cmd_ds_command *) (cmd_node->cmd_skb->data);
	priv = cmd_node->priv;

	if (adapter->ps_state != PS_STATE_AWAKE) {
		mwifiex_dbg(adapter, ERROR,
			    "%s: cannot send cmd in sleep state,\t"
			    "this should not happen\n", __func__);
		spin_unlock_irqrestore(&adapter->mwifiex_cmd_lock, cmd_flags);
		return ret;
	}

	spin_lock_irqsave(&adapter->cmd_pending_q_lock, cmd_pending_q_flags);
	list_del(&cmd_node->list);
	spin_unlock_irqrestore(&adapter->cmd_pending_q_lock,
			       cmd_pending_q_flags);

	spin_unlock_irqrestore(&adapter->mwifiex_cmd_lock, cmd_flags);
	ret = mwifiex_dnld_cmd_to_fw(priv, cmd_node);
	priv = mwifiex_get_priv(adapter, MWIFIEX_BSS_ROLE_ANY);
	/* Any command sent to the firmware when host is in sleep
	 * mode should de-configure host sleep. We should skip the
	 * host sleep configuration command itself though
	 */
	if (priv && (host_cmd->command !=
	     cpu_to_le16(HostCmd_CMD_802_11_HS_CFG_ENH))) {
		if (adapter->hs_activated) {
			adapter->is_hs_configured = false;
			mwifiex_hs_activated_event(priv, false);
		}
	}

	return ret;
}

/*
 * This function handles the command response.
 *
 * After processing, the function cleans the command node and puts
 * it back to the command free queue.
 */
int mwifiex_process_cmdresp(struct mwifiex_adapter *adapter)
{
	struct host_cmd_ds_command *resp;
	struct mwifiex_private *priv =
		mwifiex_get_priv(adapter, MWIFIEX_BSS_ROLE_ANY);
	int ret = 0;
	uint16_t orig_cmdresp_no;
	uint16_t cmdresp_no;
	uint16_t cmdresp_result;
	unsigned long flags;

	/* Now we got response from FW, cancel the command timer */
	del_timer_sync(&adapter->cmd_timer);

	if (!adapter->curr_cmd || !adapter->curr_cmd->resp_skb) {
		resp = (struct host_cmd_ds_command *) adapter->upld_buf;
		mwifiex_dbg(adapter, ERROR,
			    "CMD_RESP: NULL curr_cmd, %#x\n",
			    le16_to_cpu(resp->command));
		return -1;
	}

	adapter->is_cmd_timedout = 0;

	resp = (struct host_cmd_ds_command *) adapter->curr_cmd->resp_skb->data;
	if (adapter->curr_cmd->cmd_flag & CMD_F_HOSTCMD) {
		/* Copy original response back to response buffer */
		struct mwifiex_ds_misc_cmd *hostcmd;
		uint16_t size = le16_to_cpu(resp->size);
		mwifiex_dbg(adapter, INFO,
			    "info: host cmd resp size = %d\n", size);
		size = min_t(u16, size, MWIFIEX_SIZE_OF_CMD_BUFFER);
		if (adapter->curr_cmd->data_buf) {
			hostcmd = adapter->curr_cmd->data_buf;
			hostcmd->len = size;
			memcpy(hostcmd->cmd, resp, size);
		}
	}
	orig_cmdresp_no = le16_to_cpu(resp->command);

	/* Get BSS number and corresponding priv */
	priv = mwifiex_get_priv_by_id(adapter,
			     HostCmd_GET_BSS_NO(le16_to_cpu(resp->seq_num)),
			     HostCmd_GET_BSS_TYPE(le16_to_cpu(resp->seq_num)));
	if (!priv)
		priv = mwifiex_get_priv(adapter, MWIFIEX_BSS_ROLE_ANY);
	/* Clear RET_BIT from HostCmd */
	resp->command = cpu_to_le16(orig_cmdresp_no & HostCmd_CMD_ID_MASK);

	cmdresp_no = le16_to_cpu(resp->command);
	cmdresp_result = le16_to_cpu(resp->result);

	/* Save the last command response to debug log */
	adapter->dbg.last_cmd_resp_index =
			(adapter->dbg.last_cmd_resp_index + 1) % DBG_CMD_NUM;
	adapter->dbg.last_cmd_resp_id[adapter->dbg.last_cmd_resp_index] =
								orig_cmdresp_no;

	mwifiex_dbg(adapter, CMD,
		    "cmd: CMD_RESP: 0x%x, result %d, len %d, seqno 0x%x\n",
		    orig_cmdresp_no, cmdresp_result,
		    le16_to_cpu(resp->size), le16_to_cpu(resp->seq_num));
	mwifiex_dbg_dump(adapter, CMD_D, "CMD_RESP buffer:", resp,
			 le16_to_cpu(resp->size));

	if (!(orig_cmdresp_no & HostCmd_RET_BIT)) {
		mwifiex_dbg(adapter, ERROR, "CMD_RESP: invalid cmd resp\n");
		if (adapter->curr_cmd->wait_q_enabled)
			adapter->cmd_wait_q.status = -1;

		mwifiex_recycle_cmd_node(adapter, adapter->curr_cmd);
		spin_lock_irqsave(&adapter->mwifiex_cmd_lock, flags);
		adapter->curr_cmd = NULL;
		spin_unlock_irqrestore(&adapter->mwifiex_cmd_lock, flags);
		return -1;
	}

	if (adapter->curr_cmd->cmd_flag & CMD_F_HOSTCMD) {
		adapter->curr_cmd->cmd_flag &= ~CMD_F_HOSTCMD;
		if ((cmdresp_result == HostCmd_RESULT_OK) &&
		    (cmdresp_no == HostCmd_CMD_802_11_HS_CFG_ENH))
			ret = mwifiex_ret_802_11_hs_cfg(priv, resp);
	} else {
		/* handle response */
		ret = mwifiex_process_sta_cmdresp(priv, cmdresp_no, resp);
	}

	/* Check init command response */
	if (adapter->hw_status == MWIFIEX_HW_STATUS_INITIALIZING) {
		if (ret) {
			mwifiex_dbg(adapter, ERROR,
				    "%s: cmd %#x failed during\t"
				    "initialization\n", __func__, cmdresp_no);
			mwifiex_init_fw_complete(adapter);
			return -1;
		} else if (adapter->last_init_cmd == cmdresp_no)
			adapter->hw_status = MWIFIEX_HW_STATUS_INIT_DONE;
	}

	if (adapter->curr_cmd) {
		if (adapter->curr_cmd->wait_q_enabled)
			adapter->cmd_wait_q.status = ret;

		mwifiex_recycle_cmd_node(adapter, adapter->curr_cmd);

		spin_lock_irqsave(&adapter->mwifiex_cmd_lock, flags);
		adapter->curr_cmd = NULL;
		spin_unlock_irqrestore(&adapter->mwifiex_cmd_lock, flags);
	}

	return ret;
}

/*
 * This function handles the timeout of command sending.
 *
 * It will re-send the same command again.
 */
void
mwifiex_cmd_timeout_func(unsigned long function_context)
{
	struct mwifiex_adapter *adapter =
		(struct mwifiex_adapter *) function_context;
	struct cmd_ctrl_node *cmd_node;

	adapter->is_cmd_timedout = 1;
	if (!adapter->curr_cmd) {
		mwifiex_dbg(adapter, ERROR,
			    "cmd: empty curr_cmd\n");
		return;
	}
	cmd_node = adapter->curr_cmd;
	if (cmd_node) {
		adapter->dbg.timeout_cmd_id =
			adapter->dbg.last_cmd_id[adapter->dbg.last_cmd_index];
		adapter->dbg.timeout_cmd_act =
			adapter->dbg.last_cmd_act[adapter->dbg.last_cmd_index];
		mwifiex_dbg(adapter, MSG,
			    "%s: Timeout cmd id = %#x, act = %#x\n", __func__,
			    adapter->dbg.timeout_cmd_id,
			    adapter->dbg.timeout_cmd_act);

		mwifiex_dbg(adapter, MSG,
			    "num_data_h2c_failure = %d\n",
			    adapter->dbg.num_tx_host_to_card_failure);
		mwifiex_dbg(adapter, MSG,
			    "num_cmd_h2c_failure = %d\n",
			    adapter->dbg.num_cmd_host_to_card_failure);

		mwifiex_dbg(adapter, MSG,
			    "is_cmd_timedout = %d\n",
			    adapter->is_cmd_timedout);
		mwifiex_dbg(adapter, MSG,
			    "num_tx_timeout = %d\n",
			    adapter->dbg.num_tx_timeout);

		mwifiex_dbg(adapter, MSG,
			    "last_cmd_index = %d\n",
			    adapter->dbg.last_cmd_index);
		mwifiex_dbg(adapter, MSG,
			    "last_cmd_id: %*ph\n",
			    (int)sizeof(adapter->dbg.last_cmd_id),
			    adapter->dbg.last_cmd_id);
		mwifiex_dbg(adapter, MSG,
			    "last_cmd_act: %*ph\n",
			    (int)sizeof(adapter->dbg.last_cmd_act),
			    adapter->dbg.last_cmd_act);

		mwifiex_dbg(adapter, MSG,
			    "last_cmd_resp_index = %d\n",
			    adapter->dbg.last_cmd_resp_index);
		mwifiex_dbg(adapter, MSG,
			    "last_cmd_resp_id: %*ph\n",
			    (int)sizeof(adapter->dbg.last_cmd_resp_id),
			    adapter->dbg.last_cmd_resp_id);

		mwifiex_dbg(adapter, MSG,
			    "last_event_index = %d\n",
			    adapter->dbg.last_event_index);
		mwifiex_dbg(adapter, MSG,
			    "last_event: %*ph\n",
			    (int)sizeof(adapter->dbg.last_event),
			    adapter->dbg.last_event);

		mwifiex_dbg(adapter, MSG,
			    "data_sent=%d cmd_sent=%d\n",
			    adapter->data_sent, adapter->cmd_sent);

		mwifiex_dbg(adapter, MSG,
			    "ps_mode=%d ps_state=%d\n",
			    adapter->ps_mode, adapter->ps_state);

		if (cmd_node->wait_q_enabled) {
			adapter->cmd_wait_q.status = -ETIMEDOUT;
			mwifiex_cancel_pending_ioctl(adapter);
		}
	}
	if (adapter->hw_status == MWIFIEX_HW_STATUS_INITIALIZING) {
		mwifiex_init_fw_complete(adapter);
		return;
	}

	if (adapter->if_ops.device_dump)
		adapter->if_ops.device_dump(adapter);

	if (adapter->if_ops.card_reset)
		adapter->if_ops.card_reset(adapter);
}

/*
 * This function cancels all the pending commands.
 *
 * The current command, all commands in command pending queue and all scan
 * commands in scan pending queue are cancelled. All the completion callbacks
 * are called with failure status to ensure cleanup.
 */
void
mwifiex_cancel_all_pending_cmd(struct mwifiex_adapter *adapter)
{
	struct cmd_ctrl_node *cmd_node = NULL, *tmp_node;
	unsigned long flags, cmd_flags;
	struct mwifiex_private *priv;
	int i;

	spin_lock_irqsave(&adapter->mwifiex_cmd_lock, cmd_flags);
	/* Cancel current cmd */
	if ((adapter->curr_cmd) && (adapter->curr_cmd->wait_q_enabled)) {
		adapter->curr_cmd->wait_q_enabled = false;
		adapter->cmd_wait_q.status = -1;
		mwifiex_complete_cmd(adapter, adapter->curr_cmd);
		/* no recycle probably wait for response */
	}
	/* Cancel all pending command */
	spin_lock_irqsave(&adapter->cmd_pending_q_lock, flags);
	list_for_each_entry_safe(cmd_node, tmp_node,
				 &adapter->cmd_pending_q, list) {
		list_del(&cmd_node->list);
		spin_unlock_irqrestore(&adapter->cmd_pending_q_lock, flags);

		if (cmd_node->wait_q_enabled)
			adapter->cmd_wait_q.status = -1;
		mwifiex_recycle_cmd_node(adapter, cmd_node);
		spin_lock_irqsave(&adapter->cmd_pending_q_lock, flags);
	}
	spin_unlock_irqrestore(&adapter->cmd_pending_q_lock, flags);
	spin_unlock_irqrestore(&adapter->mwifiex_cmd_lock, cmd_flags);

	/* Cancel all pending scan command */
	spin_lock_irqsave(&adapter->scan_pending_q_lock, flags);
	list_for_each_entry_safe(cmd_node, tmp_node,
				 &adapter->scan_pending_q, list) {
		list_del(&cmd_node->list);

		cmd_node->wait_q_enabled = false;
		mwifiex_insert_cmd_to_free_q(adapter, cmd_node);
	}
	spin_unlock_irqrestore(&adapter->scan_pending_q_lock, flags);

	if (adapter->scan_processing) {
		spin_lock_irqsave(&adapter->mwifiex_cmd_lock, cmd_flags);
		adapter->scan_processing = false;
		spin_unlock_irqrestore(&adapter->mwifiex_cmd_lock, cmd_flags);
		for (i = 0; i < adapter->priv_num; i++) {
			priv = adapter->priv[i];
			if (!priv)
				continue;
			if (priv->scan_request) {
				mwifiex_dbg(adapter, WARN, "info: aborting scan\n");
				cfg80211_scan_done(priv->scan_request, 1);
				priv->scan_request = NULL;
			}
		}
	}
}

/*
 * This function cancels all pending commands that matches with
 * the given IOCTL request.
 *
 * Both the current command buffer and the pending command queue are
 * searched for matching IOCTL request. The completion callback of
 * the matched command is called with failure status to ensure cleanup.
 * In case of scan commands, all pending commands in scan pending queue
 * are cancelled.
 */
void
mwifiex_cancel_pending_ioctl(struct mwifiex_adapter *adapter)
{
	struct cmd_ctrl_node *cmd_node = NULL, *tmp_node = NULL;
	unsigned long cmd_flags;
	unsigned long scan_pending_q_flags;
	struct mwifiex_private *priv;
	int i;

	if ((adapter->curr_cmd) &&
	    (adapter->curr_cmd->wait_q_enabled)) {
		spin_lock_irqsave(&adapter->mwifiex_cmd_lock, cmd_flags);
		cmd_node = adapter->curr_cmd;
		/* setting curr_cmd to NULL is quite dangerous, because
		 * mwifiex_process_cmdresp checks curr_cmd to be != NULL
		 * at the beginning then relies on it and dereferences
		 * it at will
		 * this probably works since mwifiex_cmd_timeout_func
		 * is the only caller of this function and responses
		 * at that point
		 */
		adapter->curr_cmd = NULL;
		spin_unlock_irqrestore(&adapter->mwifiex_cmd_lock, cmd_flags);

		mwifiex_recycle_cmd_node(adapter, cmd_node);
	}

	/* Cancel all pending scan command */
	spin_lock_irqsave(&adapter->scan_pending_q_lock,
			  scan_pending_q_flags);
	list_for_each_entry_safe(cmd_node, tmp_node,
				 &adapter->scan_pending_q, list) {
		list_del(&cmd_node->list);
		cmd_node->wait_q_enabled = false;
		mwifiex_insert_cmd_to_free_q(adapter, cmd_node);
	}
	spin_unlock_irqrestore(&adapter->scan_pending_q_lock,
			       scan_pending_q_flags);

	if (adapter->scan_processing) {
		spin_lock_irqsave(&adapter->mwifiex_cmd_lock, cmd_flags);
		adapter->scan_processing = false;
		spin_unlock_irqrestore(&adapter->mwifiex_cmd_lock, cmd_flags);
		for (i = 0; i < adapter->priv_num; i++) {
			priv = adapter->priv[i];
			if (!priv)
				continue;
			if (priv->scan_request) {
				mwifiex_dbg(adapter, WARN, "info: aborting scan\n");
				cfg80211_scan_done(priv->scan_request, 1);
				priv->scan_request = NULL;
			}
		}
	}
}

/*
 * This function sends the sleep confirm command to firmware, if
 * possible.
 *
 * The sleep confirm command cannot be issued if command response,
 * data response or event response is awaiting handling, or if we
 * are in the middle of sending a command, or expecting a command
 * response.
 */
void
mwifiex_check_ps_cond(struct mwifiex_adapter *adapter)
{
	if (!adapter->cmd_sent &&
	    !adapter->curr_cmd && !IS_CARD_RX_RCVD(adapter))
		mwifiex_dnld_sleep_confirm_cmd(adapter);
	else
		mwifiex_dbg(adapter, CMD,
			    "cmd: Delay Sleep Confirm (%s%s%s)\n",
			    (adapter->cmd_sent) ? "D" : "",
			    (adapter->curr_cmd) ? "C" : "",
			    (IS_CARD_RX_RCVD(adapter)) ? "R" : "");
}

/*
 * This function sends a Host Sleep activated event to applications.
 *
 * This event is generated by the driver, with a blank event body.
 */
void
mwifiex_hs_activated_event(struct mwifiex_private *priv, u8 activated)
{
	if (activated) {
		if (priv->adapter->is_hs_configured) {
			priv->adapter->hs_activated = true;
			mwifiex_update_rxreor_flags(priv->adapter,
						    RXREOR_FORCE_NO_DROP);
			mwifiex_dbg(priv->adapter, EVENT,
				    "event: hs_activated\n");
			priv->adapter->hs_activate_wait_q_woken = true;
			wake_up_interruptible(
				&priv->adapter->hs_activate_wait_q);
		} else {
			mwifiex_dbg(priv->adapter, EVENT,
				    "event: HS not configured\n");
		}
	} else {
		mwifiex_dbg(priv->adapter, EVENT,
			    "event: hs_deactivated\n");
		priv->adapter->hs_activated = false;
	}
}

/*
 * This function handles the command response of a Host Sleep configuration
 * command.
 *
 * Handling includes changing the header fields into CPU format
 * and setting the current host sleep activation status in driver.
 *
 * In case host sleep status change, the function generates an event to
 * notify the applications.
 */
int mwifiex_ret_802_11_hs_cfg(struct mwifiex_private *priv,
			      struct host_cmd_ds_command *resp)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	struct host_cmd_ds_802_11_hs_cfg_enh *phs_cfg =
		&resp->params.opt_hs_cfg;
	uint32_t conditions = le32_to_cpu(phs_cfg->params.hs_config.conditions);

	if (phs_cfg->action == cpu_to_le16(HS_ACTIVATE) &&
	    adapter->iface_type != MWIFIEX_USB) {
		mwifiex_hs_activated_event(priv, true);
		return 0;
	} else {
		mwifiex_dbg(adapter, CMD,
			    "cmd: CMD_RESP: HS_CFG cmd reply\t"
			    " result=%#x, conditions=0x%x gpio=0x%x gap=0x%x\n",
			    resp->result, conditions,
			    phs_cfg->params.hs_config.gpio,
			    phs_cfg->params.hs_config.gap);
	}
	if (conditions != HS_CFG_CANCEL) {
		adapter->is_hs_configured = true;
		if (adapter->iface_type == MWIFIEX_USB)
			mwifiex_hs_activated_event(priv, true);
	} else {
		adapter->is_hs_configured = false;
		if (adapter->hs_activated)
			mwifiex_hs_activated_event(priv, false);
	}

	return 0;
}

/*
 * This function wakes up the adapter and generates a Host Sleep
 * cancel event on receiving the power up interrupt.
 */
void
mwifiex_process_hs_config(struct mwifiex_adapter *adapter)
{
	mwifiex_dbg(adapter, INFO,
		    "info: %s: auto cancelling host sleep\t"
		    "since there is interrupt from the firmware\n",
		    __func__);

	adapter->if_ops.wakeup(adapter);
	adapter->hs_activated = false;
	adapter->is_hs_configured = false;
	adapter->is_suspended = false;
	mwifiex_hs_activated_event(mwifiex_get_priv(adapter,
						    MWIFIEX_BSS_ROLE_ANY),
				   false);
}
EXPORT_SYMBOL_GPL(mwifiex_process_hs_config);

/*
 * This function handles the command response of a sleep confirm command.
 *
 * The function sets the card state to SLEEP if the response indicates success.
 */
void
mwifiex_process_sleep_confirm_resp(struct mwifiex_adapter *adapter,
				   u8 *pbuf, u32 upld_len)
{
	struct host_cmd_ds_command *cmd = (struct host_cmd_ds_command *) pbuf;
	struct mwifiex_private *priv =
		mwifiex_get_priv(adapter, MWIFIEX_BSS_ROLE_ANY);
	uint16_t result = le16_to_cpu(cmd->result);
	uint16_t command = le16_to_cpu(cmd->command);
	uint16_t seq_num = le16_to_cpu(cmd->seq_num);

	if (!upld_len) {
		mwifiex_dbg(adapter, ERROR,
			    "%s: cmd size is 0\n", __func__);
		return;
	}

	mwifiex_dbg(adapter, CMD,
		    "cmd: CMD_RESP: 0x%x, result %d, len %d, seqno 0x%x\n",
		    command, result, le16_to_cpu(cmd->size), seq_num);

	/* Get BSS number and corresponding priv */
	priv = mwifiex_get_priv_by_id(adapter, HostCmd_GET_BSS_NO(seq_num),
				      HostCmd_GET_BSS_TYPE(seq_num));
	if (!priv)
		priv = mwifiex_get_priv(adapter, MWIFIEX_BSS_ROLE_ANY);

	/* Update sequence number */
	seq_num = HostCmd_GET_SEQ_NO(seq_num);
	/* Clear RET_BIT from HostCmd */
	command &= HostCmd_CMD_ID_MASK;

	if (command != HostCmd_CMD_802_11_PS_MODE_ENH) {
		mwifiex_dbg(adapter, ERROR,
			    "%s: rcvd unexpected resp for cmd %#x, result = %x\n",
			    __func__, command, result);
		return;
	}

	if (result) {
		mwifiex_dbg(adapter, ERROR,
			    "%s: sleep confirm cmd failed\n",
			    __func__);
		adapter->pm_wakeup_card_req = false;
		adapter->ps_state = PS_STATE_AWAKE;
		return;
	}
	adapter->pm_wakeup_card_req = true;
	if (adapter->is_hs_configured)
		mwifiex_hs_activated_event(mwifiex_get_priv
						(adapter, MWIFIEX_BSS_ROLE_ANY),
					   true);
	adapter->ps_state = PS_STATE_SLEEP;
	cmd->command = cpu_to_le16(command);
	cmd->seq_num = cpu_to_le16(seq_num);
}
EXPORT_SYMBOL_GPL(mwifiex_process_sleep_confirm_resp);

/*
 * This function prepares an enhanced power mode command.
 *
 * This function can be used to disable power save or to configure
 * power save with auto PS or STA PS or auto deep sleep.
 *
 * Preparation includes -
 *      - Setting command ID, action and proper size
 *      - Setting Power Save bitmap, PS parameters TLV, PS mode TLV,
 *        auto deep sleep TLV (as required)
 *      - Ensuring correct endian-ness
 */
int mwifiex_cmd_enh_power_mode(struct mwifiex_private *priv,
			       struct host_cmd_ds_command *cmd,
			       u16 cmd_action, uint16_t ps_bitmap,
			       struct mwifiex_ds_auto_ds *auto_ds)
{
	struct host_cmd_ds_802_11_ps_mode_enh *psmode_enh =
		&cmd->params.psmode_enh;
	u8 *tlv;
	u16 cmd_size = 0;

	cmd->command = cpu_to_le16(HostCmd_CMD_802_11_PS_MODE_ENH);
	if (cmd_action == DIS_AUTO_PS) {
		psmode_enh->action = cpu_to_le16(DIS_AUTO_PS);
		psmode_enh->params.ps_bitmap = cpu_to_le16(ps_bitmap);
		cmd->size = cpu_to_le16(S_DS_GEN + sizeof(psmode_enh->action) +
					sizeof(psmode_enh->params.ps_bitmap));
	} else if (cmd_action == GET_PS) {
		psmode_enh->action = cpu_to_le16(GET_PS);
		psmode_enh->params.ps_bitmap = cpu_to_le16(ps_bitmap);
		cmd->size = cpu_to_le16(S_DS_GEN + sizeof(psmode_enh->action) +
					sizeof(psmode_enh->params.ps_bitmap));
	} else if (cmd_action == EN_AUTO_PS) {
		psmode_enh->action = cpu_to_le16(EN_AUTO_PS);
		psmode_enh->params.ps_bitmap = cpu_to_le16(ps_bitmap);
		cmd_size = S_DS_GEN + sizeof(psmode_enh->action) +
					sizeof(psmode_enh->params.ps_bitmap);
		tlv = (u8 *) cmd + cmd_size;
		if (ps_bitmap & BITMAP_STA_PS) {
			struct mwifiex_adapter *adapter = priv->adapter;
			struct mwifiex_ie_types_ps_param *ps_tlv =
				(struct mwifiex_ie_types_ps_param *) tlv;
			struct mwifiex_ps_param *ps_mode = &ps_tlv->param;
			ps_tlv->header.type = cpu_to_le16(TLV_TYPE_PS_PARAM);
			ps_tlv->header.len = cpu_to_le16(sizeof(*ps_tlv) -
					sizeof(struct mwifiex_ie_types_header));
			cmd_size += sizeof(*ps_tlv);
			tlv += sizeof(*ps_tlv);
			mwifiex_dbg(priv->adapter, CMD,
				    "cmd: PS Command: Enter PS\n");
			ps_mode->null_pkt_interval =
					cpu_to_le16(adapter->null_pkt_interval);
			ps_mode->multiple_dtims =
					cpu_to_le16(adapter->multiple_dtim);
			ps_mode->bcn_miss_timeout =
					cpu_to_le16(adapter->bcn_miss_time_out);
			ps_mode->local_listen_interval =
				cpu_to_le16(adapter->local_listen_interval);
			ps_mode->adhoc_wake_period =
				cpu_to_le16(adapter->adhoc_awake_period);
			ps_mode->delay_to_ps =
					cpu_to_le16(adapter->delay_to_ps);
			ps_mode->mode = cpu_to_le16(adapter->enhanced_ps_mode);

		}
		if (ps_bitmap & BITMAP_AUTO_DS) {
			struct mwifiex_ie_types_auto_ds_param *auto_ds_tlv =
				(struct mwifiex_ie_types_auto_ds_param *) tlv;
			u16 idletime = 0;

			auto_ds_tlv->header.type =
				cpu_to_le16(TLV_TYPE_AUTO_DS_PARAM);
			auto_ds_tlv->header.len =
				cpu_to_le16(sizeof(*auto_ds_tlv) -
					sizeof(struct mwifiex_ie_types_header));
			cmd_size += sizeof(*auto_ds_tlv);
			tlv += sizeof(*auto_ds_tlv);
			if (auto_ds)
				idletime = auto_ds->idle_time;
			mwifiex_dbg(priv->adapter, CMD,
				    "cmd: PS Command: Enter Auto Deep Sleep\n");
			auto_ds_tlv->deep_sleep_timeout = cpu_to_le16(idletime);
		}
		cmd->size = cpu_to_le16(cmd_size);
	}
	return 0;
}

/*
 * This function handles the command response of an enhanced power mode
 * command.
 *
 * Handling includes changing the header fields into CPU format
 * and setting the current enhanced power mode in driver.
 */
int mwifiex_ret_enh_power_mode(struct mwifiex_private *priv,
			       struct host_cmd_ds_command *resp,
			       struct mwifiex_ds_pm_cfg *pm_cfg)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	struct host_cmd_ds_802_11_ps_mode_enh *ps_mode =
		&resp->params.psmode_enh;
	uint16_t action = le16_to_cpu(ps_mode->action);
	uint16_t ps_bitmap = le16_to_cpu(ps_mode->params.ps_bitmap);
	uint16_t auto_ps_bitmap =
		le16_to_cpu(ps_mode->params.ps_bitmap);

	mwifiex_dbg(adapter, INFO,
		    "info: %s: PS_MODE cmd reply result=%#x action=%#X\n",
		    __func__, resp->result, action);
	if (action == EN_AUTO_PS) {
		if (auto_ps_bitmap & BITMAP_AUTO_DS) {
			mwifiex_dbg(adapter, CMD,
				    "cmd: Enabled auto deep sleep\n");
			priv->adapter->is_deep_sleep = true;
		}
		if (auto_ps_bitmap & BITMAP_STA_PS) {
			mwifiex_dbg(adapter, CMD,
				    "cmd: Enabled STA power save\n");
			if (adapter->sleep_period.period)
				mwifiex_dbg(adapter, CMD,
					    "cmd: set to uapsd/pps mode\n");
		}
	} else if (action == DIS_AUTO_PS) {
		if (ps_bitmap & BITMAP_AUTO_DS) {
			priv->adapter->is_deep_sleep = false;
			mwifiex_dbg(adapter, CMD,
				    "cmd: Disabled auto deep sleep\n");
		}
		if (ps_bitmap & BITMAP_STA_PS) {
			mwifiex_dbg(adapter, CMD,
				    "cmd: Disabled STA power save\n");
			if (adapter->sleep_period.period) {
				adapter->delay_null_pkt = false;
				adapter->tx_lock_flag = false;
				adapter->pps_uapsd_mode = false;
			}
		}
	} else if (action == GET_PS) {
		if (ps_bitmap & BITMAP_STA_PS)
			adapter->ps_mode = MWIFIEX_802_11_POWER_MODE_PSP;
		else
			adapter->ps_mode = MWIFIEX_802_11_POWER_MODE_CAM;

		mwifiex_dbg(adapter, CMD,
			    "cmd: ps_bitmap=%#x\n", ps_bitmap);

		if (pm_cfg) {
			/* This section is for get power save mode */
			if (ps_bitmap & BITMAP_STA_PS)
				pm_cfg->param.ps_mode = 1;
			else
				pm_cfg->param.ps_mode = 0;
		}
	}
	return 0;
}

/*
 * This function prepares command to get hardware specifications.
 *
 * Preparation includes -
 *      - Setting command ID, action and proper size
 *      - Setting permanent address parameter
 *      - Ensuring correct endian-ness
 */
int mwifiex_cmd_get_hw_spec(struct mwifiex_private *priv,
			    struct host_cmd_ds_command *cmd)
{
	struct host_cmd_ds_get_hw_spec *hw_spec = &cmd->params.hw_spec;

	cmd->command = cpu_to_le16(HostCmd_CMD_GET_HW_SPEC);
	cmd->size =
		cpu_to_le16(sizeof(struct host_cmd_ds_get_hw_spec) + S_DS_GEN);
	memcpy(hw_spec->permanent_addr, priv->curr_addr, ETH_ALEN);

	return 0;
}

/*
 * This function handles the command response of get hardware
 * specifications.
 *
 * Handling includes changing the header fields into CPU format
 * and saving/updating the following parameters in driver -
 *      - Firmware capability information
 *      - Firmware band settings
 *      - Ad-hoc start band and channel
 *      - Ad-hoc 11n activation status
 *      - Firmware release number
 *      - Number of antennas
 *      - Hardware address
 *      - Hardware interface version
 *      - Firmware version
 *      - Region code
 *      - 11n capabilities
 *      - MCS support fields
 *      - MP end port
 */
int mwifiex_ret_get_hw_spec(struct mwifiex_private *priv,
			    struct host_cmd_ds_command *resp)
{
	struct host_cmd_ds_get_hw_spec *hw_spec = &resp->params.hw_spec;
	struct mwifiex_adapter *adapter = priv->adapter;
	struct mwifiex_ie_types_header *tlv;
	struct hw_spec_api_rev *api_rev;
	u16 resp_size, api_id;
	int i, left_len, parsed_len = 0;

	adapter->fw_cap_info = le32_to_cpu(hw_spec->fw_cap_info);

	if (IS_SUPPORT_MULTI_BANDS(adapter))
		adapter->fw_bands = (u8) GET_FW_DEFAULT_BANDS(adapter);
	else
		adapter->fw_bands = BAND_B;

	adapter->config_bands = adapter->fw_bands;

	if (adapter->fw_bands & BAND_A) {
		if (adapter->fw_bands & BAND_GN) {
			adapter->config_bands |= BAND_AN;
			adapter->fw_bands |= BAND_AN;
		}
		if (adapter->fw_bands & BAND_AN) {
			adapter->adhoc_start_band = BAND_A | BAND_AN;
			adapter->adhoc_11n_enabled = true;
		} else {
			adapter->adhoc_start_band = BAND_A;
		}
		priv->adhoc_channel = DEFAULT_AD_HOC_CHANNEL_A;
	} else if (adapter->fw_bands & BAND_GN) {
		adapter->adhoc_start_band = BAND_G | BAND_B | BAND_GN;
		priv->adhoc_channel = DEFAULT_AD_HOC_CHANNEL;
		adapter->adhoc_11n_enabled = true;
	} else if (adapter->fw_bands & BAND_G) {
		adapter->adhoc_start_band = BAND_G | BAND_B;
		priv->adhoc_channel = DEFAULT_AD_HOC_CHANNEL;
	} else if (adapter->fw_bands & BAND_B) {
		adapter->adhoc_start_band = BAND_B;
		priv->adhoc_channel = DEFAULT_AD_HOC_CHANNEL;
	}

	adapter->fw_release_number = le32_to_cpu(hw_spec->fw_release_number);
	adapter->fw_api_ver = (adapter->fw_release_number >> 16) & 0xff;
	adapter->number_of_antenna = le16_to_cpu(hw_spec->number_of_antenna);

	if (le32_to_cpu(hw_spec->dot_11ac_dev_cap)) {
		adapter->is_hw_11ac_capable = true;

		/* Copy 11AC cap */
		adapter->hw_dot_11ac_dev_cap =
					le32_to_cpu(hw_spec->dot_11ac_dev_cap);
		adapter->usr_dot_11ac_dev_cap_bg = adapter->hw_dot_11ac_dev_cap
					& ~MWIFIEX_DEF_11AC_CAP_BF_RESET_MASK;
		adapter->usr_dot_11ac_dev_cap_a = adapter->hw_dot_11ac_dev_cap
					& ~MWIFIEX_DEF_11AC_CAP_BF_RESET_MASK;

		/* Copy 11AC mcs */
		adapter->hw_dot_11ac_mcs_support =
				le32_to_cpu(hw_spec->dot_11ac_mcs_support);
		adapter->usr_dot_11ac_mcs_support =
					adapter->hw_dot_11ac_mcs_support;
	} else {
		adapter->is_hw_11ac_capable = false;
	}

	resp_size = le16_to_cpu(resp->size) - S_DS_GEN;
	if (resp_size > sizeof(struct host_cmd_ds_get_hw_spec)) {
		/* we have variable HW SPEC information */
		left_len = resp_size - sizeof(struct host_cmd_ds_get_hw_spec);
		while (left_len > sizeof(struct mwifiex_ie_types_header)) {
			tlv = (void *)&hw_spec->tlvs + parsed_len;
			switch (le16_to_cpu(tlv->type)) {
			case TLV_TYPE_API_REV:
				api_rev = (struct hw_spec_api_rev *)tlv;
				api_id = le16_to_cpu(api_rev->api_id);
				switch (api_id) {
				case KEY_API_VER_ID:
					adapter->key_api_major_ver =
							api_rev->major_ver;
					adapter->key_api_minor_ver =
							api_rev->minor_ver;
					mwifiex_dbg(adapter, INFO,
						    "key_api v%d.%d\n",
						    adapter->key_api_major_ver,
						    adapter->key_api_minor_ver);
					break;
				case FW_API_VER_ID:
					adapter->fw_api_ver =
							api_rev->major_ver;
					mwifiex_dbg(adapter, INFO,
						    "Firmware api version %d\n",
						    adapter->fw_api_ver);
					break;
				default:
					mwifiex_dbg(adapter, FATAL,
						    "Unknown api_id: %d\n",
						    api_id);
					break;
				}
				break;
			default:
				mwifiex_dbg(adapter, FATAL,
					    "Unknown GET_HW_SPEC TLV type: %#x\n",
					    le16_to_cpu(tlv->type));
				break;
			}
			parsed_len += le16_to_cpu(tlv->len) +
				      sizeof(struct mwifiex_ie_types_header);
			left_len -= le16_to_cpu(tlv->len) +
				      sizeof(struct mwifiex_ie_types_header);
		}
	}

	mwifiex_dbg(adapter, INFO,
		    "info: GET_HW_SPEC: fw_release_number- %#x\n",
		    adapter->fw_release_number);
	mwifiex_dbg(adapter, INFO,
		    "info: GET_HW_SPEC: permanent addr: %pM\n",
		    hw_spec->permanent_addr);
	mwifiex_dbg(adapter, INFO,
		    "info: GET_HW_SPEC: hw_if_version=%#x version=%#x\n",
		    le16_to_cpu(hw_spec->hw_if_version),
		    le16_to_cpu(hw_spec->version));

	ether_addr_copy(priv->adapter->perm_addr, hw_spec->permanent_addr);
	adapter->region_code = le16_to_cpu(hw_spec->region_code);

	for (i = 0; i < MWIFIEX_MAX_REGION_CODE; i++)
		/* Use the region code to search for the index */
		if (adapter->region_code == region_code_index[i])
			break;

	/* If it's unidentified region code, use the default (USA) */
	if (i >= MWIFIEX_MAX_REGION_CODE) {
		adapter->region_code = 0x10;
		mwifiex_dbg(adapter, WARN,
			    "cmd: unknown region code, use default (USA)\n");
	}

	adapter->hw_dot_11n_dev_cap = le32_to_cpu(hw_spec->dot_11n_dev_cap);
	adapter->hw_dev_mcs_support = hw_spec->dev_mcs_support;
	adapter->user_dev_mcs_support = adapter->hw_dev_mcs_support;

	if (adapter->if_ops.update_mp_end_port)
		adapter->if_ops.update_mp_end_port(adapter,
					le16_to_cpu(hw_spec->mp_end_port));

	if (adapter->fw_api_ver == MWIFIEX_FW_V15)
		adapter->scan_chan_gap_enabled = true;

	return 0;
}
