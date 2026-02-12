// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "core.h"
#include "peer.h"
#include "debug.h"
#include "debugfs.h"

static int ath12k_wait_for_dp_link_peer_common(struct ath12k_base *ab, int vdev_id,
					       const u8 *addr, bool expect_mapped)
{
	int ret;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);

	ret = wait_event_timeout(ab->peer_mapping_wq, ({
				bool mapped;

				spin_lock_bh(&dp->dp_lock);
				mapped = !!ath12k_dp_link_peer_find_by_vdev_and_addr(dp,
										vdev_id,
										addr);
				spin_unlock_bh(&dp->dp_lock);

				(mapped == expect_mapped ||
				 test_bit(ATH12K_FLAG_CRASH_FLUSH, &ab->dev_flags));
				}), 3 * HZ);

	if (ret <= 0)
		return -ETIMEDOUT;

	return 0;
}

void ath12k_peer_cleanup(struct ath12k *ar, u32 vdev_id)
{
	struct ath12k_dp_link_peer *peer, *tmp;
	struct ath12k_base *ab = ar->ab;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	spin_lock_bh(&dp->dp_lock);
	list_for_each_entry_safe(peer, tmp, &dp->peers, list) {
		if (peer->vdev_id != vdev_id)
			continue;

		ath12k_warn(ab, "removing stale peer %pM from vdev_id %d\n",
			    peer->addr, vdev_id);

		ath12k_dp_link_peer_free(peer);
		ar->num_peers--;
	}

	spin_unlock_bh(&dp->dp_lock);
}

static int ath12k_wait_for_peer_deleted(struct ath12k *ar, int vdev_id, const u8 *addr)
{
	return ath12k_wait_for_dp_link_peer_common(ar->ab, vdev_id, addr, false);
}

