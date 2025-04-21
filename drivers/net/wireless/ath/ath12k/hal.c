// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/dma-mapping.h>
#include "hal_tx.h"
#include "hal_rx.h"
#include "debug.h"
#include "hal_desc.h"
#include "hif.h"

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
	[HAL_RXDMA_MONITOR_BUF] = {
		.start_ring_id = HAL_SRNG_SW2RXMON_BUF0,
		.max_rings = 1,
		.entry_size = sizeof(struct hal_mon_buf_ring) >> 2,
		.mac_type = ATH12K_HAL_SRNG_PMAC,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_RXDMA_RING_MAX_SIZE_BE,
	},
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
	[HAL_PPE2TCL] = {
		.start_ring_id = HAL_SRNG_RING_ID_PPE2TCL1,
		.max_rings = 1,
		.entry_size = sizeof(struct hal_tcl_entrance_from_ppe_ring) >> 2,
		.mac_type = ATH12K_HAL_SRNG_PMAC,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_SW2TCL1_RING_BASE_MSB_RING_SIZE,
	},
	[HAL_PPE_RELEASE] = {
		.start_ring_id = HAL_SRNG_RING_ID_WBM_PPE_RELEASE,
		.max_rings = 1,
		.entry_size = sizeof(struct hal_wbm_release_ring) >> 2,
		.mac_type = ATH12K_HAL_SRNG_PMAC,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_WBM2PPE_RELEASE_RING_BASE_MSB_RING_SIZE,
	},
	[HAL_TX_MONITOR_BUF] = {
		.start_ring_id = HAL_SRNG_RING_ID_WMAC1_SW2TXMON_BUF0,
		.max_rings = 1,
		.entry_size = sizeof(struct hal_mon_buf_ring) >> 2,
		.mac_type = ATH12K_HAL_SRNG_PMAC,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_RXDMA_RING_MAX_SIZE_BE,
	},
	[HAL_RXDMA_MONITOR_DST] = {
		.start_ring_id = HAL_SRNG_RING_ID_WMAC1_SW2RXMON_BUF0,
		.max_rings = 1,
		.entry_size = sizeof(struct hal_mon_dest_desc) >> 2,
		.mac_type = ATH12K_HAL_SRNG_PMAC,
		.ring_dir = HAL_SRNG_DIR_DST,
		.max_size = HAL_RXDMA_RING_MAX_SIZE_BE,
	},
	[HAL_TX_MONITOR_DST] = {
		.start_ring_id = HAL_SRNG_RING_ID_WMAC1_TXMON2SW0_BUF0,
		.max_rings = 1,
		.entry_size = sizeof(struct hal_mon_dest_desc) >> 2,
		.mac_type = ATH12K_HAL_SRNG_PMAC,
		.ring_dir = HAL_SRNG_DIR_DST,
		.max_size = HAL_RXDMA_RING_MAX_SIZE_BE,
	}
};

static const struct ath12k_hal_tcl_to_wbm_rbm_map
ath12k_hal_qcn9274_tcl_to_wbm_rbm_map[DP_TCL_NUM_RING_MAX] = {
	{
		.wbm_ring_num = 0,
		.rbm_id = HAL_RX_BUF_RBM_SW0_BM,
	},
	{
		.wbm_ring_num = 1,
		.rbm_id = HAL_RX_BUF_RBM_SW1_BM,
	},
	{
		.wbm_ring_num = 2,
		.rbm_id = HAL_RX_BUF_RBM_SW2_BM,
	},
	{
		.wbm_ring_num = 4,
		.rbm_id = HAL_RX_BUF_RBM_SW4_BM,
	}
};

static const struct ath12k_hal_tcl_to_wbm_rbm_map
ath12k_hal_wcn7850_tcl_to_wbm_rbm_map[DP_TCL_NUM_RING_MAX] = {
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

static unsigned int ath12k_hal_reo1_ring_id_offset(struct ath12k_base *ab)
{
	return HAL_REO1_RING_ID(ab) - HAL_REO1_RING_BASE_LSB(ab);
}

static unsigned int ath12k_hal_reo1_ring_msi1_base_lsb_offset(struct ath12k_base *ab)
{
	return HAL_REO1_RING_MSI1_BASE_LSB(ab) - HAL_REO1_RING_BASE_LSB(ab);
}

static unsigned int ath12k_hal_reo1_ring_msi1_base_msb_offset(struct ath12k_base *ab)
{
	return HAL_REO1_RING_MSI1_BASE_MSB(ab) - HAL_REO1_RING_BASE_LSB(ab);
}

static unsigned int ath12k_hal_reo1_ring_msi1_data_offset(struct ath12k_base *ab)
{
	return HAL_REO1_RING_MSI1_DATA(ab) - HAL_REO1_RING_BASE_LSB(ab);
}

static unsigned int ath12k_hal_reo1_ring_base_msb_offset(struct ath12k_base *ab)
{
	return HAL_REO1_RING_BASE_MSB(ab) - HAL_REO1_RING_BASE_LSB(ab);
}

static unsigned int ath12k_hal_reo1_ring_producer_int_setup_offset(struct ath12k_base *ab)
{
	return HAL_REO1_RING_PRODUCER_INT_SETUP(ab) - HAL_REO1_RING_BASE_LSB(ab);
}

static unsigned int ath12k_hal_reo1_ring_hp_addr_lsb_offset(struct ath12k_base *ab)
{
	return HAL_REO1_RING_HP_ADDR_LSB(ab) - HAL_REO1_RING_BASE_LSB(ab);
}

static unsigned int ath12k_hal_reo1_ring_hp_addr_msb_offset(struct ath12k_base *ab)
{
	return HAL_REO1_RING_HP_ADDR_MSB(ab) - HAL_REO1_RING_BASE_LSB(ab);
}

static unsigned int ath12k_hal_reo1_ring_misc_offset(struct ath12k_base *ab)
{
	return HAL_REO1_RING_MISC(ab) - HAL_REO1_RING_BASE_LSB(ab);
}

static bool ath12k_hw_qcn9274_rx_desc_get_first_msdu(struct hal_rx_desc *desc)
{
	return !!le16_get_bits(desc->u.qcn9274.msdu_end.info5,
			       RX_MSDU_END_INFO5_FIRST_MSDU);
}

static bool ath12k_hw_qcn9274_rx_desc_get_last_msdu(struct hal_rx_desc *desc)
{
	return !!le16_get_bits(desc->u.qcn9274.msdu_end.info5,
			       RX_MSDU_END_INFO5_LAST_MSDU);
}

static u8 ath12k_hw_qcn9274_rx_desc_get_l3_pad_bytes(struct hal_rx_desc *desc)
{
	return le16_get_bits(desc->u.qcn9274.msdu_end.info5,
			     RX_MSDU_END_INFO5_L3_HDR_PADDING);
}

static bool ath12k_hw_qcn9274_rx_desc_encrypt_valid(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.qcn9274.mpdu_start.info4,
			       RX_MPDU_START_INFO4_ENCRYPT_INFO_VALID);
}

static u32 ath12k_hw_qcn9274_rx_desc_get_encrypt_type(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274.mpdu_start.info2,
			     RX_MPDU_START_INFO2_ENC_TYPE);
}

static u8 ath12k_hw_qcn9274_rx_desc_get_decap_type(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274.msdu_end.info11,
			     RX_MSDU_END_INFO11_DECAP_FORMAT);
}

static u8 ath12k_hw_qcn9274_rx_desc_get_mesh_ctl(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274.msdu_end.info11,
			     RX_MSDU_END_INFO11_MESH_CTRL_PRESENT);
}

static bool ath12k_hw_qcn9274_rx_desc_get_mpdu_seq_ctl_vld(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.qcn9274.mpdu_start.info4,
			       RX_MPDU_START_INFO4_MPDU_SEQ_CTRL_VALID);
}

static bool ath12k_hw_qcn9274_rx_desc_get_mpdu_fc_valid(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.qcn9274.mpdu_start.info4,
			       RX_MPDU_START_INFO4_MPDU_FCTRL_VALID);
}

static u16 ath12k_hw_qcn9274_rx_desc_get_mpdu_start_seq_no(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274.mpdu_start.info4,
			     RX_MPDU_START_INFO4_MPDU_SEQ_NUM);
}

static u16 ath12k_hw_qcn9274_rx_desc_get_msdu_len(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274.msdu_end.info10,
			     RX_MSDU_END_INFO10_MSDU_LENGTH);
}

static u8 ath12k_hw_qcn9274_rx_desc_get_msdu_sgi(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274.msdu_end.info12,
			     RX_MSDU_END_INFO12_SGI);
}

static u8 ath12k_hw_qcn9274_rx_desc_get_msdu_rate_mcs(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274.msdu_end.info12,
			     RX_MSDU_END_INFO12_RATE_MCS);
}

static u8 ath12k_hw_qcn9274_rx_desc_get_msdu_rx_bw(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274.msdu_end.info12,
			     RX_MSDU_END_INFO12_RECV_BW);
}

static u32 ath12k_hw_qcn9274_rx_desc_get_msdu_freq(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->u.qcn9274.msdu_end.phy_meta_data);
}

static u8 ath12k_hw_qcn9274_rx_desc_get_msdu_pkt_type(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274.msdu_end.info12,
			     RX_MSDU_END_INFO12_PKT_TYPE);
}

static u8 ath12k_hw_qcn9274_rx_desc_get_msdu_nss(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274.msdu_end.info12,
			     RX_MSDU_END_INFO12_MIMO_SS_BITMAP);
}

static u8 ath12k_hw_qcn9274_rx_desc_get_mpdu_tid(struct hal_rx_desc *desc)
{
	return le16_get_bits(desc->u.qcn9274.msdu_end.info5,
			    RX_MSDU_END_INFO5_TID);
}

static u16 ath12k_hw_qcn9274_rx_desc_get_mpdu_peer_id(struct hal_rx_desc *desc)
{
	return __le16_to_cpu(desc->u.qcn9274.mpdu_start.sw_peer_id);
}

static void ath12k_hw_qcn9274_rx_desc_copy_end_tlv(struct hal_rx_desc *fdesc,
						   struct hal_rx_desc *ldesc)
{
	memcpy(&fdesc->u.qcn9274.msdu_end, &ldesc->u.qcn9274.msdu_end,
	       sizeof(struct rx_msdu_end_qcn9274));
}

static u32 ath12k_hw_qcn9274_rx_desc_get_mpdu_ppdu_id(struct hal_rx_desc *desc)
{
	return __le16_to_cpu(desc->u.qcn9274.mpdu_start.phy_ppdu_id);
}

static void ath12k_hw_qcn9274_rx_desc_set_msdu_len(struct hal_rx_desc *desc, u16 len)
{
	u32 info = __le32_to_cpu(desc->u.qcn9274.msdu_end.info10);

	info &= ~RX_MSDU_END_INFO10_MSDU_LENGTH;
	info |= u32_encode_bits(len, RX_MSDU_END_INFO10_MSDU_LENGTH);

	desc->u.qcn9274.msdu_end.info10 = __cpu_to_le32(info);
}

static u8 *ath12k_hw_qcn9274_rx_desc_get_msdu_payload(struct hal_rx_desc *desc)
{
	return &desc->u.qcn9274.msdu_payload[0];
}

static u32 ath12k_hw_qcn9274_rx_desc_get_mpdu_start_offset(void)
{
	return offsetof(struct hal_rx_desc_qcn9274, mpdu_start);
}

static u32 ath12k_hw_qcn9274_rx_desc_get_msdu_end_offset(void)
{
	return offsetof(struct hal_rx_desc_qcn9274, msdu_end);
}

static bool ath12k_hw_qcn9274_rx_desc_mac_addr2_valid(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->u.qcn9274.mpdu_start.info4) &
	       RX_MPDU_START_INFO4_MAC_ADDR2_VALID;
}

static u8 *ath12k_hw_qcn9274_rx_desc_mpdu_start_addr2(struct hal_rx_desc *desc)
{
	return desc->u.qcn9274.mpdu_start.addr2;
}

static bool ath12k_hw_qcn9274_rx_desc_is_da_mcbc(struct hal_rx_desc *desc)
{
	return __le16_to_cpu(desc->u.qcn9274.msdu_end.info5) &
	       RX_MSDU_END_INFO5_DA_IS_MCBC;
}

static void ath12k_hw_qcn9274_rx_desc_get_dot11_hdr(struct hal_rx_desc *desc,
						    struct ieee80211_hdr *hdr)
{
	hdr->frame_control = desc->u.qcn9274.mpdu_start.frame_ctrl;
	hdr->duration_id = desc->u.qcn9274.mpdu_start.duration;
	ether_addr_copy(hdr->addr1, desc->u.qcn9274.mpdu_start.addr1);
	ether_addr_copy(hdr->addr2, desc->u.qcn9274.mpdu_start.addr2);
	ether_addr_copy(hdr->addr3, desc->u.qcn9274.mpdu_start.addr3);
	if (__le32_to_cpu(desc->u.qcn9274.mpdu_start.info4) &
			RX_MPDU_START_INFO4_MAC_ADDR4_VALID) {
		ether_addr_copy(hdr->addr4, desc->u.qcn9274.mpdu_start.addr4);
	}
	hdr->seq_ctrl = desc->u.qcn9274.mpdu_start.seq_ctrl;
}

