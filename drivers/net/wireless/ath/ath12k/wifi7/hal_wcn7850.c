// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "hal_desc.h"
#include "hal_wcn7850.h"

static inline
bool ath12k_hal_rx_desc_get_first_msdu_wcn7850(struct hal_rx_desc *desc)
{
	return !!le16_get_bits(desc->u.wcn7850.msdu_end.info5,
			       RX_MSDU_END_INFO5_FIRST_MSDU);
}

static inline
bool ath12k_hal_rx_desc_get_last_msdu_wcn7850(struct hal_rx_desc *desc)
{
	return !!le16_get_bits(desc->u.wcn7850.msdu_end.info5,
			       RX_MSDU_END_INFO5_LAST_MSDU);
}

u8 ath12k_hal_rx_desc_get_l3_pad_bytes_wcn7850(struct hal_rx_desc *desc)
{
	return le16_get_bits(desc->u.wcn7850.msdu_end.info5,
			    RX_MSDU_END_INFO5_L3_HDR_PADDING);
}

static inline
bool ath12k_hal_rx_desc_encrypt_valid_wcn7850(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.wcn7850.mpdu_start.info4,
			       RX_MPDU_START_INFO4_ENCRYPT_INFO_VALID);
}

static inline
u32 ath12k_hal_rx_desc_get_encrypt_type_wcn7850(struct hal_rx_desc *desc)
{
	if (!ath12k_hal_rx_desc_encrypt_valid_wcn7850(desc))
		return HAL_ENCRYPT_TYPE_OPEN;

	return le32_get_bits(desc->u.wcn7850.mpdu_start.info2,
			     RX_MPDU_START_INFO2_ENC_TYPE);
}

static inline
u8 ath12k_hal_rx_desc_get_decap_type_wcn7850(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.wcn7850.msdu_end.info11,
			     RX_MSDU_END_INFO11_DECAP_FORMAT);
}

static inline
u8 ath12k_hal_rx_desc_get_mesh_ctl_wcn7850(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.wcn7850.msdu_end.info11,
			     RX_MSDU_END_INFO11_MESH_CTRL_PRESENT);
}

static inline
bool ath12k_hal_rx_desc_get_mpdu_seq_ctl_vld_wcn7850(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.wcn7850.mpdu_start.info4,
			       RX_MPDU_START_INFO4_MPDU_SEQ_CTRL_VALID);
}

static inline
bool ath12k_hal_rx_desc_get_mpdu_fc_valid_wcn7850(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.wcn7850.mpdu_start.info4,
			       RX_MPDU_START_INFO4_MPDU_FCTRL_VALID);
}

static inline
u16 ath12k_hal_rx_desc_get_mpdu_start_seq_no_wcn7850(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.wcn7850.mpdu_start.info4,
			     RX_MPDU_START_INFO4_MPDU_SEQ_NUM);
}

static inline
u16 ath12k_hal_rx_desc_get_msdu_len_wcn7850(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.wcn7850.msdu_end.info10,
			     RX_MSDU_END_INFO10_MSDU_LENGTH);
}

static inline
u8 ath12k_hal_rx_desc_get_msdu_sgi_wcn7850(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.wcn7850.msdu_end.info12,
			     RX_MSDU_END_INFO12_SGI);
}

static inline
u8 ath12k_hal_rx_desc_get_msdu_rate_mcs_wcn7850(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.wcn7850.msdu_end.info12,
			     RX_MSDU_END_INFO12_RATE_MCS);
}

static inline
u8 ath12k_hal_rx_desc_get_msdu_rx_bw_wcn7850(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.wcn7850.msdu_end.info12,
			     RX_MSDU_END_INFO12_RECV_BW);
}

static inline
u32 ath12k_hal_rx_desc_get_msdu_freq_wcn7850(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->u.wcn7850.msdu_end.phy_meta_data);
}

static inline
u8 ath12k_hal_rx_desc_get_msdu_pkt_type_wcn7850(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.wcn7850.msdu_end.info12,
			     RX_MSDU_END_INFO12_PKT_TYPE);
}

static inline
u8 ath12k_hal_rx_desc_get_msdu_nss_wcn7850(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.wcn7850.msdu_end.info12,
			     RX_MSDU_END_INFO12_MIMO_SS_BITMAP);
}

static inline
u8 ath12k_hal_rx_desc_get_mpdu_tid_wcn7850(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.wcn7850.mpdu_start.info2,
			     RX_MPDU_START_INFO2_TID);
}

