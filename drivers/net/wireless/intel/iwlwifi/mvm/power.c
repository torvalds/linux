// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2012-2014, 2018-2019, 2021-2024 Intel Corporation
 * Copyright (C) 2013-2014 Intel Mobile Communications GmbH
 * Copyright (C) 2015-2017 Intel Deutschland GmbH
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/etherdevice.h>

#include <net/mac80211.h>

#include "iwl-debug.h"
#include "mvm.h"
#include "iwl-modparams.h"
#include "fw/api/power.h"

#define POWER_KEEP_ALIVE_PERIOD_SEC    25

static
int iwl_mvm_beacon_filter_send_cmd(struct iwl_mvm *mvm,
				   struct iwl_beacon_filter_cmd *cmd)
{
	u16 len;

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
	IWL_DEBUG_POWER(mvm, "bf_threshold_absolute_low is: %d, %d\n",
			le32_to_cpu(cmd->bf_threshold_absolute_low[0]),
			le32_to_cpu(cmd->bf_threshold_absolute_low[1]));

	IWL_DEBUG_POWER(mvm, "bf_threshold_absolute_high is: %d, %d\n",
			le32_to_cpu(cmd->bf_threshold_absolute_high[0]),
			le32_to_cpu(cmd->bf_threshold_absolute_high[1]));

	if (fw_has_api(&mvm->fw->ucode_capa,
		       IWL_UCODE_TLV_API_BEACON_FILTER_V4))
		len = sizeof(struct iwl_beacon_filter_cmd);
	else
		len = offsetof(struct iwl_beacon_filter_cmd,
			       bf_threshold_absolute_low);

	return iwl_mvm_send_cmd_pdu(mvm, REPLY_BEACON_FILTERING_CMD, 0,
				    len, cmd);
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
	cmd->ba_enable_beacon_abort = cpu_to_le32(mvmvif->ba_enabled);
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

