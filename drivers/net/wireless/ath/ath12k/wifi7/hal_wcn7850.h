/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_HAL_WCN7850_H
#define ATH12K_HAL_WCN7850_H

#include "../hal.h"
#include "hal_rx.h"
#include "hal.h"

extern const struct hal_ops hal_wcn7850_ops;
extern const struct ath12k_hw_regs wcn7850_regs;
extern const struct ath12k_hal_tcl_to_wbm_rbm_map
ath12k_hal_tcl_to_wbm_rbm_map_wcn7850[DP_TCL_NUM_RING_MAX];
extern const struct ath12k_hw_hal_params ath12k_hw_hal_params_wcn7850;

u8 ath12k_hal_rx_desc_get_l3_pad_bytes_wcn7850(struct hal_rx_desc *desc);
void ath12k_hal_rx_desc_copy_end_tlv_wcn7850(struct hal_rx_desc *fdesc,
					     struct hal_rx_desc *ldesc);
u32 ath12k_hal_rx_desc_get_mpdu_start_tag_wcn7850(struct hal_rx_desc *desc);
u32 ath12k_hal_rx_desc_get_mpdu_ppdu_id_wcn7850(struct hal_rx_desc *desc);
void ath12k_hal_rx_desc_set_msdu_len_wcn7850(struct hal_rx_desc *desc, u16 len);
u8 *ath12k_hal_rx_desc_get_msdu_payload_wcn7850(struct hal_rx_desc *desc);
u32 ath12k_hal_rx_desc_get_mpdu_start_offset_wcn7850(void);
u32 ath12k_hal_rx_desc_get_msdu_end_offset_wcn7850(void);
u32 ath12k_hal_get_rx_desc_size_wcn7850(void);
u8 ath12k_hal_rx_desc_get_msdu_src_link_wcn7850(struct hal_rx_desc *desc);
void ath12k_hal_rx_desc_get_crypto_hdr_wcn7850(struct hal_rx_desc *desc,
					       u8 *crypto_hdr,
					       enum hal_encrypt_type enctype);
void ath12k_hal_rx_desc_get_dot11_hdr_wcn7850(struct hal_rx_desc *desc,
					      struct ieee80211_hdr *hdr);
void ath12k_hal_extract_rx_desc_data_wcn7850(struct hal_rx_desc_data *rx_desc_data,
					     struct hal_rx_desc *rx_desc,
					     struct hal_rx_desc *ldesc);
int ath12k_hal_srng_create_config_wcn7850(struct ath12k_hal *hal);
#endif
