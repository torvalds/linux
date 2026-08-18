// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024-2026 Intel Corporation
 */

#include <kunit/static_stub.h>

#include "constants.h"
#include "link.h"
#include "iface.h"
#include "mlo.h"
#include "hcmd.h"
#include "phy.h"
#include "fw/api/rs.h"
#include "fw/api/txq.h"
#include "fw/api/mac.h"

#include "fw/api/context.h"
#include "fw/dbg.h"

/**
 * struct iwl_mld_link_chan_load_threshold - channel load thresholds
 * @high_lim: level up transition thresholds, in percentage
 * @low_lim: level down transition thresholds, in percentage
 */
struct iwl_mld_link_chan_load_threshold {
	u8 high_lim;
	u8 low_lim;
};

static const struct iwl_mld_link_chan_load_threshold
link_chan_load_thresh_tbl[] = {
	[LINK_CHAN_LOAD_LVL1] = { .high_lim = 45, .low_lim = 40 },
	[LINK_CHAN_LOAD_LVL2] = { .high_lim = 70, .low_lim = 65 },
	[LINK_CHAN_LOAD_LVL3] = { .high_lim = 85, .low_lim = 80 },
};

int iwl_mld_send_link_cmd(struct iwl_mld *mld,
			  struct iwl_link_config_cmd *cmd,
			  enum iwl_ctxt_action action)
{
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON_ONCE(cmd->link_id == cpu_to_le32(FW_CTXT_ID_INVALID)))
		return -EINVAL;

	cmd->action = cpu_to_le32(action);
	ret = iwl_mld_send_cmd_pdu(mld,
				   WIDE_ID(MAC_CONF_GROUP, LINK_CONFIG_CMD),
				   cmd);
	if (ret)
		IWL_ERR(mld, "Failed to send LINK_CONFIG_CMD (action:%d): %d\n",
			action, ret);
	return ret;
}

static int iwl_mld_add_link_to_fw(struct iwl_mld *mld,
				  struct ieee80211_bss_conf *link_conf)
{
	struct ieee80211_vif *vif = link_conf->vif;
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld_link *link = iwl_mld_link_from_mac80211(link_conf);
	struct iwl_link_config_cmd cmd = {};

	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON(!link))
		return -EINVAL;

	cmd.link_id = cpu_to_le32(link->fw_id);
	cmd.mac_id = cpu_to_le32(mld_vif->fw_id);
	cmd.spec_link_id = link_conf->link_id;
	cmd.phy_id = cpu_to_le32(FW_CTXT_ID_INVALID);

	ether_addr_copy(cmd.local_link_addr, link_conf->addr);

	if (vif->type == NL80211_IFTYPE_ADHOC && link_conf->bssid)
		ether_addr_copy(cmd.ibss_bssid_addr, link_conf->bssid);

	return iwl_mld_send_link_cmd(mld, &cmd, FW_CTXT_ACTION_ADD);
}

/* Get the basic rates of the used band and add the mandatory ones */
static void iwl_mld_fill_rates(struct iwl_mld *mld,
			       struct ieee80211_bss_conf *link,
			       struct ieee80211_chanctx_conf *chan_ctx,
			       __le32 *cck_rates, __le32 *ofdm_rates)
{
	struct cfg80211_chan_def *chandef =
		iwl_mld_get_chandef_from_chanctx(mld, chan_ctx);
	struct ieee80211_supported_band *sband =
		mld->hw->wiphy->bands[chandef->chan->band];
	unsigned long basic = link->basic_rates;
	int lowest_present_ofdm = 100;
	int lowest_present_cck = 100;
	u32 cck = 0;
	u32 ofdm = 0;
	int i;

	for_each_set_bit(i, &basic, BITS_PER_LONG) {
		int hw = sband->bitrates[i].hw_value;

		if (hw >= IWL_FIRST_OFDM_RATE) {
			ofdm |= BIT(hw - IWL_FIRST_OFDM_RATE);
			if (lowest_present_ofdm > hw)
				lowest_present_ofdm = hw;
		} else {
			BUILD_BUG_ON(IWL_FIRST_CCK_RATE != 0);

			cck |= BIT(hw);
			if (lowest_present_cck > hw)
				lowest_present_cck = hw;
		}
	}

	/* Now we've got the basic rates as bitmaps in the ofdm and cck
	 * variables. This isn't sufficient though, as there might not
	 * be all the right rates in the bitmap. E.g. if the only basic
	 * rates are 5.5 Mbps and 11 Mbps, we still need to add 1 Mbps
	 * and 6 Mbps because the 802.11-2007 standard says in 9.6:
	 *
	 *    [...] a STA responding to a received frame shall transmit
	 *    its Control Response frame [...] at the highest rate in the
	 *    BSSBasicRateSet parameter that is less than or equal to the
	 *    rate of the immediately previous frame in the frame exchange
	 *    sequence ([...]) and that is of the same modulation class
	 *    ([...]) as the received frame. If no rate contained in the
	 *    BSSBasicRateSet parameter meets these conditions, then the
	 *    control frame sent in response to a received frame shall be
	 *    transmitted at the highest mandatory rate of the PHY that is
	 *    less than or equal to the rate of the received frame, and
	 *    that is of the same modulation class as the received frame.
	 *
	 * As a consequence, we need to add all mandatory rates that are
	 * lower than all of the basic rates to these bitmaps.
	 */

	if (lowest_present_ofdm > IWL_RATE_24M_INDEX)
		ofdm |= IWL_RATE_BIT_MSK(24) >> IWL_FIRST_OFDM_RATE;
	if (lowest_present_ofdm > IWL_RATE_12M_INDEX)
		ofdm |= IWL_RATE_BIT_MSK(12) >> IWL_FIRST_OFDM_RATE;
	/* 6M already there or needed so always add */
	ofdm |= IWL_RATE_BIT_MSK(6) >> IWL_FIRST_OFDM_RATE;

	/* CCK is a bit more complex with DSSS vs. HR/DSSS vs. ERP.
	 * Note, however:
	 *  - if no CCK rates are basic, it must be ERP since there must
	 *    be some basic rates at all, so they're OFDM => ERP PHY
	 *    (or we're in 5 GHz, and the cck bitmap will never be used)
	 *  - if 11M is a basic rate, it must be ERP as well, so add 5.5M
	 *  - if 5.5M is basic, 1M and 2M are mandatory
	 *  - if 2M is basic, 1M is mandatory
	 *  - if 1M is basic, that's the only valid ACK rate.
	 * As a consequence, it's not as complicated as it sounds, just add
	 * any lower rates to the ACK rate bitmap.
	 */
	if (lowest_present_cck > IWL_RATE_11M_INDEX)
		cck |= IWL_RATE_BIT_MSK(11) >> IWL_FIRST_CCK_RATE;
	if (lowest_present_cck > IWL_RATE_5M_INDEX)
		cck |= IWL_RATE_BIT_MSK(5) >> IWL_FIRST_CCK_RATE;
	if (lowest_present_cck > IWL_RATE_2M_INDEX)
		cck |= IWL_RATE_BIT_MSK(2) >> IWL_FIRST_CCK_RATE;
	/* 1M already there or needed so always add */
	cck |= IWL_RATE_BIT_MSK(1) >> IWL_FIRST_CCK_RATE;

	*cck_rates = cpu_to_le32((u32)cck);
	*ofdm_rates = cpu_to_le32((u32)ofdm);
}

