/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef ATH12K_DP_TX_H
#define ATH12K_DP_TX_H

#include "core.h"
#include "hal_tx.h"

struct ath12k_dp_htt_wbm_tx_status {
	bool acked;
	s8 ack_rssi;
};

int ath12k_dp_tx_htt_h2t_ver_req_msg(struct ath12k_base *ab);
int ath12k_dp_tx(struct ath12k *ar, struct ath12k_link_vif *arvif,
		 struct sk_buff *skb, bool gsn_valid, int mcbc_gsn,
		 bool is_mcast);
void ath12k_dp_tx_completion_handler(struct ath12k_base *ab, int ring_id);

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
#endif