static void ath12k_hw_qcn9274_rx_desc_get_crypto_hdr(struct hal_rx_desc *desc,
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
			HAL_RX_MPDU_INFO_PN_GET_BYTE2(desc->u.qcn9274.mpdu_start.pn[0]);
		crypto_hdr[1] = 0;
		crypto_hdr[2] =
			HAL_RX_MPDU_INFO_PN_GET_BYTE1(desc->u.qcn9274.mpdu_start.pn[0]);
		break;
	case HAL_ENCRYPT_TYPE_CCMP_128:
	case HAL_ENCRYPT_TYPE_CCMP_256:
	case HAL_ENCRYPT_TYPE_GCMP_128:
	case HAL_ENCRYPT_TYPE_AES_GCMP_256:
		crypto_hdr[0] =
			HAL_RX_MPDU_INFO_PN_GET_BYTE1(desc->u.qcn9274.mpdu_start.pn[0]);
		crypto_hdr[1] =
			HAL_RX_MPDU_INFO_PN_GET_BYTE2(desc->u.qcn9274.mpdu_start.pn[0]);
		crypto_hdr[2] = 0;
		break;
	case HAL_ENCRYPT_TYPE_WEP_40:
	case HAL_ENCRYPT_TYPE_WEP_104:
	case HAL_ENCRYPT_TYPE_WEP_128:
	case HAL_ENCRYPT_TYPE_WAPI_GCM_SM4:
	case HAL_ENCRYPT_TYPE_WAPI:
		return;
	}
	key_id = le32_get_bits(desc->u.qcn9274.mpdu_start.info5,
			       RX_MPDU_START_INFO5_KEY_ID);
	crypto_hdr[3] = 0x20 | (key_id << 6);
	crypto_hdr[4] = HAL_RX_MPDU_INFO_PN_GET_BYTE3(desc->u.qcn9274.mpdu_start.pn[0]);
	crypto_hdr[5] = HAL_RX_MPDU_INFO_PN_GET_BYTE4(desc->u.qcn9274.mpdu_start.pn[0]);
	crypto_hdr[6] = HAL_RX_MPDU_INFO_PN_GET_BYTE1(desc->u.qcn9274.mpdu_start.pn[1]);
	crypto_hdr[7] = HAL_RX_MPDU_INFO_PN_GET_BYTE2(desc->u.qcn9274.mpdu_start.pn[1]);
}

static int ath12k_hal_srng_create_config_qcn9274(struct ath12k_base *ab)
{
	struct ath12k_hal *hal = &ab->hal;
	struct hal_srng_config *s;

	hal->srng_config = kmemdup(hw_srng_config_template,
				   sizeof(hw_srng_config_template),
				   GFP_KERNEL);
	if (!hal->srng_config)
		return -ENOMEM;

	s = &hal->srng_config[HAL_REO_DST];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO1_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO1_RING_HP;
	s->reg_size[0] = HAL_REO2_RING_BASE_LSB(ab) - HAL_REO1_RING_BASE_LSB(ab);
	s->reg_size[1] = HAL_REO2_RING_HP - HAL_REO1_RING_HP;

	s = &hal->srng_config[HAL_REO_EXCEPTION];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO_SW0_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO_SW0_RING_HP;

	s = &hal->srng_config[HAL_REO_REINJECT];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_SW2REO_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_SW2REO_RING_HP;
	s->reg_size[0] = HAL_SW2REO1_RING_BASE_LSB(ab) - HAL_SW2REO_RING_BASE_LSB(ab);
	s->reg_size[1] = HAL_SW2REO1_RING_HP - HAL_SW2REO_RING_HP;

	s = &hal->srng_config[HAL_REO_CMD];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO_CMD_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO_CMD_HP;

	s = &hal->srng_config[HAL_REO_STATUS];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO_STATUS_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO_STATUS_HP;

	s = &hal->srng_config[HAL_TCL_DATA];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL1_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL1_RING_HP;
	s->reg_size[0] = HAL_TCL2_RING_BASE_LSB(ab) - HAL_TCL1_RING_BASE_LSB(ab);
	s->reg_size[1] = HAL_TCL2_RING_HP - HAL_TCL1_RING_HP;

	s = &hal->srng_config[HAL_TCL_CMD];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL_RING_HP;

	s = &hal->srng_config[HAL_TCL_STATUS];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL_STATUS_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL_STATUS_RING_HP;

	s = &hal->srng_config[HAL_CE_SRC];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_CE0_SRC_REG(ab) + HAL_CE_DST_RING_BASE_LSB;
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_CE0_SRC_REG(ab) + HAL_CE_DST_RING_HP;
	s->reg_size[0] = HAL_SEQ_WCSS_UMAC_CE1_SRC_REG(ab) -
		HAL_SEQ_WCSS_UMAC_CE0_SRC_REG(ab);
	s->reg_size[1] = HAL_SEQ_WCSS_UMAC_CE1_SRC_REG(ab) -
		HAL_SEQ_WCSS_UMAC_CE0_SRC_REG(ab);

	s = &hal->srng_config[HAL_CE_DST];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_CE0_DST_REG(ab) + HAL_CE_DST_RING_BASE_LSB;
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_CE0_DST_REG(ab) + HAL_CE_DST_RING_HP;
	s->reg_size[0] = HAL_SEQ_WCSS_UMAC_CE1_DST_REG(ab) -
		HAL_SEQ_WCSS_UMAC_CE0_DST_REG(ab);
	s->reg_size[1] = HAL_SEQ_WCSS_UMAC_CE1_DST_REG(ab) -
		HAL_SEQ_WCSS_UMAC_CE0_DST_REG(ab);

	s = &hal->srng_config[HAL_CE_DST_STATUS];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_CE0_DST_REG(ab) +
		HAL_CE_DST_STATUS_RING_BASE_LSB;
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_CE0_DST_REG(ab) + HAL_CE_DST_STATUS_RING_HP;
	s->reg_size[0] = HAL_SEQ_WCSS_UMAC_CE1_DST_REG(ab) -
		HAL_SEQ_WCSS_UMAC_CE0_DST_REG(ab);
	s->reg_size[1] = HAL_SEQ_WCSS_UMAC_CE1_DST_REG(ab) -
		HAL_SEQ_WCSS_UMAC_CE0_DST_REG(ab);

	s = &hal->srng_config[HAL_WBM_IDLE_LINK];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM_IDLE_LINK_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM_IDLE_LINK_RING_HP;

	s = &hal->srng_config[HAL_SW2WBM_RELEASE];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_WBM_REG +
		HAL_WBM_SW_RELEASE_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM_SW_RELEASE_RING_HP;
	s->reg_size[0] = HAL_WBM_SW1_RELEASE_RING_BASE_LSB(ab) -
			 HAL_WBM_SW_RELEASE_RING_BASE_LSB(ab);
	s->reg_size[1] = HAL_WBM_SW1_RELEASE_RING_HP - HAL_WBM_SW_RELEASE_RING_HP;

	s = &hal->srng_config[HAL_WBM2SW_RELEASE];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM0_RELEASE_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM0_RELEASE_RING_HP;
	s->reg_size[0] = HAL_WBM1_RELEASE_RING_BASE_LSB(ab) -
		HAL_WBM0_RELEASE_RING_BASE_LSB(ab);
	s->reg_size[1] = HAL_WBM1_RELEASE_RING_HP - HAL_WBM0_RELEASE_RING_HP;

	/* Some LMAC rings are not accessed from the host:
	 * RXDMA_BUG, RXDMA_DST, RXDMA_MONITOR_BUF, RXDMA_MONITOR_STATUS,
	 * RXDMA_MONITOR_DST, RXDMA_MONITOR_DESC, RXDMA_DIR_BUF_SRC,
	 * RXDMA_RX_MONITOR_BUF, TX_MONITOR_BUF, TX_MONITOR_DST, SW2RXDMA
	 */
	s = &hal->srng_config[HAL_PPE2TCL];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL_PPE2TCL1_RING_BASE_LSB;
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL_PPE2TCL1_RING_HP;

	s = &hal->srng_config[HAL_PPE_RELEASE];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_WBM_REG +
				HAL_WBM_PPE_RELEASE_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM_PPE_RELEASE_RING_HP;

	return 0;
}

static u16 ath12k_hal_qcn9274_rx_mpdu_start_wmask_get(void)
{
	return QCN9274_MPDU_START_WMASK;
}

static u32 ath12k_hal_qcn9274_rx_msdu_end_wmask_get(void)
{
	return QCN9274_MSDU_END_WMASK;
}

static const struct hal_rx_ops *ath12k_hal_qcn9274_get_hal_rx_compact_ops(void)
{
	return &hal_rx_qcn9274_compact_ops;
}

static bool ath12k_hw_qcn9274_dp_rx_h_msdu_done(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.qcn9274.msdu_end.info14,
			       RX_MSDU_END_INFO14_MSDU_DONE);
}

static bool ath12k_hw_qcn9274_dp_rx_h_l4_cksum_fail(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.qcn9274.msdu_end.info13,
			       RX_MSDU_END_INFO13_TCP_UDP_CKSUM_FAIL);
}

static bool ath12k_hw_qcn9274_dp_rx_h_ip_cksum_fail(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.qcn9274.msdu_end.info13,
			       RX_MSDU_END_INFO13_IP_CKSUM_FAIL);
}

static bool ath12k_hw_qcn9274_dp_rx_h_is_decrypted(struct hal_rx_desc *desc)
{
	return (le32_get_bits(desc->u.qcn9274.msdu_end.info14,
			      RX_MSDU_END_INFO14_DECRYPT_STATUS_CODE) ==
			      RX_DESC_DECRYPT_STATUS_CODE_OK);
}

static u32 ath12k_hw_qcn9274_dp_rx_h_mpdu_err(struct hal_rx_desc *desc)
{
	u32 info = __le32_to_cpu(desc->u.qcn9274.msdu_end.info13);
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

static u32 ath12k_hw_qcn9274_get_rx_desc_size(void)
{
	return sizeof(struct hal_rx_desc_qcn9274);
}

static u8 ath12k_hw_qcn9274_rx_desc_get_msdu_src_link(struct hal_rx_desc *desc)
{
	return 0;
}

const struct hal_rx_ops hal_rx_qcn9274_ops = {
	.rx_desc_get_first_msdu = ath12k_hw_qcn9274_rx_desc_get_first_msdu,
	.rx_desc_get_last_msdu = ath12k_hw_qcn9274_rx_desc_get_last_msdu,
	.rx_desc_get_l3_pad_bytes = ath12k_hw_qcn9274_rx_desc_get_l3_pad_bytes,
	.rx_desc_encrypt_valid = ath12k_hw_qcn9274_rx_desc_encrypt_valid,
	.rx_desc_get_encrypt_type = ath12k_hw_qcn9274_rx_desc_get_encrypt_type,
	.rx_desc_get_decap_type = ath12k_hw_qcn9274_rx_desc_get_decap_type,
	.rx_desc_get_mesh_ctl = ath12k_hw_qcn9274_rx_desc_get_mesh_ctl,
	.rx_desc_get_mpdu_seq_ctl_vld = ath12k_hw_qcn9274_rx_desc_get_mpdu_seq_ctl_vld,
	.rx_desc_get_mpdu_fc_valid = ath12k_hw_qcn9274_rx_desc_get_mpdu_fc_valid,
	.rx_desc_get_mpdu_start_seq_no = ath12k_hw_qcn9274_rx_desc_get_mpdu_start_seq_no,
	.rx_desc_get_msdu_len = ath12k_hw_qcn9274_rx_desc_get_msdu_len,
	.rx_desc_get_msdu_sgi = ath12k_hw_qcn9274_rx_desc_get_msdu_sgi,
	.rx_desc_get_msdu_rate_mcs = ath12k_hw_qcn9274_rx_desc_get_msdu_rate_mcs,
	.rx_desc_get_msdu_rx_bw = ath12k_hw_qcn9274_rx_desc_get_msdu_rx_bw,
	.rx_desc_get_msdu_freq = ath12k_hw_qcn9274_rx_desc_get_msdu_freq,
	.rx_desc_get_msdu_pkt_type = ath12k_hw_qcn9274_rx_desc_get_msdu_pkt_type,
	.rx_desc_get_msdu_nss = ath12k_hw_qcn9274_rx_desc_get_msdu_nss,
	.rx_desc_get_mpdu_tid = ath12k_hw_qcn9274_rx_desc_get_mpdu_tid,
	.rx_desc_get_mpdu_peer_id = ath12k_hw_qcn9274_rx_desc_get_mpdu_peer_id,
	.rx_desc_copy_end_tlv = ath12k_hw_qcn9274_rx_desc_copy_end_tlv,
	.rx_desc_get_mpdu_ppdu_id = ath12k_hw_qcn9274_rx_desc_get_mpdu_ppdu_id,
	.rx_desc_set_msdu_len = ath12k_hw_qcn9274_rx_desc_set_msdu_len,
	.rx_desc_get_msdu_payload = ath12k_hw_qcn9274_rx_desc_get_msdu_payload,
	.rx_desc_get_mpdu_start_offset = ath12k_hw_qcn9274_rx_desc_get_mpdu_start_offset,
	.rx_desc_get_msdu_end_offset = ath12k_hw_qcn9274_rx_desc_get_msdu_end_offset,
	.rx_desc_mac_addr2_valid = ath12k_hw_qcn9274_rx_desc_mac_addr2_valid,
	.rx_desc_mpdu_start_addr2 = ath12k_hw_qcn9274_rx_desc_mpdu_start_addr2,
	.rx_desc_is_da_mcbc = ath12k_hw_qcn9274_rx_desc_is_da_mcbc,
	.rx_desc_get_dot11_hdr = ath12k_hw_qcn9274_rx_desc_get_dot11_hdr,
	.rx_desc_get_crypto_header = ath12k_hw_qcn9274_rx_desc_get_crypto_hdr,
	.dp_rx_h_msdu_done = ath12k_hw_qcn9274_dp_rx_h_msdu_done,
	.dp_rx_h_l4_cksum_fail = ath12k_hw_qcn9274_dp_rx_h_l4_cksum_fail,
	.dp_rx_h_ip_cksum_fail = ath12k_hw_qcn9274_dp_rx_h_ip_cksum_fail,
	.dp_rx_h_is_decrypted = ath12k_hw_qcn9274_dp_rx_h_is_decrypted,
	.dp_rx_h_mpdu_err = ath12k_hw_qcn9274_dp_rx_h_mpdu_err,
	.rx_desc_get_desc_size = ath12k_hw_qcn9274_get_rx_desc_size,
	.rx_desc_get_msdu_src_link_id = ath12k_hw_qcn9274_rx_desc_get_msdu_src_link,
};

static bool ath12k_hw_qcn9274_compact_rx_desc_get_first_msdu(struct hal_rx_desc *desc)
{
	return !!le16_get_bits(desc->u.qcn9274_compact.msdu_end.info5,
			       RX_MSDU_END_INFO5_FIRST_MSDU);
}

static bool ath12k_hw_qcn9274_compact_rx_desc_get_last_msdu(struct hal_rx_desc *desc)
{
	return !!le16_get_bits(desc->u.qcn9274_compact.msdu_end.info5,
			       RX_MSDU_END_INFO5_LAST_MSDU);
}

static u8 ath12k_hw_qcn9274_compact_rx_desc_get_l3_pad_bytes(struct hal_rx_desc *desc)
{
	return le16_get_bits(desc->u.qcn9274_compact.msdu_end.info5,
			     RX_MSDU_END_INFO5_L3_HDR_PADDING);
}

static bool ath12k_hw_qcn9274_compact_rx_desc_encrypt_valid(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.qcn9274_compact.mpdu_start.info4,
			       RX_MPDU_START_INFO4_ENCRYPT_INFO_VALID);
}

