// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "hal_desc.h"
#include "hal_wcn7850.h"
#include "hw.h"
#include "hal.h"
#include "hal_tx.h"

static const struct hal_srng_config hw_srng_config_template[] = {
	/* TODO: max_rings can populated by querying HW capabilities */
	[HAL_REO_DST] = {
		.start_ring_id = HAL_SRNG_RING_ID_REO2SW1,
		.max_rings = 8,
		.entry_size = sizeof(struct hal_reo_dest_ring) >> 2,
		.mac_type = ATH12K_HAL_SRNG_UMAC,
		.ring_dir = HAL_SRNG_DIR_DST,
		.max_size = HAL_REO_REO2SW1_RING_BASE_MSB_RING_SIZE,
	},
	[HAL_REO_EXCEPTION] = {
		/* Designating REO2SW0 ring as exception ring.
		 * Any of theREO2SW rings can be used as exception ring.
		 */
		.start_ring_id = HAL_SRNG_RING_ID_REO2SW0,
		.max_rings = 1,
		.entry_size = sizeof(struct hal_reo_dest_ring) >> 2,
		.mac_type = ATH12K_HAL_SRNG_UMAC,
		.ring_dir = HAL_SRNG_DIR_DST,
		.max_size = HAL_REO_REO2SW0_RING_BASE_MSB_RING_SIZE,
	},
	[HAL_REO_REINJECT] = {
		.start_ring_id = HAL_SRNG_RING_ID_SW2REO,
		.max_rings = 4,
		.entry_size = sizeof(struct hal_reo_entrance_ring) >> 2,
		.mac_type = ATH12K_HAL_SRNG_UMAC,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_REO_SW2REO_RING_BASE_MSB_RING_SIZE,
	},
	[HAL_REO_CMD] = {
		.start_ring_id = HAL_SRNG_RING_ID_REO_CMD,
		.max_rings = 1,
		.entry_size = (sizeof(struct hal_tlv_64_hdr) +
			sizeof(struct hal_reo_get_queue_stats)) >> 2,
		.mac_type = ATH12K_HAL_SRNG_UMAC,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_REO_CMD_RING_BASE_MSB_RING_SIZE,
	},
	[HAL_REO_STATUS] = {
		.start_ring_id = HAL_SRNG_RING_ID_REO_STATUS,
		.max_rings = 1,
		.entry_size = (sizeof(struct hal_tlv_64_hdr) +
			sizeof(struct hal_reo_get_queue_stats_status)) >> 2,
		.mac_type = ATH12K_HAL_SRNG_UMAC,
		.ring_dir = HAL_SRNG_DIR_DST,
		.max_size = HAL_REO_STATUS_RING_BASE_MSB_RING_SIZE,
	},
	[HAL_TCL_DATA] = {
		.start_ring_id = HAL_SRNG_RING_ID_SW2TCL1,
		.max_rings = 6,
		.entry_size = sizeof(struct hal_tcl_data_cmd) >> 2,
		.mac_type = ATH12K_HAL_SRNG_UMAC,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_SW2TCL1_RING_BASE_MSB_RING_SIZE,
	},
	[HAL_TCL_CMD] = {
		.start_ring_id = HAL_SRNG_RING_ID_SW2TCL_CMD,
		.max_rings = 1,
		.entry_size = sizeof(struct hal_tcl_gse_cmd) >> 2,
		.mac_type = ATH12K_HAL_SRNG_UMAC,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_SW2TCL1_CMD_RING_BASE_MSB_RING_SIZE,
	},
	[HAL_TCL_STATUS] = {
		.start_ring_id = HAL_SRNG_RING_ID_TCL_STATUS,
		.max_rings = 1,
		.entry_size = (sizeof(struct hal_tlv_hdr) +
			     sizeof(struct hal_tcl_status_ring)) >> 2,
		.mac_type = ATH12K_HAL_SRNG_UMAC,
		.ring_dir = HAL_SRNG_DIR_DST,
		.max_size = HAL_TCL_STATUS_RING_BASE_MSB_RING_SIZE,
	},
	[HAL_CE_SRC] = {
		.start_ring_id = HAL_SRNG_RING_ID_CE0_SRC,
		.max_rings = 16,
		.entry_size = sizeof(struct hal_ce_srng_src_desc) >> 2,
		.mac_type = ATH12K_HAL_SRNG_UMAC,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_CE_SRC_RING_BASE_MSB_RING_SIZE,
	},
	[HAL_CE_DST] = {
		.start_ring_id = HAL_SRNG_RING_ID_CE0_DST,
		.max_rings = 16,
		.entry_size = sizeof(struct hal_ce_srng_dest_desc) >> 2,
		.mac_type = ATH12K_HAL_SRNG_UMAC,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_CE_DST_RING_BASE_MSB_RING_SIZE,
	},
	[HAL_CE_DST_STATUS] = {
		.start_ring_id = HAL_SRNG_RING_ID_CE0_DST_STATUS,
		.max_rings = 16,
		.entry_size = sizeof(struct hal_ce_srng_dst_status_desc) >> 2,
		.mac_type = ATH12K_HAL_SRNG_UMAC,
		.ring_dir = HAL_SRNG_DIR_DST,
		.max_size = HAL_CE_DST_STATUS_RING_BASE_MSB_RING_SIZE,
	},
	[HAL_WBM_IDLE_LINK] = {
		.start_ring_id = HAL_SRNG_RING_ID_WBM_IDLE_LINK,
		.max_rings = 1,
		.entry_size = sizeof(struct hal_wbm_link_desc) >> 2,
		.mac_type = ATH12K_HAL_SRNG_UMAC,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_WBM_IDLE_LINK_RING_BASE_MSB_RING_SIZE,
	},
	[HAL_SW2WBM_RELEASE] = {
		.start_ring_id = HAL_SRNG_RING_ID_WBM_SW0_RELEASE,
		.max_rings = 2,
		.entry_size = sizeof(struct hal_wbm_release_ring) >> 2,
		.mac_type = ATH12K_HAL_SRNG_UMAC,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_SW2WBM_RELEASE_RING_BASE_MSB_RING_SIZE,
	},
	[HAL_WBM2SW_RELEASE] = {
		.start_ring_id = HAL_SRNG_RING_ID_WBM2SW0_RELEASE,
		.max_rings = 8,
		.entry_size = sizeof(struct hal_wbm_release_ring) >> 2,
		.mac_type = ATH12K_HAL_SRNG_UMAC,
		.ring_dir = HAL_SRNG_DIR_DST,
		.max_size = HAL_WBM2SW_RELEASE_RING_BASE_MSB_RING_SIZE,
	},
	[HAL_RXDMA_BUF] = {
		.start_ring_id = HAL_SRNG_SW2RXDMA_BUF0,
		.max_rings = 1,
		.entry_size = sizeof(struct hal_wbm_buffer_ring) >> 2,
		.mac_type = ATH12K_HAL_SRNG_DMAC,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_RXDMA_RING_MAX_SIZE_BE,
	},
	[HAL_RXDMA_DST] = {
		.start_ring_id = HAL_SRNG_RING_ID_WMAC1_RXDMA2SW0,
		.max_rings = 0,
		.entry_size = 0,
		.mac_type = ATH12K_HAL_SRNG_PMAC,
		.ring_dir = HAL_SRNG_DIR_DST,
		.max_size = HAL_RXDMA_RING_MAX_SIZE_BE,
	},
	[HAL_RXDMA_MONITOR_BUF] = {},
	[HAL_RXDMA_MONITOR_STATUS] = {
		.start_ring_id = HAL_SRNG_RING_ID_WMAC1_SW2RXDMA1_STATBUF,
		.max_rings = 1,
		.entry_size = sizeof(struct hal_wbm_buffer_ring) >> 2,
		.mac_type = ATH12K_HAL_SRNG_PMAC,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_RXDMA_RING_MAX_SIZE_BE,
	},
	[HAL_RXDMA_MONITOR_DESC] = { 0, },
	[HAL_RXDMA_DIR_BUF] = {
		.start_ring_id = HAL_SRNG_RING_ID_RXDMA_DIR_BUF,
		.max_rings = 2,
		.entry_size = 8 >> 2, /* TODO: Define the struct */
		.mac_type = ATH12K_HAL_SRNG_PMAC,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_RXDMA_RING_MAX_SIZE_BE,
	},
	[HAL_PPE2TCL] = {},
	[HAL_PPE_RELEASE] = {},
	[HAL_TX_MONITOR_BUF] = {},
	[HAL_RXDMA_MONITOR_DST] = {},
	[HAL_TX_MONITOR_DST] = {}
};

