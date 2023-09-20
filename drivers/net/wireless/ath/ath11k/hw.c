// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/bitfield.h>

#include "core.h"
#include "ce.h"
#include "hif.h"
#include "hal.h"
#include "hw.h"

/* Map from pdev index to hw mac index */
static u8 ath11k_hw_ipq8074_mac_from_pdev_id(int pdev_idx)
{
	switch (pdev_idx) {
	case 0:
		return 0;
	case 1:
		return 2;
	case 2:
		return 1;
	default:
		return ATH11K_INVALID_HW_MAC_ID;
	}
}

static u8 ath11k_hw_ipq6018_mac_from_pdev_id(int pdev_idx)
{
	return pdev_idx;
}

static void ath11k_hw_ipq8074_tx_mesh_enable(struct ath11k_base *ab,
					     struct hal_tcl_data_cmd *tcl_cmd)
{
	tcl_cmd->info2 |= FIELD_PREP(HAL_IPQ8074_TCL_DATA_CMD_INFO2_MESH_ENABLE,
				     true);
}

static void ath11k_hw_qcn9074_tx_mesh_enable(struct ath11k_base *ab,
					     struct hal_tcl_data_cmd *tcl_cmd)
{
	tcl_cmd->info3 |= FIELD_PREP(HAL_QCN9074_TCL_DATA_CMD_INFO3_MESH_ENABLE,
				     true);
}

static void ath11k_hw_wcn6855_tx_mesh_enable(struct ath11k_base *ab,
					     struct hal_tcl_data_cmd *tcl_cmd)
{
	tcl_cmd->info3 |= FIELD_PREP(HAL_QCN9074_TCL_DATA_CMD_INFO3_MESH_ENABLE,
				     true);
}

static void ath11k_init_wmi_config_qca6390(struct ath11k_base *ab,
					   struct target_resource_config *config)
{
	config->num_vdevs = 4;
	config->num_peers = 16;
	config->num_tids = 32;

	config->num_offload_peers = 3;
	config->num_offload_reorder_buffs = 3;
	config->num_peer_keys = TARGET_NUM_PEER_KEYS;
	config->ast_skid_limit = TARGET_AST_SKID_LIMIT;
	config->tx_chain_mask = (1 << ab->target_caps.num_rf_chains) - 1;
	config->rx_chain_mask = (1 << ab->target_caps.num_rf_chains) - 1;
	config->rx_timeout_pri[0] = TARGET_RX_TIMEOUT_LO_PRI;
	config->rx_timeout_pri[1] = TARGET_RX_TIMEOUT_LO_PRI;
	config->rx_timeout_pri[2] = TARGET_RX_TIMEOUT_LO_PRI;
	config->rx_timeout_pri[3] = TARGET_RX_TIMEOUT_HI_PRI;
	config->rx_decap_mode = TARGET_DECAP_MODE_NATIVE_WIFI;
	config->scan_max_pending_req = TARGET_SCAN_MAX_PENDING_REQS;
	config->bmiss_offload_max_vdev = TARGET_BMISS_OFFLOAD_MAX_VDEV;
	config->roam_offload_max_vdev = TARGET_ROAM_OFFLOAD_MAX_VDEV;
	config->roam_offload_max_ap_profiles = TARGET_ROAM_OFFLOAD_MAX_AP_PROFILES;
	config->num_mcast_groups = 0;
	config->num_mcast_table_elems = 0;
	config->mcast2ucast_mode = 0;
	config->tx_dbg_log_size = TARGET_TX_DBG_LOG_SIZE;
	config->num_wds_entries = 0;
	config->dma_burst_size = 0;
	config->rx_skip_defrag_timeout_dup_detection_check = 0;
	config->vow_config = TARGET_VOW_CONFIG;
	config->gtk_offload_max_vdev = 2;
	config->num_msdu_desc = 0x400;
	config->beacon_tx_offload_max_vdev = 2;
	config->rx_batchmode = TARGET_RX_BATCHMODE;

	config->peer_map_unmap_v2_support = 0;
	config->use_pdev_id = 1;
	config->max_frag_entries = 0xa;
	config->num_tdls_vdevs = 0x1;
	config->num_tdls_conn_table_entries = 8;
	config->beacon_tx_offload_max_vdev = 0x2;
	config->num_multicast_filter_entries = 0x20;
	config->num_wow_filters = 0x16;
	config->num_keep_alive_pattern = 0;
	config->flag1 |= WMI_RSRC_CFG_FLAG1_BSS_CHANNEL_INFO_64;
}

static void ath11k_hw_ipq8074_reo_setup(struct ath11k_base *ab)
{
	u32 reo_base = HAL_SEQ_WCSS_UMAC_REO_REG;
	u32 val;
	/* Each hash entry uses three bits to map to a particular ring. */
	u32 ring_hash_map = HAL_HASH_ROUTING_RING_SW1 << 0 |
		HAL_HASH_ROUTING_RING_SW2 << 3 |
		HAL_HASH_ROUTING_RING_SW3 << 6 |
		HAL_HASH_ROUTING_RING_SW4 << 9 |
		HAL_HASH_ROUTING_RING_SW1 << 12 |
		HAL_HASH_ROUTING_RING_SW2 << 15 |
		HAL_HASH_ROUTING_RING_SW3 << 18 |
		HAL_HASH_ROUTING_RING_SW4 << 21;

	val = ath11k_hif_read32(ab, reo_base + HAL_REO1_GEN_ENABLE);

	val &= ~HAL_REO1_GEN_ENABLE_FRAG_DST_RING;
	val |= FIELD_PREP(HAL_REO1_GEN_ENABLE_FRAG_DST_RING,
			HAL_SRNG_RING_ID_REO2SW1) |
		FIELD_PREP(HAL_REO1_GEN_ENABLE_AGING_LIST_ENABLE, 1) |
		FIELD_PREP(HAL_REO1_GEN_ENABLE_AGING_FLUSH_ENABLE, 1);
	ath11k_hif_write32(ab, reo_base + HAL_REO1_GEN_ENABLE, val);

	ath11k_hif_write32(ab, reo_base + HAL_REO1_AGING_THRESH_IX_0(ab),
			   HAL_DEFAULT_REO_TIMEOUT_USEC);
	ath11k_hif_write32(ab, reo_base + HAL_REO1_AGING_THRESH_IX_1(ab),
			   HAL_DEFAULT_REO_TIMEOUT_USEC);
	ath11k_hif_write32(ab, reo_base + HAL_REO1_AGING_THRESH_IX_2(ab),
			   HAL_DEFAULT_REO_TIMEOUT_USEC);
	ath11k_hif_write32(ab, reo_base + HAL_REO1_AGING_THRESH_IX_3(ab),
			   HAL_DEFAULT_REO_TIMEOUT_USEC);

	ath11k_hif_write32(ab, reo_base + HAL_REO1_DEST_RING_CTRL_IX_0,
			   FIELD_PREP(HAL_REO_DEST_RING_CTRL_HASH_RING_MAP,
				      ring_hash_map));
	ath11k_hif_write32(ab, reo_base + HAL_REO1_DEST_RING_CTRL_IX_1,
			   FIELD_PREP(HAL_REO_DEST_RING_CTRL_HASH_RING_MAP,
				      ring_hash_map));
	ath11k_hif_write32(ab, reo_base + HAL_REO1_DEST_RING_CTRL_IX_2,
			   FIELD_PREP(HAL_REO_DEST_RING_CTRL_HASH_RING_MAP,
				      ring_hash_map));
	ath11k_hif_write32(ab, reo_base + HAL_REO1_DEST_RING_CTRL_IX_3,
			   FIELD_PREP(HAL_REO_DEST_RING_CTRL_HASH_RING_MAP,
				      ring_hash_map));
}

static void ath11k_init_wmi_config_ipq8074(struct ath11k_base *ab,
					   struct target_resource_config *config)
{
	config->num_vdevs = ab->num_radios * TARGET_NUM_VDEVS(ab);

	if (ab->num_radios == 2) {
		config->num_peers = TARGET_NUM_PEERS(ab, DBS);
		config->num_tids = TARGET_NUM_TIDS(ab, DBS);
	} else if (ab->num_radios == 3) {
		config->num_peers = TARGET_NUM_PEERS(ab, DBS_SBS);
		config->num_tids = TARGET_NUM_TIDS(ab, DBS_SBS);
	} else {
		/* Control should not reach here */
		config->num_peers = TARGET_NUM_PEERS(ab, SINGLE);
		config->num_tids = TARGET_NUM_TIDS(ab, SINGLE);
	}
	config->num_offload_peers = TARGET_NUM_OFFLD_PEERS;
	config->num_offload_reorder_buffs = TARGET_NUM_OFFLD_REORDER_BUFFS;
	config->num_peer_keys = TARGET_NUM_PEER_KEYS;
	config->ast_skid_limit = TARGET_AST_SKID_LIMIT;
	config->tx_chain_mask = (1 << ab->target_caps.num_rf_chains) - 1;
	config->rx_chain_mask = (1 << ab->target_caps.num_rf_chains) - 1;
	config->rx_timeout_pri[0] = TARGET_RX_TIMEOUT_LO_PRI;
	config->rx_timeout_pri[1] = TARGET_RX_TIMEOUT_LO_PRI;
	config->rx_timeout_pri[2] = TARGET_RX_TIMEOUT_LO_PRI;
	config->rx_timeout_pri[3] = TARGET_RX_TIMEOUT_HI_PRI;

	if (test_bit(ATH11K_FLAG_RAW_MODE, &ab->dev_flags))
		config->rx_decap_mode = TARGET_DECAP_MODE_RAW;
	else
		config->rx_decap_mode = TARGET_DECAP_MODE_NATIVE_WIFI;

	config->scan_max_pending_req = TARGET_SCAN_MAX_PENDING_REQS;
	config->bmiss_offload_max_vdev = TARGET_BMISS_OFFLOAD_MAX_VDEV;
	config->roam_offload_max_vdev = TARGET_ROAM_OFFLOAD_MAX_VDEV;
	config->roam_offload_max_ap_profiles = TARGET_ROAM_OFFLOAD_MAX_AP_PROFILES;
	config->num_mcast_groups = TARGET_NUM_MCAST_GROUPS;
	config->num_mcast_table_elems = TARGET_NUM_MCAST_TABLE_ELEMS;
	config->mcast2ucast_mode = TARGET_MCAST2UCAST_MODE;
	config->tx_dbg_log_size = TARGET_TX_DBG_LOG_SIZE;
	config->num_wds_entries = TARGET_NUM_WDS_ENTRIES;
	config->dma_burst_size = TARGET_DMA_BURST_SIZE;
	config->rx_skip_defrag_timeout_dup_detection_check =
		TARGET_RX_SKIP_DEFRAG_TIMEOUT_DUP_DETECTION_CHECK;
	config->vow_config = TARGET_VOW_CONFIG;
	config->gtk_offload_max_vdev = TARGET_GTK_OFFLOAD_MAX_VDEV;
	config->num_msdu_desc = TARGET_NUM_MSDU_DESC;
	config->beacon_tx_offload_max_vdev = ab->num_radios * TARGET_MAX_BCN_OFFLD;
	config->rx_batchmode = TARGET_RX_BATCHMODE;
	config->peer_map_unmap_v2_support = 1;
	config->twt_ap_pdev_count = ab->num_radios;
	config->twt_ap_sta_count = 1000;
	config->flag1 |= WMI_RSRC_CFG_FLAG1_BSS_CHANNEL_INFO_64;
	config->flag1 |= WMI_RSRC_CFG_FLAG1_ACK_RSSI;
	config->ema_max_vap_cnt = ab->num_radios;
	config->ema_max_profile_period = TARGET_EMA_MAX_PROFILE_PERIOD;
	config->beacon_tx_offload_max_vdev += config->ema_max_vap_cnt;
}

static int ath11k_hw_mac_id_to_pdev_id_ipq8074(struct ath11k_hw_params *hw,
					       int mac_id)
{
	return mac_id;
}

static int ath11k_hw_mac_id_to_srng_id_ipq8074(struct ath11k_hw_params *hw,
					       int mac_id)
{
	return 0;
}

static int ath11k_hw_mac_id_to_pdev_id_qca6390(struct ath11k_hw_params *hw,
					       int mac_id)
{
	return 0;
}

static int ath11k_hw_mac_id_to_srng_id_qca6390(struct ath11k_hw_params *hw,
					       int mac_id)
{
	return mac_id;
}

static bool ath11k_hw_ipq8074_rx_desc_get_first_msdu(struct hal_rx_desc *desc)
{
	return !!FIELD_GET(RX_MSDU_END_INFO2_FIRST_MSDU,
			   __le32_to_cpu(desc->u.ipq8074.msdu_end.info2));
}

static bool ath11k_hw_ipq8074_rx_desc_get_last_msdu(struct hal_rx_desc *desc)
{
	return !!FIELD_GET(RX_MSDU_END_INFO2_LAST_MSDU,
			   __le32_to_cpu(desc->u.ipq8074.msdu_end.info2));
}

static u8 ath11k_hw_ipq8074_rx_desc_get_l3_pad_bytes(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_END_INFO2_L3_HDR_PADDING,
			 __le32_to_cpu(desc->u.ipq8074.msdu_end.info2));
}

static u8 *ath11k_hw_ipq8074_rx_desc_get_hdr_status(struct hal_rx_desc *desc)
{
	return desc->u.ipq8074.hdr_status;
}

static bool ath11k_hw_ipq8074_rx_desc_encrypt_valid(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->u.ipq8074.mpdu_start.info1) &
	       RX_MPDU_START_INFO1_ENCRYPT_INFO_VALID;
}

static u32 ath11k_hw_ipq8074_rx_desc_get_encrypt_type(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MPDU_START_INFO2_ENC_TYPE,
			 __le32_to_cpu(desc->u.ipq8074.mpdu_start.info2));
}

static u8 ath11k_hw_ipq8074_rx_desc_get_decap_type(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO2_DECAP_FORMAT,
			 __le32_to_cpu(desc->u.ipq8074.msdu_start.info2));
}

static u8 ath11k_hw_ipq8074_rx_desc_get_mesh_ctl(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO2_MESH_CTRL_PRESENT,
			 __le32_to_cpu(desc->u.ipq8074.msdu_start.info2));
}

static bool ath11k_hw_ipq8074_rx_desc_get_ldpc_support(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO2_LDPC,
			 __le32_to_cpu(desc->u.ipq8074.msdu_start.info2));
}

static bool ath11k_hw_ipq8074_rx_desc_get_mpdu_seq_ctl_vld(struct hal_rx_desc *desc)
{
	return !!FIELD_GET(RX_MPDU_START_INFO1_MPDU_SEQ_CTRL_VALID,
			   __le32_to_cpu(desc->u.ipq8074.mpdu_start.info1));
}

static bool ath11k_hw_ipq8074_rx_desc_get_mpdu_fc_valid(struct hal_rx_desc *desc)
{
	return !!FIELD_GET(RX_MPDU_START_INFO1_MPDU_FCTRL_VALID,
			   __le32_to_cpu(desc->u.ipq8074.mpdu_start.info1));
}