static inline
u16 ath12k_hal_rx_desc_get_mpdu_peer_id_wcn7850(struct hal_rx_desc *desc)
{
	return __le16_to_cpu(desc->u.wcn7850.mpdu_start.sw_peer_id);
}

void ath12k_hal_rx_desc_copy_end_tlv_wcn7850(struct hal_rx_desc *fdesc,
					     struct hal_rx_desc *ldesc)
{
	memcpy(&fdesc->u.wcn7850.msdu_end, &ldesc->u.wcn7850.msdu_end,
	       sizeof(struct rx_msdu_end_qcn9274));
}

u32 ath12k_hal_rx_desc_get_mpdu_start_tag_wcn7850(struct hal_rx_desc *desc)
{
	return le64_get_bits(desc->u.wcn7850.mpdu_start_tag,
			    HAL_TLV_HDR_TAG);
}

u32 ath12k_hal_rx_desc_get_mpdu_ppdu_id_wcn7850(struct hal_rx_desc *desc)
{
	return __le16_to_cpu(desc->u.wcn7850.mpdu_start.phy_ppdu_id);
}

void ath12k_hal_rx_desc_set_msdu_len_wcn7850(struct hal_rx_desc *desc, u16 len)
{
	u32 info = __le32_to_cpu(desc->u.wcn7850.msdu_end.info10);

	info &= ~RX_MSDU_END_INFO10_MSDU_LENGTH;
	info |= u32_encode_bits(len, RX_MSDU_END_INFO10_MSDU_LENGTH);

	desc->u.wcn7850.msdu_end.info10 = __cpu_to_le32(info);
}

u8 *ath12k_hal_rx_desc_get_msdu_payload_wcn7850(struct hal_rx_desc *desc)
{
	return &desc->u.wcn7850.msdu_payload[0];
}

u32 ath12k_hal_rx_desc_get_mpdu_start_offset_wcn7850(void)
{
	return offsetof(struct hal_rx_desc_wcn7850, mpdu_start_tag);
}

u32 ath12k_hal_rx_desc_get_msdu_end_offset_wcn7850(void)
{
	return offsetof(struct hal_rx_desc_wcn7850, msdu_end_tag);
}

static inline
bool ath12k_hal_rx_desc_mac_addr2_valid_wcn7850(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->u.wcn7850.mpdu_start.info4) &
	       RX_MPDU_START_INFO4_MAC_ADDR2_VALID;
}

static inline
u8 *ath12k_hal_rx_desc_mpdu_start_addr2_wcn7850(struct hal_rx_desc *desc)
{
	return desc->u.wcn7850.mpdu_start.addr2;
}

static inline
bool ath12k_hal_rx_desc_is_da_mcbc_wcn7850(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->u.wcn7850.msdu_end.info13) &
	       RX_MSDU_END_INFO13_MCAST_BCAST;
}

static inline
bool ath12k_hal_rx_h_msdu_done_wcn7850(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.wcn7850.msdu_end.info14,
			       RX_MSDU_END_INFO14_MSDU_DONE);
}

static inline
bool ath12k_hal_rx_h_l4_cksum_fail_wcn7850(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.wcn7850.msdu_end.info13,
			       RX_MSDU_END_INFO13_TCP_UDP_CKSUM_FAIL);
}

static inline
bool ath12k_hal_rx_h_ip_cksum_fail_wcn7850(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.wcn7850.msdu_end.info13,
			      RX_MSDU_END_INFO13_IP_CKSUM_FAIL);
}

static inline
bool ath12k_hal_rx_h_is_decrypted_wcn7850(struct hal_rx_desc *desc)
{
	return (le32_get_bits(desc->u.wcn7850.msdu_end.info14,
			      RX_MSDU_END_INFO14_DECRYPT_STATUS_CODE) ==
			      RX_DESC_DECRYPT_STATUS_CODE_OK);
}

u32 ath12k_hal_get_rx_desc_size_wcn7850(void)
{
	return sizeof(struct hal_rx_desc_wcn7850);
}

u8 ath12k_hal_rx_desc_get_msdu_src_link_wcn7850(struct hal_rx_desc *desc)
{
	return 0;
}

