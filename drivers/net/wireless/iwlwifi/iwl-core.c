/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2012 Intel Corporation. All rights reserved.
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
#include <linux/slab.h>
#include <net/mac80211.h>

#include "iwl-eeprom.h"
#include "iwl-debug.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-power.h"
#include "iwl-shared.h"
#include "iwl-agn.h"
#include "iwl-trans.h"



#ifdef CONFIG_IWLWIFI_DEBUGFS

#define IWL_TRAFFIC_DUMP_SIZE	(IWL_TRAFFIC_ENTRY_SIZE * IWL_TRAFFIC_ENTRIES)

void iwl_reset_traffic_log(struct iwl_priv *priv)
{
	priv->tx_traffic_idx = 0;
	priv->rx_traffic_idx = 0;
	if (priv->tx_traffic)
		memset(priv->tx_traffic, 0, IWL_TRAFFIC_DUMP_SIZE);
	if (priv->rx_traffic)
		memset(priv->rx_traffic, 0, IWL_TRAFFIC_DUMP_SIZE);
}

int iwl_alloc_traffic_mem(struct iwl_priv *priv)
{
	u32 traffic_size = IWL_TRAFFIC_DUMP_SIZE;

	if (iwl_have_debug_level(IWL_DL_TX)) {
		if (!priv->tx_traffic) {
			priv->tx_traffic =
				kzalloc(traffic_size, GFP_KERNEL);
			if (!priv->tx_traffic)
				return -ENOMEM;
		}
	}
	if (iwl_have_debug_level(IWL_DL_RX)) {
		if (!priv->rx_traffic) {
			priv->rx_traffic =
				kzalloc(traffic_size, GFP_KERNEL);
			if (!priv->rx_traffic)
				return -ENOMEM;
		}
	}
	iwl_reset_traffic_log(priv);
	return 0;
}

void iwl_free_traffic_mem(struct iwl_priv *priv)
{
	kfree(priv->tx_traffic);
	priv->tx_traffic = NULL;

	kfree(priv->rx_traffic);
	priv->rx_traffic = NULL;
}

void iwl_dbg_log_tx_data_frame(struct iwl_priv *priv,
		      u16 length, struct ieee80211_hdr *header)
{
	__le16 fc;
	u16 len;

	if (likely(!iwl_have_debug_level(IWL_DL_TX)))
		return;

	if (!priv->tx_traffic)
		return;

	fc = header->frame_control;
	if (ieee80211_is_data(fc)) {
		len = (length > IWL_TRAFFIC_ENTRY_SIZE)
		       ? IWL_TRAFFIC_ENTRY_SIZE : length;
		memcpy((priv->tx_traffic +
		       (priv->tx_traffic_idx * IWL_TRAFFIC_ENTRY_SIZE)),
		       header, len);
		priv->tx_traffic_idx =
			(priv->tx_traffic_idx + 1) % IWL_TRAFFIC_ENTRIES;
	}
}

void iwl_dbg_log_rx_data_frame(struct iwl_priv *priv,
		      u16 length, struct ieee80211_hdr *header)
{
	__le16 fc;
	u16 len;

	if (likely(!iwl_have_debug_level(IWL_DL_RX)))
		return;

	if (!priv->rx_traffic)
		return;

	fc = header->frame_control;
	if (ieee80211_is_data(fc)) {
		len = (length > IWL_TRAFFIC_ENTRY_SIZE)
		       ? IWL_TRAFFIC_ENTRY_SIZE : length;
		memcpy((priv->rx_traffic +
		       (priv->rx_traffic_idx * IWL_TRAFFIC_ENTRY_SIZE)),
		       header, len);
		priv->rx_traffic_idx =
			(priv->rx_traffic_idx + 1) % IWL_TRAFFIC_ENTRIES;
	}
}

const char *get_mgmt_string(int cmd)
{
#define IWL_CMD(x) case x: return #x
	switch (cmd) {
		IWL_CMD(MANAGEMENT_ASSOC_REQ);
		IWL_CMD(MANAGEMENT_ASSOC_RESP);
		IWL_CMD(MANAGEMENT_REASSOC_REQ);
		IWL_CMD(MANAGEMENT_REASSOC_RESP);
		IWL_CMD(MANAGEMENT_PROBE_REQ);
		IWL_CMD(MANAGEMENT_PROBE_RESP);
		IWL_CMD(MANAGEMENT_BEACON);
		IWL_CMD(MANAGEMENT_ATIM);
		IWL_CMD(MANAGEMENT_DISASSOC);
		IWL_CMD(MANAGEMENT_AUTH);
		IWL_CMD(MANAGEMENT_DEAUTH);
		IWL_CMD(MANAGEMENT_ACTION);
	default:
		return "UNKNOWN";

	}
#undef IWL_CMD
}

