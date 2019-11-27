// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */

#include "ahb.h"
#include "hal.h"
#include "hal_tx.h"

#define DSCP_TID_MAP_TBL_ENTRY_SIZE 64

/* dscp_tid_map - Default DSCP-TID mapping
 *
 * DSCP        TID
 * 000000      0
 * 001000      1
 * 010000      2
 * 011000      3
 * 100000      4
 * 101000      5
 * 110000      6
 * 111000      7
 */
static const u8 dscp_tid_map[DSCP_TID_MAP_TBL_ENTRY_SIZE] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 1,
	2, 2, 2, 2, 2, 2, 2, 2,
	3, 3, 3, 3, 3, 3, 3, 3,
	4, 4, 4, 4, 4, 4, 4, 4,
	5, 5, 5, 5, 5, 5, 5, 5,
	6, 6, 6, 6, 6, 6, 6, 6,
	7, 7, 7, 7, 7, 7, 7, 7,
};

void ath11k_hal_tx_cmd_desc_setup(struct ath11k_base *ab, void *cmd,
				  struct hal_tx_info *ti)
{
	struct hal_tcl_data_cmd *tcl_cmd = (struct hal_tcl_data_cmd *)cmd;

	tcl_cmd->buf_addr_info.info0 =
		FIELD_PREP(BUFFER_ADDR_INFO0_ADDR, ti->paddr);
	tcl_cmd->buf_addr_info.info1 =
		FIELD_PREP(BUFFER_ADDR_INFO1_ADDR,
			   ((uint64_t)ti->paddr >> HAL_ADDR_MSB_REG_SHIFT));
	tcl_cmd->buf_addr_info.info1 |=
		FIELD_PREP(BUFFER_ADDR_INFO1_RET_BUF_MGR,
			   (ti->ring_id + HAL_RX_BUF_RBM_SW0_BM)) |
		FIELD_PREP(BUFFER_ADDR_INFO1_SW_COOKIE, ti->desc_id);

	tcl_cmd->info0 =
		FIELD_PREP(HAL_TCL_DATA_CMD_INFO0_DESC_TYPE, ti->type) |
		FIELD_PREP(HAL_TCL_DATA_CMD_INFO0_ENCAP_TYPE, ti->encap_type) |
		FIELD_PREP(HAL_TCL_DATA_CMD_INFO0_ENCRYPT_TYPE,
			   ti->encrypt_type) |
		FIELD_PREP(HAL_TCL_DATA_CMD_INFO0_SEARCH_TYPE,
			   ti->search_type) |
		FIELD_PREP(HAL_TCL_DATA_CMD_INFO0_ADDR_EN,
			   ti->addr_search_flags) |
		FIELD_PREP(HAL_TCL_DATA_CMD_INFO0_CMD_NUM,
			   ti->meta_data_flags);

	tcl_cmd->info1 = ti->flags0 |
		FIELD_PREP(HAL_TCL_DATA_CMD_INFO1_DATA_LEN, ti->data_len) |
		FIELD_PREP(HAL_TCL_DATA_CMD_INFO1_PKT_OFFSET, ti->pkt_offset);

	tcl_cmd->info2 = ti->flags1 |
		FIELD_PREP(HAL_TCL_DATA_CMD_INFO2_TID, ti->tid) |
		FIELD_PREP(HAL_TCL_DATA_CMD_INFO2_LMAC_ID, ti->lmac_id);

	tcl_cmd->info3 = FIELD_PREP(HAL_TCL_DATA_CMD_INFO3_DSCP_TID_TABLE_IDX,
				    ti->dscp_tid_tbl_idx) |
			 FIELD_PREP(HAL_TCL_DATA_CMD_INFO3_SEARCH_INDEX,
				    ti->bss_ast_hash);
}

/* Commit the descriptor to hardware */
void ath11k_hal_tx_desc_sync(void *tx_desc_cached, void *hw_desc)
{
	memcpy(hw_desc + sizeof(struct hal_tlv_hdr), tx_desc_cached,
	       sizeof(struct hal_tcl_data_cmd));
}