static u16 ath11k_hw_ipq8074_rx_desc_get_mpdu_start_seq_no(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MPDU_START_INFO1_MPDU_SEQ_NUM,
			 __le32_to_cpu(desc->u.ipq8074.mpdu_start.info1));
}

static u16 ath11k_hw_ipq8074_rx_desc_get_msdu_len(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO1_MSDU_LENGTH,
			 __le32_to_cpu(desc->u.ipq8074.msdu_start.info1));
}

static u8 ath11k_hw_ipq8074_rx_desc_get_msdu_sgi(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO3_SGI,
			 __le32_to_cpu(desc->u.ipq8074.msdu_start.info3));
}

static u8 ath11k_hw_ipq8074_rx_desc_get_msdu_rate_mcs(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO3_RATE_MCS,
			 __le32_to_cpu(desc->u.ipq8074.msdu_start.info3));
}

static u8 ath11k_hw_ipq8074_rx_desc_get_msdu_rx_bw(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO3_RECV_BW,
			 __le32_to_cpu(desc->u.ipq8074.msdu_start.info3));
}

static u32 ath11k_hw_ipq8074_rx_desc_get_msdu_freq(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->u.ipq8074.msdu_start.phy_meta_data);
}

static u8 ath11k_hw_ipq8074_rx_desc_get_msdu_pkt_type(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO3_PKT_TYPE,
			 __le32_to_cpu(desc->u.ipq8074.msdu_start.info3));
}

static u8 ath11k_hw_ipq8074_rx_desc_get_msdu_nss(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO3_MIMO_SS_BITMAP,
			 __le32_to_cpu(desc->u.ipq8074.msdu_start.info3));
}

static u8 ath11k_hw_ipq8074_rx_desc_get_mpdu_tid(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MPDU_START_INFO2_TID,
			 __le32_to_cpu(desc->u.ipq8074.mpdu_start.info2));
}

static u16 ath11k_hw_ipq8074_rx_desc_get_mpdu_peer_id(struct hal_rx_desc *desc)
{
	return __le16_to_cpu(desc->u.ipq8074.mpdu_start.sw_peer_id);
}

static void ath11k_hw_ipq8074_rx_desc_copy_attn_end(struct hal_rx_desc *fdesc,
						    struct hal_rx_desc *ldesc)
{
	memcpy((u8 *)&fdesc->u.ipq8074.msdu_end, (u8 *)&ldesc->u.ipq8074.msdu_end,
	       sizeof(struct rx_msdu_end_ipq8074));
	memcpy((u8 *)&fdesc->u.ipq8074.attention, (u8 *)&ldesc->u.ipq8074.attention,
	       sizeof(struct rx_attention));
	memcpy((u8 *)&fdesc->u.ipq8074.mpdu_end, (u8 *)&ldesc->u.ipq8074.mpdu_end,
	       sizeof(struct rx_mpdu_end));
}

static u32 ath11k_hw_ipq8074_rx_desc_get_mpdu_start_tag(struct hal_rx_desc *desc)
{
	return FIELD_GET(HAL_TLV_HDR_TAG,
			 __le32_to_cpu(desc->u.ipq8074.mpdu_start_tag));
}

static u32 ath11k_hw_ipq8074_rx_desc_get_mpdu_ppdu_id(struct hal_rx_desc *desc)
{
	return __le16_to_cpu(desc->u.ipq8074.mpdu_start.phy_ppdu_id);
}

static void ath11k_hw_ipq8074_rx_desc_set_msdu_len(struct hal_rx_desc *desc, u16 len)
{
	u32 info = __le32_to_cpu(desc->u.ipq8074.msdu_start.info1);

	info &= ~RX_MSDU_START_INFO1_MSDU_LENGTH;
	info |= FIELD_PREP(RX_MSDU_START_INFO1_MSDU_LENGTH, len);

	desc->u.ipq8074.msdu_start.info1 = __cpu_to_le32(info);
}

static bool ath11k_hw_ipq8074_rx_desc_mac_addr2_valid(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->u.ipq8074.mpdu_start.info1) &
	       RX_MPDU_START_INFO1_MAC_ADDR2_VALID;
}

static u8 *ath11k_hw_ipq8074_rx_desc_mpdu_start_addr2(struct hal_rx_desc *desc)
{
	return desc->u.ipq8074.mpdu_start.addr2;
}

static
struct rx_attention *ath11k_hw_ipq8074_rx_desc_get_attention(struct hal_rx_desc *desc)
{
	return &desc->u.ipq8074.attention;
}

static u8 *ath11k_hw_ipq8074_rx_desc_get_msdu_payload(struct hal_rx_desc *desc)
{
	return &desc->u.ipq8074.msdu_payload[0];
}

static bool ath11k_hw_qcn9074_rx_desc_get_first_msdu(struct hal_rx_desc *desc)
{
	return !!FIELD_GET(RX_MSDU_END_INFO4_FIRST_MSDU,
			   __le16_to_cpu(desc->u.qcn9074.msdu_end.info4));
}

static bool ath11k_hw_qcn9074_rx_desc_get_last_msdu(struct hal_rx_desc *desc)
{
	return !!FIELD_GET(RX_MSDU_END_INFO4_LAST_MSDU,
			   __le16_to_cpu(desc->u.qcn9074.msdu_end.info4));
}

static u8 ath11k_hw_qcn9074_rx_desc_get_l3_pad_bytes(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_END_INFO4_L3_HDR_PADDING,
			 __le16_to_cpu(desc->u.qcn9074.msdu_end.info4));
}

static u8 *ath11k_hw_qcn9074_rx_desc_get_hdr_status(struct hal_rx_desc *desc)
{
	return desc->u.qcn9074.hdr_status;
}

static bool ath11k_hw_qcn9074_rx_desc_encrypt_valid(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->u.qcn9074.mpdu_start.info11) &
	       RX_MPDU_START_INFO11_ENCRYPT_INFO_VALID;
}

static u32 ath11k_hw_qcn9074_rx_desc_get_encrypt_type(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MPDU_START_INFO9_ENC_TYPE,
			 __le32_to_cpu(desc->u.qcn9074.mpdu_start.info9));
}

static u8 ath11k_hw_qcn9074_rx_desc_get_decap_type(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO2_DECAP_FORMAT,
			 __le32_to_cpu(desc->u.qcn9074.msdu_start.info2));
}

static u8 ath11k_hw_qcn9074_rx_desc_get_mesh_ctl(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO2_MESH_CTRL_PRESENT,
			 __le32_to_cpu(desc->u.qcn9074.msdu_start.info2));
}

static bool ath11k_hw_qcn9074_rx_desc_get_ldpc_support(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO2_LDPC,
			 __le32_to_cpu(desc->u.qcn9074.msdu_start.info2));
}

static bool ath11k_hw_qcn9074_rx_desc_get_mpdu_seq_ctl_vld(struct hal_rx_desc *desc)
{
	return !!FIELD_GET(RX_MPDU_START_INFO11_MPDU_SEQ_CTRL_VALID,
			   __le32_to_cpu(desc->u.qcn9074.mpdu_start.info11));
}

static bool ath11k_hw_qcn9074_rx_desc_get_mpdu_fc_valid(struct hal_rx_desc *desc)
{
	return !!FIELD_GET(RX_MPDU_START_INFO11_MPDU_FCTRL_VALID,
			   __le32_to_cpu(desc->u.qcn9074.mpdu_start.info11));
}

static u16 ath11k_hw_qcn9074_rx_desc_get_mpdu_start_seq_no(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MPDU_START_INFO11_MPDU_SEQ_NUM,
			 __le32_to_cpu(desc->u.qcn9074.mpdu_start.info11));
}

static u16 ath11k_hw_qcn9074_rx_desc_get_msdu_len(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO1_MSDU_LENGTH,
			 __le32_to_cpu(desc->u.qcn9074.msdu_start.info1));
}

static u8 ath11k_hw_qcn9074_rx_desc_get_msdu_sgi(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO3_SGI,
			 __le32_to_cpu(desc->u.qcn9074.msdu_start.info3));
}

static u8 ath11k_hw_qcn9074_rx_desc_get_msdu_rate_mcs(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO3_RATE_MCS,
			 __le32_to_cpu(desc->u.qcn9074.msdu_start.info3));
}

static u8 ath11k_hw_qcn9074_rx_desc_get_msdu_rx_bw(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO3_RECV_BW,
			 __le32_to_cpu(desc->u.qcn9074.msdu_start.info3));
}

static u32 ath11k_hw_qcn9074_rx_desc_get_msdu_freq(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->u.qcn9074.msdu_start.phy_meta_data);
}

static u8 ath11k_hw_qcn9074_rx_desc_get_msdu_pkt_type(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO3_PKT_TYPE,
			 __le32_to_cpu(desc->u.qcn9074.msdu_start.info3));
}

static u8 ath11k_hw_qcn9074_rx_desc_get_msdu_nss(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO3_MIMO_SS_BITMAP,
			 __le32_to_cpu(desc->u.qcn9074.msdu_start.info3));
}

static u8 ath11k_hw_qcn9074_rx_desc_get_mpdu_tid(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MPDU_START_INFO9_TID,
			 __le32_to_cpu(desc->u.qcn9074.mpdu_start.info9));
}

static u16 ath11k_hw_qcn9074_rx_desc_get_mpdu_peer_id(struct hal_rx_desc *desc)
{
	return __le16_to_cpu(desc->u.qcn9074.mpdu_start.sw_peer_id);
}

static void ath11k_hw_qcn9074_rx_desc_copy_attn_end(struct hal_rx_desc *fdesc,
						    struct hal_rx_desc *ldesc)
{
	memcpy((u8 *)&fdesc->u.qcn9074.msdu_end, (u8 *)&ldesc->u.qcn9074.msdu_end,
	       sizeof(struct rx_msdu_end_qcn9074));
	memcpy((u8 *)&fdesc->u.qcn9074.attention, (u8 *)&ldesc->u.qcn9074.attention,
	       sizeof(struct rx_attention));
	memcpy((u8 *)&fdesc->u.qcn9074.mpdu_end, (u8 *)&ldesc->u.qcn9074.mpdu_end,
	       sizeof(struct rx_mpdu_end));
}

static u32 ath11k_hw_qcn9074_rx_desc_get_mpdu_start_tag(struct hal_rx_desc *desc)
{
	return FIELD_GET(HAL_TLV_HDR_TAG,
			 __le32_to_cpu(desc->u.qcn9074.mpdu_start_tag));
}

static u32 ath11k_hw_qcn9074_rx_desc_get_mpdu_ppdu_id(struct hal_rx_desc *desc)
{
	return __le16_to_cpu(desc->u.qcn9074.mpdu_start.phy_ppdu_id);
}

static void ath11k_hw_qcn9074_rx_desc_set_msdu_len(struct hal_rx_desc *desc, u16 len)
{
	u32 info = __le32_to_cpu(desc->u.qcn9074.msdu_start.info1);

	info &= ~RX_MSDU_START_INFO1_MSDU_LENGTH;
	info |= FIELD_PREP(RX_MSDU_START_INFO1_MSDU_LENGTH, len);

	desc->u.qcn9074.msdu_start.info1 = __cpu_to_le32(info);
}

static
struct rx_attention *ath11k_hw_qcn9074_rx_desc_get_attention(struct hal_rx_desc *desc)
{
	return &desc->u.qcn9074.attention;
}

static u8 *ath11k_hw_qcn9074_rx_desc_get_msdu_payload(struct hal_rx_desc *desc)
{
	return &desc->u.qcn9074.msdu_payload[0];
}

static bool ath11k_hw_ipq9074_rx_desc_mac_addr2_valid(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->u.qcn9074.mpdu_start.info11) &
	       RX_MPDU_START_INFO11_MAC_ADDR2_VALID;
}

static u8 *ath11k_hw_ipq9074_rx_desc_mpdu_start_addr2(struct hal_rx_desc *desc)
{
	return desc->u.qcn9074.mpdu_start.addr2;
}

static bool ath11k_hw_wcn6855_rx_desc_get_first_msdu(struct hal_rx_desc *desc)
{
	return !!FIELD_GET(RX_MSDU_END_INFO2_FIRST_MSDU_WCN6855,
			   __le32_to_cpu(desc->u.wcn6855.msdu_end.info2));
}

static bool ath11k_hw_wcn6855_rx_desc_get_last_msdu(struct hal_rx_desc *desc)
{
	return !!FIELD_GET(RX_MSDU_END_INFO2_LAST_MSDU_WCN6855,
			   __le32_to_cpu(desc->u.wcn6855.msdu_end.info2));
}

static u8 ath11k_hw_wcn6855_rx_desc_get_l3_pad_bytes(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_END_INFO2_L3_HDR_PADDING,
			 __le32_to_cpu(desc->u.wcn6855.msdu_end.info2));
}

static u8 *ath11k_hw_wcn6855_rx_desc_get_hdr_status(struct hal_rx_desc *desc)
{
	return desc->u.wcn6855.hdr_status;
}

static bool ath11k_hw_wcn6855_rx_desc_encrypt_valid(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->u.wcn6855.mpdu_start.info1) &
	       RX_MPDU_START_INFO1_ENCRYPT_INFO_VALID;
}

static u32 ath11k_hw_wcn6855_rx_desc_get_encrypt_type(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MPDU_START_INFO2_ENC_TYPE,
			 __le32_to_cpu(desc->u.wcn6855.mpdu_start.info2));
}

static u8 ath11k_hw_wcn6855_rx_desc_get_decap_type(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO2_DECAP_FORMAT,
			 __le32_to_cpu(desc->u.wcn6855.msdu_start.info2));
}

static u8 ath11k_hw_wcn6855_rx_desc_get_mesh_ctl(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO2_MESH_CTRL_PRESENT,
			 __le32_to_cpu(desc->u.wcn6855.msdu_start.info2));
}

static bool ath11k_hw_wcn6855_rx_desc_get_mpdu_seq_ctl_vld(struct hal_rx_desc *desc)
{
	return !!FIELD_GET(RX_MPDU_START_INFO1_MPDU_SEQ_CTRL_VALID,
			   __le32_to_cpu(desc->u.wcn6855.mpdu_start.info1));
}

static bool ath11k_hw_wcn6855_rx_desc_get_mpdu_fc_valid(struct hal_rx_desc *desc)
{
	return !!FIELD_GET(RX_MPDU_START_INFO1_MPDU_FCTRL_VALID,
			   __le32_to_cpu(desc->u.wcn6855.mpdu_start.info1));
}

static u16 ath11k_hw_wcn6855_rx_desc_get_mpdu_start_seq_no(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MPDU_START_INFO1_MPDU_SEQ_NUM,
			 __le32_to_cpu(desc->u.wcn6855.mpdu_start.info1));
}

static u16 ath11k_hw_wcn6855_rx_desc_get_msdu_len(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO1_MSDU_LENGTH,
			 __le32_to_cpu(desc->u.wcn6855.msdu_start.info1));
}

