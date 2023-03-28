// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "hal_desc.h"
#include "hal.h"
#include "hal_tx.h"
#include "hif.h"

#define DSCP_TID_MAP_TBL_ENTRY_SIZE 64

/* dscp_tid_map - Default DSCP-TID mapping
 *=================
 * DSCP        TID
 *=================
 * 000xxx      0
 * 001xxx      1
 * 010xxx      2
 * 011xxx      3
 * 100xxx      4
 * 101xxx      5
 * 110xxx      6
 * 111xxx      7
 */
static inline u8 dscp2tid(u8 dscp)
{
	return dscp >> 3;
}

void ath12k_hal_tx_cmd_desc_setup(struct ath12k_base *ab,
				  struct hal_tcl_data_cmd *tcl_cmd,
				  struct hal_tx_info *ti)
{
	tcl_cmd->buf_addr_info.info0 =
		le32_encode_bits(ti->paddr, BUFFER_ADDR_INFO0_ADDR);
	tcl_cmd->buf_addr_info.info1 =
		le32_encode_bits(((uint64_t)ti->paddr >> HAL_ADDR_MSB_REG_SHIFT),
				 BUFFER_ADDR_INFO1_ADDR);
	tcl_cmd->buf_addr_info.info1 |=
		le32_encode_bits((ti->rbm_id), BUFFER_ADDR_INFO1_RET_BUF_MGR) |
		le32_encode_bits(ti->desc_id, BUFFER_ADDR_INFO1_SW_COOKIE);

	tcl_cmd->info0 =
		le32_encode_bits(ti->type, HAL_TCL_DATA_CMD_INFO0_DESC_TYPE) |
		le32_encode_bits(ti->bank_id, HAL_TCL_DATA_CMD_INFO0_BANK_ID);

	tcl_cmd->info1 =
		le32_encode_bits(ti->meta_data_flags,
				 HAL_TCL_DATA_CMD_INFO1_CMD_NUM);

	tcl_cmd->info2 = cpu_to_le32(ti->flags0) |
		le32_encode_bits(ti->data_len, HAL_TCL_DATA_CMD_INFO2_DATA_LEN) |
		le32_encode_bits(ti->pkt_offset, HAL_TCL_DATA_CMD_INFO2_PKT_OFFSET);

	tcl_cmd->info3 = cpu_to_le32(ti->flags1) |
		le32_encode_bits(ti->tid, HAL_TCL_DATA_CMD_INFO3_TID) |
		le32_encode_bits(ti->lmac_id, HAL_TCL_DATA_CMD_INFO3_PMAC_ID) |
		le32_encode_bits(ti->vdev_id, HAL_TCL_DATA_CMD_INFO3_VDEV_ID);

	tcl_cmd->info4 = le32_encode_bits(ti->bss_ast_idx,
					  HAL_TCL_DATA_CMD_INFO4_SEARCH_INDEX) |
			 le32_encode_bits(ti->bss_ast_hash,
					  HAL_TCL_DATA_CMD_INFO4_CACHE_SET_NUM);
	tcl_cmd->info5 = 0;
}

void ath12k_hal_tx_set_dscp_tid_map(struct ath12k_base *ab, int id)
{
	u32 ctrl_reg_val;
	u32 addr;
	u8 hw_map_val[HAL_DSCP_TID_TBL_SIZE], dscp, tid;
	int i;
	u32 value;

	ctrl_reg_val = ath12k_hif_read32(ab, HAL_SEQ_WCSS_UMAC_TCL_REG +
					 HAL_TCL1_RING_CMN_CTRL_REG);
	/* Enable read/write access */
	ctrl_reg_val |= HAL_TCL1_RING_CMN_CTRL_DSCP_TID_MAP_PROG_EN;
	ath12k_hif_write32(ab, HAL_SEQ_WCSS_UMAC_TCL_REG +
			   HAL_TCL1_RING_CMN_CTRL_REG, ctrl_reg_val);

	addr = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL1_RING_DSCP_TID_MAP +
	       (4 * id * (HAL_DSCP_TID_TBL_SIZE / 4));

	/* Configure each DSCP-TID mapping in three bits there by configure
	 * three bytes in an iteration.
	 */
	for (i = 0, dscp = 0; i < HAL_DSCP_TID_TBL_SIZE; i += 3) {
		tid = dscp2tid(dscp);
		value = u32_encode_bits(tid, HAL_TCL1_RING_FIELD_DSCP_TID_MAP0);
		dscp++;

		tid = dscp2tid(dscp);
		value |= u32_encode_bits(tid, HAL_TCL1_RING_FIELD_DSCP_TID_MAP1);
		dscp++;

		tid = dscp2tid(dscp);
		value |= u32_encode_bits(tid, HAL_TCL1_RING_FIELD_DSCP_TID_MAP2);
		dscp++;

		tid = dscp2tid(dscp);
		value |= u32_encode_bits(tid, HAL_TCL1_RING_FIELD_DSCP_TID_MAP3);
		dscp++;

		tid = dscp2tid(dscp);
		value |= u32_encode_bits(tid, HAL_TCL1_RING_FIELD_DSCP_TID_MAP4);
		dscp++;

		tid = dscp2tid(dscp);
		value |= u32_encode_bits(tid, HAL_TCL1_RING_FIELD_DSCP_TID_MAP5);
		dscp++;

		tid = dscp2tid(dscp);
		value |= u32_encode_bits(tid, HAL_TCL1_RING_FIELD_DSCP_TID_MAP6);
		dscp++;

		tid = dscp2tid(dscp);
		value |= u32_encode_bits(tid, HAL_TCL1_RING_FIELD_DSCP_TID_MAP7);
		dscp++;

		memcpy(&hw_map_val[i], &value, 3);
	}

	for (i = 0; i < HAL_DSCP_TID_TBL_SIZE; i += 4) {
		ath12k_hif_write32(ab, addr, *(u32 *)&hw_map_val[i]);
		addr += 4;
	}

	/* Disable read/write access */
	ctrl_reg_val = ath12k_hif_read32(ab, HAL_SEQ_WCSS_UMAC_TCL_REG +
					 HAL_TCL1_RING_CMN_CTRL_REG);
	ctrl_reg_val &= ~HAL_TCL1_RING_CMN_CTRL_DSCP_TID_MAP_PROG_EN;
	ath12k_hif_write32(ab, HAL_SEQ_WCSS_UMAC_TCL_REG +
			   HAL_TCL1_RING_CMN_CTRL_REG,
			   ctrl_reg_val);
}

void ath12k_hal_tx_configure_bank_register(struct ath12k_base *ab, u32 bank_config,
					   u8 bank_id)
{
	ath12k_hif_write32(ab, HAL_TCL_SW_CONFIG_BANK_ADDR + 4 * bank_id,
			   bank_config);
}
