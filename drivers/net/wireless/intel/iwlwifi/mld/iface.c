// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */
#include <net/cfg80211.h>

#include "iface.h"
#include "hcmd.h"
#include "key.h"
#include "mlo.h"
#include "mac80211.h"

#include "fw/api/context.h"
#include "fw/api/mac.h"
#include "fw/api/time-event.h"
#include "fw/api/datapath.h"

/* Cleanup function for struct iwl_mld_vif, will be called in restart */
void iwl_mld_cleanup_vif(void *data, u8 *mac, struct ieee80211_vif *vif)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld *mld = mld_vif->mld;
	struct iwl_mld_link *link;

	/* EMLSR is turned back on during recovery */
	vif->driver_flags &= ~IEEE80211_VIF_EML_ACTIVE;

	mld_vif->roc_activity = ROC_NUM_ACTIVITIES;

	for_each_mld_vif_valid_link(mld_vif, link) {
		iwl_mld_cleanup_link(mld_vif->mld, link);

		/* Correctly allocated primary link in non-MLO mode */
		if (!ieee80211_vif_is_mld(vif) &&
		    link_id == 0 && link == &mld_vif->deflink)
			continue;

		if (vif->active_links & BIT(link_id))
			continue;

		/* Should not happen as link removal should always succeed */
		WARN_ON(1);
		if (link != &mld_vif->deflink)
			kfree_rcu(link, rcu_head);
		RCU_INIT_POINTER(mld_vif->link[link_id], NULL);
	}

	ieee80211_iter_keys(mld->hw, vif, iwl_mld_cleanup_keys_iter, NULL);

	CLEANUP_STRUCT(mld_vif);
}

static int iwl_mld_send_mac_cmd(struct iwl_mld *mld,
				struct iwl_mac_config_cmd *cmd)
{
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	ret = iwl_mld_send_cmd_pdu(mld,
				   WIDE_ID(MAC_CONF_GROUP, MAC_CONFIG_CMD),
				   cmd);
	if (ret)
		IWL_ERR(mld, "Failed to send MAC_CONFIG_CMD ret = %d\n", ret);

	return ret;
}

int iwl_mld_mac80211_iftype_to_fw(const struct ieee80211_vif *vif)
{
	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		return vif->p2p ? FW_MAC_TYPE_P2P_STA : FW_MAC_TYPE_BSS_STA;
	case NL80211_IFTYPE_AP:
		return FW_MAC_TYPE_GO;
	case NL80211_IFTYPE_MONITOR:
		return FW_MAC_TYPE_LISTENER;
	case NL80211_IFTYPE_P2P_DEVICE:
		return FW_MAC_TYPE_P2P_DEVICE;
	case NL80211_IFTYPE_ADHOC:
		return FW_MAC_TYPE_IBSS;
	default:
		WARN_ON_ONCE(1);
	}
	return FW_MAC_TYPE_BSS_STA;
}

static bool iwl_mld_is_nic_ack_enabled(struct iwl_mld *mld,
				       struct ieee80211_vif *vif)
{
	const struct ieee80211_supported_band *sband;
	const struct ieee80211_sta_he_cap *own_he_cap;

	lockdep_assert_wiphy(mld->wiphy);

	/* This capability is the same for all bands,
	 * so take it from one of them.
	 */
	sband = mld->hw->wiphy->bands[NL80211_BAND_2GHZ];
	own_he_cap = ieee80211_get_he_iftype_cap_vif(sband, vif);

	return own_he_cap && (own_he_cap->he_cap_elem.mac_cap_info[2] &
			       IEEE80211_HE_MAC_CAP2_ACK_EN);
}

