// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "hal_qcc2072.h"
#include "hal_wcn7850.h"

const struct ath12k_hw_regs qcc2072_regs = {
	/* SW2TCL(x) R0 ring configuration address */
	.tcl1_ring_id = 0x00000920,
	.tcl1_ring_misc = 0x00000928,
	.tcl1_ring_tp_addr_lsb = 0x00000934,
	.tcl1_ring_tp_addr_msb = 0x00000938,
	.tcl1_ring_consumer_int_setup_ix0 = 0x00000948,
	.tcl1_ring_consumer_int_setup_ix1 = 0x0000094c,
	.tcl1_ring_msi1_base_lsb = 0x00000960,
	.tcl1_ring_msi1_base_msb = 0x00000964,
	.tcl1_ring_msi1_data = 0x00000968,
	.tcl_ring_base_lsb = 0x00000b70,
	.tcl1_ring_base_lsb = 0x00000918,
	.tcl1_ring_base_msb = 0x0000091c,
	.tcl2_ring_base_lsb = 0x00000990,

	/* TCL STATUS ring address */
	.tcl_status_ring_base_lsb = 0x00000d50,

	.wbm_idle_ring_base_lsb = 0x00000d3c,
	.wbm_idle_ring_misc_addr = 0x00000d4c,
	.wbm_r0_idle_list_cntl_addr = 0x00000240,
	.wbm_r0_idle_list_size_addr = 0x00000244,
	.wbm_scattered_ring_base_lsb = 0x00000250,
	.wbm_scattered_ring_base_msb = 0x00000254,
	.wbm_scattered_desc_head_info_ix0 = 0x00000260,
	.wbm_scattered_desc_head_info_ix1 = 0x00000264,
	.wbm_scattered_desc_tail_info_ix0 = 0x00000270,
	.wbm_scattered_desc_tail_info_ix1 = 0x00000274,
	.wbm_scattered_desc_ptr_hp_addr = 0x00000027c,

	.wbm_sw_release_ring_base_lsb = 0x0000037c,
	.wbm_sw1_release_ring_base_lsb = ATH12K_HW_REG_UNDEFINED,
	.wbm0_release_ring_base_lsb = 0x00000e08,
	.wbm1_release_ring_base_lsb = 0x00000e80,

	/* PCIe base address */
	.pcie_qserdes_sysclk_en_sel = 0x01e0c0ac,
	.pcie_pcs_osc_dtct_config_base = 0x01e0cc58,

	/* PPE release ring address */
	.ppe_rel_ring_base = 0x0000046c,

	/* REO DEST ring address */
	.reo2_ring_base = 0x00000578,
	.reo1_misc_ctrl_addr = 0x00000ba0,
	.reo1_sw_cookie_cfg0 = 0x0000006c,
	.reo1_sw_cookie_cfg1 = 0x00000070,
	.reo1_qdesc_lut_base0 = ATH12K_HW_REG_UNDEFINED,
	.reo1_qdesc_lut_base1 = ATH12K_HW_REG_UNDEFINED,

	.reo1_ring_base_lsb = 0x00000500,
	.reo1_ring_base_msb = 0x00000504,
	.reo1_ring_id = 0x00000508,
	.reo1_ring_misc = 0x00000510,
	.reo1_ring_hp_addr_lsb = 0x00000514,
	.reo1_ring_hp_addr_msb = 0x00000518,
	.reo1_ring_producer_int_setup = 0x00000524,
	.reo1_ring_msi1_base_lsb = 0x00000548,
	.reo1_ring_msi1_base_msb = 0x0000054c,
	.reo1_ring_msi1_data = 0x00000550,
	.reo1_aging_thres_ix0 = 0x00000b2c,
	.reo1_aging_thres_ix1 = 0x00000b30,
	.reo1_aging_thres_ix2 = 0x00000b34,
	.reo1_aging_thres_ix3 = 0x00000b38,

	/* REO Exception ring address */
	.reo2_sw0_ring_base = 0x000008c0,

	/* REO Reinject ring address */
	.sw2reo_ring_base = 0x00000320,
	.sw2reo1_ring_base = 0x00000398,

	/* REO cmd ring address */
	.reo_cmd_ring_base = 0x000002a8,

	/* REO status ring address */
	.reo_status_ring_base = 0x00000aa0,

	/* CE base address */
	.umac_ce0_src_reg_base = 0x01b80000,
	.umac_ce0_dest_reg_base = 0x01b81000,
	.umac_ce1_src_reg_base = 0x01b82000,
	.umac_ce1_dest_reg_base = 0x01b83000,

	.gcc_gcc_pcie_hot_rst = 0x1e65304,

	.qrtr_node_id = 0x1e03300,
};

