// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include "hal_desc.h"
#include "hal_qcn9274.h"

bool ath12k_hw_qcn9274_compact_rx_desc_get_first_msdu(struct hal_rx_desc *desc)
{
	return !!le16_get_bits(desc->u.qcn9274_compact.msdu_end.info5,
			       RX_MSDU_END_INFO5_FIRST_MSDU);
}

bool ath12k_hw_qcn9274_compact_rx_desc_get_last_msdu(struct hal_rx_desc *desc)
{
	return !!le16_get_bits(desc->u.qcn9274_compact.msdu_end.info5,
			       RX_MSDU_END_INFO5_LAST_MSDU);
}

u8 ath12k_hw_qcn9274_compact_rx_desc_get_l3_pad_bytes(struct hal_rx_desc *desc)
{
	return le16_get_bits(desc->u.qcn9274_compact.msdu_end.info5,
			     RX_MSDU_END_INFO5_L3_HDR_PADDING);
}

bool ath12k_hw_qcn9274_compact_rx_desc_encrypt_valid(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.qcn9274_compact.mpdu_start.info4,
			       RX_MPDU_START_INFO4_ENCRYPT_INFO_VALID);
}

u32 ath12k_hw_qcn9274_compact_rx_desc_get_encrypt_type(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274_compact.mpdu_start.info2,
			     RX_MPDU_START_INFO2_ENC_TYPE);
}

u8 ath12k_hw_qcn9274_compact_rx_desc_get_decap_type(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274_compact.msdu_end.info11,
			     RX_MSDU_END_INFO11_DECAP_FORMAT);
}

u8 ath12k_hw_qcn9274_compact_rx_desc_get_mesh_ctl(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274_compact.msdu_end.info11,
			     RX_MSDU_END_INFO11_MESH_CTRL_PRESENT);
}

bool ath12k_hw_qcn9274_compact_rx_desc_get_mpdu_seq_ctl_vld(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.qcn9274_compact.mpdu_start.info4,
			       RX_MPDU_START_INFO4_MPDU_SEQ_CTRL_VALID);
}

bool ath12k_hw_qcn9274_compact_rx_desc_get_mpdu_fc_valid(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.qcn9274_compact.mpdu_start.info4,
			       RX_MPDU_START_INFO4_MPDU_FCTRL_VALID);
}

u16 ath12k_hw_qcn9274_compact_rx_desc_get_mpdu_start_seq_no(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274_compact.mpdu_start.info4,
			     RX_MPDU_START_INFO4_MPDU_SEQ_NUM);
}

u16 ath12k_hw_qcn9274_compact_rx_desc_get_msdu_len(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274_compact.msdu_end.info10,
			     RX_MSDU_END_INFO10_MSDU_LENGTH);
}

u8 ath12k_hw_qcn9274_compact_rx_desc_get_msdu_sgi(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274_compact.msdu_end.info12,
			     RX_MSDU_END_INFO12_SGI);
}

u8 ath12k_hw_qcn9274_compact_rx_desc_get_msdu_rate_mcs(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274_compact.msdu_end.info12,
			     RX_MSDU_END_INFO12_RATE_MCS);
}

u8 ath12k_hw_qcn9274_compact_rx_desc_get_msdu_rx_bw(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274_compact.msdu_end.info12,
			     RX_MSDU_END_INFO12_RECV_BW);
}

u32 ath12k_hw_qcn9274_compact_rx_desc_get_msdu_freq(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->u.qcn9274_compact.msdu_end.phy_meta_data);
}

u8 ath12k_hw_qcn9274_compact_rx_desc_get_msdu_pkt_type(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274_compact.msdu_end.info12,
			     RX_MSDU_END_INFO12_PKT_TYPE);
}

u8 ath12k_hw_qcn9274_compact_rx_desc_get_msdu_nss(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274_compact.msdu_end.info12,
			     RX_MSDU_END_INFO12_MIMO_SS_BITMAP);
}

u8 ath12k_hw_qcn9274_compact_rx_desc_get_mpdu_tid(struct hal_rx_desc *desc)
{
	return le16_get_bits(desc->u.qcn9274_compact.msdu_end.info5,
			     RX_MSDU_END_INFO5_TID);
}