static u32 ath12k_hw_qcn9274_compact_rx_desc_get_encrypt_type(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274_compact.mpdu_start.info2,
			     RX_MPDU_START_INFO2_ENC_TYPE);
}

static u8 ath12k_hw_qcn9274_compact_rx_desc_get_decap_type(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274_compact.msdu_end.info11,
			     RX_MSDU_END_INFO11_DECAP_FORMAT);
}

static u8 ath12k_hw_qcn9274_compact_rx_desc_get_mesh_ctl(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274.msdu_end.info11,
			     RX_MSDU_END_INFO11_MESH_CTRL_PRESENT);
}

static bool
ath12k_hw_qcn9274_compact_rx_desc_get_mpdu_seq_ctl_vld(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.qcn9274_compact.mpdu_start.info4,
			       RX_MPDU_START_INFO4_MPDU_SEQ_CTRL_VALID);
}

static bool ath12k_hw_qcn9274_compact_rx_desc_get_mpdu_fc_valid(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.qcn9274_compact.mpdu_start.info4,
			       RX_MPDU_START_INFO4_MPDU_FCTRL_VALID);
}

static u16
ath12k_hw_qcn9274_compact_rx_desc_get_mpdu_start_seq_no(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274_compact.mpdu_start.info4,
			     RX_MPDU_START_INFO4_MPDU_SEQ_NUM);
}

static u16 ath12k_hw_qcn9274_compact_rx_desc_get_msdu_len(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274_compact.msdu_end.info10,
			     RX_MSDU_END_INFO10_MSDU_LENGTH);
}

static u8 ath12k_hw_qcn9274_compact_rx_desc_get_msdu_sgi(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274_compact.msdu_end.info12,
			     RX_MSDU_END_INFO12_SGI);
}

static u8 ath12k_hw_qcn9274_compact_rx_desc_get_msdu_rate_mcs(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274_compact.msdu_end.info12,
			     RX_MSDU_END_INFO12_RATE_MCS);
}

static u8 ath12k_hw_qcn9274_compact_rx_desc_get_msdu_rx_bw(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274_compact.msdu_end.info12,
			     RX_MSDU_END_INFO12_RECV_BW);
}

static u32 ath12k_hw_qcn9274_compact_rx_desc_get_msdu_freq(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->u.qcn9274_compact.msdu_end.phy_meta_data);
}

static u8 ath12k_hw_qcn9274_compact_rx_desc_get_msdu_pkt_type(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274_compact.msdu_end.info12,
			     RX_MSDU_END_INFO12_PKT_TYPE);
}

static u8 ath12k_hw_qcn9274_compact_rx_desc_get_msdu_nss(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.qcn9274_compact.msdu_end.info12,
			     RX_MSDU_END_INFO12_MIMO_SS_BITMAP);
}

static u8 ath12k_hw_qcn9274_compact_rx_desc_get_mpdu_tid(struct hal_rx_desc *desc)
{
	return le16_get_bits(desc->u.qcn9274_compact.msdu_end.info5,
			     RX_MSDU_END_INFO5_TID);
}

static u16 ath12k_hw_qcn9274_compact_rx_desc_get_mpdu_peer_id(struct hal_rx_desc *desc)
{
	return __le16_to_cpu(desc->u.qcn9274_compact.mpdu_start.sw_peer_id);
}

static void ath12k_hw_qcn9274_compact_rx_desc_copy_end_tlv(struct hal_rx_desc *fdesc,
							   struct hal_rx_desc *ldesc)
{
	fdesc->u.qcn9274_compact.msdu_end = ldesc->u.qcn9274_compact.msdu_end;
}

static u32 ath12k_hw_qcn9274_compact_rx_desc_get_mpdu_ppdu_id(struct hal_rx_desc *desc)
{
	return __le16_to_cpu(desc->u.qcn9274_compact.mpdu_start.phy_ppdu_id);
}

static void
ath12k_hw_qcn9274_compact_rx_desc_set_msdu_len(struct hal_rx_desc *desc, u16 len)
{
	u32 info = __le32_to_cpu(desc->u.qcn9274_compact.msdu_end.info10);

	info = u32_replace_bits(info, len, RX_MSDU_END_INFO10_MSDU_LENGTH);
	desc->u.qcn9274_compact.msdu_end.info10 = __cpu_to_le32(info);
}

static u8 *ath12k_hw_qcn9274_compact_rx_desc_get_msdu_payload(struct hal_rx_desc *desc)
{
	return &desc->u.qcn9274_compact.msdu_payload[0];
}

static u32 ath12k_hw_qcn9274_compact_rx_desc_get_mpdu_start_offset(void)
{
	return offsetof(struct hal_rx_desc_qcn9274_compact, mpdu_start);
}

static u32 ath12k_hw_qcn9274_compact_rx_desc_get_msdu_end_offset(void)
{
	return offsetof(struct hal_rx_desc_qcn9274_compact, msdu_end);
}

static bool ath12k_hw_qcn9274_compact_rx_desc_mac_addr2_valid(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->u.qcn9274_compact.mpdu_start.info4) &
			     RX_MPDU_START_INFO4_MAC_ADDR2_VALID;
}

static u8 *ath12k_hw_qcn9274_compact_rx_desc_mpdu_start_addr2(struct hal_rx_desc *desc)
{
	return desc->u.qcn9274_compact.mpdu_start.addr2;
}

static bool ath12k_hw_qcn9274_compact_rx_desc_is_da_mcbc(struct hal_rx_desc *desc)
{
	return __le16_to_cpu(desc->u.qcn9274_compact.msdu_end.info5) &
	       RX_MSDU_END_INFO5_DA_IS_MCBC;
}

static void ath12k_hw_qcn9274_compact_rx_desc_get_dot11_hdr(struct hal_rx_desc *desc,
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

static void
ath12k_hw_qcn9274_compact_rx_desc_get_crypto_hdr(struct hal_rx_desc *desc,
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

static bool ath12k_hw_qcn9274_compact_dp_rx_h_msdu_done(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.qcn9274_compact.msdu_end.info14,
			       RX_MSDU_END_INFO14_MSDU_DONE);
}

static bool ath12k_hw_qcn9274_compact_dp_rx_h_l4_cksum_fail(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.qcn9274_compact.msdu_end.info13,
			       RX_MSDU_END_INFO13_TCP_UDP_CKSUM_FAIL);
}

static bool ath12k_hw_qcn9274_compact_dp_rx_h_ip_cksum_fail(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.qcn9274_compact.msdu_end.info13,
			       RX_MSDU_END_INFO13_IP_CKSUM_FAIL);
}

static bool ath12k_hw_qcn9274_compact_dp_rx_h_is_decrypted(struct hal_rx_desc *desc)
{
	return (le32_get_bits(desc->u.qcn9274_compact.msdu_end.info14,
			      RX_MSDU_END_INFO14_DECRYPT_STATUS_CODE) ==
			RX_DESC_DECRYPT_STATUS_CODE_OK);
}

static u32 ath12k_hw_qcn9274_compact_dp_rx_h_mpdu_err(struct hal_rx_desc *desc)
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

static u32 ath12k_hw_qcn9274_compact_get_rx_desc_size(void)
{
	return sizeof(struct hal_rx_desc_qcn9274_compact);
}

static u8 ath12k_hw_qcn9274_compact_rx_desc_get_msdu_src_link(struct hal_rx_desc *desc)
{
	return le64_get_bits(desc->u.qcn9274_compact.msdu_end.msdu_end_tag,
			     RX_MSDU_END_64_TLV_SRC_LINK_ID);
}

const struct hal_rx_ops hal_rx_qcn9274_compact_ops = {
	.rx_desc_get_first_msdu = ath12k_hw_qcn9274_compact_rx_desc_get_first_msdu,
	.rx_desc_get_last_msdu = ath12k_hw_qcn9274_compact_rx_desc_get_last_msdu,
	.rx_desc_get_l3_pad_bytes = ath12k_hw_qcn9274_compact_rx_desc_get_l3_pad_bytes,
	.rx_desc_encrypt_valid = ath12k_hw_qcn9274_compact_rx_desc_encrypt_valid,
	.rx_desc_get_encrypt_type = ath12k_hw_qcn9274_compact_rx_desc_get_encrypt_type,
	.rx_desc_get_decap_type = ath12k_hw_qcn9274_compact_rx_desc_get_decap_type,
	.rx_desc_get_mesh_ctl = ath12k_hw_qcn9274_compact_rx_desc_get_mesh_ctl,
	.rx_desc_get_mpdu_seq_ctl_vld =
		ath12k_hw_qcn9274_compact_rx_desc_get_mpdu_seq_ctl_vld,
	.rx_desc_get_mpdu_fc_valid = ath12k_hw_qcn9274_compact_rx_desc_get_mpdu_fc_valid,
	.rx_desc_get_mpdu_start_seq_no =
		ath12k_hw_qcn9274_compact_rx_desc_get_mpdu_start_seq_no,
	.rx_desc_get_msdu_len = ath12k_hw_qcn9274_compact_rx_desc_get_msdu_len,
	.rx_desc_get_msdu_sgi = ath12k_hw_qcn9274_compact_rx_desc_get_msdu_sgi,
	.rx_desc_get_msdu_rate_mcs = ath12k_hw_qcn9274_compact_rx_desc_get_msdu_rate_mcs,
	.rx_desc_get_msdu_rx_bw = ath12k_hw_qcn9274_compact_rx_desc_get_msdu_rx_bw,
	.rx_desc_get_msdu_freq = ath12k_hw_qcn9274_compact_rx_desc_get_msdu_freq,
	.rx_desc_get_msdu_pkt_type = ath12k_hw_qcn9274_compact_rx_desc_get_msdu_pkt_type,
	.rx_desc_get_msdu_nss = ath12k_hw_qcn9274_compact_rx_desc_get_msdu_nss,
	.rx_desc_get_mpdu_tid = ath12k_hw_qcn9274_compact_rx_desc_get_mpdu_tid,
	.rx_desc_get_mpdu_peer_id = ath12k_hw_qcn9274_compact_rx_desc_get_mpdu_peer_id,
	.rx_desc_copy_end_tlv = ath12k_hw_qcn9274_compact_rx_desc_copy_end_tlv,
	.rx_desc_get_mpdu_ppdu_id = ath12k_hw_qcn9274_compact_rx_desc_get_mpdu_ppdu_id,
	.rx_desc_set_msdu_len = ath12k_hw_qcn9274_compact_rx_desc_set_msdu_len,
	.rx_desc_get_msdu_payload = ath12k_hw_qcn9274_compact_rx_desc_get_msdu_payload,
	.rx_desc_get_mpdu_start_offset =
		ath12k_hw_qcn9274_compact_rx_desc_get_mpdu_start_offset,
	.rx_desc_get_msdu_end_offset =
		ath12k_hw_qcn9274_compact_rx_desc_get_msdu_end_offset,
	.rx_desc_mac_addr2_valid = ath12k_hw_qcn9274_compact_rx_desc_mac_addr2_valid,
	.rx_desc_mpdu_start_addr2 = ath12k_hw_qcn9274_compact_rx_desc_mpdu_start_addr2,
	.rx_desc_is_da_mcbc = ath12k_hw_qcn9274_compact_rx_desc_is_da_mcbc,
	.rx_desc_get_dot11_hdr = ath12k_hw_qcn9274_compact_rx_desc_get_dot11_hdr,
	.rx_desc_get_crypto_header = ath12k_hw_qcn9274_compact_rx_desc_get_crypto_hdr,
	.dp_rx_h_msdu_done = ath12k_hw_qcn9274_compact_dp_rx_h_msdu_done,
	.dp_rx_h_l4_cksum_fail = ath12k_hw_qcn9274_compact_dp_rx_h_l4_cksum_fail,
	.dp_rx_h_ip_cksum_fail = ath12k_hw_qcn9274_compact_dp_rx_h_ip_cksum_fail,
	.dp_rx_h_is_decrypted = ath12k_hw_qcn9274_compact_dp_rx_h_is_decrypted,
	.dp_rx_h_mpdu_err = ath12k_hw_qcn9274_compact_dp_rx_h_mpdu_err,
	.rx_desc_get_desc_size = ath12k_hw_qcn9274_compact_get_rx_desc_size,
	.rx_desc_get_msdu_src_link_id =
		ath12k_hw_qcn9274_compact_rx_desc_get_msdu_src_link,
};

