// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */
#include <net/mac80211.h>

#include "mld.h"
#include "hcmd.h"
#include "power.h"
#include "iface.h"
#include "link.h"
#include "constants.h"

static void iwl_mld_vif_ps_iterator(void *data, u8 *mac,
				    struct ieee80211_vif *vif)
{
	bool *ps_enable = (bool *)data;
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);

	if (vif->type != NL80211_IFTYPE_STATION)
		return;

	*ps_enable &= !mld_vif->ps_disabled;
}

int iwl_mld_update_device_power(struct iwl_mld *mld, bool d3)
{
	struct iwl_device_power_cmd cmd = {};
	bool enable_ps = false;

	if (iwlmld_mod_params.power_scheme != IWL_POWER_SCHEME_CAM) {
		enable_ps = true;

		/* Disable power save if any STA interface has
		 * power save turned off
		 */
		ieee80211_iterate_active_interfaces_mtx(mld->hw,
							IEEE80211_IFACE_ITER_NORMAL,
							iwl_mld_vif_ps_iterator,
							&enable_ps);
	}

	if (enable_ps)
		cmd.flags |=
			cpu_to_le16(DEVICE_POWER_FLAGS_POWER_SAVE_ENA_MSK);

	if (d3)
		cmd.flags |=
			cpu_to_le16(DEVICE_POWER_FLAGS_NO_SLEEP_TILL_D3_MSK);

	IWL_DEBUG_POWER(mld,
			"Sending device power command with flags = 0x%X\n",
			cmd.flags);

	return iwl_mld_send_cmd_pdu(mld, POWER_TABLE_CMD, &cmd);
}

int iwl_mld_enable_beacon_filter(struct iwl_mld *mld,
				 const struct ieee80211_bss_conf *link_conf,
				 bool d3)
{
	struct iwl_beacon_filter_cmd cmd = {
		IWL_BF_CMD_CONFIG_DEFAULTS,
		.bf_enable_beacon_filter = cpu_to_le32(1),
		.ba_enable_beacon_abort = cpu_to_le32(1),
	};

	if (ieee80211_vif_type_p2p(link_conf->vif) != NL80211_IFTYPE_STATION)
		return 0;

#ifdef CONFIG_IWLWIFI_DEBUGFS
	if (iwl_mld_vif_from_mac80211(link_conf->vif)->disable_bf)
		return 0;
#endif

	if (link_conf->cqm_rssi_thold) {
		cmd.bf_energy_delta =
			cpu_to_le32(link_conf->cqm_rssi_hyst);
		/* fw uses an absolute value for this */
		cmd.bf_roaming_state =
			cpu_to_le32(-link_conf->cqm_rssi_thold);
	}

	if (d3)
		cmd.ba_escape_timer = cpu_to_le32(IWL_BA_ESCAPE_TIMER_D3);

	return iwl_mld_send_cmd_pdu(mld, REPLY_BEACON_FILTERING_CMD,
				    &cmd);
}

int iwl_mld_disable_beacon_filter(struct iwl_mld *mld,
				  struct ieee80211_vif *vif)
{
	struct iwl_beacon_filter_cmd cmd = {};

	if (ieee80211_vif_type_p2p(vif) != NL80211_IFTYPE_STATION)
		return 0;

	return iwl_mld_send_cmd_pdu(mld, REPLY_BEACON_FILTERING_CMD,
				    &cmd);
}

static bool iwl_mld_power_is_radar(struct iwl_mld *mld,
				   const struct ieee80211_bss_conf *link_conf)
{
	const struct ieee80211_chanctx_conf *chanctx_conf;

	chanctx_conf = wiphy_dereference(mld->wiphy, link_conf->chanctx_conf);

	if (WARN_ON(!chanctx_conf))
		return false;

	return chanctx_conf->def.chan->flags & IEEE80211_CHAN_RADAR;
}

