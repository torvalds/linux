// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "core.h"
#include "peer.h"
#include "debug.h"

static struct ath11k_peer *ath11k_peer_find_list_by_id(struct ath11k_base *ab,
						       int peer_id)
{
	struct ath11k_peer *peer;

	lockdep_assert_held(&ab->base_lock);

	list_for_each_entry(peer, &ab->peers, list) {
		if (peer->peer_id != peer_id)
			continue;

		return peer;
	}

	return NULL;
}

struct ath11k_peer *ath11k_peer_find(struct ath11k_base *ab, int vdev_id,
				     const u8 *addr)
{
	struct ath11k_peer *peer;

	lockdep_assert_held(&ab->base_lock);

	list_for_each_entry(peer, &ab->peers, list) {
		if (peer->vdev_id != vdev_id)
			continue;
		if (!ether_addr_equal(peer->addr, addr))
			continue;

		return peer;
	}

	return NULL;
}

struct ath11k_peer *ath11k_peer_find_by_addr(struct ath11k_base *ab,
					     const u8 *addr)
{
	struct ath11k_peer *peer;

	lockdep_assert_held(&ab->base_lock);

	if (!ab->rhead_peer_addr)
		return NULL;

	peer = rhashtable_lookup_fast(ab->rhead_peer_addr, addr,
				      ab->rhash_peer_addr_param);

	return peer;
}

struct ath11k_peer *ath11k_peer_find_by_id(struct ath11k_base *ab,
					   int peer_id)
{
	struct ath11k_peer *peer;

	lockdep_assert_held(&ab->base_lock);

	if (!ab->rhead_peer_id)
		return NULL;

	peer = rhashtable_lookup_fast(ab->rhead_peer_id, &peer_id,
				      ab->rhash_peer_id_param);

	return peer;
}

struct ath11k_peer *ath11k_peer_find_by_vdev_id(struct ath11k_base *ab,
						int vdev_id)
{
	struct ath11k_peer *peer;

	spin_lock_bh(&ab->base_lock);

	list_for_each_entry(peer, &ab->peers, list) {
		if (vdev_id == peer->vdev_id) {
			spin_unlock_bh(&ab->base_lock);
			return peer;
		}
	}
	spin_unlock_bh(&ab->base_lock);
	return NULL;
}

void ath11k_peer_unmap_event(struct ath11k_base *ab, u16 peer_id)
{
	struct ath11k_peer *peer;

	spin_lock_bh(&ab->base_lock);

	peer = ath11k_peer_find_list_by_id(ab, peer_id);
	if (!peer) {
		ath11k_warn(ab, "peer-unmap-event: unknown peer id %d\n",
			    peer_id);
		goto exit;
	}

	ath11k_dbg(ab, ATH11K_DBG_DP_HTT, "peer unmap vdev %d peer %pM id %d\n",
		   peer->vdev_id, peer->addr, peer_id);

	list_del(&peer->list);
	kfree(peer);
	wake_up(&ab->peer_mapping_wq);

exit:
	spin_unlock_bh(&ab->base_lock);
}

void ath11k_peer_map_event(struct ath11k_base *ab, u8 vdev_id, u16 peer_id,
			   u8 *mac_addr, u16 ast_hash, u16 hw_peer_id)
{
	struct ath11k_peer *peer;

	spin_lock_bh(&ab->base_lock);
	peer = ath11k_peer_find(ab, vdev_id, mac_addr);
	if (!peer) {
		peer = kzalloc(sizeof(*peer), GFP_ATOMIC);
		if (!peer)
			goto exit;

		peer->vdev_id = vdev_id;
		peer->peer_id = peer_id;
		peer->ast_hash = ast_hash;
		peer->hw_peer_id = hw_peer_id;
		ether_addr_copy(peer->addr, mac_addr);
		list_add(&peer->list, &ab->peers);
		wake_up(&ab->peer_mapping_wq);
	}

	ath11k_dbg(ab, ATH11K_DBG_DP_HTT, "peer map vdev %d peer %pM id %d\n",
		   vdev_id, mac_addr, peer_id);

exit:
	spin_unlock_bh(&ab->base_lock);
}

