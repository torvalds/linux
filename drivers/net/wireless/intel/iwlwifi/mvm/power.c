/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
 * Copyright(c) 2015 - 2016 Intel Deutschland GmbH
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
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
 * Copyright(c) 2015 - 2016 Intel Deutschland GmbH
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
#include <linux/etherdevice.h>

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
					  struct iwl_beacon_filter_cmd *cmd,
					  bool d0i3)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (vif->bss_conf.cqm_rssi_thold && !d0i3) {
		cmd->bf_energy_delta =
			cpu_to_le32(vif->bss_conf.cqm_rssi_hyst);
		/* fw uses an absolute value for this */
		cmd->bf_roaming_state =
			cpu_to_le32(-vif->bss_conf.cqm_rssi_thold);
	}
	cmd->ba_enable_beacon_abort = cpu_to_le32(mvmvif->bf_data.ba_enabled);
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

	if (!(cmd->flags & cpu_to_le16(POWER_FLAGS_ADVANCE_PM_ENA_MSK))) {
#ifdef CONFIG_IWLWIFI_DEBUGFS
		/* set advanced pm flag with no uapsd ACs to enable ps-poll */
		if (mvmvif->dbgfs_pm.use_ps_poll)
			cmd->flags |=
				cpu_to_le16(POWER_FLAGS_ADVANCE_PM_ENA_MSK);
#endif
		return;
	}

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

	cmd->uapsd_max_sp = mvm->hw->uapsd_max_sp_len;

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

static void iwl_mvm_p2p_standalone_iterator(void *_data, u8 *mac,
					    struct ieee80211_vif *vif)
{
	bool *is_p2p_standalone = _data;

	switch (ieee80211_vif_type_p2p(vif)) {
	case NL80211_IFTYPE_P2P_GO:
	case NL80211_IFTYPE_AP:
		*is_p2p_standalone = false;
		break;
	case NL80211_IFTYPE_STATION:
		if (vif->bss_conf.assoc)
			*is_p2p_standalone = false;
		break;

	default:
		break;
	}
}

static bool iwl_mvm_power_allow_uapsd(struct iwl_mvm *mvm,
				       struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (!memcmp(mvmvif->uapsd_misbehaving_bssid, vif->bss_conf.bssid,
		    ETH_ALEN))
		return false;

	/*
	 * Avoid using uAPSD if P2P client is associated to GO that uses
	 * opportunistic power save. This is due to current FW limitation.
	 */
	if (vif->p2p &&
	    (vif->bss_conf.p2p_noa_attr.oppps_ctwindow &
	    IEEE80211_P2P_OPPPS_ENABLE_BIT))
		return false;

	/*
	 * Avoid using uAPSD if client is in DCM -
	 * low latency issue in Miracast
	 */
	if (iwl_mvm_phy_ctx_count(mvm) >= 2)
		return false;

	if (vif->p2p) {
		/* Allow U-APSD only if p2p is stand alone */
		bool is_p2p_standalone = true;

		if (!iwl_mvm_is_p2p_scm_uapsd_supported(mvm))
			return false;

		ieee80211_iterate_active_interfaces_atomic(mvm->hw,
					IEEE80211_IFACE_ITER_NORMAL,
					iwl_mvm_p2p_standalone_iterator,
					&is_p2p_standalone);

		if (!is_p2p_standalone)
			return false;
	}

	return true;
}

static bool iwl_mvm_power_is_radar(struct ieee80211_vif *vif)
{
	struct ieee80211_chanctx_conf *chanctx_conf;
	struct ieee80211_channel *chan;
	bool radar_detect = false;

	rcu_read_lock();
	chanctx_conf = rcu_dereference(vif->chanctx_conf);
	WARN_ON(!chanctx_conf);
	if (chanctx_conf) {
		chan = chanctx_conf->def.chan;
		radar_detect = chan->flags & IEEE80211_CHAN_RADAR;
	}
	rcu_read_unlock();

	return radar_detect;
}

