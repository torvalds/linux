// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "core.h"
#include "dp_peer.h"
#include "debug.h"

struct ath12k_dp_link_peer *
ath12k_dp_link_peer_find_by_vdev_and_addr(struct ath12k_dp *dp,
					  int vdev_id, const u8 *addr)
{
	struct ath12k_dp_link_peer *peer;

	lockdep_assert_held(&dp->dp_lock);

	list_for_each_entry(peer, &dp->peers, list) {
		if (peer->vdev_id != vdev_id)
			continue;
		if (!ether_addr_equal(peer->addr, addr))
			continue;

		return peer;
	}

	return NULL;
}

struct ath12k_dp_link_peer *
ath12k_dp_link_peer_find_by_pdev_and_addr(struct ath12k_dp *dp, u8 pdev_idx,
					  const u8 *addr)
{
	struct ath12k_dp_link_peer *peer;

	lockdep_assert_held(&dp->dp_lock);

	list_for_each_entry(peer, &dp->peers, list) {
		if (peer->pdev_idx != pdev_idx)
			continue;
		if (!ether_addr_equal(peer->addr, addr))
			continue;

		return peer;
	}

	return NULL;
}

struct ath12k_dp_link_peer *
ath12k_dp_link_peer_find_by_addr(struct ath12k_dp *dp, const u8 *addr)
{
	lockdep_assert_held(&dp->dp_lock);

	return rhashtable_lookup_fast(dp->rhead_peer_addr, addr,
				      dp->rhash_peer_addr_param);
}
EXPORT_SYMBOL(ath12k_dp_link_peer_find_by_addr);

static struct ath12k_dp_link_peer *
ath12k_dp_link_peer_find_by_ml_id(struct ath12k_dp *dp, int ml_peer_id)
{
	struct ath12k_dp_link_peer *peer;

	lockdep_assert_held(&dp->dp_lock);

	list_for_each_entry(peer, &dp->peers, list)
		if (ml_peer_id == peer->ml_id)
			return peer;

	return NULL;
}

struct ath12k_dp_link_peer *
ath12k_dp_link_peer_find_by_id(struct ath12k_dp *dp, int peer_id)
{
	struct ath12k_dp_link_peer *peer;

	lockdep_assert_held(&dp->dp_lock);

	if (peer_id == HAL_INVALID_PEERID)
		return NULL;

	if (peer_id & ATH12K_PEER_ML_ID_VALID)
		return ath12k_dp_link_peer_find_by_ml_id(dp, peer_id);

	list_for_each_entry(peer, &dp->peers, list)
		if (peer_id == peer->peer_id)
			return peer;

	return NULL;
}

bool ath12k_dp_link_peer_exist_by_vdev_id(struct ath12k_dp *dp, int vdev_id)
{
	struct ath12k_dp_link_peer *peer;

	spin_lock_bh(&dp->dp_lock);

	list_for_each_entry(peer, &dp->peers, list) {
		if (vdev_id == peer->vdev_id) {
			spin_unlock_bh(&dp->dp_lock);
			return true;
		}
	}
	spin_unlock_bh(&dp->dp_lock);
	return false;
}

struct ath12k_dp_link_peer *
ath12k_dp_link_peer_find_by_ast(struct ath12k_dp *dp, int ast_hash)
{
	struct ath12k_dp_link_peer *peer;

	lockdep_assert_held(&dp->dp_lock);

	list_for_each_entry(peer, &dp->peers, list)
		if (ast_hash == peer->ast_hash)
			return peer;

	return NULL;
}

void ath12k_dp_link_peer_unmap_event(struct ath12k_base *ab, u16 peer_id)
{
	struct ath12k_dp_link_peer *peer;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);

	spin_lock_bh(&dp->dp_lock);

	peer = ath12k_dp_link_peer_find_by_id(dp, peer_id);
	if (!peer) {
		ath12k_warn(ab, "peer-unmap-event: unknown peer id %d\n",
			    peer_id);
		goto exit;
	}

	ath12k_dbg(ab, ATH12K_DBG_DP_HTT, "htt peer unmap vdev %d peer %pM id %d\n",
		   peer->vdev_id, peer->addr, peer_id);

	ath12k_dp_link_peer_rhash_delete(dp, peer);
	list_del(&peer->list);
	kfree(peer);
	wake_up(&ab->peer_mapping_wq);

exit:
	spin_unlock_bh(&dp->dp_lock);
}

void ath12k_dp_link_peer_map_event(struct ath12k_base *ab, u8 vdev_id, u16 peer_id,
				   u8 *mac_addr, u16 ast_hash, u16 hw_peer_id)
{
	struct ath12k_dp_link_peer *peer;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	int ret;

	spin_lock_bh(&dp->dp_lock);
	peer = ath12k_dp_link_peer_find_by_vdev_and_addr(dp, vdev_id, mac_addr);
	if (!peer) {
		peer = kzalloc(sizeof(*peer), GFP_ATOMIC);
		if (!peer)
			goto exit;

		peer->vdev_id = vdev_id;
		peer->peer_id = peer_id;
		peer->ast_hash = ast_hash;
		peer->hw_peer_id = hw_peer_id;
		ether_addr_copy(peer->addr, mac_addr);
		ret = ath12k_dp_link_peer_rhash_add(dp, peer);
		if (!ret)
			list_add(&peer->list, &dp->peers);
		else
			kfree(peer);
		wake_up(&ab->peer_mapping_wq);
	}

	ath12k_dbg(ab, ATH12K_DBG_DP_HTT, "htt peer map vdev %d peer %pM id %d\n",
		   vdev_id, mac_addr, peer_id);

exit:
	spin_unlock_bh(&dp->dp_lock);
}

