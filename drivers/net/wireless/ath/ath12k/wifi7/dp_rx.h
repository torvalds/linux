/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#ifndef ATH12K_DP_RX_WIFI7_H
#define ATH12K_DP_RX_WIFI7_H

#include "../core.h"
#include "../dp_rx.h"

int ath12k_dp_rx_process_wbm_err(struct ath12k_base *ab,
				 struct napi_struct *napi, int budget);
int ath12k_dp_rx_process_err(struct ath12k_base *ab, struct napi_struct *napi,
			     int budget);
int ath12k_dp_rx_process(struct ath12k_base *ab, int mac_id,
			 struct napi_struct *napi,
			 int budget);
void ath12k_dp_rx_process_reo_status(struct ath12k_base *ab);
int ath12k_dp_rxdma_ring_sel_config_qcn9274(struct ath12k_base *ab);
int ath12k_dp_rxdma_ring_sel_config_wcn7850(struct ath12k_base *ab);
void ath12k_dp_setup_pn_check_reo_cmd(struct ath12k_hal_reo_cmd *cmd,
				      struct ath12k_dp_rx_tid *rx_tid,
				      u32 cipher, enum set_key_cmd key_cmd);
int ath12k_dp_rx_assign_reoq(struct ath12k_base *ab, struct ath12k_sta *ahsta,
			     struct ath12k_dp_rx_tid *rx_tid,
			     u16 ssn, enum hal_pn_type pn_type);
#endif
