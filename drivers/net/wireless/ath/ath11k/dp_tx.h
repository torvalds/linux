/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */

#ifndef ATH11K_DP_TX_H
#define ATH11K_DP_TX_H

#include "core.h"
#include "hal_tx.h"

struct ath11k_dp_htt_wbm_tx_status {
	u32 msdu_id;
	bool acked;
	int ack_rssi;
	u16 peer_id;
};

void ath11k_dp_tx_update_txcompl(struct ath11k *ar, struct hal_tx_status *ts);
int ath11k_dp_tx_htt_h2t_ver_req_msg(struct ath11k_base *ab);
int ath11k_dp_tx(struct ath11k *ar, struct ath11k_vif *arvif,
		 struct ath11k_sta *arsta, struct sk_buff *skb);
void ath11k_dp_tx_completion_handler(struct ath11k_base *ab, int ring_id);
int ath11k_dp_tx_send_reo_cmd(struct ath11k_base *ab, struct dp_rx_tid *rx_tid,
			      enum hal_reo_cmd_type type,
			      struct ath11k_hal_reo_cmd *cmd,
			      void (*func)(struct ath11k_dp *, void *,
					   enum hal_reo_cmd_status));

int ath11k_dp_tx_htt_h2t_ppdu_stats_req(struct ath11k *ar, u32 mask);
int
ath11k_dp_tx_htt_h2t_ext_stats_req(struct ath11k *ar, u8 type,
				   struct htt_ext_stats_cfg_params *cfg_params,
				   u64 cookie);
int ath11k_dp_tx_htt_monitor_mode_ring_config(struct ath11k *ar, bool reset);

int ath11k_dp_tx_htt_rx_filter_setup(struct ath11k_base *ab, u32 ring_id,
				     int mac_id, enum hal_ring_type ring_type,
				     int rx_buf_size,
				     struct htt_rx_ring_tlv_filter *tlv_filter);

int ath11k_dp_tx_htt_rx_full_mon_setup(struct ath11k_base *ab, int mac_id,
				       bool config);
#endif
