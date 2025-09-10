/* SPDX-License-Identifier: BSD-3-Clause-Clear*/
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_HAL_QCN9274_H
#define ATH12K_HAL_QCN9274_H

#include <linux/ieee80211.h>
#include <linux/etherdevice.h>
#include "../hal.h"
#include "hal_rx.h"

bool ath12k_hal_rx_desc_get_first_msdu_qcn9274(struct hal_rx_desc *desc);
bool ath12k_hal_rx_desc_get_last_msdu_qcn9274(struct hal_rx_desc *desc);
u8 ath12k_hal_rx_desc_get_l3_pad_bytes_qcn9274(struct hal_rx_desc *desc);
bool ath12k_hal_rx_desc_encrypt_valid_qcn9274(struct hal_rx_desc *desc);
u32 ath12k_hal_rx_desc_get_encrypt_type_qcn9274(struct hal_rx_desc *desc);
u8 ath12k_hal_rx_desc_get_decap_type_qcn9274(struct hal_rx_desc *desc);
u8 ath12k_hal_rx_desc_get_mesh_ctl_qcn9274(struct hal_rx_desc *desc);
bool ath12k_hal_rx_desc_get_mpdu_seq_ctl_vld_qcn9274(struct hal_rx_desc *desc);
bool ath12k_hal_rx_desc_get_mpdu_fc_valid_qcn9274(struct hal_rx_desc *desc);
u16 ath12k_hal_rx_desc_get_mpdu_start_seq_no_qcn9274(struct hal_rx_desc *desc);
u16 ath12k_hal_rx_desc_get_msdu_len_qcn9274(struct hal_rx_desc *desc);
u8 ath12k_hal_rx_desc_get_msdu_sgi_qcn9274(struct hal_rx_desc *desc);
u8 ath12k_hal_rx_desc_get_msdu_rate_mcs_qcn9274(struct hal_rx_desc *desc);
u8 ath12k_hal_rx_desc_get_msdu_rx_bw_qcn9274(struct hal_rx_desc *desc);
u32 ath12k_hal_rx_desc_get_msdu_freq_qcn9274(struct hal_rx_desc *desc);
u8 ath12k_hal_rx_desc_get_msdu_pkt_type_qcn9274(struct hal_rx_desc *desc);
u8 ath12k_hal_rx_desc_get_msdu_nss_qcn9274(struct hal_rx_desc *desc);
u8 ath12k_hal_rx_desc_get_mpdu_tid_qcn9274(struct hal_rx_desc *desc);
u16 ath12k_hal_rx_desc_get_mpdu_peer_id_qcn9274(struct hal_rx_desc *desc);
void ath12k_hal_rx_desc_copy_end_tlv_qcn9274(struct hal_rx_desc *fdesc,
					     struct hal_rx_desc *ldesc);
u32 ath12k_hal_rx_desc_get_mpdu_ppdu_id_qcn9274(struct hal_rx_desc *desc);
void ath12k_hal_rx_desc_set_msdu_len_qcn9274(struct hal_rx_desc *desc, u16 len);
u8 *ath12k_hal_rx_desc_get_msdu_payload_qcn9274(struct hal_rx_desc *desc);
u32 ath12k_hal_rx_desc_get_mpdu_start_offset_qcn9274(void);
u32 ath12k_hal_rx_desc_get_msdu_end_offset_qcn9274(void);
bool ath12k_hal_rx_desc_mac_addr2_valid_qcn9274(struct hal_rx_desc *desc);
u8 *ath12k_hal_rx_desc_mpdu_start_addr2_qcn9274(struct hal_rx_desc *desc);
bool ath12k_hal_rx_desc_is_da_mcbc_qcn9274(struct hal_rx_desc *desc);
bool ath12k_hal_rx_h_msdu_done_qcn9274(struct hal_rx_desc *desc);
bool ath12k_hal_rx_h_l4_cksum_fail_qcn9274(struct hal_rx_desc *desc);
bool ath12k_hal_rx_h_ip_cksum_fail_qcn9274(struct hal_rx_desc *desc);
bool ath12k_hal_rx_h_is_decrypted_qcn9274(struct hal_rx_desc *desc);
u32 ath12k_hal_get_rx_desc_size_qcn9274(void);
u8 ath12k_hal_rx_desc_get_msdu_src_link_qcn9274(struct hal_rx_desc *desc);
u16 ath12k_hal_rx_mpdu_start_wmask_get_qcn9274(void);
u32 ath12k_hal_rx_msdu_end_wmask_get_qcn9274(void);
u32 ath12k_hal_rx_h_mpdu_err_qcn9274(struct hal_rx_desc *desc);
void ath12k_hal_rx_desc_get_crypto_hdr_qcn9274(struct hal_rx_desc *desc,
					       u8 *crypto_hdr,
					       enum hal_encrypt_type enctype);
void ath12k_hal_rx_desc_get_dot11_hdr_qcn9274(struct hal_rx_desc *desc,
					      struct ieee80211_hdr *hdr);
#endif
