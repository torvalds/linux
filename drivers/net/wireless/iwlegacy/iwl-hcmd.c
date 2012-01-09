/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
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
#include <linux/sched.h>
#include <net/mac80211.h>

#include "iwl-dev.h"
#include "iwl-debug.h"
#include "iwl-eeprom.h"
#include "iwl-core.h"


const char *iwl_legacy_get_cmd_string(u8 cmd)
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
		IWL_CMD(REPLY_WEPKEY);
		IWL_CMD(REPLY_3945_RX);
		IWL_CMD(REPLY_TX);
		IWL_CMD(REPLY_RATE_SCALE);
		IWL_CMD(REPLY_LEDS_CMD);
		IWL_CMD(REPLY_TX_LINK_QUALITY_CMD);
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
		IWL_CMD(REPLY_TX_PWR_TABLE_CMD);
		IWL_CMD(REPLY_BT_CONFIG);
		IWL_CMD(REPLY_STATISTICS_CMD);
		IWL_CMD(STATISTICS_NOTIFICATION);
		IWL_CMD(CARD_STATE_NOTIFICATION);
		IWL_CMD(MISSED_BEACONS_NOTIFICATION);
		IWL_CMD(REPLY_CT_KILL_CONFIG_CMD);
		IWL_CMD(SENSITIVITY_CMD);
		IWL_CMD(REPLY_PHY_CALIBRATION_CMD);
		IWL_CMD(REPLY_RX_PHY_CMD);
		IWL_CMD(REPLY_RX_MPDU_CMD);
		IWL_CMD(REPLY_RX);
		IWL_CMD(REPLY_COMPRESSED_BA);
	default:
		return "UNKNOWN";

	}
}
EXPORT_SYMBOL(iwl_legacy_get_cmd_string);

#define HOST_COMPLETE_TIMEOUT (HZ / 2)

static void iwl_legacy_generic_cmd_callback(struct iwl_priv *priv,
				     struct iwl_device_cmd *cmd,
				     struct iwl_rx_packet *pkt)
{
	if (pkt->hdr.flags & IWL_CMD_FAILED_MSK) {
		IWL_ERR(priv, "Bad return from %s (0x%08X)\n",
		iwl_legacy_get_cmd_string(cmd->hdr.cmd), pkt->hdr.flags);
		return;
	}

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
	switch (cmd->hdr.cmd) {
	case REPLY_TX_LINK_QUALITY_CMD:
	case SENSITIVITY_CMD:
		IWL_DEBUG_HC_DUMP(priv, "back from %s (0x%08X)\n",
		iwl_legacy_get_cmd_string(cmd->hdr.cmd), pkt->hdr.flags);
		break;
	default:
		IWL_DEBUG_HC(priv, "back from %s (0x%08X)\n",
		iwl_legacy_get_cmd_string(cmd->hdr.cmd), pkt->hdr.flags);
	}
#endif
}

static int
iwl_legacy_send_cmd_async(struct iwl_priv *priv, struct iwl_host_cmd *cmd)
{
	int ret;

	BUG_ON(!(cmd->flags & CMD_ASYNC));

	/* An asynchronous command can not expect an SKB to be set. */
	BUG_ON(cmd->flags & CMD_WANT_SKB);

	/* Assign a generic callback if one is not provided */
	if (!cmd->callback)
		cmd->callback = iwl_legacy_generic_cmd_callback;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return -EBUSY;

	ret = iwl_legacy_enqueue_hcmd(priv, cmd);
	if (ret < 0) {
		IWL_ERR(priv, "Error sending %s: enqueue_hcmd failed: %d\n",
			  iwl_legacy_get_cmd_string(cmd->id), ret);
		return ret;
	}
	return 0;
}