const struct ath12k_hw_regs wcn7850_regs = {
	/* SW2TCL(x) R0 ring configuration address */
	.tcl1_ring_id = 0x00000908,
	.tcl1_ring_misc = 0x00000910,
	.tcl1_ring_tp_addr_lsb = 0x0000091c,
	.tcl1_ring_tp_addr_msb = 0x00000920,
	.tcl1_ring_consumer_int_setup_ix0 = 0x00000930,
	.tcl1_ring_consumer_int_setup_ix1 = 0x00000934,
	.tcl1_ring_msi1_base_lsb = 0x00000948,
	.tcl1_ring_msi1_base_msb = 0x0000094c,
	.tcl1_ring_msi1_data = 0x00000950,
	.tcl_ring_base_lsb = 0x00000b58,
	.tcl1_ring_base_lsb = 0x00000900,
	.tcl1_ring_base_msb = 0x00000904,
	.tcl2_ring_base_lsb = 0x00000978,

	/* TCL STATUS ring address */
	.tcl_status_ring_base_lsb = 0x00000d38,

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
	.wbm_sw1_release_ring_base_lsb = 0x00000284,
	.wbm0_release_ring_base_lsb = 0x00000e08,
	.wbm1_release_ring_base_lsb = 0x00000e80,

	/* PCIe base address */
	.pcie_qserdes_sysclk_en_sel = 0x01e0e0a8,
	.pcie_pcs_osc_dtct_config_base = 0x01e0f45c,

	/* PPE release ring address */
	.ppe_rel_ring_base = 0x0000043c,

	/* REO DEST ring address */
	.reo2_ring_base = 0x0000055c,
	.reo1_misc_ctrl_addr = 0x00000b7c,
	.reo1_sw_cookie_cfg0 = 0x00000050,
	.reo1_sw_cookie_cfg1 = 0x00000054,
	.reo1_qdesc_lut_base0 = 0x00000058,
	.reo1_qdesc_lut_base1 = 0x0000005c,
	.reo1_ring_base_lsb = 0x000004e4,
	.reo1_ring_base_msb = 0x000004e8,
	.reo1_ring_id = 0x000004ec,
	.reo1_ring_misc = 0x000004f4,
	.reo1_ring_hp_addr_lsb = 0x000004f8,
	.reo1_ring_hp_addr_msb = 0x000004fc,
	.reo1_ring_producer_int_setup = 0x00000508,
	.reo1_ring_msi1_base_lsb = 0x0000052C,
	.reo1_ring_msi1_base_msb = 0x00000530,
	.reo1_ring_msi1_data = 0x00000534,
	.reo1_aging_thres_ix0 = 0x00000b08,
	.reo1_aging_thres_ix1 = 0x00000b0c,
	.reo1_aging_thres_ix2 = 0x00000b10,
	.reo1_aging_thres_ix3 = 0x00000b14,

	/* REO Exception ring address */
	.reo2_sw0_ring_base = 0x000008a4,

	/* REO Reinject ring address */
	.sw2reo_ring_base = 0x00000304,
	.sw2reo1_ring_base = 0x0000037c,

	/* REO cmd ring address */
	.reo_cmd_ring_base = 0x0000028c,

	/* REO status ring address */
	.reo_status_ring_base = 0x00000a84,

	/* CE base address */
	.umac_ce0_src_reg_base = 0x01b80000,
	.umac_ce0_dest_reg_base = 0x01b81000,
	.umac_ce1_src_reg_base = 0x01b82000,
	.umac_ce1_dest_reg_base = 0x01b83000,

	.gcc_gcc_pcie_hot_rst = 0x1e40304,

	.qrtr_node_id = 0x1e03164,
};

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