static void iwl_mvm_power_config_skip_dtim(struct iwl_mvm *mvm,
					   struct ieee80211_vif *vif,
					   struct iwl_mac_power_cmd *cmd,
					   bool host_awake)
{
	int dtimper = vif->bss_conf.dtim_period ?: 1;
	int skip;

	/* disable, in case we're supposed to override */
	cmd->skip_dtim_periods = 0;
	cmd->flags &= ~cpu_to_le16(POWER_FLAGS_SKIP_OVER_DTIM_MSK);

	if (iwl_mvm_power_is_radar(vif))
		return;

	if (dtimper >= 10)
		return;

	/* TODO: check that multicast wake lock is off */

	if (host_awake) {
		if (iwlmvm_mod_params.power_scheme != IWL_POWER_SCHEME_LP)
			return;
		skip = 2;
	} else {
		int dtimper_tu = dtimper * vif->bss_conf.beacon_int;

		if (WARN_ON(!dtimper_tu))
			return;
		/* configure skip over dtim up to 306TU - 314 msec */
		skip = max_t(u8, 1, 306 / dtimper_tu);
	}

	/* the firmware really expects "look at every X DTIMs", so add 1 */
	cmd->skip_dtim_periods = 1 + skip;
	cmd->flags |= cpu_to_le16(POWER_FLAGS_SKIP_OVER_DTIM_MSK);
}

static void iwl_mvm_power_build_cmd(struct iwl_mvm *mvm,
				    struct ieee80211_vif *vif,
				    struct iwl_mac_power_cmd *cmd,
				    bool host_awake)
{
	int dtimper, bi;
	int keep_alive;
	struct iwl_mvm_vif *mvmvif __maybe_unused =
		iwl_mvm_vif_from_mac80211(vif);

	cmd->id_and_color = cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id,
							    mvmvif->color));
	dtimper = vif->bss_conf.dtim_period;
	bi = vif->bss_conf.beacon_int;

	/*
	 * Regardless of power management state the driver must set
	 * keep alive period. FW will use it for sending keep alive NDPs
	 * immediately after association. Check that keep alive period
	 * is at least 3 * DTIM
	 */
	keep_alive = DIV_ROUND_UP(ieee80211_tu_to_usec(3 * dtimper * bi),
				  USEC_PER_SEC);
	keep_alive = max(keep_alive, POWER_KEEP_ALIVE_PERIOD_SEC);
	cmd->keep_alive_seconds = cpu_to_le16(keep_alive);

	if (mvm->ps_disabled)
		return;

	cmd->flags |= cpu_to_le16(POWER_FLAGS_POWER_SAVE_ENA_MSK);

	if (!vif->bss_conf.ps || !mvmvif->pm_enabled)
		return;

	if (iwl_mvm_vif_low_latency(mvmvif) && vif->p2p &&
	    (!fw_has_capa(&mvm->fw->ucode_capa,
			 IWL_UCODE_TLV_CAPA_SHORT_PM_TIMEOUTS) ||
	     !IWL_MVM_P2P_LOWLATENCY_PS_ENABLE))
		return;

	cmd->flags |= cpu_to_le16(POWER_FLAGS_POWER_MANAGEMENT_ENA_MSK);

	if (vif->bss_conf.beacon_rate &&
	    (vif->bss_conf.beacon_rate->bitrate == 10 ||
	     vif->bss_conf.beacon_rate->bitrate == 60)) {
		cmd->flags |= cpu_to_le16(POWER_FLAGS_LPRX_ENA_MSK);
		cmd->lprx_rssi_threshold = POWER_LPRX_RSSI_THRESHOLD;
	}

	iwl_mvm_power_config_skip_dtim(mvm, vif, cmd, host_awake);

	if (!host_awake) {
		cmd->rx_data_timeout =
			cpu_to_le32(IWL_MVM_WOWLAN_PS_RX_DATA_TIMEOUT);
		cmd->tx_data_timeout =
			cpu_to_le32(IWL_MVM_WOWLAN_PS_TX_DATA_TIMEOUT);
	} else if (iwl_mvm_vif_low_latency(mvmvif) && vif->p2p &&
		   fw_has_capa(&mvm->fw->ucode_capa,
			       IWL_UCODE_TLV_CAPA_SHORT_PM_TIMEOUTS)) {
		cmd->tx_data_timeout =
			cpu_to_le32(IWL_MVM_SHORT_PS_TX_DATA_TIMEOUT);
		cmd->rx_data_timeout =
			cpu_to_le32(IWL_MVM_SHORT_PS_RX_DATA_TIMEOUT);
	} else {
		cmd->rx_data_timeout =
			cpu_to_le32(IWL_MVM_DEFAULT_PS_RX_DATA_TIMEOUT);
		cmd->tx_data_timeout =
			cpu_to_le32(IWL_MVM_DEFAULT_PS_TX_DATA_TIMEOUT);
	}

	if (iwl_mvm_power_allow_uapsd(mvm, vif))
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

	iwl_mvm_power_build_cmd(mvm, vif, &cmd,
				mvm->cur_ucode != IWL_UCODE_WOWLAN);
	iwl_mvm_power_log(mvm, &cmd);
