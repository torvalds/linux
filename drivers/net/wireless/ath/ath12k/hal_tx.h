/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef ATH12K_HAL_TX_H
#define ATH12K_HAL_TX_H

#include "hal_desc.h"
#include "core.h"

#define HAL_TX_ADDRX_EN			1
#define HAL_TX_ADDRY_EN			2

#define HAL_TX_ADDR_SEARCH_DEFAULT	0
#define HAL_TX_ADDR_SEARCH_INDEX	1

/* TODO: check all these data can be managed with struct ath12k_tx_desc_info for perf */
struct hal_tx_info {
	u16 meta_data_flags; /* %HAL_TCL_DATA_CMD_INFO0_META_ */
	u8 ring_id;
	u8 rbm_id;
	u32 desc_id;
	enum hal_tcl_desc_type type;
	enum hal_tcl_encap_type encap_type;
	dma_addr_t paddr;
	u32 data_len;
	u32 pkt_offset;
	enum hal_encrypt_type encrypt_type;
	u32 flags0; /* %HAL_TCL_DATA_CMD_INFO1_ */
	u32 flags1; /* %HAL_TCL_DATA_CMD_INFO2_ */
	u16 addr_search_flags; /* %HAL_TCL_DATA_CMD_INFO0_ADDR(X/Y)_ */
	u16 bss_ast_hash;
	u16 bss_ast_idx;
	u8 tid;
	u8 search_type; /* %HAL_TX_ADDR_SEARCH_ */
	u8 lmac_id;
	u8 vdev_id;
	u8 dscp_tid_tbl_idx;
	bool enable_mesh;
	int bank_id;
};

/* TODO: Check if the actual desc macros can be used instead */
#define HAL_TX_STATUS_FLAGS_FIRST_MSDU		BIT(0)
#define HAL_TX_STATUS_FLAGS_LAST_MSDU		BIT(1)
#define HAL_TX_STATUS_FLAGS_MSDU_IN_AMSDU	BIT(2)
#define HAL_TX_STATUS_FLAGS_RATE_STATS_VALID	BIT(3)
#define HAL_TX_STATUS_FLAGS_RATE_LDPC		BIT(4)
#define HAL_TX_STATUS_FLAGS_RATE_STBC		BIT(5)
#define HAL_TX_STATUS_FLAGS_OFDMA		BIT(6)

#define HAL_TX_STATUS_DESC_LEN		sizeof(struct hal_wbm_release_ring)

/* Tx status parsed from srng desc */
struct hal_tx_status {
	enum hal_wbm_rel_src_module buf_rel_source;
	enum hal_wbm_tqm_rel_reason status;
	s8 ack_rssi;
	u32 flags; /* %HAL_TX_STATUS_FLAGS_ */
	u32 ppdu_id;
	u8 try_cnt;
	u8 tid;
	u16 peer_id;
	u32 rate_stats;
};

#define HAL_TX_PHY_DESC_INFO0_BF_TYPE		GENMASK(17, 16)
#define HAL_TX_PHY_DESC_INFO0_PREAMBLE_11B	BIT(20)
#define HAL_TX_PHY_DESC_INFO0_PKT_TYPE		GENMASK(24, 21)
#define HAL_TX_PHY_DESC_INFO0_BANDWIDTH		GENMASK(30, 28)
#define HAL_TX_PHY_DESC_INFO1_MCS		GENMASK(3, 0)
#define HAL_TX_PHY_DESC_INFO1_STBC		BIT(6)
#define HAL_TX_PHY_DESC_INFO2_NSS		GENMASK(23, 21)
#define HAL_TX_PHY_DESC_INFO3_AP_PKT_BW		GENMASK(6, 4)
#define HAL_TX_PHY_DESC_INFO3_LTF_SIZE		GENMASK(20, 19)
#define HAL_TX_PHY_DESC_INFO3_ACTIVE_CHANNEL	GENMASK(17, 15)

struct hal_tx_phy_desc {
	__le32 info0;
	__le32 info1;
	__le32 info2;
	__le32 info3;
} __packed;

#define HAL_TX_FES_STAT_PROT_INFO0_STRT_FRM_TS_15_0	GENMASK(15, 0)
#define HAL_TX_FES_STAT_PROT_INFO0_STRT_FRM_TS_31_16	GENMASK(31, 16)
#define HAL_TX_FES_STAT_PROT_INFO1_END_FRM_TS_15_0	GENMASK(15, 0)
#define HAL_TX_FES_STAT_PROT_INFO1_END_FRM_TS_31_16	GENMASK(31, 16)

struct hal_tx_fes_status_prot {
	__le64 reserved;
	__le32 info0;
	__le32 info1;
	__le32 reserved1[11];
} __packed;

#define HAL_TX_FES_STAT_USR_PPDU_INFO0_DURATION		GENMASK(15, 0)

struct hal_tx_fes_status_user_ppdu {
	__le64 reserved;
	__le32 info0;
	__le32 reserved1[3];
} __packed;