static void iwl_mld_fill_protection_flags(struct iwl_mld *mld,
					  struct ieee80211_bss_conf *link,
					  __le32 *protection_flags)
{
	u8 protection_mode = link->ht_operation_mode &
				IEEE80211_HT_OP_MODE_PROTECTION;
	u8 ht_flag = LINK_PROT_FLG_HT_PROT | LINK_PROT_FLG_FAT_PROT;

	IWL_DEBUG_RATE(mld, "HT protection mode: %d\n", protection_mode);

	if (link->use_cts_prot)
		*protection_flags |= cpu_to_le32(LINK_PROT_FLG_TGG_PROTECT);

	/* See section 9.23.3.1 of IEEE 80211-2012.
	 * Nongreenfield HT STAs Present is not supported.
	 */
	switch (protection_mode) {
	case IEEE80211_HT_OP_MODE_PROTECTION_NONE:
		break;
	case IEEE80211_HT_OP_MODE_PROTECTION_NONMEMBER:
	case IEEE80211_HT_OP_MODE_PROTECTION_NONHT_MIXED:
		*protection_flags |= cpu_to_le32(ht_flag);
		break;
	case IEEE80211_HT_OP_MODE_PROTECTION_20MHZ:
		/* Protect when channel wider than 20MHz */
		if (link->chanreq.oper.width > NL80211_CHAN_WIDTH_20)
			*protection_flags |= cpu_to_le32(ht_flag);
		break;
	}
}

static u8 iwl_mld_mac80211_ac_to_fw_ac(enum ieee80211_ac_numbers ac)
{
	static const u8 mac80211_ac_to_fw[] = {
		AC_VO,
		AC_VI,
		AC_BE,
		AC_BK
	};

	return mac80211_ac_to_fw[ac];
}

static void iwl_mld_fill_qos_params(struct ieee80211_bss_conf *link,
				    struct iwl_ac_qos *ac, __le32 *qos_flags)
{
	struct iwl_mld_link *mld_link = iwl_mld_link_from_mac80211(link);

	/* no need to check mld_link since it is done in the caller */

	for (int mac_ac = 0; mac_ac < IEEE80211_NUM_ACS; mac_ac++) {
		u8 txf = iwl_mld_mac80211_ac_to_fw_tx_fifo(mac_ac);
		u8 fw_ac = iwl_mld_mac80211_ac_to_fw_ac(mac_ac);

		ac[fw_ac].cw_min =
			cpu_to_le16(mld_link->queue_params[mac_ac].cw_min);
		ac[fw_ac].cw_max =
			cpu_to_le16(mld_link->queue_params[mac_ac].cw_max);
		ac[fw_ac].edca_txop =
			cpu_to_le16(mld_link->queue_params[mac_ac].txop * 32);
		ac[fw_ac].aifsn = mld_link->queue_params[mac_ac].aifs;
		ac[fw_ac].fifos_mask = BIT(txf);
	}

	if (link->qos)
		*qos_flags |= cpu_to_le32(MAC_QOS_FLG_UPDATE_EDCA);

	if (link->chanreq.oper.width != NL80211_CHAN_WIDTH_20_NOHT)
		*qos_flags |= cpu_to_le32(MAC_QOS_FLG_TGN);
}

static bool iwl_mld_fill_mu_edca(struct iwl_mld *mld,
				 const struct iwl_mld_link *mld_link,
				 struct iwl_he_backoff_conf *trig_based_txf)
{
	for (int mac_ac = 0; mac_ac < IEEE80211_NUM_ACS; mac_ac++) {
		const struct ieee80211_he_mu_edca_param_ac_rec *mu_edca =
			&mld_link->queue_params[mac_ac].mu_edca_param_rec;
		u8 fw_ac = iwl_mld_mac80211_ac_to_fw_ac(mac_ac);

		if (!mld_link->queue_params[mac_ac].mu_edca)
			return false;

		trig_based_txf[fw_ac].cwmin =
			cpu_to_le16(mu_edca->ecw_min_max & 0xf);
		trig_based_txf[fw_ac].cwmax =
			cpu_to_le16((mu_edca->ecw_min_max & 0xf0) >> 4);
		trig_based_txf[fw_ac].aifsn =
			cpu_to_le16(mu_edca->aifsn & 0xf);
		trig_based_txf[fw_ac].mu_time =
			cpu_to_le16(mu_edca->mu_edca_timer);
	}
	return true;
}

