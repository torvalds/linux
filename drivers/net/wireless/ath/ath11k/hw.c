// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2020 The Linux Foundation. All rights reserved.
 */

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/bitfield.h>

#include "hw.h"
#include "core.h"
#include "ce.h"

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
}

static void ath11k_init_wmi_config_ipq8074(struct ath11k_base *ab,
					   struct target_resource_config *config)
{
	config->num_vdevs = ab->num_radios * TARGET_NUM_VDEVS;

	if (ab->num_radios == 2) {
		config->num_peers = TARGET_NUM_PEERS(DBS);
		config->num_tids = TARGET_NUM_TIDS(DBS);
	} else if (ab->num_radios == 3) {
		config->num_peers = TARGET_NUM_PEERS(DBS_SBS);
		config->num_tids = TARGET_NUM_TIDS(DBS_SBS);
	} else {
		/* Control should not reach here */
		config->num_peers = TARGET_NUM_PEERS(SINGLE);
		config->num_tids = TARGET_NUM_TIDS(SINGLE);
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

const struct ath11k_hw_ops ipq8074_ops = {
	.get_hw_mac_from_pdev_id = ath11k_hw_ipq8074_mac_from_pdev_id,
	.wmi_init_config = ath11k_init_wmi_config_ipq8074,
	.mac_id_to_pdev_id = ath11k_hw_mac_id_to_pdev_id_ipq8074,
	.mac_id_to_srng_id = ath11k_hw_mac_id_to_srng_id_ipq8074,
};

const struct ath11k_hw_ops ipq6018_ops = {
	.get_hw_mac_from_pdev_id = ath11k_hw_ipq6018_mac_from_pdev_id,
	.wmi_init_config = ath11k_init_wmi_config_ipq8074,
	.mac_id_to_pdev_id = ath11k_hw_mac_id_to_pdev_id_ipq8074,
	.mac_id_to_srng_id = ath11k_hw_mac_id_to_srng_id_ipq8074,
};

const struct ath11k_hw_ops qca6390_ops = {
	.get_hw_mac_from_pdev_id = ath11k_hw_ipq8074_mac_from_pdev_id,
	.wmi_init_config = ath11k_init_wmi_config_qca6390,
	.mac_id_to_pdev_id = ath11k_hw_mac_id_to_pdev_id_qca6390,
	.mac_id_to_srng_id = ath11k_hw_mac_id_to_srng_id_qca6390,
};

#define ATH11K_TX_RING_MASK_0 0x1
#define ATH11K_TX_RING_MASK_1 0x2
#define ATH11K_TX_RING_MASK_2 0x4

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

	/* REO status address */
	.hal_reo_status_ring_base_lsb = 0x00000504,
	.hal_reo_status_hp = 0x00003070,

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

	/* REO status address */
	.hal_reo_status_ring_base_lsb = 0x000004ac,
	.hal_reo_status_hp = 0x00003068,
};