const struct hal_ops hal_qcn9274_ops = {
	.create_srng_config = ath12k_hal_srng_create_config_qcn9274,
	.tcl_to_wbm_rbm_map = ath12k_hal_qcn9274_tcl_to_wbm_rbm_map,
	.rxdma_ring_wmask_rx_mpdu_start = ath12k_hal_qcn9274_rx_mpdu_start_wmask_get,
	.rxdma_ring_wmask_rx_msdu_end = ath12k_hal_qcn9274_rx_msdu_end_wmask_get,
	.get_hal_rx_compact_ops = ath12k_hal_qcn9274_get_hal_rx_compact_ops,
};

static bool ath12k_hw_wcn7850_rx_desc_get_first_msdu(struct hal_rx_desc *desc)
{
	return !!le16_get_bits(desc->u.wcn7850.msdu_end.info5,
			       RX_MSDU_END_INFO5_FIRST_MSDU);
}

static bool ath12k_hw_wcn7850_rx_desc_get_last_msdu(struct hal_rx_desc *desc)
{
	return !!le16_get_bits(desc->u.wcn7850.msdu_end.info5,
			       RX_MSDU_END_INFO5_LAST_MSDU);
}

static u8 ath12k_hw_wcn7850_rx_desc_get_l3_pad_bytes(struct hal_rx_desc *desc)
{
	return le16_get_bits(desc->u.wcn7850.msdu_end.info5,
			    RX_MSDU_END_INFO5_L3_HDR_PADDING);
}

static bool ath12k_hw_wcn7850_rx_desc_encrypt_valid(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.wcn7850.mpdu_start.info4,
			       RX_MPDU_START_INFO4_ENCRYPT_INFO_VALID);
}

static u32 ath12k_hw_wcn7850_rx_desc_get_encrypt_type(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.wcn7850.mpdu_start.info2,
			     RX_MPDU_START_INFO2_ENC_TYPE);
}

static u8 ath12k_hw_wcn7850_rx_desc_get_decap_type(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.wcn7850.msdu_end.info11,
			     RX_MSDU_END_INFO11_DECAP_FORMAT);
}

static u8 ath12k_hw_wcn7850_rx_desc_get_mesh_ctl(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.wcn7850.msdu_end.info11,
			     RX_MSDU_END_INFO11_MESH_CTRL_PRESENT);
}

static bool ath12k_hw_wcn7850_rx_desc_get_mpdu_seq_ctl_vld(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.wcn7850.mpdu_start.info4,
			       RX_MPDU_START_INFO4_MPDU_SEQ_CTRL_VALID);
}

static bool ath12k_hw_wcn7850_rx_desc_get_mpdu_fc_valid(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.wcn7850.mpdu_start.info4,
			       RX_MPDU_START_INFO4_MPDU_FCTRL_VALID);
}

static u16 ath12k_hw_wcn7850_rx_desc_get_mpdu_start_seq_no(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.wcn7850.mpdu_start.info4,
			     RX_MPDU_START_INFO4_MPDU_SEQ_NUM);
}

static u16 ath12k_hw_wcn7850_rx_desc_get_msdu_len(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.wcn7850.msdu_end.info10,
			     RX_MSDU_END_INFO10_MSDU_LENGTH);
}

static u8 ath12k_hw_wcn7850_rx_desc_get_msdu_sgi(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.wcn7850.msdu_end.info12,
			     RX_MSDU_END_INFO12_SGI);
}

static u8 ath12k_hw_wcn7850_rx_desc_get_msdu_rate_mcs(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.wcn7850.msdu_end.info12,
			     RX_MSDU_END_INFO12_RATE_MCS);
}

static u8 ath12k_hw_wcn7850_rx_desc_get_msdu_rx_bw(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.wcn7850.msdu_end.info12,
			     RX_MSDU_END_INFO12_RECV_BW);
}

static u32 ath12k_hw_wcn7850_rx_desc_get_msdu_freq(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->u.wcn7850.msdu_end.phy_meta_data);
}

static u8 ath12k_hw_wcn7850_rx_desc_get_msdu_pkt_type(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.wcn7850.msdu_end.info12,
			     RX_MSDU_END_INFO12_PKT_TYPE);
}

static u8 ath12k_hw_wcn7850_rx_desc_get_msdu_nss(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.wcn7850.msdu_end.info12,
			     RX_MSDU_END_INFO12_MIMO_SS_BITMAP);
}

static u8 ath12k_hw_wcn7850_rx_desc_get_mpdu_tid(struct hal_rx_desc *desc)
{
	return le32_get_bits(desc->u.wcn7850.mpdu_start.info2,
			     RX_MPDU_START_INFO2_TID);
}

static u16 ath12k_hw_wcn7850_rx_desc_get_mpdu_peer_id(struct hal_rx_desc *desc)
{
	return __le16_to_cpu(desc->u.wcn7850.mpdu_start.sw_peer_id);
}

static void ath12k_hw_wcn7850_rx_desc_copy_end_tlv(struct hal_rx_desc *fdesc,
						   struct hal_rx_desc *ldesc)
{
	memcpy(&fdesc->u.wcn7850.msdu_end, &ldesc->u.wcn7850.msdu_end,
	       sizeof(struct rx_msdu_end_qcn9274));
}

static u32 ath12k_hw_wcn7850_rx_desc_get_mpdu_start_tag(struct hal_rx_desc *desc)
{
	return le64_get_bits(desc->u.wcn7850.mpdu_start_tag,
			    HAL_TLV_HDR_TAG);
}

static u32 ath12k_hw_wcn7850_rx_desc_get_mpdu_ppdu_id(struct hal_rx_desc *desc)
{
	return __le16_to_cpu(desc->u.wcn7850.mpdu_start.phy_ppdu_id);
}

static void ath12k_hw_wcn7850_rx_desc_set_msdu_len(struct hal_rx_desc *desc, u16 len)
{
	u32 info = __le32_to_cpu(desc->u.wcn7850.msdu_end.info10);

	info &= ~RX_MSDU_END_INFO10_MSDU_LENGTH;
	info |= u32_encode_bits(len, RX_MSDU_END_INFO10_MSDU_LENGTH);

	desc->u.wcn7850.msdu_end.info10 = __cpu_to_le32(info);
}

static u8 *ath12k_hw_wcn7850_rx_desc_get_msdu_payload(struct hal_rx_desc *desc)
{
	return &desc->u.wcn7850.msdu_payload[0];
}

static u32 ath12k_hw_wcn7850_rx_desc_get_mpdu_start_offset(void)
{
	return offsetof(struct hal_rx_desc_wcn7850, mpdu_start_tag);
}

static u32 ath12k_hw_wcn7850_rx_desc_get_msdu_end_offset(void)
{
	return offsetof(struct hal_rx_desc_wcn7850, msdu_end_tag);
}

static bool ath12k_hw_wcn7850_rx_desc_mac_addr2_valid(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->u.wcn7850.mpdu_start.info4) &
	       RX_MPDU_START_INFO4_MAC_ADDR2_VALID;
}

static u8 *ath12k_hw_wcn7850_rx_desc_mpdu_start_addr2(struct hal_rx_desc *desc)
{
	return desc->u.wcn7850.mpdu_start.addr2;
}

static bool ath12k_hw_wcn7850_rx_desc_is_da_mcbc(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->u.wcn7850.msdu_end.info13) &
	       RX_MSDU_END_INFO13_MCAST_BCAST;
}

