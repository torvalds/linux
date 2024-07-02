// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2013-2014, 2018-2020, 2022-2024 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 */
#include <linux/ieee80211.h>
#include <linux/etherdevice.h>
#include <net/mac80211.h>

#include "fw/api/coex.h"
#include "iwl-modparams.h"
#include "mvm.h"
#include "iwl-debug.h"

/* 20MHz / 40MHz below / 40Mhz above*/
static const __le64 iwl_ci_mask[][3] = {
	/* dummy entry for channel 0 */
	{cpu_to_le64(0), cpu_to_le64(0), cpu_to_le64(0)},
	{
		cpu_to_le64(0x0000001FFFULL),
		cpu_to_le64(0x0ULL),
		cpu_to_le64(0x00007FFFFFULL),
	},
	{
		cpu_to_le64(0x000000FFFFULL),
		cpu_to_le64(0x0ULL),
		cpu_to_le64(0x0003FFFFFFULL),
	},
	{
		cpu_to_le64(0x000003FFFCULL),
		cpu_to_le64(0x0ULL),
		cpu_to_le64(0x000FFFFFFCULL),
	},
	{
		cpu_to_le64(0x00001FFFE0ULL),
		cpu_to_le64(0x0ULL),
		cpu_to_le64(0x007FFFFFE0ULL),
	},
	{
		cpu_to_le64(0x00007FFF80ULL),
		cpu_to_le64(0x00007FFFFFULL),
		cpu_to_le64(0x01FFFFFF80ULL),
	},
	{
		cpu_to_le64(0x0003FFFC00ULL),
		cpu_to_le64(0x0003FFFFFFULL),
		cpu_to_le64(0x0FFFFFFC00ULL),
	},
	{
		cpu_to_le64(0x000FFFF000ULL),
		cpu_to_le64(0x000FFFFFFCULL),
		cpu_to_le64(0x3FFFFFF000ULL),
	},
	{
		cpu_to_le64(0x007FFF8000ULL),
		cpu_to_le64(0x007FFFFFE0ULL),
		cpu_to_le64(0xFFFFFF8000ULL),
	},
	{
		cpu_to_le64(0x01FFFE0000ULL),
		cpu_to_le64(0x01FFFFFF80ULL),
		cpu_to_le64(0xFFFFFE0000ULL),
	},
	{
		cpu_to_le64(0x0FFFF00000ULL),
		cpu_to_le64(0x0FFFFFFC00ULL),
		cpu_to_le64(0x0ULL),
	},
	{
		cpu_to_le64(0x3FFFC00000ULL),
		cpu_to_le64(0x3FFFFFF000ULL),
		cpu_to_le64(0x0)
	},
	{
		cpu_to_le64(0xFFFE000000ULL),
		cpu_to_le64(0xFFFFFF8000ULL),
		cpu_to_le64(0x0)
	},
	{
		cpu_to_le64(0xFFF8000000ULL),
		cpu_to_le64(0xFFFFFE0000ULL),
		cpu_to_le64(0x0)
	},
	{
		cpu_to_le64(0xFE00000000ULL),
		cpu_to_le64(0x0ULL),
		cpu_to_le64(0x0ULL)
	},
};

static enum iwl_bt_coex_lut_type
iwl_get_coex_type(struct iwl_mvm *mvm, const struct ieee80211_vif *vif)
{
	struct ieee80211_chanctx_conf *chanctx_conf;
	enum iwl_bt_coex_lut_type ret;
	u16 phy_ctx_id;
	u32 primary_ch_phy_id, secondary_ch_phy_id;

	/*
	 * Checking that we hold mvm->mutex is a good idea, but the rate
	 * control can't acquire the mutex since it runs in Tx path.
	 * So this is racy in that case, but in the worst case, the AMPDU
	 * size limit will be wrong for a short time which is not a big
	 * issue.
	 */

	rcu_read_lock();

	chanctx_conf = rcu_dereference(vif->bss_conf.chanctx_conf);

	if (!chanctx_conf ||
	     chanctx_conf->def.chan->band != NL80211_BAND_2GHZ) {
		rcu_read_unlock();
		return BT_COEX_INVALID_LUT;
	}

	ret = BT_COEX_TX_DIS_LUT;

	phy_ctx_id = *((u16 *)chanctx_conf->drv_priv);
	primary_ch_phy_id = le32_to_cpu(mvm->last_bt_ci_cmd.primary_ch_phy_id);
	secondary_ch_phy_id =
		le32_to_cpu(mvm->last_bt_ci_cmd.secondary_ch_phy_id);

	if (primary_ch_phy_id == phy_ctx_id)
		ret = le32_to_cpu(mvm->last_bt_notif.primary_ch_lut);
	else if (secondary_ch_phy_id == phy_ctx_id)
		ret = le32_to_cpu(mvm->last_bt_notif.secondary_ch_lut);
	/* else - default = TX TX disallowed */

	rcu_read_unlock();

	return ret;
}

