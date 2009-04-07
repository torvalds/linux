/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2009 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <net/mac80211.h>

#include "iwl-dev.h" /* FIXME: remove */
#include "iwl-debug.h"
#include "iwl-eeprom.h"
#include "iwl-core.h"


#define IWL_CMD(x) case x: return #x

const char *get_cmd_string(u8 cmd)
{
	switch (cmd) {
		IWL_CMD(REPLY_ALIVE);
		IWL_CMD(REPLY_ERROR);
		IWL_CMD(REPLY_RXON);
		IWL_CMD(REPLY_RXON_ASSOC);
		IWL_CMD(REPLY_QOS_PARAM);
		IWL_CMD(REPLY_RXON_TIMING);
		IWL_CMD(REPLY_ADD_STA);
		IWL_CMD(REPLY_REMOVE_STA);
		IWL_CMD(REPLY_REMOVE_ALL_STA);
		IWL_CMD(REPLY_WEPKEY);
		IWL_CMD(REPLY_3945_RX);
		IWL_CMD(REPLY_TX);
		IWL_CMD(REPLY_RATE_SCALE);
		IWL_CMD(REPLY_LEDS_CMD);
		IWL_CMD(REPLY_TX_LINK_QUALITY_CMD);
		IWL_CMD(COEX_PRIORITY_TABLE_CMD);
		IWL_CMD(RADAR_NOTIFICATION);
		IWL_CMD(REPLY_QUIET_CMD);
		IWL_CMD(REPLY_CHANNEL_SWITCH);
		IWL_CMD(CHANNEL_SWITCH_NOTIFICATION);
		IWL_CMD(REPLY_SPECTRUM_MEASUREMENT_CMD);
		IWL_CMD(SPECTRUM_MEASURE_NOTIFICATION);
		IWL_CMD(POWER_TABLE_CMD);
		IWL_CMD(PM_SLEEP_NOTIFICATION);
		IWL_CMD(PM_DEBUG_STATISTIC_NOTIFIC);
		IWL_CMD(REPLY_SCAN_CMD);
		IWL_CMD(REPLY_SCAN_ABORT_CMD);
		IWL_CMD(SCAN_START_NOTIFICATION);
		IWL_CMD(SCAN_RESULTS_NOTIFICATION);
		IWL_CMD(SCAN_COMPLETE_NOTIFICATION);
		IWL_CMD(BEACON_NOTIFICATION);
		IWL_CMD(REPLY_TX_BEACON);
		IWL_CMD(WHO_IS_AWAKE_NOTIFICATION);
		IWL_CMD(QUIET_NOTIFICATION);
		IWL_CMD(REPLY_TX_PWR_TABLE_CMD);
		IWL_CMD(MEASURE_ABORT_NOTIFICATION);
		IWL_CMD(REPLY_BT_CONFIG);
		IWL_CMD(REPLY_STATISTICS_CMD);
		IWL_CMD(STATISTICS_NOTIFICATION);
		IWL_CMD(REPLY_CARD_STATE_CMD);
		IWL_CMD(CARD_STATE_NOTIFICATION);
		IWL_CMD(MISSED_BEACONS_NOTIFICATION);
		IWL_CMD(REPLY_CT_KILL_CONFIG_CMD);
		IWL_CMD(SENSITIVITY_CMD);
		IWL_CMD(REPLY_PHY_CALIBRATION_CMD);
		IWL_CMD(REPLY_RX_PHY_CMD);
		IWL_CMD(REPLY_RX_MPDU_CMD);
		IWL_CMD(REPLY_RX);
		IWL_CMD(REPLY_COMPRESSED_BA);
		IWL_CMD(CALIBRATION_CFG_CMD);
		IWL_CMD(CALIBRATION_RES_NOTIFICATION);
		IWL_CMD(CALIBRATION_COMPLETE_NOTIFICATION);
		IWL_CMD(REPLY_TX_POWER_DBM_CMD);
	default:
		return "UNKNOWN";

	}
}
EXPORT_SYMBOL(get_cmd_string);

#define HOST_COMPLETE_TIMEOUT (HZ / 2)

static int iwl_generic_cmd_callback(struct iwl_priv *priv,
				    struct iwl_cmd *cmd, struct sk_buff *skb)
{
	struct iwl_rx_packet *pkt = NULL;

	if (!skb) {
		IWL_ERR(priv, "Error: Response NULL in %s.\n",
				get_cmd_string(cmd->hdr.cmd));
		return 1;
	}

	pkt = (struct iwl_rx_packet *)skb->data;
	if (pkt->hdr.flags & IWL_CMD_FAILED_MSK) {
		IWL_ERR(priv, "Bad return from %s (0x%08X)\n",
			get_cmd_string(cmd->hdr.cmd), pkt->hdr.flags);
		return 1;
	}

#ifdef CONFIG_IWLWIFI_DEBUG
	switch (cmd->hdr.cmd) {
	case REPLY_TX_LINK_QUALITY_CMD:
	case SENSITIVITY_CMD:
		IWL_DEBUG_HC_DUMP(priv, "back from %s (0x%08X)\n",
				get_cmd_string(cmd->hdr.cmd), pkt->hdr.flags);
				break;
	default:
		IWL_DEBUG_HC(priv, "back from %s (0x%08X)\n",
				get_cmd_string(cmd->hdr.cmd), pkt->hdr.flags);
	}
#endif

	/* Let iwl_tx_complete free the response skb */
	return 1;
}

