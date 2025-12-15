// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "core.h"
#include "dp_peer.h"
#include "debug.h"
#include "debugfs.h"

void ath12k_dp_link_peer_free(struct ath12k_dp_link_peer *peer)
{
	list_del(&peer->list);

	kfree(peer->peer_stats.rx_stats);
	kfree(peer);
}

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

static struct ath12k_dp_link_peer *
ath12k_dp_link_peer_search_by_id(struct ath12k_dp *dp, int peer_id)
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

	peer = ath12k_dp_link_peer_search_by_id(dp, peer_id);
	if (!peer) {
		ath12k_warn(ab, "peer-unmap-event: unknown peer id %d\n",
			    peer_id);
		goto exit;
	}

	ath12k_dbg(ab, ATH12K_DBG_DP_HTT, "htt peer unmap vdev %d peer %pM id %d\n",
		   peer->vdev_id, peer->addr, peer_id);

	ath12k_dp_link_peer_free(peer);
	wake_up(&ab->peer_mapping_wq);

exit:
	spin_unlock_bh(&dp->dp_lock);
}

void ath12k_dp_link_peer_map_event(struct ath12k_base *ab, u8 vdev_id, u16 peer_id,
				   u8 *mac_addr, u16 ast_hash, u16 hw_peer_id)
{
	struct ath12k_dp_link_peer *peer;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k *ar;

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

		rcu_read_lock();
		ar = ath12k_mac_get_ar_by_vdev_id(ab, vdev_id);
		if (ar && ath12k_debugfs_is_extd_rx_stats_enabled(ar) &&
		    !peer->peer_stats.rx_stats) {
			peer->peer_stats.rx_stats =
				kzalloc(sizeof(*peer->peer_stats.rx_stats), GFP_ATOMIC);
		}
		rcu_read_unlock();

		list_add(&peer->list, &dp->peers);
		wake_up(&ab->peer_mapping_wq);
		ewma_avg_rssi_init(&peer->avg_rssi);
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

	RCU_LOCKDEP_WARN(!rcu_read_lock_held(),
			 "ath12k_dp_link_peer to ath12k_link_sta called without rcu lock");

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

struct ath12k_dp_peer *ath12k_dp_peer_find_by_addr(struct ath12k_dp_hw *dp_hw, u8 *addr)
{
	struct ath12k_dp_peer *peer;

	lockdep_assert_held(&dp_hw->peer_lock);

	list_for_each_entry(peer, &dp_hw->dp_peers_list, list) {
		if (ether_addr_equal(peer->addr, addr))
			return peer;
	}

	return NULL;
}
EXPORT_SYMBOL(ath12k_dp_peer_find_by_addr);

struct ath12k_dp_peer *ath12k_dp_peer_find_by_addr_and_sta(struct ath12k_dp_hw *dp_hw,
							   u8 *addr,
							   struct ieee80211_sta *sta)
{
	struct ath12k_dp_peer *dp_peer;

	lockdep_assert_held(&dp_hw->peer_lock);

	list_for_each_entry(dp_peer, &dp_hw->dp_peers_list, list) {
		if (ether_addr_equal(dp_peer->addr, addr) && (dp_peer->sta == sta))
			return dp_peer;
	}

	return NULL;
}

static struct ath12k_dp_peer *ath12k_dp_peer_create_find(struct ath12k_dp_hw *dp_hw,
							 u8 *addr,
							 struct ieee80211_sta *sta,
							 bool mlo_peer)
{
	struct ath12k_dp_peer *dp_peer;

	lockdep_assert_held(&dp_hw->peer_lock);

	list_for_each_entry(dp_peer, &dp_hw->dp_peers_list, list) {
		if (ether_addr_equal(dp_peer->addr, addr)) {
			if (!sta || mlo_peer || dp_peer->is_mlo ||
			    dp_peer->sta == sta)
				return dp_peer;
		}
	}

	return NULL;
}

/*
 * Index of ath12k_dp_peer for MLO client is same as peer id of ath12k_dp_peer,
 * while for ath12k_dp_link_peer(mlo and non-mlo) and ath12k_dp_peer for
 * Non-MLO client it is derived as ((DEVICE_ID << 10) | (10 bits of peer id)).
 *
 * This is done because ml_peer_id and peer_id_table are at hw granularity,
 * while link_peer_id is at device granularity, hence in order to avoid
 * conflict this approach is followed.
 */
#define ATH12K_DP_PEER_TABLE_DEVICE_ID_SHIFT        10

u16 ath12k_dp_peer_get_peerid_index(struct ath12k_dp *dp, u16 peer_id)
{
	return (peer_id & ATH12K_PEER_ML_ID_VALID) ? peer_id :
		((dp->device_id << ATH12K_DP_PEER_TABLE_DEVICE_ID_SHIFT) | peer_id);
}

struct ath12k_dp_peer *ath12k_dp_peer_find_by_peerid(struct ath12k_pdev_dp *dp_pdev,
						     u16 peer_id)
{
	u16 index;
	struct ath12k_dp *dp = dp_pdev->dp;

	RCU_LOCKDEP_WARN(!rcu_read_lock_held(),
			 "ath12k dp peer find by peerid index called without rcu lock");

	if (!peer_id || peer_id >= ATH12K_DP_PEER_ID_INVALID)
		return NULL;

	index = ath12k_dp_peer_get_peerid_index(dp, peer_id);

	return rcu_dereference(dp_pdev->dp_hw->dp_peers[index]);
}
EXPORT_SYMBOL(ath12k_dp_peer_find_by_peerid);

struct ath12k_dp_link_peer *
ath12k_dp_link_peer_find_by_peerid(struct ath12k_pdev_dp *dp_pdev, u16 peer_id)
{
	struct ath12k_dp_peer *dp_peer = NULL;
	u8 link_id;

	RCU_LOCKDEP_WARN(!rcu_read_lock_held(),
			 "ath12k dp link peer find by peerid index called without rcu lock");

	if (dp_pdev->hw_link_id >= ATH12K_GROUP_MAX_RADIO)
		return NULL;

	dp_peer = ath12k_dp_peer_find_by_peerid(dp_pdev, peer_id);
	if (!dp_peer)
		return NULL;

	link_id = dp_peer->hw_links[dp_pdev->hw_link_id];

	return rcu_dereference(dp_peer->link_peers[link_id]);
}
EXPORT_SYMBOL(ath12k_dp_link_peer_find_by_peerid);

int ath12k_dp_peer_create(struct ath12k_dp_hw *dp_hw, u8 *addr,
			  struct ath12k_dp_peer_create_params *params)
{
	struct ath12k_dp_peer *dp_peer;

	spin_lock_bh(&dp_hw->peer_lock);
	dp_peer = ath12k_dp_peer_create_find(dp_hw, addr, params->sta, params->is_mlo);
	if (dp_peer) {
		spin_unlock_bh(&dp_hw->peer_lock);
		return -EEXIST;
	}
	spin_unlock_bh(&dp_hw->peer_lock);

	dp_peer = kzalloc(sizeof(*dp_peer), GFP_ATOMIC);
	if (!dp_peer)
		return -ENOMEM;

	ether_addr_copy(dp_peer->addr, addr);
	dp_peer->sta = params->sta;
	dp_peer->is_mlo = params->is_mlo;

	/*
	 * For MLO client, the host assigns the ML peer ID, so set peer_id in dp_peer
	 * For non-MLO client, host gets link peer ID from firmware and will be
	 * assigned at the time of link peer creation
	 */
	dp_peer->peer_id = params->is_mlo ? params->peer_id : ATH12K_DP_PEER_ID_INVALID;
	dp_peer->ucast_ra_only = params->ucast_ra_only;

	dp_peer->sec_type = HAL_ENCRYPT_TYPE_OPEN;
	dp_peer->sec_type_grp = HAL_ENCRYPT_TYPE_OPEN;
	dp_peer->ucast_ra_only = params->ucast_ra_only;

	spin_lock_bh(&dp_hw->peer_lock);

	list_add(&dp_peer->list, &dp_hw->dp_peers_list);

	/*
	 * For MLO client, the peer_id for ath12k_dp_peer is allocated by host
	 * and that peer_id is known at this point, and hence this ath12k_dp_peer
	 * can be added to the RCU table using the peer_id.
	 * For non-MLO client, this addition to RCU table shall be done at the
	 * time of assignment of ath12k_dp_link_peer to ath12k_dp_peer.
	 */
	if (dp_peer->is_mlo)
		rcu_assign_pointer(dp_hw->dp_peers[dp_peer->peer_id], dp_peer);

	spin_unlock_bh(&dp_hw->peer_lock);

	return 0;
}

void ath12k_dp_peer_delete(struct ath12k_dp_hw *dp_hw, u8 *addr,
			   struct ieee80211_sta *sta)
{
	struct ath12k_dp_peer *dp_peer;

	spin_lock_bh(&dp_hw->peer_lock);

	dp_peer = ath12k_dp_peer_find_by_addr_and_sta(dp_hw, addr, sta);
	if (!dp_peer) {
		spin_unlock_bh(&dp_hw->peer_lock);
		return;
	}

	if (dp_peer->is_mlo)
		rcu_assign_pointer(dp_hw->dp_peers[dp_peer->peer_id], NULL);

	list_del(&dp_peer->list);

	spin_unlock_bh(&dp_hw->peer_lock);

	synchronize_rcu();
	kfree(dp_peer);
}

int ath12k_dp_link_peer_assign(struct ath12k_dp *dp, struct ath12k_dp_hw *dp_hw,
			       u8 vdev_id, struct ieee80211_sta *sta, u8 *addr,
			       u8 link_id, u32 hw_link_id)
{
	struct ath12k_dp_peer *dp_peer;
	struct ath12k_dp_link_peer *peer, *temp_peer;
	u16 peerid_index;
	int ret = -EINVAL;
	u8 *dp_peer_mac = !sta ? addr : sta->addr;

	spin_lock_bh(&dp->dp_lock);

	peer = ath12k_dp_link_peer_find_by_vdev_and_addr(dp, vdev_id, addr);
	if (!peer) {
		ath12k_warn(dp, "failed to find dp_link_peer with mac %pM on vdev %u\n",
			    addr, vdev_id);
		ret = -ENOENT;
		goto err_peer;
	}

	spin_lock_bh(&dp_hw->peer_lock);

	dp_peer = ath12k_dp_peer_find_by_addr_and_sta(dp_hw, dp_peer_mac, sta);
	if (!dp_peer) {
		ath12k_warn(dp, "failed to find dp_peer with mac %pM\n", dp_peer_mac);
		ret = -ENOENT;
		goto err_dp_peer;
	}

	/*
	 * Set peer_id in dp_peer for non-mlo client, peer_id for mlo client is
	 * set during dp_peer create
	 */
	if (!dp_peer->is_mlo)
		dp_peer->peer_id = peer->peer_id;

	peer->dp_peer = dp_peer;
	peer->hw_link_id = hw_link_id;

	dp_peer->hw_links[peer->hw_link_id] = link_id;

	peerid_index = ath12k_dp_peer_get_peerid_index(dp, peer->peer_id);

	rcu_assign_pointer(dp_peer->link_peers[peer->link_id], peer);

	rcu_assign_pointer(dp_hw->dp_peers[peerid_index], dp_peer);

	spin_unlock_bh(&dp_hw->peer_lock);

	/*
	 * In case of Split PHY and roaming scenario, pdev idx
	 * might differ but both the pdev will share same rhash
	 * table. In that case update the rhash table if link_peer is
	 * already present
	 */
	temp_peer = ath12k_dp_link_peer_find_by_addr(dp, addr);
	if (temp_peer && temp_peer->hw_link_id != hw_link_id)
		ath12k_dp_link_peer_rhash_delete(dp, temp_peer);

	ret = ath12k_dp_link_peer_rhash_add(dp, peer);
	if (ret) {
		/*
		 * If new entry addition failed, add back old entry
		 * If old entry addition also fails, then nothing
		 * can be done, simply proceed
		 */
		if (temp_peer)
			ath12k_dp_link_peer_rhash_add(dp, temp_peer);
	}

	spin_unlock_bh(&dp->dp_lock);

	return ret;

err_dp_peer:
	spin_unlock_bh(&dp_hw->peer_lock);

err_peer:
	spin_unlock_bh(&dp->dp_lock);

	return ret;
}

void ath12k_dp_link_peer_unassign(struct ath12k_dp *dp, struct ath12k_dp_hw *dp_hw,
				  u8 vdev_id, u8 *addr, u32 hw_link_id)
{
	struct ath12k_dp_peer *dp_peer;
	struct ath12k_dp_link_peer *peer, *temp_peer;
	u16 peerid_index;

	spin_lock_bh(&dp->dp_lock);

	peer = ath12k_dp_link_peer_find_by_vdev_and_addr(dp, vdev_id, addr);
	if (!peer || !peer->dp_peer) {
		spin_unlock_bh(&dp->dp_lock);
		return;
	}

	spin_lock_bh(&dp_hw->peer_lock);

	dp_peer = peer->dp_peer;
	dp_peer->hw_links[peer->hw_link_id] = 0;

	peerid_index = ath12k_dp_peer_get_peerid_index(dp, peer->peer_id);

	rcu_assign_pointer(dp_peer->link_peers[peer->link_id], NULL);

	rcu_assign_pointer(dp_hw->dp_peers[peerid_index], NULL);

	spin_unlock_bh(&dp_hw->peer_lock);

	/* To handle roaming and split phy scenario */
	temp_peer = ath12k_dp_link_peer_find_by_addr(dp, addr);
	if (temp_peer && temp_peer->hw_link_id == hw_link_id)
		ath12k_dp_link_peer_rhash_delete(dp, peer);

	spin_unlock_bh(&dp->dp_lock);

	synchronize_rcu();
}

void
ath12k_dp_link_peer_get_sta_rate_info_stats(struct ath12k_dp *dp, const u8 *addr,
					    struct ath12k_dp_link_peer_rate_info *info)
{
	struct ath12k_dp_link_peer *link_peer;

	guard(spinlock_bh)(&dp->dp_lock);

	link_peer = ath12k_dp_link_peer_find_by_addr(dp, addr);
	if (!link_peer)
		return;

	info->rx_duration = link_peer->rx_duration;
	info->tx_duration = link_peer->tx_duration;
	info->txrate.legacy = link_peer->txrate.legacy;
	info->txrate.mcs = link_peer->txrate.mcs;
	info->txrate.nss = link_peer->txrate.nss;
	info->txrate.bw = link_peer->txrate.bw;
	info->txrate.he_gi = link_peer->txrate.he_gi;
	info->txrate.he_dcm = link_peer->txrate.he_dcm;
	info->txrate.he_ru_alloc = link_peer->txrate.he_ru_alloc;
	info->txrate.flags = link_peer->txrate.flags;
	info->rssi_comb = link_peer->rssi_comb;
	info->signal_avg = ewma_avg_rssi_read(&link_peer->avg_rssi);
}

void ath12k_dp_link_peer_reset_rx_stats(struct ath12k_dp *dp, const u8 *addr)
{
	struct ath12k_rx_peer_stats *rx_stats;
	struct ath12k_dp_link_peer *link_peer;

	guard(spinlock_bh)(&dp->dp_lock);

	link_peer = ath12k_dp_link_peer_find_by_addr(dp, addr);
	if (!link_peer || !link_peer->peer_stats.rx_stats)
		return;

	rx_stats = link_peer->peer_stats.rx_stats;
	if (rx_stats)
		memset(rx_stats, 0, sizeof(*rx_stats));
}