int iwl_mvm_send_bt_init_conf(struct iwl_mvm *mvm)
{
	struct iwl_bt_coex_cmd bt_cmd = {};
	u32 mode;

	lockdep_assert_held(&mvm->mutex);

	if (unlikely(mvm->bt_force_ant_mode != BT_FORCE_ANT_DIS)) {
		switch (mvm->bt_force_ant_mode) {
		case BT_FORCE_ANT_BT:
			mode = BT_COEX_BT;
			break;
		case BT_FORCE_ANT_WIFI:
			mode = BT_COEX_WIFI;
			break;
		default:
			WARN_ON(1);
			mode = 0;
		}

		bt_cmd.mode = cpu_to_le32(mode);
		goto send_cmd;
	}

	bt_cmd.mode = cpu_to_le32(BT_COEX_NW);

	if (IWL_MVM_BT_COEX_SYNC2SCO)
		bt_cmd.enabled_modules |=
			cpu_to_le32(BT_COEX_SYNC2SCO_ENABLED);

	if (iwl_mvm_is_mplut_supported(mvm))
		bt_cmd.enabled_modules |= cpu_to_le32(BT_COEX_MPLUT_ENABLED);

	bt_cmd.enabled_modules |= cpu_to_le32(BT_COEX_HIGH_BAND_RET);

send_cmd:
	memset(&mvm->last_bt_notif, 0, sizeof(mvm->last_bt_notif));
	memset(&mvm->last_bt_ci_cmd, 0, sizeof(mvm->last_bt_ci_cmd));

	return iwl_mvm_send_cmd_pdu(mvm, BT_CONFIG, 0, sizeof(bt_cmd), &bt_cmd);
}

static int iwl_mvm_bt_coex_reduced_txp(struct iwl_mvm *mvm, u8 sta_id,
				       bool enable)
{
	struct iwl_bt_coex_reduced_txp_update_cmd cmd = {};
	struct iwl_mvm_sta *mvmsta;
	u32 value;

	if (mvm->trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_AX210)
		return 0;

	mvmsta = iwl_mvm_sta_from_staid_protected(mvm, sta_id);
	if (!mvmsta)
		return 0;

	/* nothing to do */
	if (mvmsta->bt_reduced_txpower == enable)
		return 0;

	value = mvmsta->deflink.sta_id;

	if (enable)
		value |= BT_REDUCED_TX_POWER_BIT;

	IWL_DEBUG_COEX(mvm, "%sable reduced Tx Power for sta %d\n",
		       enable ? "en" : "dis", sta_id);

	cmd.reduced_txp = cpu_to_le32(value);
	mvmsta->bt_reduced_txpower = enable;

	return iwl_mvm_send_cmd_pdu(mvm, BT_COEX_UPDATE_REDUCED_TXP,
				    CMD_ASYNC, sizeof(cmd), &cmd);
}

struct iwl_bt_iterator_data {
	struct iwl_bt_coex_profile_notif *notif;
	struct iwl_mvm *mvm;
	struct ieee80211_chanctx_conf *primary;
	struct ieee80211_chanctx_conf *secondary;
	bool primary_ll;
	u8 primary_load;
	u8 secondary_load;
};

static inline
void iwl_mvm_bt_coex_enable_rssi_event(struct iwl_mvm *mvm,
				       struct iwl_mvm_vif_link_info *link_info,
				       bool enable, int rssi)
{
	link_info->bf_data.last_bt_coex_event = rssi;
	link_info->bf_data.bt_coex_max_thold =
		enable ? -IWL_MVM_BT_COEX_EN_RED_TXP_THRESH : 0;
	link_info->bf_data.bt_coex_min_thold =
		enable ? -IWL_MVM_BT_COEX_DIS_RED_TXP_THRESH : 0;
}

