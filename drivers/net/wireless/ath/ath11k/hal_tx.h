/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */

#ifndef ATH11K_HAL_TX_H
#define ATH11K_HAL_TX_H

#include "hal_desc.h"
#include "core.h"

#define HAL_TX_ADDRX_EN			1
#define HAL_TX_ADDRY_EN			2

#define HAL_TX_ADDR_SEARCH_DEFAULT	0
#define HAL_TX_ADDR_SEARCH_INDEX	1

struct hal_tx_info {
	u16 meta_data_flags; /* %HAL_TCL_DATA_CMD_INFO0_META_ */
	u8 ring_id;
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
	u8 tid;
	u8 search_type; /* %HAL_TX_ADDR_SEARCH_ */
	u8 lmac_id;
	u8 dscp_tid_tbl_idx;
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
	u8 ack_rssi;
	u32 flags; /* %HAL_TX_STATUS_FLAGS_ */
	u32 ppdu_id;
	u8 try_cnt;
	u8 tid;
	u16 peer_id;
	u32 rate_stats;
};

void ath11k_hal_tx_cmd_desc_setup(struct ath11k_base *ab, void *cmd,
				  struct hal_tx_info *ti);
void ath11k_hal_tx_set_dscp_tid_map(struct ath11k_base *ab, int id);
int ath11k_hal_reo_cmd_send(struct ath11k_base *ab, struct hal_srng *srng,
			    enum hal_reo_cmd_type type,
			    struct ath11k_hal_reo_cmd *cmd);
void ath11k_hal_tx_init_data_ring(struct ath11k_base *ab,
				  struct hal_srng *srng);
#endif
