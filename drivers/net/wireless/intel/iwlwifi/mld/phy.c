// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */
#include <net/mac80211.h>

#include "phy.h"
#include "hcmd.h"
#include "fw/api/phy-ctxt.h"

int iwl_mld_allocate_fw_phy_id(struct iwl_mld *mld)
{
	int id;
	unsigned long used = mld->used_phy_ids;

	for_each_clear_bit(id, &used, NUM_PHY_CTX) {
		mld->used_phy_ids |= BIT(id);
		return id;
	}

	return -ENOSPC;
}
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_mld_allocate_fw_phy_id);

struct iwl_mld_chanctx_usage_data {
	struct iwl_mld *mld;
	struct ieee80211_chanctx_conf *ctx;
	bool use_def;
};

static bool iwl_mld_chanctx_fils_enabled(struct ieee80211_vif *vif,
					 struct ieee80211_chanctx_conf *ctx)
{
	if (vif->type != NL80211_IFTYPE_AP)
		return false;

	return cfg80211_channel_is_psc(ctx->def.chan) ||
		(ctx->def.chan->band == NL80211_BAND_6GHZ &&
		 ctx->def.width >= NL80211_CHAN_WIDTH_80);
}

static void iwl_mld_chanctx_usage_iter(void *_data, u8 *mac,
				       struct ieee80211_vif *vif)
{
	struct iwl_mld_chanctx_usage_data *data = _data;
	struct ieee80211_bss_conf *link_conf;
	int link_id;

	for_each_vif_active_link(vif, link_conf, link_id) {
		if (rcu_access_pointer(link_conf->chanctx_conf) != data->ctx)
			continue;

		if (iwl_mld_chanctx_fils_enabled(vif, data->ctx))
			data->use_def = true;
	}
}

struct cfg80211_chan_def *
iwl_mld_get_chandef_from_chanctx(struct iwl_mld *mld,
				 struct ieee80211_chanctx_conf *ctx)
{
	struct iwl_mld_chanctx_usage_data data = {
		.mld = mld,
		.ctx = ctx,
	};

	ieee80211_iterate_active_interfaces_mtx(mld->hw,
						IEEE80211_IFACE_ITER_NORMAL,
						iwl_mld_chanctx_usage_iter,
						&data);

	return data.use_def ? &ctx->def : &ctx->min_def;
}

static u8
iwl_mld_nl80211_width_to_fw(enum nl80211_chan_width width)
{
	switch (width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
		return IWL_PHY_CHANNEL_MODE20;
	case NL80211_CHAN_WIDTH_40:
		return IWL_PHY_CHANNEL_MODE40;
	case NL80211_CHAN_WIDTH_80:
		return IWL_PHY_CHANNEL_MODE80;
	case NL80211_CHAN_WIDTH_160:
		return IWL_PHY_CHANNEL_MODE160;
	case NL80211_CHAN_WIDTH_320:
		return IWL_PHY_CHANNEL_MODE320;
	default:
		WARN(1, "Invalid channel width=%u", width);
		return IWL_PHY_CHANNEL_MODE20;
	}
}

/* Maps the driver specific control channel position (relative to the center
 * freq) definitions to the fw values
 */
u8 iwl_mld_get_fw_ctrl_pos(const struct cfg80211_chan_def *chandef)
{
	int offs = chandef->chan->center_freq - chandef->center_freq1;
	int abs_offs = abs(offs);
	u8 ret;

	if (offs == 0) {
		/* The FW is expected to check the control channel position only
		 * when in HT/VHT and the channel width is not 20MHz. Return
		 * this value as the default one.
		 */
		return 0;
	}

	/* this results in a value 0-7, i.e. fitting into 0b0111 */
	ret = (abs_offs - 10) / 20;
	/* But we need the value to be in 0b1011 because 0b0100 is
	 * IWL_PHY_CTRL_POS_ABOVE, so shift bit 2 up to land in
	 * IWL_PHY_CTRL_POS_OFFS_EXT (0b1000)
	 */
	ret = (ret & IWL_PHY_CTRL_POS_OFFS_MSK) |
	      ((ret & BIT(2)) << 1);
	/* and add the above bit */
	ret |= (offs > 0) * IWL_PHY_CTRL_POS_ABOVE;

	return ret;
}

int iwl_mld_phy_fw_action(struct iwl_mld *mld,
			  struct ieee80211_chanctx_conf *ctx, u32 action)
{
	struct iwl_mld_phy *phy = iwl_mld_phy_from_mac80211(ctx);
	struct cfg80211_chan_def *chandef = &phy->chandef;
	struct iwl_phy_context_cmd cmd = {
		.id_and_color = cpu_to_le32(phy->fw_id),
		.action = cpu_to_le32(action),
		.puncture_mask = cpu_to_le16(chandef->punctured),
		/* Channel info */
		.ci.channel = cpu_to_le32(chandef->chan->hw_value),
		.ci.band = iwl_mld_nl80211_band_to_fw(chandef->chan->band),
		.ci.width = iwl_mld_nl80211_width_to_fw(chandef->width),
		.ci.ctrl_pos = iwl_mld_get_fw_ctrl_pos(chandef),
	};
	int ret;

	if (ctx->ap.chan) {
		cmd.sbb_bandwidth =
			iwl_mld_nl80211_width_to_fw(ctx->ap.width);
		cmd.sbb_ctrl_channel_loc = iwl_mld_get_fw_ctrl_pos(&ctx->ap);
	}

	ret = iwl_mld_send_cmd_pdu(mld, PHY_CONTEXT_CMD, &cmd);
	if (ret)
		IWL_ERR(mld, "Failed to send PHY_CONTEXT_CMD ret = %d\n", ret);

	return ret;
}
