/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef ATH12K_PEER_H
#define ATH12K_PEER_H

#include "dp_rx.h"

struct ppdu_user_delayba {
	u16 sw_peer_id;
	u32 info0;
	u16 ru_end;
	u16 ru_start;
	u32 info1;
	u32 rate_flags;
	u32 resp_rate_flags;
};

#define ATH12K_PEER_ML_ID_VALID         BIT(13)

struct ath12k_peer {
	struct list_head list;
	struct ieee80211_sta *sta;
	int vdev_id;
	u8 addr[ETH_ALEN];
	int peer_id;
	u16 ast_hash;
	u8 pdev_idx;
	u16 hw_peer_id;

	/* protected by ab->data_lock */
	struct ieee80211_key_conf *keys[WMI_MAX_KEY_INDEX + 1];
	struct ath12k_dp_rx_tid rx_tid[IEEE80211_NUM_TIDS + 1];

	/* Info used in MMIC verification of
	 * RX fragments
	 */
	struct crypto_shash *tfm_mmic;
	u8 mcast_keyidx;
	u8 ucast_keyidx;
	u16 sec_type;
	u16 sec_type_grp;
	struct ppdu_user_delayba ppdu_stats_delayba;
	bool delayba_flag;
	bool is_authorized;
	bool mlo;
	/* protected by ab->data_lock */
	bool dp_setup_done;

	u16 ml_id;

	/* any other ML info common for all partners can be added
	 * here and would be same for all partner peers.
	 */
	u8 ml_addr[ETH_ALEN];

	/* To ensure only certain work related to dp is done once */
	bool primary_link;

	/* for reference to ath12k_link_sta */
	u8 link_id;
	bool ucast_ra_only;
};

struct ath12k_ml_peer {
	struct list_head list;
	u8 addr[ETH_ALEN];
	u16 id;
};

void ath12k_peer_unmap_event(struct ath12k_base *ab, u16 peer_id);
void ath12k_peer_map_event(struct ath12k_base *ab, u8 vdev_id, u16 peer_id,
			   u8 *mac_addr, u16 ast_hash, u16 hw_peer_id);
struct ath12k_peer *ath12k_peer_find(struct ath12k_base *ab, int vdev_id,
				     const u8 *addr);
struct ath12k_peer *ath12k_peer_find_by_addr(struct ath12k_base *ab,
					     const u8 *addr);
struct ath12k_peer *ath12k_peer_find_by_id(struct ath12k_base *ab, int peer_id);
void ath12k_peer_cleanup(struct ath12k *ar, u32 vdev_id);
int ath12k_peer_delete(struct ath12k *ar, u32 vdev_id, u8 *addr);
int ath12k_peer_create(struct ath12k *ar, struct ath12k_link_vif *arvif,
		       struct ieee80211_sta *sta,
		       struct ath12k_wmi_peer_create_arg *arg);
int ath12k_wait_for_peer_delete_done(struct ath12k *ar, u32 vdev_id,
				     const u8 *addr);
bool ath12k_peer_exist_by_vdev_id(struct ath12k_base *ab, int vdev_id);
struct ath12k_peer *ath12k_peer_find_by_ast(struct ath12k_base *ab, int ast_hash);
int ath12k_peer_ml_create(struct ath12k_hw *ah, struct ieee80211_sta *sta);
int ath12k_peer_ml_delete(struct ath12k_hw *ah, struct ieee80211_sta *sta);
int ath12k_peer_mlo_link_peers_delete(struct ath12k_vif *ahvif, struct ath12k_sta *ahsta);
static inline
struct ath12k_link_sta *ath12k_peer_get_link_sta(struct ath12k_base *ab,
						 struct ath12k_peer *peer)
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

#endif /* _PEER_H_ */
