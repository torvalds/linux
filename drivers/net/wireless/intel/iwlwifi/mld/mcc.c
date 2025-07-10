// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */

#include <net/cfg80211.h>
#include <net/mac80211.h>

#include <fw/dbg.h>
#include <iwl-nvm-parse.h>

#include "mld.h"
#include "hcmd.h"
#include "mcc.h"

/* It is the caller's responsibility to free the pointer returned here */
static struct iwl_mcc_update_resp_v8 *
iwl_mld_parse_mcc_update_resp_v8(const struct iwl_rx_packet *pkt)
{
	const struct iwl_mcc_update_resp_v8 *mcc_resp_v8 = (const void *)pkt->data;
	int n_channels = __le32_to_cpu(mcc_resp_v8->n_channels);
	struct iwl_mcc_update_resp_v8 *resp_cp;
	int notif_len = struct_size(resp_cp, channels, n_channels);

	if (iwl_rx_packet_payload_len(pkt) != notif_len)
		return ERR_PTR(-EINVAL);

	resp_cp = kmemdup(mcc_resp_v8, notif_len, GFP_KERNEL);
	if (!resp_cp)
		return ERR_PTR(-ENOMEM);

	return resp_cp;
}

/* It is the caller's responsibility to free the pointer returned here */
static struct iwl_mcc_update_resp_v8 *
iwl_mld_parse_mcc_update_resp_v5_v6(const struct iwl_rx_packet *pkt)
{
	const struct iwl_mcc_update_resp_v4 *mcc_resp_v4 = (const void *)pkt->data;
	struct iwl_mcc_update_resp_v8 *resp_cp;
	int n_channels = __le32_to_cpu(mcc_resp_v4->n_channels);
	int resp_len;

	if (iwl_rx_packet_payload_len(pkt) !=
	    struct_size(mcc_resp_v4, channels, n_channels))
		return ERR_PTR(-EINVAL);

	resp_len = struct_size(resp_cp, channels, n_channels);
	resp_cp = kzalloc(resp_len, GFP_KERNEL);
	if (!resp_cp)
		return ERR_PTR(-ENOMEM);

	resp_cp->status = mcc_resp_v4->status;
	resp_cp->mcc = mcc_resp_v4->mcc;
	resp_cp->cap = cpu_to_le32(le16_to_cpu(mcc_resp_v4->cap));
	resp_cp->source_id = mcc_resp_v4->source_id;
	resp_cp->geo_info = mcc_resp_v4->geo_info;
	resp_cp->n_channels = mcc_resp_v4->n_channels;
	memcpy(resp_cp->channels, mcc_resp_v4->channels,
	       n_channels * sizeof(__le32));

	return resp_cp;
}

/* It is the caller's responsibility to free the pointer returned here */
static struct iwl_mcc_update_resp_v8 *
iwl_mld_update_mcc(struct iwl_mld *mld, const char *alpha2,
		   enum iwl_mcc_source src_id)
{
	int resp_ver = iwl_fw_lookup_notif_ver(mld->fw, LONG_GROUP,
					       MCC_UPDATE_CMD, 0);
	struct iwl_mcc_update_cmd mcc_update_cmd = {
		.mcc = cpu_to_le16(alpha2[0] << 8 | alpha2[1]),
		.source_id = (u8)src_id,
	};
	struct iwl_mcc_update_resp_v8 *resp_cp;
	struct iwl_rx_packet *pkt;
	struct iwl_host_cmd cmd = {
		.id = MCC_UPDATE_CMD,
		.flags = CMD_WANT_SKB,
		.data = { &mcc_update_cmd },
		.len[0] = sizeof(mcc_update_cmd),
	};
	int ret;
	u16 mcc;

	IWL_DEBUG_LAR(mld, "send MCC update to FW with '%c%c' src = %d\n",
		      alpha2[0], alpha2[1], src_id);

	ret = iwl_mld_send_cmd(mld, &cmd);
	if (ret)
		return ERR_PTR(ret);

	pkt = cmd.resp_pkt;

