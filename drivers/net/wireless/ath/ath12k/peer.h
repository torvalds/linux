/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

	/* protected by ab->data_lock */
	bool dp_setup_done;
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
int ath12k_peer_create(struct ath12k *ar, struct ath12k_vif *arvif,
		       struct ieee80211_sta *sta,
		       struct ath12k_wmi_peer_create_arg *arg);
int ath12k_wait_for_peer_delete_done(struct ath12k *ar, u32 vdev_id,
				     const u8 *addr);
bool ath12k_peer_exist_by_vdev_id(struct ath12k_base *ab, int vdev_id);
struct ath12k_peer *ath12k_peer_find_by_ast(struct ath12k_base *ab, int ast_hash);

#endif /* _PEER_H_ */
