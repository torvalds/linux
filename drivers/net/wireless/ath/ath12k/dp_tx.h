/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_DP_TX_H
#define ATH12K_DP_TX_H

#include "core.h"

struct ath12k_dp_htt_wbm_tx_status {
	bool acked;
	s8 ack_rssi;
};

void ath12k_dp_tx_put_bank_profile(struct ath12k_dp *dp, u8 bank_id);
enum hal_tcl_encap_type
ath12k_dp_tx_get_encap_type(struct ath12k_base *ab, struct sk_buff *skb);
void ath12k_dp_tx_encap_nwifi(struct sk_buff *skb);
u8 ath12k_dp_tx_get_tid(struct sk_buff *skb);
void *ath12k_dp_metadata_align_skb(struct sk_buff *skb, u8 tail_len);
int ath12k_dp_tx_align_payload(struct ath12k_dp *dp, struct sk_buff **pskb);
void ath12k_dp_tx_release_txbuf(struct ath12k_dp *dp,
				struct ath12k_tx_desc_info *tx_desc,
				u8 pool_id);
struct ath12k_tx_desc_info *ath12k_dp_tx_assign_buffer(struct ath12k_dp *dp,
						       u8 pool_id);
void ath12k_dp_tx_free_txbuf(struct ath12k_dp *dp,
			     struct dp_tx_ring *tx_ring,
			     struct ath12k_tx_desc_params *desc_params);
#endif