static u8 ath11k_hw_wcn6855_rx_desc_get_msdu_sgi(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO3_SGI,
			 __le32_to_cpu(desc->u.wcn6855.msdu_start.info3));
}

static u8 ath11k_hw_wcn6855_rx_desc_get_msdu_rate_mcs(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO3_RATE_MCS,
			 __le32_to_cpu(desc->u.wcn6855.msdu_start.info3));
}

static u8 ath11k_hw_wcn6855_rx_desc_get_msdu_rx_bw(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO3_RECV_BW,
			 __le32_to_cpu(desc->u.wcn6855.msdu_start.info3));
}

static u32 ath11k_hw_wcn6855_rx_desc_get_msdu_freq(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->u.wcn6855.msdu_start.phy_meta_data);
}

static u8 ath11k_hw_wcn6855_rx_desc_get_msdu_pkt_type(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO3_PKT_TYPE,
			 __le32_to_cpu(desc->u.wcn6855.msdu_start.info3));
}

static u8 ath11k_hw_wcn6855_rx_desc_get_msdu_nss(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO3_MIMO_SS_BITMAP,
			 __le32_to_cpu(desc->u.wcn6855.msdu_start.info3));
}

static u8 ath11k_hw_wcn6855_rx_desc_get_mpdu_tid(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MPDU_START_INFO2_TID_WCN6855,
			 __le32_to_cpu(desc->u.wcn6855.mpdu_start.info2));
}

static u16 ath11k_hw_wcn6855_rx_desc_get_mpdu_peer_id(struct hal_rx_desc *desc)
{
	return __le16_to_cpu(desc->u.wcn6855.mpdu_start.sw_peer_id);
}

static void ath11k_hw_wcn6855_rx_desc_copy_attn_end(struct hal_rx_desc *fdesc,
						    struct hal_rx_desc *ldesc)
{
	memcpy((u8 *)&fdesc->u.wcn6855.msdu_end, (u8 *)&ldesc->u.wcn6855.msdu_end,
	       sizeof(struct rx_msdu_end_wcn6855));
	memcpy((u8 *)&fdesc->u.wcn6855.attention, (u8 *)&ldesc->u.wcn6855.attention,
	       sizeof(struct rx_attention));
	memcpy((u8 *)&fdesc->u.wcn6855.mpdu_end, (u8 *)&ldesc->u.wcn6855.mpdu_end,
	       sizeof(struct rx_mpdu_end));
}

static u32 ath11k_hw_wcn6855_rx_desc_get_mpdu_start_tag(struct hal_rx_desc *desc)
{
	return FIELD_GET(HAL_TLV_HDR_TAG,
			 __le32_to_cpu(desc->u.wcn6855.mpdu_start_tag));
}

static u32 ath11k_hw_wcn6855_rx_desc_get_mpdu_ppdu_id(struct hal_rx_desc *desc)
{
	return __le16_to_cpu(desc->u.wcn6855.mpdu_start.phy_ppdu_id);
}

static void ath11k_hw_wcn6855_rx_desc_set_msdu_len(struct hal_rx_desc *desc, u16 len)
{
	u32 info = __le32_to_cpu(desc->u.wcn6855.msdu_start.info1);

	info &= ~RX_MSDU_START_INFO1_MSDU_LENGTH;
	info |= FIELD_PREP(RX_MSDU_START_INFO1_MSDU_LENGTH, len);

	desc->u.wcn6855.msdu_start.info1 = __cpu_to_le32(info);
}

static
struct rx_attention *ath11k_hw_wcn6855_rx_desc_get_attention(struct hal_rx_desc *desc)
{
	return &desc->u.wcn6855.attention;
}

static u8 *ath11k_hw_wcn6855_rx_desc_get_msdu_payload(struct hal_rx_desc *desc)
{
	return &desc->u.wcn6855.msdu_payload[0];
}

static bool ath11k_hw_wcn6855_rx_desc_mac_addr2_valid(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->u.wcn6855.mpdu_start.info1) &
	       RX_MPDU_START_INFO1_MAC_ADDR2_VALID;
}

static u8 *ath11k_hw_wcn6855_rx_desc_mpdu_start_addr2(struct hal_rx_desc *desc)
{
	return desc->u.wcn6855.mpdu_start.addr2;
}

static void ath11k_hw_wcn6855_reo_setup(struct ath11k_base *ab)
{
	u32 reo_base = HAL_SEQ_WCSS_UMAC_REO_REG;
	u32 val;
	/* Each hash entry uses four bits to map to a particular ring. */
	u32 ring_hash_map = HAL_HASH_ROUTING_RING_SW1 << 0 |
		HAL_HASH_ROUTING_RING_SW2 << 4 |
		HAL_HASH_ROUTING_RING_SW3 << 8 |
		HAL_HASH_ROUTING_RING_SW4 << 12 |
		HAL_HASH_ROUTING_RING_SW1 << 16 |
		HAL_HASH_ROUTING_RING_SW2 << 20 |
		HAL_HASH_ROUTING_RING_SW3 << 24 |
		HAL_HASH_ROUTING_RING_SW4 << 28;

	val = ath11k_hif_read32(ab, reo_base + HAL_REO1_GEN_ENABLE);
	val |= FIELD_PREP(HAL_REO1_GEN_ENABLE_AGING_LIST_ENABLE, 1) |
		FIELD_PREP(HAL_REO1_GEN_ENABLE_AGING_FLUSH_ENABLE, 1);
	ath11k_hif_write32(ab, reo_base + HAL_REO1_GEN_ENABLE, val);

	val = ath11k_hif_read32(ab, reo_base + HAL_REO1_MISC_CTL(ab));
	val &= ~HAL_REO1_MISC_CTL_FRAGMENT_DST_RING;
	val |= FIELD_PREP(HAL_REO1_MISC_CTL_FRAGMENT_DST_RING, HAL_SRNG_RING_ID_REO2SW1);
	ath11k_hif_write32(ab, reo_base + HAL_REO1_MISC_CTL(ab), val);

	ath11k_hif_write32(ab, reo_base + HAL_REO1_AGING_THRESH_IX_0(ab),
			   HAL_DEFAULT_REO_TIMEOUT_USEC);
	ath11k_hif_write32(ab, reo_base + HAL_REO1_AGING_THRESH_IX_1(ab),
			   HAL_DEFAULT_REO_TIMEOUT_USEC);
	ath11k_hif_write32(ab, reo_base + HAL_REO1_AGING_THRESH_IX_2(ab),
			   HAL_DEFAULT_REO_TIMEOUT_USEC);
	ath11k_hif_write32(ab, reo_base + HAL_REO1_AGING_THRESH_IX_3(ab),
			   HAL_DEFAULT_REO_TIMEOUT_USEC);

	ath11k_hif_write32(ab, reo_base + HAL_REO1_DEST_RING_CTRL_IX_2,
			   ring_hash_map);
	ath11k_hif_write32(ab, reo_base + HAL_REO1_DEST_RING_CTRL_IX_3,
			   ring_hash_map);
}

static void ath11k_hw_ipq5018_reo_setup(struct ath11k_base *ab)
{
	u32 reo_base = HAL_SEQ_WCSS_UMAC_REO_REG;
	u32 val;

	/* Each hash entry uses three bits to map to a particular ring. */
	u32 ring_hash_map = HAL_HASH_ROUTING_RING_SW1 << 0 |
		HAL_HASH_ROUTING_RING_SW2 << 4 |
		HAL_HASH_ROUTING_RING_SW3 << 8 |
		HAL_HASH_ROUTING_RING_SW4 << 12 |
		HAL_HASH_ROUTING_RING_SW1 << 16 |
		HAL_HASH_ROUTING_RING_SW2 << 20 |
		HAL_HASH_ROUTING_RING_SW3 << 24 |
		HAL_HASH_ROUTING_RING_SW4 << 28;

	val = ath11k_hif_read32(ab, reo_base + HAL_REO1_GEN_ENABLE);

	val &= ~HAL_REO1_GEN_ENABLE_FRAG_DST_RING;
	val |= FIELD_PREP(HAL_REO1_GEN_ENABLE_FRAG_DST_RING,
			HAL_SRNG_RING_ID_REO2SW1) |
		FIELD_PREP(HAL_REO1_GEN_ENABLE_AGING_LIST_ENABLE, 1) |
		FIELD_PREP(HAL_REO1_GEN_ENABLE_AGING_FLUSH_ENABLE, 1);
	ath11k_hif_write32(ab, reo_base + HAL_REO1_GEN_ENABLE, val);

	ath11k_hif_write32(ab, reo_base + HAL_REO1_AGING_THRESH_IX_0(ab),
			   HAL_DEFAULT_REO_TIMEOUT_USEC);
	ath11k_hif_write32(ab, reo_base + HAL_REO1_AGING_THRESH_IX_1(ab),
			   HAL_DEFAULT_REO_TIMEOUT_USEC);
	ath11k_hif_write32(ab, reo_base + HAL_REO1_AGING_THRESH_IX_2(ab),
			   HAL_DEFAULT_REO_TIMEOUT_USEC);
	ath11k_hif_write32(ab, reo_base + HAL_REO1_AGING_THRESH_IX_3(ab),
			   HAL_DEFAULT_REO_TIMEOUT_USEC);

	ath11k_hif_write32(ab, reo_base + HAL_REO1_DEST_RING_CTRL_IX_0,
			   ring_hash_map);
	ath11k_hif_write32(ab, reo_base + HAL_REO1_DEST_RING_CTRL_IX_1,
			   ring_hash_map);
	ath11k_hif_write32(ab, reo_base + HAL_REO1_DEST_RING_CTRL_IX_2,
			   ring_hash_map);
	ath11k_hif_write32(ab, reo_base + HAL_REO1_DEST_RING_CTRL_IX_3,
			   ring_hash_map);
}

static u16
ath11k_hw_ipq8074_mpdu_info_get_peerid(struct hal_rx_mpdu_info *mpdu_info)
{
	u16 peer_id = 0;

	peer_id = FIELD_GET(HAL_RX_MPDU_INFO_INFO0_PEERID,
			    __le32_to_cpu(mpdu_info->u.ipq8074.info0));

	return peer_id;
}

static u16
ath11k_hw_qcn9074_mpdu_info_get_peerid(struct hal_rx_mpdu_info *mpdu_info)
{
	u16 peer_id = 0;

	peer_id = FIELD_GET(HAL_RX_MPDU_INFO_INFO0_PEERID,
			    __le32_to_cpu(mpdu_info->u.qcn9074.info0));

	return peer_id;
}

static u16
ath11k_hw_wcn6855_mpdu_info_get_peerid(struct hal_rx_mpdu_info *mpdu_info)
{
	u16 peer_id = 0;

	peer_id = FIELD_GET(HAL_RX_MPDU_INFO_INFO0_PEERID_WCN6855,
			    __le32_to_cpu(mpdu_info->u.wcn6855.info0));
	return peer_id;
}

static bool ath11k_hw_wcn6855_rx_desc_get_ldpc_support(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO2_LDPC,
			 __le32_to_cpu(desc->u.wcn6855.msdu_start.info2));
}

static u32 ath11k_hw_ipq8074_get_tcl_ring_selector(struct sk_buff *skb)
{
	/* Let the default ring selection be based on current processor
	 * number, where one of the 3 tcl rings are selected based on
	 * the smp_processor_id(). In case that ring
	 * is full/busy, we resort to other available rings.
	 * If all rings are full, we drop the packet.
	 *
	 * TODO: Add throttling logic when all rings are full
	 */
	return smp_processor_id();
}

static u32 ath11k_hw_wcn6750_get_tcl_ring_selector(struct sk_buff *skb)
{
	/* Select the TCL ring based on the flow hash of the SKB instead
	 * of CPU ID. Since applications pumping the traffic can be scheduled
	 * on multiple CPUs, there is a chance that packets of the same flow
	 * could end on different TCL rings, this could sometimes results in
	 * an out of order arrival of the packets at the receiver.
	 */
	return skb_get_hash(skb);
}

const struct ath11k_hw_ops ipq8074_ops = {
	.get_hw_mac_from_pdev_id = ath11k_hw_ipq8074_mac_from_pdev_id,
	.wmi_init_config = ath11k_init_wmi_config_ipq8074,
	.mac_id_to_pdev_id = ath11k_hw_mac_id_to_pdev_id_ipq8074,
	.mac_id_to_srng_id = ath11k_hw_mac_id_to_srng_id_ipq8074,
	.tx_mesh_enable = ath11k_hw_ipq8074_tx_mesh_enable,
	.rx_desc_get_first_msdu = ath11k_hw_ipq8074_rx_desc_get_first_msdu,
	.rx_desc_get_last_msdu = ath11k_hw_ipq8074_rx_desc_get_last_msdu,
	.rx_desc_get_l3_pad_bytes = ath11k_hw_ipq8074_rx_desc_get_l3_pad_bytes,
	.rx_desc_get_hdr_status = ath11k_hw_ipq8074_rx_desc_get_hdr_status,
	.rx_desc_encrypt_valid = ath11k_hw_ipq8074_rx_desc_encrypt_valid,
	.rx_desc_get_encrypt_type = ath11k_hw_ipq8074_rx_desc_get_encrypt_type,
	.rx_desc_get_decap_type = ath11k_hw_ipq8074_rx_desc_get_decap_type,
	.rx_desc_get_mesh_ctl = ath11k_hw_ipq8074_rx_desc_get_mesh_ctl,
	.rx_desc_get_ldpc_support = ath11k_hw_ipq8074_rx_desc_get_ldpc_support,
	.rx_desc_get_mpdu_seq_ctl_vld = ath11k_hw_ipq8074_rx_desc_get_mpdu_seq_ctl_vld,
	.rx_desc_get_mpdu_fc_valid = ath11k_hw_ipq8074_rx_desc_get_mpdu_fc_valid,
	.rx_desc_get_mpdu_start_seq_no = ath11k_hw_ipq8074_rx_desc_get_mpdu_start_seq_no,
	.rx_desc_get_msdu_len = ath11k_hw_ipq8074_rx_desc_get_msdu_len,
	.rx_desc_get_msdu_sgi = ath11k_hw_ipq8074_rx_desc_get_msdu_sgi,
	.rx_desc_get_msdu_rate_mcs = ath11k_hw_ipq8074_rx_desc_get_msdu_rate_mcs,
	.rx_desc_get_msdu_rx_bw = ath11k_hw_ipq8074_rx_desc_get_msdu_rx_bw,
	.rx_desc_get_msdu_freq = ath11k_hw_ipq8074_rx_desc_get_msdu_freq,
	.rx_desc_get_msdu_pkt_type = ath11k_hw_ipq8074_rx_desc_get_msdu_pkt_type,
	.rx_desc_get_msdu_nss = ath11k_hw_ipq8074_rx_desc_get_msdu_nss,
	.rx_desc_get_mpdu_tid = ath11k_hw_ipq8074_rx_desc_get_mpdu_tid,
	.rx_desc_get_mpdu_peer_id = ath11k_hw_ipq8074_rx_desc_get_mpdu_peer_id,
	.rx_desc_copy_attn_end_tlv = ath11k_hw_ipq8074_rx_desc_copy_attn_end,
	.rx_desc_get_mpdu_start_tag = ath11k_hw_ipq8074_rx_desc_get_mpdu_start_tag,
	.rx_desc_get_mpdu_ppdu_id = ath11k_hw_ipq8074_rx_desc_get_mpdu_ppdu_id,
	.rx_desc_set_msdu_len = ath11k_hw_ipq8074_rx_desc_set_msdu_len,
	.rx_desc_get_attention = ath11k_hw_ipq8074_rx_desc_get_attention,
	.rx_desc_get_msdu_payload = ath11k_hw_ipq8074_rx_desc_get_msdu_payload,
	.reo_setup = ath11k_hw_ipq8074_reo_setup,
	.mpdu_info_get_peerid = ath11k_hw_ipq8074_mpdu_info_get_peerid,
	.rx_desc_mac_addr2_valid = ath11k_hw_ipq8074_rx_desc_mac_addr2_valid,
	.rx_desc_mpdu_start_addr2 = ath11k_hw_ipq8074_rx_desc_mpdu_start_addr2,
	.get_ring_selector = ath11k_hw_ipq8074_get_tcl_ring_selector,
};