int
iwl_mld_change_link_in_fw(struct iwl_mld *mld, struct ieee80211_bss_conf *link,
			  u32 changes)
{
	struct iwl_mld_link *mld_link = iwl_mld_link_from_mac80211(link);
	struct ieee80211_vif *vif = link->vif;
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct ieee80211_chanctx_conf *chan_ctx;
	struct iwl_link_config_cmd cmd = {};
	u32 flags = 0;

	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON(!mld_link))
		return -EINVAL;

	cmd.link_id = cpu_to_le32(mld_link->fw_id);
	cmd.spec_link_id = link->link_id;
	cmd.mac_id = cpu_to_le32(mld_vif->fw_id);

	chan_ctx = wiphy_dereference(mld->wiphy, mld_link->chan_ctx);

	cmd.phy_id = cpu_to_le32(chan_ctx ?
		iwl_mld_phy_from_mac80211(chan_ctx)->fw_id :
		FW_CTXT_ID_INVALID);

	ether_addr_copy(cmd.local_link_addr, link->addr);

	cmd.active = cpu_to_le32(mld_link->active);

	if ((changes & LINK_CONTEXT_MODIFY_ACTIVE) && !mld_link->active &&
	    mld_link->silent_deactivation) {
		/* We are de-activating a link that is having CSA with
		 * immediate quiet in EMLSR. Tell the firmware not to send any
		 * frame.
		 */
		cmd.block_tx = 1;
		mld_link->silent_deactivation = false;
	}

	if (vif->type == NL80211_IFTYPE_ADHOC && link->bssid)
		ether_addr_copy(cmd.ibss_bssid_addr, link->bssid);

	/* Channel context is needed to get the rates */
	if (chan_ctx)
		iwl_mld_fill_rates(mld, link, chan_ctx, &cmd.cck_rates,
				   &cmd.ofdm_rates);

	cmd.cck_short_preamble = cpu_to_le32(link->use_short_preamble);
	cmd.short_slot = cpu_to_le32(link->use_short_slot);

	iwl_mld_fill_protection_flags(mld, link, &cmd.protection_flags);

	iwl_mld_fill_qos_params(link, cmd.ac, &cmd.qos_flags);

	cmd.bi = cpu_to_le32(link->beacon_int);
	cmd.dtim_interval = cpu_to_le32(link->beacon_int * link->dtim_period);

	/* Configure HE parameters only if HE is supported, and only after
	 * the parameters are set in mac80211 (meaning after assoc)
	 */
	if (!link->he_support || iwlwifi_mod_params.disable_11ax ||
	    (vif->type == NL80211_IFTYPE_STATION && !vif->cfg.assoc)) {
		changes &= ~LINK_CONTEXT_MODIFY_HE_PARAMS;
		goto send_cmd;
	}

	/* ap_sta may be NULL if we're disconnecting */
	if (mld_vif->ap_sta) {
		struct ieee80211_link_sta *link_sta =
			link_sta_dereference_check(mld_vif->ap_sta,
						   link->link_id);

		if (WARN_ON(!link_sta))
			return -EINVAL;

		if (link_sta->he_cap.has_he &&
		    link_sta->he_cap.he_cap_elem.mac_cap_info[5] &
		    IEEE80211_HE_MAC_CAP5_OM_CTRL_UL_MU_DATA_DIS_RX)
			cmd.ul_mu_data_disable = 1;

		if (link_sta->uhr_cap.has_uhr &&
		    link_sta->uhr_cap.mac.mac_cap[0] &
				IEEE80211_UHR_MAC_CAP0_DPS_ASSIST_SUPP)
			flags |= LINK_FLG_DPS;

		if (link_sta->uhr_cap.has_uhr &&
		    link_sta->uhr_cap.mac.mac_cap[1] &
				IEEE80211_UHR_MAC_CAP1_DUO_SUPP)
			flags |= LINK_FLG_DUO;

		if (link_sta->uhr_cap.has_uhr &&
		    mld_vif->ap_sta->ext_mld_capa_ops &
				IEEE80211_UHR_ML_EXT_MLD_CAPA_ML_PM)
			flags |= LINK_FLG_MLPM;
	}

	cmd.htc_trig_based_pkt_ext = link->htc_trig_based_pkt_ext;

	if (link->uora_exists) {
		cmd.rand_alloc_ecwmin = link->uora_ocw_range & 0x7;
		cmd.rand_alloc_ecwmax = (link->uora_ocw_range >> 3) & 0x7;
	}

	if (iwl_mld_fill_mu_edca(mld, mld_link, cmd.trig_based_txf))
		flags |= LINK_FLG_MU_EDCA_CW;

	cmd.bss_color = link->he_bss_color.color;

	if (!link->he_bss_color.enabled)
		flags |= LINK_FLG_BSS_COLOR_DIS;

	cmd.frame_time_rts_th = cpu_to_le16(link->frame_time_rts_th);

	/* Block 26-tone RU OFDMA transmissions */
	if (mld_link->he_ru_2mhz_block)
		flags |= LINK_FLG_RU_2MHZ_BLOCK;

	if (link->nontransmitted) {
		ether_addr_copy(cmd.ref_bssid_addr, link->transmitter_bssid);
		cmd.bssid_index = link->bssid_index;
	}

	/* The only EHT parameter is puncturing, and starting from PHY cmd
	 * version 6 - it is sent there. For older versions of the PHY cmd,
	 * puncturing is not needed at all.
	 */
	if (WARN_ON(changes & LINK_CONTEXT_MODIFY_EHT_PARAMS))
		changes &= ~LINK_CONTEXT_MODIFY_EHT_PARAMS;

	if (link->uhr_support && link->npca.enabled) {
		flags |= LINK_FLG_NPCA;
		if (link->npca.moplen)
			cmd.npca_params.flags |= IWL_NPCA_FLAG_MAC_HDR_BASED;
		cmd.npca_params.dis_subch_bmap =
			cpu_to_le16(link->chanreq.oper.npca_punctured);
		cmd.npca_params.initial_qsrc = link->npca.init_qsrc;
		cmd.npca_params.min_dur_threshold = link->npca.min_dur_thresh;
		/* spec/mac80211 have these in units of 4 usec */
		cmd.npca_params.switch_delay =
			4 * link->npca.switch_delay;
		cmd.npca_params.switch_back_delay =
			4 * link->npca.switch_back_delay;
	}

send_cmd:
	cmd.modify_mask = cpu_to_le32(changes);
	cmd.flags = cpu_to_le32(flags);

	return iwl_mld_send_link_cmd(mld, &cmd, FW_CTXT_ACTION_MODIFY);
}

int iwl_mld_activate_link(struct iwl_mld *mld,
			  struct ieee80211_bss_conf *link)
{
	struct iwl_mld_link *mld_link = iwl_mld_link_from_mac80211(link);
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(link->vif);
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON(!mld_link || mld_link->active))
		return -EINVAL;

	mld_link->active = true;

	ret = iwl_mld_change_link_in_fw(mld, link,
					LINK_CONTEXT_MODIFY_ACTIVE);
	if (ret)
		mld_link->active = false;
	else
		mld_vif->last_link_activation_time =
			ktime_get_boottime_seconds();

	return ret;
}

void iwl_mld_deactivate_link(struct iwl_mld *mld,
			     struct ieee80211_bss_conf *link)
{
	struct iwl_mld_link *mld_link = iwl_mld_link_from_mac80211(link);
	struct iwl_probe_resp_data *probe_data;

	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON(!mld_link || !mld_link->active))
		return;

	iwl_mld_cancel_session_protection(mld, link->vif, link->link_id);

	/* If we deactivate the link, we will probably remove it, or switch
	 * channel. In both cases, the CSA or Notice of Absence information is
	 * now irrelevant. Remove the data here.
	 */
	probe_data = wiphy_dereference(mld->wiphy, mld_link->probe_resp_data);
	RCU_INIT_POINTER(mld_link->probe_resp_data, NULL);
	if (probe_data)
		kfree_rcu(probe_data, rcu_head);

	mld_link->active = false;

	iwl_mld_change_link_in_fw(mld, link, LINK_CONTEXT_MODIFY_ACTIVE);

	/* Now that the link is not active in FW, we don't expect any new
	 * notifications for it. Cancel the ones that are already pending
	 */
	iwl_mld_cancel_notifications_of_object(mld, IWL_MLD_OBJECT_TYPE_LINK,
					       mld_link->fw_id);
}

static void
iwl_mld_rm_link_from_fw(struct iwl_mld *mld, struct ieee80211_bss_conf *link)
{
	struct iwl_mld_link *mld_link = iwl_mld_link_from_mac80211(link);
	struct iwl_link_config_cmd cmd = {};

	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON(!mld_link))
		return;

	cmd.link_id = cpu_to_le32(mld_link->fw_id);
	cmd.spec_link_id = link->link_id;
	cmd.phy_id = cpu_to_le32(FW_CTXT_ID_INVALID);

	iwl_mld_send_link_cmd(mld, &cmd, FW_CTXT_ACTION_REMOVE);
}

IWL_MLD_ALLOC_FN(link, bss_conf)
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_mld_allocate_link_fw_id);

/* Constructor function for struct iwl_mld_link */
static int
iwl_mld_init_link(struct iwl_mld *mld, struct ieee80211_bss_conf *link,
		  struct iwl_mld_link *mld_link)
{
	mld_link->average_beacon_energy = 0;

	iwl_mld_init_internal_sta(&mld_link->bcast_sta);
	iwl_mld_init_internal_sta(&mld_link->mcast_sta);
	iwl_mld_init_internal_sta(&mld_link->mon_sta);