#define HAL_TX_FES_STAT_STRT_INFO0_PROT_TS_LOWER_32	GENMASK(31, 0)
#define HAL_TX_FES_STAT_STRT_INFO1_PROT_TS_UPPER_32	GENMASK(31, 0)

struct hal_tx_fes_status_start_prot {
	__le32 info0;
	__le32 info1;
	__le64 reserved;
} __packed;

#define HAL_TX_FES_STATUS_START_INFO0_MEDIUM_PROT_TYPE	GENMASK(29, 27)

struct hal_tx_fes_status_start {
	__le32 reserved;
	__le32 info0;
	__le64 reserved1;
} __packed;

#define HAL_TX_Q_EXT_INFO0_FRAME_CTRL		GENMASK(15, 0)
#define HAL_TX_Q_EXT_INFO0_QOS_CTRL		GENMASK(31, 16)
#define HAL_TX_Q_EXT_INFO1_AMPDU_FLAG		BIT(0)

struct hal_tx_queue_exten {
	__le32 info0;
	__le32 info1;
} __packed;

#define HAL_TX_FES_SETUP_INFO0_NUM_OF_USERS	GENMASK(28, 23)

struct hal_tx_fes_setup {
	__le32 schedule_id;
	__le32 info0;
	__le64 reserved;
} __packed;

#define HAL_TX_PPDU_SETUP_INFO0_MEDIUM_PROT_TYPE	GENMASK(2, 0)
#define HAL_TX_PPDU_SETUP_INFO1_PROT_FRAME_ADDR1_31_0	GENMASK(31, 0)
#define HAL_TX_PPDU_SETUP_INFO2_PROT_FRAME_ADDR1_47_32	GENMASK(15, 0)
#define HAL_TX_PPDU_SETUP_INFO2_PROT_FRAME_ADDR2_15_0	GENMASK(31, 16)
#define HAL_TX_PPDU_SETUP_INFO3_PROT_FRAME_ADDR2_47_16	GENMASK(31, 0)
#define HAL_TX_PPDU_SETUP_INFO4_PROT_FRAME_ADDR3_31_0	GENMASK(31, 0)
#define HAL_TX_PPDU_SETUP_INFO5_PROT_FRAME_ADDR3_47_32	GENMASK(15, 0)
#define HAL_TX_PPDU_SETUP_INFO5_PROT_FRAME_ADDR4_15_0	GENMASK(31, 16)
#define HAL_TX_PPDU_SETUP_INFO6_PROT_FRAME_ADDR4_47_16	GENMASK(31, 0)

struct hal_tx_pcu_ppdu_setup_init {
	__le32 info0;
	__le32 info1;
	__le32 info2;
	__le32 info3;
	__le32 reserved;
	__le32 info4;
	__le32 info5;
	__le32 info6;
} __packed;

#define HAL_TX_FES_STATUS_END_INFO0_START_TIMESTAMP_15_0	GENMASK(15, 0)
#define HAL_TX_FES_STATUS_END_INFO0_START_TIMESTAMP_31_16	GENMASK(31, 16)

struct hal_tx_fes_status_end {
	__le32 reserved[2];
	__le32 info0;
	__le32 reserved1[19];
} __packed;

#define HAL_TX_BANK_CONFIG_EPD			BIT(0)
#define HAL_TX_BANK_CONFIG_ENCAP_TYPE		GENMASK(2, 1)
#define HAL_TX_BANK_CONFIG_ENCRYPT_TYPE		GENMASK(6, 3)
#define HAL_TX_BANK_CONFIG_SRC_BUFFER_SWAP	BIT(7)
#define HAL_TX_BANK_CONFIG_LINK_META_SWAP	BIT(8)
#define HAL_TX_BANK_CONFIG_INDEX_LOOKUP_EN	BIT(9)
#define HAL_TX_BANK_CONFIG_ADDRX_EN		BIT(10)
#define HAL_TX_BANK_CONFIG_ADDRY_EN		BIT(11)
#define HAL_TX_BANK_CONFIG_MESH_EN		GENMASK(13, 12)
#define HAL_TX_BANK_CONFIG_VDEV_ID_CHECK_EN	BIT(14)
#define HAL_TX_BANK_CONFIG_PMAC_ID		GENMASK(16, 15)
/* STA mode will have MCAST_PKT_CTRL instead of DSCP_TID_MAP bitfield */
#define HAL_TX_BANK_CONFIG_DSCP_TIP_MAP_ID	GENMASK(22, 17)

void ath12k_hal_tx_cmd_desc_setup(struct ath12k_base *ab,
				  struct hal_tcl_data_cmd *tcl_cmd,
				  struct hal_tx_info *ti);
void ath12k_hal_tx_set_dscp_tid_map(struct ath12k_base *ab, int id);
int ath12k_hal_reo_cmd_send(struct ath12k_base *ab, struct hal_srng *srng,
			    enum hal_reo_cmd_type type,
			    struct ath12k_hal_reo_cmd *cmd);
void ath12k_hal_tx_configure_bank_register(struct ath12k_base *ab, u32 bank_config,
					   u8 bank_id);
#endif