	if (test_bit(IWL_MVM_STATUS_IN_D3, &mvm->status) ||
	    cmd->flags & cpu_to_le16(POWER_FLAGS_SNOOZE_ENA_MSK)) {
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

#ifdef CONFIG_IWLWIFI_DEBUGFS
	/* set advanced pm flag with no uapsd ACs to enable ps-poll */
	if (mvmvif->dbgfs_pm.use_ps_poll) {
		cmd->flags |= cpu_to_le16(POWER_FLAGS_ADVANCE_PM_ENA_MSK);
		return;
	}
#endif

	for (ac = IEEE80211_AC_VO; ac <= IEEE80211_AC_BK; ac++) {
		if (!mvmvif->deflink.queue_params[ac].uapsd)
			continue;

		if (!test_bit(IWL_MVM_STATUS_IN_D3, &mvm->status))
			cmd->flags |=
				cpu_to_le16(POWER_FLAGS_ADVANCE_PM_ENA_MSK);

		cmd->uapsd_ac_flags |= BIT(ac);

		/* QNDP TID - the highest TID with no admission control */
		if (!tid_found && !mvmvif->deflink.queue_params[ac].acm) {
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

	cmd->flags |= cpu_to_le16(POWER_FLAGS_UAPSD_MISBEHAVING_ENA_MSK);

	if (cmd->uapsd_ac_flags == (BIT(IEEE80211_AC_VO) |
				    BIT(IEEE80211_AC_VI) |
				    BIT(IEEE80211_AC_BE) |
				    BIT(IEEE80211_AC_BK))) {
		cmd->flags |= cpu_to_le16(POWER_FLAGS_SNOOZE_ENA_MSK);
		cmd->snooze_interval = cpu_to_le16(IWL_MVM_PS_SNOOZE_INTERVAL);
		cmd->snooze_window =
			test_bit(IWL_MVM_STATUS_IN_D3, &mvm->status) ?
				cpu_to_le16(IWL_MVM_WOWLAN_PS_SNOOZE_WINDOW) :
				cpu_to_le16(IWL_MVM_PS_SNOOZE_WINDOW);
	}

	cmd->uapsd_max_sp = mvm->hw->uapsd_max_sp_len;

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

struct iwl_allow_uapsd_iface_iterator_data {
	struct ieee80211_vif *current_vif;
	bool allow_uapsd;
};

static void iwl_mvm_allow_uapsd_iterator(void *_data, u8 *mac,
					 struct ieee80211_vif *vif)
{
	struct iwl_allow_uapsd_iface_iterator_data *data = _data;
	struct iwl_mvm_vif *other_mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_vif *curr_mvmvif =
		iwl_mvm_vif_from_mac80211(data->current_vif);

	/* exclude the given vif */
	if (vif == data->current_vif)
		return;

	switch (vif->type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_NAN:
		data->allow_uapsd = false;
		break;
	case NL80211_IFTYPE_STATION:
		/* allow UAPSD if P2P interface and BSS station interface share
		 * the same channel.
		 */
		if (vif->cfg.assoc && other_mvmvif->deflink.phy_ctxt &&
		    curr_mvmvif->deflink.phy_ctxt &&
		    other_mvmvif->deflink.phy_ctxt->id != curr_mvmvif->deflink.phy_ctxt->id)
			data->allow_uapsd = false;
		break;

	default:
		break;
	}
}

static bool iwl_mvm_power_allow_uapsd(struct iwl_mvm *mvm,
				       struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_allow_uapsd_iface_iterator_data data = {
		.current_vif = vif,
		.allow_uapsd = true,
	};

	if (ether_addr_equal(mvmvif->uapsd_misbehaving_ap_addr,
			     vif->cfg.ap_addr))
		return false;

	/*
	 * Avoid using uAPSD if P2P client is associated to GO that uses
	 * opportunistic power save. This is due to current FW limitation.
	 */
	if (vif->p2p &&
	    (vif->bss_conf.p2p_noa_attr.oppps_ctwindow &
	    IEEE80211_P2P_OPPPS_ENABLE_BIT))
		return false;

	if (vif->p2p && !iwl_mvm_is_p2p_scm_uapsd_supported(mvm))
		return false;

	ieee80211_iterate_active_interfaces_atomic(mvm->hw,
				IEEE80211_IFACE_ITER_NORMAL,
				iwl_mvm_allow_uapsd_iterator,
				&data);

	return data.allow_uapsd;
}

static bool iwl_mvm_power_is_radar(struct ieee80211_bss_conf *link_conf)
{
	struct ieee80211_chanctx_conf *chanctx_conf;

	chanctx_conf = rcu_dereference(link_conf->chanctx_conf);

	/* this happens on link switching, just ignore inactive ones */
	if (!chanctx_conf)
		return false;

	return chanctx_conf->def.chan->flags & IEEE80211_CHAN_RADAR;
}

static void iwl_mvm_power_config_skip_dtim(struct iwl_mvm *mvm,
					   struct ieee80211_vif *vif,
					   struct iwl_mac_power_cmd *cmd)
{
	struct ieee80211_bss_conf *link_conf;
	unsigned int min_link_skip = ~0;
	unsigned int link_id;

	/* disable, in case we're supposed to override */
	cmd->skip_dtim_periods = 0;
	cmd->flags &= ~cpu_to_le16(POWER_FLAGS_SKIP_OVER_DTIM_MSK);

	if (!test_bit(IWL_MVM_STATUS_IN_D3, &mvm->status)) {
		if (iwlmvm_mod_params.power_scheme != IWL_POWER_SCHEME_LP)
			return;
		cmd->skip_dtim_periods = 2;
		cmd->flags |= cpu_to_le16(POWER_FLAGS_SKIP_OVER_DTIM_MSK);
		return;
	}

	rcu_read_lock();
	for_each_vif_active_link(vif, link_conf, link_id) {
		unsigned int dtimper = link_conf->dtim_period ?: 1;
		unsigned int dtimper_tu = dtimper * link_conf->beacon_int;
		unsigned int skip;

		if (dtimper >= 10 || iwl_mvm_power_is_radar(link_conf)) {
			rcu_read_unlock();
			return;
		}

		if (WARN_ON(!dtimper_tu))
			continue;

		/* configure skip over dtim up to 900 TU DTIM interval */
		skip = max_t(int, 1, 900 / dtimper_tu);
		min_link_skip = min(min_link_skip, skip);
	}
	rcu_read_unlock();

	/* no WARN_ON, can only happen with WARN_ON above */
	if (min_link_skip == ~0)
		return;

	cmd->skip_dtim_periods = min_link_skip;
	cmd->flags |= cpu_to_le16(POWER_FLAGS_SKIP_OVER_DTIM_MSK);
}

static void iwl_mvm_power_build_cmd(struct iwl_mvm *mvm,
				    struct ieee80211_vif *vif,
				    struct iwl_mac_power_cmd *cmd)
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

	if (!vif->cfg.ps || !mvmvif->pm_enabled)
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

	iwl_mvm_power_config_skip_dtim(mvm, vif, cmd);

	if (test_bit(IWL_MVM_STATUS_IN_D3, &mvm->status)) {
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

	iwl_mvm_power_build_cmd(mvm, vif, &cmd);
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
	if (test_bit(IWL_MVM_STATUS_IN_D3, &mvm->status) ?
			mvm->disable_power_off_d3 : mvm->disable_power_off)
		cmd.flags &=
			cpu_to_le16(~DEVICE_POWER_FLAGS_POWER_SAVE_ENA_MSK);
#endif
	if (mvm->ext_clock_valid)
		cmd.flags |= cpu_to_le16(DEVICE_POWER_FLAGS_32K_CLK_VALID_MSK);

	if (iwl_fw_lookup_cmd_ver(mvm->fw, POWER_TABLE_CMD, 0) >= 7 &&
	    test_bit(IWL_MVM_STATUS_IN_D3, &mvm->status))
		cmd.flags |=
			cpu_to_le16(DEVICE_POWER_FLAGS_NO_SLEEP_TILL_D3_MSK);

	IWL_DEBUG_POWER(mvm,
			"Sending device power command with flags = 0x%X\n",
			cmd.flags);

	return iwl_mvm_send_cmd_pdu(mvm, POWER_TABLE_CMD, 0, sizeof(cmd),
				    &cmd);
}

void iwl_mvm_power_vif_assoc(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (!ether_addr_equal(mvmvif->uapsd_misbehaving_ap_addr,
			      vif->cfg.ap_addr))
		eth_zero_addr(mvmvif->uapsd_misbehaving_ap_addr);
}

static void iwl_mvm_power_uapsd_misbehav_ap_iterator(void *_data, u8 *mac,
						     struct ieee80211_vif *vif)
{
	u8 *ap_sta_id = _data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct ieee80211_bss_conf *link_conf;
	unsigned int link_id;

	rcu_read_lock();
	for_each_vif_active_link(vif, link_conf, link_id) {
		struct iwl_mvm_vif_link_info *link_info = mvmvif->link[link_id];

		/* The ap_sta_id is not expected to change during current
		 * association so no explicit protection is needed
		 */
		if (link_info->ap_sta_id == *ap_sta_id) {
			ether_addr_copy(mvmvif->uapsd_misbehaving_ap_addr,
					vif->cfg.ap_addr);
			break;
		}
	}
	rcu_read_unlock();
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

static void iwl_mvm_power_disable_pm_iterator(void *_data, u8 *mac,
					      struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	mvmvif->pm_enabled = false;
}

static void iwl_mvm_power_ps_disabled_iterator(void *_data, u8 *mac,
					       struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	bool *disable_ps = _data;

	if (iwl_mvm_vif_is_active(mvmvif))
		*disable_ps |= mvmvif->ps_disabled;
}

static void iwl_mvm_power_get_vifs_iterator(void *_data, u8 *mac,
					    struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_power_vifs *power_iterator = _data;
	bool active;

	if (!mvmvif->uploaded)
		return;

	active = iwl_mvm_vif_is_active(mvmvif);

	switch (ieee80211_vif_type_p2p(vif)) {
	case NL80211_IFTYPE_P2P_DEVICE:
		break;

	case NL80211_IFTYPE_P2P_GO:
	case NL80211_IFTYPE_AP:
		/* only a single MAC of the same type */
		WARN_ON(power_iterator->ap_vif);
		power_iterator->ap_vif = vif;
		if (active)
			power_iterator->ap_active = true;
		break;

	case NL80211_IFTYPE_MONITOR:
		/* only a single MAC of the same type */
		WARN_ON(power_iterator->monitor_vif);
		power_iterator->monitor_vif = vif;
		if (active)
			power_iterator->monitor_active = true;
		break;

	case NL80211_IFTYPE_P2P_CLIENT:
		/* only a single MAC of the same type */
		WARN_ON(power_iterator->p2p_vif);
		power_iterator->p2p_vif = vif;
		if (active)
			power_iterator->p2p_active = true;
		break;

	case NL80211_IFTYPE_STATION:
		power_iterator->bss_vif = vif;
		if (active)
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
	if (bss_mvmvif && vifs->bss_active && !vifs->p2p_active &&
	    !vifs->ap_active) {
		bss_mvmvif->pm_enabled = true;
		return;
	}

	/* enable PM on p2p if p2p stand alone */
	if (p2p_mvmvif && vifs->p2p_active && !vifs->bss_active &&
	    !vifs->ap_active) {
		p2p_mvmvif->pm_enabled = true;
		return;
	}

	if (p2p_mvmvif && bss_mvmvif && vifs->bss_active && vifs->p2p_active)
		client_same_channel =
			iwl_mvm_have_links_same_channel(bss_mvmvif, p2p_mvmvif);

	if (bss_mvmvif && ap_mvmvif && vifs->bss_active && vifs->ap_active)
		ap_same_channel =
			iwl_mvm_have_links_same_channel(bss_mvmvif, ap_mvmvif);

	/* clients are not stand alone: enable PM if DCM */
	if (!(client_same_channel || ap_same_channel)) {
		if (bss_mvmvif && vifs->bss_active)
			bss_mvmvif->pm_enabled = true;
		if (p2p_mvmvif && vifs->p2p_active)
			p2p_mvmvif->pm_enabled = true;
		return;
	}

	/*
	 * There is only one channel in the system and there are only
	 * bss and p2p clients that share it
	 */
	if (client_same_channel && !vifs->ap_active) {
		/* share same channel*/
		bss_mvmvif->pm_enabled = true;
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
					 struct iwl_beacon_filter_cmd *cmd)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	if (mvmvif != mvm->bf_allowed_vif || !vif->bss_conf.dtim_period ||
	    vif->type != NL80211_IFTYPE_STATION || vif->p2p)
		return 0;

	iwl_mvm_beacon_filter_set_cqm_params(mvm, vif, cmd);
	iwl_mvm_beacon_filter_debugfs_parameters(vif, cmd);
	ret = iwl_mvm_beacon_filter_send_cmd(mvm, cmd);

	if (!ret)
		mvmvif->bf_enabled = true;

	return ret;
}

int iwl_mvm_enable_beacon_filter(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif)
{
	struct iwl_beacon_filter_cmd cmd = {
		IWL_BF_CMD_CONFIG_DEFAULTS,
		.bf_enable_beacon_filter = cpu_to_le32(1),
	};

	return _iwl_mvm_enable_beacon_filter(mvm, vif, &cmd);
}

static int _iwl_mvm_disable_beacon_filter(struct iwl_mvm *mvm,
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

int iwl_mvm_disable_beacon_filter(struct iwl_mvm *mvm,
				  struct ieee80211_vif *vif)
{
	return _iwl_mvm_disable_beacon_filter(mvm, vif);
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

	if (!mvmvif->bf_enabled)
		return 0;

	if (test_bit(IWL_MVM_STATUS_IN_D3, &mvm->status))
		cmd.ba_escape_timer = cpu_to_le32(IWL_BA_ESCAPE_TIMER_D3);

	mvmvif->ba_enabled = !(!mvmvif->pm_enabled ||
			       mvm->ps_disabled ||
			       !vif->cfg.ps ||
			       iwl_mvm_vif_low_latency(mvmvif));

	return _iwl_mvm_enable_beacon_filter(mvm, vif, &cmd);
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
