/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2013 Intel Corporation. All rights reserved.
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
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2013 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>

#include <net/mac80211.h>

#include "iwl-debug.h"
#include "mvm.h"
#include "iwl-modparams.h"
#include "fw-api-power.h"

#define POWER_KEEP_ALIVE_PERIOD_SEC    25

static int iwl_mvm_beacon_filter_send_cmd(struct iwl_mvm *mvm,
					  struct iwl_beacon_filter_cmd *cmd)
{
	int ret;

	ret = iwl_mvm_send_cmd_pdu(mvm, REPLY_BEACON_FILTERING_CMD, CMD_SYNC,
				   sizeof(struct iwl_beacon_filter_cmd), cmd);

	if (!ret) {
		IWL_DEBUG_POWER(mvm, "ba_enable_beacon_abort is: %d\n",
				cmd->ba_enable_beacon_abort);
		IWL_DEBUG_POWER(mvm, "ba_escape_timer is: %d\n",
				cmd->ba_escape_timer);
		IWL_DEBUG_POWER(mvm, "bf_debug_flag is: %d\n",
				cmd->bf_debug_flag);
		IWL_DEBUG_POWER(mvm, "bf_enable_beacon_filter is: %d\n",
				cmd->bf_enable_beacon_filter);
		IWL_DEBUG_POWER(mvm, "bf_energy_delta is: %d\n",
				cmd->bf_energy_delta);
		IWL_DEBUG_POWER(mvm, "bf_escape_timer is: %d\n",
				cmd->bf_escape_timer);
		IWL_DEBUG_POWER(mvm, "bf_roaming_energy_delta is: %d\n",
				cmd->bf_roaming_energy_delta);
		IWL_DEBUG_POWER(mvm, "bf_roaming_state is: %d\n",
				cmd->bf_roaming_state);
		IWL_DEBUG_POWER(mvm, "bf_temperature_delta is: %d\n",
				cmd->bf_temperature_delta);
	}
	return ret;
}

static int iwl_mvm_update_beacon_abort(struct iwl_mvm *mvm,
				       struct ieee80211_vif *vif, bool enable)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_beacon_filter_cmd cmd = {
		IWL_BF_CMD_CONFIG_DEFAULTS,
		.bf_enable_beacon_filter = 1,
		.ba_enable_beacon_abort = enable,
	};

	if (!mvmvif->bf_enabled)
		return 0;

	return iwl_mvm_beacon_filter_send_cmd(mvm, &cmd);
}

static void iwl_mvm_power_log(struct iwl_mvm *mvm,
			      struct iwl_powertable_cmd *cmd)
{
	IWL_DEBUG_POWER(mvm,
			"Sending power table command for power level %d, flags = 0x%X\n",
			iwlmvm_mod_params.power_scheme,
			le16_to_cpu(cmd->flags));
	IWL_DEBUG_POWER(mvm, "Keep alive = %u sec\n", cmd->keep_alive_seconds);

	if (cmd->flags & cpu_to_le16(POWER_FLAGS_POWER_MANAGEMENT_ENA_MSK)) {
		IWL_DEBUG_POWER(mvm, "Rx timeout = %u usec\n",
				le32_to_cpu(cmd->rx_data_timeout));
		IWL_DEBUG_POWER(mvm, "Tx timeout = %u usec\n",
				le32_to_cpu(cmd->tx_data_timeout));
		IWL_DEBUG_POWER(mvm, "LP RX RSSI threshold = %u\n",
				cmd->lprx_rssi_threshold);
		if (cmd->flags & cpu_to_le16(POWER_FLAGS_SKIP_OVER_DTIM_MSK))
			IWL_DEBUG_POWER(mvm, "DTIM periods to skip = %u\n",
					le32_to_cpu(cmd->skip_dtim_periods));
	}
}