#ifdef CONFIG_IWLWIFI_DEBUGFS
	memcpy(&iwl_mvm_vif_from_mac80211(vif)->mac_pwr_cmd, &cmd, sizeof(cmd));
#endif

	return iwl_mvm_send_cmd_pdu(mvm, MAC_PM_POWER_TABLE, 0,
				    sizeof(cmd), &cmd);
}

int iwl_mvm_power_update_device(struct iwl_mvm *mvm)
{
	struct iwl_device_power_cmd cmd = {
		.flags = 0,
	};

	if (iwlmvm_mod_params.power_scheme == IWL_POWER_SCHEME_CAM)
		mvm->ps_disabled = true;

	if (!mvm->ps_disabled)
		cmd.flags |= cpu_to_le16(DEVICE_POWER_FLAGS_POWER_SAVE_ENA_MSK);

#ifdef CONFIG_IWLWIFI_DEBUGFS
	if ((mvm->cur_ucode == IWL_UCODE_WOWLAN) ? mvm->disable_power_off_d3 :
	    mvm->disable_power_off)
		cmd.flags &=
			cpu_to_le16(~DEVICE_POWER_FLAGS_POWER_SAVE_ENA_MSK);
#endif
	IWL_DEBUG_POWER(mvm,
			"Sending device power command with flags = 0x%X\n",
			cmd.flags);

	return iwl_mvm_send_cmd_pdu(mvm, POWER_TABLE_CMD, 0, sizeof(cmd),
				    &cmd);
}