/* fill the common part for all interface types */
static void iwl_mld_mac_cmd_fill_common(struct iwl_mld *mld,
					struct ieee80211_vif *vif,
					struct iwl_mac_config_cmd *cmd,
					u32 action)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct ieee80211_bss_conf *link_conf;
	unsigned int link_id;

	lockdep_assert_wiphy(mld->wiphy);

	cmd->id_and_color = cpu_to_le32(mld_vif->fw_id);
	cmd->action = cpu_to_le32(action);

	cmd->mac_type =
		cpu_to_le32(iwl_mld_mac80211_iftype_to_fw(vif));

	memcpy(cmd->local_mld_addr, vif->addr, ETH_ALEN);

	if (iwlwifi_mod_params.disable_11ax)
		return;

	cmd->nic_not_ack_enabled =
		cpu_to_le32(!iwl_mld_is_nic_ack_enabled(mld, vif));

	/* If we have MLO enabled, then the firmware needs to enable
	 * address translation for the station(s) we add. That depends
	 * on having EHT enabled in firmware, which in turn depends on
	 * mac80211 in the code below.
	 * However, mac80211 doesn't enable HE/EHT until it has parsed
	 * the association response successfully, so just skip all that
	 * and enable both when we have MLO.
	 */
	if (ieee80211_vif_is_mld(vif)) {
		if (vif->type == NL80211_IFTYPE_AP)
			cmd->he_ap_support = cpu_to_le16(1);
		else
			cmd->he_support = cpu_to_le16(1);

		cmd->eht_support = cpu_to_le32(1);
		return;
	}

	for_each_vif_active_link(vif, link_conf, link_id) {
		if (!link_conf->he_support)
			continue;

		if (vif->type == NL80211_IFTYPE_AP)
			cmd->he_ap_support = cpu_to_le16(1);
		else
			cmd->he_support = cpu_to_le16(1);

		/* EHT, if supported, was already set above */
		break;
	}
}

static void iwl_mld_fill_mac_cmd_sta(struct iwl_mld *mld,
				     struct ieee80211_vif *vif, u32 action,
				     struct iwl_mac_config_cmd *cmd)
{
	struct ieee80211_bss_conf *link;
	u32 twt_policy = 0;
	int link_id;

	lockdep_assert_wiphy(mld->wiphy);

	WARN_ON(vif->type != NL80211_IFTYPE_STATION);

	/* We always want to hear MCAST frames, if we're not authorized yet,
	 * we'll drop them.
	 */
	cmd->filter_flags |= cpu_to_le32(MAC_CFG_FILTER_ACCEPT_GRP);

	/* Adding a MAC ctxt with is_assoc set is not allowed in fw
	 * (and shouldn't happen)
	 */
	if (vif->cfg.assoc && action != FW_CTXT_ACTION_ADD) {
		cmd->client.is_assoc = 1;

		if (!iwl_mld_vif_from_mac80211(vif)->authorized)
			cmd->client.data_policy |=
				cpu_to_le16(COEX_HIGH_PRIORITY_ENABLE);
	} else {
		/* Allow beacons to pass through as long as we are not
		 * associated
		 */
		cmd->filter_flags |= cpu_to_le32(MAC_CFG_FILTER_ACCEPT_BEACON);
	}

	cmd->client.assoc_id = cpu_to_le16(vif->cfg.aid);

	if (ieee80211_vif_is_mld(vif)) {
		u16 esr_transition_timeout =
			u16_get_bits(vif->cfg.eml_cap,
				     IEEE80211_EML_CAP_TRANSITION_TIMEOUT);

		cmd->client.esr_transition_timeout =
			min_t(u16, IEEE80211_EML_CAP_TRANSITION_TIMEOUT_128TU,
			      esr_transition_timeout);
		cmd->client.medium_sync_delay =
			cpu_to_le16(vif->cfg.eml_med_sync_delay);
	}

	for_each_vif_active_link(vif, link, link_id) {
		if (!link->he_support)
			continue;

		if (link->twt_requester)
			twt_policy |= TWT_SUPPORTED;
		if (link->twt_protected)
			twt_policy |= PROTECTED_TWT_SUPPORTED;
		if (link->twt_broadcast)
			twt_policy |= BROADCAST_TWT_SUPPORTED;
	}

	if (!iwlwifi_mod_params.disable_11ax)
		cmd->client.data_policy |= cpu_to_le16(twt_policy);

	if (vif->probe_req_reg && vif->cfg.assoc && vif->p2p)
		cmd->filter_flags |=
			cpu_to_le32(MAC_CFG_FILTER_ACCEPT_PROBE_REQ);

