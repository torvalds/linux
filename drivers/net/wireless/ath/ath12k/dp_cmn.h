/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_DP_CMN_H
#define ATH12K_DP_CMN_H

#include "cmn_defs.h"

struct ath12k_hw_group;

struct ath12k_dp_hw_group {
	struct ath12k_dp *dp[ATH12K_MAX_DEVICES];
};

struct ath12k_dp_link_vif {
	u32 vdev_id;
	u8 search_type;
	u8 hal_addr_search_flags;
	u8 pdev_idx;
	u8 lmac_id;
	u16 ast_idx;
	u16 ast_hash;
	u16 tcl_metadata;
	u8 vdev_id_check_en;
	int bank_id;
};

struct ath12k_dp_vif {
	u8 tx_encap_type;
	u32 key_cipher;
	atomic_t mcbc_gsn;
	struct ath12k_dp_link_vif dp_link_vif[ATH12K_NUM_MAX_LINKS];
};

/* TODO: Move this to a separate dp_stats file */
struct ath12k_per_peer_tx_stats {
	u32 succ_bytes;
	u32 retry_bytes;
	u32 failed_bytes;
	u32 duration;
	u16 succ_pkts;
	u16 retry_pkts;
	u16 failed_pkts;
	u16 ru_start;
	u16 ru_tones;
	u8 ba_fails;
	u8 ppdu_type;
	u32 mu_grpid;
	u32 mu_pos;
	bool is_ampdu;
};

static inline struct ath12k_dp_link_vif *
ath12k_dp_vif_to_dp_link_vif(struct ath12k_dp_vif *dp_vif, u8 link_id)
{
	return &dp_vif->dp_link_vif[link_id];
}

void ath12k_dp_cmn_device_deinit(struct ath12k_dp *dp);
int ath12k_dp_cmn_device_init(struct ath12k_dp *dp);
void ath12k_dp_cmn_hw_group_unassign(struct ath12k_dp *dp,
				     struct ath12k_hw_group *ag);
void ath12k_dp_cmn_hw_group_assign(struct ath12k_dp *dp,
				   struct ath12k_hw_group *ag);

#endif
