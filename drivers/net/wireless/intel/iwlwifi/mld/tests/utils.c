// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * KUnit tests for channel helper functions
 *
 * Copyright (C) 2024-2025 Intel Corporation
 */
#include <kunit/test.h>
#include <kunit/test-bug.h>

#include "utils.h"

#include <linux/device.h>

#include "fw/api/scan.h"
#include "fw/api/mac-cfg.h"
#include "iwl-trans.h"
#include "mld.h"
#include "iface.h"
#include "link.h"
#include "phy.h"
#include "sta.h"

int iwlmld_kunit_test_init(struct kunit *test)
{
	struct iwl_mld *mld;
	struct iwl_trans *trans;
	const struct iwl_cfg *cfg;
	struct iwl_fw *fw;
	struct ieee80211_hw *hw;

	KUNIT_ALLOC_AND_ASSERT(test, trans);
	KUNIT_ALLOC_AND_ASSERT(test, trans->dev);
	KUNIT_ALLOC_AND_ASSERT(test, cfg);
	KUNIT_ALLOC_AND_ASSERT(test, fw);
	KUNIT_ALLOC_AND_ASSERT(test, hw);
	KUNIT_ALLOC_AND_ASSERT(test, hw->wiphy);

	mutex_init(&hw->wiphy->mtx);

	/* Allocate and initialize the mld structure */
	KUNIT_ALLOC_AND_ASSERT(test, mld);
	iwl_construct_mld(mld, trans, cfg, fw, hw, NULL);

	fw->ucode_capa.num_stations = IWL_STATION_COUNT_MAX;
	fw->ucode_capa.num_links = IWL_FW_MAX_LINK_ID + 1;

	mld->fwrt.trans = trans;
	mld->fwrt.fw = fw;
	mld->fwrt.dev = trans->dev;

	/* TODO: add priv_size to hw allocation and setup hw->priv to enable
	 * testing mac80211 callbacks
	 */

	KUNIT_ALLOC_AND_ASSERT(test, mld->nvm_data);
	KUNIT_ALLOC_AND_ASSERT_SIZE(test, mld->scan.cmd,
				    sizeof(struct iwl_scan_req_umac_v17));
	mld->scan.cmd_size = sizeof(struct iwl_scan_req_umac_v17);

	/* This is not the state at the end of the regular opmode_start,
	 * but it is more common to need it. Explicitly undo this if needed.
	 */
	mld->trans->state = IWL_TRANS_FW_ALIVE;
	mld->fw_status.running = true;

	/* Avoid passing mld struct around */
	test->priv = mld;
	return 0;
}

IWL_MLD_ALLOC_FN(link, bss_conf)

static void iwlmld_kunit_init_link(struct ieee80211_vif *vif,
				   struct ieee80211_bss_conf *link,
				   struct iwl_mld_link *mld_link, int link_id)
{
	struct kunit *test = kunit_get_current_test();
	struct iwl_mld *mld = test->priv;
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	int ret;

	/* setup mac80211 link */
	rcu_assign_pointer(vif->link_conf[link_id], link);
	link->link_id = link_id;
	link->vif = vif;
	link->beacon_int = 100;
	link->dtim_period = 3;
	link->qos = true;

	/* and mld_link */
	ret = iwl_mld_allocate_link_fw_id(mld, &mld_link->fw_id, link);
	KUNIT_ASSERT_EQ(test, ret, 0);
	rcu_assign_pointer(mld_vif->link[link_id], mld_link);
	rcu_assign_pointer(vif->link_conf[link_id], link);
}

IWL_MLD_ALLOC_FN(vif, vif)

/* Helper function to add and initialize a VIF for KUnit tests */
struct ieee80211_vif *iwlmld_kunit_add_vif(bool mlo, enum nl80211_iftype type)
{
	struct kunit *test = kunit_get_current_test();
	struct iwl_mld *mld = test->priv;
	struct ieee80211_vif *vif;
	struct iwl_mld_vif *mld_vif;
	int ret;

	/* TODO: support more types */
	KUNIT_ASSERT_EQ(test, type, NL80211_IFTYPE_STATION);

	KUNIT_ALLOC_AND_ASSERT_SIZE(test, vif,
				    sizeof(*vif) + sizeof(*mld_vif));

	vif->type = type;
	mld_vif = iwl_mld_vif_from_mac80211(vif);
	mld_vif->mld = mld;

