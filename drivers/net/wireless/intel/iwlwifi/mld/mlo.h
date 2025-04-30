/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */
#ifndef __iwl_mld_mlo_h__
#define __iwl_mld_mlo_h__

#include <linux/ieee80211.h>
#include <linux/types.h>
#include <net/mac80211.h>
#include "iwl-config.h"
#include "iwl-trans.h"
#include "iface.h"
#include "phy.h"

struct iwl_mld;

void iwl_mld_emlsr_prevent_done_wk(struct wiphy *wiphy, struct wiphy_work *wk);
void iwl_mld_emlsr_tmp_non_bss_done_wk(struct wiphy *wiphy,
				       struct wiphy_work *wk);

static inline bool iwl_mld_emlsr_active(struct ieee80211_vif *vif)
{
	/* Set on phy context activation, so should be a good proxy */
	return !!(vif->driver_flags & IEEE80211_VIF_EML_ACTIVE);
}

static inline bool iwl_mld_vif_has_emlsr_cap(struct ieee80211_vif *vif)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);

	/* We only track/permit EMLSR state once authorized */
	if (!mld_vif->authorized)
		return false;

	/* No EMLSR on dual radio devices */
	return ieee80211_vif_type_p2p(vif) == NL80211_IFTYPE_STATION &&
	       ieee80211_vif_is_mld(vif) &&
	       vif->cfg.eml_cap & IEEE80211_EML_CAP_EMLSR_SUPP &&
	       !CSR_HW_RFID_IS_CDB(mld_vif->mld->trans->hw_rf_id);
}

static inline int
iwl_mld_max_active_links(struct iwl_mld *mld, struct ieee80211_vif *vif)
{
	if (vif->type == NL80211_IFTYPE_AP)
		return mld->fw->ucode_capa.num_beacons;

	if (ieee80211_vif_type_p2p(vif) == NL80211_IFTYPE_STATION)
		return IWL_FW_MAX_ACTIVE_LINKS_NUM;

	/* For now, do not accept more links on other interface types */
	return 1;
}

static inline int
iwl_mld_count_active_links(struct iwl_mld *mld, struct ieee80211_vif *vif)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld_link *mld_link;
	int n_active = 0;

	for_each_mld_vif_valid_link(mld_vif, mld_link) {
		if (rcu_access_pointer(mld_link->chan_ctx))
			n_active++;
	}

	return n_active;
}

static inline u8 iwl_mld_get_primary_link(struct ieee80211_vif *vif)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);

	lockdep_assert_wiphy(mld_vif->mld->wiphy);

	if (!ieee80211_vif_is_mld(vif) || WARN_ON(!vif->active_links))
		return 0;

	/* In AP mode, there is no primary link */
	if (vif->type == NL80211_IFTYPE_AP)
		return __ffs(vif->active_links);

	if (iwl_mld_emlsr_active(vif) &&
	    !WARN_ON(!(BIT(mld_vif->emlsr.primary) & vif->active_links)))
		return mld_vif->emlsr.primary;

	return __ffs(vif->active_links);
}

/*
 * For non-MLO/single link, this will return the deflink/single active link,
 * respectively
 */
static inline u8 iwl_mld_get_other_link(struct ieee80211_vif *vif, u8 link_id)
{
	switch (hweight16(vif->active_links)) {
	case 0:
		return 0;
	default:
		WARN_ON(1);
		fallthrough;
	case 1:
		return __ffs(vif->active_links);
	case 2:
		return __ffs(vif->active_links & ~BIT(link_id));
	}
}

s8 iwl_mld_get_emlsr_rssi_thresh(struct iwl_mld *mld,
				 const struct cfg80211_chan_def *chandef,
				 bool low);

/* EMLSR block/unblock and exit */
void iwl_mld_block_emlsr(struct iwl_mld *mld, struct ieee80211_vif *vif,
			 enum iwl_mld_emlsr_blocked reason, u8 link_to_keep);
int iwl_mld_block_emlsr_sync(struct iwl_mld *mld, struct ieee80211_vif *vif,
			     enum iwl_mld_emlsr_blocked reason, u8 link_to_keep);
void iwl_mld_unblock_emlsr(struct iwl_mld *mld, struct ieee80211_vif *vif,
			   enum iwl_mld_emlsr_blocked reason);
void iwl_mld_exit_emlsr(struct iwl_mld *mld, struct ieee80211_vif *vif,
			enum iwl_mld_emlsr_exit exit, u8 link_to_keep);

int iwl_mld_emlsr_check_non_bss_block(struct iwl_mld *mld,
				      int pending_link_changes);

void iwl_mld_handle_emlsr_mode_notif(struct iwl_mld *mld,
				     struct iwl_rx_packet *pkt);
void iwl_mld_handle_emlsr_trans_fail_notif(struct iwl_mld *mld,
					   struct iwl_rx_packet *pkt);

void iwl_mld_emlsr_check_tpt(struct wiphy *wiphy, struct wiphy_work *wk);
void iwl_mld_emlsr_unblock_tpt_wk(struct wiphy *wiphy, struct wiphy_work *wk);

void iwl_mld_select_links(struct iwl_mld *mld);

void iwl_mld_emlsr_check_bt(struct iwl_mld *mld);

void iwl_mld_emlsr_check_chan_load(struct ieee80211_hw *hw,
				   struct iwl_mld_phy *phy,
				   u32 prev_chan_load_not_by_us);

/**
 * iwl_mld_retry_emlsr - Retry entering EMLSR
 * @mld: MLD context
 * @vif: VIF to retry EMLSR on
 *
 * Retry entering EMLSR on the given VIF.
 * Use this if one of the parameters that can prevent EMLSR has changed.
 */
void iwl_mld_retry_emlsr(struct iwl_mld *mld, struct ieee80211_vif *vif);

struct iwl_mld_link_sel_data {
	u8 link_id;
	const struct cfg80211_chan_def *chandef;
	s32 signal;
	u16 grade;
};

#if IS_ENABLED(CONFIG_IWLWIFI_KUNIT_TESTS)
u32 iwl_mld_emlsr_pair_state(struct ieee80211_vif *vif,
			     struct iwl_mld_link_sel_data *a,
			     struct iwl_mld_link_sel_data *b);
#endif

#endif /* __iwl_mld_mlo_h__ */