const char *get_ctrl_string(int cmd)
{
#define IWL_CMD(x) case x: return #x
	switch (cmd) {
		IWL_CMD(CONTROL_BACK_REQ);
		IWL_CMD(CONTROL_BACK);
		IWL_CMD(CONTROL_PSPOLL);
		IWL_CMD(CONTROL_RTS);
		IWL_CMD(CONTROL_CTS);
		IWL_CMD(CONTROL_ACK);
		IWL_CMD(CONTROL_CFEND);
		IWL_CMD(CONTROL_CFENDACK);
	default:
		return "UNKNOWN";

	}
#undef IWL_CMD
}

void iwl_clear_traffic_stats(struct iwl_priv *priv)
{
	memset(&priv->tx_stats, 0, sizeof(struct traffic_stats));
	memset(&priv->rx_stats, 0, sizeof(struct traffic_stats));
}

/*
 * if CONFIG_IWLWIFI_DEBUGFS defined, iwl_update_stats function will
 * record all the MGMT, CTRL and DATA pkt for both TX and Rx pass.
 * Use debugFs to display the rx/rx_statistics
 * if CONFIG_IWLWIFI_DEBUGFS not being defined, then no MGMT and CTRL
 * information will be recorded, but DATA pkt still will be recorded
 * for the reason of iwl_led.c need to control the led blinking based on
 * number of tx and rx data.
 *
 */
void iwl_update_stats(struct iwl_priv *priv, bool is_tx, __le16 fc, u16 len)
{
	struct traffic_stats	*stats;

	if (is_tx)
		stats = &priv->tx_stats;
	else
		stats = &priv->rx_stats;

	if (ieee80211_is_mgmt(fc)) {
		switch (fc & cpu_to_le16(IEEE80211_FCTL_STYPE)) {
		case cpu_to_le16(IEEE80211_STYPE_ASSOC_REQ):
			stats->mgmt[MANAGEMENT_ASSOC_REQ]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_ASSOC_RESP):
			stats->mgmt[MANAGEMENT_ASSOC_RESP]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_REASSOC_REQ):
			stats->mgmt[MANAGEMENT_REASSOC_REQ]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_REASSOC_RESP):
			stats->mgmt[MANAGEMENT_REASSOC_RESP]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_PROBE_REQ):
			stats->mgmt[MANAGEMENT_PROBE_REQ]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_PROBE_RESP):
			stats->mgmt[MANAGEMENT_PROBE_RESP]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_BEACON):
			stats->mgmt[MANAGEMENT_BEACON]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_ATIM):
			stats->mgmt[MANAGEMENT_ATIM]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_DISASSOC):
			stats->mgmt[MANAGEMENT_DISASSOC]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_AUTH):
			stats->mgmt[MANAGEMENT_AUTH]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_DEAUTH):
			stats->mgmt[MANAGEMENT_DEAUTH]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_ACTION):
			stats->mgmt[MANAGEMENT_ACTION]++;
			break;
		}
	} else if (ieee80211_is_ctl(fc)) {
		switch (fc & cpu_to_le16(IEEE80211_FCTL_STYPE)) {
		case cpu_to_le16(IEEE80211_STYPE_BACK_REQ):
			stats->ctrl[CONTROL_BACK_REQ]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_BACK):
			stats->ctrl[CONTROL_BACK]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_PSPOLL):
			stats->ctrl[CONTROL_PSPOLL]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_RTS):
			stats->ctrl[CONTROL_RTS]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_CTS):
			stats->ctrl[CONTROL_CTS]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_ACK):
			stats->ctrl[CONTROL_ACK]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_CFEND):
			stats->ctrl[CONTROL_CFEND]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_CFENDACK):
			stats->ctrl[CONTROL_CFENDACK]++;
			break;
		}
	} else {
		/* data */
		stats->data_cnt++;
		stats->data_bytes += len;
	}
}
#endif

int iwl_cmd_echo_test(struct iwl_priv *priv)
{
	int ret;
	struct iwl_host_cmd cmd = {
		.id = REPLY_ECHO,
		.len = { 0 },
		.flags = CMD_SYNC,
	};

	ret = iwl_dvm_send_cmd(priv, &cmd);
	if (ret)
		IWL_ERR(priv, "echo testing fail: 0X%x\n", ret);
	else
		IWL_DEBUG_INFO(priv, "echo testing pass\n");
	return ret;
}