struct ath12k_link_sta *ath12k_dp_link_peer_to_link_sta(struct ath12k_base *ab,
							struct ath12k_dp_link_peer *peer)
{
	struct ath12k_sta *ahsta;
	struct ath12k_link_sta *arsta;

	if (!peer->sta)
		return NULL;

	ahsta = ath12k_sta_to_ahsta(peer->sta);
	if (peer->ml_id & ATH12K_PEER_ML_ID_VALID) {
		if (!(ahsta->links_map & BIT(peer->link_id))) {
			ath12k_warn(ab, "peer %pM id %d link_id %d can't found in STA link_map 0x%x\n",
				    peer->addr, peer->peer_id, peer->link_id,
				    ahsta->links_map);
			return NULL;
		}
		arsta = rcu_dereference(ahsta->link[peer->link_id]);
		if (!arsta)
			return NULL;
	} else {
		arsta =  &ahsta->deflink;
	}
	return arsta;
}

static int ath12k_dp_link_peer_rhash_addr_tbl_init(struct ath12k_dp *dp)
{
	struct ath12k_base *ab = dp->ab;
	struct rhashtable_params *param;
	struct rhashtable *rhash_addr_tbl;
	int ret;

	lockdep_assert_held(&dp->link_peer_rhash_tbl_lock);

	rhash_addr_tbl = kzalloc(sizeof(*dp->rhead_peer_addr), GFP_KERNEL);
	if (!rhash_addr_tbl)
		return -ENOMEM;

	param = &dp->rhash_peer_addr_param;

	param->key_offset = offsetof(struct ath12k_dp_link_peer, addr);
	param->head_offset = offsetof(struct ath12k_dp_link_peer, rhash_addr);
	param->key_len = sizeof_field(struct ath12k_dp_link_peer, addr);
	param->automatic_shrinking = true;
	param->nelem_hint = ab->num_radios * ath12k_core_get_max_peers_per_radio(ab);

	ret = rhashtable_init(rhash_addr_tbl, param);
	if (ret) {
		ath12k_warn(ab, "failed to init peer addr rhash table %d\n", ret);
		goto err_free;
	}

	dp->rhead_peer_addr = rhash_addr_tbl;

	return 0;

err_free:
	kfree(rhash_addr_tbl);

	return ret;
}

int ath12k_dp_link_peer_rhash_tbl_init(struct ath12k_dp *dp)
{
	int ret;

	mutex_lock(&dp->link_peer_rhash_tbl_lock);
	ret = ath12k_dp_link_peer_rhash_addr_tbl_init(dp);
	mutex_unlock(&dp->link_peer_rhash_tbl_lock);

	return ret;
}

void ath12k_dp_link_peer_rhash_tbl_destroy(struct ath12k_dp *dp)
{
	mutex_lock(&dp->link_peer_rhash_tbl_lock);
	rhashtable_destroy(dp->rhead_peer_addr);
	kfree(dp->rhead_peer_addr);
	dp->rhead_peer_addr = NULL;
	mutex_unlock(&dp->link_peer_rhash_tbl_lock);
}

static int ath12k_dp_link_peer_rhash_insert(struct ath12k_dp *dp,
					    struct ath12k_dp_link_peer *peer)
{
	struct ath12k_dp_link_peer *tmp;

	lockdep_assert_held(&dp->dp_lock);

	tmp = rhashtable_lookup_get_insert_fast(dp->rhead_peer_addr, &peer->rhash_addr,
						dp->rhash_peer_addr_param);
	if (!tmp)
		return 0;
	else if (IS_ERR(tmp))
		return PTR_ERR(tmp);
	else
		return -EEXIST;
}

static int ath12k_dp_link_peer_rhash_remove(struct ath12k_dp *dp,
					    struct ath12k_dp_link_peer *peer)
{
	int ret;

	lockdep_assert_held(&dp->dp_lock);

	ret = rhashtable_remove_fast(dp->rhead_peer_addr, &peer->rhash_addr,
				     dp->rhash_peer_addr_param);
	if (ret && ret != -ENOENT)
		return ret;

	return 0;
}

int ath12k_dp_link_peer_rhash_add(struct ath12k_dp *dp,
				  struct ath12k_dp_link_peer *peer)
{
	int ret;

	lockdep_assert_held(&dp->dp_lock);

	ret = ath12k_dp_link_peer_rhash_insert(dp, peer);
	if (ret)
		ath12k_warn(dp, "failed to add peer %pM with id %d in rhash_addr ret %d\n",
			    peer->addr, peer->peer_id, ret);

	return ret;
}

void ath12k_dp_link_peer_rhash_delete(struct ath12k_dp *dp,
				      struct ath12k_dp_link_peer *peer)
{
	/* No failure handling and hence return type is void */
	int ret;

	lockdep_assert_held(&dp->dp_lock);

	ret = ath12k_dp_link_peer_rhash_remove(dp, peer);
	if (ret)
		ath12k_warn(dp, "failed to remove peer %pM with id %d in rhash_addr ret %d\n",
			    peer->addr, peer->peer_id, ret);
}
