/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */

#ifndef ATH11K_HW_H
#define ATH11K_HW_H

/* Target configuration defines */

/* Num VDEVS per radio */
#define TARGET_NUM_VDEVS	(16 + 1)

#define TARGET_NUM_PEERS_PDEV	(512 + TARGET_NUM_VDEVS)

/* Num of peers for Single Radio mode */
#define TARGET_NUM_PEERS_SINGLE		(TARGET_NUM_PEERS_PDEV)

/* Num of peers for DBS */
#define TARGET_NUM_PEERS_DBS		(2 * TARGET_NUM_PEERS_PDEV)

/* Num of peers for DBS_SBS */
#define TARGET_NUM_PEERS_DBS_SBS	(3 * TARGET_NUM_PEERS_PDEV)

/* Max num of stations (per radio) */
#define TARGET_NUM_STATIONS	512

#define TARGET_NUM_PEERS(x)	TARGET_NUM_PEERS_##x
#define TARGET_NUM_PEER_KEYS	2
#define TARGET_NUM_TIDS(x)	(2 * TARGET_NUM_PEERS(x) + \
				 4 * TARGET_NUM_VDEVS + 8)

#define TARGET_AST_SKID_LIMIT	16
#define TARGET_NUM_OFFLD_PEERS	4
#define TARGET_NUM_OFFLD_REORDER_BUFFS 4

#define TARGET_TX_CHAIN_MASK	(BIT(0) | BIT(1) | BIT(2) | BIT(4))
#define TARGET_RX_CHAIN_MASK	(BIT(0) | BIT(1) | BIT(2) | BIT(4))
#define TARGET_RX_TIMEOUT_LO_PRI	100
#define TARGET_RX_TIMEOUT_HI_PRI	40

#define TARGET_DECAP_MODE_RAW		0
#define TARGET_DECAP_MODE_NATIVE_WIFI	1
#define TARGET_DECAP_MODE_ETH		2

#define TARGET_SCAN_MAX_PENDING_REQS	4
#define TARGET_BMISS_OFFLOAD_MAX_VDEV	3
#define TARGET_ROAM_OFFLOAD_MAX_VDEV	3
#define TARGET_ROAM_OFFLOAD_MAX_AP_PROFILES	8
#define TARGET_GTK_OFFLOAD_MAX_VDEV	3
#define TARGET_NUM_MCAST_GROUPS		12
#define TARGET_NUM_MCAST_TABLE_ELEMS	64
#define TARGET_MCAST2UCAST_MODE		2
#define TARGET_TX_DBG_LOG_SIZE		1024
#define TARGET_RX_SKIP_DEFRAG_TIMEOUT_DUP_DETECTION_CHECK 1
#define TARGET_VOW_CONFIG		0
#define TARGET_NUM_MSDU_DESC		(2500)
#define TARGET_MAX_FRAG_ENTRIES		6
#define TARGET_MAX_BCN_OFFLD		16
#define TARGET_NUM_WDS_ENTRIES		32
#define TARGET_DMA_BURST_SIZE		1
#define TARGET_RX_BATCHMODE		1

#define ATH11K_HW_MAX_QUEUES		4
#define ATH11K_QUEUE_LEN		4096

#define ATH11k_HW_RATECODE_CCK_SHORT_PREAM_MASK  0x4

#define ATH11K_FW_DIR			"ath11k"

/* IPQ8074 definitions */
#define IPQ8074_FW_DIR			"IPQ8074"
#define IPQ8074_MAX_BOARD_DATA_SZ	(256 * 1024)
#define IPQ8074_MAX_CAL_DATA_SZ		IPQ8074_MAX_BOARD_DATA_SZ

#define ATH11K_BOARD_MAGIC		"QCA-ATH11K-BOARD"
#define ATH11K_BOARD_API2_FILE		"board-2.bin"
#define ATH11K_DEFAULT_BOARD_FILE	"bdwlan.bin"
#define ATH11K_DEFAULT_CAL_FILE		"caldata.bin"

enum ath11k_hw_rate_cck {
	ATH11K_HW_RATE_CCK_LP_11M = 0,
	ATH11K_HW_RATE_CCK_LP_5_5M,
	ATH11K_HW_RATE_CCK_LP_2M,
	ATH11K_HW_RATE_CCK_LP_1M,
	ATH11K_HW_RATE_CCK_SP_11M,
	ATH11K_HW_RATE_CCK_SP_5_5M,
	ATH11K_HW_RATE_CCK_SP_2M,
};

enum ath11k_hw_rate_ofdm {
	ATH11K_HW_RATE_OFDM_48M = 0,
	ATH11K_HW_RATE_OFDM_24M,
	ATH11K_HW_RATE_OFDM_12M,
	ATH11K_HW_RATE_OFDM_6M,
	ATH11K_HW_RATE_OFDM_54M,
	ATH11K_HW_RATE_OFDM_36M,
	ATH11K_HW_RATE_OFDM_18M,
	ATH11K_HW_RATE_OFDM_9M,
};

enum ath11k_bus {
	ATH11K_BUS_AHB,
	ATH11K_BUS_PCI,
};

struct ath11k_hw_params {
	const char *name;
	struct {
		const char *dir;
		size_t board_size;
		size_t cal_size;
	} fw;
};

struct ath11k_fw_ie {
	__le32 id;
	__le32 len;
	u8 data[];
};

enum ath11k_bd_ie_board_type {
	ATH11K_BD_IE_BOARD_NAME = 0,
	ATH11K_BD_IE_BOARD_DATA = 1,
};

enum ath11k_bd_ie_type {
	/* contains sub IEs of enum ath11k_bd_ie_board_type */
	ATH11K_BD_IE_BOARD = 0,
	ATH11K_BD_IE_BOARD_EXT = 1,
};

#endif