u16 ath12k_hw_qcn9274_compact_rx_desc_get_mpdu_peer_id(struct hal_rx_desc *desc)
{
	return __le16_to_cpu(desc->u.qcn9274_compact.mpdu_start.sw_peer_id);
}

void ath12k_hw_qcn9274_compact_rx_desc_copy_end_tlv(struct hal_rx_desc *fdesc,
						    struct hal_rx_desc *ldesc)
{
	fdesc->u.qcn9274_compact.msdu_end = ldesc->u.qcn9274_compact.msdu_end;
}

u32 ath12k_hw_qcn9274_compact_rx_desc_get_mpdu_ppdu_id(struct hal_rx_desc *desc)
{
	return __le16_to_cpu(desc->u.qcn9274_compact.mpdu_start.phy_ppdu_id);
}

void ath12k_hw_qcn9274_compact_rx_desc_set_msdu_len(struct hal_rx_desc *desc, u16 len)
{
	u32 info = __le32_to_cpu(desc->u.qcn9274_compact.msdu_end.info10);

	info = u32_replace_bits(info, len, RX_MSDU_END_INFO10_MSDU_LENGTH);
	desc->u.qcn9274_compact.msdu_end.info10 = __cpu_to_le32(info);
}

u8 *ath12k_hw_qcn9274_compact_rx_desc_get_msdu_payload(struct hal_rx_desc *desc)
{
	return &desc->u.qcn9274_compact.msdu_payload[0];
}

u32 ath12k_hw_qcn9274_compact_rx_desc_get_mpdu_start_offset(void)
{
	return offsetof(struct hal_rx_desc_qcn9274_compact, mpdu_start);
}

u32 ath12k_hw_qcn9274_compact_rx_desc_get_msdu_end_offset(void)
{
	return offsetof(struct hal_rx_desc_qcn9274_compact, msdu_end);
}

bool ath12k_hw_qcn9274_compact_rx_desc_mac_addr2_valid(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->u.qcn9274_compact.mpdu_start.info4) &
			     RX_MPDU_START_INFO4_MAC_ADDR2_VALID;
}

u8 *ath12k_hw_qcn9274_compact_rx_desc_mpdu_start_addr2(struct hal_rx_desc *desc)
{
	return desc->u.qcn9274_compact.mpdu_start.addr2;
}

bool ath12k_hw_qcn9274_compact_rx_desc_is_da_mcbc(struct hal_rx_desc *desc)
{
	return __le16_to_cpu(desc->u.qcn9274_compact.msdu_end.info5) &
	       RX_MSDU_END_INFO5_DA_IS_MCBC;
}

bool ath12k_hw_qcn9274_compact_dp_rx_h_msdu_done(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.qcn9274_compact.msdu_end.info14,
			       RX_MSDU_END_INFO14_MSDU_DONE);
}

bool ath12k_hw_qcn9274_compact_dp_rx_h_l4_cksum_fail(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.qcn9274_compact.msdu_end.info13,
			       RX_MSDU_END_INFO13_TCP_UDP_CKSUM_FAIL);
}

bool ath12k_hw_qcn9274_compact_dp_rx_h_ip_cksum_fail(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.qcn9274_compact.msdu_end.info13,
			       RX_MSDU_END_INFO13_IP_CKSUM_FAIL);
}

bool ath12k_hw_qcn9274_compact_dp_rx_h_is_decrypted(struct hal_rx_desc *desc)
{
	return (le32_get_bits(desc->u.qcn9274_compact.msdu_end.info14,
			      RX_MSDU_END_INFO14_DECRYPT_STATUS_CODE) ==
			RX_DESC_DECRYPT_STATUS_CODE_OK);
}

u32 ath12k_hw_qcn9274_compact_get_rx_desc_size(void)
{
	return sizeof(struct hal_rx_desc_qcn9274_compact);
}

u8 ath12k_hw_qcn9274_compact_rx_desc_get_msdu_src_link(struct hal_rx_desc *desc)
{
	return le64_get_bits(desc->u.qcn9274_compact.msdu_end.msdu_end_tag,
			     RX_MSDU_END_64_TLV_SRC_LINK_ID);
}

