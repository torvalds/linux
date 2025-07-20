/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */
#ifndef __iwl_mld_phy_h__
#define __iwl_mld_phy_h__

#include "mld.h"

/**
 * struct iwl_mld_phy - PHY configuration parameters
 *
 * @fw_id: fw id of the phy.
 * @chandef: the last chandef that mac80211 configured the driver
 *	with. Used to detect a no-op when the chanctx changes.
 * @channel_load_by_us: channel load on this channel caused by
 *	the NIC itself, as indicated by firmware
 * @avg_channel_load_not_by_us: averaged channel load on this channel caused by
 *	others. This value is invalid when in EMLSR (due to FW limitations)
 * @mld: pointer to the MLD context
 */
struct iwl_mld_phy {
	/* Add here fields that need clean up on hw restart */
	struct_group(zeroed_on_hw_restart,
		u8 fw_id;
		struct cfg80211_chan_def chandef;
	);
	/* And here fields that survive a hw restart */
	u32 channel_load_by_us;
	u32 avg_channel_load_not_by_us;
	struct iwl_mld *mld;
};

static inline struct iwl_mld_phy *
iwl_mld_phy_from_mac80211(struct ieee80211_chanctx_conf *channel)
{
	return (void *)channel->drv_priv;
}

/* Cleanup function for struct iwl_mld_phy, will be called in restart */
static inline void
iwl_mld_cleanup_phy(struct iwl_mld *mld, struct iwl_mld_phy *phy)
{
	CLEANUP_STRUCT(phy);
}

int iwl_mld_allocate_fw_phy_id(struct iwl_mld *mld);
int iwl_mld_phy_fw_action(struct iwl_mld *mld,
			  struct ieee80211_chanctx_conf *ctx, u32 action);
struct cfg80211_chan_def *
iwl_mld_get_chandef_from_chanctx(struct iwl_mld *mld,
				 struct ieee80211_chanctx_conf *ctx);
u8 iwl_mld_get_fw_ctrl_pos(const struct cfg80211_chan_def *chandef);

int iwl_mld_send_phy_cfg_cmd(struct iwl_mld *mld);

void iwl_mld_update_phy_chandef(struct iwl_mld *mld,
				struct ieee80211_chanctx_conf *ctx);

#endif /* __iwl_mld_phy_h__ */