const struct ath11k_hw_ops ipq6018_ops = {
	.get_hw_mac_from_pdev_id = ath11k_hw_ipq6018_mac_from_pdev_id,
	.wmi_init_config = ath11k_init_wmi_config_ipq8074,
	.mac_id_to_pdev_id = ath11k_hw_mac_id_to_pdev_id_ipq8074,
	.mac_id_to_srng_id = ath11k_hw_mac_id_to_srng_id_ipq8074,
	.tx_mesh_enable = ath11k_hw_ipq8074_tx_mesh_enable,
	.rx_desc_get_first_msdu = ath11k_hw_ipq8074_rx_desc_get_first_msdu,
	.rx_desc_get_last_msdu = ath11k_hw_ipq8074_rx_desc_get_last_msdu,
	.rx_desc_get_l3_pad_bytes = ath11k_hw_ipq8074_rx_desc_get_l3_pad_bytes,
	.rx_desc_get_hdr_status = ath11k_hw_ipq8074_rx_desc_get_hdr_status,
	.rx_desc_encrypt_valid = ath11k_hw_ipq8074_rx_desc_encrypt_valid,
	.rx_desc_get_encrypt_type = ath11k_hw_ipq8074_rx_desc_get_encrypt_type,
	.rx_desc_get_decap_type = ath11k_hw_ipq8074_rx_desc_get_decap_type,
	.rx_desc_get_mesh_ctl = ath11k_hw_ipq8074_rx_desc_get_mesh_ctl,
	.rx_desc_get_ldpc_support = ath11k_hw_ipq8074_rx_desc_get_ldpc_support,
	.rx_desc_get_mpdu_seq_ctl_vld = ath11k_hw_ipq8074_rx_desc_get_mpdu_seq_ctl_vld,
	.rx_desc_get_mpdu_fc_valid = ath11k_hw_ipq8074_rx_desc_get_mpdu_fc_valid,
	.rx_desc_get_mpdu_start_seq_no = ath11k_hw_ipq8074_rx_desc_get_mpdu_start_seq_no,
	.rx_desc_get_msdu_len = ath11k_hw_ipq8074_rx_desc_get_msdu_len,
	.rx_desc_get_msdu_sgi = ath11k_hw_ipq8074_rx_desc_get_msdu_sgi,
	.rx_desc_get_msdu_rate_mcs = ath11k_hw_ipq8074_rx_desc_get_msdu_rate_mcs,
	.rx_desc_get_msdu_rx_bw = ath11k_hw_ipq8074_rx_desc_get_msdu_rx_bw,
	.rx_desc_get_msdu_freq = ath11k_hw_ipq8074_rx_desc_get_msdu_freq,
	.rx_desc_get_msdu_pkt_type = ath11k_hw_ipq8074_rx_desc_get_msdu_pkt_type,
	.rx_desc_get_msdu_nss = ath11k_hw_ipq8074_rx_desc_get_msdu_nss,
	.rx_desc_get_mpdu_tid = ath11k_hw_ipq8074_rx_desc_get_mpdu_tid,
	.rx_desc_get_mpdu_peer_id = ath11k_hw_ipq8074_rx_desc_get_mpdu_peer_id,
	.rx_desc_copy_attn_end_tlv = ath11k_hw_ipq8074_rx_desc_copy_attn_end,
	.rx_desc_get_mpdu_start_tag = ath11k_hw_ipq8074_rx_desc_get_mpdu_start_tag,
	.rx_desc_get_mpdu_ppdu_id = ath11k_hw_ipq8074_rx_desc_get_mpdu_ppdu_id,
	.rx_desc_set_msdu_len = ath11k_hw_ipq8074_rx_desc_set_msdu_len,
	.rx_desc_get_attention = ath11k_hw_ipq8074_rx_desc_get_attention,
	.rx_desc_get_msdu_payload = ath11k_hw_ipq8074_rx_desc_get_msdu_payload,
	.reo_setup = ath11k_hw_ipq8074_reo_setup,
	.mpdu_info_get_peerid = ath11k_hw_ipq8074_mpdu_info_get_peerid,
	.rx_desc_mac_addr2_valid = ath11k_hw_ipq8074_rx_desc_mac_addr2_valid,
	.rx_desc_mpdu_start_addr2 = ath11k_hw_ipq8074_rx_desc_mpdu_start_addr2,
	.get_ring_selector = ath11k_hw_ipq8074_get_tcl_ring_selector,
};

const struct ath11k_hw_ops qca6390_ops = {
	.get_hw_mac_from_pdev_id = ath11k_hw_ipq8074_mac_from_pdev_id,
	.wmi_init_config = ath11k_init_wmi_config_qca6390,
	.mac_id_to_pdev_id = ath11k_hw_mac_id_to_pdev_id_qca6390,
	.mac_id_to_srng_id = ath11k_hw_mac_id_to_srng_id_qca6390,
	.tx_mesh_enable = ath11k_hw_ipq8074_tx_mesh_enable,
	.rx_desc_get_first_msdu = ath11k_hw_ipq8074_rx_desc_get_first_msdu,
	.rx_desc_get_last_msdu = ath11k_hw_ipq8074_rx_desc_get_last_msdu,
	.rx_desc_get_l3_pad_bytes = ath11k_hw_ipq8074_rx_desc_get_l3_pad_bytes,
	.rx_desc_get_hdr_status = ath11k_hw_ipq8074_rx_desc_get_hdr_status,
	.rx_desc_encrypt_valid = ath11k_hw_ipq8074_rx_desc_encrypt_valid,
	.rx_desc_get_encrypt_type = ath11k_hw_ipq8074_rx_desc_get_encrypt_type,
	.rx_desc_get_decap_type = ath11k_hw_ipq8074_rx_desc_get_decap_type,
	.rx_desc_get_mesh_ctl = ath11k_hw_ipq8074_rx_desc_get_mesh_ctl,
	.rx_desc_get_ldpc_support = ath11k_hw_ipq8074_rx_desc_get_ldpc_support,
	.rx_desc_get_mpdu_seq_ctl_vld = ath11k_hw_ipq8074_rx_desc_get_mpdu_seq_ctl_vld,
	.rx_desc_get_mpdu_fc_valid = ath11k_hw_ipq8074_rx_desc_get_mpdu_fc_valid,
	.rx_desc_get_mpdu_start_seq_no = ath11k_hw_ipq8074_rx_desc_get_mpdu_start_seq_no,
	.rx_desc_get_msdu_len = ath11k_hw_ipq8074_rx_desc_get_msdu_len,
	.rx_desc_get_msdu_sgi = ath11k_hw_ipq8074_rx_desc_get_msdu_sgi,
	.rx_desc_get_msdu_rate_mcs = ath11k_hw_ipq8074_rx_desc_get_msdu_rate_mcs,
	.rx_desc_get_msdu_rx_bw = ath11k_hw_ipq8074_rx_desc_get_msdu_rx_bw,
	.rx_desc_get_msdu_freq = ath11k_hw_ipq8074_rx_desc_get_msdu_freq,
	.rx_desc_get_msdu_pkt_type = ath11k_hw_ipq8074_rx_desc_get_msdu_pkt_type,
	.rx_desc_get_msdu_nss = ath11k_hw_ipq8074_rx_desc_get_msdu_nss,
	.rx_desc_get_mpdu_tid = ath11k_hw_ipq8074_rx_desc_get_mpdu_tid,
	.rx_desc_get_mpdu_peer_id = ath11k_hw_ipq8074_rx_desc_get_mpdu_peer_id,
	.rx_desc_copy_attn_end_tlv = ath11k_hw_ipq8074_rx_desc_copy_attn_end,
	.rx_desc_get_mpdu_start_tag = ath11k_hw_ipq8074_rx_desc_get_mpdu_start_tag,
	.rx_desc_get_mpdu_ppdu_id = ath11k_hw_ipq8074_rx_desc_get_mpdu_ppdu_id,
	.rx_desc_set_msdu_len = ath11k_hw_ipq8074_rx_desc_set_msdu_len,
	.rx_desc_get_attention = ath11k_hw_ipq8074_rx_desc_get_attention,
	.rx_desc_get_msdu_payload = ath11k_hw_ipq8074_rx_desc_get_msdu_payload,
	.reo_setup = ath11k_hw_ipq8074_reo_setup,
	.mpdu_info_get_peerid = ath11k_hw_ipq8074_mpdu_info_get_peerid,
	.rx_desc_mac_addr2_valid = ath11k_hw_ipq8074_rx_desc_mac_addr2_valid,
	.rx_desc_mpdu_start_addr2 = ath11k_hw_ipq8074_rx_desc_mpdu_start_addr2,
	.get_ring_selector = ath11k_hw_ipq8074_get_tcl_ring_selector,
};

const struct ath11k_hw_ops qcn9074_ops = {
	.get_hw_mac_from_pdev_id = ath11k_hw_ipq6018_mac_from_pdev_id,
	.wmi_init_config = ath11k_init_wmi_config_ipq8074,
	.mac_id_to_pdev_id = ath11k_hw_mac_id_to_pdev_id_ipq8074,
	.mac_id_to_srng_id = ath11k_hw_mac_id_to_srng_id_ipq8074,
	.tx_mesh_enable = ath11k_hw_qcn9074_tx_mesh_enable,
	.rx_desc_get_first_msdu = ath11k_hw_qcn9074_rx_desc_get_first_msdu,
	.rx_desc_get_last_msdu = ath11k_hw_qcn9074_rx_desc_get_last_msdu,
	.rx_desc_get_l3_pad_bytes = ath11k_hw_qcn9074_rx_desc_get_l3_pad_bytes,
	.rx_desc_get_hdr_status = ath11k_hw_qcn9074_rx_desc_get_hdr_status,
	.rx_desc_encrypt_valid = ath11k_hw_qcn9074_rx_desc_encrypt_valid,
	.rx_desc_get_encrypt_type = ath11k_hw_qcn9074_rx_desc_get_encrypt_type,
	.rx_desc_get_decap_type = ath11k_hw_qcn9074_rx_desc_get_decap_type,
	.rx_desc_get_mesh_ctl = ath11k_hw_qcn9074_rx_desc_get_mesh_ctl,
	.rx_desc_get_ldpc_support = ath11k_hw_qcn9074_rx_desc_get_ldpc_support,
	.rx_desc_get_mpdu_seq_ctl_vld = ath11k_hw_qcn9074_rx_desc_get_mpdu_seq_ctl_vld,
	.rx_desc_get_mpdu_fc_valid = ath11k_hw_qcn9074_rx_desc_get_mpdu_fc_valid,
	.rx_desc_get_mpdu_start_seq_no = ath11k_hw_qcn9074_rx_desc_get_mpdu_start_seq_no,
	.rx_desc_get_msdu_len = ath11k_hw_qcn9074_rx_desc_get_msdu_len,
	.rx_desc_get_msdu_sgi = ath11k_hw_qcn9074_rx_desc_get_msdu_sgi,
	.rx_desc_get_msdu_rate_mcs = ath11k_hw_qcn9074_rx_desc_get_msdu_rate_mcs,
	.rx_desc_get_msdu_rx_bw = ath11k_hw_qcn9074_rx_desc_get_msdu_rx_bw,
	.rx_desc_get_msdu_freq = ath11k_hw_qcn9074_rx_desc_get_msdu_freq,
	.rx_desc_get_msdu_pkt_type = ath11k_hw_qcn9074_rx_desc_get_msdu_pkt_type,
	.rx_desc_get_msdu_nss = ath11k_hw_qcn9074_rx_desc_get_msdu_nss,
	.rx_desc_get_mpdu_tid = ath11k_hw_qcn9074_rx_desc_get_mpdu_tid,
	.rx_desc_get_mpdu_peer_id = ath11k_hw_qcn9074_rx_desc_get_mpdu_peer_id,
	.rx_desc_copy_attn_end_tlv = ath11k_hw_qcn9074_rx_desc_copy_attn_end,
	.rx_desc_get_mpdu_start_tag = ath11k_hw_qcn9074_rx_desc_get_mpdu_start_tag,
	.rx_desc_get_mpdu_ppdu_id = ath11k_hw_qcn9074_rx_desc_get_mpdu_ppdu_id,
	.rx_desc_set_msdu_len = ath11k_hw_qcn9074_rx_desc_set_msdu_len,
	.rx_desc_get_attention = ath11k_hw_qcn9074_rx_desc_get_attention,
	.rx_desc_get_msdu_payload = ath11k_hw_qcn9074_rx_desc_get_msdu_payload,
	.reo_setup = ath11k_hw_ipq8074_reo_setup,
	.mpdu_info_get_peerid = ath11k_hw_qcn9074_mpdu_info_get_peerid,
	.rx_desc_mac_addr2_valid = ath11k_hw_ipq9074_rx_desc_mac_addr2_valid,
	.rx_desc_mpdu_start_addr2 = ath11k_hw_ipq9074_rx_desc_mpdu_start_addr2,
	.get_ring_selector = ath11k_hw_ipq8074_get_tcl_ring_selector,
};