int ath12k_hal_srng_create_config_wcn7850(struct ath12k_hal *hal)
{
	struct hal_srng_config *s;

	hal->srng_config = kmemdup(hw_srng_config_template,
				   sizeof(hw_srng_config_template),
				   GFP_KERNEL);
	if (!hal->srng_config)
		return -ENOMEM;

	s = &hal->srng_config[HAL_REO_DST];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO1_RING_BASE_LSB(hal);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO1_RING_HP;
	s->reg_size[0] = HAL_REO2_RING_BASE_LSB(hal) - HAL_REO1_RING_BASE_LSB(hal);
	s->reg_size[1] = HAL_REO2_RING_HP - HAL_REO1_RING_HP;

	s = &hal->srng_config[HAL_REO_EXCEPTION];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO_SW0_RING_BASE_LSB(hal);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO_SW0_RING_HP;

	s = &hal->srng_config[HAL_REO_REINJECT];
	s->max_rings = 1;
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_SW2REO_RING_BASE_LSB(hal);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_SW2REO_RING_HP;

	s = &hal->srng_config[HAL_REO_CMD];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO_CMD_RING_BASE_LSB(hal);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO_CMD_HP;

	s = &hal->srng_config[HAL_REO_STATUS];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO_STATUS_RING_BASE_LSB(hal);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO_STATUS_HP;

	s = &hal->srng_config[HAL_TCL_DATA];
	s->max_rings = 5;
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL1_RING_BASE_LSB(hal);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL1_RING_HP;
	s->reg_size[0] = HAL_TCL2_RING_BASE_LSB(hal) - HAL_TCL1_RING_BASE_LSB(hal);
	s->reg_size[1] = HAL_TCL2_RING_HP - HAL_TCL1_RING_HP;

	s = &hal->srng_config[HAL_TCL_CMD];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL_RING_BASE_LSB(hal);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL_RING_HP;

	s = &hal->srng_config[HAL_TCL_STATUS];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL_STATUS_RING_BASE_LSB(hal);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL_STATUS_RING_HP;

	s = &hal->srng_config[HAL_CE_SRC];
	s->max_rings = 12;
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_CE0_SRC_REG(hal) + HAL_CE_DST_RING_BASE_LSB;
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_CE0_SRC_REG(hal) + HAL_CE_DST_RING_HP;
	s->reg_size[0] = HAL_SEQ_WCSS_UMAC_CE1_SRC_REG(hal) -
		HAL_SEQ_WCSS_UMAC_CE0_SRC_REG(hal);
	s->reg_size[1] = HAL_SEQ_WCSS_UMAC_CE1_SRC_REG(hal) -
		HAL_SEQ_WCSS_UMAC_CE0_SRC_REG(hal);

	s = &hal->srng_config[HAL_CE_DST];
	s->max_rings = 12;
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_CE0_DST_REG(hal) + HAL_CE_DST_RING_BASE_LSB;
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_CE0_DST_REG(hal) + HAL_CE_DST_RING_HP;
	s->reg_size[0] = HAL_SEQ_WCSS_UMAC_CE1_DST_REG(hal) -
		HAL_SEQ_WCSS_UMAC_CE0_DST_REG(hal);
	s->reg_size[1] = HAL_SEQ_WCSS_UMAC_CE1_DST_REG(hal) -
		HAL_SEQ_WCSS_UMAC_CE0_DST_REG(hal);

	s = &hal->srng_config[HAL_CE_DST_STATUS];
	s->max_rings = 12;
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_CE0_DST_REG(hal) +
		HAL_CE_DST_STATUS_RING_BASE_LSB;
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_CE0_DST_REG(hal) + HAL_CE_DST_STATUS_RING_HP;
	s->reg_size[0] = HAL_SEQ_WCSS_UMAC_CE1_DST_REG(hal) -
		HAL_SEQ_WCSS_UMAC_CE0_DST_REG(hal);
	s->reg_size[1] = HAL_SEQ_WCSS_UMAC_CE1_DST_REG(hal) -
		HAL_SEQ_WCSS_UMAC_CE0_DST_REG(hal);

	s = &hal->srng_config[HAL_WBM_IDLE_LINK];
	s->reg_start[0] =
		HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM_IDLE_LINK_RING_BASE_LSB(hal);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM_IDLE_LINK_RING_HP;

	s = &hal->srng_config[HAL_SW2WBM_RELEASE];
	s->max_rings = 1;
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_WBM_REG +
		HAL_WBM_SW_RELEASE_RING_BASE_LSB(hal);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM_SW_RELEASE_RING_HP;

	s = &hal->srng_config[HAL_WBM2SW_RELEASE];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM0_RELEASE_RING_BASE_LSB(hal);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM0_RELEASE_RING_HP;
	s->reg_size[0] = HAL_WBM1_RELEASE_RING_BASE_LSB(hal) -
		HAL_WBM0_RELEASE_RING_BASE_LSB(hal);
	s->reg_size[1] = HAL_WBM1_RELEASE_RING_HP - HAL_WBM0_RELEASE_RING_HP;

	s = &hal->srng_config[HAL_RXDMA_BUF];
	s->max_rings = 2;
	s->mac_type = ATH12K_HAL_SRNG_PMAC;

	s = &hal->srng_config[HAL_RXDMA_DST];
	s->max_rings = 1;
	s->entry_size = sizeof(struct hal_reo_entrance_ring) >> 2;

	/* below rings are not used */
	s = &hal->srng_config[HAL_RXDMA_DIR_BUF];
	s->max_rings = 0;

	s = &hal->srng_config[HAL_PPE2TCL];
	s->max_rings = 0;

	s = &hal->srng_config[HAL_PPE_RELEASE];
	s->max_rings = 0;

	s = &hal->srng_config[HAL_TX_MONITOR_BUF];
	s->max_rings = 0;

	s = &hal->srng_config[HAL_TX_MONITOR_DST];
	s->max_rings = 0;

	s = &hal->srng_config[HAL_PPE2TCL];
	s->max_rings = 0;

	return 0;
}