#define MVM_COEX_TCM_PERIOD (HZ * 10)

static void iwl_mvm_bt_coex_tcm_based_ci(struct iwl_mvm *mvm,
					 struct iwl_bt_iterator_data *data)
{
	unsigned long now = jiffies;

	if (!time_after(now, mvm->bt_coex_last_tcm_ts + MVM_COEX_TCM_PERIOD))
		return;

	mvm->bt_coex_last_tcm_ts = now;

	/* We assume here that we don't have more than 2 vifs on 2.4GHz */

	/* if the primary is low latency, it will stay primary */
	if (data->primary_ll)
		return;

	if (data->primary_load >= data->secondary_load)
		return;

	swap(data->primary, data->secondary);
}

/*
 * This function receives the LB link id and checks if eSR should be
 * enabled or disabled (due to BT coex)
 */
bool
iwl_mvm_bt_coex_calculate_esr_mode(struct iwl_mvm *mvm,
				   struct ieee80211_vif *vif,
				   s32 link_rssi,
				   bool primary)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	bool have_wifi_loss_rate =
		iwl_fw_lookup_notif_ver(mvm->fw, LEGACY_GROUP,
					BT_PROFILE_NOTIFICATION, 0) > 4;
	u8 wifi_loss_rate;

	if (mvm->last_bt_notif.wifi_loss_low_rssi == BT_OFF)
		return true;

	if (primary)
		return false;

	/* The feature is not supported */
	if (!have_wifi_loss_rate)
		return true;


	/*
	 * In case we don't know the RSSI - take the lower wifi loss,
	 * so we will more likely enter eSR, and if RSSI is low -
	 * we will get an update on this and exit eSR.
	 */
	if (!link_rssi)
		wifi_loss_rate = mvm->last_bt_notif.wifi_loss_mid_high_rssi;

	else if (mvmvif->esr_active)
		 /* RSSI needs to get really low to disable eSR... */
		wifi_loss_rate =
			link_rssi <= -IWL_MVM_BT_COEX_DISABLE_ESR_THRESH ?
				mvm->last_bt_notif.wifi_loss_low_rssi :
				mvm->last_bt_notif.wifi_loss_mid_high_rssi;
	else
		/* ...And really high before we enable it back */
		wifi_loss_rate =
			link_rssi <= -IWL_MVM_BT_COEX_ENABLE_ESR_THRESH ?
				mvm->last_bt_notif.wifi_loss_low_rssi :
				mvm->last_bt_notif.wifi_loss_mid_high_rssi;

	return wifi_loss_rate <= IWL_MVM_BT_COEX_WIFI_LOSS_THRESH;
}

void iwl_mvm_bt_coex_update_link_esr(struct iwl_mvm *mvm,
				     struct ieee80211_vif *vif,
				     int link_id)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_vif_link_info *link = mvmvif->link[link_id];

	if (!ieee80211_vif_is_mld(vif) ||
	    !iwl_mvm_vif_from_mac80211(vif)->authorized ||
	    WARN_ON(!link))
		return;

	if (!iwl_mvm_bt_coex_calculate_esr_mode(mvm, vif,
						(s8)link->beacon_stats.avg_signal,
						link_id == iwl_mvm_get_primary_link(vif)))
		/* In case we decided to exit eSR - stay with the primary */
		iwl_mvm_exit_esr(mvm, vif, IWL_MVM_ESR_EXIT_COEX,
				 iwl_mvm_get_primary_link(vif));
}