int iwl_legacy_send_cmd_sync(struct iwl_priv *priv, struct iwl_host_cmd *cmd)
{
	int cmd_idx;
	int ret;

	lockdep_assert_held(&priv->mutex);

	BUG_ON(cmd->flags & CMD_ASYNC);

	 /* A synchronous command can not have a callback set. */
	BUG_ON(cmd->callback);

	IWL_DEBUG_INFO(priv, "Attempting to send sync command %s\n",
			iwl_legacy_get_cmd_string(cmd->id));

	set_bit(STATUS_HCMD_ACTIVE, &priv->status);
	IWL_DEBUG_INFO(priv, "Setting HCMD_ACTIVE for command %s\n",
			iwl_legacy_get_cmd_string(cmd->id));

	cmd_idx = iwl_legacy_enqueue_hcmd(priv, cmd);
	if (cmd_idx < 0) {
		ret = cmd_idx;
		IWL_ERR(priv, "Error sending %s: enqueue_hcmd failed: %d\n",
			  iwl_legacy_get_cmd_string(cmd->id), ret);
		goto out;
	}

	ret = wait_event_timeout(priv->wait_command_queue,
			!test_bit(STATUS_HCMD_ACTIVE, &priv->status),
			HOST_COMPLETE_TIMEOUT);
	if (!ret) {
		if (test_bit(STATUS_HCMD_ACTIVE, &priv->status)) {
			IWL_ERR(priv,
				"Error sending %s: time out after %dms.\n",
				iwl_legacy_get_cmd_string(cmd->id),
				jiffies_to_msecs(HOST_COMPLETE_TIMEOUT));

			clear_bit(STATUS_HCMD_ACTIVE, &priv->status);
			IWL_DEBUG_INFO(priv,
				"Clearing HCMD_ACTIVE for command %s\n",
				       iwl_legacy_get_cmd_string(cmd->id));
			ret = -ETIMEDOUT;
			goto cancel;
		}
	}

	if (test_bit(STATUS_RF_KILL_HW, &priv->status)) {
		IWL_ERR(priv, "Command %s aborted: RF KILL Switch\n",
			       iwl_legacy_get_cmd_string(cmd->id));
		ret = -ECANCELED;
		goto fail;
	}
	if (test_bit(STATUS_FW_ERROR, &priv->status)) {
		IWL_ERR(priv, "Command %s failed: FW Error\n",
			       iwl_legacy_get_cmd_string(cmd->id));
		ret = -EIO;
		goto fail;
	}
	if ((cmd->flags & CMD_WANT_SKB) && !cmd->reply_page) {
		IWL_ERR(priv, "Error: Response NULL in '%s'\n",
			  iwl_legacy_get_cmd_string(cmd->id));
		ret = -EIO;
		goto cancel;
	}

	ret = 0;
	goto out;

cancel:
	if (cmd->flags & CMD_WANT_SKB) {
		/*
		 * Cancel the CMD_WANT_SKB flag for the cmd in the
		 * TX cmd queue. Otherwise in case the cmd comes
		 * in later, it will possibly set an invalid
		 * address (cmd->meta.source).
		 */
		priv->txq[priv->cmd_queue].meta[cmd_idx].flags &=
							~CMD_WANT_SKB;
	}
fail:
	if (cmd->reply_page) {
		iwl_legacy_free_pages(priv, cmd->reply_page);
		cmd->reply_page = 0;
	}
out:
	return ret;
}
EXPORT_SYMBOL(iwl_legacy_send_cmd_sync);

int iwl_legacy_send_cmd(struct iwl_priv *priv, struct iwl_host_cmd *cmd)
{
	if (cmd->flags & CMD_ASYNC)
		return iwl_legacy_send_cmd_async(priv, cmd);

	return iwl_legacy_send_cmd_sync(priv, cmd);
}
EXPORT_SYMBOL(iwl_legacy_send_cmd);

int
iwl_legacy_send_cmd_pdu(struct iwl_priv *priv, u8 id, u16 len, const void *data)
{
	struct iwl_host_cmd cmd = {
		.id = id,
		.len = len,
		.data = data,
	};

	return iwl_legacy_send_cmd_sync(priv, &cmd);
}
EXPORT_SYMBOL(iwl_legacy_send_cmd_pdu);

int iwl_legacy_send_cmd_pdu_async(struct iwl_priv *priv,
			   u8 id, u16 len, const void *data,
			   void (*callback)(struct iwl_priv *priv,
					    struct iwl_device_cmd *cmd,
					    struct iwl_rx_packet *pkt))
{
	struct iwl_host_cmd cmd = {
		.id = id,
		.len = len,
		.data = data,
	};

	cmd.flags |= CMD_ASYNC;
	cmd.callback = callback;

	return iwl_legacy_send_cmd_async(priv, &cmd);
}
EXPORT_SYMBOL(iwl_legacy_send_cmd_pdu_async);
