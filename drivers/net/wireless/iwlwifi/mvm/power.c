/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
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
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
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

#include <net/mac80211.h>

#include "iwl-debug.h"
#include "mvm.h"
#include "iwl-modparams.h"
#include "fw-api-power.h"

#define POWER_KEEP_ALIVE_PERIOD_SEC    25

static
int iwl_mvm_beacon_filter_send_cmd(struct iwl_mvm *mvm,
				   struct iwl_beacon_filter_cmd *cmd,
				   u32 flags)
{
	IWL_DEBUG_POWER(mvm, "ba_enable_beacon_abort is: %d\n",
			le32_to_cpu(cmd->ba_enable_beacon_abort));
	IWL_DEBUG_POWER(mvm, "ba_escape_timer is: %d\n",
			le32_to_cpu(cmd->ba_escape_timer));
	IWL_DEBUG_POWER(mvm, "bf_debug_flag is: %d\n",
			le32_to_cpu(cmd->bf_debug_flag));
	IWL_DEBUG_POWER(mvm, "bf_enable_beacon_filter is: %d\n",
			le32_to_cpu(cmd->bf_enable_beacon_filter));
	IWL_DEBUG_POWER(mvm, "bf_energy_delta is: %d\n",
			le32_to_cpu(cmd->bf_energy_delta));
	IWL_DEBUG_POWER(mvm, "bf_escape_timer is: %d\n",
			le32_to_cpu(cmd->bf_escape_timer));
	IWL_DEBUG_POWER(mvm, "bf_roaming_energy_delta is: %d\n",
			le32_to_cpu(cmd->bf_roaming_energy_delta));
	IWL_DEBUG_POWER(mvm, "bf_roaming_state is: %d\n",
			le32_to_cpu(cmd->bf_roaming_state));
	IWL_DEBUG_POWER(mvm, "bf_temp_threshold is: %d\n",
			le32_to_cpu(cmd->bf_temp_threshold));
	IWL_DEBUG_POWER(mvm, "bf_temp_fast_filter is: %d\n",
			le32_to_cpu(cmd->bf_temp_fast_filter));
	IWL_DEBUG_POWER(mvm, "bf_temp_slow_filter is: %d\n",
			le32_to_cpu(cmd->bf_temp_slow_filter));

	return iwl_mvm_send_cmd_pdu(mvm, REPLY_BEACON_FILTERING_CMD, flags,
				    sizeof(struct iwl_beacon_filter_cmd), cmd);
}

static
void iwl_mvm_beacon_filter_set_cqm_params(struct iwl_mvm *mvm,
					  struct ieee80211_vif *vif,
					  struct iwl_beacon_filter_cmd *cmd)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (vif->bss_conf.cqm_rssi_thold) {
		cmd->bf_energy_delta =
			cpu_to_le32(vif->bss_conf.cqm_rssi_hyst);
		/* fw uses an absolute value for this */
		cmd->bf_roaming_state =
			cpu_to_le32(-vif->bss_conf.cqm_rssi_thold);
	}
	cmd->ba_enable_beacon_abort = cpu_to_le32(mvmvif->bf_data.ba_enabled);
}

int iwl_mvm_update_beacon_abort(struct iwl_mvm *mvm,
				struct ieee80211_vif *vif, bool enable)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_beacon_filter_cmd cmd = {
		IWL_BF_CMD_CONFIG_DEFAULTS,
		.bf_enable_beacon_filter = cpu_to_le32(1),
		.ba_enable_beacon_abort = cpu_to_le32(enable),
	};

	if (!mvmvif->bf_data.bf_enabled)
		return 0;

	if (mvm->cur_ucode == IWL_UCODE_WOWLAN)
		cmd.ba_escape_timer = cpu_to_le32(IWL_BA_ESCAPE_TIMER_D3);

	mvmvif->bf_data.ba_enabled = enable;
	iwl_mvm_beacon_filter_set_cqm_params(mvm, vif, &cmd);
	iwl_mvm_beacon_filter_debugfs_parameters(vif, &cmd);
	return iwl_mvm_beacon_filter_send_cmd(mvm, &cmd, CMD_SYNC);
}

static void iwl_mvm_power_log(struct iwl_mvm *mvm,
			      struct iwl_mac_power_cmd *cmd)
{
	IWL_DEBUG_POWER(mvm,
			"Sending power table command on mac id 0x%X for power level %d, flags = 0x%X\n",
			cmd->id_and_color, iwlmvm_mod_params.power_scheme,
			le16_to_cpu(cmd->flags));
	IWL_DEBUG_POWER(mvm, "Keep alive = %u sec\n",
			le16_to_cpu(cmd->keep_alive_seconds));

	if (!(cmd->flags & cpu_to_le16(POWER_FLAGS_POWER_MANAGEMENT_ENA_MSK))) {
		IWL_DEBUG_POWER(mvm, "Disable power management\n");
		return;
	}