	return iwl_mld_allocate_link_fw_id(mld, &mld_link->fw_id, link);
}

/* Initializes the link structure, maps fw id to the ieee80211_bss_conf, and
 * adds a link to the fw
 */
int iwl_mld_add_link(struct iwl_mld *mld,
		     struct ieee80211_bss_conf *bss_conf)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(bss_conf->vif);
	struct iwl_mld_link *link = iwl_mld_link_from_mac80211(bss_conf);
	bool is_deflink = bss_conf == &bss_conf->vif->bss_conf;
	int ret;

	if (!link) {
		if (is_deflink) {
			link = &mld_vif->deflink;
		} else {
			link = kzalloc_obj(*link);
			if (!link)
				return -ENOMEM;
		}
	} else {
		WARN_ON(!mld->fw_status.in_hw_restart);
	}

	ret = iwl_mld_init_link(mld, bss_conf, link);
	if (ret)
		goto free;

	rcu_assign_pointer(mld_vif->link[bss_conf->link_id], link);

	ret = iwl_mld_add_link_to_fw(mld, bss_conf);
	if (ret) {
		RCU_INIT_POINTER(mld->fw_id_to_bss_conf[link->fw_id], NULL);
		RCU_INIT_POINTER(mld_vif->link[bss_conf->link_id], NULL);
		goto free;
	}

	return ret;

free:
	if (!is_deflink)
		kfree(link);
	return ret;
}

/* Remove link from fw, unmap the bss_conf, and destroy the link structure */
void iwl_mld_remove_link(struct iwl_mld *mld,
			 struct ieee80211_bss_conf *bss_conf)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(bss_conf->vif);
	struct iwl_mld_link *link = iwl_mld_link_from_mac80211(bss_conf);
	bool is_deflink = link == &mld_vif->deflink;

	if (WARN_ON(!link || link->active))
		return;

	iwl_mld_rm_link_from_fw(mld, bss_conf);
	/* Continue cleanup on failure */

	RCU_INIT_POINTER(mld_vif->link[bss_conf->link_id], NULL);

	if (WARN_ON(link->fw_id >= mld->fw->ucode_capa.num_links))
		return;

	RCU_INIT_POINTER(mld->fw_id_to_bss_conf[link->fw_id], NULL);

	if (!is_deflink)
		kfree_rcu(link, rcu_head);
}

void iwl_mld_handle_missed_beacon_notif(struct iwl_mld *mld,
					struct iwl_rx_packet *pkt)
{
	const struct iwl_missed_beacons_notif *notif = (const void *)pkt->data;
	union iwl_dbg_tlv_tp_data tp_data = { .fw_pkt = pkt };
	u32 fw_link_id = le32_to_cpu(notif->link_id);
	u32 missed_bcon = le32_to_cpu(notif->consec_missed_beacons);
	u32 missed_bcon_since_rx =
		le32_to_cpu(notif->consec_missed_beacons_since_last_rx);
	u32 scnd_lnk_bcn_lost =
		le32_to_cpu(notif->consec_missed_beacons_other_link);
	struct ieee80211_bss_conf *link_conf =
		iwl_mld_fw_id_to_link_conf(mld, fw_link_id);
	struct ieee80211_bss_conf *other_link;
	u32 bss_param_ch_cnt_link_id, other_link_fw_id;
	struct ieee80211_vif *vif;
	u8 link_id;

	if (WARN_ON(!link_conf))
		return;

	vif = link_conf->vif;
	link_id = link_conf->link_id;
	bss_param_ch_cnt_link_id = link_conf->bss_param_ch_cnt_link_id;

	IWL_DEBUG_INFO(mld,
		       "missed bcn link_id=%u, %u consecutive=%u\n",
		       link_id, missed_bcon, missed_bcon_since_rx);

	if (WARN_ON(!vif))
		return;

	iwl_dbg_tlv_time_point(&mld->fwrt,
			       IWL_FW_INI_TIME_POINT_MISSED_BEACONS, &tp_data);

	if (missed_bcon >= IWL_MLD_MISSED_BEACONS_THRESHOLD_LONG) {
		if (missed_bcon_since_rx >=
		    IWL_MLD_MISSED_BEACONS_SINCE_RX_THOLD) {
			ieee80211_connection_loss(vif);
			return;
		}
		IWL_WARN(mld,
			 "missed beacons exceeds threshold, but receiving data. Stay connected, Expect bugs.\n");
		return;
	}

	if (missed_bcon_since_rx > IWL_MLD_MISSED_BEACONS_THRESHOLD) {
		ieee80211_cqm_beacon_loss_notify(vif, GFP_ATOMIC);

		/* Not in EMLSR and we can't hear the link.
		 * Try to switch to a better link. EMLSR case is handled below.
		 */
		if (!iwl_mld_emlsr_active(vif)) {
			IWL_DEBUG_EHT(mld,
				      "missed beacons exceeds threshold. link_id=%u. Try to switch to a better link.\n",
				      link_id);
			iwl_mld_int_mlo_scan(mld, vif);
		}
	}

	/* no more logic if we're not in EMLSR */
	if (hweight16(vif->active_links) <= 1)
		return;

	/* We are processing a notification before link activation */
	if (le32_to_cpu(notif->other_link_id) == FW_CTXT_ID_INVALID)
		return;

	other_link_fw_id = le32_to_cpu(notif->other_link_id);
	other_link = iwl_mld_fw_id_to_link_conf(mld, other_link_fw_id);

	if (IWL_FW_CHECK(mld, !other_link, "link doesn't exist for: %d\n",
			 other_link_fw_id))
		return;

	IWL_DEBUG_EHT(mld,
		      "missed bcn link_id=%u: %u consecutive=%u, other link_id=%u: %u\n",
		      link_id, missed_bcon, missed_bcon_since_rx,
		      other_link->link_id, scnd_lnk_bcn_lost);

	/* Exit EMLSR if we lost more than
	 * IWL_MLD_MISSED_BEACONS_EXIT_ESR_THRESH beacons on boths links
	 * OR more than IWL_MLD_BCN_LOSS_EXIT_ESR_THRESH on current link.
	 * OR more than IWL_MLD_BCN_LOSS_EXIT_ESR_THRESH_BSS_PARAM_CHANGED
	 * on current link and the link's bss_param_ch_count has changed on
	 * the other link's beacon.
	 *
	 * When both links lose beacons, keep the primary (symmetric failure).
	 * When only the current link is sick, keep the other link.
	 */
	if (missed_bcon >= IWL_MLD_BCN_LOSS_EXIT_ESR_THRESH_2_LINKS &&
	    scnd_lnk_bcn_lost >= IWL_MLD_BCN_LOSS_EXIT_ESR_THRESH_2_LINKS) {
		iwl_mld_exit_emlsr(mld, vif, IWL_MLD_EMLSR_EXIT_MISSED_BEACON,
				   iwl_mld_get_primary_link(vif));
	} else if (missed_bcon >= IWL_MLD_BCN_LOSS_EXIT_ESR_THRESH ||
		   (bss_param_ch_cnt_link_id != link_id &&
		    missed_bcon >=
		    IWL_MLD_BCN_LOSS_EXIT_ESR_THRESH_BSS_PARAM_CHANGED)) {
		iwl_mld_exit_emlsr(mld, vif, IWL_MLD_EMLSR_EXIT_MISSED_BEACON,
				   iwl_mld_get_other_link(vif, link_id));
	}
}
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_mld_handle_missed_beacon_notif);