static void iwl_mvm_bt_notif_per_link(struct iwl_mvm *mvm,
				      struct ieee80211_vif *vif,
				      struct iwl_bt_iterator_data *data,
				      unsigned int link_id)
{
	/* default smps_mode is AUTOMATIC - only used for client modes */
	enum ieee80211_smps_mode smps_mode = IEEE80211_SMPS_AUTOMATIC;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	u32 bt_activity_grading, min_ag_for_static_smps;
	struct ieee80211_chanctx_conf *chanctx_conf;
	struct iwl_mvm_vif_link_info *link_info;
	struct ieee80211_bss_conf *link_conf;
	int ave_rssi;

	lockdep_assert_held(&mvm->mutex);

	link_info = mvmvif->link[link_id];
	if (!link_info)
		return;

	link_conf = rcu_dereference(vif->link_conf[link_id]);
	/* This can happen due to races: if we receive the notification
	 * and have the mutex held, while mac80211 is stuck on our mutex
	 * in the middle of removing the link.
	 */
	if (!link_conf)
		return;

	chanctx_conf = rcu_dereference(link_conf->chanctx_conf);

	/* If channel context is invalid or not on 2.4GHz .. */
	if ((!chanctx_conf ||
	     chanctx_conf->def.chan->band != NL80211_BAND_2GHZ)) {
		if (vif->type == NL80211_IFTYPE_STATION) {
			/* ... relax constraints and disable rssi events */
			iwl_mvm_update_smps(mvm, vif, IWL_MVM_SMPS_REQ_BT_COEX,
					    smps_mode, link_id);
			iwl_mvm_bt_coex_reduced_txp(mvm, link_info->ap_sta_id,
						    false);
			iwl_mvm_bt_coex_enable_rssi_event(mvm, link_info, false,
							  0);
		}
		return;
	}

	iwl_mvm_bt_coex_update_link_esr(mvm, vif, link_id);

	if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_COEX_SCHEMA_2))
		min_ag_for_static_smps = BT_VERY_HIGH_TRAFFIC;
	else
		min_ag_for_static_smps = BT_HIGH_TRAFFIC;

	bt_activity_grading = le32_to_cpu(data->notif->bt_activity_grading);
	if (bt_activity_grading >= min_ag_for_static_smps)
		smps_mode = IEEE80211_SMPS_STATIC;
	else if (bt_activity_grading >= BT_LOW_TRAFFIC)
		smps_mode = IEEE80211_SMPS_DYNAMIC;

	/* relax SMPS constraints for next association */
	if (!vif->cfg.assoc)
		smps_mode = IEEE80211_SMPS_AUTOMATIC;

	if (link_info->phy_ctxt &&
	    (mvm->last_bt_notif.rrc_status & BIT(link_info->phy_ctxt->id)))
		smps_mode = IEEE80211_SMPS_AUTOMATIC;

	IWL_DEBUG_COEX(data->mvm,
		       "mac %d link %d: bt_activity_grading %d smps_req %d\n",
		       mvmvif->id, link_info->fw_link_id,
		       bt_activity_grading, smps_mode);

	if (vif->type == NL80211_IFTYPE_STATION)
		iwl_mvm_update_smps(mvm, vif, IWL_MVM_SMPS_REQ_BT_COEX,
				    smps_mode, link_id);

	/* low latency is always primary */
	if (iwl_mvm_vif_low_latency(mvmvif)) {
		data->primary_ll = true;

		data->secondary = data->primary;
		data->primary = chanctx_conf;
	}

	if (vif->type == NL80211_IFTYPE_AP) {
		if (!mvmvif->ap_ibss_active)
			return;

		if (chanctx_conf == data->primary)
			return;

		if (!data->primary_ll) {
			/*
			 * downgrade the current primary no matter what its
			 * type is.
			 */
			data->secondary = data->primary;
			data->primary = chanctx_conf;
		} else {
			/* there is low latency vif - we will be secondary */
			data->secondary = chanctx_conf;
		}

		/* FIXME: TCM load per interface? or need something per link? */
		if (data->primary == chanctx_conf)
			data->primary_load = mvm->tcm.result.load[mvmvif->id];
		else if (data->secondary == chanctx_conf)
			data->secondary_load = mvm->tcm.result.load[mvmvif->id];
		return;
	}

	/*
	 * STA / P2P Client, try to be primary if first vif. If we are in low
	 * latency mode, we are already in primary and just don't do much
	 */
	if (!data->primary || data->primary == chanctx_conf)
		data->primary = chanctx_conf;
	else if (!data->secondary)
		/* if secondary is not NULL, it might be a GO */
		data->secondary = chanctx_conf;

	/* FIXME: TCM load per interface? or need something per link? */
	if (data->primary == chanctx_conf)
		data->primary_load = mvm->tcm.result.load[mvmvif->id];
	else if (data->secondary == chanctx_conf)
		data->secondary_load = mvm->tcm.result.load[mvmvif->id];
	/*
	 * don't reduce the Tx power if one of these is true:
	 *  we are in LOOSE
	 *  BT is inactive
	 *  we are not associated
	 */
	if (iwl_get_coex_type(mvm, vif) == BT_COEX_LOOSE_LUT ||
	    le32_to_cpu(mvm->last_bt_notif.bt_activity_grading) == BT_OFF ||
	    !vif->cfg.assoc) {
		iwl_mvm_bt_coex_reduced_txp(mvm, link_info->ap_sta_id, false);
		iwl_mvm_bt_coex_enable_rssi_event(mvm, link_info, false, 0);
		return;
	}

	/* try to get the avg rssi from fw */
	ave_rssi = link_info->bf_data.ave_beacon_signal;

	/* if the RSSI isn't valid, fake it is very low */
	if (!ave_rssi)
		ave_rssi = -100;
	if (ave_rssi > -IWL_MVM_BT_COEX_EN_RED_TXP_THRESH) {
		if (iwl_mvm_bt_coex_reduced_txp(mvm, link_info->ap_sta_id,
						true))
			IWL_ERR(mvm, "Couldn't send BT_CONFIG cmd\n");
	} else if (ave_rssi < -IWL_MVM_BT_COEX_DIS_RED_TXP_THRESH) {
		if (iwl_mvm_bt_coex_reduced_txp(mvm, link_info->ap_sta_id,
						false))
			IWL_ERR(mvm, "Couldn't send BT_CONFIG cmd\n");
	}

	/* Begin to monitor the RSSI: it may influence the reduced Tx power */
	iwl_mvm_bt_coex_enable_rssi_event(mvm, link_info, true, ave_rssi);
}