	IWL_DEBUG_POWER(mvm, "Rx timeout = %u usec\n",
			le32_to_cpu(cmd->rx_data_timeout));
	IWL_DEBUG_POWER(mvm, "Tx timeout = %u usec\n",
			le32_to_cpu(cmd->tx_data_timeout));
	if (cmd->flags & cpu_to_le16(POWER_FLAGS_SKIP_OVER_DTIM_MSK))
		IWL_DEBUG_POWER(mvm, "DTIM periods to skip = %u\n",
				cmd->skip_dtim_periods);
	if (cmd->flags & cpu_to_le16(POWER_FLAGS_LPRX_ENA_MSK))
		IWL_DEBUG_POWER(mvm, "LP RX RSSI threshold = %u\n",
				cmd->lprx_rssi_threshold);
	if (cmd->flags & cpu_to_le16(POWER_FLAGS_ADVANCE_PM_ENA_MSK)) {
		IWL_DEBUG_POWER(mvm, "uAPSD enabled\n");
		IWL_DEBUG_POWER(mvm, "Rx timeout (uAPSD) = %u usec\n",
				le32_to_cpu(cmd->rx_data_timeout_uapsd));
		IWL_DEBUG_POWER(mvm, "Tx timeout (uAPSD) = %u usec\n",
				le32_to_cpu(cmd->tx_data_timeout_uapsd));
		IWL_DEBUG_POWER(mvm, "QNDP TID = %d\n", cmd->qndp_tid);
		IWL_DEBUG_POWER(mvm, "ACs flags = 0x%x\n", cmd->uapsd_ac_flags);
		IWL_DEBUG_POWER(mvm, "Max SP = %d\n", cmd->uapsd_max_sp);
	}
}

static void iwl_mvm_power_configure_uapsd(struct iwl_mvm *mvm,
					  struct ieee80211_vif *vif,
					  struct iwl_mac_power_cmd *cmd)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	enum ieee80211_ac_numbers ac;
	bool tid_found = false;

	for (ac = IEEE80211_AC_VO; ac <= IEEE80211_AC_BK; ac++) {
		if (!mvmvif->queue_params[ac].uapsd)
			continue;

		if (mvm->cur_ucode != IWL_UCODE_WOWLAN)
			cmd->flags |=
				cpu_to_le16(POWER_FLAGS_ADVANCE_PM_ENA_MSK);

		cmd->uapsd_ac_flags |= BIT(ac);

		/* QNDP TID - the highest TID with no admission control */
		if (!tid_found && !mvmvif->queue_params[ac].acm) {
			tid_found = true;
			switch (ac) {
			case IEEE80211_AC_VO:
				cmd->qndp_tid = 6;
				break;
			case IEEE80211_AC_VI:
				cmd->qndp_tid = 5;
				break;
			case IEEE80211_AC_BE:
				cmd->qndp_tid = 0;
				break;
			case IEEE80211_AC_BK:
				cmd->qndp_tid = 1;
				break;
			}
		}
	}

	if (!(cmd->flags & cpu_to_le16(POWER_FLAGS_ADVANCE_PM_ENA_MSK)))
		return;

	cmd->flags |= cpu_to_le16(POWER_FLAGS_UAPSD_MISBEHAVING_ENA_MSK);

	if (cmd->uapsd_ac_flags == (BIT(IEEE80211_AC_VO) |
				    BIT(IEEE80211_AC_VI) |
				    BIT(IEEE80211_AC_BE) |
				    BIT(IEEE80211_AC_BK))) {
		cmd->flags |= cpu_to_le16(POWER_FLAGS_SNOOZE_ENA_MSK);
		cmd->snooze_interval = cpu_to_le16(IWL_MVM_PS_SNOOZE_INTERVAL);
		cmd->snooze_window = (mvm->cur_ucode == IWL_UCODE_WOWLAN) ?
			cpu_to_le16(IWL_MVM_WOWLAN_PS_SNOOZE_WINDOW) :
			cpu_to_le16(IWL_MVM_PS_SNOOZE_WINDOW);
	}

	cmd->uapsd_max_sp = IWL_UAPSD_MAX_SP;

	if (mvm->cur_ucode == IWL_UCODE_WOWLAN || cmd->flags &
	    cpu_to_le16(POWER_FLAGS_SNOOZE_ENA_MSK)) {
		cmd->rx_data_timeout_uapsd =
			cpu_to_le32(IWL_MVM_WOWLAN_PS_RX_DATA_TIMEOUT);
		cmd->tx_data_timeout_uapsd =
			cpu_to_le32(IWL_MVM_WOWLAN_PS_TX_DATA_TIMEOUT);
	} else {
		cmd->rx_data_timeout_uapsd =
			cpu_to_le32(IWL_MVM_UAPSD_RX_DATA_TIMEOUT);
		cmd->tx_data_timeout_uapsd =
			cpu_to_le32(IWL_MVM_UAPSD_TX_DATA_TIMEOUT);
	}

	if (cmd->flags & cpu_to_le16(POWER_FLAGS_SNOOZE_ENA_MSK)) {
		cmd->heavy_tx_thld_packets =
			IWL_MVM_PS_SNOOZE_HEAVY_TX_THLD_PACKETS;
		cmd->heavy_rx_thld_packets =
			IWL_MVM_PS_SNOOZE_HEAVY_RX_THLD_PACKETS;
	} else {
		cmd->heavy_tx_thld_packets =
			IWL_MVM_PS_HEAVY_TX_THLD_PACKETS;
		cmd->heavy_rx_thld_packets =
			IWL_MVM_PS_HEAVY_RX_THLD_PACKETS;
	}
	cmd->heavy_tx_thld_percentage =
		IWL_MVM_PS_HEAVY_TX_THLD_PERCENT;
	cmd->heavy_rx_thld_percentage =
		IWL_MVM_PS_HEAVY_RX_THLD_PERCENT;
}