bool iwl_mld_cancel_missed_beacon_notif(struct iwl_mld *mld,
					struct iwl_rx_packet *pkt,
					u32 removed_link_id)
{
	struct iwl_missed_beacons_notif *notif = (void *)pkt->data;

	if (le32_to_cpu(notif->other_link_id) == removed_link_id) {
		/* Second link is being removed. Don't cancel the notification,
		 * but mark second link as invalid.
		 */
		notif->other_link_id = cpu_to_le32(FW_CTXT_ID_INVALID);
	}

	/* If the primary link is removed, cancel the notification */
	return le32_to_cpu(notif->link_id) == removed_link_id;
}

int iwl_mld_link_set_associated(struct iwl_mld *mld, struct ieee80211_vif *vif,
				struct ieee80211_bss_conf *link)
{
	return iwl_mld_change_link_in_fw(mld, link, LINK_CONTEXT_MODIFY_ALL &
					 ~(LINK_CONTEXT_MODIFY_ACTIVE |
					   LINK_CONTEXT_MODIFY_EHT_PARAMS));
}

struct iwl_mld_rssi_to_grade {
	s8 rssi[2];
	u16 grade;
};

#define RSSI_TO_GRADE_LINE_WITH_LB(_lb, _hb_uhb, _grade) \
	{ \
		.rssi = {_lb, _hb_uhb}, \
		.grade = _grade \
	}

#define RSSI_TO_GRADE_LINE(_rssi, _grade) \
	RSSI_TO_GRADE_LINE_WITH_LB(_rssi, _rssi, _grade)

/* Tables must be sorted by increasing RSSI */

/* 20 MHz Operational BW Grading Table */
static const struct iwl_mld_rssi_to_grade rssi_to_grade_20mhz[] = {
	RSSI_TO_GRADE_LINE_WITH_LB(-94, -95, 9),
	RSSI_TO_GRADE_LINE_WITH_LB(-92, -93, 17),
	RSSI_TO_GRADE_LINE_WITH_LB(-90, -90, 34),
	RSSI_TO_GRADE_LINE_WITH_LB(-87, -87, 52),
	RSSI_TO_GRADE_LINE_WITH_LB(-83, -84, 69),
	RSSI_TO_GRADE_LINE_WITH_LB(-79, -80, 103),
	RSSI_TO_GRADE_LINE_WITH_LB(-75, -75, 137),
	RSSI_TO_GRADE_LINE_WITH_LB(-72, -73, 155),
	RSSI_TO_GRADE_LINE_WITH_LB(-70, -71, 172),
	RSSI_TO_GRADE_LINE_WITH_LB(-67, -68, 206),
	RSSI_TO_GRADE_LINE_WITH_LB(-64, -65, 230),
	RSSI_TO_GRADE_LINE_WITH_LB(-59, -60, 258),
	RSSI_TO_GRADE_LINE_WITH_LB(-57, -58, 287),
	RSSI_TO_GRADE_LINE_WITH_LB(-52, -53, 310),
	RSSI_TO_GRADE_LINE_WITH_LB(-50, -50, 345),
};

/* 40 MHz Operational BW Grading Table */
static const struct iwl_mld_rssi_to_grade rssi_to_grade_40mhz[] = {
	RSSI_TO_GRADE_LINE_WITH_LB(-95, -95, 9),
	RSSI_TO_GRADE_LINE_WITH_LB(-93, -93, 17),
	RSSI_TO_GRADE_LINE_WITH_LB(-91, -92, 18),
	RSSI_TO_GRADE_LINE_WITH_LB(-89, -90, 34),
	RSSI_TO_GRADE_LINE_WITH_LB(-87, -87, 68),
	RSSI_TO_GRADE_LINE_WITH_LB(-84, -84, 104),
	RSSI_TO_GRADE_LINE_WITH_LB(-80, -81, 138),
	RSSI_TO_GRADE_LINE_WITH_LB(-77, -77, 206),
	RSSI_TO_GRADE_LINE_WITH_LB(-72, -72, 274),
	RSSI_TO_GRADE_LINE_WITH_LB(-69, -70, 310),
	RSSI_TO_GRADE_LINE_WITH_LB(-67, -68, 344),
	RSSI_TO_GRADE_LINE_WITH_LB(-64, -65, 412),
	RSSI_TO_GRADE_LINE_WITH_LB(-61, -62, 460),
	RSSI_TO_GRADE_LINE_WITH_LB(-56, -57, 516),
	RSSI_TO_GRADE_LINE_WITH_LB(-54, -55, 574),
	RSSI_TO_GRADE_LINE_WITH_LB(-49, -50, 620),
	RSSI_TO_GRADE_LINE_WITH_LB(-46, -47, 690),
};

/* 80 MHz Operational BW Grading Table */
static const struct iwl_mld_rssi_to_grade rssi_to_grade_80mhz[] = {
	RSSI_TO_GRADE_LINE(-95, 9),
	RSSI_TO_GRADE_LINE(-93, 17),
	RSSI_TO_GRADE_LINE(-92, 18),
	RSSI_TO_GRADE_LINE(-90, 34),
	RSSI_TO_GRADE_LINE(-89, 36),
	RSSI_TO_GRADE_LINE(-87, 68),
	RSSI_TO_GRADE_LINE(-83, 136),
	RSSI_TO_GRADE_LINE(-80, 208),
	RSSI_TO_GRADE_LINE(-77, 276),
	RSSI_TO_GRADE_LINE(-74, 412),
	RSSI_TO_GRADE_LINE(-69, 548),
	RSSI_TO_GRADE_LINE(-67, 620),
	RSSI_TO_GRADE_LINE(-66, 688),
	RSSI_TO_GRADE_LINE(-61, 824),
	RSSI_TO_GRADE_LINE(-59, 920),
	RSSI_TO_GRADE_LINE(-54, 1032),
	RSSI_TO_GRADE_LINE(-52, 1148),
	RSSI_TO_GRADE_LINE(-47, 1240),
	RSSI_TO_GRADE_LINE(-44, 1380),
};

/* 160 MHz Operational BW Grading Table */
static const struct iwl_mld_rssi_to_grade rssi_to_grade_160mhz[] = {
	RSSI_TO_GRADE_LINE(-95, 9),
	RSSI_TO_GRADE_LINE(-93, 17),
	RSSI_TO_GRADE_LINE(-92, 18),
	RSSI_TO_GRADE_LINE(-90, 34),
	RSSI_TO_GRADE_LINE(-89, 36),
	RSSI_TO_GRADE_LINE(-87, 68),
	RSSI_TO_GRADE_LINE(-86, 72),
	RSSI_TO_GRADE_LINE(-84, 136),
	RSSI_TO_GRADE_LINE(-81, 272),
	RSSI_TO_GRADE_LINE(-78, 416),
	RSSI_TO_GRADE_LINE(-75, 552),
	RSSI_TO_GRADE_LINE(-71, 824),
	RSSI_TO_GRADE_LINE(-67, 1096),
	RSSI_TO_GRADE_LINE(-65, 1240),
	RSSI_TO_GRADE_LINE(-63, 1376),
	RSSI_TO_GRADE_LINE(-59, 1648),
	RSSI_TO_GRADE_LINE(-57, 1840),
	RSSI_TO_GRADE_LINE(-52, 2064),
	RSSI_TO_GRADE_LINE(-50, 2296),
	RSSI_TO_GRADE_LINE(-46, 2480),
	RSSI_TO_GRADE_LINE(-42, 2760),
};