	if (vif->p2p)
		cmd->client.ctwin =
			cpu_to_le32(vif->bss_conf.p2p_noa_attr.oppps_ctwindow &
				    IEEE80211_P2P_OPPPS_CTWINDOW_MASK);
}

static void iwl_mld_fill_mac_cmd_ap(struct iwl_mld *mld,
				    struct ieee80211_vif *vif,
				    struct iwl_mac_config_cmd *cmd)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);

	lockdep_assert_wiphy(mld->wiphy);

	WARN_ON(vif->type != NL80211_IFTYPE_AP);

	cmd->filter_flags |= cpu_to_le32(MAC_CFG_FILTER_ACCEPT_PROBE_REQ);

	/* in AP mode, pass beacons from other APs (needed for ht protection).
	 * When there're no any associated station, which means that we are not
	 * TXing anyway, don't ask FW to pass beacons to prevent unnecessary
	 * wake-ups.
	 */
	if (mld_vif->num_associated_stas)
		cmd->filter_flags |= cpu_to_le32(MAC_CFG_FILTER_ACCEPT_BEACON);
}

static void iwl_mld_go_iterator(void *_data, u8 *mac, struct ieee80211_vif *vif)
{
	bool *go_active = _data;

	if (ieee80211_vif_type_p2p(vif) == NL80211_IFTYPE_P2P_GO &&
	    iwl_mld_vif_from_mac80211(vif)->ap_ibss_active)
		*go_active = true;
}

static bool iwl_mld_p2p_dev_has_extended_disc(struct iwl_mld *mld)
{
	bool go_active = false;

	/* This flag should be set to true when the P2P Device is
	 * discoverable and there is at least a P2P GO. Setting
	 * this flag will allow the P2P Device to be discoverable on other
	 * channels in addition to its listen channel.
	 * Note that this flag should not be set in other cases as it opens the
	 * Rx filters on all MAC and increases the number of interrupts.
	 */
	ieee80211_iterate_active_interfaces(mld->hw,
					    IEEE80211_IFACE_ITER_RESUME_ALL,
					    iwl_mld_go_iterator, &go_active);

	return go_active;
}

static void iwl_mld_fill_mac_cmd_p2p_dev(struct iwl_mld *mld,
					 struct ieee80211_vif *vif,
					 struct iwl_mac_config_cmd *cmd)
{
	bool ext_disc = iwl_mld_p2p_dev_has_extended_disc(mld);

	lockdep_assert_wiphy(mld->wiphy);

	/* Override the filter flags to accept all management frames. This is
	 * needed to support both P2P device discovery using probe requests and
	 * P2P service discovery using action frames
	 */
	cmd->filter_flags = cpu_to_le32(MAC_CFG_FILTER_ACCEPT_CONTROL_AND_MGMT);

	if (ext_disc)
		cmd->p2p_dev.is_disc_extended = cpu_to_le32(1);
}

static void iwl_mld_fill_mac_cmd_ibss(struct iwl_mld *mld,
				      struct ieee80211_vif *vif,
				      struct iwl_mac_config_cmd *cmd)
{
	lockdep_assert_wiphy(mld->wiphy);

	WARN_ON(vif->type != NL80211_IFTYPE_ADHOC);

	cmd->filter_flags |= cpu_to_le32(MAC_CFG_FILTER_ACCEPT_BEACON |
					 MAC_CFG_FILTER_ACCEPT_PROBE_REQ |
					 MAC_CFG_FILTER_ACCEPT_GRP);
}

static int
iwl_mld_rm_mac_from_fw(struct iwl_mld *mld, struct ieee80211_vif *vif)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mac_config_cmd cmd = {
		.action = cpu_to_le32(FW_CTXT_ACTION_REMOVE),
		.id_and_color = cpu_to_le32(mld_vif->fw_id),
	};

	return iwl_mld_send_mac_cmd(mld, &cmd);
}