static void iwl_mvm_power_build_cmd(struct iwl_mvm *mvm,
				    struct ieee80211_vif *vif,
				    struct iwl_mac_power_cmd *cmd)
{
	struct ieee80211_hw *hw = mvm->hw;
	struct ieee80211_chanctx_conf *chanctx_conf;
	struct ieee80211_channel *chan;
	int dtimper, dtimper_msec;
	int keep_alive;
	bool radar_detect = false;
	struct iwl_mvm_vif *mvmvif __maybe_unused =
		iwl_mvm_vif_from_mac80211(vif);
	bool allow_uapsd = true;

	cmd->id_and_color = cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id,
							    mvmvif->color));
	dtimper = hw->conf.ps_dtim_period ?: 1;

	/*
	 * Regardless of power management state the driver must set
	 * keep alive period. FW will use it for sending keep alive NDPs
	 * immediately after association. Check that keep alive period
	 * is at least 3 * DTIM
	 */
	dtimper_msec = dtimper * vif->bss_conf.beacon_int;
	keep_alive = max_t(int, 3 * dtimper_msec,
			   MSEC_PER_SEC * POWER_KEEP_ALIVE_PERIOD_SEC);
	keep_alive = DIV_ROUND_UP(keep_alive, MSEC_PER_SEC);
	cmd->keep_alive_seconds = cpu_to_le16(keep_alive);

	if (mvm->ps_disabled)
		return;

	cmd->flags |= cpu_to_le16(POWER_FLAGS_POWER_SAVE_ENA_MSK);

#ifdef CONFIG_IWLWIFI_DEBUGFS
	if (mvmvif->dbgfs_pm.mask & MVM_DEBUGFS_PM_DISABLE_POWER_OFF &&
	    mvmvif->dbgfs_pm.disable_power_off)
		cmd->flags &= cpu_to_le16(~POWER_FLAGS_POWER_SAVE_ENA_MSK);
#endif
	if (!vif->bss_conf.ps || iwl_mvm_vif_low_latency(mvmvif) ||
	    mvm->pm_disabled)
		return;

	cmd->flags |= cpu_to_le16(POWER_FLAGS_POWER_MANAGEMENT_ENA_MSK);

	if (vif->bss_conf.beacon_rate &&
	    (vif->bss_conf.beacon_rate->bitrate == 10 ||
	     vif->bss_conf.beacon_rate->bitrate == 60)) {
		cmd->flags |= cpu_to_le16(POWER_FLAGS_LPRX_ENA_MSK);
		cmd->lprx_rssi_threshold = POWER_LPRX_RSSI_THRESHOLD;
	}

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
		cmd->skip_dtim_periods = 3;
	}

	if (mvm->cur_ucode != IWL_UCODE_WOWLAN) {
		cmd->rx_data_timeout =
			cpu_to_le32(IWL_MVM_DEFAULT_PS_RX_DATA_TIMEOUT);
		cmd->tx_data_timeout =
			cpu_to_le32(IWL_MVM_DEFAULT_PS_TX_DATA_TIMEOUT);
	} else {
		cmd->rx_data_timeout =
			cpu_to_le32(IWL_MVM_WOWLAN_PS_RX_DATA_TIMEOUT);
		cmd->tx_data_timeout =
			cpu_to_le32(IWL_MVM_WOWLAN_PS_TX_DATA_TIMEOUT);
	}

	if (!memcmp(mvmvif->uapsd_misbehaving_bssid, vif->bss_conf.bssid,
		    ETH_ALEN))
		allow_uapsd = false;

	if (vif->p2p &&
	    !(mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_P2P_PS_UAPSD))
		allow_uapsd = false;
	/*
	 * Avoid using uAPSD if P2P client is associated to GO that uses
	 * opportunistic power save. This is due to current FW limitation.
	 */
	if (vif->p2p &&
	    vif->bss_conf.p2p_noa_attr.oppps_ctwindow &
	    IEEE80211_P2P_OPPPS_ENABLE_BIT)
		allow_uapsd = false;

	if (allow_uapsd)
		iwl_mvm_power_configure_uapsd(mvm, vif, cmd);