static u32 ath12k_hal_rx_h_mpdu_err_wcn7850(struct hal_rx_desc *desc)
{
	u32 info = __le32_to_cpu(desc->u.wcn7850.msdu_end.info13);
	u32 errmap = 0;

	if (info & RX_MSDU_END_INFO13_FCS_ERR)
		errmap |= HAL_RX_MPDU_ERR_FCS;

	if (info & RX_MSDU_END_INFO13_DECRYPT_ERR)
		errmap |= HAL_RX_MPDU_ERR_DECRYPT;

	if (info & RX_MSDU_END_INFO13_TKIP_MIC_ERR)
		errmap |= HAL_RX_MPDU_ERR_TKIP_MIC;

	if (info & RX_MSDU_END_INFO13_A_MSDU_ERROR)
		errmap |= HAL_RX_MPDU_ERR_AMSDU_ERR;

	if (info & RX_MSDU_END_INFO13_OVERFLOW_ERR)
		errmap |= HAL_RX_MPDU_ERR_OVERFLOW;

	if (info & RX_MSDU_END_INFO13_MSDU_LEN_ERR)
		errmap |= HAL_RX_MPDU_ERR_MSDU_LEN;

	if (info & RX_MSDU_END_INFO13_MPDU_LEN_ERR)
		errmap |= HAL_RX_MPDU_ERR_MPDU_LEN;

	return errmap;
}

void ath12k_hal_rx_desc_get_crypto_hdr_wcn7850(struct hal_rx_desc *desc,
					       u8 *crypto_hdr,
					       enum hal_encrypt_type enctype)
{
	unsigned int key_id;

	switch (enctype) {
	case HAL_ENCRYPT_TYPE_OPEN:
		return;
	case HAL_ENCRYPT_TYPE_TKIP_NO_MIC:
	case HAL_ENCRYPT_TYPE_TKIP_MIC:
		crypto_hdr[0] =
			HAL_RX_MPDU_INFO_PN_GET_BYTE2(desc->u.wcn7850.mpdu_start.pn[0]);
		crypto_hdr[1] = 0;
		crypto_hdr[2] =
			HAL_RX_MPDU_INFO_PN_GET_BYTE1(desc->u.wcn7850.mpdu_start.pn[0]);
		break;
	case HAL_ENCRYPT_TYPE_CCMP_128:
	case HAL_ENCRYPT_TYPE_CCMP_256:
	case HAL_ENCRYPT_TYPE_GCMP_128:
	case HAL_ENCRYPT_TYPE_AES_GCMP_256:
		crypto_hdr[0] =
			HAL_RX_MPDU_INFO_PN_GET_BYTE1(desc->u.wcn7850.mpdu_start.pn[0]);
		crypto_hdr[1] =
			HAL_RX_MPDU_INFO_PN_GET_BYTE2(desc->u.wcn7850.mpdu_start.pn[0]);
		crypto_hdr[2] = 0;
		break;
	case HAL_ENCRYPT_TYPE_WEP_40:
	case HAL_ENCRYPT_TYPE_WEP_104:
	case HAL_ENCRYPT_TYPE_WEP_128:
	case HAL_ENCRYPT_TYPE_WAPI_GCM_SM4:
	case HAL_ENCRYPT_TYPE_WAPI:
		return;
	}
	key_id = u32_get_bits(__le32_to_cpu(desc->u.wcn7850.mpdu_start.info5),
			      RX_MPDU_START_INFO5_KEY_ID);
	crypto_hdr[3] = 0x20 | (key_id << 6);
	crypto_hdr[4] = HAL_RX_MPDU_INFO_PN_GET_BYTE3(desc->u.wcn7850.mpdu_start.pn[0]);
	crypto_hdr[5] = HAL_RX_MPDU_INFO_PN_GET_BYTE4(desc->u.wcn7850.mpdu_start.pn[0]);
	crypto_hdr[6] = HAL_RX_MPDU_INFO_PN_GET_BYTE1(desc->u.wcn7850.mpdu_start.pn[1]);
	crypto_hdr[7] = HAL_RX_MPDU_INFO_PN_GET_BYTE2(desc->u.wcn7850.mpdu_start.pn[1]);
}

