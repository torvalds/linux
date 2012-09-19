/*
 * Marvell Wireless LAN device driver: commands and events
 *
 * Copyright (C) 2011, Marvell International Ltd.
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
		      u32 cmd_oid, void *data_buf)
{
	cmd_node->priv = priv;
	cmd_node->cmd_oid = cmd_oid;
	if (priv->adapter->cmd_wait_q_required) {
		cmd_node->wait_q_enabled = priv->adapter->cmd_wait_q_required;
		priv->adapter->cmd_wait_q_required = false;
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
		dev_err(adapter->dev, "GET_CMD_NODE: cmd node not available\n");
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
	dev_dbg(priv->adapter->dev, "cmd: host cmd size = %d\n", pcmd_ptr->len);
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
	struct timeval tstamp;
	unsigned long flags;
	__le32 tmp;

	if (!adapter || !cmd_node)
		return -1;

	host_cmd = (struct host_cmd_ds_command *) (cmd_node->cmd_skb->data);

	/* Sanity test */
	if (host_cmd == NULL || host_cmd->size == 0) {
		dev_err(adapter->dev, "DNLD_CMD: host_cmd is null"
			" or cmd size is 0, not sending\n");
		if (cmd_node->wait_q_enabled)
			adapter->cmd_wait_q.status = -1;
		mwifiex_insert_cmd_to_free_q(adapter, cmd_node);
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

	cmd_code = le16_to_cpu(host_cmd->command);
	cmd_size = le16_to_cpu(host_cmd->size);

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

	do_gettimeofday(&tstamp);
	dev_dbg(adapter->dev, "cmd: DNLD_CMD: (%lu.%lu): %#x, act %#x, len %d,"
		" seqno %#x\n",
		tstamp.tv_sec, tstamp.tv_usec, cmd_code,
		le16_to_cpu(*(__le16 *) ((u8 *) host_cmd + S_DS_GEN)), cmd_size,
		le16_to_cpu(host_cmd->seq_num));

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
		dev_err(adapter->dev, "DNLD_CMD: host to card failed\n");
		if (adapter->iface_type == MWIFIEX_USB)
			adapter->cmd_sent = false;
		if (cmd_node->wait_q_enabled)
			adapter->cmd_wait_q.status = -1;
		mwifiex_insert_cmd_to_free_q(adapter, adapter->curr_cmd);

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
		  jiffies + (MWIFIEX_TIMER_10S * HZ) / 1000);

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

	sleep_cfm_buf->seq_num =
		cpu_to_le16((HostCmd_SET_SEQ_NO_BSS_INFO
					(adapter->seq_num, priv->bss_num,
					 priv->bss_type)));
	adapter->seq_num++;

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
		dev_err(adapter->dev, "SLEEP_CFM: failed\n");
		adapter->dbg.num_cmd_sleep_cfm_host_to_card_failure++;
		return -1;
	}
	if (GET_BSS_ROLE(mwifiex_get_priv(adapter, MWIFIEX_BSS_ROLE_ANY))
	    == MWIFIEX_BSS_ROLE_STA) {
		if (!sleep_cfm_buf->resp_ctrl)
			/* Response is not needed for sleep
			   confirm command */
			adapter->ps_state = PS_STATE_SLEEP;
		else
			adapter->ps_state = PS_STATE_SLEEP_CFM;

		if (!sleep_cfm_buf->resp_ctrl &&
		    (adapter->is_hs_configured &&
		     !adapter->sleep_period.period)) {
			adapter->pm_wakeup_card_req = true;
			mwifiex_hs_activated_event(mwifiex_get_priv
					(adapter, MWIFIEX_BSS_ROLE_STA), true);
		}
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
	u32 buf_size;
	u32 i;

	/* Allocate and initialize struct cmd_ctrl_node */
	buf_size = sizeof(struct cmd_ctrl_node) * MWIFIEX_NUM_OF_CMD_BUFFER;
	cmd_array = kzalloc(buf_size, GFP_KERNEL);
	if (!cmd_array) {
		dev_err(adapter->dev, "%s: failed to alloc cmd_array\n",
			__func__);
		return -ENOMEM;
	}

	adapter->cmd_pool = cmd_array;
	memset(adapter->cmd_pool, 0, buf_size);

	/* Allocate and initialize command buffers */
	for (i = 0; i < MWIFIEX_NUM_OF_CMD_BUFFER; i++) {
		cmd_array[i].skb = dev_alloc_skb(MWIFIEX_SIZE_OF_CMD_BUFFER);
		if (!cmd_array[i].skb) {
			dev_err(adapter->dev, "ALLOC_CMD_BUF: out of memory\n");
			return -1;
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
		dev_dbg(adapter->dev, "info: FREE_CMD_BUF: cmd_pool is null\n");
		return 0;
	}

	cmd_array = adapter->cmd_pool;

	/* Release shared memory buffers */
	for (i = 0; i < MWIFIEX_NUM_OF_CMD_BUFFER; i++) {
		if (cmd_array[i].skb) {
			dev_dbg(adapter->dev, "cmd: free cmd buffer %d\n", i);
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
		dev_dbg(adapter->dev, "cmd: free cmd pool\n");
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
	struct timeval tstamp;
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
		rx_info->bss_num = priv->bss_num;
		rx_info->bss_type = priv->bss_type;
	}

	if (eventcause != EVENT_PS_SLEEP && eventcause != EVENT_PS_AWAKE) {
		do_gettimeofday(&tstamp);
		dev_dbg(adapter->dev, "event: %lu.%lu: cause: %#x\n",
			tstamp.tv_sec, tstamp.tv_usec, eventcause);
	} else {
		/* Handle PS_SLEEP/AWAKE events on STA */
		priv = mwifiex_get_priv(adapter, MWIFIEX_BSS_ROLE_STA);
		if (!priv)
			priv = mwifiex_get_priv(adapter, MWIFIEX_BSS_ROLE_ANY);
	}

	ret = mwifiex_process_sta_event(priv);

	adapter->event_cause = 0;
	adapter->event_skb = NULL;
	adapter->if_ops.event_complete(adapter, skb);

	return ret;
}

/*
 * This function is used to send synchronous command to the firmware.
 *
 * it allocates a wait queue for the command and wait for the command
 * response.
 */
int mwifiex_send_cmd_sync(struct mwifiex_private *priv, uint16_t cmd_no,
			  u16 cmd_action, u32 cmd_oid, void *data_buf)
{
	int ret = 0;
	struct mwifiex_adapter *adapter = priv->adapter;

	adapter->cmd_wait_q_required = true;

	ret = mwifiex_send_cmd_async(priv, cmd_no, cmd_action, cmd_oid,
				     data_buf);
	if (!ret)
		ret = mwifiex_wait_queue_complete(adapter);

	return ret;
}


/*
 * This function prepares a command and asynchronously send it to the firmware.
 *
 * Preparation includes -
 *      - Sanity tests to make sure the card is still present or the FW
 *        is not reset
 *      - Getting a new command node from the command free queue
 *      - Initializing the command node for default parameters
 *      - Fill up the non-default parameters and buffer pointers
 *      - Add the command to pending queue
 */
int mwifiex_send_cmd_async(struct mwifiex_private *priv, uint16_t cmd_no,
			   u16 cmd_action, u32 cmd_oid, void *data_buf)
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
		dev_err(adapter->dev, "PREP_CMD: device in suspended state\n");
		return -1;
	}

	if (adapter->surprise_removed) {
		dev_err(adapter->dev, "PREP_CMD: card is removed\n");
		return -1;
	}

	if (adapter->hw_status == MWIFIEX_HW_STATUS_RESET) {
		if (cmd_no != HostCmd_CMD_FUNC_INIT) {
			dev_err(adapter->dev, "PREP_CMD: FW in reset state\n");
			return -1;
		}
	}

	/* Get a new command node */
	cmd_node = mwifiex_get_cmd_node(adapter);

	if (!cmd_node) {
		dev_err(adapter->dev, "PREP_CMD: no free cmd node\n");
		return -1;
	}

	/* Initialize the command node */
	mwifiex_init_cmd_node(priv, cmd_node, cmd_oid, data_buf);

	if (!cmd_node->cmd_skb) {
		dev_err(adapter->dev, "PREP_CMD: no free cmd buf\n");
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
		dev_err(adapter->dev, "PREP_CMD: cmd %#x preparation failed\n",
			cmd_no);
		mwifiex_insert_cmd_to_free_q(adapter, cmd_node);
		return -1;
	}

	/* Send command */
	if (cmd_no == HostCmd_CMD_802_11_SCAN) {
		mwifiex_queue_scan_cmd(priv, cmd_node);
	} else {
		adapter->cmd_queued = cmd_node;
		mwifiex_insert_cmd_to_pending_q(adapter, cmd_node, true);
		queue_work(adapter->workqueue, &adapter->main_work);
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
		dev_err(adapter->dev, "QUEUE_CMD: host_cmd is NULL\n");
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

	dev_dbg(adapter->dev, "cmd: QUEUE_CMD: cmd=%#x is queued\n", command);
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
		dev_err(adapter->dev, "EXEC_NEXT_CMD: cmd in processing\n");
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
		dev_err(adapter->dev, "%s: cannot send cmd in sleep state,"
				" this should not happen\n", __func__);
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
	struct timeval tstamp;
	unsigned long flags;

	/* Now we got response from FW, cancel the command timer */
	del_timer(&adapter->cmd_timer);

	if (!adapter->curr_cmd || !adapter->curr_cmd->resp_skb) {
		resp = (struct host_cmd_ds_command *) adapter->upld_buf;
		dev_err(adapter->dev, "CMD_RESP: NULL curr_cmd, %#x\n",
			le16_to_cpu(resp->command));
		return -1;
	}

	adapter->num_cmd_timeout = 0;

	resp = (struct host_cmd_ds_command *) adapter->curr_cmd->resp_skb->data;
	if (adapter->curr_cmd->cmd_flag & CMD_F_CANCELED) {
		dev_err(adapter->dev, "CMD_RESP: %#x been canceled\n",
			le16_to_cpu(resp->command));
		mwifiex_insert_cmd_to_free_q(adapter, adapter->curr_cmd);
		spin_lock_irqsave(&adapter->mwifiex_cmd_lock, flags);
		adapter->curr_cmd = NULL;
		spin_unlock_irqrestore(&adapter->mwifiex_cmd_lock, flags);
		return -1;
	}

	if (adapter->curr_cmd->cmd_flag & CMD_F_HOSTCMD) {
		/* Copy original response back to response buffer */
		struct mwifiex_ds_misc_cmd *hostcmd;
		uint16_t size = le16_to_cpu(resp->size);
		dev_dbg(adapter->dev, "info: host cmd resp size = %d\n", size);
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

	do_gettimeofday(&tstamp);
	dev_dbg(adapter->dev, "cmd: CMD_RESP: (%lu.%lu): 0x%x, result %d,"
		" len %d, seqno 0x%x\n",
	       tstamp.tv_sec, tstamp.tv_usec, orig_cmdresp_no, cmdresp_result,
	       le16_to_cpu(resp->size), le16_to_cpu(resp->seq_num));

	if (!(orig_cmdresp_no & HostCmd_RET_BIT)) {
		dev_err(adapter->dev, "CMD_RESP: invalid cmd resp\n");
		if (adapter->curr_cmd->wait_q_enabled)
			adapter->cmd_wait_q.status = -1;

		mwifiex_insert_cmd_to_free_q(adapter, adapter->curr_cmd);
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
			dev_err(adapter->dev, "%s: cmd %#x failed during "
				"initialization\n", __func__, cmdresp_no);
			mwifiex_init_fw_complete(adapter);
			return -1;
		} else if (adapter->last_init_cmd == cmdresp_no)
			adapter->hw_status = MWIFIEX_HW_STATUS_INIT_DONE;
	}

	if (adapter->curr_cmd) {
		if (adapter->curr_cmd->wait_q_enabled)
			adapter->cmd_wait_q.status = ret;

		/* Clean up and put current command back to cmd_free_q */
		mwifiex_insert_cmd_to_free_q(adapter, adapter->curr_cmd);

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
	struct timeval tstamp;

	adapter->num_cmd_timeout++;
	adapter->dbg.num_cmd_timeout++;
	if (!adapter->curr_cmd) {
		dev_dbg(adapter->dev, "cmd: empty curr_cmd\n");
		return;
	}
	cmd_node = adapter->curr_cmd;
	if (cmd_node->wait_q_enabled)
		adapter->cmd_wait_q.status = -ETIMEDOUT;

	if (cmd_node) {
		adapter->dbg.timeout_cmd_id =
			adapter->dbg.last_cmd_id[adapter->dbg.last_cmd_index];
		adapter->dbg.timeout_cmd_act =
			adapter->dbg.last_cmd_act[adapter->dbg.last_cmd_index];
		do_gettimeofday(&tstamp);
		dev_err(adapter->dev,
			"%s: Timeout cmd id (%lu.%lu) = %#x, act = %#x\n",
			__func__, tstamp.tv_sec, tstamp.tv_usec,
			adapter->dbg.timeout_cmd_id,
			adapter->dbg.timeout_cmd_act);

		dev_err(adapter->dev, "num_data_h2c_failure = %d\n",
			adapter->dbg.num_tx_host_to_card_failure);
		dev_err(adapter->dev, "num_cmd_h2c_failure = %d\n",
			adapter->dbg.num_cmd_host_to_card_failure);

		dev_err(adapter->dev, "num_cmd_timeout = %d\n",
			adapter->dbg.num_cmd_timeout);
		dev_err(adapter->dev, "num_tx_timeout = %d\n",
			adapter->dbg.num_tx_timeout);

		dev_err(adapter->dev, "last_cmd_index = %d\n",
			adapter->dbg.last_cmd_index);
		print_hex_dump_bytes("last_cmd_id: ", DUMP_PREFIX_OFFSET,
				     adapter->dbg.last_cmd_id, DBG_CMD_NUM);
		print_hex_dump_bytes("last_cmd_act: ", DUMP_PREFIX_OFFSET,
				     adapter->dbg.last_cmd_act, DBG_CMD_NUM);

		dev_err(adapter->dev, "last_cmd_resp_index = %d\n",
			adapter->dbg.last_cmd_resp_index);
		print_hex_dump_bytes("last_cmd_resp_id: ", DUMP_PREFIX_OFFSET,
				     adapter->dbg.last_cmd_resp_id,
				     DBG_CMD_NUM);

		dev_err(adapter->dev, "last_event_index = %d\n",
			adapter->dbg.last_event_index);
		print_hex_dump_bytes("last_event: ", DUMP_PREFIX_OFFSET,
				     adapter->dbg.last_event, DBG_CMD_NUM);

		dev_err(adapter->dev, "data_sent=%d cmd_sent=%d\n",
			adapter->data_sent, adapter->cmd_sent);

		dev_err(adapter->dev, "ps_mode=%d ps_state=%d\n",
			adapter->ps_mode, adapter->ps_state);
	}
	if (adapter->hw_status == MWIFIEX_HW_STATUS_INITIALIZING)
		mwifiex_init_fw_complete(adapter);
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
	unsigned long flags;

	/* Cancel current cmd */
	if ((adapter->curr_cmd) && (adapter->curr_cmd->wait_q_enabled)) {
		spin_lock_irqsave(&adapter->mwifiex_cmd_lock, flags);
		adapter->curr_cmd->wait_q_enabled = false;
		spin_unlock_irqrestore(&adapter->mwifiex_cmd_lock, flags);
		adapter->cmd_wait_q.status = -1;
		mwifiex_complete_cmd(adapter, adapter->curr_cmd);
	}
	/* Cancel all pending command */
	spin_lock_irqsave(&adapter->cmd_pending_q_lock, flags);
	list_for_each_entry_safe(cmd_node, tmp_node,
				 &adapter->cmd_pending_q, list) {
		list_del(&cmd_node->list);
		spin_unlock_irqrestore(&adapter->cmd_pending_q_lock, flags);

		if (cmd_node->wait_q_enabled) {
			adapter->cmd_wait_q.status = -1;
			mwifiex_complete_cmd(adapter, cmd_node);
			cmd_node->wait_q_enabled = false;
		}
		mwifiex_insert_cmd_to_free_q(adapter, cmd_node);
		spin_lock_irqsave(&adapter->cmd_pending_q_lock, flags);
	}
	spin_unlock_irqrestore(&adapter->cmd_pending_q_lock, flags);

	/* Cancel all pending scan command */
	spin_lock_irqsave(&adapter->scan_pending_q_lock, flags);
	list_for_each_entry_safe(cmd_node, tmp_node,
				 &adapter->scan_pending_q, list) {
		list_del(&cmd_node->list);
		spin_unlock_irqrestore(&adapter->scan_pending_q_lock, flags);

		cmd_node->wait_q_enabled = false;
		mwifiex_insert_cmd_to_free_q(adapter, cmd_node);
		spin_lock_irqsave(&adapter->scan_pending_q_lock, flags);
	}
	spin_unlock_irqrestore(&adapter->scan_pending_q_lock, flags);

	spin_lock_irqsave(&adapter->mwifiex_cmd_lock, flags);
	adapter->scan_processing = false;
	spin_unlock_irqrestore(&adapter->mwifiex_cmd_lock, flags);
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
	uint16_t cancel_scan_cmd = false;

	if ((adapter->curr_cmd) &&
	    (adapter->curr_cmd->wait_q_enabled)) {
		spin_lock_irqsave(&adapter->mwifiex_cmd_lock, cmd_flags);
		cmd_node = adapter->curr_cmd;
		cmd_node->wait_q_enabled = false;
		cmd_node->cmd_flag |= CMD_F_CANCELED;
		mwifiex_insert_cmd_to_free_q(adapter, cmd_node);
		mwifiex_complete_cmd(adapter, adapter->curr_cmd);
		adapter->curr_cmd = NULL;
		spin_unlock_irqrestore(&adapter->mwifiex_cmd_lock, cmd_flags);
	}

	/* Cancel all pending scan command */
	spin_lock_irqsave(&adapter->scan_pending_q_lock,
			  scan_pending_q_flags);
	list_for_each_entry_safe(cmd_node, tmp_node,
				 &adapter->scan_pending_q, list) {
		list_del(&cmd_node->list);
		spin_unlock_irqrestore(&adapter->scan_pending_q_lock,
				       scan_pending_q_flags);
		cmd_node->wait_q_enabled = false;
		mwifiex_insert_cmd_to_free_q(adapter, cmd_node);
		spin_lock_irqsave(&adapter->scan_pending_q_lock,
				  scan_pending_q_flags);
		cancel_scan_cmd = true;
	}
	spin_unlock_irqrestore(&adapter->scan_pending_q_lock,
			       scan_pending_q_flags);

	if (cancel_scan_cmd) {
		spin_lock_irqsave(&adapter->mwifiex_cmd_lock, cmd_flags);
		adapter->scan_processing = false;
		spin_unlock_irqrestore(&adapter->mwifiex_cmd_lock, cmd_flags);
	}
	adapter->cmd_wait_q.status = -1;
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
		dev_dbg(adapter->dev,
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
			dev_dbg(priv->adapter->dev, "event: hs_activated\n");
			priv->adapter->hs_activate_wait_q_woken = true;
			wake_up_interruptible(
				&priv->adapter->hs_activate_wait_q);
		} else {
			dev_dbg(priv->adapter->dev, "event: HS not configured\n");
		}
	} else {
		dev_dbg(priv->adapter->dev, "event: hs_deactivated\n");
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
	    adapter->iface_type == MWIFIEX_SDIO) {
		mwifiex_hs_activated_event(priv, true);
		return 0;
	} else {
		dev_dbg(adapter->dev, "cmd: CMD_RESP: HS_CFG cmd reply"
			" result=%#x, conditions=0x%x gpio=0x%x gap=0x%x\n",
			resp->result, conditions,
			phs_cfg->params.hs_config.gpio,
			phs_cfg->params.hs_config.gap);
	}
	if (conditions != HOST_SLEEP_CFG_CANCEL) {
		adapter->is_hs_configured = true;
		if (adapter->iface_type == MWIFIEX_USB ||
		    adapter->iface_type == MWIFIEX_PCIE)
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
	dev_dbg(adapter->dev, "info: %s: auto cancelling host sleep"
		" since there is interrupt from the firmware\n", __func__);

	adapter->if_ops.wakeup(adapter);
	adapter->hs_activated = false;
	adapter->is_hs_configured = false;
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
		dev_err(adapter->dev, "%s: cmd size is 0\n", __func__);
		return;
	}

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
		dev_err(adapter->dev,
			"%s: rcvd unexpected resp for cmd %#x, result = %x\n",
			__func__, command, result);
		return;
	}

	if (result) {
		dev_err(adapter->dev, "%s: sleep confirm cmd failed\n",
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
			dev_dbg(adapter->dev, "cmd: PS Command: Enter PS\n");
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
			dev_dbg(priv->adapter->dev,
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

	dev_dbg(adapter->dev,
		"info: %s: PS_MODE cmd reply result=%#x action=%#X\n",
		__func__, resp->result, action);
	if (action == EN_AUTO_PS) {
		if (auto_ps_bitmap & BITMAP_AUTO_DS) {
			dev_dbg(adapter->dev, "cmd: Enabled auto deep sleep\n");
			priv->adapter->is_deep_sleep = true;
		}
		if (auto_ps_bitmap & BITMAP_STA_PS) {
			dev_dbg(adapter->dev, "cmd: Enabled STA power save\n");
			if (adapter->sleep_period.period)
				dev_dbg(adapter->dev,
					"cmd: set to uapsd/pps mode\n");
		}
	} else if (action == DIS_AUTO_PS) {
		if (ps_bitmap & BITMAP_AUTO_DS) {
			priv->adapter->is_deep_sleep = false;
			dev_dbg(adapter->dev, "cmd: Disabled auto deep sleep\n");
		}
		if (ps_bitmap & BITMAP_STA_PS) {
			dev_dbg(adapter->dev, "cmd: Disabled STA power save\n");
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

		dev_dbg(adapter->dev, "cmd: ps_bitmap=%#x\n", ps_bitmap);

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
	int i;

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
	adapter->number_of_antenna = le16_to_cpu(hw_spec->number_of_antenna);

	dev_dbg(adapter->dev, "info: GET_HW_SPEC: fw_release_number- %#x\n",
		adapter->fw_release_number);
	dev_dbg(adapter->dev, "info: GET_HW_SPEC: permanent addr: %pM\n",
		hw_spec->permanent_addr);
	dev_dbg(adapter->dev,
		"info: GET_HW_SPEC: hw_if_version=%#x version=%#x\n",
		le16_to_cpu(hw_spec->hw_if_version),
		le16_to_cpu(hw_spec->version));

	if (priv->curr_addr[0] == 0xff)
		memmove(priv->curr_addr, hw_spec->permanent_addr, ETH_ALEN);

	adapter->region_code = le16_to_cpu(hw_spec->region_code);

	for (i = 0; i < MWIFIEX_MAX_REGION_CODE; i++)
		/* Use the region code to search for the index */
		if (adapter->region_code == region_code_index[i])
			break;

	/* If it's unidentified region code, use the default (USA) */
	if (i >= MWIFIEX_MAX_REGION_CODE) {
		adapter->region_code = 0x10;
		dev_dbg(adapter->dev,
			"cmd: unknown region code, use default (USA)\n");
	}

	adapter->hw_dot_11n_dev_cap = le32_to_cpu(hw_spec->dot_11n_dev_cap);
	adapter->hw_dev_mcs_support = hw_spec->dev_mcs_support;

	if (adapter->if_ops.update_mp_end_port)
		adapter->if_ops.update_mp_end_port(adapter,
					le16_to_cpu(hw_spec->mp_end_port));

	return 0;
}
