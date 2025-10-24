/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_PEER_H
#define ATH12K_PEER_H

#include "dp_peer.h"

struct ath12k_ml_peer {
	struct list_head list;
	u8 addr[ETH_ALEN];
	u16 id;
};

void ath12k_peer_cleanup(struct ath12k *ar, u32 vdev_id);
int ath12k_peer_delete(struct ath12k *ar, u32 vdev_id, u8 *addr);
int ath12k_peer_create(struct ath12k *ar, struct ath12k_link_vif *arvif,
		       struct ieee80211_sta *sta,
		       struct ath12k_wmi_peer_create_arg *arg);
int ath12k_wait_for_peer_delete_done(struct ath12k *ar, u32 vdev_id,
				     const u8 *addr);
int ath12k_peer_ml_create(struct ath12k_hw *ah, struct ieee80211_sta *sta);
int ath12k_peer_ml_delete(struct ath12k_hw *ah, struct ieee80211_sta *sta);
int ath12k_peer_mlo_link_peers_delete(struct ath12k_vif *ahvif, struct ath12k_sta *ahsta);
struct ath12k_ml_peer *ath12k_peer_ml_find(struct ath12k_hw *ah,
					   const u8 *addr);
#endif /* _PEER_H_ */