/* Get the descriptor status from hardware */
void ath11k_hal_tx_status_desc_sync(void *hw_desc, void *local_desc)
{
	memcpy(local_desc, hw_desc, HAL_TX_STATUS_DESC_LEN);
}

void ath11k_hal_tx_status_parse(struct ath11k_base *ab,
				struct hal_wbm_release_ring *desc,
				struct hal_tx_status *ts)
{
	ts->buf_rel_source =
		FIELD_GET(HAL_WBM_RELEASE_INFO0_REL_SRC_MODULE, desc->info0);
	if (ts->buf_rel_source != HAL_WBM_REL_SRC_MODULE_FW &&
	    ts->buf_rel_source != HAL_WBM_REL_SRC_MODULE_TQM)
		return;

	ts->desc_id = FIELD_GET(BUFFER_ADDR_INFO1_SW_COOKIE,
				desc->buf_addr_info.info1);

	if (ts->buf_rel_source == HAL_WBM_REL_SRC_MODULE_FW)
		return;

	ts->status = FIELD_GET(HAL_WBM_RELEASE_INFO0_TQM_RELEASE_REASON,
			       desc->info0);
	ts->ppdu_id = FIELD_GET(HAL_WBM_RELEASE_INFO1_TQM_STATUS_NUMBER,
				desc->info1);
	ts->try_cnt = FIELD_GET(HAL_WBM_RELEASE_INFO1_TRANSMIT_COUNT,
				desc->info1);

	ts->ack_rssi = FIELD_GET(HAL_WBM_RELEASE_INFO2_ACK_FRAME_RSSI,
				 desc->info2);
	if (desc->info2 & HAL_WBM_RELEASE_INFO2_FIRST_MSDU)
		ts->flags |= HAL_TX_STATUS_FLAGS_FIRST_MSDU;

	if (desc->info2 & HAL_WBM_RELEASE_INFO2_LAST_MSDU)
		ts->flags |= HAL_TX_STATUS_FLAGS_LAST_MSDU;

	if (desc->info2 & HAL_WBM_RELEASE_INFO2_MSDU_IN_AMSDU)
		ts->flags |= HAL_TX_STATUS_FLAGS_MSDU_IN_AMSDU;

	ts->peer_id = FIELD_GET(HAL_WBM_RELEASE_INFO3_PEER_ID, desc->info3);
	ts->tid = FIELD_GET(HAL_WBM_RELEASE_INFO3_TID, desc->info3);

	if (!(desc->rate_stats.info0 & HAL_TX_RATE_STATS_INFO0_VALID))
		return;

	ts->flags |= HAL_TX_STATUS_FLAGS_RATE_STATS_VALID;
	ts->tsf = desc->rate_stats.tsf;
	ts->bw = FIELD_GET(HAL_TX_RATE_STATS_INFO0_BW, desc->rate_stats.info0);
	ts->pkt_type = FIELD_GET(HAL_TX_RATE_STATS_INFO0_PKT_TYPE,
				 desc->rate_stats.info0);
	if (desc->rate_stats.info0 & HAL_TX_RATE_STATS_INFO0_STBC)
		ts->flags |= HAL_TX_STATUS_FLAGS_RATE_STBC;
	if (desc->rate_stats.info0 & HAL_TX_RATE_STATS_INFO0_LDPC)
		ts->flags |= HAL_TX_STATUS_FLAGS_RATE_LDPC;
	if (desc->rate_stats.info0 & HAL_TX_RATE_STATS_INFO0_OFDMA_TX)
		ts->flags |= HAL_TX_STATUS_FLAGS_OFDMA;

	ts->sgi = FIELD_GET(HAL_TX_RATE_STATS_INFO0_SGI,
			    desc->rate_stats.info0);
	ts->mcs = FIELD_GET(HAL_TX_RATE_STATS_INFO0_MCS,
			    desc->rate_stats.info0);
	ts->num_tones_in_ru = FIELD_GET(HAL_TX_RATE_STATS_INFO0_TONES_IN_RU,
					desc->rate_stats.info0);
}

