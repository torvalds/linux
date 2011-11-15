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


const char *il_get_cmd_string(u8 cmd)
{
	switch (cmd) {
		IL_CMD(REPLY_ALIVE);
		IL_CMD(REPLY_ERROR);
		IL_CMD(REPLY_RXON);
		IL_CMD(REPLY_RXON_ASSOC);
		IL_CMD(REPLY_QOS_PARAM);
		IL_CMD(REPLY_RXON_TIMING);
		IL_CMD(REPLY_ADD_STA);
		IL_CMD(REPLY_REMOVE_STA);
		IL_CMD(REPLY_WEPKEY);
		IL_CMD(REPLY_3945_RX);
		IL_CMD(REPLY_TX);
		IL_CMD(REPLY_RATE_SCALE);
		IL_CMD(REPLY_LEDS_CMD);
		IL_CMD(REPLY_TX_LINK_QUALITY_CMD);
		IL_CMD(REPLY_CHANNEL_SWITCH);
		IL_CMD(CHANNEL_SWITCH_NOTIFICATION);
		IL_CMD(REPLY_SPECTRUM_MEASUREMENT_CMD);
		IL_CMD(SPECTRUM_MEASURE_NOTIFICATION);
		IL_CMD(POWER_TABLE_CMD);
		IL_CMD(PM_SLEEP_NOTIFICATION);
		IL_CMD(PM_DEBUG_STATISTIC_NOTIFIC);
		IL_CMD(REPLY_SCAN_CMD);
		IL_CMD(REPLY_SCAN_ABORT_CMD);
		IL_CMD(SCAN_START_NOTIFICATION);
		IL_CMD(SCAN_RESULTS_NOTIFICATION);
		IL_CMD(SCAN_COMPLETE_NOTIFICATION);
		IL_CMD(BEACON_NOTIFICATION);
		IL_CMD(REPLY_TX_BEACON);
		IL_CMD(REPLY_TX_PWR_TABLE_CMD);
		IL_CMD(REPLY_BT_CONFIG);
		IL_CMD(REPLY_STATISTICS_CMD);
		IL_CMD(STATISTICS_NOTIFICATION);
		IL_CMD(CARD_STATE_NOTIFICATION);
		IL_CMD(MISSED_BEACONS_NOTIFICATION);
		IL_CMD(REPLY_CT_KILL_CONFIG_CMD);
		IL_CMD(SENSITIVITY_CMD);
		IL_CMD(REPLY_PHY_CALIBRATION_CMD);
		IL_CMD(REPLY_RX_PHY_CMD);
		IL_CMD(REPLY_RX_MPDU_CMD);
		IL_CMD(REPLY_RX);
		IL_CMD(REPLY_COMPRESSED_BA);
	default:
		return "UNKNOWN";

	}
}
EXPORT_SYMBOL(il_get_cmd_string);

#define HOST_COMPLETE_TIMEOUT (HZ / 2)

static void il_generic_cmd_callback(struct il_priv *il,
				     struct il_device_cmd *cmd,
				     struct il_rx_packet *pkt)
{
	if (pkt->hdr.flags & IL_CMD_FAILED_MSK) {
		IL_ERR(il, "Bad return from %s (0x%08X)\n",
		il_get_cmd_string(cmd->hdr.cmd), pkt->hdr.flags);
		return;
	}

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
	switch (cmd->hdr.cmd) {
	case REPLY_TX_LINK_QUALITY_CMD:
	case SENSITIVITY_CMD:
		D_HC_DUMP("back from %s (0x%08X)\n",
		il_get_cmd_string(cmd->hdr.cmd), pkt->hdr.flags);
		break;
	default:
		D_HC("back from %s (0x%08X)\n",
		il_get_cmd_string(cmd->hdr.cmd), pkt->hdr.flags);
	}
#endif
}

static int
il_send_cmd_async(struct il_priv *il, struct il_host_cmd *cmd)
{
	int ret;

	BUG_ON(!(cmd->flags & CMD_ASYNC));

	/* An asynchronous command can not expect an SKB to be set. */
	BUG_ON(cmd->flags & CMD_WANT_SKB);

	/* Assign a generic callback if one is not provided */
	if (!cmd->callback)
		cmd->callback = il_generic_cmd_callback;

	if (test_bit(STATUS_EXIT_PENDING, &il->status))
		return -EBUSY;

	ret = il_enqueue_hcmd(il, cmd);
	if (ret < 0) {
		IL_ERR(il, "Error sending %s: enqueue_hcmd failed: %d\n",
			  il_get_cmd_string(cmd->id), ret);
		return ret;
	}
	return 0;
}