#ifdef CONFIG_IWLWIFI_DEBUGFS
	if (mvmvif->dbgfs_pm.mask & MVM_DEBUGFS_PM_KEEP_ALIVE)
		cmd->keep_alive_seconds =
			cpu_to_le16(mvmvif->dbgfs_pm.keep_alive_seconds);
	if (mvmvif->dbgfs_pm.mask & MVM_DEBUGFS_PM_SKIP_OVER_DTIM) {
		if (mvmvif->dbgfs_pm.skip_over_dtim)
			cmd->flags |=
				cpu_to_le16(POWER_FLAGS_SKIP_OVER_DTIM_MSK);
		else
			cmd->flags &=
				cpu_to_le16(~POWER_FLAGS_SKIP_OVER_DTIM_MSK);
	}
	if (mvmvif->dbgfs_pm.mask & MVM_DEBUGFS_PM_RX_DATA_TIMEOUT)
		cmd->rx_data_timeout =
			cpu_to_le32(mvmvif->dbgfs_pm.rx_data_timeout);
	if (mvmvif->dbgfs_pm.mask & MVM_DEBUGFS_PM_TX_DATA_TIMEOUT)
		cmd->tx_data_timeout =
			cpu_to_le32(mvmvif->dbgfs_pm.tx_data_timeout);
	if (mvmvif->dbgfs_pm.mask & MVM_DEBUGFS_PM_SKIP_DTIM_PERIODS)
		cmd->skip_dtim_periods = mvmvif->dbgfs_pm.skip_dtim_periods;
	if (mvmvif->dbgfs_pm.mask & MVM_DEBUGFS_PM_LPRX_ENA) {
		if (mvmvif->dbgfs_pm.lprx_ena)
			cmd->flags |= cpu_to_le16(POWER_FLAGS_LPRX_ENA_MSK);
		else
			cmd->flags &= cpu_to_le16(~POWER_FLAGS_LPRX_ENA_MSK);
	}
	if (mvmvif->dbgfs_pm.mask & MVM_DEBUGFS_PM_LPRX_RSSI_THRESHOLD)
		cmd->lprx_rssi_threshold = mvmvif->dbgfs_pm.lprx_rssi_threshold;
	if (mvmvif->dbgfs_pm.mask & MVM_DEBUGFS_PM_SNOOZE_ENABLE) {
		if (mvmvif->dbgfs_pm.snooze_ena)
			cmd->flags |=
				cpu_to_le16(POWER_FLAGS_SNOOZE_ENA_MSK);
		else
			cmd->flags &=
				cpu_to_le16(~POWER_FLAGS_SNOOZE_ENA_MSK);
	}
	if (mvmvif->dbgfs_pm.mask & MVM_DEBUGFS_PM_UAPSD_MISBEHAVING) {
		u16 flag = POWER_FLAGS_UAPSD_MISBEHAVING_ENA_MSK;
		if (mvmvif->dbgfs_pm.uapsd_misbehaving)
			cmd->flags |= cpu_to_le16(flag);
		else
			cmd->flags &= cpu_to_le16(flag);
	}
#endif /* CONFIG_IWLWIFI_DEBUGFS */
}

static int iwl_mvm_power_send_cmd(struct iwl_mvm *mvm,
					 struct ieee80211_vif *vif)
{
	struct iwl_mac_power_cmd cmd = {};

	if (vif->type != NL80211_IFTYPE_STATION)
		return 0;

	if (vif->p2p &&
	    !(mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_BSS_P2P_PS_DCM))
		return 0;

	iwl_mvm_power_build_cmd(mvm, vif, &cmd);
	iwl_mvm_power_log(mvm, &cmd);
#ifdef CONFIG_IWLWIFI_DEBUGFS
	memcpy(&iwl_mvm_vif_from_mac80211(vif)->mac_pwr_cmd, &cmd, sizeof(cmd));
#endif

	return iwl_mvm_send_cmd_pdu(mvm, MAC_PM_POWER_TABLE, CMD_SYNC,
				    sizeof(cmd), &cmd);
}

int iwl_mvm_power_update_device(struct iwl_mvm *mvm)
{
	struct iwl_device_power_cmd cmd = {
		.flags = cpu_to_le16(DEVICE_POWER_FLAGS_POWER_SAVE_ENA_MSK),
	};

	if (!(mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_PM_CMD_SUPPORT))
		return 0;

	if (!(mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_DEVICE_PS_CMD))
		return 0;

	if (iwlmvm_mod_params.power_scheme == IWL_POWER_SCHEME_CAM)
		mvm->ps_disabled = true;

	if (mvm->ps_disabled)
		cmd.flags |= cpu_to_le16(DEVICE_POWER_FLAGS_CAM_MSK);

#ifdef CONFIG_IWLWIFI_DEBUGFS
	if ((mvm->cur_ucode == IWL_UCODE_WOWLAN) ? mvm->disable_power_off_d3 :
	    mvm->disable_power_off)
		cmd.flags &=
			cpu_to_le16(~DEVICE_POWER_FLAGS_POWER_SAVE_ENA_MSK);
#endif
	IWL_DEBUG_POWER(mvm,
			"Sending device power command with flags = 0x%X\n",
			cmd.flags);

	return iwl_mvm_send_cmd_pdu(mvm, POWER_TABLE_CMD, CMD_SYNC, sizeof(cmd),
				    &cmd);
}