void ath11k_hal_tx_set_dscp_tid_map(struct ath11k_base *ab, int id)
{
	u32 ctrl_reg_val;
	u32 addr;
	u8 hw_map_val[HAL_DSCP_TID_TBL_SIZE];
	int i;
	u32 value;
	int cnt = 0;

	ctrl_reg_val = ath11k_ahb_read32(ab, HAL_SEQ_WCSS_UMAC_TCL_REG +
					 HAL_TCL1_RING_CMN_CTRL_REG);
	/* Enable read/write access */
	ctrl_reg_val |= HAL_TCL1_RING_CMN_CTRL_DSCP_TID_MAP_PROG_EN;
	ath11k_ahb_write32(ab, HAL_SEQ_WCSS_UMAC_TCL_REG +
			   HAL_TCL1_RING_CMN_CTRL_REG, ctrl_reg_val);

	addr = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL1_RING_DSCP_TID_MAP +
	       (4 * id * (HAL_DSCP_TID_TBL_SIZE / 4));

	/* Configure each DSCP-TID mapping in three bits there by configure
	 * three bytes in an iteration.
	 */
	for (i = 0; i < DSCP_TID_MAP_TBL_ENTRY_SIZE; i += 8) {
		value = FIELD_PREP(HAL_TCL1_RING_FIELD_DSCP_TID_MAP0,
				   dscp_tid_map[i]) |
			FIELD_PREP(HAL_TCL1_RING_FIELD_DSCP_TID_MAP1,
				   dscp_tid_map[i + 1]) |
			FIELD_PREP(HAL_TCL1_RING_FIELD_DSCP_TID_MAP2,
				   dscp_tid_map[i + 2]) |
			FIELD_PREP(HAL_TCL1_RING_FIELD_DSCP_TID_MAP3,
				   dscp_tid_map[i + 3]) |
			FIELD_PREP(HAL_TCL1_RING_FIELD_DSCP_TID_MAP4,
				   dscp_tid_map[i + 4]) |
			FIELD_PREP(HAL_TCL1_RING_FIELD_DSCP_TID_MAP5,
				   dscp_tid_map[i + 5]) |
			FIELD_PREP(HAL_TCL1_RING_FIELD_DSCP_TID_MAP6,
				   dscp_tid_map[i + 6]) |
			FIELD_PREP(HAL_TCL1_RING_FIELD_DSCP_TID_MAP7,
				   dscp_tid_map[i + 7]);
		memcpy(&hw_map_val[cnt], (u8 *)&value, 3);
		cnt += 3;
	}

	for (i = 0; i < HAL_DSCP_TID_TBL_SIZE; i += 4) {
		ath11k_ahb_write32(ab, addr, *(u32 *)&hw_map_val[i]);
		addr += 4;
	}

	/* Disable read/write access */
	ctrl_reg_val = ath11k_ahb_read32(ab, HAL_SEQ_WCSS_UMAC_TCL_REG +
					 HAL_TCL1_RING_CMN_CTRL_REG);
	ctrl_reg_val &= ~HAL_TCL1_RING_CMN_CTRL_DSCP_TID_MAP_PROG_EN;
	ath11k_ahb_write32(ab, HAL_SEQ_WCSS_UMAC_TCL_REG +
			   HAL_TCL1_RING_CMN_CTRL_REG,
			   ctrl_reg_val);
}

void ath11k_hal_tx_init_data_ring(struct ath11k_base *ab, struct hal_srng *srng)
{
	struct hal_srng_params params;
	struct hal_tlv_hdr *tlv;
	int i, entry_size;
	u8 *desc;

	memset(&params, 0, sizeof(params));

	entry_size = ath11k_hal_srng_get_entrysize(HAL_TCL_DATA);
	ath11k_hal_srng_get_params(ab, srng, &params);
	desc = (u8 *)params.ring_base_vaddr;

	for (i = 0; i < params.num_entries; i++) {
		tlv = (struct hal_tlv_hdr *)desc;
		tlv->tl = FIELD_PREP(HAL_TLV_HDR_TAG, HAL_TCL_DATA_CMD) |
			  FIELD_PREP(HAL_TLV_HDR_LEN,
				     sizeof(struct hal_tcl_data_cmd));
		desc += entry_size;
	}
}