static int ath11k_wait_for_peer_common(struct ath11k_base *ab, int vdev_id,
				       const u8 *addr, bool expect_mapped)
{
	int ret;

	ret = wait_event_timeout(ab->peer_mapping_wq, ({
				bool mapped;

				spin_lock_bh(&ab->base_lock);
				mapped = !!ath11k_peer_find(ab, vdev_id, addr);
				spin_unlock_bh(&ab->base_lock);

				(mapped == expect_mapped ||
				 test_bit(ATH11K_FLAG_CRASH_FLUSH, &ab->dev_flags));
				}), 3 * HZ);

	if (ret <= 0)
		return -ETIMEDOUT;

	return 0;
}

static inline int ath11k_peer_rhash_insert(struct ath11k_base *ab,
					   struct rhashtable *rtbl,
					   struct rhash_head *rhead,
					   struct rhashtable_params *params,
					   void *key)
{
	struct ath11k_peer *tmp;

	lockdep_assert_held(&ab->tbl_mtx_lock);

	tmp = rhashtable_lookup_get_insert_fast(rtbl, rhead, *params);

	if (!tmp)
		return 0;
	else if (IS_ERR(tmp))
		return PTR_ERR(tmp);
	else
		return -EEXIST;
}

static inline int ath11k_peer_rhash_remove(struct ath11k_base *ab,
					   struct rhashtable *rtbl,
					   struct rhash_head *rhead,
					   struct rhashtable_params *params)
{
	int ret;

	lockdep_assert_held(&ab->tbl_mtx_lock);

	ret = rhashtable_remove_fast(rtbl, rhead, *params);
	if (ret && ret != -ENOENT)
		return ret;

	return 0;
}

static int ath11k_peer_rhash_add(struct ath11k_base *ab, struct ath11k_peer *peer)
{
	int ret;

	lockdep_assert_held(&ab->base_lock);
	lockdep_assert_held(&ab->tbl_mtx_lock);

	if (!ab->rhead_peer_id || !ab->rhead_peer_addr)
		return -EPERM;

	ret = ath11k_peer_rhash_insert(ab, ab->rhead_peer_id, &peer->rhash_id,
				       &ab->rhash_peer_id_param, &peer->peer_id);
	if (ret) {
		ath11k_warn(ab, "failed to add peer %pM with id %d in rhash_id ret %d\n",
			    peer->addr, peer->peer_id, ret);
		return ret;
	}

	ret = ath11k_peer_rhash_insert(ab, ab->rhead_peer_addr, &peer->rhash_addr,
				       &ab->rhash_peer_addr_param, &peer->addr);
	if (ret) {
		ath11k_warn(ab, "failed to add peer %pM with id %d in rhash_addr ret %d\n",
			    peer->addr, peer->peer_id, ret);
		goto err_clean;
	}

	return 0;

err_clean:
	ath11k_peer_rhash_remove(ab, ab->rhead_peer_id, &peer->rhash_id,
				 &ab->rhash_peer_id_param);
	return ret;
}

void ath11k_peer_cleanup(struct ath11k *ar, u32 vdev_id)
{
	struct ath11k_peer *peer, *tmp;
	struct ath11k_base *ab = ar->ab;

	lockdep_assert_held(&ar->conf_mutex);

	mutex_lock(&ab->tbl_mtx_lock);
	spin_lock_bh(&ab->base_lock);
	list_for_each_entry_safe(peer, tmp, &ab->peers, list) {
		if (peer->vdev_id != vdev_id)
			continue;

		ath11k_warn(ab, "removing stale peer %pM from vdev_id %d\n",
			    peer->addr, vdev_id);

		ath11k_peer_rhash_delete(ab, peer);
		list_del(&peer->list);
		kfree(peer);
		ar->num_peers--;
	}

	spin_unlock_bh(&ab->base_lock);
	mutex_unlock(&ab->tbl_mtx_lock);
}

static int ath11k_wait_for_peer_deleted(struct ath11k *ar, int vdev_id, const u8 *addr)
{
	return ath11k_wait_for_peer_common(ar->ab, vdev_id, addr, false);
}