void iwl_mvm_power_vif_assoc(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (memcmp(vif->bss_conf.bssid, mvmvif->uapsd_misbehaving_bssid,
		   ETH_ALEN))
		eth_zero_addr(mvmvif->uapsd_misbehaving_bssid);
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

void iwl_mvm_power_uapsd_misbehaving_ap_notif(struct iwl_mvm *mvm,
					      struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_uapsd_misbehaving_ap_notif *notif = (void *)pkt->data;
	u8 ap_sta_id = le32_to_cpu(notif->sta_id);

	ieee80211_iterate_active_interfaces_atomic(
		mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
		iwl_mvm_power_uapsd_misbehav_ap_iterator, &ap_sta_id);
}

struct iwl_power_vifs {
	struct iwl_mvm *mvm;
	struct ieee80211_vif *bss_vif;
	struct ieee80211_vif *p2p_vif;
	struct ieee80211_vif *ap_vif;
	struct ieee80211_vif *monitor_vif;
	bool p2p_active;
	bool bss_active;
	bool ap_active;
	bool monitor_active;
};

static void iwl_mvm_power_disable_pm_iterator(void *_data, u8* mac,
					      struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	mvmvif->pm_enabled = false;
}

static void iwl_mvm_power_ps_disabled_iterator(void *_data, u8* mac,
					       struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	bool *disable_ps = _data;

	if (mvmvif->phy_ctxt)
		if (mvmvif->phy_ctxt->id < MAX_PHYS)
			*disable_ps |= mvmvif->ps_disabled;
}

static void iwl_mvm_power_get_vifs_iterator(void *_data, u8 *mac,
					    struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_power_vifs *power_iterator = _data;

	switch (ieee80211_vif_type_p2p(vif)) {
	case NL80211_IFTYPE_P2P_DEVICE:
		break;

	case NL80211_IFTYPE_P2P_GO:
	case NL80211_IFTYPE_AP:
		/* only a single MAC of the same type */
		WARN_ON(power_iterator->ap_vif);
		power_iterator->ap_vif = vif;
		if (mvmvif->phy_ctxt)
			if (mvmvif->phy_ctxt->id < MAX_PHYS)
				power_iterator->ap_active = true;
		break;

	case NL80211_IFTYPE_MONITOR:
		/* only a single MAC of the same type */
		WARN_ON(power_iterator->monitor_vif);
		power_iterator->monitor_vif = vif;
		if (mvmvif->phy_ctxt)
			if (mvmvif->phy_ctxt->id < MAX_PHYS)
				power_iterator->monitor_active = true;
		break;

	case NL80211_IFTYPE_P2P_CLIENT:
		/* only a single MAC of the same type */
		WARN_ON(power_iterator->p2p_vif);
		power_iterator->p2p_vif = vif;
		if (mvmvif->phy_ctxt)
			if (mvmvif->phy_ctxt->id < MAX_PHYS)
				power_iterator->p2p_active = true;
		break;

	case NL80211_IFTYPE_STATION:
		power_iterator->bss_vif = vif;
		if (mvmvif->phy_ctxt)
			if (mvmvif->phy_ctxt->id < MAX_PHYS)
				power_iterator->bss_active = true;
		break;

	default:
		break;
	}
}

static void iwl_mvm_power_set_pm(struct iwl_mvm *mvm,
				 struct iwl_power_vifs *vifs)
{
	struct iwl_mvm_vif *bss_mvmvif = NULL;
	struct iwl_mvm_vif *p2p_mvmvif = NULL;
	struct iwl_mvm_vif *ap_mvmvif = NULL;
	bool client_same_channel = false;
	bool ap_same_channel = false;

	lockdep_assert_held(&mvm->mutex);

	/* set pm_enable to false */
	ieee80211_iterate_active_interfaces_atomic(mvm->hw,
					IEEE80211_IFACE_ITER_NORMAL,
					iwl_mvm_power_disable_pm_iterator,
					NULL);

	if (vifs->bss_vif)
		bss_mvmvif = iwl_mvm_vif_from_mac80211(vifs->bss_vif);

	if (vifs->p2p_vif)
		p2p_mvmvif = iwl_mvm_vif_from_mac80211(vifs->p2p_vif);

	if (vifs->ap_vif)
		ap_mvmvif = iwl_mvm_vif_from_mac80211(vifs->ap_vif);

	/* don't allow PM if any TDLS stations exist */
	if (iwl_mvm_tdls_sta_count(mvm, NULL))
		return;

	/* enable PM on bss if bss stand alone */
	if (vifs->bss_active && !vifs->p2p_active && !vifs->ap_active) {
		bss_mvmvif->pm_enabled = true;
		return;
	}

	/* enable PM on p2p if p2p stand alone */
	if (vifs->p2p_active && !vifs->bss_active && !vifs->ap_active) {
		if (mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_P2P_PM)
			p2p_mvmvif->pm_enabled = true;
		return;
	}

	if (vifs->bss_active && vifs->p2p_active)
		client_same_channel = (bss_mvmvif->phy_ctxt->id ==
				       p2p_mvmvif->phy_ctxt->id);
	if (vifs->bss_active && vifs->ap_active)
		ap_same_channel = (bss_mvmvif->phy_ctxt->id ==
				   ap_mvmvif->phy_ctxt->id);

	/* clients are not stand alone: enable PM if DCM */
	if (!(client_same_channel || ap_same_channel) &&
	    (mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_BSS_P2P_PS_DCM)) {
		if (vifs->bss_active)
			bss_mvmvif->pm_enabled = true;
		if (vifs->p2p_active &&
		    (mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_P2P_PM))
			p2p_mvmvif->pm_enabled = true;
		return;
	}

	/*
	 * There is only one channel in the system and there are only
	 * bss and p2p clients that share it
	 */
	if (client_same_channel && !vifs->ap_active &&
	    (mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_BSS_P2P_PS_SCM)) {
		/* share same channel*/
		bss_mvmvif->pm_enabled = true;
		if (mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_P2P_PM)
			p2p_mvmvif->pm_enabled = true;
	}
}

#ifdef CONFIG_IWLWIFI_DEBUGFS
int iwl_mvm_power_mac_dbgfs_read(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif, char *buf,
				 int bufsz)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mac_power_cmd cmd = {};
	int pos = 0;

	mutex_lock(&mvm->mutex);
	memcpy(&cmd, &mvmvif->mac_pwr_cmd, sizeof(cmd));
	mutex_unlock(&mvm->mutex);

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

	if (mvmvif != mvm->bf_allowed_vif || !vif->bss_conf.dtim_period ||
	    vif->type != NL80211_IFTYPE_STATION || vif->p2p)
		return 0;

	iwl_mvm_beacon_filter_set_cqm_params(mvm, vif, cmd, d0i3);
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

static int _iwl_mvm_disable_beacon_filter(struct iwl_mvm *mvm,
					  struct ieee80211_vif *vif,
					  u32 flags, bool d0i3)
{
	struct iwl_beacon_filter_cmd cmd = {};
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	if (vif->type != NL80211_IFTYPE_STATION || vif->p2p)
		return 0;

	ret = iwl_mvm_beacon_filter_send_cmd(mvm, &cmd, flags);

	/* don't change bf_enabled in case of temporary d0i3 configuration */
	if (!ret && !d0i3)
		mvmvif->bf_data.bf_enabled = false;

	return ret;
}

int iwl_mvm_disable_beacon_filter(struct iwl_mvm *mvm,
				  struct ieee80211_vif *vif,
				  u32 flags)
{
	return _iwl_mvm_disable_beacon_filter(mvm, vif, flags, false);
}

static int iwl_mvm_power_set_ps(struct iwl_mvm *mvm)
{
	bool disable_ps;
	int ret;

	/* disable PS if CAM */
	disable_ps = (iwlmvm_mod_params.power_scheme == IWL_POWER_SCHEME_CAM);
	/* ...or if any of the vifs require PS to be off */
	ieee80211_iterate_active_interfaces_atomic(mvm->hw,
					IEEE80211_IFACE_ITER_NORMAL,
					iwl_mvm_power_ps_disabled_iterator,
					&disable_ps);

	/* update device power state if it has changed */
	if (mvm->ps_disabled != disable_ps) {
		bool old_ps_disabled = mvm->ps_disabled;

		mvm->ps_disabled = disable_ps;
		ret = iwl_mvm_power_update_device(mvm);
		if (ret) {
			mvm->ps_disabled = old_ps_disabled;
			return ret;
		}
	}

	return 0;
}

static int iwl_mvm_power_set_ba(struct iwl_mvm *mvm,
				struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_beacon_filter_cmd cmd = {
		IWL_BF_CMD_CONFIG_DEFAULTS,
		.bf_enable_beacon_filter = cpu_to_le32(1),
	};

	if (!mvmvif->bf_data.bf_enabled)
		return 0;

	if (mvm->cur_ucode == IWL_UCODE_WOWLAN)
		cmd.ba_escape_timer = cpu_to_le32(IWL_BA_ESCAPE_TIMER_D3);

	mvmvif->bf_data.ba_enabled = !(!mvmvif->pm_enabled ||
				       mvm->ps_disabled ||
				       !vif->bss_conf.ps ||
				       iwl_mvm_vif_low_latency(mvmvif));

	return _iwl_mvm_enable_beacon_filter(mvm, vif, &cmd, 0, false);
}

int iwl_mvm_power_update_ps(struct iwl_mvm *mvm)
{
	struct iwl_power_vifs vifs = {
		.mvm = mvm,
	};
	int ret;

	lockdep_assert_held(&mvm->mutex);

	/* get vifs info */
	ieee80211_iterate_active_interfaces_atomic(mvm->hw,
					IEEE80211_IFACE_ITER_NORMAL,
					iwl_mvm_power_get_vifs_iterator, &vifs);

	ret = iwl_mvm_power_set_ps(mvm);
	if (ret)
		return ret;

	if (vifs.bss_vif)
		return iwl_mvm_power_set_ba(mvm, vifs.bss_vif);

	return 0;
}

int iwl_mvm_power_update_mac(struct iwl_mvm *mvm)
{
	struct iwl_power_vifs vifs = {
		.mvm = mvm,
	};
	int ret;

	lockdep_assert_held(&mvm->mutex);

	/* get vifs info */
	ieee80211_iterate_active_interfaces_atomic(mvm->hw,
					IEEE80211_IFACE_ITER_NORMAL,
					iwl_mvm_power_get_vifs_iterator, &vifs);

	iwl_mvm_power_set_pm(mvm, &vifs);

	ret = iwl_mvm_power_set_ps(mvm);
	if (ret)
		return ret;

	if (vifs.bss_vif) {
		ret = iwl_mvm_power_send_cmd(mvm, vifs.bss_vif);
		if (ret)
			return ret;
	}

	if (vifs.p2p_vif) {
		ret = iwl_mvm_power_send_cmd(mvm, vifs.p2p_vif);
		if (ret)
			return ret;
	}

	if (vifs.bss_vif)
		return iwl_mvm_power_set_ba(mvm, vifs.bss_vif);

	return 0;
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

	iwl_mvm_power_build_cmd(mvm, vif, &cmd, !enable);

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
		/*
		 * When beacon storing is supported - disable beacon filtering
		 * altogether - the latest beacon will be sent when exiting d0i3
		 */
		if (fw_has_capa(&mvm->fw->ucode_capa,
				IWL_UCODE_TLV_CAPA_BEACON_STORING))
			ret = _iwl_mvm_disable_beacon_filter(mvm, vif, flags,
							     true);
		else
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