void iwl_mvm_power_vif_assoc(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (memcmp(vif->bss_conf.bssid, mvmvif->uapsd_misbehaving_bssid,
		   ETH_ALEN))
		memset(mvmvif->uapsd_misbehaving_bssid, 0, ETH_ALEN);
}

static void iwl_mvm_power_uapsd_misbehav_ap_iterator(void *_data, u8 *mac,
						     struct ieee80211_vif *vif)
{
	u8 *ap_sta_id = _data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	/* The ap_sta_id is not expected to change during current association
	 * so no explicit protection is needed
	 */
	if (mvmvif->ap_sta_id == *ap_sta_id)
		memcpy(mvmvif->uapsd_misbehaving_bssid, vif->bss_conf.bssid,
		       ETH_ALEN);
}

int iwl_mvm_power_uapsd_misbehaving_ap_notif(struct iwl_mvm *mvm,
					     struct iwl_rx_cmd_buffer *rxb,
					     struct iwl_device_cmd *cmd)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_uapsd_misbehaving_ap_notif *notif = (void *)pkt->data;
	u8 ap_sta_id = le32_to_cpu(notif->sta_id);

	ieee80211_iterate_active_interfaces_atomic(
		mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
		iwl_mvm_power_uapsd_misbehav_ap_iterator, &ap_sta_id);

	return 0;
}

struct iwl_power_constraint {
	struct ieee80211_vif *bf_vif;
	struct ieee80211_vif *bss_vif;
	struct ieee80211_vif *p2p_vif;
	u16 bss_phyctx_id;
	u16 p2p_phyctx_id;
	bool pm_disabled;
	bool ps_disabled;
	struct iwl_mvm *mvm;
};

static void iwl_mvm_power_iterator(void *_data, u8 *mac,
				   struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_power_constraint *power_iterator = _data;
	struct iwl_mvm *mvm = power_iterator->mvm;

	switch (ieee80211_vif_type_p2p(vif)) {
	case NL80211_IFTYPE_P2P_DEVICE:
		break;

	case NL80211_IFTYPE_P2P_GO:
	case NL80211_IFTYPE_AP:
		/* no BSS power mgmt if we have an active AP */
		if (mvmvif->ap_ibss_active)
			power_iterator->pm_disabled = true;
		break;

	case NL80211_IFTYPE_MONITOR:
		/* no BSS power mgmt and no device power save */
		power_iterator->pm_disabled = true;
		power_iterator->ps_disabled = true;
		break;

	case NL80211_IFTYPE_P2P_CLIENT:
		if (mvmvif->phy_ctxt)
			power_iterator->p2p_phyctx_id = mvmvif->phy_ctxt->id;

		/* we should have only one P2P vif */
		WARN_ON(power_iterator->p2p_vif);
		power_iterator->p2p_vif = vif;

		IWL_DEBUG_POWER(mvm, "p2p: p2p_id=%d, bss_id=%d\n",
				power_iterator->p2p_phyctx_id,
				power_iterator->bss_phyctx_id);
		if (!(mvm->fw->ucode_capa.flags &
		      IWL_UCODE_TLV_FLAGS_BSS_P2P_PS_DCM)) {
			/* no BSS power mgmt if we have a P2P client*/
			power_iterator->pm_disabled = true;
		} else if (power_iterator->p2p_phyctx_id < MAX_PHYS &&
			   power_iterator->bss_phyctx_id < MAX_PHYS &&
			   power_iterator->p2p_phyctx_id ==
			   power_iterator->bss_phyctx_id) {
			power_iterator->pm_disabled = true;
		}
		break;

	case NL80211_IFTYPE_STATION:
		if (mvmvif->phy_ctxt)
			power_iterator->bss_phyctx_id = mvmvif->phy_ctxt->id;

		/* we should have only one BSS vif */
		WARN_ON(power_iterator->bss_vif);
		power_iterator->bss_vif = vif;

		if (mvmvif->bf_data.bf_enabled &&
		    !WARN_ON(power_iterator->bf_vif))
			power_iterator->bf_vif = vif;

		IWL_DEBUG_POWER(mvm, "bss: p2p_id=%d, bss_id=%d\n",
				power_iterator->p2p_phyctx_id,
				power_iterator->bss_phyctx_id);
		if (mvm->fw->ucode_capa.flags &
		    IWL_UCODE_TLV_FLAGS_BSS_P2P_PS_DCM &&
			(power_iterator->p2p_phyctx_id < MAX_PHYS &&
			 power_iterator->bss_phyctx_id < MAX_PHYS &&
			 power_iterator->p2p_phyctx_id ==
			 power_iterator->bss_phyctx_id))
			power_iterator->pm_disabled = true;
		break;

	default:
		break;
	}
}

