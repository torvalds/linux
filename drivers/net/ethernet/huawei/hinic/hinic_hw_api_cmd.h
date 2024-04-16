/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#ifndef HINIC_HW_API_CMD_H
#define HINIC_HW_API_CMD_H

#include <linux/types.h>
#include <linux/semaphore.h>

#include "hinic_hw_if.h"

#define HINIC_API_CMD_PI_IDX_SHIFT                              0

#define HINIC_API_CMD_PI_IDX_MASK                               0xFFFFFF

#define HINIC_API_CMD_PI_SET(val, member)                       \
	(((u32)(val) & HINIC_API_CMD_PI_##member##_MASK) <<     \
	 HINIC_API_CMD_PI_##member##_SHIFT)

#define HINIC_API_CMD_PI_CLEAR(val, member)                     \
	((val) & (~(HINIC_API_CMD_PI_##member##_MASK            \
	 << HINIC_API_CMD_PI_##member##_SHIFT)))

#define HINIC_API_CMD_CHAIN_REQ_RESTART_SHIFT                   1

#define HINIC_API_CMD_CHAIN_REQ_RESTART_MASK                    0x1

#define HINIC_API_CMD_CHAIN_REQ_SET(val, member)                \
	(((u32)(val) & HINIC_API_CMD_CHAIN_REQ_##member##_MASK) << \
	 HINIC_API_CMD_CHAIN_REQ_##member##_SHIFT)

#define HINIC_API_CMD_CHAIN_REQ_GET(val, member)                \
	(((val) >> HINIC_API_CMD_CHAIN_REQ_##member##_SHIFT) &  \
	 HINIC_API_CMD_CHAIN_REQ_##member##_MASK)

#define HINIC_API_CMD_CHAIN_REQ_CLEAR(val, member)              \
	((val) & (~(HINIC_API_CMD_CHAIN_REQ_##member##_MASK     \
	 << HINIC_API_CMD_CHAIN_REQ_##member##_SHIFT)))

#define HINIC_API_CMD_CHAIN_CTRL_RESTART_WB_STAT_SHIFT          1
#define HINIC_API_CMD_CHAIN_CTRL_XOR_ERR_SHIFT                  2
#define HINIC_API_CMD_CHAIN_CTRL_AEQE_EN_SHIFT                  4
#define HINIC_API_CMD_CHAIN_CTRL_AEQ_ID_SHIFT                   8
#define HINIC_API_CMD_CHAIN_CTRL_XOR_CHK_EN_SHIFT               28
#define HINIC_API_CMD_CHAIN_CTRL_CELL_SIZE_SHIFT                30

#define HINIC_API_CMD_CHAIN_CTRL_RESTART_WB_STAT_MASK           0x1
#define HINIC_API_CMD_CHAIN_CTRL_XOR_ERR_MASK                   0x1
#define HINIC_API_CMD_CHAIN_CTRL_AEQE_EN_MASK                   0x1
#define HINIC_API_CMD_CHAIN_CTRL_AEQ_ID_MASK                    0x3
#define HINIC_API_CMD_CHAIN_CTRL_XOR_CHK_EN_MASK                0x3
#define HINIC_API_CMD_CHAIN_CTRL_CELL_SIZE_MASK                 0x3

#define HINIC_API_CMD_CHAIN_CTRL_SET(val, member)               \
	(((u32)(val) & HINIC_API_CMD_CHAIN_CTRL_##member##_MASK) << \
	 HINIC_API_CMD_CHAIN_CTRL_##member##_SHIFT)

#define HINIC_API_CMD_CHAIN_CTRL_CLEAR(val, member)             \
	((val) & (~(HINIC_API_CMD_CHAIN_CTRL_##member##_MASK    \
	 << HINIC_API_CMD_CHAIN_CTRL_##member##_SHIFT)))

#define HINIC_API_CMD_CELL_CTRL_DATA_SZ_SHIFT                   0
#define HINIC_API_CMD_CELL_CTRL_RD_DMA_ATTR_SHIFT               16
#define HINIC_API_CMD_CELL_CTRL_WR_DMA_ATTR_SHIFT               24
#define HINIC_API_CMD_CELL_CTRL_XOR_CHKSUM_SHIFT                56

#define HINIC_API_CMD_CELL_CTRL_DATA_SZ_MASK                    0x3F
#define HINIC_API_CMD_CELL_CTRL_RD_DMA_ATTR_MASK                0x3F
#define HINIC_API_CMD_CELL_CTRL_WR_DMA_ATTR_MASK                0x3F
#define HINIC_API_CMD_CELL_CTRL_XOR_CHKSUM_MASK                 0xFF

#define HINIC_API_CMD_CELL_CTRL_SET(val, member)                \
	((((u64)val) & HINIC_API_CMD_CELL_CTRL_##member##_MASK) << \
	 HINIC_API_CMD_CELL_CTRL_##member##_SHIFT)

#define HINIC_API_CMD_DESC_API_TYPE_SHIFT                       0
#define HINIC_API_CMD_DESC_RD_WR_SHIFT                          1
#define HINIC_API_CMD_DESC_MGMT_BYPASS_SHIFT                    2
#define HINIC_API_CMD_DESC_DEST_SHIFT                           32
#define HINIC_API_CMD_DESC_SIZE_SHIFT                           40
#define HINIC_API_CMD_DESC_XOR_CHKSUM_SHIFT                     56

#define HINIC_API_CMD_DESC_API_TYPE_MASK                        0x1
#define HINIC_API_CMD_DESC_RD_WR_MASK                           0x1
#define HINIC_API_CMD_DESC_MGMT_BYPASS_MASK                     0x1
#define HINIC_API_CMD_DESC_DEST_MASK                            0x1F
#define HINIC_API_CMD_DESC_SIZE_MASK                            0x7FF
#define HINIC_API_CMD_DESC_XOR_CHKSUM_MASK                      0xFF

#define HINIC_API_CMD_DESC_SET(val, member)                     \
	((((u64)val) & HINIC_API_CMD_DESC_##member##_MASK) <<   \
	 HINIC_API_CMD_DESC_##member##_SHIFT)

#define HINIC_API_CMD_STATUS_HEADER_CHAIN_ID_SHIFT              16

#define HINIC_API_CMD_STATUS_HEADER_CHAIN_ID_MASK               0xFF

#define HINIC_API_CMD_STATUS_HEADER_GET(val, member)            \
	(((val) >> HINIC_API_CMD_STATUS_HEADER_##member##_SHIFT) & \
	 HINIC_API_CMD_STATUS_HEADER_##member##_MASK)

#define HINIC_API_CMD_STATUS_CONS_IDX_SHIFT                     0
#define HINIC_API_CMD_STATUS_FSM_SHIFT				24
#define HINIC_API_CMD_STATUS_CHKSUM_ERR_SHIFT                   28
#define HINIC_API_CMD_STATUS_CPLD_ERR_SHIFT			30

#define HINIC_API_CMD_STATUS_CONS_IDX_MASK                      0xFFFFFF
#define HINIC_API_CMD_STATUS_FSM_MASK				0xFU
#define HINIC_API_CMD_STATUS_CHKSUM_ERR_MASK                    0x3
#define HINIC_API_CMD_STATUS_CPLD_ERR_MASK			0x1U

#define HINIC_API_CMD_STATUS_GET(val, member)                   \
	(((val) >> HINIC_API_CMD_STATUS_##member##_SHIFT) &     \
	 HINIC_API_CMD_STATUS_##member##_MASK)

enum hinic_api_cmd_chain_type {
	HINIC_API_CMD_WRITE_TO_MGMT_CPU = 2,

	HINIC_API_CMD_MAX,
};

struct hinic_api_cmd_chain_attr {
	struct hinic_hwif               *hwif;
	enum hinic_api_cmd_chain_type   chain_type;

	u32                             num_cells;
	u16                             cell_size;
};

struct hinic_api_cmd_status {
	u64     header;
	u32     status;
	u32     rsvd0;
	u32     rsvd1;
	u32     rsvd2;
	u64     rsvd3;
};

/* HW struct */
struct hinic_api_cmd_cell {
	u64 ctrl;

	/* address is 64 bit in HW struct */
	u64 next_cell_paddr;

	u64 desc;

	/* HW struct */
	union {
		struct {
			u64 hw_cmd_paddr;
		} write;

		struct {
			u64 hw_wb_resp_paddr;
			u64 hw_cmd_paddr;
		} read;
	};
};

struct hinic_api_cmd_cell_ctxt {
	dma_addr_t                      cell_paddr;
	struct hinic_api_cmd_cell       *cell_vaddr;

	dma_addr_t                      api_cmd_paddr;
	u8                              *api_cmd_vaddr;
};

struct hinic_api_cmd_chain {
	struct hinic_hwif               *hwif;
	enum hinic_api_cmd_chain_type   chain_type;

	u32                             num_cells;
	u16                             cell_size;

	/* HW members in 24 bit format */
	u32                             prod_idx;
	u32                             cons_idx;

	struct semaphore                sem;

	struct hinic_api_cmd_cell_ctxt  *cell_ctxt;

	dma_addr_t                      wb_status_paddr;
	struct hinic_api_cmd_status     *wb_status;

	dma_addr_t                      head_cell_paddr;
	struct hinic_api_cmd_cell       *head_node;
	struct hinic_api_cmd_cell       *curr_node;
};

int hinic_api_cmd_write(struct hinic_api_cmd_chain *chain,
			enum hinic_node_id dest, u8 *cmd, u16 size);

int hinic_api_cmd_init(struct hinic_api_cmd_chain **chain,
		       struct hinic_hwif *hwif);

void hinic_api_cmd_free(struct hinic_api_cmd_chain **chain);

#endif