void ath12k_hal_rx_desc_get_dot11_hdr_wcn7850(struct hal_rx_desc *desc,
					      struct ieee80211_hdr *hdr)
{
	hdr->frame_control = desc->u.wcn7850.mpdu_start.frame_ctrl;
	hdr->duration_id = desc->u.wcn7850.mpdu_start.duration;
	ether_addr_copy(hdr->addr1, desc->u.wcn7850.mpdu_start.addr1);
	ether_addr_copy(hdr->addr2, desc->u.wcn7850.mpdu_start.addr2);
	ether_addr_copy(hdr->addr3, desc->u.wcn7850.mpdu_start.addr3);
	if (__le32_to_cpu(desc->u.wcn7850.mpdu_start.info4) &
			RX_MPDU_START_INFO4_MAC_ADDR4_VALID) {
		ether_addr_copy(hdr->addr4, desc->u.wcn7850.mpdu_start.addr4);
	}
	hdr->seq_ctrl = desc->u.wcn7850.mpdu_start.seq_ctrl;
}

void ath12k_hal_extract_rx_desc_data_wcn7850(struct hal_rx_desc_data *rx_desc_data,
					     struct hal_rx_desc *rx_desc,
					     struct hal_rx_desc *ldesc)
{
	rx_desc_data->is_first_msdu = ath12k_hal_rx_desc_get_first_msdu_wcn7850(ldesc);
	rx_desc_data->is_last_msdu = ath12k_hal_rx_desc_get_last_msdu_wcn7850(ldesc);
	rx_desc_data->l3_pad_bytes = ath12k_hal_rx_desc_get_l3_pad_bytes_wcn7850(ldesc);
	rx_desc_data->enctype = ath12k_hal_rx_desc_get_encrypt_type_wcn7850(rx_desc);
	rx_desc_data->decap_type = ath12k_hal_rx_desc_get_decap_type_wcn7850(rx_desc);
	rx_desc_data->mesh_ctrl_present =
		ath12k_hal_rx_desc_get_mesh_ctl_wcn7850(rx_desc);
	rx_desc_data->seq_ctl_valid =
		ath12k_hal_rx_desc_get_mpdu_seq_ctl_vld_wcn7850(rx_desc);
	rx_desc_data->fc_valid = ath12k_hal_rx_desc_get_mpdu_fc_valid_wcn7850(rx_desc);
	rx_desc_data->seq_no = ath12k_hal_rx_desc_get_mpdu_start_seq_no_wcn7850(rx_desc);
	rx_desc_data->msdu_len = ath12k_hal_rx_desc_get_msdu_len_wcn7850(ldesc);
	rx_desc_data->sgi = ath12k_hal_rx_desc_get_msdu_sgi_wcn7850(rx_desc);
	rx_desc_data->rate_mcs = ath12k_hal_rx_desc_get_msdu_rate_mcs_wcn7850(rx_desc);
	rx_desc_data->bw = ath12k_hal_rx_desc_get_msdu_rx_bw_wcn7850(rx_desc);
	rx_desc_data->phy_meta_data = ath12k_hal_rx_desc_get_msdu_freq_wcn7850(rx_desc);
	rx_desc_data->pkt_type = ath12k_hal_rx_desc_get_msdu_pkt_type_wcn7850(rx_desc);
	rx_desc_data->nss = hweight8(ath12k_hal_rx_desc_get_msdu_nss_wcn7850(rx_desc));
	rx_desc_data->tid = ath12k_hal_rx_desc_get_mpdu_tid_wcn7850(rx_desc);
	rx_desc_data->peer_id = ath12k_hal_rx_desc_get_mpdu_peer_id_wcn7850(rx_desc);
	rx_desc_data->addr2_present = ath12k_hal_rx_desc_mac_addr2_valid_wcn7850(rx_desc);
	rx_desc_data->addr2 = ath12k_hal_rx_desc_mpdu_start_addr2_wcn7850(rx_desc);
	rx_desc_data->is_mcbc = ath12k_hal_rx_desc_is_da_mcbc_wcn7850(rx_desc);
	rx_desc_data->msdu_done = ath12k_hal_rx_h_msdu_done_wcn7850(ldesc);
	rx_desc_data->l4_csum_fail = ath12k_hal_rx_h_l4_cksum_fail_wcn7850(rx_desc);
	rx_desc_data->ip_csum_fail = ath12k_hal_rx_h_ip_cksum_fail_wcn7850(rx_desc);
	rx_desc_data->is_decrypted = ath12k_hal_rx_h_is_decrypted_wcn7850(rx_desc);
	rx_desc_data->err_bitmap = ath12k_hal_rx_h_mpdu_err_wcn7850(rx_desc);
}
