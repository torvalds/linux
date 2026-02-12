/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_DP_PEER_H
#define ATH12K_DP_PEER_H

#include "dp_rx.h"

#define ATH12K_DP_PEER_ID_INVALID              0x3FFF

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

struct ath12k_rx_peer_rate_stats {
	u64 ht_mcs_count[HAL_RX_MAX_MCS_HT + 1];
	u64 vht_mcs_count[HAL_RX_MAX_MCS_VHT + 1];
	u64 he_mcs_count[HAL_RX_MAX_MCS_HE + 1];
	u64 be_mcs_count[HAL_RX_MAX_MCS_BE + 1];
	u64 nss_count[HAL_RX_MAX_NSS];
	u64 bw_count[HAL_RX_BW_MAX];
	u64 gi_count[HAL_RX_GI_MAX];
	u64 legacy_count[HAL_RX_MAX_NUM_LEGACY_RATES];
	u64 rx_rate[HAL_RX_BW_MAX][HAL_RX_GI_MAX][HAL_RX_MAX_NSS][HAL_RX_MAX_MCS_HT + 1];
};

struct ath12k_rx_peer_stats {
	u64 num_msdu;
	u64 num_mpdu_fcs_ok;
	u64 num_mpdu_fcs_err;
	u64 tcp_msdu_count;
	u64 udp_msdu_count;
	u64 other_msdu_count;
	u64 ampdu_msdu_count;
	u64 non_ampdu_msdu_count;
	u64 stbc_count;
	u64 beamformed_count;
	u64 coding_count[HAL_RX_SU_MU_CODING_MAX];
	u64 tid_count[IEEE80211_NUM_TIDS + 1];
	u64 pream_cnt[HAL_RX_PREAMBLE_MAX];
	u64 reception_type[HAL_RX_RECEPTION_TYPE_MAX];
	u64 rx_duration;
	u64 dcm_count;
	u64 ru_alloc_cnt[HAL_RX_RU_ALLOC_TYPE_MAX];
	struct ath12k_rx_peer_rate_stats pkt_stats;
	struct ath12k_rx_peer_rate_stats byte_stats;
};

struct ath12k_wbm_tx_stats {
	u64 wbm_tx_comp_stats[HAL_WBM_REL_HTT_TX_COMP_STATUS_MAX];
};

struct ath12k_dp_peer_stats {
	struct ath12k_rx_peer_stats *rx_stats;
	struct ath12k_wbm_tx_stats *wbm_tx_stats;
};

DECLARE_EWMA(avg_rssi, 10, 8)

struct ath12k_dp_link_peer {
	struct list_head list;
	struct ieee80211_sta *sta;
	struct ath12k_dp_peer *dp_peer;
	int vdev_id;
	u8 addr[ETH_ALEN];
	int peer_id;
	u16 ast_hash;
	u8 pdev_idx;
	u16 hw_peer_id;

	struct ppdu_user_delayba ppdu_stats_delayba;
	bool delayba_flag;
	bool is_authorized;
	bool mlo;
	/* protected by ab->data_lock */

	u16 ml_id;

	/* any other ML info common for all partners can be added
	 * here and would be same for all partner peers.
	 */
	u8 ml_addr[ETH_ALEN];

	/* To ensure only certain work related to dp is done once */
	bool primary_link;

	/* for reference to ath12k_link_sta */
	u8 link_id;

	/* peer addr based rhashtable list pointer */
	struct rhash_head rhash_addr;

	u8 hw_link_id;
	u32 rx_tid_active_bitmask;

	/* link stats */
	struct rate_info txrate;
	struct rate_info last_txrate;
	u64 rx_duration;
	u64 tx_duration;
	u8 rssi_comb;
	struct ewma_avg_rssi avg_rssi;
	struct ath12k_dp_peer_stats peer_stats;
	u32 tx_retry_failed;
	u32 tx_retry_count;
};