/* must be called under rcu_read_lock */
static void iwl_mvm_bt_notif_iterator(void *_data, u8 *mac,
				      struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_bt_iterator_data *data = _data;
	struct iwl_mvm *mvm = data->mvm;
	unsigned int link_id;

	lockdep_assert_held(&mvm->mutex);

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		break;
	case NL80211_IFTYPE_AP:
		if (!mvmvif->ap_ibss_active)
			return;
		break;
	default:
		return;
	}

	for (link_id = 0; link_id < IEEE80211_MLD_MAX_NUM_LINKS; link_id++)
		iwl_mvm_bt_notif_per_link(mvm, vif, data, link_id);
}

static void iwl_mvm_bt_coex_notif_handle(struct iwl_mvm *mvm)
{
	struct iwl_bt_iterator_data data = {
		.mvm = mvm,
		.notif = &mvm->last_bt_notif,
	};
	struct iwl_bt_coex_ci_cmd cmd = {};
	u8 ci_bw_idx;

	/* Ignore updates if we are in force mode */
	if (unlikely(mvm->bt_force_ant_mode != BT_FORCE_ANT_DIS))
		return;

	rcu_read_lock();
	ieee80211_iterate_active_interfaces_atomic(
					mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
					iwl_mvm_bt_notif_iterator, &data);

	if (mvm->trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_AX210) {
		rcu_read_unlock();
		return;
	}

	iwl_mvm_bt_coex_tcm_based_ci(mvm, &data);

	if (data.primary) {
		struct ieee80211_chanctx_conf *chan = data.primary;
		if (WARN_ON(!chan->def.chan)) {
			rcu_read_unlock();
			return;
		}

		if (chan->def.width < NL80211_CHAN_WIDTH_40) {
			ci_bw_idx = 0;
		} else {
			if (chan->def.center_freq1 >
			    chan->def.chan->center_freq)
				ci_bw_idx = 2;
			else
				ci_bw_idx = 1;
		}

		cmd.bt_primary_ci =
			iwl_ci_mask[chan->def.chan->hw_value][ci_bw_idx];
		cmd.primary_ch_phy_id =
			cpu_to_le32(*((u16 *)data.primary->drv_priv));
	}

	if (data.secondary) {
		struct ieee80211_chanctx_conf *chan = data.secondary;
		if (WARN_ON(!data.secondary->def.chan)) {
			rcu_read_unlock();
			return;
		}

		if (chan->def.width < NL80211_CHAN_WIDTH_40) {
			ci_bw_idx = 0;
		} else {
			if (chan->def.center_freq1 >
			    chan->def.chan->center_freq)
				ci_bw_idx = 2;
			else
				ci_bw_idx = 1;
		}

		cmd.bt_secondary_ci =
			iwl_ci_mask[chan->def.chan->hw_value][ci_bw_idx];
		cmd.secondary_ch_phy_id =
			cpu_to_le32(*((u16 *)data.secondary->drv_priv));
	}

	rcu_read_unlock();

	/* Don't spam the fw with the same command over and over */
	if (memcmp(&cmd, &mvm->last_bt_ci_cmd, sizeof(cmd))) {
		if (iwl_mvm_send_cmd_pdu(mvm, BT_COEX_CI, 0,
					 sizeof(cmd), &cmd))
			IWL_ERR(mvm, "Failed to send BT_CI cmd\n");
		memcpy(&mvm->last_bt_ci_cmd, &cmd, sizeof(cmd));
	}
}