const struct ath11k_hw_ops wcn6855_ops = {
	.get_hw_mac_from_pdev_id = ath11k_hw_ipq8074_mac_from_pdev_id,
	.wmi_init_config = ath11k_init_wmi_config_qca6390,
	.mac_id_to_pdev_id = ath11k_hw_mac_id_to_pdev_id_qca6390,
	.mac_id_to_srng_id = ath11k_hw_mac_id_to_srng_id_qca6390,
	.tx_mesh_enable = ath11k_hw_wcn6855_tx_mesh_enable,
	.rx_desc_get_first_msdu = ath11k_hw_wcn6855_rx_desc_get_first_msdu,
	.rx_desc_get_last_msdu = ath11k_hw_wcn6855_rx_desc_get_last_msdu,
	.rx_desc_get_l3_pad_bytes = ath11k_hw_wcn6855_rx_desc_get_l3_pad_bytes,
	.rx_desc_get_hdr_status = ath11k_hw_wcn6855_rx_desc_get_hdr_status,
	.rx_desc_encrypt_valid = ath11k_hw_wcn6855_rx_desc_encrypt_valid,
	.rx_desc_get_encrypt_type = ath11k_hw_wcn6855_rx_desc_get_encrypt_type,
	.rx_desc_get_decap_type = ath11k_hw_wcn6855_rx_desc_get_decap_type,
	.rx_desc_get_mesh_ctl = ath11k_hw_wcn6855_rx_desc_get_mesh_ctl,
	.rx_desc_get_ldpc_support = ath11k_hw_wcn6855_rx_desc_get_ldpc_support,
	.rx_desc_get_mpdu_seq_ctl_vld = ath11k_hw_wcn6855_rx_desc_get_mpdu_seq_ctl_vld,
	.rx_desc_get_mpdu_fc_valid = ath11k_hw_wcn6855_rx_desc_get_mpdu_fc_valid,
	.rx_desc_get_mpdu_start_seq_no = ath11k_hw_wcn6855_rx_desc_get_mpdu_start_seq_no,
	.rx_desc_get_msdu_len = ath11k_hw_wcn6855_rx_desc_get_msdu_len,
	.rx_desc_get_msdu_sgi = ath11k_hw_wcn6855_rx_desc_get_msdu_sgi,
	.rx_desc_get_msdu_rate_mcs = ath11k_hw_wcn6855_rx_desc_get_msdu_rate_mcs,
	.rx_desc_get_msdu_rx_bw = ath11k_hw_wcn6855_rx_desc_get_msdu_rx_bw,
	.rx_desc_get_msdu_freq = ath11k_hw_wcn6855_rx_desc_get_msdu_freq,
	.rx_desc_get_msdu_pkt_type = ath11k_hw_wcn6855_rx_desc_get_msdu_pkt_type,
	.rx_desc_get_msdu_nss = ath11k_hw_wcn6855_rx_desc_get_msdu_nss,
	.rx_desc_get_mpdu_tid = ath11k_hw_wcn6855_rx_desc_get_mpdu_tid,
	.rx_desc_get_mpdu_peer_id = ath11k_hw_wcn6855_rx_desc_get_mpdu_peer_id,
	.rx_desc_copy_attn_end_tlv = ath11k_hw_wcn6855_rx_desc_copy_attn_end,
	.rx_desc_get_mpdu_start_tag = ath11k_hw_wcn6855_rx_desc_get_mpdu_start_tag,
	.rx_desc_get_mpdu_ppdu_id = ath11k_hw_wcn6855_rx_desc_get_mpdu_ppdu_id,
	.rx_desc_set_msdu_len = ath11k_hw_wcn6855_rx_desc_set_msdu_len,
	.rx_desc_get_attention = ath11k_hw_wcn6855_rx_desc_get_attention,
	.rx_desc_get_msdu_payload = ath11k_hw_wcn6855_rx_desc_get_msdu_payload,
	.reo_setup = ath11k_hw_wcn6855_reo_setup,
	.mpdu_info_get_peerid = ath11k_hw_wcn6855_mpdu_info_get_peerid,
	.rx_desc_mac_addr2_valid = ath11k_hw_wcn6855_rx_desc_mac_addr2_valid,
	.rx_desc_mpdu_start_addr2 = ath11k_hw_wcn6855_rx_desc_mpdu_start_addr2,
	.get_ring_selector = ath11k_hw_ipq8074_get_tcl_ring_selector,
};

const struct ath11k_hw_ops wcn6750_ops = {
	.get_hw_mac_from_pdev_id = ath11k_hw_ipq8074_mac_from_pdev_id,
	.wmi_init_config = ath11k_init_wmi_config_qca6390,
	.mac_id_to_pdev_id = ath11k_hw_mac_id_to_pdev_id_qca6390,
	.mac_id_to_srng_id = ath11k_hw_mac_id_to_srng_id_qca6390,
	.tx_mesh_enable = ath11k_hw_qcn9074_tx_mesh_enable,
	.rx_desc_get_first_msdu = ath11k_hw_qcn9074_rx_desc_get_first_msdu,
	.rx_desc_get_last_msdu = ath11k_hw_qcn9074_rx_desc_get_last_msdu,
	.rx_desc_get_l3_pad_bytes = ath11k_hw_qcn9074_rx_desc_get_l3_pad_bytes,
	.rx_desc_get_hdr_status = ath11k_hw_qcn9074_rx_desc_get_hdr_status,
	.rx_desc_encrypt_valid = ath11k_hw_qcn9074_rx_desc_encrypt_valid,
	.rx_desc_get_encrypt_type = ath11k_hw_qcn9074_rx_desc_get_encrypt_type,
	.rx_desc_get_decap_type = ath11k_hw_qcn9074_rx_desc_get_decap_type,
	.rx_desc_get_mesh_ctl = ath11k_hw_qcn9074_rx_desc_get_mesh_ctl,
	.rx_desc_get_ldpc_support = ath11k_hw_qcn9074_rx_desc_get_ldpc_support,
	.rx_desc_get_mpdu_seq_ctl_vld = ath11k_hw_qcn9074_rx_desc_get_mpdu_seq_ctl_vld,
	.rx_desc_get_mpdu_fc_valid = ath11k_hw_qcn9074_rx_desc_get_mpdu_fc_valid,
	.rx_desc_get_mpdu_start_seq_no = ath11k_hw_qcn9074_rx_desc_get_mpdu_start_seq_no,
	.rx_desc_get_msdu_len = ath11k_hw_qcn9074_rx_desc_get_msdu_len,
	.rx_desc_get_msdu_sgi = ath11k_hw_qcn9074_rx_desc_get_msdu_sgi,
	.rx_desc_get_msdu_rate_mcs = ath11k_hw_qcn9074_rx_desc_get_msdu_rate_mcs,
	.rx_desc_get_msdu_rx_bw = ath11k_hw_qcn9074_rx_desc_get_msdu_rx_bw,
	.rx_desc_get_msdu_freq = ath11k_hw_qcn9074_rx_desc_get_msdu_freq,
	.rx_desc_get_msdu_pkt_type = ath11k_hw_qcn9074_rx_desc_get_msdu_pkt_type,
	.rx_desc_get_msdu_nss = ath11k_hw_qcn9074_rx_desc_get_msdu_nss,
	.rx_desc_get_mpdu_tid = ath11k_hw_qcn9074_rx_desc_get_mpdu_tid,
	.rx_desc_get_mpdu_peer_id = ath11k_hw_qcn9074_rx_desc_get_mpdu_peer_id,
	.rx_desc_copy_attn_end_tlv = ath11k_hw_qcn9074_rx_desc_copy_attn_end,
	.rx_desc_get_mpdu_start_tag = ath11k_hw_qcn9074_rx_desc_get_mpdu_start_tag,
	.rx_desc_get_mpdu_ppdu_id = ath11k_hw_qcn9074_rx_desc_get_mpdu_ppdu_id,
	.rx_desc_set_msdu_len = ath11k_hw_qcn9074_rx_desc_set_msdu_len,
	.rx_desc_get_attention = ath11k_hw_qcn9074_rx_desc_get_attention,
	.rx_desc_get_msdu_payload = ath11k_hw_qcn9074_rx_desc_get_msdu_payload,
	.reo_setup = ath11k_hw_wcn6855_reo_setup,
	.mpdu_info_get_peerid = ath11k_hw_ipq8074_mpdu_info_get_peerid,
	.rx_desc_mac_addr2_valid = ath11k_hw_ipq9074_rx_desc_mac_addr2_valid,
	.rx_desc_mpdu_start_addr2 = ath11k_hw_ipq9074_rx_desc_mpdu_start_addr2,
	.get_ring_selector = ath11k_hw_wcn6750_get_tcl_ring_selector,
};

/* IPQ5018 hw ops is similar to QCN9074 except for the dest ring remap */
const struct ath11k_hw_ops ipq5018_ops = {
	.get_hw_mac_from_pdev_id = ath11k_hw_ipq6018_mac_from_pdev_id,
	.wmi_init_config = ath11k_init_wmi_config_ipq8074,
	.mac_id_to_pdev_id = ath11k_hw_mac_id_to_pdev_id_ipq8074,
	.mac_id_to_srng_id = ath11k_hw_mac_id_to_srng_id_ipq8074,
	.tx_mesh_enable = ath11k_hw_qcn9074_tx_mesh_enable,
	.rx_desc_get_first_msdu = ath11k_hw_qcn9074_rx_desc_get_first_msdu,
	.rx_desc_get_last_msdu = ath11k_hw_qcn9074_rx_desc_get_last_msdu,
	.rx_desc_get_l3_pad_bytes = ath11k_hw_qcn9074_rx_desc_get_l3_pad_bytes,
	.rx_desc_get_hdr_status = ath11k_hw_qcn9074_rx_desc_get_hdr_status,
	.rx_desc_encrypt_valid = ath11k_hw_qcn9074_rx_desc_encrypt_valid,
	.rx_desc_get_encrypt_type = ath11k_hw_qcn9074_rx_desc_get_encrypt_type,
	.rx_desc_get_decap_type = ath11k_hw_qcn9074_rx_desc_get_decap_type,
	.rx_desc_get_mesh_ctl = ath11k_hw_qcn9074_rx_desc_get_mesh_ctl,
	.rx_desc_get_ldpc_support = ath11k_hw_qcn9074_rx_desc_get_ldpc_support,
	.rx_desc_get_mpdu_seq_ctl_vld = ath11k_hw_qcn9074_rx_desc_get_mpdu_seq_ctl_vld,
	.rx_desc_get_mpdu_fc_valid = ath11k_hw_qcn9074_rx_desc_get_mpdu_fc_valid,
	.rx_desc_get_mpdu_start_seq_no = ath11k_hw_qcn9074_rx_desc_get_mpdu_start_seq_no,
	.rx_desc_get_msdu_len = ath11k_hw_qcn9074_rx_desc_get_msdu_len,
	.rx_desc_get_msdu_sgi = ath11k_hw_qcn9074_rx_desc_get_msdu_sgi,
	.rx_desc_get_msdu_rate_mcs = ath11k_hw_qcn9074_rx_desc_get_msdu_rate_mcs,
	.rx_desc_get_msdu_rx_bw = ath11k_hw_qcn9074_rx_desc_get_msdu_rx_bw,
	.rx_desc_get_msdu_freq = ath11k_hw_qcn9074_rx_desc_get_msdu_freq,
	.rx_desc_get_msdu_pkt_type = ath11k_hw_qcn9074_rx_desc_get_msdu_pkt_type,
	.rx_desc_get_msdu_nss = ath11k_hw_qcn9074_rx_desc_get_msdu_nss,
	.rx_desc_get_mpdu_tid = ath11k_hw_qcn9074_rx_desc_get_mpdu_tid,
	.rx_desc_get_mpdu_peer_id = ath11k_hw_qcn9074_rx_desc_get_mpdu_peer_id,
	.rx_desc_copy_attn_end_tlv = ath11k_hw_qcn9074_rx_desc_copy_attn_end,
	.rx_desc_get_mpdu_start_tag = ath11k_hw_qcn9074_rx_desc_get_mpdu_start_tag,
	.rx_desc_get_mpdu_ppdu_id = ath11k_hw_qcn9074_rx_desc_get_mpdu_ppdu_id,
	.rx_desc_set_msdu_len = ath11k_hw_qcn9074_rx_desc_set_msdu_len,
	.rx_desc_get_attention = ath11k_hw_qcn9074_rx_desc_get_attention,
	.reo_setup = ath11k_hw_ipq5018_reo_setup,
	.rx_desc_get_msdu_payload = ath11k_hw_qcn9074_rx_desc_get_msdu_payload,
	.mpdu_info_get_peerid = ath11k_hw_ipq8074_mpdu_info_get_peerid,
	.rx_desc_mac_addr2_valid = ath11k_hw_ipq9074_rx_desc_mac_addr2_valid,
	.rx_desc_mpdu_start_addr2 = ath11k_hw_ipq9074_rx_desc_mpdu_start_addr2,
	.get_ring_selector = ath11k_hw_ipq8074_get_tcl_ring_selector,
};

#define ATH11K_TX_RING_MASK_0 BIT(0)
#define ATH11K_TX_RING_MASK_1 BIT(1)
#define ATH11K_TX_RING_MASK_2 BIT(2)
#define ATH11K_TX_RING_MASK_3 BIT(3)
#define ATH11K_TX_RING_MASK_4 BIT(4)

#define ATH11K_RX_RING_MASK_0 0x1
#define ATH11K_RX_RING_MASK_1 0x2
#define ATH11K_RX_RING_MASK_2 0x4
#define ATH11K_RX_RING_MASK_3 0x8

#define ATH11K_RX_ERR_RING_MASK_0 0x1

#define ATH11K_RX_WBM_REL_RING_MASK_0 0x1

#define ATH11K_REO_STATUS_RING_MASK_0 0x1

#define ATH11K_RXDMA2HOST_RING_MASK_0 0x1
#define ATH11K_RXDMA2HOST_RING_MASK_1 0x2
#define ATH11K_RXDMA2HOST_RING_MASK_2 0x4

#define ATH11K_HOST2RXDMA_RING_MASK_0 0x1
#define ATH11K_HOST2RXDMA_RING_MASK_1 0x2
#define ATH11K_HOST2RXDMA_RING_MASK_2 0x4

#define ATH11K_RX_MON_STATUS_RING_MASK_0 0x1
#define ATH11K_RX_MON_STATUS_RING_MASK_1 0x2
#define ATH11K_RX_MON_STATUS_RING_MASK_2 0x4

const struct ath11k_hw_ring_mask ath11k_hw_ring_mask_ipq8074 = {
	.tx  = {
		ATH11K_TX_RING_MASK_0,
		ATH11K_TX_RING_MASK_1,
		ATH11K_TX_RING_MASK_2,
	},
	.rx_mon_status = {
		0, 0, 0, 0,
		ATH11K_RX_MON_STATUS_RING_MASK_0,
		ATH11K_RX_MON_STATUS_RING_MASK_1,
		ATH11K_RX_MON_STATUS_RING_MASK_2,
	},
	.rx = {
		0, 0, 0, 0, 0, 0, 0,
		ATH11K_RX_RING_MASK_0,
		ATH11K_RX_RING_MASK_1,
		ATH11K_RX_RING_MASK_2,
		ATH11K_RX_RING_MASK_3,
	},
	.rx_err = {
		ATH11K_RX_ERR_RING_MASK_0,
	},
	.rx_wbm_rel = {
		ATH11K_RX_WBM_REL_RING_MASK_0,
	},
	.reo_status = {
		0, 0, 0,
		ATH11K_REO_STATUS_RING_MASK_0,
	},
	.rxdma2host = {
		ATH11K_RXDMA2HOST_RING_MASK_0,
		ATH11K_RXDMA2HOST_RING_MASK_1,
		ATH11K_RXDMA2HOST_RING_MASK_2,
	},
	.host2rxdma = {
		ATH11K_HOST2RXDMA_RING_MASK_0,
		ATH11K_HOST2RXDMA_RING_MASK_1,
		ATH11K_HOST2RXDMA_RING_MASK_2,
	},
};