void iwl_mvm_power_build_cmd(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			     struct iwl_powertable_cmd *cmd)
{
	struct ieee80211_hw *hw = mvm->hw;
	struct ieee80211_chanctx_conf *chanctx_conf;
	struct ieee80211_channel *chan;
	int dtimper, dtimper_msec;
	int keep_alive;
	bool radar_detect = false;

	/*
	 * Regardless of power management state the driver must set
	 * keep alive period. FW will use it for sending keep alive NDPs
	 * immediately after association.
	 */
	cmd->keep_alive_seconds = POWER_KEEP_ALIVE_PERIOD_SEC;

	if (iwlmvm_mod_params.power_scheme == IWL_POWER_SCHEME_CAM)
		return;

	cmd->flags |= cpu_to_le16(POWER_FLAGS_POWER_SAVE_ENA_MSK);

	if (!vif->bss_conf.ps)
		return;

	cmd->flags |= cpu_to_le16(POWER_FLAGS_POWER_MANAGEMENT_ENA_MSK);

	dtimper = hw->conf.ps_dtim_period ?: 1;

	/* Check if radar detection is required on current channel */
	rcu_read_lock();
	chanctx_conf = rcu_dereference(vif->chanctx_conf);
	WARN_ON(!chanctx_conf);
	if (chanctx_conf) {
		chan = chanctx_conf->def.chan;
		radar_detect = chan->flags & IEEE80211_CHAN_RADAR;
	}
	rcu_read_unlock();

	/* Check skip over DTIM conditions */
	if (!radar_detect && (dtimper <= 10) &&
	    (iwlmvm_mod_params.power_scheme == IWL_POWER_SCHEME_LP ||
	     mvm->cur_ucode == IWL_UCODE_WOWLAN)) {
		cmd->flags |= cpu_to_le16(POWER_FLAGS_SKIP_OVER_DTIM_MSK);
		cmd->skip_dtim_periods = cpu_to_le32(3);
	}

	/* Check that keep alive period is at least 3 * DTIM */
	dtimper_msec = dtimper * vif->bss_conf.beacon_int;
	keep_alive = max_t(int, 3 * dtimper_msec,
			   MSEC_PER_SEC * cmd->keep_alive_seconds);
	keep_alive = DIV_ROUND_UP(keep_alive, MSEC_PER_SEC);
	cmd->keep_alive_seconds = keep_alive;

	if (mvm->cur_ucode != IWL_UCODE_WOWLAN) {
		cmd->rx_data_timeout = cpu_to_le32(100 * USEC_PER_MSEC);
		cmd->tx_data_timeout = cpu_to_le32(100 * USEC_PER_MSEC);
	} else {
		cmd->rx_data_timeout = cpu_to_le32(10 * USEC_PER_MSEC);
		cmd->tx_data_timeout = cpu_to_le32(10 * USEC_PER_MSEC);
	}
}

int iwl_mvm_power_update_mode(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	int ret;
	bool ba_enable;
	struct iwl_powertable_cmd cmd = {};

	if (vif->type != NL80211_IFTYPE_STATION || vif->p2p)
		return 0;

	iwl_mvm_power_build_cmd(mvm, vif, &cmd);
	iwl_mvm_power_log(mvm, &cmd);

	ret = iwl_mvm_send_cmd_pdu(mvm, POWER_TABLE_CMD, CMD_SYNC,
				   sizeof(cmd), &cmd);
	if (ret)
		return ret;

	ba_enable = !!(cmd.flags &
		       cpu_to_le16(POWER_FLAGS_POWER_MANAGEMENT_ENA_MSK));

	return iwl_mvm_update_beacon_abort(mvm, vif, ba_enable);
}

int iwl_mvm_power_disable(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_powertable_cmd cmd = {};

	if (vif->type != NL80211_IFTYPE_STATION || vif->p2p)
		return 0;

	if (iwlmvm_mod_params.power_scheme != IWL_POWER_SCHEME_CAM)
		cmd.flags |= cpu_to_le16(POWER_FLAGS_POWER_SAVE_ENA_MSK);

	iwl_mvm_power_log(mvm, &cmd);

	return iwl_mvm_send_cmd_pdu(mvm, POWER_TABLE_CMD, CMD_ASYNC,
				    sizeof(cmd), &cmd);
}

int iwl_mvm_enable_beacon_filter(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_beacon_filter_cmd cmd = {
		IWL_BF_CMD_CONFIG_DEFAULTS,
		.bf_enable_beacon_filter = 1,
	};
	int ret;

	if (mvmvif != mvm->bf_allowed_vif ||
	    vif->type != NL80211_IFTYPE_STATION || vif->p2p)
		return 0;

	ret = iwl_mvm_beacon_filter_send_cmd(mvm, &cmd);

	if (!ret)
		mvmvif->bf_enabled = true;

	return ret;
}

int iwl_mvm_disable_beacon_filter(struct iwl_mvm *mvm,
				  struct ieee80211_vif *vif)
{
	struct iwl_beacon_filter_cmd cmd = {};
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	if (vif->type != NL80211_IFTYPE_STATION || vif->p2p)
		return 0;

	ret = iwl_mvm_beacon_filter_send_cmd(mvm, &cmd);

	if (!ret)
		mvmvif->bf_enabled = false;

	return ret;
}