static void
iwl_mvm_power_get_global_constraint(struct iwl_mvm *mvm,
				    struct iwl_power_constraint *constraint)
{
	lockdep_assert_held(&mvm->mutex);

	if (iwlmvm_mod_params.power_scheme == IWL_POWER_SCHEME_CAM) {
		constraint->pm_disabled = true;
		constraint->ps_disabled = true;
	}

	ieee80211_iterate_active_interfaces_atomic(mvm->hw,
					    IEEE80211_IFACE_ITER_NORMAL,
					    iwl_mvm_power_iterator, constraint);
}

int iwl_mvm_power_update_mac(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_power_constraint constraint = {
		    .p2p_phyctx_id = MAX_PHYS,
		    .bss_phyctx_id = MAX_PHYS,
		    .mvm = mvm,
	};
	bool ba_enable;
	int ret;

	lockdep_assert_held(&mvm->mutex);

	if (!(mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_PM_CMD_SUPPORT))
		return 0;

	iwl_mvm_power_get_global_constraint(mvm, &constraint);
	mvm->ps_disabled = constraint.ps_disabled;
	mvm->pm_disabled = constraint.pm_disabled;

	/* don't update device power state unless we add / remove monitor */
	if (vif->type == NL80211_IFTYPE_MONITOR) {
		ret = iwl_mvm_power_update_device(mvm);
		if (ret)
			return ret;
	}

	if (constraint.bss_vif) {
		ret = iwl_mvm_power_send_cmd(mvm, constraint.bss_vif);
		if (ret)
			return ret;
	}

	if (constraint.p2p_vif) {
		ret = iwl_mvm_power_send_cmd(mvm, constraint.p2p_vif);
		if (ret)
			return ret;
	}

	if (!constraint.bf_vif)
		return 0;

	vif = constraint.bf_vif;
	mvmvif = iwl_mvm_vif_from_mac80211(vif);

	ba_enable = !(constraint.pm_disabled || constraint.ps_disabled ||
		      !vif->bss_conf.ps || iwl_mvm_vif_low_latency(mvmvif));

	return iwl_mvm_update_beacon_abort(mvm, constraint.bf_vif, ba_enable);
}