static void ath12k_hal_rx_desc_set_msdu_len_qcc2072(struct hal_rx_desc *desc, u16 len)
{
	u32 info = __le32_to_cpu(desc->u.qcc2072.msdu_end.info10);

	info &= ~RX_MSDU_END_INFO10_MSDU_LENGTH;
	info |= u32_encode_bits(len, RX_MSDU_END_INFO10_MSDU_LENGTH);

	desc->u.qcc2072.msdu_end.info10 = __cpu_to_le32(info);
}

static void ath12k_hal_rx_desc_get_dot11_hdr_qcc2072(struct hal_rx_desc *desc,
						     struct ieee80211_hdr *hdr)
{
	hdr->frame_control = desc->u.qcc2072.mpdu_start.frame_ctrl;
	hdr->duration_id = desc->u.qcc2072.mpdu_start.duration;
	ether_addr_copy(hdr->addr1, desc->u.qcc2072.mpdu_start.addr1);
	ether_addr_copy(hdr->addr2, desc->u.qcc2072.mpdu_start.addr2);
	ether_addr_copy(hdr->addr3, desc->u.qcc2072.mpdu_start.addr3);

	if (__le32_to_cpu(desc->u.qcc2072.mpdu_start.info4) &
	    RX_MPDU_START_INFO4_MAC_ADDR4_VALID)
		ether_addr_copy(hdr->addr4, desc->u.qcc2072.mpdu_start.addr4);

	hdr->seq_ctrl = desc->u.qcc2072.mpdu_start.seq_ctrl;
}

static void ath12k_hal_rx_desc_get_crypto_hdr_qcc2072(struct hal_rx_desc *desc,
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
			HAL_RX_MPDU_INFO_PN_GET_BYTE2(desc->u.qcc2072.mpdu_start.pn[0]);
		crypto_hdr[1] = 0;
		crypto_hdr[2] =
			HAL_RX_MPDU_INFO_PN_GET_BYTE1(desc->u.qcc2072.mpdu_start.pn[0]);
		break;
	case HAL_ENCRYPT_TYPE_CCMP_128:
	case HAL_ENCRYPT_TYPE_CCMP_256:
	case HAL_ENCRYPT_TYPE_GCMP_128:
	case HAL_ENCRYPT_TYPE_AES_GCMP_256:
		crypto_hdr[0] =
			HAL_RX_MPDU_INFO_PN_GET_BYTE1(desc->u.qcc2072.mpdu_start.pn[0]);
		crypto_hdr[1] =
			HAL_RX_MPDU_INFO_PN_GET_BYTE2(desc->u.qcc2072.mpdu_start.pn[0]);
		crypto_hdr[2] = 0;
		break;
	case HAL_ENCRYPT_TYPE_WEP_40:
	case HAL_ENCRYPT_TYPE_WEP_104:
	case HAL_ENCRYPT_TYPE_WEP_128:
	case HAL_ENCRYPT_TYPE_WAPI_GCM_SM4:
	case HAL_ENCRYPT_TYPE_WAPI:
		return;
	}

	key_id = u32_get_bits(__le32_to_cpu(desc->u.qcc2072.mpdu_start.info5),
			      RX_MPDU_START_INFO5_KEY_ID);
	crypto_hdr[3] = 0x20 | (key_id << 6);
	crypto_hdr[4] = HAL_RX_MPDU_INFO_PN_GET_BYTE3(desc->u.qcc2072.mpdu_start.pn[0]);
	crypto_hdr[5] = HAL_RX_MPDU_INFO_PN_GET_BYTE4(desc->u.qcc2072.mpdu_start.pn[0]);
	crypto_hdr[6] = HAL_RX_MPDU_INFO_PN_GET_BYTE1(desc->u.qcc2072.mpdu_start.pn[1]);
	crypto_hdr[7] = HAL_RX_MPDU_INFO_PN_GET_BYTE2(desc->u.qcc2072.mpdu_start.pn[1]);
}

