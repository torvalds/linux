/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#ifndef ATH12K_DP_RX_WIFI7_H
#define ATH12K_DP_RX_WIFI7_H

#include "../core.h"
#include "../dp_rx.h"

int ath12k_wifi7_dp_rx_process_wbm_err(struct ath12k_base *ab,
				       struct napi_struct *napi, int budget);
int ath12k_wifi7_dp_rx_process_err(struct ath12k_base *ab, struct napi_struct *napi,
				   int budget);
int ath12k_wifi7_dp_rx_process(struct ath12k_base *ab, int mac_id,
			       struct napi_struct *napi,
			       int budget);
void ath12k_wifi7_dp_rx_process_reo_status(struct ath12k_base *ab);
int ath12k_dp_rxdma_ring_sel_config_qcn9274(struct ath12k_base *ab);
int ath12k_dp_rxdma_ring_sel_config_wcn7850(struct ath12k_base *ab);
void ath12k_wifi7_dp_setup_pn_check_reo_cmd(struct ath12k_hal_reo_cmd *cmd,
					    struct ath12k_dp_rx_tid *rx_tid,
					    u32 cipher, enum set_key_cmd key_cmd);
int ath12k_wifi7_dp_rx_assign_reoq(struct ath12k_base *ab, struct ath12k_sta *ahsta,
				   struct ath12k_dp_rx_tid *rx_tid,
				   u16 ssn, enum hal_pn_type pn_type);
int ath12k_wifi7_dp_rx_link_desc_return(struct ath12k_base *ab,
					struct ath12k_buffer_addr *buf_addr_info,
					enum hal_wbm_rel_bm_act action);
void ath12k_wifi7_peer_rx_tid_qref_setup(struct ath12k_base *ab, u16 peer_id, u16 tid,
					 dma_addr_t paddr);
void ath12k_wifi7_dp_rx_peer_tid_delete(struct ath12k *ar,
					struct ath12k_peer *peer, u8 tid);
int ath12k_wifi7_dp_reo_cmd_send(struct ath12k_base *ab, struct ath12k_dp_rx_tid *rx_tid,
				 enum hal_reo_cmd_type type,
				 struct ath12k_hal_reo_cmd *cmd,
				 void (*cb)(struct ath12k_dp *dp, void *ctx,
					    enum hal_reo_cmd_status status));
void ath12k_wifi7_dp_reo_cache_flush(struct ath12k_base *ab,
				     struct ath12k_dp_rx_tid *rx_tid);
int ath12k_wifi7_peer_rx_tid_reo_update(struct ath12k *ar,
					struct ath12k_peer *peer,
					struct ath12k_dp_rx_tid *rx_tid,
					u32 ba_win_sz, u16 ssn,
					bool update_ssn);
static inline
void ath12k_wifi7_dp_extract_rx_desc_data(struct ath12k_base *ab,
					  struct hal_rx_desc_data *rx_info,
					  struct hal_rx_desc *rx_desc,
					  struct hal_rx_desc *ldesc)
{
	ab->hal.hal_ops->extract_rx_desc_data(rx_info, rx_desc, ldesc);
}
#endif