const struct ath11k_hw_ring_mask ath11k_hw_ring_mask_qca6390 = {
	.tx  = {
		ATH11K_TX_RING_MASK_0,
	},
	.rx_mon_status = {
		0, 0, 0, 0,
		ATH11K_RX_MON_STATUS_RING_MASK_0,
		ATH11K_RX_MON_STATUS_RING_MASK_1,
		ATH11K_RX_MON_STATUS_RING_MASK_2,
	},
	.rx = {
		0, 0, 0, 0, 0, 0, 0,
		ATH11K_RX_RING_MASK_0,
		ATH11K_RX_RING_MASK_1,
		ATH11K_RX_RING_MASK_2,
		ATH11K_RX_RING_MASK_3,
	},
	.rx_err = {
		ATH11K_RX_ERR_RING_MASK_0,
	},
	.rx_wbm_rel = {
		ATH11K_RX_WBM_REL_RING_MASK_0,
	},
	.reo_status = {
		ATH11K_REO_STATUS_RING_MASK_0,
	},
	.rxdma2host = {
		ATH11K_RXDMA2HOST_RING_MASK_0,
		ATH11K_RXDMA2HOST_RING_MASK_1,
		ATH11K_RXDMA2HOST_RING_MASK_2,
	},
	.host2rxdma = {
	},
};

/* Target firmware's Copy Engine configuration. */
const struct ce_pipe_config ath11k_target_ce_config_wlan_ipq8074[] = {
	/* CE0: host->target HTC control and raw streams */
	{
		.pipenum = __cpu_to_le32(0),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE1: target->host HTT + HTC control */
	{
		.pipenum = __cpu_to_le32(1),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE2: target->host WMI */
	{
		.pipenum = __cpu_to_le32(2),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE3: host->target WMI */
	{
		.pipenum = __cpu_to_le32(3),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE4: host->target HTT */
	{
		.pipenum = __cpu_to_le32(4),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(256),
		.nbytes_max = __cpu_to_le32(256),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS | CE_ATTR_DIS_INTR),
		.reserved = __cpu_to_le32(0),
	},

	/* CE5: target->host Pktlog */
	{
		.pipenum = __cpu_to_le32(5),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(0),
		.reserved = __cpu_to_le32(0),
	},

	/* CE6: Reserved for target autonomous hif_memcpy */
	{
		.pipenum = __cpu_to_le32(6),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(65535),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE7 used only by Host */
	{
		.pipenum = __cpu_to_le32(7),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE8 target->host used only by IPA */
	{
		.pipenum = __cpu_to_le32(8),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(65535),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE9 host->target HTT */
	{
		.pipenum = __cpu_to_le32(9),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE10 target->host HTT */
	{
		.pipenum = __cpu_to_le32(10),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT_H2H),
		.nentries = __cpu_to_le32(0),
		.nbytes_max = __cpu_to_le32(0),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE11 Not used */
};

/* Map from service/endpoint to Copy Engine.
 * This table is derived from the CE_PCI TABLE, above.
 * It is passed to the Target at startup for use by firmware.
 */
const struct service_to_pipe ath11k_target_service_to_ce_map_wlan_ipq8074[] = {
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VO),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(3),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VO),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BK),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(3),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BK),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BE),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(3),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BE),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VI),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(3),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VI),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_CONTROL),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(3),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_CONTROL),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_CONTROL_MAC1),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(7),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_CONTROL_MAC1),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_CONTROL_MAC2),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(9),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_CONTROL_MAC2),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_RSVD_CTRL),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(0),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_RSVD_CTRL),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(1),
	},
	{ /* not used */
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_TEST_RAW_STREAMS),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(0),
	},
	{ /* not used */
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_TEST_RAW_STREAMS),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(1),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_HTT_DATA_MSG),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(4),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_HTT_DATA_MSG),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(1),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_PKT_LOG),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(5),
	},

	/* (Additions here) */

	{ /* terminator entry */ }
};

const struct service_to_pipe ath11k_target_service_to_ce_map_wlan_ipq6018[] = {
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VO),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(3),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VO),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BK),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(3),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BK),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BE),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(3),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BE),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VI),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(3),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VI),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_CONTROL),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(3),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_CONTROL),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_CONTROL_MAC1),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(7),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_CONTROL_MAC1),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_RSVD_CTRL),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(0),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_RSVD_CTRL),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(1),
	},
	{ /* not used */
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_TEST_RAW_STREAMS),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(0),
	},
	{ /* not used */
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_TEST_RAW_STREAMS),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(1),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_HTT_DATA_MSG),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(4),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_HTT_DATA_MSG),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(1),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_PKT_LOG),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(5),
	},

	/* (Additions here) */

	{ /* terminator entry */ }
};

/* Target firmware's Copy Engine configuration. */
const struct ce_pipe_config ath11k_target_ce_config_wlan_qca6390[] = {
	/* CE0: host->target HTC control and raw streams */
	{
		.pipenum = __cpu_to_le32(0),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE1: target->host HTT + HTC control */
	{
		.pipenum = __cpu_to_le32(1),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE2: target->host WMI */
	{
		.pipenum = __cpu_to_le32(2),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE3: host->target WMI */
	{
		.pipenum = __cpu_to_le32(3),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE4: host->target HTT */
	{
		.pipenum = __cpu_to_le32(4),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(256),
		.nbytes_max = __cpu_to_le32(256),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS | CE_ATTR_DIS_INTR),
		.reserved = __cpu_to_le32(0),
	},

	/* CE5: target->host Pktlog */
	{
		.pipenum = __cpu_to_le32(5),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE6: Reserved for target autonomous hif_memcpy */
	{
		.pipenum = __cpu_to_le32(6),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(16384),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE7 used only by Host */
	{
		.pipenum = __cpu_to_le32(7),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT_H2H),
		.nentries = __cpu_to_le32(0),
		.nbytes_max = __cpu_to_le32(0),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS | CE_ATTR_DIS_INTR),
		.reserved = __cpu_to_le32(0),
	},

	/* CE8 target->host used only by IPA */
	{
		.pipenum = __cpu_to_le32(8),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(16384),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},
	/* CE 9, 10, 11 are used by MHI driver */
};

/* Map from service/endpoint to Copy Engine.
 * This table is derived from the CE_PCI TABLE, above.
 * It is passed to the Target at startup for use by firmware.
 */
const struct service_to_pipe ath11k_target_service_to_ce_map_wlan_qca6390[] = {
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VO),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VO),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BK),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BK),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BE),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BE),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VI),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VI),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_CONTROL),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_CONTROL),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_RSVD_CTRL),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(0),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_RSVD_CTRL),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_HTT_DATA_MSG),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(4),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_HTT_DATA_MSG),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(1),
	},

	/* (Additions here) */

	{ /* must be last */
		__cpu_to_le32(0),
		__cpu_to_le32(0),
		__cpu_to_le32(0),
	},
};

/* Target firmware's Copy Engine configuration. */
const struct ce_pipe_config ath11k_target_ce_config_wlan_qcn9074[] = {
	/* CE0: host->target HTC control and raw streams */
	{
		.pipenum = __cpu_to_le32(0),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE1: target->host HTT + HTC control */
	{
		.pipenum = __cpu_to_le32(1),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE2: target->host WMI */
	{
		.pipenum = __cpu_to_le32(2),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE3: host->target WMI */
	{
		.pipenum = __cpu_to_le32(3),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE4: host->target HTT */
	{
		.pipenum = __cpu_to_le32(4),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(256),
		.nbytes_max = __cpu_to_le32(256),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS | CE_ATTR_DIS_INTR),
		.reserved = __cpu_to_le32(0),
	},

	/* CE5: target->host Pktlog */
	{
		.pipenum = __cpu_to_le32(5),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE6: Reserved for target autonomous hif_memcpy */
	{
		.pipenum = __cpu_to_le32(6),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(16384),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE7 used only by Host */
	{
		.pipenum = __cpu_to_le32(7),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT_H2H),
		.nentries = __cpu_to_le32(0),
		.nbytes_max = __cpu_to_le32(0),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS | CE_ATTR_DIS_INTR),
		.reserved = __cpu_to_le32(0),
	},

	/* CE8 target->host used only by IPA */
	{
		.pipenum = __cpu_to_le32(8),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(16384),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},
	/* CE 9, 10, 11 are used by MHI driver */
};

/* Map from service/endpoint to Copy Engine.
 * This table is derived from the CE_PCI TABLE, above.
 * It is passed to the Target at startup for use by firmware.
 */
const struct service_to_pipe ath11k_target_service_to_ce_map_wlan_qcn9074[] = {
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VO),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VO),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BK),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BK),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BE),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BE),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VI),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VI),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_CONTROL),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_CONTROL),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_RSVD_CTRL),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(0),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_RSVD_CTRL),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(1),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_TEST_RAW_STREAMS),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(0),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_TEST_RAW_STREAMS),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(1),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_HTT_DATA_MSG),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(4),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_HTT_DATA_MSG),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(1),
	},
	{
		__cpu_to_le32(ATH11K_HTC_SVC_ID_PKT_LOG),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(5),
	},

	/* (Additions here) */

	{ /* must be last */
		__cpu_to_le32(0),
		__cpu_to_le32(0),
		__cpu_to_le32(0),
	},
};

const struct ath11k_hw_ring_mask ath11k_hw_ring_mask_qcn9074 = {
	.tx  = {
		ATH11K_TX_RING_MASK_0,
		ATH11K_TX_RING_MASK_1,
		ATH11K_TX_RING_MASK_2,
	},
	.rx_mon_status = {
		0, 0, 0,
		ATH11K_RX_MON_STATUS_RING_MASK_0,
		ATH11K_RX_MON_STATUS_RING_MASK_1,
		ATH11K_RX_MON_STATUS_RING_MASK_2,
	},
	.rx = {
		0, 0, 0, 0,
		ATH11K_RX_RING_MASK_0,
		ATH11K_RX_RING_MASK_1,
		ATH11K_RX_RING_MASK_2,
		ATH11K_RX_RING_MASK_3,
	},
	.rx_err = {
		0, 0, 0,
		ATH11K_RX_ERR_RING_MASK_0,
	},
	.rx_wbm_rel = {
		0, 0, 0,
		ATH11K_RX_WBM_REL_RING_MASK_0,
	},
	.reo_status = {
		0, 0, 0,
		ATH11K_REO_STATUS_RING_MASK_0,
	},
	.rxdma2host = {
		0, 0, 0,
		ATH11K_RXDMA2HOST_RING_MASK_0,
	},
	.host2rxdma = {
		0, 0, 0,
		ATH11K_HOST2RXDMA_RING_MASK_0,
	},
};

const struct ath11k_hw_ring_mask ath11k_hw_ring_mask_wcn6750 = {
	.tx  = {
		ATH11K_TX_RING_MASK_0,
		0,
		ATH11K_TX_RING_MASK_2,
		0,
		ATH11K_TX_RING_MASK_4,
	},
	.rx_mon_status = {
		0, 0, 0, 0, 0, 0,
		ATH11K_RX_MON_STATUS_RING_MASK_0,
	},
	.rx = {
		0, 0, 0, 0, 0, 0, 0,
		ATH11K_RX_RING_MASK_0,
		ATH11K_RX_RING_MASK_1,
		ATH11K_RX_RING_MASK_2,
		ATH11K_RX_RING_MASK_3,
	},
	.rx_err = {
		0, ATH11K_RX_ERR_RING_MASK_0,
	},
	.rx_wbm_rel = {
		0, ATH11K_RX_WBM_REL_RING_MASK_0,
	},
	.reo_status = {
		0, ATH11K_REO_STATUS_RING_MASK_0,
	},
	.rxdma2host = {
		ATH11K_RXDMA2HOST_RING_MASK_0,
		ATH11K_RXDMA2HOST_RING_MASK_1,
		ATH11K_RXDMA2HOST_RING_MASK_2,
	},
	.host2rxdma = {
	},
};

/* Target firmware's Copy Engine configuration for IPQ5018 */
const struct ce_pipe_config ath11k_target_ce_config_wlan_ipq5018[] = {
	/* CE0: host->target HTC control and raw streams */
	{
		.pipenum = __cpu_to_le32(0),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE1: target->host HTT + HTC control */
	{
		.pipenum = __cpu_to_le32(1),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE2: target->host WMI */
	{
		.pipenum = __cpu_to_le32(2),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE3: host->target WMI */
	{
		.pipenum = __cpu_to_le32(3),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE4: host->target HTT */
	{
		.pipenum = __cpu_to_le32(4),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(256),
		.nbytes_max = __cpu_to_le32(256),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS | CE_ATTR_DIS_INTR),
		.reserved = __cpu_to_le32(0),
	},

	/* CE5: target->host Pktlog */
	{
		.pipenum = __cpu_to_le32(5),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE6: Reserved for target autonomous hif_memcpy */
	{
		.pipenum = __cpu_to_le32(6),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(16384),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE7 used only by Host */
	{
		.pipenum = __cpu_to_le32(7),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(0x2000),
		.reserved = __cpu_to_le32(0),
	},

	/* CE8 target->host used only by IPA */
	{
		.pipenum = __cpu_to_le32(8),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(16384),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},
};

/* Map from service/endpoint to Copy Engine for IPQ5018.
 * This table is derived from the CE TABLE, above.
 * It is passed to the Target at startup for use by firmware.
 */
const struct service_to_pipe ath11k_target_service_to_ce_map_wlan_ipq5018[] = {
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VO),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(3),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VO),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BK),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(3),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BK),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BE),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(3),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BE),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VI),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(3),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VI),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_CONTROL),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(3),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_CONTROL),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},

	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_RSVD_CTRL),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(0),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_RSVD_CTRL),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(1),
	},

	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_TEST_RAW_STREAMS),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(0),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_TEST_RAW_STREAMS),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(1),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_HTT_DATA_MSG),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(4),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_HTT_DATA_MSG),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(1),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_PKT_LOG),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(5),
	},

       /* (Additions here) */

	{ /* terminator entry */ }
};

const struct ce_ie_addr ath11k_ce_ie_addr_ipq8074 = {
	.ie1_reg_addr = CE_HOST_IE_ADDRESS,
	.ie2_reg_addr = CE_HOST_IE_2_ADDRESS,
	.ie3_reg_addr = CE_HOST_IE_3_ADDRESS,
};

const struct ce_ie_addr ath11k_ce_ie_addr_ipq5018 = {
	.ie1_reg_addr = CE_HOST_IPQ5018_IE_ADDRESS - HAL_IPQ5018_CE_WFSS_REG_BASE,
	.ie2_reg_addr = CE_HOST_IPQ5018_IE_2_ADDRESS - HAL_IPQ5018_CE_WFSS_REG_BASE,
	.ie3_reg_addr = CE_HOST_IPQ5018_IE_3_ADDRESS - HAL_IPQ5018_CE_WFSS_REG_BASE,
};