const struct ath12k_hal_tcl_to_wbm_rbm_map
ath12k_hal_tcl_to_wbm_rbm_map_wcn7850[DP_TCL_NUM_RING_MAX] = {
	{
		.wbm_ring_num = 0,
		.rbm_id = HAL_RX_BUF_RBM_SW0_BM,
	},
	{
		.wbm_ring_num = 2,
		.rbm_id = HAL_RX_BUF_RBM_SW2_BM,
	},
	{
		.wbm_ring_num = 4,
		.rbm_id = HAL_RX_BUF_RBM_SW4_BM,
	},
};

const struct ath12k_hw_hal_params ath12k_hw_hal_params_wcn7850 = {
	.rx_buf_rbm = HAL_RX_BUF_RBM_SW1_BM,
	.wbm2sw_cc_enable = HAL_WBM_SW_COOKIE_CONV_CFG_WBM2SW0_EN |
			    HAL_WBM_SW_COOKIE_CONV_CFG_WBM2SW2_EN |
			    HAL_WBM_SW_COOKIE_CONV_CFG_WBM2SW3_EN |
			    HAL_WBM_SW_COOKIE_CONV_CFG_WBM2SW4_EN,
};

const struct hal_ops hal_wcn7850_ops = {
	.create_srng_config = ath12k_hal_srng_create_config_wcn7850,
	.rx_desc_set_msdu_len = ath12k_hal_rx_desc_set_msdu_len_wcn7850,
	.rx_desc_get_dot11_hdr = ath12k_hal_rx_desc_get_dot11_hdr_wcn7850,
	.rx_desc_get_crypto_header = ath12k_hal_rx_desc_get_crypto_hdr_wcn7850,
	.rx_desc_copy_end_tlv = ath12k_hal_rx_desc_copy_end_tlv_wcn7850,
	.rx_desc_get_msdu_src_link_id = ath12k_hal_rx_desc_get_msdu_src_link_wcn7850,
	.extract_rx_desc_data = ath12k_hal_extract_rx_desc_data_wcn7850,
	.rx_desc_get_l3_pad_bytes = ath12k_hal_rx_desc_get_l3_pad_bytes_wcn7850,
	.rx_desc_get_mpdu_start_tag = ath12k_hal_rx_desc_get_mpdu_start_tag_wcn7850,
	.rx_desc_get_mpdu_ppdu_id = ath12k_hal_rx_desc_get_mpdu_ppdu_id_wcn7850,
	.rx_desc_get_msdu_payload = ath12k_hal_rx_desc_get_msdu_payload_wcn7850,
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
	.reo_init_cmd_ring = ath12k_wifi7_hal_reo_init_cmd_ring_tlv64,
	.reo_shared_qaddr_cache_clear = ath12k_wifi7_hal_reo_shared_qaddr_cache_clear,
	.reo_hw_setup = ath12k_wifi7_hal_reo_hw_setup,
	.rx_buf_addr_info_set = ath12k_wifi7_hal_rx_buf_addr_info_set,
	.rx_buf_addr_info_get = ath12k_wifi7_hal_rx_buf_addr_info_get,
	.cc_config = ath12k_wifi7_hal_cc_config,
	.get_idle_link_rbm = ath12k_wifi7_hal_get_idle_link_rbm,
	.rx_msdu_list_get = ath12k_wifi7_hal_rx_msdu_list_get,
	.rx_reo_ent_buf_paddr_get = ath12k_wifi7_hal_rx_reo_ent_buf_paddr_get,
	.reo_cmd_enc_tlv_hdr = ath12k_hal_encode_tlv64_hdr,
	.reo_status_dec_tlv_hdr = ath12k_hal_decode_tlv64_hdr,
};