int ath12k_wait_for_peer_delete_done(struct ath12k *ar, u32 vdev_id,
				     const u8 *addr)
{
	int ret;
	unsigned long time_left;

	ret = ath12k_wait_for_peer_deleted(ar, vdev_id, addr);
	if (ret) {
		ath12k_warn(ar->ab, "failed wait for peer deleted");
		return ret;
	}

	time_left = wait_for_completion_timeout(&ar->peer_delete_done,
						3 * HZ);
	if (time_left == 0) {
		ath12k_warn(ar->ab, "Timeout in receiving peer delete response\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int ath12k_peer_delete_send(struct ath12k *ar, u32 vdev_id, const u8 *addr)
{
	struct ath12k_base *ab = ar->ab;
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	reinit_completion(&ar->peer_delete_done);

	ret = ath12k_wmi_send_peer_delete_cmd(ar, addr, vdev_id);
	if (ret) {
		ath12k_warn(ab,
			    "failed to delete peer vdev_id %d addr %pM ret %d\n",
			    vdev_id, addr, ret);
		return ret;
	}

	return 0;
}

int ath12k_peer_delete(struct ath12k *ar, u32 vdev_id, u8 *addr)
{
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	ath12k_dp_link_peer_unassign(ath12k_ab_to_dp(ar->ab),
				     &(ath12k_ar_to_ah(ar)->dp_hw), vdev_id,
				     addr, ar->hw_link_id);

	ret = ath12k_peer_delete_send(ar, vdev_id, addr);
	if (ret)
		return ret;

	ret = ath12k_wait_for_peer_delete_done(ar, vdev_id, addr);
	if (ret)
		return ret;

	ar->num_peers--;

	return 0;
}

static int ath12k_wait_for_peer_created(struct ath12k *ar, int vdev_id, const u8 *addr)
{
	return ath12k_wait_for_dp_link_peer_common(ar->ab, vdev_id, addr, true);
}

int ath12k_peer_create(struct ath12k *ar, struct ath12k_link_vif *arvif,
		       struct ieee80211_sta *sta,
		       struct ath12k_wmi_peer_create_arg *arg)
{
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(arvif->ahvif);
	struct ath12k_vif *ahvif = arvif->ahvif;
	struct ath12k_dp_link_vif *dp_link_vif;
	struct ath12k_link_sta *arsta;
	u8 link_id = arvif->link_id;
	struct ath12k_dp_link_peer *peer;
	struct ath12k_sta *ahsta;
	u16 ml_peer_id;
	int ret;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ar->ab);

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	dp_link_vif = ath12k_dp_vif_to_dp_link_vif(&ahvif->dp_vif, link_id);

	if (ar->num_peers > (ar->max_num_peers - 1)) {
		ath12k_warn(ar->ab,
			    "failed to create peer due to insufficient peer entry resource in firmware\n");
		return -ENOBUFS;
	}

	spin_lock_bh(&dp->dp_lock);
	peer = ath12k_dp_link_peer_find_by_pdev_and_addr(dp, ar->pdev_idx,
							 arg->peer_addr);
	if (peer) {
		spin_unlock_bh(&dp->dp_lock);
		return -EINVAL;
	}
	spin_unlock_bh(&dp->dp_lock);

	ret = ath12k_wmi_send_peer_create_cmd(ar, arg);
	if (ret) {
		ath12k_warn(ar->ab,
			    "failed to send peer create vdev_id %d ret %d\n",
			    arg->vdev_id, ret);
		return ret;
	}

	ret = ath12k_wait_for_peer_created(ar, arg->vdev_id,
					   arg->peer_addr);
	if (ret)
		return ret;

	spin_lock_bh(&dp->dp_lock);

	peer = ath12k_dp_link_peer_find_by_vdev_and_addr(dp, arg->vdev_id,
							 arg->peer_addr);
	if (!peer) {
		spin_unlock_bh(&dp->dp_lock);
		ath12k_warn(ar->ab, "failed to find peer %pM on vdev %i after creation\n",
			    arg->peer_addr, arg->vdev_id);

		reinit_completion(&ar->peer_delete_done);

		ret = ath12k_wmi_send_peer_delete_cmd(ar, arg->peer_addr,
						      arg->vdev_id);
		if (ret) {
			ath12k_warn(ar->ab, "failed to delete peer vdev_id %d addr %pM\n",
				    arg->vdev_id, arg->peer_addr);
			return ret;
		}

		ret = ath12k_wait_for_peer_delete_done(ar, arg->vdev_id,
						       arg->peer_addr);
		if (ret)
			return ret;

		return -ENOENT;
	}

	peer->pdev_idx = ar->pdev_idx;
	peer->sta = sta;

	if (vif->type == NL80211_IFTYPE_STATION) {
		dp_link_vif->ast_hash = peer->ast_hash;
		dp_link_vif->ast_idx = peer->hw_peer_id;
	}

	if (sta) {
		ahsta = ath12k_sta_to_ahsta(sta);
		arsta = wiphy_dereference(ath12k_ar_to_hw(ar)->wiphy,
					  ahsta->link[link_id]);

		peer->link_id = arsta->link_id;

		/* Fill ML info into created peer */
		if (sta->mlo) {
			ml_peer_id = ahsta->ml_peer_id;
			peer->ml_id = ml_peer_id | ATH12K_PEER_ML_ID_VALID;
			ether_addr_copy(peer->ml_addr, sta->addr);

			/* the assoc link is considered primary for now */
			peer->primary_link = arsta->is_assoc_link;
			peer->mlo = true;
		} else {
			peer->ml_id = ATH12K_MLO_PEER_ID_INVALID;
			peer->primary_link = true;
			peer->mlo = false;
		}
	}

	ar->num_peers++;

	spin_unlock_bh(&dp->dp_lock);

	if (arvif->link_id < IEEE80211_MLD_MAX_NUM_LINKS) {
		ret = ath12k_dp_link_peer_assign(ath12k_ab_to_dp(ar->ab),
						 &(ath12k_ar_to_ah(ar)->dp_hw),
						 arvif->vdev_id, sta,
						 (u8 *)arg->peer_addr, link_id,
						 ar->hw_link_id);
	}

	return ret;
}

u16 ath12k_peer_ml_alloc(struct ath12k_hw *ah)
{
	u16 ml_peer_id;

	lockdep_assert_wiphy(ah->hw->wiphy);

	for (ml_peer_id = 0; ml_peer_id < ATH12K_MAX_MLO_PEERS; ml_peer_id++) {
		if (test_bit(ml_peer_id, ah->free_ml_peer_id_map))
			continue;

		set_bit(ml_peer_id, ah->free_ml_peer_id_map);
		break;
	}

	if (ml_peer_id == ATH12K_MAX_MLO_PEERS)
		ml_peer_id = ATH12K_MLO_PEER_ID_INVALID;

	return ml_peer_id;
}

int ath12k_peer_mlo_link_peers_delete(struct ath12k_vif *ahvif, struct ath12k_sta *ahsta)
{
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(ahsta);
	struct ath12k_hw *ah = ahvif->ah;
	struct ath12k_link_vif *arvif;
	struct ath12k_link_sta *arsta;
	unsigned long links;
	struct ath12k *ar;
	int ret, err_ret = 0;
	u8 link_id;

	lockdep_assert_wiphy(ah->hw->wiphy);

	if (!sta->mlo)
		return -EINVAL;

	/* FW expects delete of all link peers at once before waiting for reception
	 * of peer unmap or delete responses
	 */
	links = ahsta->links_map;
	for_each_set_bit(link_id, &links, IEEE80211_MLD_MAX_NUM_LINKS) {
		arvif = wiphy_dereference(ah->hw->wiphy, ahvif->link[link_id]);
		arsta = wiphy_dereference(ah->hw->wiphy, ahsta->link[link_id]);
		if (!arvif || !arsta)
			continue;

		ar = arvif->ar;
		if (!ar)
			continue;

		ath12k_dp_peer_cleanup(ar, arvif->vdev_id, arsta->addr);

		ath12k_dp_link_peer_unassign(ath12k_ab_to_dp(ar->ab),
					     &(ath12k_ar_to_ah(ar)->dp_hw),
					     arvif->vdev_id, arsta->addr,
					     ar->hw_link_id);

		ret = ath12k_peer_delete_send(ar, arvif->vdev_id, arsta->addr);
		if (ret) {
			ath12k_warn(ar->ab,
				    "failed to delete peer vdev_id %d addr %pM ret %d\n",
				    arvif->vdev_id, arsta->addr, ret);
			err_ret = ret;
			continue;
		}
	}

	/* Ensure all link peers are deleted and unmapped */
	links = ahsta->links_map;
	for_each_set_bit(link_id, &links, IEEE80211_MLD_MAX_NUM_LINKS) {
		arvif = wiphy_dereference(ah->hw->wiphy, ahvif->link[link_id]);
		arsta = wiphy_dereference(ah->hw->wiphy, ahsta->link[link_id]);
		if (!arvif || !arsta)
			continue;

		ar = arvif->ar;
		if (!ar)
			continue;

		ret = ath12k_wait_for_peer_delete_done(ar, arvif->vdev_id, arsta->addr);
		if (ret) {
			err_ret = ret;
			continue;
		}
		ar->num_peers--;
	}

	return err_ret;
}

static int ath12k_link_sta_rhash_insert(struct ath12k_base *ab,
					struct ath12k_link_sta *arsta)
{
	struct ath12k_link_sta *tmp;

	lockdep_assert_held(&ab->base_lock);

	tmp = rhashtable_lookup_get_insert_fast(ab->rhead_sta_addr, &arsta->rhash_addr,
						ab->rhash_sta_addr_param);
	if (!tmp)
		return 0;
	else if (IS_ERR(tmp))
		return PTR_ERR(tmp);
	else
		return -EEXIST;
}

static int ath12k_link_sta_rhash_remove(struct ath12k_base *ab,
					struct ath12k_link_sta *arsta)
{
	int ret;

	lockdep_assert_held(&ab->base_lock);

	ret = rhashtable_remove_fast(ab->rhead_sta_addr, &arsta->rhash_addr,
				     ab->rhash_sta_addr_param);
	if (ret && ret != -ENOENT)
		return ret;

	return 0;
}

int ath12k_link_sta_rhash_add(struct ath12k_base *ab,
			      struct ath12k_link_sta *arsta)
{
	int ret;

	lockdep_assert_held(&ab->base_lock);

	ret = ath12k_link_sta_rhash_insert(ab, arsta);
	if (ret)
		ath12k_warn(ab, "failed to add arsta %pM in rhash_addr ret %d\n",
			    arsta->addr, ret);

	return ret;
}

void ath12k_link_sta_rhash_delete(struct ath12k_base *ab,
				  struct ath12k_link_sta *arsta)
{
	/*
	 * Return type of this function is void since there is nothing to be
	 * done in failure case
	 */
	int ret;

	lockdep_assert_held(&ab->base_lock);

	ret = ath12k_link_sta_rhash_remove(ab, arsta);
	if (ret)
		ath12k_warn(ab,
			    "failed to remove arsta %pM in rhash_addr ret %d\n",
			    arsta->addr, ret);
}

int ath12k_link_sta_rhash_tbl_init(struct ath12k_base *ab)
{
	struct rhashtable_params *param;
	struct rhashtable *rhash_addr_tbl;
	int ret;

	rhash_addr_tbl = kzalloc(sizeof(*ab->rhead_sta_addr), GFP_KERNEL);
	if (!rhash_addr_tbl)
		return -ENOMEM;

	param = &ab->rhash_sta_addr_param;

	param->key_offset = offsetof(struct ath12k_link_sta, addr);
	param->head_offset = offsetof(struct ath12k_link_sta, rhash_addr);
	param->key_len = sizeof_field(struct ath12k_link_sta, addr);
	param->automatic_shrinking = true;
	param->nelem_hint = ab->num_radios * ath12k_core_get_max_peers_per_radio(ab);

	ret = rhashtable_init(rhash_addr_tbl, param);
	if (ret) {
		ath12k_warn(ab, "failed to init peer addr rhash table %d\n",
			    ret);
		goto err_free;
	}

	ab->rhead_sta_addr = rhash_addr_tbl;

	return 0;

err_free:
	kfree(rhash_addr_tbl);

	return ret;
}

void ath12k_link_sta_rhash_tbl_destroy(struct ath12k_base *ab)
{
	rhashtable_destroy(ab->rhead_sta_addr);
	kfree(ab->rhead_sta_addr);
	ab->rhead_sta_addr = NULL;
}

struct ath12k_link_sta *ath12k_link_sta_find_by_addr(struct ath12k_base *ab,
						     const u8 *addr)
{
	lockdep_assert_held(&ab->base_lock);

	return rhashtable_lookup_fast(ab->rhead_sta_addr, addr,
				      ab->rhash_sta_addr_param);
}