const struct ce_remap ath11k_ce_remap_ipq5018 = {
	.base = HAL_IPQ5018_CE_WFSS_REG_BASE,
	.size = HAL_IPQ5018_CE_SIZE,
};

const struct ath11k_hw_regs ipq8074_regs = {
	/* SW2TCL(x) R0 ring configuration address */
	.hal_tcl1_ring_base_lsb = 0x00000510,
	.hal_tcl1_ring_base_msb = 0x00000514,
	.hal_tcl1_ring_id = 0x00000518,
	.hal_tcl1_ring_misc = 0x00000520,
	.hal_tcl1_ring_tp_addr_lsb = 0x0000052c,
	.hal_tcl1_ring_tp_addr_msb = 0x00000530,
	.hal_tcl1_ring_consumer_int_setup_ix0 = 0x00000540,
	.hal_tcl1_ring_consumer_int_setup_ix1 = 0x00000544,
	.hal_tcl1_ring_msi1_base_lsb = 0x00000558,
	.hal_tcl1_ring_msi1_base_msb = 0x0000055c,
	.hal_tcl1_ring_msi1_data = 0x00000560,
	.hal_tcl2_ring_base_lsb = 0x00000568,
	.hal_tcl_ring_base_lsb = 0x00000618,

	/* TCL STATUS ring address */
	.hal_tcl_status_ring_base_lsb = 0x00000720,

	/* REO2SW(x) R0 ring configuration address */
	.hal_reo1_ring_base_lsb = 0x0000029c,
	.hal_reo1_ring_base_msb = 0x000002a0,
	.hal_reo1_ring_id = 0x000002a4,
	.hal_reo1_ring_misc = 0x000002ac,
	.hal_reo1_ring_hp_addr_lsb = 0x000002b0,
	.hal_reo1_ring_hp_addr_msb = 0x000002b4,
	.hal_reo1_ring_producer_int_setup = 0x000002c0,
	.hal_reo1_ring_msi1_base_lsb = 0x000002e4,
	.hal_reo1_ring_msi1_base_msb = 0x000002e8,
	.hal_reo1_ring_msi1_data = 0x000002ec,
	.hal_reo2_ring_base_lsb = 0x000002f4,
	.hal_reo1_aging_thresh_ix_0 = 0x00000564,
	.hal_reo1_aging_thresh_ix_1 = 0x00000568,
	.hal_reo1_aging_thresh_ix_2 = 0x0000056c,
	.hal_reo1_aging_thresh_ix_3 = 0x00000570,

	/* REO2SW(x) R2 ring pointers (head/tail) address */
	.hal_reo1_ring_hp = 0x00003038,
	.hal_reo1_ring_tp = 0x0000303c,
	.hal_reo2_ring_hp = 0x00003040,

	/* REO2TCL R0 ring configuration address */
	.hal_reo_tcl_ring_base_lsb = 0x000003fc,
	.hal_reo_tcl_ring_hp = 0x00003058,

	/* REO CMD ring address */
	.hal_reo_cmd_ring_base_lsb = 0x00000194,
	.hal_reo_cmd_ring_hp = 0x00003020,

	/* REO status address */
	.hal_reo_status_ring_base_lsb = 0x00000504,
	.hal_reo_status_hp = 0x00003070,

	/* SW2REO ring address */
	.hal_sw2reo_ring_base_lsb = 0x000001ec,
	.hal_sw2reo_ring_hp = 0x00003028,

	/* WCSS relative address */
	.hal_seq_wcss_umac_ce0_src_reg = 0x00a00000,
	.hal_seq_wcss_umac_ce0_dst_reg = 0x00a01000,
	.hal_seq_wcss_umac_ce1_src_reg = 0x00a02000,
	.hal_seq_wcss_umac_ce1_dst_reg = 0x00a03000,

	/* WBM Idle address */
	.hal_wbm_idle_link_ring_base_lsb = 0x00000860,
	.hal_wbm_idle_link_ring_misc = 0x00000870,

	/* SW2WBM release address */
	.hal_wbm_release_ring_base_lsb = 0x000001d8,

	/* WBM2SW release address */
	.hal_wbm0_release_ring_base_lsb = 0x00000910,
	.hal_wbm1_release_ring_base_lsb = 0x00000968,

	/* PCIe base address */
	.pcie_qserdes_sysclk_en_sel = 0x0,
	.pcie_pcs_osc_dtct_config_base = 0x0,

	/* Shadow register area */
	.hal_shadow_base_addr = 0x0,

	/* REO misc control register, not used in IPQ8074 */
	.hal_reo1_misc_ctl = 0x0,
};

const struct ath11k_hw_regs qca6390_regs = {
	/* SW2TCL(x) R0 ring configuration address */
	.hal_tcl1_ring_base_lsb = 0x00000684,
	.hal_tcl1_ring_base_msb = 0x00000688,
	.hal_tcl1_ring_id = 0x0000068c,
	.hal_tcl1_ring_misc = 0x00000694,
	.hal_tcl1_ring_tp_addr_lsb = 0x000006a0,
	.hal_tcl1_ring_tp_addr_msb = 0x000006a4,
	.hal_tcl1_ring_consumer_int_setup_ix0 = 0x000006b4,
	.hal_tcl1_ring_consumer_int_setup_ix1 = 0x000006b8,
	.hal_tcl1_ring_msi1_base_lsb = 0x000006cc,
	.hal_tcl1_ring_msi1_base_msb = 0x000006d0,
	.hal_tcl1_ring_msi1_data = 0x000006d4,
	.hal_tcl2_ring_base_lsb = 0x000006dc,
	.hal_tcl_ring_base_lsb = 0x0000078c,

	/* TCL STATUS ring address */
	.hal_tcl_status_ring_base_lsb = 0x00000894,

	/* REO2SW(x) R0 ring configuration address */
	.hal_reo1_ring_base_lsb = 0x00000244,
	.hal_reo1_ring_base_msb = 0x00000248,
	.hal_reo1_ring_id = 0x0000024c,
	.hal_reo1_ring_misc = 0x00000254,
	.hal_reo1_ring_hp_addr_lsb = 0x00000258,
	.hal_reo1_ring_hp_addr_msb = 0x0000025c,
	.hal_reo1_ring_producer_int_setup = 0x00000268,
	.hal_reo1_ring_msi1_base_lsb = 0x0000028c,
	.hal_reo1_ring_msi1_base_msb = 0x00000290,
	.hal_reo1_ring_msi1_data = 0x00000294,
	.hal_reo2_ring_base_lsb = 0x0000029c,
	.hal_reo1_aging_thresh_ix_0 = 0x0000050c,
	.hal_reo1_aging_thresh_ix_1 = 0x00000510,
	.hal_reo1_aging_thresh_ix_2 = 0x00000514,
	.hal_reo1_aging_thresh_ix_3 = 0x00000518,

	/* REO2SW(x) R2 ring pointers (head/tail) address */
	.hal_reo1_ring_hp = 0x00003030,
	.hal_reo1_ring_tp = 0x00003034,
	.hal_reo2_ring_hp = 0x00003038,

	/* REO2TCL R0 ring configuration address */
	.hal_reo_tcl_ring_base_lsb = 0x000003a4,
	.hal_reo_tcl_ring_hp = 0x00003050,

	/* REO CMD ring address */
	.hal_reo_cmd_ring_base_lsb = 0x00000194,
	.hal_reo_cmd_ring_hp = 0x00003020,

	/* REO status address */
	.hal_reo_status_ring_base_lsb = 0x000004ac,
	.hal_reo_status_hp = 0x00003068,

	/* SW2REO ring address */
	.hal_sw2reo_ring_base_lsb = 0x000001ec,
	.hal_sw2reo_ring_hp = 0x00003028,

	/* WCSS relative address */
	.hal_seq_wcss_umac_ce0_src_reg = 0x00a00000,
	.hal_seq_wcss_umac_ce0_dst_reg = 0x00a01000,
	.hal_seq_wcss_umac_ce1_src_reg = 0x00a02000,
	.hal_seq_wcss_umac_ce1_dst_reg = 0x00a03000,

	/* WBM Idle address */
	.hal_wbm_idle_link_ring_base_lsb = 0x00000860,
	.hal_wbm_idle_link_ring_misc = 0x00000870,

	/* SW2WBM release address */
	.hal_wbm_release_ring_base_lsb = 0x000001d8,

	/* WBM2SW release address */
	.hal_wbm0_release_ring_base_lsb = 0x00000910,
	.hal_wbm1_release_ring_base_lsb = 0x00000968,

	/* PCIe base address */
	.pcie_qserdes_sysclk_en_sel = 0x01e0c0ac,
	.pcie_pcs_osc_dtct_config_base = 0x01e0c628,

	/* Shadow register area */
	.hal_shadow_base_addr = 0x000008fc,

	/* REO misc control register, not used in QCA6390 */
	.hal_reo1_misc_ctl = 0x0,
};

const struct ath11k_hw_regs qcn9074_regs = {
	/* SW2TCL(x) R0 ring configuration address */
	.hal_tcl1_ring_base_lsb = 0x000004f0,
	.hal_tcl1_ring_base_msb = 0x000004f4,
	.hal_tcl1_ring_id = 0x000004f8,
	.hal_tcl1_ring_misc = 0x00000500,
	.hal_tcl1_ring_tp_addr_lsb = 0x0000050c,
	.hal_tcl1_ring_tp_addr_msb = 0x00000510,
	.hal_tcl1_ring_consumer_int_setup_ix0 = 0x00000520,
	.hal_tcl1_ring_consumer_int_setup_ix1 = 0x00000524,
	.hal_tcl1_ring_msi1_base_lsb = 0x00000538,
	.hal_tcl1_ring_msi1_base_msb = 0x0000053c,
	.hal_tcl1_ring_msi1_data = 0x00000540,
	.hal_tcl2_ring_base_lsb = 0x00000548,
	.hal_tcl_ring_base_lsb = 0x000005f8,

	/* TCL STATUS ring address */
	.hal_tcl_status_ring_base_lsb = 0x00000700,

	/* REO2SW(x) R0 ring configuration address */
	.hal_reo1_ring_base_lsb = 0x0000029c,
	.hal_reo1_ring_base_msb = 0x000002a0,
	.hal_reo1_ring_id = 0x000002a4,
	.hal_reo1_ring_misc = 0x000002ac,
	.hal_reo1_ring_hp_addr_lsb = 0x000002b0,
	.hal_reo1_ring_hp_addr_msb = 0x000002b4,
	.hal_reo1_ring_producer_int_setup = 0x000002c0,
	.hal_reo1_ring_msi1_base_lsb = 0x000002e4,
	.hal_reo1_ring_msi1_base_msb = 0x000002e8,
	.hal_reo1_ring_msi1_data = 0x000002ec,
	.hal_reo2_ring_base_lsb = 0x000002f4,
	.hal_reo1_aging_thresh_ix_0 = 0x00000564,
	.hal_reo1_aging_thresh_ix_1 = 0x00000568,
	.hal_reo1_aging_thresh_ix_2 = 0x0000056c,
	.hal_reo1_aging_thresh_ix_3 = 0x00000570,

	/* REO2SW(x) R2 ring pointers (head/tail) address */
	.hal_reo1_ring_hp = 0x00003038,
	.hal_reo1_ring_tp = 0x0000303c,
	.hal_reo2_ring_hp = 0x00003040,

	/* REO2TCL R0 ring configuration address */
	.hal_reo_tcl_ring_base_lsb = 0x000003fc,
	.hal_reo_tcl_ring_hp = 0x00003058,

	/* REO CMD ring address */
	.hal_reo_cmd_ring_base_lsb = 0x00000194,
	.hal_reo_cmd_ring_hp = 0x00003020,

	/* REO status address */
	.hal_reo_status_ring_base_lsb = 0x00000504,
	.hal_reo_status_hp = 0x00003070,

	/* SW2REO ring address */
	.hal_sw2reo_ring_base_lsb = 0x000001ec,
	.hal_sw2reo_ring_hp = 0x00003028,

	/* WCSS relative address */
	.hal_seq_wcss_umac_ce0_src_reg = 0x01b80000,
	.hal_seq_wcss_umac_ce0_dst_reg = 0x01b81000,
	.hal_seq_wcss_umac_ce1_src_reg = 0x01b82000,
	.hal_seq_wcss_umac_ce1_dst_reg = 0x01b83000,

	/* WBM Idle address */
	.hal_wbm_idle_link_ring_base_lsb = 0x00000874,
	.hal_wbm_idle_link_ring_misc = 0x00000884,

	/* SW2WBM release address */
	.hal_wbm_release_ring_base_lsb = 0x000001ec,

	/* WBM2SW release address */
	.hal_wbm0_release_ring_base_lsb = 0x00000924,
	.hal_wbm1_release_ring_base_lsb = 0x0000097c,

	/* PCIe base address */
	.pcie_qserdes_sysclk_en_sel = 0x01e0e0a8,
	.pcie_pcs_osc_dtct_config_base = 0x01e0f45c,

	/* Shadow register area */
	.hal_shadow_base_addr = 0x0,

	/* REO misc control register, not used in QCN9074 */
	.hal_reo1_misc_ctl = 0x0,
};