int ath11k_wait_for_peer_delete_done(struct ath11k *ar, u32 vdev_id,
				     const u8 *addr)
{
	int ret;
	unsigned long time_left;

	ret = ath11k_wait_for_peer_deleted(ar, vdev_id, addr);
	if (ret) {
		ath11k_warn(ar->ab, "failed wait for peer deleted");
		return ret;
	}

	time_left = wait_for_completion_timeout(&ar->peer_delete_done,
						3 * HZ);
	if (time_left == 0) {
		ath11k_warn(ar->ab, "Timeout in receiving peer delete response\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int __ath11k_peer_delete(struct ath11k *ar, u32 vdev_id, const u8 *addr)
{
	int ret;
	struct ath11k_peer *peer;
	struct ath11k_base *ab = ar->ab;

	lockdep_assert_held(&ar->conf_mutex);

	mutex_lock(&ab->tbl_mtx_lock);
	spin_lock_bh(&ab->base_lock);

	peer = ath11k_peer_find_by_addr(ab, addr);
	/* Check if the found peer is what we want to remove.
	 * While the sta is transitioning to another band we may
	 * have 2 peer with the same addr assigned to different
	 * vdev_id. Make sure we are deleting the correct peer.
	 */
	if (peer && peer->vdev_id == vdev_id)
		ath11k_peer_rhash_delete(ab, peer);

	/* Fallback to peer list search if the correct peer can't be found.
	 * Skip the deletion of the peer from the rhash since it has already
	 * been deleted in peer add.
	 */
	if (!peer)
		peer = ath11k_peer_find(ab, vdev_id, addr);

	if (!peer) {
		spin_unlock_bh(&ab->base_lock);
		mutex_unlock(&ab->tbl_mtx_lock);

		ath11k_warn(ab,
			    "failed to find peer vdev_id %d addr %pM in delete\n",
			    vdev_id, addr);
		return -EINVAL;
	}

	spin_unlock_bh(&ab->base_lock);
	mutex_unlock(&ab->tbl_mtx_lock);

	reinit_completion(&ar->peer_delete_done);

	ret = ath11k_wmi_send_peer_delete_cmd(ar, addr, vdev_id);
	if (ret) {
		ath11k_warn(ab,
			    "failed to delete peer vdev_id %d addr %pM ret %d\n",
			    vdev_id, addr, ret);
		return ret;
	}

	ret = ath11k_wait_for_peer_delete_done(ar, vdev_id, addr);
	if (ret)
		return ret;

	return 0;
}

int ath11k_peer_delete(struct ath11k *ar, u32 vdev_id, u8 *addr)
{
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	ret = __ath11k_peer_delete(ar, vdev_id, addr);
	if (ret)
		return ret;

	ar->num_peers--;

	return 0;
}

static int ath11k_wait_for_peer_created(struct ath11k *ar, int vdev_id, const u8 *addr)
{
	return ath11k_wait_for_peer_common(ar->ab, vdev_id, addr, true);
}

int ath11k_peer_create(struct ath11k *ar, struct ath11k_vif *arvif,
		       struct ieee80211_sta *sta, struct peer_create_params *param)
{
	struct ath11k_peer *peer;
	struct ath11k_sta *arsta;
	int ret, fbret;

	lockdep_assert_held(&ar->conf_mutex);

	if (ar->num_peers > (ar->max_num_peers - 1)) {
		ath11k_warn(ar->ab,
			    "failed to create peer due to insufficient peer entry resource in firmware\n");
		return -ENOBUFS;
	}

	mutex_lock(&ar->ab->tbl_mtx_lock);
	spin_lock_bh(&ar->ab->base_lock);
	peer = ath11k_peer_find_by_addr(ar->ab, param->peer_addr);
	if (peer) {
		if (peer->vdev_id == param->vdev_id) {
			spin_unlock_bh(&ar->ab->base_lock);
			mutex_unlock(&ar->ab->tbl_mtx_lock);
			return -EINVAL;
		}

		/* Assume sta is transitioning to another band.
		 * Remove here the peer from rhash.
		 */
		ath11k_peer_rhash_delete(ar->ab, peer);
	}
	spin_unlock_bh(&ar->ab->base_lock);
	mutex_unlock(&ar->ab->tbl_mtx_lock);

	ret = ath11k_wmi_send_peer_create_cmd(ar, param);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to send peer create vdev_id %d ret %d\n",
			    param->vdev_id, ret);
		return ret;
	}

	ret = ath11k_wait_for_peer_created(ar, param->vdev_id,
					   param->peer_addr);
	if (ret)
		return ret;

	mutex_lock(&ar->ab->tbl_mtx_lock);
	spin_lock_bh(&ar->ab->base_lock);

	peer = ath11k_peer_find(ar->ab, param->vdev_id, param->peer_addr);
	if (!peer) {
		spin_unlock_bh(&ar->ab->base_lock);
		mutex_unlock(&ar->ab->tbl_mtx_lock);
		ath11k_warn(ar->ab, "failed to find peer %pM on vdev %i after creation\n",
			    param->peer_addr, param->vdev_id);

		ret = -ENOENT;
		goto cleanup;
	}

	ret = ath11k_peer_rhash_add(ar->ab, peer);
	if (ret) {
		spin_unlock_bh(&ar->ab->base_lock);
		mutex_unlock(&ar->ab->tbl_mtx_lock);
		goto cleanup;
	}

	peer->pdev_idx = ar->pdev_idx;
	peer->sta = sta;

	if (arvif->vif->type == NL80211_IFTYPE_STATION) {
		arvif->ast_hash = peer->ast_hash;
		arvif->ast_idx = peer->hw_peer_id;
	}

	peer->sec_type = HAL_ENCRYPT_TYPE_OPEN;
	peer->sec_type_grp = HAL_ENCRYPT_TYPE_OPEN;

	if (sta) {
		arsta = ath11k_sta_to_arsta(sta);
		arsta->tcl_metadata |= FIELD_PREP(HTT_TCL_META_DATA_TYPE, 0) |
				       FIELD_PREP(HTT_TCL_META_DATA_PEER_ID,
						  peer->peer_id);

		/* set HTT extension valid bit to 0 by default */
		arsta->tcl_metadata &= ~HTT_TCL_META_DATA_VALID_HTT;
	}

	ar->num_peers++;

	spin_unlock_bh(&ar->ab->base_lock);
	mutex_unlock(&ar->ab->tbl_mtx_lock);

	return 0;

cleanup:
	fbret = __ath11k_peer_delete(ar, param->vdev_id, param->peer_addr);
	if (fbret)
		ath11k_warn(ar->ab, "failed peer %pM delete vdev_id %d fallback ret %d\n",
			    param->peer_addr, param->vdev_id, fbret);

	return ret;
}

int ath11k_peer_rhash_delete(struct ath11k_base *ab, struct ath11k_peer *peer)
{
	int ret;

	lockdep_assert_held(&ab->base_lock);
	lockdep_assert_held(&ab->tbl_mtx_lock);

	if (!ab->rhead_peer_id || !ab->rhead_peer_addr)
		return -EPERM;

	ret = ath11k_peer_rhash_remove(ab, ab->rhead_peer_addr, &peer->rhash_addr,
				       &ab->rhash_peer_addr_param);
	if (ret) {
		ath11k_warn(ab, "failed to remove peer %pM id %d in rhash_addr ret %d\n",
			    peer->addr, peer->peer_id, ret);
		return ret;
	}

	ret = ath11k_peer_rhash_remove(ab, ab->rhead_peer_id, &peer->rhash_id,
				       &ab->rhash_peer_id_param);
	if (ret) {
		ath11k_warn(ab, "failed to remove peer %pM id %d in rhash_id ret %d\n",
			    peer->addr, peer->peer_id, ret);
		return ret;
	}

	return 0;
}

static int ath11k_peer_rhash_id_tbl_init(struct ath11k_base *ab)
{
	struct rhashtable_params *param;
	struct rhashtable *rhash_id_tbl;
	int ret;
	size_t size;

	lockdep_assert_held(&ab->tbl_mtx_lock);

	if (ab->rhead_peer_id)
		return 0;

	size = sizeof(*ab->rhead_peer_id);
	rhash_id_tbl = kzalloc(size, GFP_KERNEL);
	if (!rhash_id_tbl) {
		ath11k_warn(ab, "failed to init rhash id table due to no mem (size %zu)\n",
			    size);
		return -ENOMEM;
	}

	param = &ab->rhash_peer_id_param;

	param->key_offset = offsetof(struct ath11k_peer, peer_id);
	param->head_offset = offsetof(struct ath11k_peer, rhash_id);
	param->key_len = sizeof_field(struct ath11k_peer, peer_id);
	param->automatic_shrinking = true;
	param->nelem_hint = ab->num_radios * TARGET_NUM_PEERS_PDEV(ab);

	ret = rhashtable_init(rhash_id_tbl, param);
	if (ret) {
		ath11k_warn(ab, "failed to init peer id rhash table %d\n", ret);
		goto err_free;
	}

	spin_lock_bh(&ab->base_lock);

	if (!ab->rhead_peer_id) {
		ab->rhead_peer_id = rhash_id_tbl;
	} else {
		spin_unlock_bh(&ab->base_lock);
		goto cleanup_tbl;
	}

	spin_unlock_bh(&ab->base_lock);

	return 0;

cleanup_tbl:
	rhashtable_destroy(rhash_id_tbl);
err_free:
	kfree(rhash_id_tbl);

	return ret;
}

static int ath11k_peer_rhash_addr_tbl_init(struct ath11k_base *ab)
{
	struct rhashtable_params *param;
	struct rhashtable *rhash_addr_tbl;
	int ret;
	size_t size;

	lockdep_assert_held(&ab->tbl_mtx_lock);

	if (ab->rhead_peer_addr)
		return 0;

	size = sizeof(*ab->rhead_peer_addr);
	rhash_addr_tbl = kzalloc(size, GFP_KERNEL);
	if (!rhash_addr_tbl) {
		ath11k_warn(ab, "failed to init rhash addr table due to no mem (size %zu)\n",
			    size);
		return -ENOMEM;
	}

	param = &ab->rhash_peer_addr_param;

	param->key_offset = offsetof(struct ath11k_peer, addr);
	param->head_offset = offsetof(struct ath11k_peer, rhash_addr);
	param->key_len = sizeof_field(struct ath11k_peer, addr);
	param->automatic_shrinking = true;
	param->nelem_hint = ab->num_radios * TARGET_NUM_PEERS_PDEV(ab);

	ret = rhashtable_init(rhash_addr_tbl, param);
	if (ret) {
		ath11k_warn(ab, "failed to init peer addr rhash table %d\n", ret);
		goto err_free;
	}

	spin_lock_bh(&ab->base_lock);

	if (!ab->rhead_peer_addr) {
		ab->rhead_peer_addr = rhash_addr_tbl;
	} else {
		spin_unlock_bh(&ab->base_lock);
		goto cleanup_tbl;
	}

	spin_unlock_bh(&ab->base_lock);

	return 0;

cleanup_tbl:
	rhashtable_destroy(rhash_addr_tbl);
err_free:
	kfree(rhash_addr_tbl);

	return ret;
}

static inline void ath11k_peer_rhash_id_tbl_destroy(struct ath11k_base *ab)
{
	lockdep_assert_held(&ab->tbl_mtx_lock);

	if (!ab->rhead_peer_id)
		return;

	rhashtable_destroy(ab->rhead_peer_id);
	kfree(ab->rhead_peer_id);
	ab->rhead_peer_id = NULL;
}

static inline void ath11k_peer_rhash_addr_tbl_destroy(struct ath11k_base *ab)
{
	lockdep_assert_held(&ab->tbl_mtx_lock);

	if (!ab->rhead_peer_addr)
		return;

	rhashtable_destroy(ab->rhead_peer_addr);
	kfree(ab->rhead_peer_addr);
	ab->rhead_peer_addr = NULL;
}

int ath11k_peer_rhash_tbl_init(struct ath11k_base *ab)
{
	int ret;

	mutex_lock(&ab->tbl_mtx_lock);

	ret = ath11k_peer_rhash_id_tbl_init(ab);
	if (ret)
		goto out;

	ret = ath11k_peer_rhash_addr_tbl_init(ab);
	if (ret)
		goto cleanup_tbl;

	mutex_unlock(&ab->tbl_mtx_lock);

	return 0;

cleanup_tbl:
	ath11k_peer_rhash_id_tbl_destroy(ab);
out:
	mutex_unlock(&ab->tbl_mtx_lock);
	return ret;
}

void ath11k_peer_rhash_tbl_destroy(struct ath11k_base *ab)
{
	mutex_lock(&ab->tbl_mtx_lock);

	ath11k_peer_rhash_addr_tbl_destroy(ab);
	ath11k_peer_rhash_id_tbl_destroy(ab);

	mutex_unlock(&ab->tbl_mtx_lock);
}