	/* For Wifi-7 radios, we get version 8
	 * For Wifi-6E radios, we get version 6
	 * For Wifi-6 radios, we get version 5, but 5, 6, and 4 are compatible.
	 */
	switch (resp_ver) {
	case 5:
	case 6:
		resp_cp = iwl_mld_parse_mcc_update_resp_v5_v6(pkt);
		break;
	case 8:
		resp_cp = iwl_mld_parse_mcc_update_resp_v8(pkt);
		break;
	default:
		IWL_FW_CHECK_FAILED(mld, "Unknown MCC_UPDATE_CMD version %d\n", resp_ver);
		resp_cp = ERR_PTR(-EINVAL);
	}

	if (IS_ERR(resp_cp))
		goto exit;

	mcc = le16_to_cpu(resp_cp->mcc);

	IWL_FW_CHECK(mld, !mcc, "mcc can't be 0: %d\n", mcc);

	IWL_DEBUG_LAR(mld,
		      "MCC response status: 0x%x. new MCC: 0x%x ('%c%c')\n",
		      le32_to_cpu(resp_cp->status), mcc, mcc >> 8, mcc & 0xff);

exit:
	iwl_free_resp(&cmd);
	return resp_cp;
}

/* It is the caller's responsibility to free the pointer returned here */
struct ieee80211_regdomain *
iwl_mld_get_regdomain(struct iwl_mld *mld,
		      const char *alpha2,
		      enum iwl_mcc_source src_id,
		      bool *changed)
{
	struct ieee80211_regdomain *regd = NULL;
	struct iwl_mcc_update_resp_v8 *resp;
	u8 resp_ver = iwl_fw_lookup_notif_ver(mld->fw, IWL_ALWAYS_LONG_GROUP,
					      MCC_UPDATE_CMD, 0);

	IWL_DEBUG_LAR(mld, "Getting regdomain data for %s from FW\n", alpha2);

	lockdep_assert_wiphy(mld->wiphy);

	resp = iwl_mld_update_mcc(mld, alpha2, src_id);
	if (IS_ERR(resp)) {
		IWL_DEBUG_LAR(mld, "Could not get update from FW %ld\n",
			      PTR_ERR(resp));
		resp = NULL;
		goto out;
	}

	if (changed) {
		u32 status = le32_to_cpu(resp->status);

		*changed = (status == MCC_RESP_NEW_CHAN_PROFILE ||
			    status == MCC_RESP_ILLEGAL);
	}
	IWL_DEBUG_LAR(mld, "MCC update response version: %d\n", resp_ver);

	regd = iwl_parse_nvm_mcc_info(mld->trans,
				      __le32_to_cpu(resp->n_channels),
				      resp->channels,
				      __le16_to_cpu(resp->mcc),
				      __le16_to_cpu(resp->geo_info),
				      le32_to_cpu(resp->cap), resp_ver);

	if (IS_ERR(regd)) {
		IWL_DEBUG_LAR(mld, "Could not get parse update from FW %ld\n",
			      PTR_ERR(regd));
		goto out;
	}

	IWL_DEBUG_LAR(mld, "setting alpha2 from FW to %s (0x%x, 0x%x) src=%d\n",
		      regd->alpha2, regd->alpha2[0],
		      regd->alpha2[1], resp->source_id);

	mld->mcc_src = resp->source_id;

	/* FM is the earliest supported and later always do puncturing */
	if (CSR_HW_RFID_TYPE(mld->trans->info.hw_rf_id) == IWL_CFG_RF_TYPE_FM) {
		if (!iwl_puncturing_is_allowed_in_bios(mld->bios_enable_puncturing,
						       le16_to_cpu(resp->mcc)))
			ieee80211_hw_set(mld->hw, DISALLOW_PUNCTURING);
		else
			__clear_bit(IEEE80211_HW_DISALLOW_PUNCTURING,
				    mld->hw->flags);
	}

out:
	kfree(resp);
	return regd;
}

/* It is the caller's responsibility to free the pointer returned here */
static struct ieee80211_regdomain *
iwl_mld_get_current_regdomain(struct iwl_mld *mld,
			      bool *changed)
{
	return iwl_mld_get_regdomain(mld, "ZZ",
				     MCC_SOURCE_GET_CURRENT, changed);
}