u16 ath12k_hal_qcn9274_rx_mpdu_start_wmask_get(void)
{
	return QCN9274_MPDU_START_WMASK;
}

u32 ath12k_hal_qcn9274_rx_msdu_end_wmask_get(void)
{
	return QCN9274_MSDU_END_WMASK;
}

u32 ath12k_hw_qcn9274_compact_dp_rx_h_mpdu_err(struct hal_rx_desc *desc)
{
	u32 info = __le32_to_cpu(desc->u.qcn9274_compact.msdu_end.info13);
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

void ath12k_hw_qcn9274_compact_rx_desc_get_crypto_hdr(struct hal_rx_desc *desc,
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
		HAL_RX_MPDU_INFO_PN_GET_BYTE2(desc->u.qcn9274_compact.mpdu_start.pn[0]);
		crypto_hdr[1] = 0;
		crypto_hdr[2] =
		HAL_RX_MPDU_INFO_PN_GET_BYTE1(desc->u.qcn9274_compact.mpdu_start.pn[0]);
		break;
	case HAL_ENCRYPT_TYPE_CCMP_128:
	case HAL_ENCRYPT_TYPE_CCMP_256:
	case HAL_ENCRYPT_TYPE_GCMP_128:
	case HAL_ENCRYPT_TYPE_AES_GCMP_256:
		crypto_hdr[0] =
		HAL_RX_MPDU_INFO_PN_GET_BYTE1(desc->u.qcn9274_compact.mpdu_start.pn[0]);
		crypto_hdr[1] =
		HAL_RX_MPDU_INFO_PN_GET_BYTE2(desc->u.qcn9274_compact.mpdu_start.pn[0]);
		crypto_hdr[2] = 0;
		break;
	case HAL_ENCRYPT_TYPE_WEP_40:
	case HAL_ENCRYPT_TYPE_WEP_104:
	case HAL_ENCRYPT_TYPE_WEP_128:
	case HAL_ENCRYPT_TYPE_WAPI_GCM_SM4:
	case HAL_ENCRYPT_TYPE_WAPI:
		return;
	}
	key_id = le32_get_bits(desc->u.qcn9274_compact.mpdu_start.info5,
			       RX_MPDU_START_INFO5_KEY_ID);
	crypto_hdr[3] = 0x20 | (key_id << 6);
	crypto_hdr[4] =
		HAL_RX_MPDU_INFO_PN_GET_BYTE3(desc->u.qcn9274_compact.mpdu_start.pn[0]);
	crypto_hdr[5] =
		HAL_RX_MPDU_INFO_PN_GET_BYTE4(desc->u.qcn9274_compact.mpdu_start.pn[0]);
	crypto_hdr[6] =
		HAL_RX_MPDU_INFO_PN_GET_BYTE1(desc->u.qcn9274_compact.mpdu_start.pn[1]);
	crypto_hdr[7] =
		HAL_RX_MPDU_INFO_PN_GET_BYTE2(desc->u.qcn9274_compact.mpdu_start.pn[1]);
}

void ath12k_hw_qcn9274_compact_rx_desc_get_dot11_hdr(struct hal_rx_desc *desc,
						     struct ieee80211_hdr *hdr)
{
	hdr->frame_control = desc->u.qcn9274_compact.mpdu_start.frame_ctrl;
	hdr->duration_id = desc->u.qcn9274_compact.mpdu_start.duration;
	ether_addr_copy(hdr->addr1, desc->u.qcn9274_compact.mpdu_start.addr1);
	ether_addr_copy(hdr->addr2, desc->u.qcn9274_compact.mpdu_start.addr2);
	ether_addr_copy(hdr->addr3, desc->u.qcn9274_compact.mpdu_start.addr3);
	if (__le32_to_cpu(desc->u.qcn9274_compact.mpdu_start.info4) &
			RX_MPDU_START_INFO4_MAC_ADDR4_VALID) {
		ether_addr_copy(hdr->addr4, desc->u.qcn9274_compact.mpdu_start.addr4);
	}
	hdr->seq_ctrl = desc->u.qcn9274_compact.mpdu_start.seq_ctrl;
}