static void ath12k_hw_wcn7850_rx_desc_get_dot11_hdr(struct hal_rx_desc *desc,
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

static void ath12k_hw_wcn7850_rx_desc_get_crypto_hdr(struct hal_rx_desc *desc,
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

static int ath12k_hal_srng_create_config_wcn7850(struct ath12k_base *ab)
{
	struct ath12k_hal *hal = &ab->hal;
	struct hal_srng_config *s;

	hal->srng_config = kmemdup(hw_srng_config_template,
				   sizeof(hw_srng_config_template),
				   GFP_KERNEL);
	if (!hal->srng_config)
		return -ENOMEM;

	s = &hal->srng_config[HAL_REO_DST];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO1_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO1_RING_HP;
	s->reg_size[0] = HAL_REO2_RING_BASE_LSB(ab) - HAL_REO1_RING_BASE_LSB(ab);
	s->reg_size[1] = HAL_REO2_RING_HP - HAL_REO1_RING_HP;

	s = &hal->srng_config[HAL_REO_EXCEPTION];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO_SW0_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO_SW0_RING_HP;

	s = &hal->srng_config[HAL_REO_REINJECT];
	s->max_rings = 1;
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_SW2REO_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_SW2REO_RING_HP;

	s = &hal->srng_config[HAL_REO_CMD];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO_CMD_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO_CMD_HP;

	s = &hal->srng_config[HAL_REO_STATUS];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO_STATUS_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO_STATUS_HP;

	s = &hal->srng_config[HAL_TCL_DATA];
	s->max_rings = 5;
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL1_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL1_RING_HP;
	s->reg_size[0] = HAL_TCL2_RING_BASE_LSB(ab) - HAL_TCL1_RING_BASE_LSB(ab);
	s->reg_size[1] = HAL_TCL2_RING_HP - HAL_TCL1_RING_HP;

	s = &hal->srng_config[HAL_TCL_CMD];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL_RING_HP;

	s = &hal->srng_config[HAL_TCL_STATUS];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL_STATUS_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL_STATUS_RING_HP;

	s = &hal->srng_config[HAL_CE_SRC];
	s->max_rings = 12;
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_CE0_SRC_REG(ab) + HAL_CE_DST_RING_BASE_LSB;
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_CE0_SRC_REG(ab) + HAL_CE_DST_RING_HP;
	s->reg_size[0] = HAL_SEQ_WCSS_UMAC_CE1_SRC_REG(ab) -
		HAL_SEQ_WCSS_UMAC_CE0_SRC_REG(ab);
	s->reg_size[1] = HAL_SEQ_WCSS_UMAC_CE1_SRC_REG(ab) -
		HAL_SEQ_WCSS_UMAC_CE0_SRC_REG(ab);

	s = &hal->srng_config[HAL_CE_DST];
	s->max_rings = 12;
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_CE0_DST_REG(ab) + HAL_CE_DST_RING_BASE_LSB;
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_CE0_DST_REG(ab) + HAL_CE_DST_RING_HP;
	s->reg_size[0] = HAL_SEQ_WCSS_UMAC_CE1_DST_REG(ab) -
		HAL_SEQ_WCSS_UMAC_CE0_DST_REG(ab);
	s->reg_size[1] = HAL_SEQ_WCSS_UMAC_CE1_DST_REG(ab) -
		HAL_SEQ_WCSS_UMAC_CE0_DST_REG(ab);

	s = &hal->srng_config[HAL_CE_DST_STATUS];
	s->max_rings = 12;
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_CE0_DST_REG(ab) +
		HAL_CE_DST_STATUS_RING_BASE_LSB;
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_CE0_DST_REG(ab) + HAL_CE_DST_STATUS_RING_HP;
	s->reg_size[0] = HAL_SEQ_WCSS_UMAC_CE1_DST_REG(ab) -
		HAL_SEQ_WCSS_UMAC_CE0_DST_REG(ab);
	s->reg_size[1] = HAL_SEQ_WCSS_UMAC_CE1_DST_REG(ab) -
		HAL_SEQ_WCSS_UMAC_CE0_DST_REG(ab);

	s = &hal->srng_config[HAL_WBM_IDLE_LINK];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM_IDLE_LINK_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM_IDLE_LINK_RING_HP;

	s = &hal->srng_config[HAL_SW2WBM_RELEASE];
	s->max_rings = 1;
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_WBM_REG +
		HAL_WBM_SW_RELEASE_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM_SW_RELEASE_RING_HP;

	s = &hal->srng_config[HAL_WBM2SW_RELEASE];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM0_RELEASE_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM0_RELEASE_RING_HP;
	s->reg_size[0] = HAL_WBM1_RELEASE_RING_BASE_LSB(ab) -
		HAL_WBM0_RELEASE_RING_BASE_LSB(ab);
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

static bool ath12k_hw_wcn7850_dp_rx_h_msdu_done(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.wcn7850.msdu_end.info14,
			       RX_MSDU_END_INFO14_MSDU_DONE);
}

static bool ath12k_hw_wcn7850_dp_rx_h_l4_cksum_fail(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.wcn7850.msdu_end.info13,
			       RX_MSDU_END_INFO13_TCP_UDP_CKSUM_FAIL);
}

static bool ath12k_hw_wcn7850_dp_rx_h_ip_cksum_fail(struct hal_rx_desc *desc)
{
	return !!le32_get_bits(desc->u.wcn7850.msdu_end.info13,
			      RX_MSDU_END_INFO13_IP_CKSUM_FAIL);
}

static bool ath12k_hw_wcn7850_dp_rx_h_is_decrypted(struct hal_rx_desc *desc)
{
	return (le32_get_bits(desc->u.wcn7850.msdu_end.info14,
			      RX_MSDU_END_INFO14_DECRYPT_STATUS_CODE) ==
			      RX_DESC_DECRYPT_STATUS_CODE_OK);
}

static u32 ath12k_hw_wcn7850_dp_rx_h_mpdu_err(struct hal_rx_desc *desc)
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

static u32 ath12k_hw_wcn7850_get_rx_desc_size(void)
{
	return sizeof(struct hal_rx_desc_wcn7850);
}

static u8 ath12k_hw_wcn7850_rx_desc_get_msdu_src_link(struct hal_rx_desc *desc)
{
	return 0;
}

const struct hal_rx_ops hal_rx_wcn7850_ops = {
	.rx_desc_get_first_msdu = ath12k_hw_wcn7850_rx_desc_get_first_msdu,
	.rx_desc_get_last_msdu = ath12k_hw_wcn7850_rx_desc_get_last_msdu,
	.rx_desc_get_l3_pad_bytes = ath12k_hw_wcn7850_rx_desc_get_l3_pad_bytes,
	.rx_desc_encrypt_valid = ath12k_hw_wcn7850_rx_desc_encrypt_valid,
	.rx_desc_get_encrypt_type = ath12k_hw_wcn7850_rx_desc_get_encrypt_type,
	.rx_desc_get_decap_type = ath12k_hw_wcn7850_rx_desc_get_decap_type,
	.rx_desc_get_mesh_ctl = ath12k_hw_wcn7850_rx_desc_get_mesh_ctl,
	.rx_desc_get_mpdu_seq_ctl_vld = ath12k_hw_wcn7850_rx_desc_get_mpdu_seq_ctl_vld,
	.rx_desc_get_mpdu_fc_valid = ath12k_hw_wcn7850_rx_desc_get_mpdu_fc_valid,
	.rx_desc_get_mpdu_start_seq_no = ath12k_hw_wcn7850_rx_desc_get_mpdu_start_seq_no,
	.rx_desc_get_msdu_len = ath12k_hw_wcn7850_rx_desc_get_msdu_len,
	.rx_desc_get_msdu_sgi = ath12k_hw_wcn7850_rx_desc_get_msdu_sgi,
	.rx_desc_get_msdu_rate_mcs = ath12k_hw_wcn7850_rx_desc_get_msdu_rate_mcs,
	.rx_desc_get_msdu_rx_bw = ath12k_hw_wcn7850_rx_desc_get_msdu_rx_bw,
	.rx_desc_get_msdu_freq = ath12k_hw_wcn7850_rx_desc_get_msdu_freq,
	.rx_desc_get_msdu_pkt_type = ath12k_hw_wcn7850_rx_desc_get_msdu_pkt_type,
	.rx_desc_get_msdu_nss = ath12k_hw_wcn7850_rx_desc_get_msdu_nss,
	.rx_desc_get_mpdu_tid = ath12k_hw_wcn7850_rx_desc_get_mpdu_tid,
	.rx_desc_get_mpdu_peer_id = ath12k_hw_wcn7850_rx_desc_get_mpdu_peer_id,
	.rx_desc_copy_end_tlv = ath12k_hw_wcn7850_rx_desc_copy_end_tlv,
	.rx_desc_get_mpdu_start_tag = ath12k_hw_wcn7850_rx_desc_get_mpdu_start_tag,
	.rx_desc_get_mpdu_ppdu_id = ath12k_hw_wcn7850_rx_desc_get_mpdu_ppdu_id,
	.rx_desc_set_msdu_len = ath12k_hw_wcn7850_rx_desc_set_msdu_len,
	.rx_desc_get_msdu_payload = ath12k_hw_wcn7850_rx_desc_get_msdu_payload,
	.rx_desc_get_mpdu_start_offset = ath12k_hw_wcn7850_rx_desc_get_mpdu_start_offset,
	.rx_desc_get_msdu_end_offset = ath12k_hw_wcn7850_rx_desc_get_msdu_end_offset,
	.rx_desc_mac_addr2_valid = ath12k_hw_wcn7850_rx_desc_mac_addr2_valid,
	.rx_desc_mpdu_start_addr2 = ath12k_hw_wcn7850_rx_desc_mpdu_start_addr2,
	.rx_desc_is_da_mcbc = ath12k_hw_wcn7850_rx_desc_is_da_mcbc,
	.rx_desc_get_dot11_hdr = ath12k_hw_wcn7850_rx_desc_get_dot11_hdr,
	.rx_desc_get_crypto_header = ath12k_hw_wcn7850_rx_desc_get_crypto_hdr,
	.dp_rx_h_msdu_done = ath12k_hw_wcn7850_dp_rx_h_msdu_done,
	.dp_rx_h_l4_cksum_fail = ath12k_hw_wcn7850_dp_rx_h_l4_cksum_fail,
	.dp_rx_h_ip_cksum_fail = ath12k_hw_wcn7850_dp_rx_h_ip_cksum_fail,
	.dp_rx_h_is_decrypted = ath12k_hw_wcn7850_dp_rx_h_is_decrypted,
	.dp_rx_h_mpdu_err = ath12k_hw_wcn7850_dp_rx_h_mpdu_err,
	.rx_desc_get_desc_size = ath12k_hw_wcn7850_get_rx_desc_size,
	.rx_desc_get_msdu_src_link_id = ath12k_hw_wcn7850_rx_desc_get_msdu_src_link,
};

const struct hal_ops hal_wcn7850_ops = {
	.create_srng_config = ath12k_hal_srng_create_config_wcn7850,
	.tcl_to_wbm_rbm_map = ath12k_hal_wcn7850_tcl_to_wbm_rbm_map,
	.rxdma_ring_wmask_rx_mpdu_start = NULL,
	.rxdma_ring_wmask_rx_msdu_end = NULL,
	.get_hal_rx_compact_ops = NULL,
};

static int ath12k_hal_alloc_cont_rdp(struct ath12k_base *ab)
{
	struct ath12k_hal *hal = &ab->hal;
	size_t size;

	size = sizeof(u32) * HAL_SRNG_RING_ID_MAX;
	hal->rdp.vaddr = dma_alloc_coherent(ab->dev, size, &hal->rdp.paddr,
					    GFP_KERNEL);
	if (!hal->rdp.vaddr)
		return -ENOMEM;

	return 0;
}

static void ath12k_hal_free_cont_rdp(struct ath12k_base *ab)
{
	struct ath12k_hal *hal = &ab->hal;
	size_t size;

	if (!hal->rdp.vaddr)
		return;

	size = sizeof(u32) * HAL_SRNG_RING_ID_MAX;
	dma_free_coherent(ab->dev, size,
			  hal->rdp.vaddr, hal->rdp.paddr);
	hal->rdp.vaddr = NULL;
}

static int ath12k_hal_alloc_cont_wrp(struct ath12k_base *ab)
{
	struct ath12k_hal *hal = &ab->hal;
	size_t size;

	size = sizeof(u32) * (HAL_SRNG_NUM_PMAC_RINGS + HAL_SRNG_NUM_DMAC_RINGS);
	hal->wrp.vaddr = dma_alloc_coherent(ab->dev, size, &hal->wrp.paddr,
					    GFP_KERNEL);
	if (!hal->wrp.vaddr)
		return -ENOMEM;

	return 0;
}

static void ath12k_hal_free_cont_wrp(struct ath12k_base *ab)
{
	struct ath12k_hal *hal = &ab->hal;
	size_t size;

	if (!hal->wrp.vaddr)
		return;

	size = sizeof(u32) * (HAL_SRNG_NUM_PMAC_RINGS + HAL_SRNG_NUM_DMAC_RINGS);
	dma_free_coherent(ab->dev, size,
			  hal->wrp.vaddr, hal->wrp.paddr);
	hal->wrp.vaddr = NULL;
}

static void ath12k_hal_ce_dst_setup(struct ath12k_base *ab,
				    struct hal_srng *srng, int ring_num)
{
	struct hal_srng_config *srng_config = &ab->hal.srng_config[HAL_CE_DST];
	u32 addr;
	u32 val;

	addr = HAL_CE_DST_RING_CTRL +
	       srng_config->reg_start[HAL_SRNG_REG_GRP_R0] +
	       ring_num * srng_config->reg_size[HAL_SRNG_REG_GRP_R0];

	val = ath12k_hif_read32(ab, addr);
	val &= ~HAL_CE_DST_R0_DEST_CTRL_MAX_LEN;
	val |= u32_encode_bits(srng->u.dst_ring.max_buffer_length,
			       HAL_CE_DST_R0_DEST_CTRL_MAX_LEN);
	ath12k_hif_write32(ab, addr, val);
}

static void ath12k_hal_srng_dst_hw_init(struct ath12k_base *ab,
					struct hal_srng *srng)
{
	struct ath12k_hal *hal = &ab->hal;
	u32 val;
	u64 hp_addr;
	u32 reg_base;

	reg_base = srng->hwreg_base[HAL_SRNG_REG_GRP_R0];

	if (srng->flags & HAL_SRNG_FLAGS_MSI_INTR) {
		ath12k_hif_write32(ab, reg_base +
				   ath12k_hal_reo1_ring_msi1_base_lsb_offset(ab),
				   srng->msi_addr);

		val = u32_encode_bits(((u64)srng->msi_addr >> HAL_ADDR_MSB_REG_SHIFT),
				      HAL_REO1_RING_MSI1_BASE_MSB_ADDR) |
				      HAL_REO1_RING_MSI1_BASE_MSB_MSI1_ENABLE;
		ath12k_hif_write32(ab, reg_base +
				   ath12k_hal_reo1_ring_msi1_base_msb_offset(ab), val);

		ath12k_hif_write32(ab,
				   reg_base + ath12k_hal_reo1_ring_msi1_data_offset(ab),
				   srng->msi_data);
	}

	ath12k_hif_write32(ab, reg_base, srng->ring_base_paddr);

	val = u32_encode_bits(((u64)srng->ring_base_paddr >> HAL_ADDR_MSB_REG_SHIFT),
			      HAL_REO1_RING_BASE_MSB_RING_BASE_ADDR_MSB) |
	      u32_encode_bits((srng->entry_size * srng->num_entries),
			      HAL_REO1_RING_BASE_MSB_RING_SIZE);
	ath12k_hif_write32(ab, reg_base + ath12k_hal_reo1_ring_base_msb_offset(ab), val);

	val = u32_encode_bits(srng->ring_id, HAL_REO1_RING_ID_RING_ID) |
	      u32_encode_bits(srng->entry_size, HAL_REO1_RING_ID_ENTRY_SIZE);
	ath12k_hif_write32(ab, reg_base + ath12k_hal_reo1_ring_id_offset(ab), val);

	/* interrupt setup */
	val = u32_encode_bits((srng->intr_timer_thres_us >> 3),
			      HAL_REO1_RING_PRDR_INT_SETUP_INTR_TMR_THOLD);

	val |= u32_encode_bits((srng->intr_batch_cntr_thres_entries * srng->entry_size),
				HAL_REO1_RING_PRDR_INT_SETUP_BATCH_COUNTER_THOLD);

	ath12k_hif_write32(ab,
			   reg_base + ath12k_hal_reo1_ring_producer_int_setup_offset(ab),
			   val);

	hp_addr = hal->rdp.paddr +
		  ((unsigned long)srng->u.dst_ring.hp_addr -
		   (unsigned long)hal->rdp.vaddr);
	ath12k_hif_write32(ab, reg_base + ath12k_hal_reo1_ring_hp_addr_lsb_offset(ab),
			   hp_addr & HAL_ADDR_LSB_REG_MASK);
	ath12k_hif_write32(ab, reg_base + ath12k_hal_reo1_ring_hp_addr_msb_offset(ab),
			   hp_addr >> HAL_ADDR_MSB_REG_SHIFT);

	/* Initialize head and tail pointers to indicate ring is empty */
	reg_base = srng->hwreg_base[HAL_SRNG_REG_GRP_R2];
	ath12k_hif_write32(ab, reg_base, 0);
	ath12k_hif_write32(ab, reg_base + HAL_REO1_RING_TP_OFFSET, 0);
	*srng->u.dst_ring.hp_addr = 0;

	reg_base = srng->hwreg_base[HAL_SRNG_REG_GRP_R0];
	val = 0;
	if (srng->flags & HAL_SRNG_FLAGS_DATA_TLV_SWAP)
		val |= HAL_REO1_RING_MISC_DATA_TLV_SWAP;
	if (srng->flags & HAL_SRNG_FLAGS_RING_PTR_SWAP)
		val |= HAL_REO1_RING_MISC_HOST_FW_SWAP;
	if (srng->flags & HAL_SRNG_FLAGS_MSI_SWAP)
		val |= HAL_REO1_RING_MISC_MSI_SWAP;
	val |= HAL_REO1_RING_MISC_SRNG_ENABLE;

	ath12k_hif_write32(ab, reg_base + ath12k_hal_reo1_ring_misc_offset(ab), val);
}

static void ath12k_hal_srng_src_hw_init(struct ath12k_base *ab,
					struct hal_srng *srng)
{
	struct ath12k_hal *hal = &ab->hal;
	u32 val;
	u64 tp_addr;
	u32 reg_base;

	reg_base = srng->hwreg_base[HAL_SRNG_REG_GRP_R0];

	if (srng->flags & HAL_SRNG_FLAGS_MSI_INTR) {
		ath12k_hif_write32(ab, reg_base +
				   HAL_TCL1_RING_MSI1_BASE_LSB_OFFSET(ab),
				   srng->msi_addr);

		val = u32_encode_bits(((u64)srng->msi_addr >> HAL_ADDR_MSB_REG_SHIFT),
				      HAL_TCL1_RING_MSI1_BASE_MSB_ADDR) |
				      HAL_TCL1_RING_MSI1_BASE_MSB_MSI1_ENABLE;
		ath12k_hif_write32(ab, reg_base +
				       HAL_TCL1_RING_MSI1_BASE_MSB_OFFSET(ab),
				   val);

		ath12k_hif_write32(ab, reg_base +
				       HAL_TCL1_RING_MSI1_DATA_OFFSET(ab),
				   srng->msi_data);
	}

	ath12k_hif_write32(ab, reg_base, srng->ring_base_paddr);

	val = u32_encode_bits(((u64)srng->ring_base_paddr >> HAL_ADDR_MSB_REG_SHIFT),
			      HAL_TCL1_RING_BASE_MSB_RING_BASE_ADDR_MSB) |
	      u32_encode_bits((srng->entry_size * srng->num_entries),
			      HAL_TCL1_RING_BASE_MSB_RING_SIZE);
	ath12k_hif_write32(ab, reg_base + HAL_TCL1_RING_BASE_MSB_OFFSET(ab), val);

	val = u32_encode_bits(srng->entry_size, HAL_REO1_RING_ID_ENTRY_SIZE);
	ath12k_hif_write32(ab, reg_base + HAL_TCL1_RING_ID_OFFSET(ab), val);

	val = u32_encode_bits(srng->intr_timer_thres_us,
			      HAL_TCL1_RING_CONSR_INT_SETUP_IX0_INTR_TMR_THOLD);

	val |= u32_encode_bits((srng->intr_batch_cntr_thres_entries * srng->entry_size),
			       HAL_TCL1_RING_CONSR_INT_SETUP_IX0_BATCH_COUNTER_THOLD);

	ath12k_hif_write32(ab,
			   reg_base + HAL_TCL1_RING_CONSR_INT_SETUP_IX0_OFFSET(ab),
			   val);

	val = 0;
	if (srng->flags & HAL_SRNG_FLAGS_LOW_THRESH_INTR_EN) {
		val |= u32_encode_bits(srng->u.src_ring.low_threshold,
				       HAL_TCL1_RING_CONSR_INT_SETUP_IX1_LOW_THOLD);
	}
	ath12k_hif_write32(ab,
			   reg_base + HAL_TCL1_RING_CONSR_INT_SETUP_IX1_OFFSET(ab),
			   val);

	if (srng->ring_id != HAL_SRNG_RING_ID_WBM_IDLE_LINK) {
		tp_addr = hal->rdp.paddr +
			  ((unsigned long)srng->u.src_ring.tp_addr -
			   (unsigned long)hal->rdp.vaddr);
		ath12k_hif_write32(ab,
				   reg_base + HAL_TCL1_RING_TP_ADDR_LSB_OFFSET(ab),
				   tp_addr & HAL_ADDR_LSB_REG_MASK);
		ath12k_hif_write32(ab,
				   reg_base + HAL_TCL1_RING_TP_ADDR_MSB_OFFSET(ab),
				   tp_addr >> HAL_ADDR_MSB_REG_SHIFT);
	}

	/* Initialize head and tail pointers to indicate ring is empty */
	reg_base = srng->hwreg_base[HAL_SRNG_REG_GRP_R2];
	ath12k_hif_write32(ab, reg_base, 0);
	ath12k_hif_write32(ab, reg_base + HAL_TCL1_RING_TP_OFFSET, 0);
	*srng->u.src_ring.tp_addr = 0;

	reg_base = srng->hwreg_base[HAL_SRNG_REG_GRP_R0];
	val = 0;
	if (srng->flags & HAL_SRNG_FLAGS_DATA_TLV_SWAP)
		val |= HAL_TCL1_RING_MISC_DATA_TLV_SWAP;
	if (srng->flags & HAL_SRNG_FLAGS_RING_PTR_SWAP)
		val |= HAL_TCL1_RING_MISC_HOST_FW_SWAP;
	if (srng->flags & HAL_SRNG_FLAGS_MSI_SWAP)
		val |= HAL_TCL1_RING_MISC_MSI_SWAP;

	/* Loop count is not used for SRC rings */
	val |= HAL_TCL1_RING_MISC_MSI_LOOPCNT_DISABLE;

	val |= HAL_TCL1_RING_MISC_SRNG_ENABLE;

	if (srng->ring_id == HAL_SRNG_RING_ID_WBM_IDLE_LINK)
		val |= HAL_TCL1_RING_MISC_MSI_RING_ID_DISABLE;

	ath12k_hif_write32(ab, reg_base + HAL_TCL1_RING_MISC_OFFSET(ab), val);
}

static void ath12k_hal_srng_hw_init(struct ath12k_base *ab,
				    struct hal_srng *srng)
{
	if (srng->ring_dir == HAL_SRNG_DIR_SRC)
		ath12k_hal_srng_src_hw_init(ab, srng);
	else
		ath12k_hal_srng_dst_hw_init(ab, srng);
}

static int ath12k_hal_srng_get_ring_id(struct ath12k_base *ab,
				       enum hal_ring_type type,
				       int ring_num, int mac_id)
{
	struct hal_srng_config *srng_config = &ab->hal.srng_config[type];
	int ring_id;

	if (ring_num >= srng_config->max_rings) {
		ath12k_warn(ab, "invalid ring number :%d\n", ring_num);
		return -EINVAL;
	}

	ring_id = srng_config->start_ring_id + ring_num;
	if (srng_config->mac_type == ATH12K_HAL_SRNG_PMAC)
		ring_id += mac_id * HAL_SRNG_RINGS_PER_PMAC;

	if (WARN_ON(ring_id >= HAL_SRNG_RING_ID_MAX))
		return -EINVAL;

	return ring_id;
}

int ath12k_hal_srng_get_entrysize(struct ath12k_base *ab, u32 ring_type)
{
	struct hal_srng_config *srng_config;

	if (WARN_ON(ring_type >= HAL_MAX_RING_TYPES))
		return -EINVAL;

	srng_config = &ab->hal.srng_config[ring_type];

	return (srng_config->entry_size << 2);
}

int ath12k_hal_srng_get_max_entries(struct ath12k_base *ab, u32 ring_type)
{
	struct hal_srng_config *srng_config;

	if (WARN_ON(ring_type >= HAL_MAX_RING_TYPES))
		return -EINVAL;

	srng_config = &ab->hal.srng_config[ring_type];

	return (srng_config->max_size / srng_config->entry_size);
}

void ath12k_hal_srng_get_params(struct ath12k_base *ab, struct hal_srng *srng,
				struct hal_srng_params *params)
{
	params->ring_base_paddr = srng->ring_base_paddr;
	params->ring_base_vaddr = srng->ring_base_vaddr;
	params->num_entries = srng->num_entries;
	params->intr_timer_thres_us = srng->intr_timer_thres_us;
	params->intr_batch_cntr_thres_entries =
		srng->intr_batch_cntr_thres_entries;
	params->low_threshold = srng->u.src_ring.low_threshold;
	params->msi_addr = srng->msi_addr;
	params->msi2_addr = srng->msi2_addr;
	params->msi_data = srng->msi_data;
	params->msi2_data = srng->msi2_data;
	params->flags = srng->flags;
}

dma_addr_t ath12k_hal_srng_get_hp_addr(struct ath12k_base *ab,
				       struct hal_srng *srng)
{
	if (!(srng->flags & HAL_SRNG_FLAGS_LMAC_RING))
		return 0;

	if (srng->ring_dir == HAL_SRNG_DIR_SRC)
		return ab->hal.wrp.paddr +
		       ((unsigned long)srng->u.src_ring.hp_addr -
			(unsigned long)ab->hal.wrp.vaddr);
	else
		return ab->hal.rdp.paddr +
		       ((unsigned long)srng->u.dst_ring.hp_addr -
			 (unsigned long)ab->hal.rdp.vaddr);
}

dma_addr_t ath12k_hal_srng_get_tp_addr(struct ath12k_base *ab,
				       struct hal_srng *srng)
{
	if (!(srng->flags & HAL_SRNG_FLAGS_LMAC_RING))
		return 0;

	if (srng->ring_dir == HAL_SRNG_DIR_SRC)
		return ab->hal.rdp.paddr +
		       ((unsigned long)srng->u.src_ring.tp_addr -
			(unsigned long)ab->hal.rdp.vaddr);
	else
		return ab->hal.wrp.paddr +
		       ((unsigned long)srng->u.dst_ring.tp_addr -
			(unsigned long)ab->hal.wrp.vaddr);
}

u32 ath12k_hal_ce_get_desc_size(enum hal_ce_desc type)
{
	switch (type) {
	case HAL_CE_DESC_SRC:
		return sizeof(struct hal_ce_srng_src_desc);
	case HAL_CE_DESC_DST:
		return sizeof(struct hal_ce_srng_dest_desc);
	case HAL_CE_DESC_DST_STATUS:
		return sizeof(struct hal_ce_srng_dst_status_desc);
	}

	return 0;
}

void ath12k_hal_ce_src_set_desc(struct hal_ce_srng_src_desc *desc, dma_addr_t paddr,
				u32 len, u32 id, u8 byte_swap_data)
{
	desc->buffer_addr_low = cpu_to_le32(paddr & HAL_ADDR_LSB_REG_MASK);
	desc->buffer_addr_info =
		le32_encode_bits(((u64)paddr >> HAL_ADDR_MSB_REG_SHIFT),
				 HAL_CE_SRC_DESC_ADDR_INFO_ADDR_HI) |
		le32_encode_bits(byte_swap_data,
				 HAL_CE_SRC_DESC_ADDR_INFO_BYTE_SWAP) |
		le32_encode_bits(0, HAL_CE_SRC_DESC_ADDR_INFO_GATHER) |
		le32_encode_bits(len, HAL_CE_SRC_DESC_ADDR_INFO_LEN);
	desc->meta_info = le32_encode_bits(id, HAL_CE_SRC_DESC_META_INFO_DATA);
}

void ath12k_hal_ce_dst_set_desc(struct hal_ce_srng_dest_desc *desc, dma_addr_t paddr)
{
	desc->buffer_addr_low = cpu_to_le32(paddr & HAL_ADDR_LSB_REG_MASK);
	desc->buffer_addr_info =
		le32_encode_bits(((u64)paddr >> HAL_ADDR_MSB_REG_SHIFT),
				 HAL_CE_DEST_DESC_ADDR_INFO_ADDR_HI);
}

u32 ath12k_hal_ce_dst_status_get_length(struct hal_ce_srng_dst_status_desc *desc)
{
	u32 len;

	len = le32_get_bits(desc->flags, HAL_CE_DST_STATUS_DESC_FLAGS_LEN);
	desc->flags &= ~cpu_to_le32(HAL_CE_DST_STATUS_DESC_FLAGS_LEN);

	return len;
}

void ath12k_hal_set_link_desc_addr(struct hal_wbm_link_desc *desc, u32 cookie,
				   dma_addr_t paddr,
				   enum hal_rx_buf_return_buf_manager rbm)
{
	desc->buf_addr_info.info0 = le32_encode_bits((paddr & HAL_ADDR_LSB_REG_MASK),
						     BUFFER_ADDR_INFO0_ADDR);
	desc->buf_addr_info.info1 =
			le32_encode_bits(((u64)paddr >> HAL_ADDR_MSB_REG_SHIFT),
					 BUFFER_ADDR_INFO1_ADDR) |
			le32_encode_bits(rbm, BUFFER_ADDR_INFO1_RET_BUF_MGR) |
			le32_encode_bits(cookie, BUFFER_ADDR_INFO1_SW_COOKIE);
}

void *ath12k_hal_srng_dst_peek(struct ath12k_base *ab, struct hal_srng *srng)
{
	lockdep_assert_held(&srng->lock);

	if (srng->u.dst_ring.tp != srng->u.dst_ring.cached_hp)
		return (srng->ring_base_vaddr + srng->u.dst_ring.tp);

	return NULL;
}

void *ath12k_hal_srng_dst_get_next_entry(struct ath12k_base *ab,
					 struct hal_srng *srng)
{
	void *desc;

	lockdep_assert_held(&srng->lock);

	if (srng->u.dst_ring.tp == srng->u.dst_ring.cached_hp)
		return NULL;

	desc = srng->ring_base_vaddr + srng->u.dst_ring.tp;

	srng->u.dst_ring.tp = (srng->u.dst_ring.tp + srng->entry_size) %
			      srng->ring_size;

	return desc;
}

int ath12k_hal_srng_dst_num_free(struct ath12k_base *ab, struct hal_srng *srng,
				 bool sync_hw_ptr)
{
	u32 tp, hp;

	lockdep_assert_held(&srng->lock);

	tp = srng->u.dst_ring.tp;

	if (sync_hw_ptr) {
		hp = *srng->u.dst_ring.hp_addr;
		srng->u.dst_ring.cached_hp = hp;
	} else {
		hp = srng->u.dst_ring.cached_hp;
	}

	if (hp >= tp)
		return (hp - tp) / srng->entry_size;
	else
		return (srng->ring_size - tp + hp) / srng->entry_size;
}

/* Returns number of available entries in src ring */
int ath12k_hal_srng_src_num_free(struct ath12k_base *ab, struct hal_srng *srng,
				 bool sync_hw_ptr)
{
	u32 tp, hp;

	lockdep_assert_held(&srng->lock);

	hp = srng->u.src_ring.hp;

	if (sync_hw_ptr) {
		tp = *srng->u.src_ring.tp_addr;
		srng->u.src_ring.cached_tp = tp;
	} else {
		tp = srng->u.src_ring.cached_tp;
	}

	if (tp > hp)
		return ((tp - hp) / srng->entry_size) - 1;
	else
		return ((srng->ring_size - hp + tp) / srng->entry_size) - 1;
}

void *ath12k_hal_srng_src_next_peek(struct ath12k_base *ab,
				    struct hal_srng *srng)
{
	void *desc;
	u32 next_hp;

	lockdep_assert_held(&srng->lock);

	next_hp = (srng->u.src_ring.hp + srng->entry_size) % srng->ring_size;

	if (next_hp == srng->u.src_ring.cached_tp)
		return NULL;

	desc = srng->ring_base_vaddr + next_hp;

	return desc;
}

void *ath12k_hal_srng_src_get_next_entry(struct ath12k_base *ab,
					 struct hal_srng *srng)
{
	void *desc;
	u32 next_hp;

	lockdep_assert_held(&srng->lock);

	/* TODO: Using % is expensive, but we have to do this since size of some
	 * SRNG rings is not power of 2 (due to descriptor sizes). Need to see
	 * if separate function is defined for rings having power of 2 ring size
	 * (TCL2SW, REO2SW, SW2RXDMA and CE rings) so that we can avoid the
	 * overhead of % by using mask (with &).
	 */
	next_hp = (srng->u.src_ring.hp + srng->entry_size) % srng->ring_size;

	if (next_hp == srng->u.src_ring.cached_tp)
		return NULL;

	desc = srng->ring_base_vaddr + srng->u.src_ring.hp;
	srng->u.src_ring.hp = next_hp;

	/* TODO: Reap functionality is not used by all rings. If particular
	 * ring does not use reap functionality, we need not update reap_hp
	 * with next_hp pointer. Need to make sure a separate function is used
	 * before doing any optimization by removing below code updating
	 * reap_hp.
	 */
	srng->u.src_ring.reap_hp = next_hp;

	return desc;
}

void *ath12k_hal_srng_src_peek(struct ath12k_base *ab, struct hal_srng *srng)
{
	lockdep_assert_held(&srng->lock);

	if (((srng->u.src_ring.hp + srng->entry_size) % srng->ring_size) ==
	    srng->u.src_ring.cached_tp)
		return NULL;

	return srng->ring_base_vaddr + srng->u.src_ring.hp;
}

void *ath12k_hal_srng_src_reap_next(struct ath12k_base *ab,
				    struct hal_srng *srng)
{
	void *desc;
	u32 next_reap_hp;

	lockdep_assert_held(&srng->lock);

	next_reap_hp = (srng->u.src_ring.reap_hp + srng->entry_size) %
		       srng->ring_size;

	if (next_reap_hp == srng->u.src_ring.cached_tp)
		return NULL;

	desc = srng->ring_base_vaddr + next_reap_hp;
	srng->u.src_ring.reap_hp = next_reap_hp;

	return desc;
}

void *ath12k_hal_srng_src_get_next_reaped(struct ath12k_base *ab,
					  struct hal_srng *srng)
{
	void *desc;

	lockdep_assert_held(&srng->lock);

	if (srng->u.src_ring.hp == srng->u.src_ring.reap_hp)
		return NULL;

	desc = srng->ring_base_vaddr + srng->u.src_ring.hp;
	srng->u.src_ring.hp = (srng->u.src_ring.hp + srng->entry_size) %
			      srng->ring_size;

	return desc;
}

void ath12k_hal_srng_access_begin(struct ath12k_base *ab, struct hal_srng *srng)
{
	lockdep_assert_held(&srng->lock);

	if (srng->ring_dir == HAL_SRNG_DIR_SRC)
		srng->u.src_ring.cached_tp =
			*(volatile u32 *)srng->u.src_ring.tp_addr;
	else
		srng->u.dst_ring.cached_hp = *srng->u.dst_ring.hp_addr;
}

/* Update cached ring head/tail pointers to HW. ath12k_hal_srng_access_begin()
 * should have been called before this.
 */
void ath12k_hal_srng_access_end(struct ath12k_base *ab, struct hal_srng *srng)
{
	lockdep_assert_held(&srng->lock);

	/* TODO: See if we need a write memory barrier here */
	if (srng->flags & HAL_SRNG_FLAGS_LMAC_RING) {
		/* For LMAC rings, ring pointer updates are done through FW and
		 * hence written to a shared memory location that is read by FW
		 */
		if (srng->ring_dir == HAL_SRNG_DIR_SRC) {
			srng->u.src_ring.last_tp =
				*(volatile u32 *)srng->u.src_ring.tp_addr;
			*srng->u.src_ring.hp_addr = srng->u.src_ring.hp;
		} else {
			srng->u.dst_ring.last_hp = *srng->u.dst_ring.hp_addr;
			*srng->u.dst_ring.tp_addr = srng->u.dst_ring.tp;
		}
	} else {
		if (srng->ring_dir == HAL_SRNG_DIR_SRC) {
			srng->u.src_ring.last_tp =
				*(volatile u32 *)srng->u.src_ring.tp_addr;
			ath12k_hif_write32(ab,
					   (unsigned long)srng->u.src_ring.hp_addr -
					   (unsigned long)ab->mem,
					   srng->u.src_ring.hp);
		} else {
			srng->u.dst_ring.last_hp = *srng->u.dst_ring.hp_addr;
			ath12k_hif_write32(ab,
					   (unsigned long)srng->u.dst_ring.tp_addr -
					   (unsigned long)ab->mem,
					   srng->u.dst_ring.tp);
		}
	}

	srng->timestamp = jiffies;
}

void ath12k_hal_setup_link_idle_list(struct ath12k_base *ab,
				     struct hal_wbm_idle_scatter_list *sbuf,
				     u32 nsbufs, u32 tot_link_desc,
				     u32 end_offset)
{
	struct ath12k_buffer_addr *link_addr;
	int i;
	u32 reg_scatter_buf_sz = HAL_WBM_IDLE_SCATTER_BUF_SIZE / 64;
	u32 val;

	link_addr = (void *)sbuf[0].vaddr + HAL_WBM_IDLE_SCATTER_BUF_SIZE;

	for (i = 1; i < nsbufs; i++) {
		link_addr->info0 = cpu_to_le32(sbuf[i].paddr & HAL_ADDR_LSB_REG_MASK);

		link_addr->info1 =
			le32_encode_bits((u64)sbuf[i].paddr >> HAL_ADDR_MSB_REG_SHIFT,
					 HAL_WBM_SCATTERED_DESC_MSB_BASE_ADDR_39_32) |
			le32_encode_bits(BASE_ADDR_MATCH_TAG_VAL,
					 HAL_WBM_SCATTERED_DESC_MSB_BASE_ADDR_MATCH_TAG);

		link_addr = (void *)sbuf[i].vaddr +
			     HAL_WBM_IDLE_SCATTER_BUF_SIZE;
	}

	val = u32_encode_bits(reg_scatter_buf_sz, HAL_WBM_SCATTER_BUFFER_SIZE) |
	      u32_encode_bits(0x1, HAL_WBM_LINK_DESC_IDLE_LIST_MODE);

	ath12k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_R0_IDLE_LIST_CONTROL_ADDR(ab),
			   val);

	val = u32_encode_bits(reg_scatter_buf_sz * nsbufs,
			      HAL_WBM_SCATTER_RING_SIZE_OF_IDLE_LINK_DESC_LIST);
	ath12k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM_R0_IDLE_LIST_SIZE_ADDR(ab),
			   val);

	val = u32_encode_bits(sbuf[0].paddr & HAL_ADDR_LSB_REG_MASK,
			      BUFFER_ADDR_INFO0_ADDR);
	ath12k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_SCATTERED_RING_BASE_LSB(ab),
			   val);

	val = u32_encode_bits(BASE_ADDR_MATCH_TAG_VAL,
			      HAL_WBM_SCATTERED_DESC_MSB_BASE_ADDR_MATCH_TAG) |
	      u32_encode_bits((u64)sbuf[0].paddr >> HAL_ADDR_MSB_REG_SHIFT,
			      HAL_WBM_SCATTERED_DESC_MSB_BASE_ADDR_39_32);
	ath12k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_SCATTERED_RING_BASE_MSB(ab),
			   val);

	/* Setup head and tail pointers for the idle list */
	val = u32_encode_bits(sbuf[nsbufs - 1].paddr, BUFFER_ADDR_INFO0_ADDR);
	ath12k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_SCATTERED_DESC_PTR_HEAD_INFO_IX0(ab),
			   val);

	val = u32_encode_bits(((u64)sbuf[nsbufs - 1].paddr >> HAL_ADDR_MSB_REG_SHIFT),
			      HAL_WBM_SCATTERED_DESC_MSB_BASE_ADDR_39_32) |
	       u32_encode_bits((end_offset >> 2),
			       HAL_WBM_SCATTERED_DESC_HEAD_P_OFFSET_IX1);
	ath12k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_SCATTERED_DESC_PTR_HEAD_INFO_IX1(ab),
			   val);

	val = u32_encode_bits(sbuf[0].paddr, BUFFER_ADDR_INFO0_ADDR);
	ath12k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_SCATTERED_DESC_PTR_HEAD_INFO_IX0(ab),
			   val);

	val = u32_encode_bits(sbuf[0].paddr, BUFFER_ADDR_INFO0_ADDR);
	ath12k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_SCATTERED_DESC_PTR_TAIL_INFO_IX0(ab),
			   val);

	val = u32_encode_bits(((u64)sbuf[0].paddr >> HAL_ADDR_MSB_REG_SHIFT),
			      HAL_WBM_SCATTERED_DESC_MSB_BASE_ADDR_39_32) |
	      u32_encode_bits(0, HAL_WBM_SCATTERED_DESC_TAIL_P_OFFSET_IX1);
	ath12k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_SCATTERED_DESC_PTR_TAIL_INFO_IX1(ab),
			   val);

	val = 2 * tot_link_desc;
	ath12k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_SCATTERED_DESC_PTR_HP_ADDR(ab),
			   val);

	/* Enable the SRNG */
	val = u32_encode_bits(1, HAL_WBM_IDLE_LINK_RING_MISC_SRNG_ENABLE) |
	      u32_encode_bits(1, HAL_WBM_IDLE_LINK_RING_MISC_RIND_ID_DISABLE);
	ath12k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_IDLE_LINK_RING_MISC_ADDR(ab),
			   val);
}