static void ath12k_hal_rx_desc_copy_end_tlv_qcc2072(struct hal_rx_desc *fdesc,
						    struct hal_rx_desc *ldesc)
{
	memcpy(&fdesc->u.qcc2072.msdu_end, &ldesc->u.qcc2072.msdu_end,
	       sizeof(struct rx_msdu_end_qcn9274));
}

static u8 ath12k_hal_rx_desc_get_msdu_src_link_qcc2072(struct hal_rx_desc *desc)
{
	return 0;
}

static u8 ath12k_hal_rx_desc_get_l3_pad_bytes_qcc2072(struct hal_rx_desc *desc)
{
	return le16_get_bits(desc->u.qcc2072.msdu_end.info5,
			     RX_MSDU_END_INFO5_L3_HDR_PADDING);
}

static u32 ath12k_hal_rx_desc_get_mpdu_start_tag_qcc2072(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcc2072.mpdu_start_tag,
			     HAL_TLV_HDR_TAG);
}

static u32 ath12k_hal_rx_desc_get_mpdu_ppdu_id_qcc2072(struct hal_rx_desc *desc)
{
	return __le16_to_cpu(desc->u.qcc2072.mpdu_start.phy_ppdu_id);
}

static u8 *ath12k_hal_rx_desc_get_msdu_payload_qcc2072(struct hal_rx_desc *desc)
{
	return &desc->u.qcc2072.msdu_payload[0];
}

static bool ath12k_hal_rx_desc_get_first_msdu_qcc2072(struct hal_rx_desc *desc)
{
	return !!le16_get_bits(desc->u.qcc2072.msdu_end.info5,
			       RX_MSDU_END_INFO5_FIRST_MSDU);
}

static bool ath12k_hal_rx_desc_get_last_msdu_qcc2072(struct hal_rx_desc *desc)
{
	return !!le16_get_bits(desc->u.qcc2072.msdu_end.info5,
			       RX_MSDU_END_INFO5_LAST_MSDU);
}

static bool ath12k_hal_rx_desc_encrypt_valid_qcc2072(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.qcc2072.mpdu_start.info4,
			       RX_MPDU_START_INFO4_ENCRYPT_INFO_VALID);
}

static u32 ath12k_hal_rx_desc_get_encrypt_type_qcc2072(struct hal_rx_desc *desc)
{
	if (!ath12k_hal_rx_desc_encrypt_valid_qcc2072(desc))
		return HAL_ENCRYPT_TYPE_OPEN;

	return le32_get_bits(desc->u.qcc2072.mpdu_start.info2,
			     RX_MPDU_START_INFO2_ENC_TYPE);
}

static u8 ath12k_hal_rx_desc_get_decap_type_qcc2072(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcc2072.msdu_end.info11,
			     RX_MSDU_END_INFO11_DECAP_FORMAT);
}

static u8 ath12k_hal_rx_desc_get_mesh_ctl_qcc2072(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcc2072.msdu_end.info11,
			     RX_MSDU_END_INFO11_MESH_CTRL_PRESENT);
}

static bool ath12k_hal_rx_desc_get_mpdu_seq_ctl_vld_qcc2072(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.qcc2072.mpdu_start.info4,
			       RX_MPDU_START_INFO4_MPDU_SEQ_CTRL_VALID);
}

static bool ath12k_hal_rx_desc_get_mpdu_fc_valid_qcc2072(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.qcc2072.mpdu_start.info4,
			       RX_MPDU_START_INFO4_MPDU_FCTRL_VALID);
}

static u16 ath12k_hal_rx_desc_get_mpdu_start_seq_no_qcc2072(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcc2072.mpdu_start.info4,
			     RX_MPDU_START_INFO4_MPDU_SEQ_NUM);
}

static u16 ath12k_hal_rx_desc_get_msdu_len_qcc2072(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcc2072.msdu_end.info10,
			     RX_MSDU_END_INFO10_MSDU_LENGTH);
}

static u8 ath12k_hal_rx_desc_get_msdu_sgi_qcc2072(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcc2072.msdu_end.info12,
			     RX_MSDU_END_INFO12_SGI);
}