void iwl_mld_update_changed_regdomain(struct iwl_mld *mld)
{
	struct ieee80211_regdomain *regd;
	bool changed;

	regd = iwl_mld_get_current_regdomain(mld, &changed);

	if (IS_ERR_OR_NULL(regd))
		return;

	if (changed)
		regulatory_set_wiphy_regd(mld->wiphy, regd);
	kfree(regd);
}

static int iwl_mld_apply_last_mcc(struct iwl_mld *mld,
				  const char *alpha2)
{
	struct ieee80211_regdomain *regd;
	u32 used_src;
	bool changed;
	int ret;

	/* save the last source in case we overwrite it below */
	used_src = mld->mcc_src;

	/* Notify the firmware we support wifi location updates */
	regd = iwl_mld_get_current_regdomain(mld, NULL);
	if (!IS_ERR_OR_NULL(regd))
		kfree(regd);

	/* Now set our last stored MCC and source */
	regd = iwl_mld_get_regdomain(mld, alpha2, used_src,
				     &changed);
	if (IS_ERR_OR_NULL(regd))
		return -EIO;

	/* update cfg80211 if the regdomain was changed */
	if (changed)
		ret = regulatory_set_wiphy_regd_sync(mld->wiphy, regd);
	else
		ret = 0;

	kfree(regd);
	return ret;
}

int iwl_mld_init_mcc(struct iwl_mld *mld)
{
	const struct ieee80211_regdomain *r;
	struct ieee80211_regdomain *regd;
	char mcc[3];
	int retval;

	/* try to replay the last set MCC to FW */
	r = wiphy_dereference(mld->wiphy, mld->wiphy->regd);

	if (r)
		return iwl_mld_apply_last_mcc(mld, r->alpha2);

	regd = iwl_mld_get_current_regdomain(mld, NULL);
	if (IS_ERR_OR_NULL(regd))
		return -EIO;

	if (!iwl_bios_get_mcc(&mld->fwrt, mcc)) {
		kfree(regd);
		regd = iwl_mld_get_regdomain(mld, mcc, MCC_SOURCE_BIOS, NULL);
		if (IS_ERR_OR_NULL(regd))
			return -EIO;
	}

	retval = regulatory_set_wiphy_regd_sync(mld->wiphy, regd);

	kfree(regd);
	return retval;
}

static void iwl_mld_find_assoc_vif_iterator(void *data, u8 *mac,
					    struct ieee80211_vif *vif)
{
	bool *assoc = data;

	if (vif->type == NL80211_IFTYPE_STATION &&
	    vif->cfg.assoc)
		*assoc = true;
}

static bool iwl_mld_is_a_vif_assoc(struct iwl_mld *mld)
{
	bool assoc = false;

	ieee80211_iterate_active_interfaces_atomic(mld->hw,
						   IEEE80211_IFACE_ITER_NORMAL,
						   iwl_mld_find_assoc_vif_iterator,
						   &assoc);
	return assoc;
}

void iwl_mld_handle_update_mcc(struct iwl_mld *mld, struct iwl_rx_packet *pkt)
{
	struct iwl_mcc_chub_notif *notif = (void *)pkt->data;
	enum iwl_mcc_source src;
	char mcc[3];
	struct ieee80211_regdomain *regd;
	bool changed;

	lockdep_assert_wiphy(mld->wiphy);

	if (iwl_mld_is_a_vif_assoc(mld) &&
	    notif->source_id == MCC_SOURCE_WIFI) {
		IWL_DEBUG_LAR(mld, "Ignore mcc update while associated\n");
		return;
	}

	mcc[0] = le16_to_cpu(notif->mcc) >> 8;
	mcc[1] = le16_to_cpu(notif->mcc) & 0xff;
	mcc[2] = '\0';
	src = notif->source_id;

	IWL_DEBUG_LAR(mld,
		      "RX: received chub update mcc cmd (mcc '%s' src %d)\n",
		      mcc, src);
	regd = iwl_mld_get_regdomain(mld, mcc, src, &changed);
	if (IS_ERR_OR_NULL(regd))
		return;

	if (changed)
		regulatory_set_wiphy_regd(mld->hw->wiphy, regd);
	kfree(regd);
}