/* 320 MHz Operational BW Grading Table */
static const struct iwl_mld_rssi_to_grade rssi_to_grade_320mhz[] = {
	RSSI_TO_GRADE_LINE(-95, 9),
	RSSI_TO_GRADE_LINE(-93, 17),
	RSSI_TO_GRADE_LINE(-92, 18),
	RSSI_TO_GRADE_LINE(-90, 34),
	RSSI_TO_GRADE_LINE(-89, 36),
	RSSI_TO_GRADE_LINE(-87, 68),
	RSSI_TO_GRADE_LINE(-86, 72),
	RSSI_TO_GRADE_LINE(-84, 136),
	RSSI_TO_GRADE_LINE(-83, 144),
	RSSI_TO_GRADE_LINE(-81, 272),
	RSSI_TO_GRADE_LINE(-78, 544),
	RSSI_TO_GRADE_LINE(-75, 832),
	RSSI_TO_GRADE_LINE(-72, 1104),
	RSSI_TO_GRADE_LINE(-69, 1648),
	RSSI_TO_GRADE_LINE(-64, 2192),
	RSSI_TO_GRADE_LINE(-62, 2480),
	RSSI_TO_GRADE_LINE(-61, 2752),
	RSSI_TO_GRADE_LINE(-57, 3296),
	RSSI_TO_GRADE_LINE(-55, 3680),
	RSSI_TO_GRADE_LINE(-50, 4128),
	RSSI_TO_GRADE_LINE(-47, 4592),
	RSSI_TO_GRADE_LINE(-43, 4960),
	RSSI_TO_GRADE_LINE(-40, 5520),
};

#define DEFAULT_CHAN_LOAD_2GHZ	30
#define DEFAULT_CHAN_LOAD_5GHZ	15
#define DEFAULT_CHAN_LOAD_6GHZ	0

/* Factors calculation is done with fixed-point with a scaling factor of 1/256 */
#define SCALE_FACTOR 256
#define MAX_CHAN_LOAD 256

static void
iwl_mld_apply_puncturing_penalty(const struct ieee80211_bss_conf *link_conf,
				 unsigned int *grade, int bw_mhz)
{
	unsigned int n_punctured, n_subchannels;

	/* Puncturing only applicable for BW >= 80 MHz */
	if (bw_mhz < 80)
		return;

	n_punctured = hweight16(link_conf->chanreq.oper.punctured);
	if (n_punctured == 0)
		return;

	n_subchannels = bw_mhz / 20;

	*grade = *grade * (n_subchannels - n_punctured) / n_subchannels;
}

int iwl_mld_get_chan_load_from_element(struct iwl_mld *mld,
				       struct ieee80211_bss_conf *link_conf)
{
	const struct cfg80211_bss_ies *ies;
	const struct element *bss_load_elem = NULL;
	const struct ieee80211_bss_load_elem *bss_load;

	guard(rcu)();

	ies = rcu_dereference(link_conf->bss->beacon_ies);
	if (ies)
		bss_load_elem = cfg80211_find_elem(WLAN_EID_QBSS_LOAD,
						   ies->data, ies->len);

	if (!bss_load_elem ||
	    bss_load_elem->datalen != sizeof(*bss_load))
		return -EINVAL;

	bss_load = (const void *)bss_load_elem->data;

	return bss_load->channel_util;
}

static unsigned int
iwl_mld_get_chan_load_by_us(struct iwl_mld *mld,
			    struct ieee80211_bss_conf *link_conf,
			    bool expect_active_link)
{
	struct iwl_mld_link *mld_link = iwl_mld_link_from_mac80211(link_conf);
	struct ieee80211_chanctx_conf *chan_ctx;
	struct iwl_mld_phy *phy;

	if (!mld_link || !mld_link->active) {
		WARN_ON(expect_active_link);
		return 0;
	}

	if (WARN_ONCE(!rcu_access_pointer(mld_link->chan_ctx),
		      "Active link (%u) without channel ctxt assigned!\n",
		      link_conf->link_id))
		return 0;

	chan_ctx = wiphy_dereference(mld->wiphy, mld_link->chan_ctx);
	phy = iwl_mld_phy_from_mac80211(chan_ctx);

	return phy->channel_load_by_us;
}

/* Returns error if the channel utilization element is invalid/unavailable */
int iwl_mld_get_chan_load_by_others(struct iwl_mld *mld,
				    struct ieee80211_bss_conf *link_conf,
				    bool expect_active_link)
{
	int chan_load;
	unsigned int chan_load_by_us;

	/* get overall load */
	chan_load = iwl_mld_get_chan_load_from_element(mld, link_conf);
	if (chan_load < 0)
		return chan_load;

	chan_load_by_us = iwl_mld_get_chan_load_by_us(mld, link_conf,
						      expect_active_link);

	/* channel load by us is given in percentage */
	chan_load_by_us =
		NORMALIZE_PERCENT_TO_255(chan_load_by_us);

	/* Use only values that firmware sends that can possibly be valid */
	if (chan_load_by_us <= chan_load)
		chan_load -= chan_load_by_us;

	return chan_load;
}

/* Returns whether internal MLO Scan needs to be triggered */
bool iwl_mld_chan_load_requires_scan(struct iwl_mld *mld,
				     struct ieee80211_bss_conf *link_conf,
				     u32 new_chan_load)
{
	struct iwl_mld_link *mld_link = iwl_mld_link_from_mac80211(link_conf);
	enum iwl_mld_link_chan_load_level new_lvl;
	bool scan_trig = false;

	if (WARN_ON(!mld_link))
		return false;

	/* For each Level,
	 * First check if high limit threshold crosses
	 * If not then, check if low limit threshold crosses
	 * Set new level based on low limit thresh only if old level
	 * is not lower than level threshold
	 */
	for (new_lvl = LINK_CHAN_LOAD_LVL_MAX;
	     new_lvl > LINK_CHAN_LOAD_LVL_NONE; new_lvl--) {
		if (new_chan_load >=
		    link_chan_load_thresh_tbl[new_lvl].high_lim)
			break;
		if (new_chan_load >=
		    link_chan_load_thresh_tbl[new_lvl].low_lim &&
		    mld_link->chan_load_lvl >= new_lvl)
			break;
	}

	/* Trigger scan only for Level Up Transition */
	if (new_lvl > mld_link->chan_load_lvl)
		scan_trig = true;

	IWL_DEBUG_EHT(mld,
		      "Link %d: chan_load=%d%%, old_lvl=%d, new_lvl=%d, scan_trig=%d\n",
		      link_conf->link_id, new_chan_load,
		      mld_link->chan_load_lvl, new_lvl, scan_trig);

	/* Update computed new level */
	mld_link->chan_load_lvl = new_lvl;

	return scan_trig;
}
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_mld_chan_load_requires_scan);