void ath12k_dp_link_peer_unmap_event(struct ath12k_base *ab, u16 peer_id);
void ath12k_dp_link_peer_map_event(struct ath12k_base *ab, u8 vdev_id, u16 peer_id,
				   u8 *mac_addr, u16 ast_hash, u16 hw_peer_id);

struct ath12k_dp_peer {
	struct list_head list;
	bool is_mlo;
	bool dp_setup_done;

	u8 ucast_keyidx;
	u8 addr[ETH_ALEN];

	u8 mcast_keyidx;
	bool ucast_ra_only;
	int peer_id;
	struct ieee80211_sta *sta;

	u8 hw_links[ATH12K_GROUP_MAX_RADIO];

	u16 sec_type_grp;
	u16 sec_type;

	/* Info used in MMIC verification of * RX fragments */
	struct crypto_shash *tfm_mmic;
	struct ieee80211_key_conf *keys[WMI_MAX_KEY_INDEX + 1];
	struct ath12k_dp_link_peer __rcu *link_peers[ATH12K_NUM_MAX_LINKS];
	struct ath12k_reoq_buf reoq_bufs[IEEE80211_NUM_TIDS + 1];
	struct ath12k_dp_rx_tid rx_tid[IEEE80211_NUM_TIDS + 1];
};

struct ath12k_dp_link_peer *
ath12k_dp_link_peer_find_by_vdev_and_addr(struct ath12k_dp *dp,
					  int vdev_id, const u8 *addr);
struct ath12k_dp_link_peer *
ath12k_dp_link_peer_find_by_addr(struct ath12k_dp *dp, const u8 *addr);
bool ath12k_dp_link_peer_exist_by_vdev_id(struct ath12k_dp *dp, int vdev_id);
struct ath12k_dp_link_peer *
ath12k_dp_link_peer_find_by_ast(struct ath12k_dp *dp, int ast_hash);
struct ath12k_dp_link_peer *
ath12k_dp_link_peer_find_by_pdev_and_addr(struct ath12k_dp *dp, u8 pdev_idx,
					  const u8 *addr);
struct ath12k_link_sta *ath12k_dp_link_peer_to_link_sta(struct ath12k_base *ab,
							struct ath12k_dp_link_peer *peer);
int ath12k_dp_link_peer_rhash_tbl_init(struct ath12k_dp *dp);
void ath12k_dp_link_peer_rhash_tbl_destroy(struct ath12k_dp *dp);
int ath12k_dp_link_peer_rhash_add(struct ath12k_dp *dp,
				  struct ath12k_dp_link_peer *peer);
void ath12k_dp_link_peer_rhash_delete(struct ath12k_dp *dp,
				      struct ath12k_dp_link_peer *peer);
int ath12k_dp_peer_create(struct ath12k_dp_hw *dp_hw, u8 *addr,
			  struct ath12k_dp_peer_create_params *params);
void ath12k_dp_peer_delete(struct ath12k_dp_hw *dp_hw, u8 *addr,
			   struct ieee80211_sta *sta);
struct ath12k_dp_peer *ath12k_dp_peer_find_by_addr(struct ath12k_dp_hw *dp_hw, u8 *addr);
struct ath12k_dp_peer *ath12k_dp_peer_find_by_addr_and_sta(struct ath12k_dp_hw *dp_hw,
							   u8 *addr,
							   struct ieee80211_sta *sta);
u16 ath12k_dp_peer_get_peerid_index(struct ath12k_dp *dp, u16 peer_id);
struct ath12k_dp_peer *ath12k_dp_peer_find_by_peerid(struct ath12k_pdev_dp *dp_pdev,
						     u16 peer_id);
struct ath12k_dp_link_peer *
ath12k_dp_link_peer_find_by_peerid(struct ath12k_pdev_dp *dp_pdev, u16 peer_id);
void ath12k_dp_link_peer_free(struct ath12k_dp_link_peer *peer);
#endif