	ret = iwl_mld_allocate_vif_fw_id(mld, &mld_vif->fw_id, vif);
	KUNIT_ASSERT_EQ(test, ret, 0);

	/* TODO: revisit (task=EHT) */
	if (mlo)
		return vif;

	/* Initialize the default link */
	iwlmld_kunit_init_link(vif, &vif->bss_conf, &mld_vif->deflink, 0);

	return vif;
}

/* Use only for MLO vif */
struct ieee80211_bss_conf *
iwlmld_kunit_add_link(struct ieee80211_vif *vif, int link_id)
{
	struct kunit *test = kunit_get_current_test();
	struct ieee80211_bss_conf *link;
	struct iwl_mld_link *mld_link;

	KUNIT_ALLOC_AND_ASSERT(test, link);
	KUNIT_ALLOC_AND_ASSERT(test, mld_link);

	iwlmld_kunit_init_link(vif, link, mld_link, link_id);
	vif->valid_links |= BIT(link_id);

	return link;
}

struct ieee80211_chanctx_conf *
iwlmld_kunit_add_chanctx_from_def(struct cfg80211_chan_def *def)
{
	struct kunit *test = kunit_get_current_test();
	struct iwl_mld *mld = test->priv;
	struct ieee80211_chanctx_conf *ctx;
	struct iwl_mld_phy *phy;
	int fw_id;

	KUNIT_ALLOC_AND_ASSERT_SIZE(test, ctx, sizeof(*ctx) + sizeof(*phy));

	/* Setup the chanctx conf */
	ctx->def = *def;
	ctx->min_def = *def;
	ctx->ap = *def;

	/* and the iwl_mld_phy */
	phy = iwl_mld_phy_from_mac80211(ctx);

	fw_id = iwl_mld_allocate_fw_phy_id(mld);
	KUNIT_ASSERT_GE(test, fw_id, 0);

	phy->fw_id = fw_id;
	phy->mld = mld;
	phy->chandef = *def;

	return ctx;
}

void iwlmld_kunit_assign_chanctx_to_link(struct ieee80211_vif *vif,
					 struct ieee80211_bss_conf *link,
					 struct ieee80211_chanctx_conf *ctx)
{
	struct kunit *test = kunit_get_current_test();
	struct iwl_mld *mld = test->priv;
	struct iwl_mld_link *mld_link;

	KUNIT_EXPECT_NULL(test, rcu_access_pointer(link->chanctx_conf));
	rcu_assign_pointer(link->chanctx_conf, ctx);

	lockdep_assert_wiphy(mld->wiphy);

	mld_link = iwl_mld_link_from_mac80211(link);

	KUNIT_EXPECT_NULL(test, rcu_access_pointer(mld_link->chan_ctx));
	KUNIT_EXPECT_FALSE(test, mld_link->active);

	rcu_assign_pointer(mld_link->chan_ctx, ctx);
	mld_link->active = true;

	if (ieee80211_vif_is_mld(vif))
		vif->active_links |= BIT(link->link_id);
}

IWL_MLD_ALLOC_FN(link_sta, link_sta)

static void iwlmld_kunit_add_link_sta(struct ieee80211_sta *sta,
				      struct ieee80211_link_sta *link_sta,
				      struct iwl_mld_link_sta *mld_link_sta,
				      u8 link_id)
{
	struct kunit *test = kunit_get_current_test();
	struct iwl_mld_sta *mld_sta = iwl_mld_sta_from_mac80211(sta);
	struct iwl_mld *mld = test->priv;
	u8 fw_id;
	int ret;

	/* initialize mac80211's link_sta */
	link_sta->link_id = link_id;
	rcu_assign_pointer(sta->link[link_id], link_sta);

	link_sta->sta = sta;

	/* and the iwl_mld_link_sta */
	ret = iwl_mld_allocate_link_sta_fw_id(mld, &fw_id, link_sta);
	KUNIT_ASSERT_EQ(test, ret, 0);
	mld_link_sta->fw_id = fw_id;

	rcu_assign_pointer(mld_sta->link[link_id], mld_link_sta);
}

static struct ieee80211_link_sta *
iwlmld_kunit_alloc_link_sta(struct ieee80211_sta *sta, int link_id)
{
	struct kunit *test = kunit_get_current_test();
	struct ieee80211_link_sta *link_sta;
	struct iwl_mld_link_sta *mld_link_sta;

	/* Only valid for MLO */
	KUNIT_ASSERT_TRUE(test, sta->valid_links);

	KUNIT_ALLOC_AND_ASSERT(test, link_sta);
	KUNIT_ALLOC_AND_ASSERT(test, mld_link_sta);

	iwlmld_kunit_add_link_sta(sta, link_sta, mld_link_sta, link_id);

	sta->valid_links |= BIT(link_id);

	return link_sta;
}