static unsigned int
iwl_mld_get_default_chan_load(struct ieee80211_bss_conf *link_conf)
{
	enum nl80211_band band = link_conf->chanreq.oper.chan->band;

	switch (band) {
	case NL80211_BAND_2GHZ:
		return DEFAULT_CHAN_LOAD_2GHZ;
	case NL80211_BAND_5GHZ:
		return DEFAULT_CHAN_LOAD_5GHZ;
	case NL80211_BAND_6GHZ:
		return DEFAULT_CHAN_LOAD_6GHZ;
	default:
		WARN_ON(1);
		return 0;
	}
}

unsigned int iwl_mld_get_chan_load(struct iwl_mld *mld,
				   struct ieee80211_bss_conf *link_conf)
{
	int chan_load;

	chan_load = iwl_mld_get_chan_load_by_others(mld, link_conf, false);
	if (chan_load >= 0)
		return chan_load;

	/* No information from the element, take the defaults */
	chan_load = iwl_mld_get_default_chan_load(link_conf);

	/* The defaults are given in percentage */
	return NORMALIZE_PERCENT_TO_255(chan_load);
}

static unsigned int
iwl_mld_get_avail_chan_load(struct iwl_mld *mld,
			    struct ieee80211_bss_conf *link_conf)
{
	return MAX_CHAN_LOAD - iwl_mld_get_chan_load(mld, link_conf);
}

VISIBLE_IF_IWLWIFI_KUNIT s8
iwl_mld_get_dup_beacon_rssi_adjust(struct iwl_mld *mld,
				   struct ieee80211_bss_conf *link_conf)
{
	const struct ieee80211_he_6ghz_oper *he_6ghz_oper;
	const struct cfg80211_bss_ies *beacon_ies;
	const struct element *elem;

	KUNIT_STATIC_STUB_REDIRECT(iwl_mld_get_dup_beacon_rssi_adjust,
				   mld, link_conf);

	/* Duplicated beacon feature is only specific to 6 GHz */
	if (WARN_ONCE(link_conf->chanreq.oper.chan->band != NL80211_BAND_6GHZ,
		      "Unexpected band %d\n",
		      link_conf->chanreq.oper.chan->band))
		return 0;

	lockdep_assert_wiphy(mld->wiphy);

	beacon_ies = wiphy_dereference(mld->wiphy, link_conf->bss->beacon_ies);
	if (!beacon_ies)
		return 0;

	elem = cfg80211_find_ext_elem(WLAN_EID_EXT_HE_OPERATION,
				      beacon_ies->data, beacon_ies->len);
	if (!elem ||
	    elem->datalen < sizeof(struct ieee80211_he_operation) + 1 ||
	    elem->datalen < ieee80211_he_oper_size(&elem->data[1]))
		return 0;

	he_6ghz_oper = ieee80211_he_6ghz_oper((const void *)&elem->data[1]);
	if (!he_6ghz_oper)
		return 0;

	if (!(he_6ghz_oper->control & IEEE80211_HE_6GHZ_OPER_CTRL_DUP_BEACON))
		return 0;

	/* Apply adjustment based on operational bandwidth */
	switch (link_conf->chanreq.oper.width) {
	case NL80211_CHAN_WIDTH_20:
	case NL80211_CHAN_WIDTH_20_NOHT:
		return 0;
	case NL80211_CHAN_WIDTH_40:
		return 3;
	case NL80211_CHAN_WIDTH_80:
		return 6;
	case NL80211_CHAN_WIDTH_160:
		return 9;
	case NL80211_CHAN_WIDTH_320:
		return 12;
	default:
		WARN_ONCE(1, "Unexpected channel width: %d\n",
			  link_conf->chanreq.oper.width);
		return 0;
	}
}
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_mld_get_dup_beacon_rssi_adjust);

static s8 iwl_mld_get_primary_psd(const struct ieee80211_parsed_tpe_psd *psd,
				  const struct cfg80211_chan_def *chandef,
				  int bw_mhz)
{
	int start_freq, primary_idx;

	if (!psd->valid)
		return S8_MAX;

	start_freq = chandef->center_freq1 - (bw_mhz / 2);
	primary_idx = (chandef->chan->center_freq - start_freq - 10) / 20;

	if (primary_idx < 0 || primary_idx >= psd->count)
		return S8_MAX;

	/* TPE element stores PSD limit as value * 2 */
	return psd->power[primary_idx] / 2;
}

VISIBLE_IF_IWLWIFI_KUNIT s8
iwl_mld_get_psd_eirp_rssi_adjust(struct ieee80211_bss_conf *link_conf)
{
	const struct ieee80211_parsed_tpe *tpe = &link_conf->tpe;
	s8 psd_20mhz, psd_oper, psd_local, psd_reg, psd_boost;
	s8 min_20mhz, min_oper, adjustment, ap_power_limit;
	s8 psd_avg_local = S8_MAX, psd_avg_reg = S8_MAX;
	s8 eirp_20mhz, eirp_oper, eirp_local, eirp_reg;
	int bw_mhz, num_subchans;
	u8 bw_index;

	KUNIT_STATIC_STUB_REDIRECT(iwl_mld_get_psd_eirp_rssi_adjust,
				   link_conf);

	/* PSD/EIRP adjustment is only specific to 6 GHz */
	if (WARN_ONCE(link_conf->chanreq.oper.chan->band != NL80211_BAND_6GHZ,
		      "PSD/EIRP adjustment called for non-6 GHz band %d\n",
		      link_conf->chanreq.oper.chan->band))
		return 0;

	bw_mhz = nl80211_chan_width_to_mhz(link_conf->chanreq.oper.width);

	switch (bw_mhz) {
	case 20:
		bw_index = 0;
		break;
	case 40:
		bw_index = 1;
		break;
	case 80:
		bw_index = 2;
		break;
	case 160:
		bw_index = 3;
		break;
	case 320:
		bw_index = 4;
		break;
	default:
		WARN_ONCE(1, "Unexpected bandwidth: %d MHz\n", bw_mhz);
		return 0;
	}

	if (link_conf->power_type == IEEE80211_REG_VLP_AP)
		ap_power_limit = 14;
	else
		ap_power_limit = 23;

	/* Primary 20 MHz PSD */
	psd_local = iwl_mld_get_primary_psd(&tpe->psd_local[0],
					    &link_conf->chanreq.oper,
					    bw_mhz);
	psd_reg = iwl_mld_get_primary_psd(&tpe->psd_reg_client[0],
					  &link_conf->chanreq.oper,
					  bw_mhz);
	psd_20mhz = min(psd_local, psd_reg);

	/* TPE element stores EIRP limit as value * 2 */
	eirp_local = (tpe->max_local[0].valid && tpe->max_local[0].count > 0) ?
			tpe->max_local[0].power[0] / 2 : S8_MAX;
	eirp_reg = (tpe->max_reg_client[0].valid &&
		    tpe->max_reg_client[0].count > 0) ?
		      tpe->max_reg_client[0].power[0] / 2 : S8_MAX;
	eirp_20mhz = min(eirp_local, eirp_reg);

	num_subchans = bw_mhz / 20;

	if (tpe->psd_local[0].valid) {
		int sum_local = 0, valid_local = 0;
		int count_local = min(num_subchans, tpe->psd_local[0].count);

		for (int i = 0; i < count_local; i++) {
			if (tpe->psd_local[0].power[i] != S8_MIN) {
				sum_local += tpe->psd_local[0].power[i];
				valid_local++;
			}
		}
		/* TPE element stores PSD limit as value * 2 */
		if (valid_local > 0)
			psd_avg_local = sum_local / valid_local / 2;
	}

	if (tpe->psd_reg_client[0].valid) {
		int sum_reg = 0, valid_reg = 0;
		int count_reg = min(num_subchans, tpe->psd_reg_client[0].count);

		for (int i = 0; i < count_reg; i++) {
			if (tpe->psd_reg_client[0].power[i] != S8_MIN) {
				sum_reg +=
					tpe->psd_reg_client[0].power[i];
				valid_reg++;
			}
		}
		/* TPE element stores PSD limit as value * 2 */
		if (valid_reg > 0)
			psd_avg_reg = sum_reg / valid_reg / 2;
	}

	psd_oper = min(psd_avg_local, psd_avg_reg);

	/* TPE element stores EIRP limit as value * 2 */
	eirp_local = (tpe->max_local[0].valid &&
		      tpe->max_local[0].count > bw_index) ?
			tpe->max_local[0].power[bw_index] / 2 : S8_MAX;
	eirp_reg = (tpe->max_reg_client[0].valid &&
		    tpe->max_reg_client[0].count > bw_index) ?
		      tpe->max_reg_client[0].power[bw_index] / 2 : S8_MAX;
	eirp_oper = min(eirp_local, eirp_reg);

	min_20mhz = min(ap_power_limit, min(eirp_20mhz, psd_20mhz));

	/* PSD boost: 10*log10(BW/20) approximated as 3*ilog2(BW/20) */
	psd_boost = 3 * ilog2(bw_mhz / 20);

	/* Use int for psd_oper + psd_boost to prevent s8 overflow */
	min_oper = min(ap_power_limit,
		       min(eirp_oper,
			   (s8)min_t(int, psd_oper + psd_boost, S8_MAX)));

	adjustment = max(min_oper - min_20mhz, 0);

	return adjustment;
}
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_mld_get_psd_eirp_rssi_adjust);