int iwl_mld_mac_fw_action(struct iwl_mld *mld, struct ieee80211_vif *vif,
			  u32 action)
{
	struct iwl_mac_config_cmd cmd = {};

	lockdep_assert_wiphy(mld->wiphy);

	if (action == FW_CTXT_ACTION_REMOVE)
		return iwl_mld_rm_mac_from_fw(mld, vif);

	iwl_mld_mac_cmd_fill_common(mld, vif, &cmd, action);

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		iwl_mld_fill_mac_cmd_sta(mld, vif, action, &cmd);
		break;
	case NL80211_IFTYPE_AP:
		iwl_mld_fill_mac_cmd_ap(mld, vif, &cmd);
		break;
	case NL80211_IFTYPE_MONITOR:
		cmd.filter_flags =
			cpu_to_le32(MAC_CFG_FILTER_PROMISC |
				    MAC_CFG_FILTER_ACCEPT_CONTROL_AND_MGMT |
				    MAC_CFG_FILTER_ACCEPT_BEACON |
				    MAC_CFG_FILTER_ACCEPT_PROBE_REQ |
				    MAC_CFG_FILTER_ACCEPT_GRP);
		break;
	case NL80211_IFTYPE_P2P_DEVICE:
		iwl_mld_fill_mac_cmd_p2p_dev(mld, vif, &cmd);
		break;
	case NL80211_IFTYPE_ADHOC:
		iwl_mld_fill_mac_cmd_ibss(mld, vif, &cmd);
		break;
	default:
		WARN(1, "not supported yet\n");
		return -EOPNOTSUPP;
	}

	return iwl_mld_send_mac_cmd(mld, &cmd);
}

IWL_MLD_ALLOC_FN(vif, vif)

/* Constructor function for struct iwl_mld_vif */
static int
iwl_mld_init_vif(struct iwl_mld *mld, struct ieee80211_vif *vif)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	mld_vif->mld = mld;
	mld_vif->roc_activity = ROC_NUM_ACTIVITIES;

	ret = iwl_mld_allocate_vif_fw_id(mld, &mld_vif->fw_id, vif);
	if (ret)
		return ret;

	if (!mld->fw_status.in_hw_restart) {
		wiphy_work_init(&mld_vif->emlsr.unblock_tpt_wk,
				iwl_mld_emlsr_unblock_tpt_wk);
		wiphy_delayed_work_init(&mld_vif->emlsr.check_tpt_wk,
					iwl_mld_emlsr_check_tpt);
		wiphy_delayed_work_init(&mld_vif->emlsr.prevent_done_wk,
					iwl_mld_emlsr_prevent_done_wk);
		wiphy_delayed_work_init(&mld_vif->emlsr.tmp_non_bss_done_wk,
					iwl_mld_emlsr_tmp_non_bss_done_wk);
	}

	return 0;
}

int iwl_mld_add_vif(struct iwl_mld *mld, struct ieee80211_vif *vif)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	ret = iwl_mld_init_vif(mld, vif);
	if (ret)
		return ret;

	ret = iwl_mld_mac_fw_action(mld, vif, FW_CTXT_ACTION_ADD);
	if (ret)
		RCU_INIT_POINTER(mld->fw_id_to_vif[mld_vif->fw_id], NULL);

	return ret;
}

int iwl_mld_rm_vif(struct iwl_mld *mld, struct ieee80211_vif *vif)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	ret = iwl_mld_mac_fw_action(mld, vif, FW_CTXT_ACTION_REMOVE);

	if (WARN_ON(mld_vif->fw_id >= ARRAY_SIZE(mld->fw_id_to_vif)))
		return -EINVAL;

	RCU_INIT_POINTER(mld->fw_id_to_vif[mld_vif->fw_id], NULL);

	iwl_mld_cancel_notifications_of_object(mld, IWL_MLD_OBJECT_TYPE_VIF,
					       mld_vif->fw_id);

	return ret;
}

void iwl_mld_set_vif_associated(struct iwl_mld *mld,
				struct ieee80211_vif *vif)
{
	struct ieee80211_bss_conf *link;
	unsigned int link_id;

	for_each_vif_active_link(vif, link, link_id) {
		if (iwl_mld_link_set_associated(mld, vif, link))
			IWL_ERR(mld, "failed to update link %d\n", link_id);
	}

	iwl_mld_recalc_multicast_filter(mld);
}

static void iwl_mld_get_fw_id_bss_bitmap_iter(void *_data, u8 *mac,
					      struct ieee80211_vif *vif)
{
	u8 *fw_id_bitmap = _data;
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);

	if (ieee80211_vif_type_p2p(vif) != NL80211_IFTYPE_STATION)
		return;

	*fw_id_bitmap |= BIT(mld_vif->fw_id);
}

