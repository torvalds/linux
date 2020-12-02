/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */

#ifndef ATH11K_PEER_H
#define ATH11K_PEER_H

struct ath11k_peer {
	struct list_head list;
	struct ieee80211_sta *sta;
	int vdev_id;
	u8 addr[ETH_ALEN];
	int peer_id;
	u16 ast_hash;
	u8 pdev_idx;

	/* protected by ab->data_lock */
	struct ieee80211_key_conf *keys[WMI_MAX_KEY_INDEX + 1];
	struct dp_rx_tid rx_tid[IEEE80211_NUM_TIDS + 1];

	/* Info used in MMIC verification of
	 * RX fragments
	 */
	struct crypto_shash *tfm_mmic;
	u8 mcast_keyidx;
	u8 ucast_keyidx;
	u16 sec_type;
	u16 sec_type_grp;
};

void ath11k_peer_unmap_event(struct ath11k_base *ab, u16 peer_id);
void ath11k_peer_map_event(struct ath11k_base *ab, u8 vdev_id, u16 peer_id,
			   u8 *mac_addr, u16 ast_hash);
struct ath11k_peer *ath11k_peer_find(struct ath11k_base *ab, int vdev_id,
				     const u8 *addr);
struct ath11k_peer *ath11k_peer_find_by_addr(struct ath11k_base *ab,
					     const u8 *addr);
struct ath11k_peer *ath11k_peer_find_by_id(struct ath11k_base *ab, int peer_id);
void ath11k_peer_cleanup(struct ath11k *ar, u32 vdev_id);
int ath11k_peer_delete(struct ath11k *ar, u32 vdev_id, u8 *addr);
int ath11k_peer_create(struct ath11k *ar, struct ath11k_vif *arvif,
		       struct ieee80211_sta *sta, struct peer_create_params *param);
int ath11k_wait_for_peer_delete_done(struct ath11k *ar, u32 vdev_id,
				     const u8 *addr);

#endif /* _PEER_H_ */