int ath12k_hal_srng_setup(struct ath12k_base *ab, enum hal_ring_type type,
			  int ring_num, int mac_id,
			  struct hal_srng_params *params)
{
	struct ath12k_hal *hal = &ab->hal;
	struct hal_srng_config *srng_config = &ab->hal.srng_config[type];
	struct hal_srng *srng;
	int ring_id;
	u32 idx;
	int i;
	u32 reg_base;

	ring_id = ath12k_hal_srng_get_ring_id(ab, type, ring_num, mac_id);
	if (ring_id < 0)
		return ring_id;

	srng = &hal->srng_list[ring_id];

	srng->ring_id = ring_id;
	srng->ring_dir = srng_config->ring_dir;
	srng->ring_base_paddr = params->ring_base_paddr;
	srng->ring_base_vaddr = params->ring_base_vaddr;
	srng->entry_size = srng_config->entry_size;
	srng->num_entries = params->num_entries;
	srng->ring_size = srng->entry_size * srng->num_entries;
	srng->intr_batch_cntr_thres_entries =
				params->intr_batch_cntr_thres_entries;
	srng->intr_timer_thres_us = params->intr_timer_thres_us;
	srng->flags = params->flags;
	srng->msi_addr = params->msi_addr;
	srng->msi2_addr = params->msi2_addr;
	srng->msi_data = params->msi_data;
	srng->msi2_data = params->msi2_data;
	srng->initialized = 1;
	spin_lock_init(&srng->lock);
	lockdep_set_class(&srng->lock, &srng->lock_key);

