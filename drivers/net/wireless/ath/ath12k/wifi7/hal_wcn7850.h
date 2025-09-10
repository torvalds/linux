/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_HAL_WCN7850_H
#define ATH12K_HAL_WCN7850_H

#include "../hal.h"
#include "hal_rx.h"

bool ath12k_hal_rx_desc_get_first_msdu_wcn7850(struct hal_rx_desc *desc);
bool ath12k_hal_rx_desc_get_last_msdu_wcn7850(struct hal_rx_desc *desc);
u8 ath12k_hal_rx_desc_get_l3_pad_bytes_wcn7850(struct hal_rx_desc *desc);
bool ath12k_hal_rx_desc_encrypt_valid_wcn7850(struct hal_rx_desc *desc);
u32 ath12k_hal_rx_desc_get_encrypt_type_wcn7850(struct hal_rx_desc *desc);
u8 ath12k_hal_rx_desc_get_decap_type_wcn7850(struct hal_rx_desc *desc);
u8 ath12k_hal_rx_desc_get_mesh_ctl_wcn7850(struct hal_rx_desc *desc);
bool ath12k_hal_rx_desc_get_mpdu_seq_ctl_vld_wcn7850(struct hal_rx_desc *desc);
bool ath12k_hal_rx_desc_get_mpdu_fc_valid_wcn7850(struct hal_rx_desc *desc);
u16 ath12k_hal_rx_desc_get_mpdu_start_seq_no_wcn7850(struct hal_rx_desc *desc);
u16 ath12k_hal_rx_desc_get_msdu_len_wcn7850(struct hal_rx_desc *desc);
u8 ath12k_hal_rx_desc_get_msdu_sgi_wcn7850(struct hal_rx_desc *desc);
u8 ath12k_hal_rx_desc_get_msdu_rate_mcs_wcn7850(struct hal_rx_desc *desc);
u8 ath12k_hal_rx_desc_get_msdu_rx_bw_wcn7850(struct hal_rx_desc *desc);
u32 ath12k_hal_rx_desc_get_msdu_freq_wcn7850(struct hal_rx_desc *desc);
u8 ath12k_hal_rx_desc_get_msdu_pkt_type_wcn7850(struct hal_rx_desc *desc);
u8 ath12k_hal_rx_desc_get_msdu_nss_wcn7850(struct hal_rx_desc *desc);
u8 ath12k_hal_rx_desc_get_mpdu_tid_wcn7850(struct hal_rx_desc *desc);
u16 ath12k_hal_rx_desc_get_mpdu_peer_id_wcn7850(struct hal_rx_desc *desc);
void ath12k_hal_rx_desc_copy_end_tlv_wcn7850(struct hal_rx_desc *fdesc,
					     struct hal_rx_desc *ldesc);
u32 ath12k_hal_rx_desc_get_mpdu_start_tag_wcn7850(struct hal_rx_desc *desc);
u32 ath12k_hal_rx_desc_get_mpdu_ppdu_id_wcn7850(struct hal_rx_desc *desc);
void ath12k_hal_rx_desc_set_msdu_len_wcn7850(struct hal_rx_desc *desc, u16 len);
u8 *ath12k_hal_rx_desc_get_msdu_payload_wcn7850(struct hal_rx_desc *desc);
u32 ath12k_hal_rx_desc_get_mpdu_start_offset_wcn7850(void);
u32 ath12k_hal_rx_desc_get_msdu_end_offset_wcn7850(void);
bool ath12k_hal_rx_desc_mac_addr2_valid_wcn7850(struct hal_rx_desc *desc);
u8 *ath12k_hal_rx_desc_mpdu_start_addr2_wcn7850(struct hal_rx_desc *desc);
bool ath12k_hal_rx_desc_is_da_mcbc_wcn7850(struct hal_rx_desc *desc);
bool ath12k_hal_rx_h_msdu_done_wcn7850(struct hal_rx_desc *desc);
bool ath12k_hal_rx_h_l4_cksum_fail_wcn7850(struct hal_rx_desc *desc);
bool ath12k_hal_rx_h_ip_cksum_fail_wcn7850(struct hal_rx_desc *desc);
bool ath12k_hal_rx_h_is_decrypted_wcn7850(struct hal_rx_desc *desc);
u32 ath12k_hal_get_rx_desc_size_wcn7850(void);
u8 ath12k_hal_rx_desc_get_msdu_src_link_wcn7850(struct hal_rx_desc *desc);
u32 ath12k_hal_rx_h_mpdu_err_wcn7850(struct hal_rx_desc *desc);
void ath12k_hal_rx_desc_get_crypto_hdr_wcn7850(struct hal_rx_desc *desc,
					       u8 *crypto_hdr,
					       enum hal_encrypt_type enctype);
void ath12k_hal_rx_desc_get_dot11_hdr_wcn7850(struct hal_rx_desc *desc,
					      struct ieee80211_hdr *hdr);
#endif