#ifdef CONFIG_IWLWIFI_DEBUGFS
int iwl_mvm_power_mac_dbgfs_read(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif, char *buf,
				 int bufsz)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mac_power_cmd cmd = {};
	int pos = 0;

	if (WARN_ON(!(mvm->fw->ucode_capa.flags &
		      IWL_UCODE_TLV_FLAGS_PM_CMD_SUPPORT)))
		return 0;

	mutex_lock(&mvm->mutex);
	memcpy(&cmd, &mvmvif->mac_pwr_cmd, sizeof(cmd));
	mutex_unlock(&mvm->mutex);

	if (!(mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_DEVICE_PS_CMD))
		pos += scnprintf(buf+pos, bufsz-pos, "disable_power_off = %d\n",
				 (cmd.flags &
				 cpu_to_le16(POWER_FLAGS_POWER_SAVE_ENA_MSK)) ?
				 0 : 1);
	pos += scnprintf(buf+pos, bufsz-pos, "power_scheme = %d\n",
			 iwlmvm_mod_params.power_scheme);
	pos += scnprintf(buf+pos, bufsz-pos, "flags = 0x%x\n",
			 le16_to_cpu(cmd.flags));
	pos += scnprintf(buf+pos, bufsz-pos, "keep_alive = %d\n",
			 le16_to_cpu(cmd.keep_alive_seconds));

	if (!(cmd.flags & cpu_to_le16(POWER_FLAGS_POWER_MANAGEMENT_ENA_MSK)))
		return pos;

	pos += scnprintf(buf+pos, bufsz-pos, "skip_over_dtim = %d\n",
			 (cmd.flags &
			 cpu_to_le16(POWER_FLAGS_SKIP_OVER_DTIM_MSK)) ? 1 : 0);
	pos += scnprintf(buf+pos, bufsz-pos, "skip_dtim_periods = %d\n",
			 cmd.skip_dtim_periods);
	if (!(cmd.flags & cpu_to_le16(POWER_FLAGS_ADVANCE_PM_ENA_MSK))) {
		pos += scnprintf(buf+pos, bufsz-pos, "rx_data_timeout = %d\n",
				 le32_to_cpu(cmd.rx_data_timeout));
		pos += scnprintf(buf+pos, bufsz-pos, "tx_data_timeout = %d\n",
				 le32_to_cpu(cmd.tx_data_timeout));
	}
	if (cmd.flags & cpu_to_le16(POWER_FLAGS_LPRX_ENA_MSK))
		pos += scnprintf(buf+pos, bufsz-pos,
				 "lprx_rssi_threshold = %d\n",
				 cmd.lprx_rssi_threshold);

	if (!(cmd.flags & cpu_to_le16(POWER_FLAGS_ADVANCE_PM_ENA_MSK)))
		return pos;

	pos += scnprintf(buf+pos, bufsz-pos, "rx_data_timeout_uapsd = %d\n",
			 le32_to_cpu(cmd.rx_data_timeout_uapsd));
	pos += scnprintf(buf+pos, bufsz-pos, "tx_data_timeout_uapsd = %d\n",
			 le32_to_cpu(cmd.tx_data_timeout_uapsd));
	pos += scnprintf(buf+pos, bufsz-pos, "qndp_tid = %d\n", cmd.qndp_tid);
	pos += scnprintf(buf+pos, bufsz-pos, "uapsd_ac_flags = 0x%x\n",
			 cmd.uapsd_ac_flags);
	pos += scnprintf(buf+pos, bufsz-pos, "uapsd_max_sp = %d\n",
			 cmd.uapsd_max_sp);
	pos += scnprintf(buf+pos, bufsz-pos, "heavy_tx_thld_packets = %d\n",
			 cmd.heavy_tx_thld_packets);
	pos += scnprintf(buf+pos, bufsz-pos, "heavy_rx_thld_packets = %d\n",
			 cmd.heavy_rx_thld_packets);
	pos += scnprintf(buf+pos, bufsz-pos, "heavy_tx_thld_percentage = %d\n",
			 cmd.heavy_tx_thld_percentage);
	pos += scnprintf(buf+pos, bufsz-pos, "heavy_rx_thld_percentage = %d\n",
			 cmd.heavy_rx_thld_percentage);
	pos += scnprintf(buf+pos, bufsz-pos, "uapsd_misbehaving_enable = %d\n",
			 (cmd.flags &
			  cpu_to_le16(POWER_FLAGS_UAPSD_MISBEHAVING_ENA_MSK)) ?
			 1 : 0);

	if (!(cmd.flags & cpu_to_le16(POWER_FLAGS_SNOOZE_ENA_MSK)))
		return pos;

	pos += scnprintf(buf+pos, bufsz-pos, "snooze_interval = %d\n",
			 cmd.snooze_interval);
	pos += scnprintf(buf+pos, bufsz-pos, "snooze_window = %d\n",
			 cmd.snooze_window);

	return pos;
}

void
iwl_mvm_beacon_filter_debugfs_parameters(struct ieee80211_vif *vif,
					 struct iwl_beacon_filter_cmd *cmd)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_dbgfs_bf *dbgfs_bf = &mvmvif->dbgfs_bf;

	if (dbgfs_bf->mask & MVM_DEBUGFS_BF_ENERGY_DELTA)
		cmd->bf_energy_delta = cpu_to_le32(dbgfs_bf->bf_energy_delta);
	if (dbgfs_bf->mask & MVM_DEBUGFS_BF_ROAMING_ENERGY_DELTA)
		cmd->bf_roaming_energy_delta =
				cpu_to_le32(dbgfs_bf->bf_roaming_energy_delta);
	if (dbgfs_bf->mask & MVM_DEBUGFS_BF_ROAMING_STATE)
		cmd->bf_roaming_state = cpu_to_le32(dbgfs_bf->bf_roaming_state);
	if (dbgfs_bf->mask & MVM_DEBUGFS_BF_TEMP_THRESHOLD)
		cmd->bf_temp_threshold =
				cpu_to_le32(dbgfs_bf->bf_temp_threshold);
	if (dbgfs_bf->mask & MVM_DEBUGFS_BF_TEMP_FAST_FILTER)
		cmd->bf_temp_fast_filter =
				cpu_to_le32(dbgfs_bf->bf_temp_fast_filter);
	if (dbgfs_bf->mask & MVM_DEBUGFS_BF_TEMP_SLOW_FILTER)
		cmd->bf_temp_slow_filter =
				cpu_to_le32(dbgfs_bf->bf_temp_slow_filter);
	if (dbgfs_bf->mask & MVM_DEBUGFS_BF_DEBUG_FLAG)
		cmd->bf_debug_flag = cpu_to_le32(dbgfs_bf->bf_debug_flag);
	if (dbgfs_bf->mask & MVM_DEBUGFS_BF_ESCAPE_TIMER)
		cmd->bf_escape_timer = cpu_to_le32(dbgfs_bf->bf_escape_timer);
	if (dbgfs_bf->mask & MVM_DEBUGFS_BA_ESCAPE_TIMER)
		cmd->ba_escape_timer = cpu_to_le32(dbgfs_bf->ba_escape_timer);
	if (dbgfs_bf->mask & MVM_DEBUGFS_BA_ENABLE_BEACON_ABORT)
		cmd->ba_enable_beacon_abort =
				cpu_to_le32(dbgfs_bf->ba_enable_beacon_abort);
}
#endif