void iwl_mvm_rx_bt_coex_notif(struct iwl_mvm *mvm,
			      struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_bt_coex_profile_notif *notif = (void *)pkt->data;

	IWL_DEBUG_COEX(mvm, "BT Coex Notification received\n");
	IWL_DEBUG_COEX(mvm, "\tBT ci compliance %d\n", notif->bt_ci_compliance);
	IWL_DEBUG_COEX(mvm, "\tBT primary_ch_lut %d\n",
		       le32_to_cpu(notif->primary_ch_lut));
	IWL_DEBUG_COEX(mvm, "\tBT secondary_ch_lut %d\n",
		       le32_to_cpu(notif->secondary_ch_lut));
	IWL_DEBUG_COEX(mvm, "\tBT activity grading %d\n",
		       le32_to_cpu(notif->bt_activity_grading));

	/* remember this notification for future use: rssi fluctuations */
	memcpy(&mvm->last_bt_notif, notif, sizeof(mvm->last_bt_notif));

	iwl_mvm_bt_coex_notif_handle(mvm);
}

void iwl_mvm_bt_rssi_event(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			   enum ieee80211_rssi_event_data rssi_event)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	lockdep_assert_held(&mvm->mutex);

	/* Ignore updates if we are in force mode */
	if (unlikely(mvm->bt_force_ant_mode != BT_FORCE_ANT_DIS))
		return;

	/*
	 * Rssi update while not associated - can happen since the statistics
	 * are handled asynchronously
	 */
	if (mvmvif->deflink.ap_sta_id == IWL_MVM_INVALID_STA)
		return;

	/* No BT - reports should be disabled */
	if (le32_to_cpu(mvm->last_bt_notif.bt_activity_grading) == BT_OFF)
		return;

	IWL_DEBUG_COEX(mvm, "RSSI for %pM is now %s\n", vif->bss_conf.bssid,
		       rssi_event == RSSI_EVENT_HIGH ? "HIGH" : "LOW");

	/*
	 * Check if rssi is good enough for reduced Tx power, but not in loose
	 * scheme.
	 */
	if (rssi_event == RSSI_EVENT_LOW ||
	    iwl_get_coex_type(mvm, vif) == BT_COEX_LOOSE_LUT)
		ret = iwl_mvm_bt_coex_reduced_txp(mvm,
						  mvmvif->deflink.ap_sta_id,
						  false);
	else
		ret = iwl_mvm_bt_coex_reduced_txp(mvm,
						  mvmvif->deflink.ap_sta_id,
						  true);

	if (ret)
		IWL_ERR(mvm, "couldn't send BT_CONFIG HCMD upon RSSI event\n");
}

#define LINK_QUAL_AGG_TIME_LIMIT_DEF	(4000)
#define LINK_QUAL_AGG_TIME_LIMIT_BT_ACT	(1200)

u16 iwl_mvm_coex_agg_time_limit(struct iwl_mvm *mvm,
				struct ieee80211_sta *sta)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(mvmsta->vif);
	struct iwl_mvm_phy_ctxt *phy_ctxt = mvmvif->deflink.phy_ctxt;
	enum iwl_bt_coex_lut_type lut_type;

	if (mvm->last_bt_notif.ttc_status & BIT(phy_ctxt->id))
		return LINK_QUAL_AGG_TIME_LIMIT_DEF;

	if (le32_to_cpu(mvm->last_bt_notif.bt_activity_grading) <
	    BT_HIGH_TRAFFIC)
		return LINK_QUAL_AGG_TIME_LIMIT_DEF;

	lut_type = iwl_get_coex_type(mvm, mvmsta->vif);

	if (lut_type == BT_COEX_LOOSE_LUT || lut_type == BT_COEX_INVALID_LUT)
		return LINK_QUAL_AGG_TIME_LIMIT_DEF;

	/* tight coex, high bt traffic, reduce AGG time limit */
	return LINK_QUAL_AGG_TIME_LIMIT_BT_ACT;
}