static u8 ath12k_hal_rx_desc_get_msdu_rate_mcs_qcc2072(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcc2072.msdu_end.info12,
			     RX_MSDU_END_INFO12_RATE_MCS);
}

static u8 ath12k_hal_rx_desc_get_msdu_rx_bw_qcc2072(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcc2072.msdu_end.info12,
			     RX_MSDU_END_INFO12_RECV_BW);
}

static u32 ath12k_hal_rx_desc_get_msdu_freq_qcc2072(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->u.qcc2072.msdu_end.phy_meta_data);
}

static u8 ath12k_hal_rx_desc_get_msdu_pkt_type_qcc2072(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcc2072.msdu_end.info12,
			     RX_MSDU_END_INFO12_PKT_TYPE);
}

static u8 ath12k_hal_rx_desc_get_msdu_nss_qcc2072(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcc2072.msdu_end.info12,
			     RX_MSDU_END_INFO12_MIMO_SS_BITMAP);
}

static u8 ath12k_hal_rx_desc_get_mpdu_tid_qcc2072(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcc2072.mpdu_start.info2,
			     RX_MPDU_START_INFO2_TID);
}

static u16 ath12k_hal_rx_desc_get_mpdu_peer_id_qcc2072(struct hal_rx_desc *desc)
{
	return __le16_to_cpu(desc->u.qcc2072.mpdu_start.sw_peer_id);
}

static bool ath12k_hal_rx_desc_mac_addr2_valid_qcc2072(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->u.qcc2072.mpdu_start.info4) &
			     RX_MPDU_START_INFO4_MAC_ADDR2_VALID;
}

static u8 *ath12k_hal_rx_desc_mpdu_start_addr2_qcc2072(struct hal_rx_desc *desc)
{
	return desc->u.qcc2072.mpdu_start.addr2;
}

static bool ath12k_hal_rx_desc_is_da_mcbc_qcc2072(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->u.qcc2072.msdu_end.info13) &
			     RX_MSDU_END_INFO13_MCAST_BCAST;
}

static bool ath12k_hal_rx_h_msdu_done_qcc2072(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.qcc2072.msdu_end.info14,
			       RX_MSDU_END_INFO14_MSDU_DONE);
}

static bool ath12k_hal_rx_h_l4_cksum_fail_qcc2072(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.qcc2072.msdu_end.info13,
			       RX_MSDU_END_INFO13_TCP_UDP_CKSUM_FAIL);
}

static bool ath12k_hal_rx_h_ip_cksum_fail_qcc2072(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.qcc2072.msdu_end.info13,
			       RX_MSDU_END_INFO13_IP_CKSUM_FAIL);
}

static bool ath12k_hal_rx_h_is_decrypted_qcc2072(struct hal_rx_desc *desc)
{
	return (le32_get_bits(desc->u.qcc2072.msdu_end.info14,
			      RX_MSDU_END_INFO14_DECRYPT_STATUS_CODE) ==
		RX_DESC_DECRYPT_STATUS_CODE_OK);
}