static int iwl_send_cmd_async(struct iwl_priv *priv, struct iwl_host_cmd *cmd)
{
	int ret;

	BUG_ON(!(cmd->meta.flags & CMD_ASYNC));

	/* An asynchronous command can not expect an SKB to be set. */
	BUG_ON(cmd->meta.flags & CMD_WANT_SKB);

	/* Assign a generic callback if one is not provided */
	if (!cmd->meta.u.callback)
		cmd->meta.u.callback = iwl_generic_cmd_callback;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return -EBUSY;

	ret = iwl_enqueue_hcmd(priv, cmd);
	if (ret < 0) {
		IWL_ERR(priv, "Error sending %s: enqueue_hcmd failed: %d\n",
			  get_cmd_string(cmd->id), ret);
		return ret;
	}
	return 0;
}

int iwl_send_cmd_sync(struct iwl_priv *priv, struct iwl_host_cmd *cmd)
{
	int cmd_idx;
	int ret;

	BUG_ON(cmd->meta.flags & CMD_ASYNC);

	 /* A synchronous command can not have a callback set. */
	BUG_ON(cmd->meta.u.callback != NULL);

	if (test_and_set_bit(STATUS_HCMD_SYNC_ACTIVE, &priv->status)) {
		IWL_ERR(priv,
			"Error sending %s: Already sending a host command\n",
			get_cmd_string(cmd->id));
		ret = -EBUSY;
		goto out;
	}

	set_bit(STATUS_HCMD_ACTIVE, &priv->status);

	if (cmd->meta.flags & CMD_WANT_SKB)
		cmd->meta.source = &cmd->meta;

	cmd_idx = iwl_enqueue_hcmd(priv, cmd);
	if (cmd_idx < 0) {
		ret = cmd_idx;
		IWL_ERR(priv, "Error sending %s: enqueue_hcmd failed: %d\n",
			  get_cmd_string(cmd->id), ret);
		goto out;
	}

	ret = wait_event_interruptible_timeout(priv->wait_command_queue,
			!test_bit(STATUS_HCMD_ACTIVE, &priv->status),
			HOST_COMPLETE_TIMEOUT);
	if (!ret) {
		if (test_bit(STATUS_HCMD_ACTIVE, &priv->status)) {
			IWL_ERR(priv,
				"Error sending %s: time out after %dms.\n",
				get_cmd_string(cmd->id),
				jiffies_to_msecs(HOST_COMPLETE_TIMEOUT));

			clear_bit(STATUS_HCMD_ACTIVE, &priv->status);
			ret = -ETIMEDOUT;
			goto cancel;
		}
	}

	if (test_bit(STATUS_RF_KILL_HW, &priv->status)) {
		IWL_DEBUG_INFO(priv, "Command %s aborted: RF KILL Switch\n",
			       get_cmd_string(cmd->id));
		ret = -ECANCELED;
		goto fail;
	}
	if (test_bit(STATUS_FW_ERROR, &priv->status)) {
		IWL_DEBUG_INFO(priv, "Command %s failed: FW Error\n",
			       get_cmd_string(cmd->id));
		ret = -EIO;
		goto fail;
	}
	if ((cmd->meta.flags & CMD_WANT_SKB) && !cmd->meta.u.skb) {
		IWL_ERR(priv, "Error: Response NULL in '%s'\n",
			  get_cmd_string(cmd->id));
		ret = -EIO;
		goto cancel;
	}

	ret = 0;
	goto out;

cancel:
	if (cmd->meta.flags & CMD_WANT_SKB) {
		struct iwl_cmd *qcmd;

		/* Cancel the CMD_WANT_SKB flag for the cmd in the
		 * TX cmd queue. Otherwise in case the cmd comes
		 * in later, it will possibly set an invalid
		 * address (cmd->meta.source). */
		qcmd = priv->txq[IWL_CMD_QUEUE_NUM].cmd[cmd_idx];
		qcmd->meta.flags &= ~CMD_WANT_SKB;
	}
fail:
	if (cmd->meta.u.skb) {
		dev_kfree_skb_any(cmd->meta.u.skb);
		cmd->meta.u.skb = NULL;
	}
out:
	clear_bit(STATUS_HCMD_SYNC_ACTIVE, &priv->status);
	return ret;
}
EXPORT_SYMBOL(iwl_send_cmd_sync);

int iwl_send_cmd(struct iwl_priv *priv, struct iwl_host_cmd *cmd)
{
	if (cmd->meta.flags & CMD_ASYNC)
		return iwl_send_cmd_async(priv, cmd);

	return iwl_send_cmd_sync(priv, cmd);
}
EXPORT_SYMBOL(iwl_send_cmd);

int iwl_send_cmd_pdu(struct iwl_priv *priv, u8 id, u16 len, const void *data)
{
	struct iwl_host_cmd cmd = {
		.id = id,
		.len = len,
		.data = data,
	};

	return iwl_send_cmd_sync(priv, &cmd);
}
EXPORT_SYMBOL(iwl_send_cmd_pdu);

int iwl_send_cmd_pdu_async(struct iwl_priv *priv,
			   u8 id, u16 len, const void *data,
			   int (*callback)(struct iwl_priv *priv,
					   struct iwl_cmd *cmd,
					   struct sk_buff *skb))
{
	struct iwl_host_cmd cmd = {
		.id = id,
		.len = len,
		.data = data,
	};

	cmd.meta.flags |= CMD_ASYNC;
	cmd.meta.u.callback = callback;

	return iwl_send_cmd_async(priv, &cmd);
}
EXPORT_SYMBOL(iwl_send_cmd_pdu_async);