/* Allocate and initialize a STA with the first link_sta */
static struct ieee80211_sta *
iwlmld_kunit_add_sta(struct ieee80211_vif *vif, int link_id)
{
	struct kunit *test = kunit_get_current_test();
	struct ieee80211_sta *sta;
	struct iwl_mld_sta *mld_sta;

	/* Allocate memory for ieee80211_sta with embedded iwl_mld_sta */
	KUNIT_ALLOC_AND_ASSERT_SIZE(test, sta, sizeof(*sta) + sizeof(*mld_sta));

	/* TODO: allocate and initialize the TXQs ? */

	mld_sta = iwl_mld_sta_from_mac80211(sta);
	mld_sta->vif = vif;
	mld_sta->mld = test->priv;

	/* TODO: adjust for internal stations */
	mld_sta->sta_type = STATION_TYPE_PEER;

	if (link_id >= 0) {
		iwlmld_kunit_add_link_sta(sta, &sta->deflink,
					  &mld_sta->deflink, link_id);
		sta->valid_links = BIT(link_id);
	} else {
		iwlmld_kunit_add_link_sta(sta, &sta->deflink,
					  &mld_sta->deflink, 0);
	}
	return sta;
}

/* Move s STA to a state */
static void iwlmld_kunit_move_sta_state(struct ieee80211_vif *vif,
					struct ieee80211_sta *sta,
					enum ieee80211_sta_state state)
{
	struct kunit *test = kunit_get_current_test();
	struct iwl_mld_sta *mld_sta;
	struct iwl_mld_vif *mld_vif;

	/* The sta will be removed automatically at the end of the test */
	KUNIT_ASSERT_NE(test, state, IEEE80211_STA_NOTEXIST);

	mld_sta = iwl_mld_sta_from_mac80211(sta);
	mld_sta->sta_state = state;

	mld_vif = iwl_mld_vif_from_mac80211(mld_sta->vif);
	mld_vif->authorized = state == IEEE80211_STA_AUTHORIZED;

	if (vif->type == NL80211_IFTYPE_STATION && !sta->tdls)
		mld_vif->ap_sta = sta;
}

struct ieee80211_sta *iwlmld_kunit_setup_sta(struct ieee80211_vif *vif,
					     enum ieee80211_sta_state state,
					     int link_id)
{
	struct kunit *test = kunit_get_current_test();
	struct ieee80211_sta *sta;

	/* The sta will be removed automatically at the end of the test */
	KUNIT_ASSERT_NE(test, state, IEEE80211_STA_NOTEXIST);

	/* First - allocate and init the STA */
	sta = iwlmld_kunit_add_sta(vif, link_id);

	/* Now move it all the way to the wanted state */
	for (enum ieee80211_sta_state _state = IEEE80211_STA_NONE;
	     _state <= state; _state++)
		iwlmld_kunit_move_sta_state(vif, sta, state);

	return sta;
}

static void iwlmld_kunit_set_vif_associated(struct ieee80211_vif *vif)
{
	/* TODO: setup chanreq */
	/* TODO setup capabilities */

	vif->cfg.assoc = 1;
}

static struct ieee80211_vif *
iwlmld_kunit_setup_assoc(bool mlo, struct iwl_mld_kunit_link *assoc_link)
{
	struct kunit *test = kunit_get_current_test();
	struct iwl_mld *mld = test->priv;
	struct ieee80211_vif *vif;
	struct ieee80211_bss_conf *link;
	struct ieee80211_chanctx_conf *chan_ctx;

	KUNIT_ASSERT_TRUE(test, mlo || assoc_link->id == 0);

	vif = iwlmld_kunit_add_vif(mlo, NL80211_IFTYPE_STATION);

	if (mlo)
		link = iwlmld_kunit_add_link(vif, assoc_link->id);
	else
		link = &vif->bss_conf;

	chan_ctx = iwlmld_kunit_add_chanctx(assoc_link->band,
					    assoc_link->bandwidth);

	wiphy_lock(mld->wiphy);
	iwlmld_kunit_assign_chanctx_to_link(vif, link, chan_ctx);
	wiphy_unlock(mld->wiphy);

	/* The AP sta will now be pointer to by mld_vif->ap_sta */
	iwlmld_kunit_setup_sta(vif, IEEE80211_STA_AUTHORIZED, assoc_link->id);

	iwlmld_kunit_set_vif_associated(vif);

	return vif;
}