/* This function calculates the grade of a link. Returns 0 in error case */
unsigned int iwl_mld_get_link_grade(struct iwl_mld *mld,
				    struct ieee80211_bss_conf *link_conf)
{
	const struct iwl_mld_rssi_to_grade *grade_table;
	enum nl80211_band band;
	int rssi_idx, table_size, bw_mhz;
	s32 link_rssi;
	unsigned int grade;

	if (WARN_ON_ONCE(!link_conf || !link_conf->bss))
		return 0;

	bw_mhz = nl80211_chan_width_to_mhz(link_conf->chanreq.oper.width);
	if (bw_mhz < 0)
		return 0;

	band = link_conf->chanreq.oper.chan->band;
	if (WARN_ONCE(band != NL80211_BAND_2GHZ &&
		      band != NL80211_BAND_5GHZ &&
		      band != NL80211_BAND_6GHZ,
		      "Invalid band (%u)\n", band))
		return 0;

	link_rssi = MBM_TO_DBM(link_conf->bss->signal);
	/*
	 * For 6 GHz the RSSI of the beacons is lower than
	 * the RSSI of the data.
	 */
	if (band == NL80211_BAND_6GHZ && link_rssi) {
		s8 rssi_adj_6g =
			iwl_mld_get_dup_beacon_rssi_adjust(mld, link_conf);

		if (!rssi_adj_6g)
			rssi_adj_6g =
				iwl_mld_get_psd_eirp_rssi_adjust(link_conf);

		if (!rssi_adj_6g)
			rssi_adj_6g = 4;

		link_rssi += rssi_adj_6g;
	}

	/* Select grading table based on operational bandwidth */
	switch (bw_mhz) {
	case 20:
		grade_table = rssi_to_grade_20mhz;
		table_size = ARRAY_SIZE(rssi_to_grade_20mhz);
		break;
	case 40:
		grade_table = rssi_to_grade_40mhz;
		table_size = ARRAY_SIZE(rssi_to_grade_40mhz);
		break;
	case 80:
		grade_table = rssi_to_grade_80mhz;
		table_size = ARRAY_SIZE(rssi_to_grade_80mhz);
		break;
	case 160:
		grade_table = rssi_to_grade_160mhz;
		table_size = ARRAY_SIZE(rssi_to_grade_160mhz);
		break;
	case 320:
		grade_table = rssi_to_grade_320mhz;
		table_size = ARRAY_SIZE(rssi_to_grade_320mhz);
		break;
	default:
		WARN_ONCE(1, "Invalid bandwidth: %d MHz\n", bw_mhz);
		return 0;
	}

	/* Initialize grade to maximum value from selected table */
	grade = grade_table[table_size - 1].grade;

	rssi_idx = band == NL80211_BAND_2GHZ ? 0 : 1;

	/* No valid RSSI - take the lowest grade from selected table */
	if (!link_rssi)
		link_rssi = grade_table[0].rssi[rssi_idx];

	IWL_DEBUG_EHT(mld,
		      "Calculating grade of link %d: band = %d, BW = %d, punct subchannels = 0x%x RSSI = %d\n",
		      link_conf->link_id, band, bw_mhz,
		      link_conf->chanreq.oper.punctured, link_rssi);

	/* Get grade based on RSSI from the bandwidth-specific table */
	for (int i = 0; i < table_size; i++) {
		const struct iwl_mld_rssi_to_grade *line = &grade_table[i];

		if (link_rssi > line->rssi[rssi_idx])
			continue;
		grade = line->grade;
		break;
	}

	/* Apply the channel load and puncturing factors */
	grade = grade * iwl_mld_get_avail_chan_load(mld, link_conf) /
		SCALE_FACTOR;

	iwl_mld_apply_puncturing_penalty(link_conf, &grade, bw_mhz);

	IWL_DEBUG_EHT(mld, "Link %d's grade: %d\n", link_conf->link_id, grade);

	return grade;
}
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_mld_get_link_grade);

void iwl_mld_handle_beacon_filter_notif(struct iwl_mld *mld,
					struct iwl_rx_packet *pkt)
{
	const struct iwl_beacon_filter_notif *notif = (const void *)pkt->data;
	u32 link_id = le32_to_cpu(notif->link_id);
	struct ieee80211_bss_conf *link_conf =
		iwl_mld_fw_id_to_link_conf(mld, link_id);
	struct iwl_mld_link *mld_link;

	if (IWL_FW_CHECK(mld, !link_conf, "invalid link ID %d\n", link_id))
		return;

	mld_link = iwl_mld_link_from_mac80211(link_conf);
	if (WARN_ON_ONCE(!mld_link))
		return;

	mld_link->average_beacon_energy = le32_to_cpu(notif->average_energy);
}