	for (i = 0; i < HAL_SRNG_NUM_REG_GRP; i++) {
		srng->hwreg_base[i] = srng_config->reg_start[i] +
				      (ring_num * srng_config->reg_size[i]);
	}

	memset(srng->ring_base_vaddr, 0,
	       (srng->entry_size * srng->num_entries) << 2);

	reg_base = srng->hwreg_base[HAL_SRNG_REG_GRP_R2];

	if (srng->ring_dir == HAL_SRNG_DIR_SRC) {
		srng->u.src_ring.hp = 0;
		srng->u.src_ring.cached_tp = 0;
		srng->u.src_ring.reap_hp = srng->ring_size - srng->entry_size;
		srng->u.src_ring.tp_addr = (void *)(hal->rdp.vaddr + ring_id);
		srng->u.src_ring.low_threshold = params->low_threshold *
						 srng->entry_size;
		if (srng_config->mac_type == ATH12K_HAL_SRNG_UMAC) {
			if (!ab->hw_params->supports_shadow_regs)
				srng->u.src_ring.hp_addr =
					(u32 *)((unsigned long)ab->mem + reg_base);
			else
				ath12k_dbg(ab, ATH12K_DBG_HAL,
					   "hal type %d ring_num %d reg_base 0x%x shadow 0x%lx\n",
					   type, ring_num,
					   reg_base,
					   (unsigned long)srng->u.src_ring.hp_addr -
					   (unsigned long)ab->mem);
		} else {
			idx = ring_id - HAL_SRNG_RING_ID_DMAC_CMN_ID_START;
			srng->u.src_ring.hp_addr = (void *)(hal->wrp.vaddr +
						   idx);
			srng->flags |= HAL_SRNG_FLAGS_LMAC_RING;
		}
	} else {
		/* During initialization loop count in all the descriptors
		 * will be set to zero, and HW will set it to 1 on completing
		 * descriptor update in first loop, and increments it by 1 on
		 * subsequent loops (loop count wraps around after reaching
		 * 0xffff). The 'loop_cnt' in SW ring state is the expected
		 * loop count in descriptors updated by HW (to be processed
		 * by SW).
		 */
		srng->u.dst_ring.loop_cnt = 1;
		srng->u.dst_ring.tp = 0;
		srng->u.dst_ring.cached_hp = 0;
		srng->u.dst_ring.hp_addr = (void *)(hal->rdp.vaddr + ring_id);
		if (srng_config->mac_type == ATH12K_HAL_SRNG_UMAC) {
			if (!ab->hw_params->supports_shadow_regs)
				srng->u.dst_ring.tp_addr =
					(u32 *)((unsigned long)ab->mem + reg_base +
					(HAL_REO1_RING_TP - HAL_REO1_RING_HP));
			else
				ath12k_dbg(ab, ATH12K_DBG_HAL,
					   "type %d ring_num %d target_reg 0x%x shadow 0x%lx\n",
					   type, ring_num,
					   reg_base + HAL_REO1_RING_TP - HAL_REO1_RING_HP,
					   (unsigned long)srng->u.dst_ring.tp_addr -
					   (unsigned long)ab->mem);
		} else {
			/* For PMAC & DMAC rings, tail pointer updates will be done
			 * through FW by writing to a shared memory location
			 */
			idx = ring_id - HAL_SRNG_RING_ID_DMAC_CMN_ID_START;
			srng->u.dst_ring.tp_addr = (void *)(hal->wrp.vaddr +
						   idx);
			srng->flags |= HAL_SRNG_FLAGS_LMAC_RING;
		}
	}