struct ieee80211_vif *
iwlmld_kunit_setup_mlo_assoc(u16 valid_links,
			     struct iwl_mld_kunit_link *assoc_link)
{
	struct kunit *test = kunit_get_current_test();
	struct ieee80211_vif *vif;

	KUNIT_ASSERT_TRUE(test,
			  hweight16(valid_links) == 1 ||
			  hweight16(valid_links) == 2);
	KUNIT_ASSERT_TRUE(test, valid_links & BIT(assoc_link->id));

	vif = iwlmld_kunit_setup_assoc(true, assoc_link);

	/* Add the other link, if applicable */
	if (hweight16(valid_links) > 1) {
		u8 other_link_id = ffs(valid_links & ~BIT(assoc_link->id)) - 1;

		iwlmld_kunit_add_link(vif, other_link_id);
	}

	return vif;
}

struct ieee80211_vif *
iwlmld_kunit_setup_non_mlo_assoc(struct iwl_mld_kunit_link *assoc_link)
{
	return iwlmld_kunit_setup_assoc(false, assoc_link);
}

struct iwl_rx_packet *
_iwl_mld_kunit_create_pkt(const void *notif, size_t notif_sz)
{
	struct kunit *test = kunit_get_current_test();
	struct iwl_rx_packet *pkt;

	KUNIT_ALLOC_AND_ASSERT_SIZE(test, pkt, sizeof(pkt) + notif_sz);

	memcpy(pkt->data, notif, notif_sz);
	pkt->len_n_flags = cpu_to_le32(sizeof(pkt->hdr) + notif_sz);

	return pkt;
}

struct ieee80211_vif *iwlmld_kunit_assoc_emlsr(struct iwl_mld_kunit_link *link1,
					       struct iwl_mld_kunit_link *link2)
{
	struct kunit *test = kunit_get_current_test();
	struct iwl_mld *mld = test->priv;
	struct ieee80211_vif *vif;
	struct ieee80211_bss_conf *link;
	struct ieee80211_chanctx_conf *chan_ctx;
	struct ieee80211_sta *sta;
	struct iwl_mld_vif *mld_vif;
	u16 valid_links = BIT(link1->id) | BIT(link2->id);

	KUNIT_ASSERT_TRUE(test, hweight16(valid_links) == 2);

	vif = iwlmld_kunit_setup_mlo_assoc(valid_links, link1);
	mld_vif = iwl_mld_vif_from_mac80211(vif);

	/* Activate second link */
	wiphy_lock(mld->wiphy);

	link = wiphy_dereference(mld->wiphy, vif->link_conf[link2->id]);
	KUNIT_EXPECT_NOT_NULL(test, link);

	chan_ctx = iwlmld_kunit_add_chanctx(link2->band, link2->bandwidth);
	iwlmld_kunit_assign_chanctx_to_link(vif, link, chan_ctx);

	wiphy_unlock(mld->wiphy);

	/* And other link sta */
	sta = mld_vif->ap_sta;
	KUNIT_EXPECT_NOT_NULL(test, sta);

	iwlmld_kunit_alloc_link_sta(sta, link2->id);

	return vif;
}

struct element *iwlmld_kunit_gen_element(u8 id, const void *data, size_t len)
{
	struct kunit *test = kunit_get_current_test();
	struct element *elem;

	KUNIT_ALLOC_AND_ASSERT_SIZE(test, elem, sizeof(*elem) + len);

	elem->id = id;
	elem->datalen = len;
	memcpy(elem->data, data, len);

	return elem;
}

struct iwl_mld_phy *iwlmld_kunit_get_phy_of_link(struct ieee80211_vif *vif,
						 u8 link_id)
{
	struct kunit *test = kunit_get_current_test();
	struct iwl_mld *mld = test->priv;
	struct ieee80211_chanctx_conf *chanctx;
	struct ieee80211_bss_conf *link =
		wiphy_dereference(mld->wiphy, vif->link_conf[link_id]);

	KUNIT_EXPECT_NOT_NULL(test, link);

	chanctx = wiphy_dereference(mld->wiphy, link->chanctx_conf);
	KUNIT_EXPECT_NOT_NULL(test, chanctx);

	return iwl_mld_phy_from_mac80211(chanctx);
}