static int _iwl_mvm_enable_beacon_filter(struct iwl_mvm *mvm,
					 struct ieee80211_vif *vif,
					 struct iwl_beacon_filter_cmd *cmd,
					 u32 cmd_flags,
					 bool d0i3)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	if (mvmvif != mvm->bf_allowed_vif ||
	    vif->type != NL80211_IFTYPE_STATION || vif->p2p)
		return 0;

	iwl_mvm_beacon_filter_set_cqm_params(mvm, vif, cmd);
	if (!d0i3)
		iwl_mvm_beacon_filter_debugfs_parameters(vif, cmd);
	ret = iwl_mvm_beacon_filter_send_cmd(mvm, cmd, cmd_flags);

	/* don't change bf_enabled in case of temporary d0i3 configuration */
	if (!ret && !d0i3)
		mvmvif->bf_data.bf_enabled = true;

	return ret;
}

int iwl_mvm_enable_beacon_filter(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif,
				 u32 flags)
{
	struct iwl_beacon_filter_cmd cmd = {
		IWL_BF_CMD_CONFIG_DEFAULTS,
		.bf_enable_beacon_filter = cpu_to_le32(1),
	};

	return _iwl_mvm_enable_beacon_filter(mvm, vif, &cmd, flags, false);
}

int iwl_mvm_disable_beacon_filter(struct iwl_mvm *mvm,
				  struct ieee80211_vif *vif,
				  u32 flags)
{
	struct iwl_beacon_filter_cmd cmd = {};
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	if (!(mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_BF_UPDATED) ||
	    vif->type != NL80211_IFTYPE_STATION || vif->p2p)
		return 0;

	ret = iwl_mvm_beacon_filter_send_cmd(mvm, &cmd, flags);

	if (!ret)
		mvmvif->bf_data.bf_enabled = false;

	return ret;
}

int iwl_mvm_update_d0i3_power_mode(struct iwl_mvm *mvm,
				   struct ieee80211_vif *vif,
				   bool enable, u32 flags)
{
	int ret;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mac_power_cmd cmd = {};

	if (vif->type != NL80211_IFTYPE_STATION || vif->p2p)
		return 0;

	if (!vif->bss_conf.assoc)
		return 0;

	iwl_mvm_power_build_cmd(mvm, vif, &cmd);
	if (enable) {
		/* configure skip over dtim up to 300 msec */
		int dtimper = mvm->hw->conf.ps_dtim_period ?: 1;
		int dtimper_msec = dtimper * vif->bss_conf.beacon_int;

		if (WARN_ON(!dtimper_msec))
			return 0;

		cmd.flags |=
			cpu_to_le16(POWER_FLAGS_SKIP_OVER_DTIM_MSK);
		cmd.skip_dtim_periods = 300 / dtimper_msec;
	}
	iwl_mvm_power_log(mvm, &cmd);
#ifdef CONFIG_IWLWIFI_DEBUGFS
	memcpy(&mvmvif->mac_pwr_cmd, &cmd, sizeof(cmd));
#endif
	ret = iwl_mvm_send_cmd_pdu(mvm, MAC_PM_POWER_TABLE, flags,
				   sizeof(cmd), &cmd);
	if (ret)
		return ret;

	/* configure beacon filtering */
	if (mvmvif != mvm->bf_allowed_vif)
		return 0;

	if (enable) {
		struct iwl_beacon_filter_cmd cmd_bf = {
			IWL_BF_CMD_CONFIG_D0I3,
			.bf_enable_beacon_filter = cpu_to_le32(1),
		};
		ret = _iwl_mvm_enable_beacon_filter(mvm, vif, &cmd_bf,
						    flags, true);
	} else {
		if (mvmvif->bf_data.bf_enabled)
			ret = iwl_mvm_enable_beacon_filter(mvm, vif, flags);
		else
			ret = iwl_mvm_disable_beacon_filter(mvm, vif, flags);
	}

	return ret;
}

int iwl_mvm_update_beacon_filter(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif,
				 bool force,
				 u32 flags)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (mvmvif != mvm->bf_allowed_vif)
		return 0;

	if (!mvmvif->bf_data.bf_enabled) {
		/* disable beacon filtering explicitly if force is true */
		if (force)
			return iwl_mvm_disable_beacon_filter(mvm, vif, flags);
		return 0;
	}

	return iwl_mvm_enable_beacon_filter(mvm, vif, flags);
}

int iwl_power_legacy_set_cam_mode(struct iwl_mvm *mvm)
{
	struct iwl_powertable_cmd cmd = {
		.keep_alive_seconds = POWER_KEEP_ALIVE_PERIOD_SEC,
	};

	return iwl_mvm_send_cmd_pdu(mvm, POWER_TABLE_CMD, CMD_SYNC,
				    sizeof(cmd), &cmd);
}