const struct ath11k_hw_regs wcn6855_regs = {
	/* SW2TCL(x) R0 ring configuration address */
	.hal_tcl1_ring_base_lsb = 0x00000690,
	.hal_tcl1_ring_base_msb = 0x00000694,
	.hal_tcl1_ring_id = 0x00000698,
	.hal_tcl1_ring_misc = 0x000006a0,
	.hal_tcl1_ring_tp_addr_lsb = 0x000006ac,
	.hal_tcl1_ring_tp_addr_msb = 0x000006b0,
	.hal_tcl1_ring_consumer_int_setup_ix0 = 0x000006c0,
	.hal_tcl1_ring_consumer_int_setup_ix1 = 0x000006c4,
	.hal_tcl1_ring_msi1_base_lsb = 0x000006d8,
	.hal_tcl1_ring_msi1_base_msb = 0x000006dc,
	.hal_tcl1_ring_msi1_data = 0x000006e0,
	.hal_tcl2_ring_base_lsb = 0x000006e8,
	.hal_tcl_ring_base_lsb = 0x00000798,

	/* TCL STATUS ring address */
	.hal_tcl_status_ring_base_lsb = 0x000008a0,

	/* REO2SW(x) R0 ring configuration address */
	.hal_reo1_ring_base_lsb = 0x00000244,
	.hal_reo1_ring_base_msb = 0x00000248,
	.hal_reo1_ring_id = 0x0000024c,
	.hal_reo1_ring_misc = 0x00000254,
	.hal_reo1_ring_hp_addr_lsb = 0x00000258,
	.hal_reo1_ring_hp_addr_msb = 0x0000025c,
	.hal_reo1_ring_producer_int_setup = 0x00000268,
	.hal_reo1_ring_msi1_base_lsb = 0x0000028c,
	.hal_reo1_ring_msi1_base_msb = 0x00000290,
	.hal_reo1_ring_msi1_data = 0x00000294,
	.hal_reo2_ring_base_lsb = 0x0000029c,
	.hal_reo1_aging_thresh_ix_0 = 0x000005bc,
	.hal_reo1_aging_thresh_ix_1 = 0x000005c0,
	.hal_reo1_aging_thresh_ix_2 = 0x000005c4,
	.hal_reo1_aging_thresh_ix_3 = 0x000005c8,

	/* REO2SW(x) R2 ring pointers (head/tail) address */
	.hal_reo1_ring_hp = 0x00003030,
	.hal_reo1_ring_tp = 0x00003034,
	.hal_reo2_ring_hp = 0x00003038,

	/* REO2TCL R0 ring configuration address */
	.hal_reo_tcl_ring_base_lsb = 0x00000454,
	.hal_reo_tcl_ring_hp = 0x00003060,

	/* REO CMD ring address */
	.hal_reo_cmd_ring_base_lsb = 0x00000194,
	.hal_reo_cmd_ring_hp = 0x00003020,

	/* REO status address */
	.hal_reo_status_ring_base_lsb = 0x0000055c,
	.hal_reo_status_hp = 0x00003078,

	/* SW2REO ring address */
	.hal_sw2reo_ring_base_lsb = 0x000001ec,
	.hal_sw2reo_ring_hp = 0x00003028,

	/* WCSS relative address */
	.hal_seq_wcss_umac_ce0_src_reg = 0x1b80000,
	.hal_seq_wcss_umac_ce0_dst_reg = 0x1b81000,
	.hal_seq_wcss_umac_ce1_src_reg = 0x1b82000,
	.hal_seq_wcss_umac_ce1_dst_reg = 0x1b83000,

	/* WBM Idle address */
	.hal_wbm_idle_link_ring_base_lsb = 0x00000870,
	.hal_wbm_idle_link_ring_misc = 0x00000880,

	/* SW2WBM release address */
	.hal_wbm_release_ring_base_lsb = 0x000001e8,

	/* WBM2SW release address */
	.hal_wbm0_release_ring_base_lsb = 0x00000920,
	.hal_wbm1_release_ring_base_lsb = 0x00000978,

	/* PCIe base address */
	.pcie_qserdes_sysclk_en_sel = 0x01e0c0ac,
	.pcie_pcs_osc_dtct_config_base = 0x01e0c628,

	/* Shadow register area */
	.hal_shadow_base_addr = 0x000008fc,

	/* REO misc control register, used for fragment
	 * destination ring config in WCN6855.
	 */
	.hal_reo1_misc_ctl = 0x00000630,
};

const struct ath11k_hw_regs wcn6750_regs = {
	/* SW2TCL(x) R0 ring configuration address */
	.hal_tcl1_ring_base_lsb = 0x00000694,
	.hal_tcl1_ring_base_msb = 0x00000698,
	.hal_tcl1_ring_id = 0x0000069c,
	.hal_tcl1_ring_misc = 0x000006a4,
	.hal_tcl1_ring_tp_addr_lsb = 0x000006b0,
	.hal_tcl1_ring_tp_addr_msb = 0x000006b4,
	.hal_tcl1_ring_consumer_int_setup_ix0 = 0x000006c4,
	.hal_tcl1_ring_consumer_int_setup_ix1 = 0x000006c8,
	.hal_tcl1_ring_msi1_base_lsb = 0x000006dc,
	.hal_tcl1_ring_msi1_base_msb = 0x000006e0,
	.hal_tcl1_ring_msi1_data = 0x000006e4,
	.hal_tcl2_ring_base_lsb = 0x000006ec,
	.hal_tcl_ring_base_lsb = 0x0000079c,

	/* TCL STATUS ring address */
	.hal_tcl_status_ring_base_lsb = 0x000008a4,

	/* REO2SW(x) R0 ring configuration address */
	.hal_reo1_ring_base_lsb = 0x000001ec,
	.hal_reo1_ring_base_msb = 0x000001f0,
	.hal_reo1_ring_id = 0x000001f4,
	.hal_reo1_ring_misc = 0x000001fc,
	.hal_reo1_ring_hp_addr_lsb = 0x00000200,
	.hal_reo1_ring_hp_addr_msb = 0x00000204,
	.hal_reo1_ring_producer_int_setup = 0x00000210,
	.hal_reo1_ring_msi1_base_lsb = 0x00000234,
	.hal_reo1_ring_msi1_base_msb = 0x00000238,
	.hal_reo1_ring_msi1_data = 0x0000023c,
	.hal_reo2_ring_base_lsb = 0x00000244,
	.hal_reo1_aging_thresh_ix_0 = 0x00000564,
	.hal_reo1_aging_thresh_ix_1 = 0x00000568,
	.hal_reo1_aging_thresh_ix_2 = 0x0000056c,
	.hal_reo1_aging_thresh_ix_3 = 0x00000570,

	/* REO2SW(x) R2 ring pointers (head/tail) address */
	.hal_reo1_ring_hp = 0x00003028,
	.hal_reo1_ring_tp = 0x0000302c,
	.hal_reo2_ring_hp = 0x00003030,

	/* REO2TCL R0 ring configuration address */
	.hal_reo_tcl_ring_base_lsb = 0x000003fc,
	.hal_reo_tcl_ring_hp = 0x00003058,

	/* REO CMD ring address */
	.hal_reo_cmd_ring_base_lsb = 0x000000e4,
	.hal_reo_cmd_ring_hp = 0x00003010,

	/* REO status address */
	.hal_reo_status_ring_base_lsb = 0x00000504,
	.hal_reo_status_hp = 0x00003070,

	/* SW2REO ring address */
	.hal_sw2reo_ring_base_lsb = 0x0000013c,
	.hal_sw2reo_ring_hp = 0x00003018,

	/* WCSS relative address */
	.hal_seq_wcss_umac_ce0_src_reg = 0x01b80000,
	.hal_seq_wcss_umac_ce0_dst_reg = 0x01b81000,
	.hal_seq_wcss_umac_ce1_src_reg = 0x01b82000,
	.hal_seq_wcss_umac_ce1_dst_reg = 0x01b83000,

	/* WBM Idle address */
	.hal_wbm_idle_link_ring_base_lsb = 0x00000874,
	.hal_wbm_idle_link_ring_misc = 0x00000884,

	/* SW2WBM release address */
	.hal_wbm_release_ring_base_lsb = 0x000001ec,

	/* WBM2SW release address */
	.hal_wbm0_release_ring_base_lsb = 0x00000924,
	.hal_wbm1_release_ring_base_lsb = 0x0000097c,

	/* PCIe base address */
	.pcie_qserdes_sysclk_en_sel = 0x0,
	.pcie_pcs_osc_dtct_config_base = 0x0,

	/* Shadow register area */
	.hal_shadow_base_addr = 0x00000504,

	/* REO misc control register, used for fragment
	 * destination ring config in WCN6750.
	 */
	.hal_reo1_misc_ctl = 0x000005d8,
};

static const struct ath11k_hw_tcl2wbm_rbm_map ath11k_hw_tcl2wbm_rbm_map_ipq8074[] = {
	{
		.tcl_ring_num = 0,
		.wbm_ring_num = 0,
		.rbm_id = HAL_RX_BUF_RBM_SW0_BM,
	},
	{
		.tcl_ring_num = 1,
		.wbm_ring_num = 1,
		.rbm_id = HAL_RX_BUF_RBM_SW1_BM,
	},
	{
		.tcl_ring_num = 2,
		.wbm_ring_num = 2,
		.rbm_id = HAL_RX_BUF_RBM_SW2_BM,
	},
};

static const struct ath11k_hw_tcl2wbm_rbm_map ath11k_hw_tcl2wbm_rbm_map_wcn6750[] = {
	{
		.tcl_ring_num = 0,
		.wbm_ring_num = 0,
		.rbm_id = HAL_RX_BUF_RBM_SW0_BM,
	},
	{
		.tcl_ring_num = 1,
		.wbm_ring_num = 4,
		.rbm_id = HAL_RX_BUF_RBM_SW4_BM,
	},
	{
		.tcl_ring_num = 2,
		.wbm_ring_num = 2,
		.rbm_id = HAL_RX_BUF_RBM_SW2_BM,
	},
};

const struct ath11k_hw_regs ipq5018_regs = {
	/* SW2TCL(x) R0 ring configuration address */
	.hal_tcl1_ring_base_lsb = 0x00000694,
	.hal_tcl1_ring_base_msb = 0x00000698,
	.hal_tcl1_ring_id =	0x0000069c,
	.hal_tcl1_ring_misc = 0x000006a4,
	.hal_tcl1_ring_tp_addr_lsb = 0x000006b0,
	.hal_tcl1_ring_tp_addr_msb = 0x000006b4,
	.hal_tcl1_ring_consumer_int_setup_ix0 = 0x000006c4,
	.hal_tcl1_ring_consumer_int_setup_ix1 = 0x000006c8,
	.hal_tcl1_ring_msi1_base_lsb = 0x000006dc,
	.hal_tcl1_ring_msi1_base_msb = 0x000006e0,
	.hal_tcl1_ring_msi1_data = 0x000006e4,
	.hal_tcl2_ring_base_lsb = 0x000006ec,
	.hal_tcl_ring_base_lsb = 0x0000079c,

	/* TCL STATUS ring address */
	.hal_tcl_status_ring_base_lsb = 0x000008a4,

	/* REO2SW(x) R0 ring configuration address */
	.hal_reo1_ring_base_lsb = 0x000001ec,
	.hal_reo1_ring_base_msb = 0x000001f0,
	.hal_reo1_ring_id = 0x000001f4,
	.hal_reo1_ring_misc = 0x000001fc,
	.hal_reo1_ring_hp_addr_lsb = 0x00000200,
	.hal_reo1_ring_hp_addr_msb = 0x00000204,
	.hal_reo1_ring_producer_int_setup = 0x00000210,
	.hal_reo1_ring_msi1_base_lsb = 0x00000234,
	.hal_reo1_ring_msi1_base_msb = 0x00000238,
	.hal_reo1_ring_msi1_data = 0x0000023c,
	.hal_reo2_ring_base_lsb = 0x00000244,
	.hal_reo1_aging_thresh_ix_0 = 0x00000564,
	.hal_reo1_aging_thresh_ix_1 = 0x00000568,
	.hal_reo1_aging_thresh_ix_2 = 0x0000056c,
	.hal_reo1_aging_thresh_ix_3 = 0x00000570,

	/* REO2SW(x) R2 ring pointers (head/tail) address */
	.hal_reo1_ring_hp = 0x00003028,
	.hal_reo1_ring_tp = 0x0000302c,
	.hal_reo2_ring_hp = 0x00003030,

	/* REO2TCL R0 ring configuration address */
	.hal_reo_tcl_ring_base_lsb = 0x000003fc,
	.hal_reo_tcl_ring_hp = 0x00003058,

	/* SW2REO ring address */
	.hal_sw2reo_ring_base_lsb = 0x0000013c,
	.hal_sw2reo_ring_hp = 0x00003018,

	/* REO CMD ring address */
	.hal_reo_cmd_ring_base_lsb = 0x000000e4,
	.hal_reo_cmd_ring_hp = 0x00003010,

	/* REO status address */
	.hal_reo_status_ring_base_lsb = 0x00000504,
	.hal_reo_status_hp = 0x00003070,

	/* WCSS relative address */
	.hal_seq_wcss_umac_ce0_src_reg = 0x08400000
		- HAL_IPQ5018_CE_WFSS_REG_BASE,
	.hal_seq_wcss_umac_ce0_dst_reg = 0x08401000
		- HAL_IPQ5018_CE_WFSS_REG_BASE,
	.hal_seq_wcss_umac_ce1_src_reg = 0x08402000
		- HAL_IPQ5018_CE_WFSS_REG_BASE,
	.hal_seq_wcss_umac_ce1_dst_reg = 0x08403000
		- HAL_IPQ5018_CE_WFSS_REG_BASE,

	/* WBM Idle address */
	.hal_wbm_idle_link_ring_base_lsb = 0x00000874,
	.hal_wbm_idle_link_ring_misc = 0x00000884,

	/* SW2WBM release address */
	.hal_wbm_release_ring_base_lsb = 0x000001ec,

	/* WBM2SW release address */
	.hal_wbm0_release_ring_base_lsb = 0x00000924,
	.hal_wbm1_release_ring_base_lsb = 0x0000097c,
};

const struct ath11k_hw_hal_params ath11k_hw_hal_params_ipq8074 = {
	.rx_buf_rbm = HAL_RX_BUF_RBM_SW3_BM,
	.tcl2wbm_rbm_map = ath11k_hw_tcl2wbm_rbm_map_ipq8074,
};

const struct ath11k_hw_hal_params ath11k_hw_hal_params_qca6390 = {
	.rx_buf_rbm = HAL_RX_BUF_RBM_SW1_BM,
	.tcl2wbm_rbm_map = ath11k_hw_tcl2wbm_rbm_map_ipq8074,
};

const struct ath11k_hw_hal_params ath11k_hw_hal_params_wcn6750 = {
	.rx_buf_rbm = HAL_RX_BUF_RBM_SW1_BM,
	.tcl2wbm_rbm_map = ath11k_hw_tcl2wbm_rbm_map_wcn6750,
};

static const struct cfg80211_sar_freq_ranges ath11k_hw_sar_freq_ranges_wcn6855[] = {
	{.start_freq = 2402, .end_freq = 2482 },  /* 2G ch1~ch13 */
	{.start_freq = 5150, .end_freq = 5250 },  /* 5G UNII-1 ch32~ch48 */
	{.start_freq = 5250, .end_freq = 5725 },  /* 5G UNII-2 ch50~ch144 */
	{.start_freq = 5725, .end_freq = 5810 },  /* 5G UNII-3 ch149~ch161 */
	{.start_freq = 5815, .end_freq = 5895 },  /* 5G UNII-4 ch163~ch177 */
	{.start_freq = 5925, .end_freq = 6165 },  /* 6G UNII-5 Ch1, Ch2 ~ Ch41 */
	{.start_freq = 6165, .end_freq = 6425 },  /* 6G UNII-5 ch45~ch93 */
	{.start_freq = 6425, .end_freq = 6525 },  /* 6G UNII-6 ch97~ch113 */
	{.start_freq = 6525, .end_freq = 6705 },  /* 6G UNII-7 ch117~ch149 */
	{.start_freq = 6705, .end_freq = 6875 },  /* 6G UNII-7 ch153~ch185 */
	{.start_freq = 6875, .end_freq = 7125 },  /* 6G UNII-8 ch189~ch233 */
};

const struct cfg80211_sar_capa ath11k_hw_sar_capa_wcn6855 = {
	.type = NL80211_SAR_TYPE_POWER,
	.num_freq_ranges = (ARRAY_SIZE(ath11k_hw_sar_freq_ranges_wcn6855)),
	.freq_ranges = ath11k_hw_sar_freq_ranges_wcn6855,
};