u8 iwl_mld_get_fw_bss_vifs_ids(struct iwl_mld *mld)
{
	u8 fw_id_bitmap = 0;

	ieee80211_iterate_active_interfaces_mtx(mld->hw,
						IEEE80211_IFACE_SKIP_SDATA_NOT_IN_DRIVER,
						iwl_mld_get_fw_id_bss_bitmap_iter,
						&fw_id_bitmap);

	return fw_id_bitmap;
}

void iwl_mld_handle_probe_resp_data_notif(struct iwl_mld *mld,
					  struct iwl_rx_packet *pkt)
{
	const struct iwl_probe_resp_data_notif *notif = (void *)pkt->data;
	struct iwl_probe_resp_data *old_data, *new_data;
	struct ieee80211_vif *vif;
	struct iwl_mld_link *mld_link;

	IWL_DEBUG_INFO(mld, "Probe response data notif: noa %d, csa %d\n",
		       notif->noa_active, notif->csa_counter);

	if (IWL_FW_CHECK(mld, le32_to_cpu(notif->mac_id) >=
			 ARRAY_SIZE(mld->fw_id_to_vif),
			 "mac id is invalid: %d\n",
			 le32_to_cpu(notif->mac_id)))
		return;

	vif = wiphy_dereference(mld->wiphy,
				mld->fw_id_to_vif[le32_to_cpu(notif->mac_id)]);

	/* the firmware gives us the mac_id (and not the link_id), mac80211
	 * gets a vif and not a link, bottom line, this flow is not MLD ready
	 * yet.
	 */
	if (WARN_ON(!vif) || ieee80211_vif_is_mld(vif))
		return;

	if (notif->csa_counter != IWL_PROBE_RESP_DATA_NO_CSA &&
	    notif->csa_counter >= 1)
		ieee80211_beacon_set_cntdwn(vif, notif->csa_counter);

	if (!vif->p2p)
		return;

	mld_link = &iwl_mld_vif_from_mac80211(vif)->deflink;

	new_data = kzalloc(sizeof(*new_data), GFP_KERNEL);
	if (!new_data)
		return;

	memcpy(&new_data->notif, notif, sizeof(new_data->notif));

	/* noa_attr contains 1 reserved byte, need to substruct it */
	new_data->noa_len = sizeof(struct ieee80211_vendor_ie) +
			    sizeof(new_data->notif.noa_attr) - 1;

	/*
	 * If it's a one time NoA, only one descriptor is needed,
	 * adjust the length according to len_low.
	 */
	if (new_data->notif.noa_attr.len_low ==
	    sizeof(struct ieee80211_p2p_noa_desc) + 2)
		new_data->noa_len -= sizeof(struct ieee80211_p2p_noa_desc);

	old_data = wiphy_dereference(mld->wiphy, mld_link->probe_resp_data);
	rcu_assign_pointer(mld_link->probe_resp_data, new_data);

	if (old_data)
		kfree_rcu(old_data, rcu_head);
}

void iwl_mld_handle_uapsd_misbehaving_ap_notif(struct iwl_mld *mld,
					       struct iwl_rx_packet *pkt)
{
	struct iwl_uapsd_misbehaving_ap_notif *notif = (void *)pkt->data;
	struct ieee80211_vif *vif;

	if (IWL_FW_CHECK(mld, notif->mac_id >= ARRAY_SIZE(mld->fw_id_to_vif),
			 "mac id is invalid: %d\n", notif->mac_id))
		return;

	vif = wiphy_dereference(mld->wiphy, mld->fw_id_to_vif[notif->mac_id]);

	if (WARN_ON(!vif) || ieee80211_vif_is_mld(vif))
		return;

	IWL_WARN(mld, "uapsd misbehaving AP: %pM\n", vif->bss_conf.bssid);
}