int il_send_cmd_sync(struct il_priv *il, struct il_host_cmd *cmd)
{
	int cmd_idx;
	int ret;

	lockdep_assert_held(&il->mutex);

	BUG_ON(cmd->flags & CMD_ASYNC);

	 /* A synchronous command can not have a callback set. */
	BUG_ON(cmd->callback);

	D_INFO("Attempting to send sync command %s\n",
			il_get_cmd_string(cmd->id));

	set_bit(STATUS_HCMD_ACTIVE, &il->status);
	D_INFO("Setting HCMD_ACTIVE for command %s\n",
			il_get_cmd_string(cmd->id));

	cmd_idx = il_enqueue_hcmd(il, cmd);
	if (cmd_idx < 0) {
		ret = cmd_idx;
		IL_ERR(il, "Error sending %s: enqueue_hcmd failed: %d\n",
			  il_get_cmd_string(cmd->id), ret);
		goto out;
	}

	ret = wait_event_timeout(il->wait_command_queue,
			!test_bit(STATUS_HCMD_ACTIVE, &il->status),
			HOST_COMPLETE_TIMEOUT);
	if (!ret) {
		if (test_bit(STATUS_HCMD_ACTIVE, &il->status)) {
			IL_ERR(il,
				"Error sending %s: time out after %dms.\n",
				il_get_cmd_string(cmd->id),
				jiffies_to_msecs(HOST_COMPLETE_TIMEOUT));

			clear_bit(STATUS_HCMD_ACTIVE, &il->status);
			D_INFO(
				"Clearing HCMD_ACTIVE for command %s\n",
				       il_get_cmd_string(cmd->id));
			ret = -ETIMEDOUT;
			goto cancel;
		}
	}

	if (test_bit(STATUS_RF_KILL_HW, &il->status)) {
		IL_ERR(il, "Command %s aborted: RF KILL Switch\n",
			       il_get_cmd_string(cmd->id));
		ret = -ECANCELED;
		goto fail;
	}
	if (test_bit(STATUS_FW_ERROR, &il->status)) {
		IL_ERR(il, "Command %s failed: FW Error\n",
			       il_get_cmd_string(cmd->id));
		ret = -EIO;
		goto fail;
	}
	if ((cmd->flags & CMD_WANT_SKB) && !cmd->reply_page) {
		IL_ERR(il, "Error: Response NULL in '%s'\n",
			  il_get_cmd_string(cmd->id));
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
		il->txq[il->cmd_queue].meta[cmd_idx].flags &=
							~CMD_WANT_SKB;
	}
fail:
	if (cmd->reply_page) {
		il_free_pages(il, cmd->reply_page);
		cmd->reply_page = 0;
	}
out:
	return ret;
}
EXPORT_SYMBOL(il_send_cmd_sync);

int il_send_cmd(struct il_priv *il, struct il_host_cmd *cmd)
{
	if (cmd->flags & CMD_ASYNC)
		return il_send_cmd_async(il, cmd);

	return il_send_cmd_sync(il, cmd);
}
EXPORT_SYMBOL(il_send_cmd);

int
il_send_cmd_pdu(struct il_priv *il, u8 id, u16 len, const void *data)
{
	struct il_host_cmd cmd = {
		.id = id,
		.len = len,
		.data = data,
	};

	return il_send_cmd_sync(il, &cmd);
}
EXPORT_SYMBOL(il_send_cmd_pdu);

int il_send_cmd_pdu_async(struct il_priv *il,
			   u8 id, u16 len, const void *data,
			   void (*callback)(struct il_priv *il,
					    struct il_device_cmd *cmd,
					    struct il_rx_packet *pkt))
{
	struct il_host_cmd cmd = {
		.id = id,
		.len = len,
		.data = data,
	};

	cmd.flags |= CMD_ASYNC;
	cmd.callback = callback;

	return il_send_cmd_async(il, &cmd);
}
EXPORT_SYMBOL(il_send_cmd_pdu_async);