static void iwl_mld_power_configure_uapsd(struct iwl_mld *mld,
					  struct iwl_mld_link *link,
					  struct iwl_mac_power_cmd *cmd,
					  bool ps_poll)
{
	bool tid_found = false;

	cmd->rx_data_timeout_uapsd =
		cpu_to_le32(IWL_MLD_UAPSD_RX_DATA_TIMEOUT);
	cmd->tx_data_timeout_uapsd =
		cpu_to_le32(IWL_MLD_UAPSD_TX_DATA_TIMEOUT);

	 /* set advanced pm flag with no uapsd ACs to enable ps-poll */
	if (ps_poll) {
		cmd->flags |= cpu_to_le16(POWER_FLAGS_ADVANCE_PM_ENA_MSK);
		return;
	}

	for (enum ieee80211_ac_numbers ac = IEEE80211_AC_VO;
	     ac <= IEEE80211_AC_BK;
	     ac++) {
		if (!link->queue_params[ac].uapsd)
			continue;

		cmd->flags |=
			cpu_to_le16(POWER_FLAGS_ADVANCE_PM_ENA_MSK |
				    POWER_FLAGS_UAPSD_MISBEHAVING_ENA_MSK);

		cmd->uapsd_ac_flags |= BIT(ac);

		/* QNDP TID - the highest TID with no admission control */
		if (!tid_found && !link->queue_params[ac].acm) {
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

	if (cmd->uapsd_ac_flags == (BIT(IEEE80211_AC_VO) |
				    BIT(IEEE80211_AC_VI) |
				    BIT(IEEE80211_AC_BE) |
				    BIT(IEEE80211_AC_BK))) {
		cmd->flags |= cpu_to_le16(POWER_FLAGS_SNOOZE_ENA_MSK);
		cmd->snooze_interval = cpu_to_le16(IWL_MLD_PS_SNOOZE_INTERVAL);
		cmd->snooze_window = cpu_to_le16(IWL_MLD_PS_SNOOZE_WINDOW);
	}

	cmd->uapsd_max_sp = mld->hw->uapsd_max_sp_len;
}

static void
iwl_mld_power_config_skip_dtim(struct iwl_mld *mld,
			       const struct ieee80211_bss_conf *link_conf,
			       struct iwl_mac_power_cmd *cmd)
{
	unsigned int dtimper_tu;
	unsigned int dtimper;
	unsigned int skip;

	dtimper = link_conf->dtim_period ?: 1;
	dtimper_tu = dtimper * link_conf->beacon_int;

	if (dtimper >= 10 || iwl_mld_power_is_radar(mld, link_conf))
		return;

	if (WARN_ON(!dtimper_tu))
		return;

	/* configure skip over dtim up to 900 TU DTIM interval */
	skip = max_t(int, 1, 900 / dtimper_tu);

	cmd->skip_dtim_periods = skip;
	cmd->flags |= cpu_to_le16(POWER_FLAGS_SKIP_OVER_DTIM_MSK);
}

#define POWER_KEEP_ALIVE_PERIOD_SEC    25
static void iwl_mld_power_build_cmd(struct iwl_mld *mld,
				    struct ieee80211_vif *vif,
				    struct iwl_mac_power_cmd *cmd,
				    bool d3)
{
	int dtimper, bi;
	int keep_alive;
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct ieee80211_bss_conf *link_conf = &vif->bss_conf;
	struct iwl_mld_link *link = &mld_vif->deflink;
	bool ps_poll = false;

	cmd->id_and_color = cpu_to_le32(mld_vif->fw_id);

	if (ieee80211_vif_is_mld(vif)) {
		int link_id;

		if (WARN_ON(!vif->active_links))
			return;

		/* The firmware consumes one single configuration for the vif
		 * and can't differentiate between links, just pick the lowest
		 * link_id's configuration and use that.
		 */
		link_id = __ffs(vif->active_links);
		link_conf = link_conf_dereference_check(vif, link_id);
		link = iwl_mld_link_dereference_check(mld_vif, link_id);

		if (WARN_ON(!link_conf || !link))
			return;
	}
	dtimper = link_conf->dtim_period;
	bi = link_conf->beacon_int;

	/* Regardless of power management state the driver must set
	 * keep alive period. FW will use it for sending keep alive NDPs
	 * immediately after association. Check that keep alive period
	 * is at least 3 * DTIM
	 */
	keep_alive = DIV_ROUND_UP(ieee80211_tu_to_usec(3 * dtimper * bi),
				  USEC_PER_SEC);
	keep_alive = max(keep_alive, POWER_KEEP_ALIVE_PERIOD_SEC);
	cmd->keep_alive_seconds = cpu_to_le16(keep_alive);

	if (iwlmld_mod_params.power_scheme != IWL_POWER_SCHEME_CAM)
		cmd->flags |= cpu_to_le16(POWER_FLAGS_POWER_SAVE_ENA_MSK);

	if (!vif->cfg.ps || iwl_mld_tdls_sta_count(mld) > 0)
		return;

	cmd->flags |= cpu_to_le16(POWER_FLAGS_POWER_MANAGEMENT_ENA_MSK);

	if (iwl_fw_lookup_cmd_ver(mld->fw, MAC_PM_POWER_TABLE, 0) >= 2)
		cmd->flags |= cpu_to_le16(POWER_FLAGS_ENABLE_SMPS_MSK);

	/* firmware supports LPRX for beacons at rate 1 Mbps or 6 Mbps only */
	if (link_conf->beacon_rate &&
	    (link_conf->beacon_rate->bitrate == 10 ||
	     link_conf->beacon_rate->bitrate == 60)) {
		cmd->flags |= cpu_to_le16(POWER_FLAGS_LPRX_ENA_MSK);
		cmd->lprx_rssi_threshold = POWER_LPRX_RSSI_THRESHOLD;
	}

	if (d3) {
		iwl_mld_power_config_skip_dtim(mld, link_conf, cmd);
		cmd->rx_data_timeout =
			cpu_to_le32(IWL_MLD_WOWLAN_PS_RX_DATA_TIMEOUT);
		cmd->tx_data_timeout =
			cpu_to_le32(IWL_MLD_WOWLAN_PS_TX_DATA_TIMEOUT);
	} else if (iwl_mld_vif_low_latency(mld_vif) && vif->p2p) {
		cmd->tx_data_timeout =
			cpu_to_le32(IWL_MLD_SHORT_PS_TX_DATA_TIMEOUT);
		cmd->rx_data_timeout =
			cpu_to_le32(IWL_MLD_SHORT_PS_RX_DATA_TIMEOUT);
	} else {
		cmd->rx_data_timeout =
			cpu_to_le32(IWL_MLD_DEFAULT_PS_RX_DATA_TIMEOUT);
		cmd->tx_data_timeout =
			cpu_to_le32(IWL_MLD_DEFAULT_PS_TX_DATA_TIMEOUT);
	}

	/* uAPSD is only enabled for specific certifications. For those cases,
	 * mac80211 will allow uAPSD. Always call iwl_mld_power_configure_uapsd
	 * which will look at what mac80211 is saying.
	 */
#ifdef CONFIG_IWLWIFI_DEBUGFS
	ps_poll = mld_vif->use_ps_poll;
#endif
	iwl_mld_power_configure_uapsd(mld, link, cmd, ps_poll);
}

int iwl_mld_update_mac_power(struct iwl_mld *mld, struct ieee80211_vif *vif,
			     bool d3)
{
	struct iwl_mac_power_cmd cmd = {};

	iwl_mld_power_build_cmd(mld, vif, &cmd, d3);

	return iwl_mld_send_cmd_pdu(mld, MAC_PM_POWER_TABLE, &cmd);
}

static void
iwl_mld_tpe_sta_cmd_data(struct iwl_txpower_constraints_cmd *cmd,
			 const struct ieee80211_bss_conf *link)
{
	u8 i;

	/* NOTE: the 0 here is IEEE80211_TPE_CAT_6GHZ_DEFAULT,
	 * we fully ignore IEEE80211_TPE_CAT_6GHZ_SUBORDINATE
	 */

	BUILD_BUG_ON(ARRAY_SIZE(cmd->psd_pwr) !=
		     ARRAY_SIZE(link->tpe.psd_local[0].power));

	/* if not valid, mac80211 puts default (max value) */
	for (i = 0; i < ARRAY_SIZE(cmd->psd_pwr); i++)
		cmd->psd_pwr[i] = min(link->tpe.psd_local[0].power[i],
				      link->tpe.psd_reg_client[0].power[i]);

	BUILD_BUG_ON(ARRAY_SIZE(cmd->eirp_pwr) !=
		     ARRAY_SIZE(link->tpe.max_local[0].power));

	for (i = 0; i < ARRAY_SIZE(cmd->eirp_pwr); i++)
		cmd->eirp_pwr[i] = min(link->tpe.max_local[0].power[i],
				       link->tpe.max_reg_client[0].power[i]);
}

void
iwl_mld_send_ap_tx_power_constraint_cmd(struct iwl_mld *mld,
					struct ieee80211_vif *vif,
					struct ieee80211_bss_conf *link)
{
	struct iwl_txpower_constraints_cmd cmd = {};
	struct iwl_mld_link *mld_link = iwl_mld_link_from_mac80211(link);
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	if (!mld_link->active)
		return;

	if (link->chanreq.oper.chan->band != NL80211_BAND_6GHZ)
		return;

	cmd.link_id = cpu_to_le16(mld_link->fw_id);
	memset(cmd.psd_pwr, DEFAULT_TPE_TX_POWER, sizeof(cmd.psd_pwr));
	memset(cmd.eirp_pwr, DEFAULT_TPE_TX_POWER, sizeof(cmd.eirp_pwr));

	if (vif->type == NL80211_IFTYPE_AP) {
		cmd.ap_type = cpu_to_le16(IWL_6GHZ_AP_TYPE_VLP);
	} else if (link->power_type == IEEE80211_REG_UNSET_AP) {
		return;
	} else {
		cmd.ap_type = cpu_to_le16(link->power_type - 1);
		iwl_mld_tpe_sta_cmd_data(&cmd, link);
	}

	ret = iwl_mld_send_cmd_pdu(mld,
				   WIDE_ID(PHY_OPS_GROUP,
					   AP_TX_POWER_CONSTRAINTS_CMD),
				   &cmd);
	if (ret)
		IWL_ERR(mld,
			"failed to send AP_TX_POWER_CONSTRAINTS_CMD (%d)\n",
			ret);
}

int iwl_mld_set_tx_power(struct iwl_mld *mld,
			 struct ieee80211_bss_conf *link_conf,
			 s16 tx_power)
{
	u32 cmd_id = REDUCE_TX_POWER_CMD;
	struct iwl_mld_link *mld_link = iwl_mld_link_from_mac80211(link_conf);
	u16 u_tx_power = tx_power == IWL_DEFAULT_MAX_TX_POWER ?
		IWL_DEV_MAX_TX_POWER : 8 * tx_power;
	struct iwl_dev_tx_power_cmd cmd = {
		.common.set_mode = cpu_to_le32(IWL_TX_POWER_MODE_SET_LINK),
		.common.pwr_restriction = cpu_to_le16(u_tx_power),
	};
	int len = sizeof(cmd.common) + sizeof(cmd.v10);

	if (WARN_ON(!mld_link))
		return -ENODEV;

	cmd.common.link_id = cpu_to_le32(mld_link->fw_id);

	return iwl_mld_send_cmd_pdu(mld, cmd_id, &cmd, len);
}