bool iwl_mvm_bt_coex_is_mimo_allowed(struct iwl_mvm *mvm,
				     struct ieee80211_sta *sta)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(mvmsta->vif);
	struct iwl_mvm_phy_ctxt *phy_ctxt = mvmvif->deflink.phy_ctxt;
	enum iwl_bt_coex_lut_type lut_type;

	if (mvm->last_bt_notif.ttc_status & BIT(phy_ctxt->id))
		return true;

	if (le32_to_cpu(mvm->last_bt_notif.bt_activity_grading) <
	    BT_HIGH_TRAFFIC)
		return true;

	/*
	 * In Tight / TxTxDis, BT can't Rx while we Tx, so use both antennas
	 * since BT is already killed.
	 * In Loose, BT can Rx while we Tx, so forbid MIMO to let BT Rx while
	 * we Tx.
	 * When we are in 5GHz, we'll get BT_COEX_INVALID_LUT allowing MIMO.
	 */
	lut_type = iwl_get_coex_type(mvm, mvmsta->vif);
	return lut_type != BT_COEX_LOOSE_LUT;
}

bool iwl_mvm_bt_coex_is_ant_avail(struct iwl_mvm *mvm, u8 ant)
{
	if (ant & mvm->cfg->non_shared_ant)
		return true;

	return le32_to_cpu(mvm->last_bt_notif.bt_activity_grading) <
		BT_HIGH_TRAFFIC;
}

bool iwl_mvm_bt_coex_is_shared_ant_avail(struct iwl_mvm *mvm)
{
	return le32_to_cpu(mvm->last_bt_notif.bt_activity_grading) < BT_HIGH_TRAFFIC;
}

bool iwl_mvm_bt_coex_is_tpc_allowed(struct iwl_mvm *mvm,
				    enum nl80211_band band)
{
	u32 bt_activity = le32_to_cpu(mvm->last_bt_notif.bt_activity_grading);

	if (band != NL80211_BAND_2GHZ)
		return false;

	return bt_activity >= BT_LOW_TRAFFIC;
}

u8 iwl_mvm_bt_coex_get_single_ant_msk(struct iwl_mvm *mvm, u8 enabled_ants)
{
	if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_COEX_SCHEMA_2) &&
	    (mvm->cfg->non_shared_ant & enabled_ants))
		return mvm->cfg->non_shared_ant;

	return first_antenna(enabled_ants);
}

u8 iwl_mvm_bt_coex_tx_prio(struct iwl_mvm *mvm, struct ieee80211_hdr *hdr,
			   struct ieee80211_tx_info *info, u8 ac)
{
	__le16 fc = hdr->frame_control;
	bool mplut_enabled = iwl_mvm_is_mplut_supported(mvm);

	if (info->band != NL80211_BAND_2GHZ)
		return 0;

	if (unlikely(mvm->bt_tx_prio))
		return mvm->bt_tx_prio - 1;

	if (likely(ieee80211_is_data(fc))) {
		if (likely(ieee80211_is_data_qos(fc))) {
			switch (ac) {
			case IEEE80211_AC_BE:
				return mplut_enabled ? 1 : 0;
			case IEEE80211_AC_VI:
				return mplut_enabled ? 2 : 3;
			case IEEE80211_AC_VO:
				return 3;
			default:
				return 0;
			}
		} else if (is_multicast_ether_addr(hdr->addr1)) {
			return 3;
		} else
			return 0;
	} else if (ieee80211_is_mgmt(fc)) {
		return ieee80211_is_disassoc(fc) ? 0 : 3;
	} else if (ieee80211_is_ctl(fc)) {
		/* ignore cfend and cfendack frames as we never send those */
		return 3;
	}

	return 0;
}

void iwl_mvm_bt_coex_vif_change(struct iwl_mvm *mvm)
{
	iwl_mvm_bt_coex_notif_handle(mvm);
}