void iwl_mld_handle_datapath_monitor_notif(struct iwl_mld *mld,
					   struct iwl_rx_packet *pkt)
{
	struct iwl_datapath_monitor_notif *notif = (void *)pkt->data;
	struct ieee80211_bss_conf *link;
	struct ieee80211_supported_band *sband;
	const struct ieee80211_sta_he_cap *he_cap;
	struct ieee80211_vif *vif;
	struct iwl_mld_vif *mld_vif;

	if (notif->type != cpu_to_le32(IWL_DP_MON_NOTIF_TYPE_EXT_CCA))
		return;

	link = iwl_mld_fw_id_to_link_conf(mld, notif->link_id);
	if (WARN_ON(!link))
		return;

	vif = link->vif;
	if (WARN_ON(!vif) || vif->type != NL80211_IFTYPE_STATION ||
	    !vif->cfg.assoc)
		return;

	if (!link->chanreq.oper.chan ||
	    link->chanreq.oper.chan->band != NL80211_BAND_2GHZ ||
	    link->chanreq.oper.width < NL80211_CHAN_WIDTH_40)
		return;

	mld_vif = iwl_mld_vif_from_mac80211(vif);

	/* this shouldn't happen *again*, ignore it */
	if (mld_vif->cca_40mhz_workaround != CCA_40_MHZ_WA_NONE)
		return;

	mld_vif->cca_40mhz_workaround = CCA_40_MHZ_WA_RECONNECT;

	/*
	 * This capability manipulation isn't really ideal, but it's the
	 * easiest choice - otherwise we'd have to do some major changes
	 * in mac80211 to support this, which isn't worth it. This does
	 * mean that userspace may have outdated information, but that's
	 * actually not an issue at all.
	 */
	sband = mld->wiphy->bands[NL80211_BAND_2GHZ];

	WARN_ON(!sband->ht_cap.ht_supported);
	WARN_ON(!(sband->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40));
	sband->ht_cap.cap &= ~IEEE80211_HT_CAP_SUP_WIDTH_20_40;

	he_cap = ieee80211_get_he_iftype_cap_vif(sband, vif);

	if (he_cap) {
		/* we know that ours is writable */
		struct ieee80211_sta_he_cap *he = (void *)(uintptr_t)he_cap;

		WARN_ON(!he->has_he);
		WARN_ON(!(he->he_cap_elem.phy_cap_info[0] &
			  IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G));
		he->he_cap_elem.phy_cap_info[0] &=
			~IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G;
	}

	ieee80211_disconnect(vif, true);
}

void iwl_mld_reset_cca_40mhz_workaround(struct iwl_mld *mld,
					struct ieee80211_vif *vif)
{
	struct ieee80211_supported_band *sband;
	const struct ieee80211_sta_he_cap *he_cap;
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);

	if (vif->type != NL80211_IFTYPE_STATION)
		return;

	if (mld_vif->cca_40mhz_workaround == CCA_40_MHZ_WA_NONE)
		return;

	/* Now we are just reconnecting with the new capabilities,
	 * but remember to reset the capabilities when we disconnect for real
	 */
	if (mld_vif->cca_40mhz_workaround == CCA_40_MHZ_WA_RECONNECT) {
		mld_vif->cca_40mhz_workaround = CCA_40_MHZ_WA_RESET;
		return;
	}

	/* Now cca_40mhz_workaround == CCA_40_MHZ_WA_RESET */

	sband = mld->wiphy->bands[NL80211_BAND_2GHZ];

	sband->ht_cap.cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;

	he_cap = ieee80211_get_he_iftype_cap_vif(sband, vif);

	if (he_cap) {
		/* we know that ours is writable */
		struct ieee80211_sta_he_cap *he = (void *)(uintptr_t)he_cap;

		he->he_cap_elem.phy_cap_info[0] |=
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G;
	}

	mld_vif->cca_40mhz_workaround = CCA_40_MHZ_WA_NONE;
}

struct ieee80211_vif *iwl_mld_get_bss_vif(struct iwl_mld *mld)
{
	unsigned long fw_id_bitmap = iwl_mld_get_fw_bss_vifs_ids(mld);
	int fw_id;

	if (hweight8(fw_id_bitmap) != 1)
		return NULL;

	fw_id = __ffs(fw_id_bitmap);

	return wiphy_dereference(mld->wiphy,
				 mld->fw_id_to_vif[fw_id]);
}