static u32 ath12k_hal_rx_h_mpdu_err_qcc2072(struct hal_rx_desc *desc)
{
	u32 info = __le32_to_cpu(desc->u.qcc2072.msdu_end.info13);
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

static void ath12k_hal_extract_rx_desc_data_qcc2072(struct hal_rx_desc_data *rx_desc_data,
						    struct hal_rx_desc *rx_desc,
						    struct hal_rx_desc *ldesc)
{
	rx_desc_data->is_first_msdu = ath12k_hal_rx_desc_get_first_msdu_qcc2072(ldesc);
	rx_desc_data->is_last_msdu = ath12k_hal_rx_desc_get_last_msdu_qcc2072(ldesc);
	rx_desc_data->l3_pad_bytes = ath12k_hal_rx_desc_get_l3_pad_bytes_qcc2072(ldesc);
	rx_desc_data->enctype = ath12k_hal_rx_desc_get_encrypt_type_qcc2072(rx_desc);
	rx_desc_data->decap_type = ath12k_hal_rx_desc_get_decap_type_qcc2072(rx_desc);
	rx_desc_data->mesh_ctrl_present =
				ath12k_hal_rx_desc_get_mesh_ctl_qcc2072(rx_desc);
	rx_desc_data->seq_ctl_valid =
				ath12k_hal_rx_desc_get_mpdu_seq_ctl_vld_qcc2072(rx_desc);
	rx_desc_data->fc_valid = ath12k_hal_rx_desc_get_mpdu_fc_valid_qcc2072(rx_desc);
	rx_desc_data->seq_no = ath12k_hal_rx_desc_get_mpdu_start_seq_no_qcc2072(rx_desc);
	rx_desc_data->msdu_len = ath12k_hal_rx_desc_get_msdu_len_qcc2072(ldesc);
	rx_desc_data->sgi = ath12k_hal_rx_desc_get_msdu_sgi_qcc2072(rx_desc);
	rx_desc_data->rate_mcs = ath12k_hal_rx_desc_get_msdu_rate_mcs_qcc2072(rx_desc);
	rx_desc_data->bw = ath12k_hal_rx_desc_get_msdu_rx_bw_qcc2072(rx_desc);
	rx_desc_data->phy_meta_data = ath12k_hal_rx_desc_get_msdu_freq_qcc2072(rx_desc);
	rx_desc_data->pkt_type = ath12k_hal_rx_desc_get_msdu_pkt_type_qcc2072(rx_desc);
	rx_desc_data->nss = hweight8(ath12k_hal_rx_desc_get_msdu_nss_qcc2072(rx_desc));
	rx_desc_data->tid = ath12k_hal_rx_desc_get_mpdu_tid_qcc2072(rx_desc);
	rx_desc_data->peer_id = ath12k_hal_rx_desc_get_mpdu_peer_id_qcc2072(rx_desc);
	rx_desc_data->addr2_present = ath12k_hal_rx_desc_mac_addr2_valid_qcc2072(rx_desc);
	rx_desc_data->addr2 = ath12k_hal_rx_desc_mpdu_start_addr2_qcc2072(rx_desc);
	rx_desc_data->is_mcbc = ath12k_hal_rx_desc_is_da_mcbc_qcc2072(rx_desc);
	rx_desc_data->msdu_done = ath12k_hal_rx_h_msdu_done_qcc2072(ldesc);
	rx_desc_data->l4_csum_fail = ath12k_hal_rx_h_l4_cksum_fail_qcc2072(rx_desc);
	rx_desc_data->ip_csum_fail = ath12k_hal_rx_h_ip_cksum_fail_qcc2072(rx_desc);
	rx_desc_data->is_decrypted = ath12k_hal_rx_h_is_decrypted_qcc2072(rx_desc);
	rx_desc_data->err_bitmap = ath12k_hal_rx_h_mpdu_err_qcc2072(rx_desc);
}

static int ath12k_hal_srng_create_config_qcc2072(struct ath12k_hal *hal)
{
	struct hal_srng_config *s;
	int ret;

	ret = ath12k_hal_srng_create_config_wcn7850(hal);
	if (ret)
		return ret;

	s = &hal->srng_config[HAL_REO_CMD];
	s->entry_size = (sizeof(struct hal_tlv_hdr) +
			 sizeof(struct hal_reo_get_queue_stats_qcc2072)) >> 2;

	s = &hal->srng_config[HAL_REO_STATUS];
	s->entry_size = (sizeof(struct hal_tlv_hdr) +
			 sizeof(struct hal_reo_get_queue_stats_status_qcc2072)) >> 2;

	return 0;
}

static u16 ath12k_hal_reo_status_dec_tlv_hdr_qcc2072(void *tlv, void **desc)
{
	struct hal_reo_get_queue_stats_status_qcc2072 *status_tlv;
	u16 tag;

	tag = ath12k_hal_decode_tlv32_hdr(tlv, (void **)&status_tlv);
	/*
	 * actual desc of REO status entry starts after tlv32_padding,
	 * see hal_reo_get_queue_stats_status_qcc2072
	 */
	*desc = &status_tlv->status;

	return tag;
}

const struct hal_ops hal_qcc2072_ops = {
	.create_srng_config = ath12k_hal_srng_create_config_qcc2072,
	.rx_desc_set_msdu_len = ath12k_hal_rx_desc_set_msdu_len_qcc2072,
	.rx_desc_get_dot11_hdr = ath12k_hal_rx_desc_get_dot11_hdr_qcc2072,
	.rx_desc_get_crypto_header = ath12k_hal_rx_desc_get_crypto_hdr_qcc2072,
	.rx_desc_copy_end_tlv = ath12k_hal_rx_desc_copy_end_tlv_qcc2072,
	.rx_desc_get_msdu_src_link_id = ath12k_hal_rx_desc_get_msdu_src_link_qcc2072,
	.extract_rx_desc_data = ath12k_hal_extract_rx_desc_data_qcc2072,
	.rx_desc_get_l3_pad_bytes = ath12k_hal_rx_desc_get_l3_pad_bytes_qcc2072,
	.rx_desc_get_mpdu_start_tag = ath12k_hal_rx_desc_get_mpdu_start_tag_qcc2072,
	.rx_desc_get_mpdu_ppdu_id = ath12k_hal_rx_desc_get_mpdu_ppdu_id_qcc2072,
	.rx_desc_get_msdu_payload = ath12k_hal_rx_desc_get_msdu_payload_qcc2072,
	.ce_dst_setup = ath12k_wifi7_hal_ce_dst_setup,
	.srng_src_hw_init = ath12k_wifi7_hal_srng_src_hw_init,
	.srng_dst_hw_init = ath12k_wifi7_hal_srng_dst_hw_init,
	.set_umac_srng_ptr_addr = ath12k_wifi7_hal_set_umac_srng_ptr_addr,
	.srng_update_shadow_config = ath12k_wifi7_hal_srng_update_shadow_config,
	.srng_get_ring_id = ath12k_wifi7_hal_srng_get_ring_id,
	.ce_get_desc_size = ath12k_wifi7_hal_ce_get_desc_size,
	.ce_src_set_desc = ath12k_wifi7_hal_ce_src_set_desc,
	.ce_dst_set_desc = ath12k_wifi7_hal_ce_dst_set_desc,
	.ce_dst_status_get_length = ath12k_wifi7_hal_ce_dst_status_get_length,
	.set_link_desc_addr = ath12k_wifi7_hal_set_link_desc_addr,
	.tx_set_dscp_tid_map = ath12k_wifi7_hal_tx_set_dscp_tid_map,
	.tx_configure_bank_register =
				ath12k_wifi7_hal_tx_configure_bank_register,
	.reoq_lut_addr_read_enable = ath12k_wifi7_hal_reoq_lut_addr_read_enable,
	.reoq_lut_set_max_peerid = ath12k_wifi7_hal_reoq_lut_set_max_peerid,
	.write_reoq_lut_addr = ath12k_wifi7_hal_write_reoq_lut_addr,
	.write_ml_reoq_lut_addr = ath12k_wifi7_hal_write_ml_reoq_lut_addr,
	.setup_link_idle_list = ath12k_wifi7_hal_setup_link_idle_list,
	.reo_init_cmd_ring = ath12k_wifi7_hal_reo_init_cmd_ring_tlv32,
	.reo_hw_setup = ath12k_wifi7_hal_reo_hw_setup,
	.rx_buf_addr_info_set = ath12k_wifi7_hal_rx_buf_addr_info_set,
	.rx_buf_addr_info_get = ath12k_wifi7_hal_rx_buf_addr_info_get,
	.cc_config = ath12k_wifi7_hal_cc_config,
	.get_idle_link_rbm = ath12k_wifi7_hal_get_idle_link_rbm,
	.rx_msdu_list_get = ath12k_wifi7_hal_rx_msdu_list_get,
	.rx_reo_ent_buf_paddr_get = ath12k_wifi7_hal_rx_reo_ent_buf_paddr_get,
	.reo_cmd_enc_tlv_hdr = ath12k_hal_encode_tlv32_hdr,
	.reo_status_dec_tlv_hdr = ath12k_hal_reo_status_dec_tlv_hdr_qcc2072,
};

u32 ath12k_hal_rx_desc_get_mpdu_start_offset_qcc2072(void)
{
	return offsetof(struct hal_rx_desc_qcc2072, mpdu_start_tag);
}

u32 ath12k_hal_rx_desc_get_msdu_end_offset_qcc2072(void)
{
	return offsetof(struct hal_rx_desc_qcc2072, msdu_end_tag);
}
