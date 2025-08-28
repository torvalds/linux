/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_DP_TX_H
#define ATH12K_DP_TX_H

#include "core.h"
#include "wifi7/hal_tx.h"

struct ath12k_dp_htt_wbm_tx_status {
	bool acked;
	s8 ack_rssi;
};

int ath12k_dp_tx_htt_h2t_ver_req_msg(struct ath12k_base *ab);
int ath12k_dp_tx_htt_h2t_ppdu_stats_req(struct ath12k *ar, u32 mask);
int
ath12k_dp_tx_htt_h2t_ext_stats_req(struct ath12k *ar, u8 type,
				   struct htt_ext_stats_cfg_params *cfg_params,
				   u64 cookie);
int ath12k_dp_tx_htt_rx_monitor_mode_ring_config(struct ath12k *ar, bool reset);

int ath12k_dp_tx_htt_rx_filter_setup(struct ath12k_base *ab, u32 ring_id,
				     int mac_id, enum hal_ring_type ring_type,
				     int rx_buf_size,
				     struct htt_rx_ring_tlv_filter *tlv_filter);
void ath12k_dp_tx_put_bank_profile(struct ath12k_dp *dp, u8 bank_id);
int ath12k_dp_tx_htt_tx_filter_setup(struct ath12k_base *ab, u32 ring_id,
				     int mac_id, enum hal_ring_type ring_type,
				     int tx_buf_size,
				     struct htt_tx_ring_tlv_filter *htt_tlv_filter);
int ath12k_dp_tx_htt_monitor_mode_ring_config(struct ath12k *ar, bool reset);

enum hal_tcl_encap_type
ath12k_dp_tx_get_encap_type(struct ath12k_base *ab, struct sk_buff *skb);
void ath12k_dp_tx_encap_nwifi(struct sk_buff *skb);
u8 ath12k_dp_tx_get_tid(struct sk_buff *skb);
void *ath12k_dp_metadata_align_skb(struct sk_buff *skb, u8 tail_len);
int ath12k_dp_tx_align_payload(struct ath12k_base *ab,
			       struct sk_buff **pskb);
void ath12k_dp_tx_release_txbuf(struct ath12k_dp *dp,
				struct ath12k_tx_desc_info *tx_desc,
				u8 pool_id);
struct ath12k_tx_desc_info *ath12k_dp_tx_assign_buffer(struct ath12k_dp *dp,
						       u8 pool_id);
void ath12k_dp_tx_free_txbuf(struct ath12k_base *ab,
			     struct dp_tx_ring *tx_ring,
			     struct ath12k_tx_desc_params *desc_params);
#endif