	if (srng_config->mac_type != ATH12K_HAL_SRNG_UMAC)
		return ring_id;

	ath12k_hal_srng_hw_init(ab, srng);

	if (type == HAL_CE_DST) {
		srng->u.dst_ring.max_buffer_length = params->max_buffer_len;
		ath12k_hal_ce_dst_setup(ab, srng, ring_num);
	}

	return ring_id;
}

static void ath12k_hal_srng_update_hp_tp_addr(struct ath12k_base *ab,
					      int shadow_cfg_idx,
					      enum hal_ring_type ring_type,
					      int ring_num)
{
	struct hal_srng *srng;
	struct ath12k_hal *hal = &ab->hal;
	int ring_id;
	struct hal_srng_config *srng_config = &hal->srng_config[ring_type];

	ring_id = ath12k_hal_srng_get_ring_id(ab, ring_type, ring_num, 0);
	if (ring_id < 0)
		return;

	srng = &hal->srng_list[ring_id];

	if (srng_config->ring_dir == HAL_SRNG_DIR_DST)
		srng->u.dst_ring.tp_addr = (u32 *)(HAL_SHADOW_REG(shadow_cfg_idx) +
						   (unsigned long)ab->mem);
	else
		srng->u.src_ring.hp_addr = (u32 *)(HAL_SHADOW_REG(shadow_cfg_idx) +
						   (unsigned long)ab->mem);
}

int ath12k_hal_srng_update_shadow_config(struct ath12k_base *ab,
					 enum hal_ring_type ring_type,
					 int ring_num)
{
	struct ath12k_hal *hal = &ab->hal;
	struct hal_srng_config *srng_config = &hal->srng_config[ring_type];
	int shadow_cfg_idx = hal->num_shadow_reg_configured;
	u32 target_reg;

	if (shadow_cfg_idx >= HAL_SHADOW_NUM_REGS)
		return -EINVAL;

	hal->num_shadow_reg_configured++;

	target_reg = srng_config->reg_start[HAL_HP_OFFSET_IN_REG_START];
	target_reg += srng_config->reg_size[HAL_HP_OFFSET_IN_REG_START] *
		ring_num;

	/* For destination ring, shadow the TP */
	if (srng_config->ring_dir == HAL_SRNG_DIR_DST)
		target_reg += HAL_OFFSET_FROM_HP_TO_TP;

	hal->shadow_reg_addr[shadow_cfg_idx] = target_reg;

	/* update hp/tp addr to hal structure*/
	ath12k_hal_srng_update_hp_tp_addr(ab, shadow_cfg_idx, ring_type,
					  ring_num);

	ath12k_dbg(ab, ATH12K_DBG_HAL,
		   "target_reg %x, shadow reg 0x%x shadow_idx 0x%x, ring_type %d, ring num %d",
		  target_reg,
		  HAL_SHADOW_REG(shadow_cfg_idx),
		  shadow_cfg_idx,
		  ring_type, ring_num);

	return 0;
}

void ath12k_hal_srng_shadow_config(struct ath12k_base *ab)
{
	struct ath12k_hal *hal = &ab->hal;
	int ring_type, ring_num;

	/* update all the non-CE srngs. */
	for (ring_type = 0; ring_type < HAL_MAX_RING_TYPES; ring_type++) {
		struct hal_srng_config *srng_config = &hal->srng_config[ring_type];

		if (ring_type == HAL_CE_SRC ||
		    ring_type == HAL_CE_DST ||
			ring_type == HAL_CE_DST_STATUS)
			continue;

		if (srng_config->mac_type == ATH12K_HAL_SRNG_DMAC ||
		    srng_config->mac_type == ATH12K_HAL_SRNG_PMAC)
			continue;

		for (ring_num = 0; ring_num < srng_config->max_rings; ring_num++)
			ath12k_hal_srng_update_shadow_config(ab, ring_type, ring_num);
	}
}

void ath12k_hal_srng_get_shadow_config(struct ath12k_base *ab,
				       u32 **cfg, u32 *len)
{
	struct ath12k_hal *hal = &ab->hal;

	*len = hal->num_shadow_reg_configured;
	*cfg = hal->shadow_reg_addr;
}

void ath12k_hal_srng_shadow_update_hp_tp(struct ath12k_base *ab,
					 struct hal_srng *srng)
{
	lockdep_assert_held(&srng->lock);

	/* check whether the ring is empty. Update the shadow
	 * HP only when then ring isn't' empty.
	 */
	if (srng->ring_dir == HAL_SRNG_DIR_SRC &&
	    *srng->u.src_ring.tp_addr != srng->u.src_ring.hp)
		ath12k_hal_srng_access_end(ab, srng);
}

static void ath12k_hal_register_srng_lock_keys(struct ath12k_base *ab)
{
	struct ath12k_hal *hal = &ab->hal;
	u32 ring_id;

	for (ring_id = 0; ring_id < HAL_SRNG_RING_ID_MAX; ring_id++)
		lockdep_register_key(&hal->srng_list[ring_id].lock_key);
}

static void ath12k_hal_unregister_srng_lock_keys(struct ath12k_base *ab)
{
	struct ath12k_hal *hal = &ab->hal;
	u32 ring_id;

	for (ring_id = 0; ring_id < HAL_SRNG_RING_ID_MAX; ring_id++)
		lockdep_unregister_key(&hal->srng_list[ring_id].lock_key);
}

int ath12k_hal_srng_init(struct ath12k_base *ab)
{
	struct ath12k_hal *hal = &ab->hal;
	int ret;

	memset(hal, 0, sizeof(*hal));

	ret = ab->hw_params->hal_ops->create_srng_config(ab);
	if (ret)
		goto err_hal;

	ret = ath12k_hal_alloc_cont_rdp(ab);
	if (ret)
		goto err_hal;

	ret = ath12k_hal_alloc_cont_wrp(ab);
	if (ret)
		goto err_free_cont_rdp;

	ath12k_hal_register_srng_lock_keys(ab);

	return 0;

err_free_cont_rdp:
	ath12k_hal_free_cont_rdp(ab);

err_hal:
	return ret;
}

void ath12k_hal_srng_deinit(struct ath12k_base *ab)
{
	struct ath12k_hal *hal = &ab->hal;

	ath12k_hal_unregister_srng_lock_keys(ab);
	ath12k_hal_free_cont_rdp(ab);
	ath12k_hal_free_cont_wrp(ab);
	kfree(hal->srng_config);
	hal->srng_config = NULL;
}

void ath12k_hal_dump_srng_stats(struct ath12k_base *ab)
{
	struct hal_srng *srng;
	struct ath12k_ext_irq_grp *irq_grp;
	struct ath12k_ce_pipe *ce_pipe;
	int i;

	ath12k_err(ab, "Last interrupt received for each CE:\n");
	for (i = 0; i < ab->hw_params->ce_count; i++) {
		ce_pipe = &ab->ce.ce_pipe[i];

		if (ath12k_ce_get_attr_flags(ab, i) & CE_ATTR_DIS_INTR)
			continue;

		ath12k_err(ab, "CE_id %d pipe_num %d %ums before\n",
			   i, ce_pipe->pipe_num,
			   jiffies_to_msecs(jiffies - ce_pipe->timestamp));
	}

	ath12k_err(ab, "\nLast interrupt received for each group:\n");
	for (i = 0; i < ATH12K_EXT_IRQ_GRP_NUM_MAX; i++) {
		irq_grp = &ab->ext_irq_grp[i];
		ath12k_err(ab, "group_id %d %ums before\n",
			   irq_grp->grp_id,
			   jiffies_to_msecs(jiffies - irq_grp->timestamp));
	}

	for (i = 0; i < HAL_SRNG_RING_ID_MAX; i++) {
		srng = &ab->hal.srng_list[i];

		if (!srng->initialized)
			continue;

		if (srng->ring_dir == HAL_SRNG_DIR_SRC)
			ath12k_err(ab,
				   "src srng id %u hp %u, reap_hp %u, cur tp %u, cached tp %u last tp %u napi processed before %ums\n",
				   srng->ring_id, srng->u.src_ring.hp,
				   srng->u.src_ring.reap_hp,
				   *srng->u.src_ring.tp_addr, srng->u.src_ring.cached_tp,
				   srng->u.src_ring.last_tp,
				   jiffies_to_msecs(jiffies - srng->timestamp));
		else if (srng->ring_dir == HAL_SRNG_DIR_DST)
			ath12k_err(ab,
				   "dst srng id %u tp %u, cur hp %u, cached hp %u last hp %u napi processed before %ums\n",
				   srng->ring_id, srng->u.dst_ring.tp,
				   *srng->u.dst_ring.hp_addr,
				   srng->u.dst_ring.cached_hp,
				   srng->u.dst_ring.last_hp,
				   jiffies_to_msecs(jiffies - srng->timestamp));
	}
}
