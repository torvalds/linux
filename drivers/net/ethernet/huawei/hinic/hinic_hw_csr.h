/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#ifndef HINIC_HW_CSR_H
#define HINIC_HW_CSR_H

/* HW interface registers */
#define HINIC_CSR_FUNC_ATTR0_ADDR                       0x0
#define HINIC_CSR_FUNC_ATTR1_ADDR                       0x4

#define HINIC_CSR_FUNC_ATTR4_ADDR                       0x10
#define HINIC_CSR_FUNC_ATTR5_ADDR                       0x14

#define HINIC_DMA_ATTR_BASE                             0xC80
#define HINIC_ELECTION_BASE                             0x4200

#define HINIC_DMA_ATTR_STRIDE                           0x4
#define HINIC_CSR_DMA_ATTR_ADDR(idx)                    \
	(HINIC_DMA_ATTR_BASE + (idx) * HINIC_DMA_ATTR_STRIDE)

#define HINIC_PPF_ELECTION_STRIDE                       0x4
#define HINIC_CSR_MAX_PORTS                             4

#define HINIC_CSR_PPF_ELECTION_ADDR(idx)                \
	(HINIC_ELECTION_BASE +  (idx) * HINIC_PPF_ELECTION_STRIDE)

/* API CMD registers */
#define HINIC_CSR_API_CMD_BASE                          0xF000

#define HINIC_CSR_API_CMD_STRIDE                        0x100

#define HINIC_CSR_API_CMD_CHAIN_HEAD_HI_ADDR(idx)       \
	(HINIC_CSR_API_CMD_BASE + 0x0 + (idx) * HINIC_CSR_API_CMD_STRIDE)

#define HINIC_CSR_API_CMD_CHAIN_HEAD_LO_ADDR(idx)       \
	(HINIC_CSR_API_CMD_BASE + 0x4 + (idx) * HINIC_CSR_API_CMD_STRIDE)

#define HINIC_CSR_API_CMD_STATUS_HI_ADDR(idx)           \
	(HINIC_CSR_API_CMD_BASE + 0x8 + (idx) * HINIC_CSR_API_CMD_STRIDE)

#define HINIC_CSR_API_CMD_STATUS_LO_ADDR(idx)           \
	(HINIC_CSR_API_CMD_BASE + 0xC + (idx) * HINIC_CSR_API_CMD_STRIDE)

#define HINIC_CSR_API_CMD_CHAIN_NUM_CELLS_ADDR(idx)     \
	(HINIC_CSR_API_CMD_BASE + 0x10 + (idx) * HINIC_CSR_API_CMD_STRIDE)

#define HINIC_CSR_API_CMD_CHAIN_CTRL_ADDR(idx)          \
	(HINIC_CSR_API_CMD_BASE + 0x14 + (idx) * HINIC_CSR_API_CMD_STRIDE)

#define HINIC_CSR_API_CMD_CHAIN_PI_ADDR(idx)            \
	(HINIC_CSR_API_CMD_BASE + 0x1C + (idx) * HINIC_CSR_API_CMD_STRIDE)

#define HINIC_CSR_API_CMD_CHAIN_REQ_ADDR(idx)           \
	(HINIC_CSR_API_CMD_BASE + 0x20 + (idx) * HINIC_CSR_API_CMD_STRIDE)

#define HINIC_CSR_API_CMD_STATUS_ADDR(idx)              \
	(HINIC_CSR_API_CMD_BASE + 0x30 + (idx) * HINIC_CSR_API_CMD_STRIDE)

/* MSI-X registers */
#define HINIC_CSR_MSIX_CTRL_BASE                        0x2000
#define HINIC_CSR_MSIX_CNT_BASE                         0x2004

#define HINIC_CSR_MSIX_STRIDE                           0x8

#define HINIC_CSR_MSIX_CTRL_ADDR(idx)                   \
	(HINIC_CSR_MSIX_CTRL_BASE + (idx) * HINIC_CSR_MSIX_STRIDE)

#define HINIC_CSR_MSIX_CNT_ADDR(idx)                    \
	(HINIC_CSR_MSIX_CNT_BASE + (idx) * HINIC_CSR_MSIX_STRIDE)

/* EQ registers */
#define HINIC_AEQ_MTT_OFF_BASE_ADDR                     0x200
#define HINIC_CEQ_MTT_OFF_BASE_ADDR                     0x400

#define HINIC_EQ_MTT_OFF_STRIDE                         0x40

#define HINIC_CSR_AEQ_MTT_OFF(id)                       \
	(HINIC_AEQ_MTT_OFF_BASE_ADDR + (id) * HINIC_EQ_MTT_OFF_STRIDE)

#define HINIC_CSR_CEQ_MTT_OFF(id)                       \
	(HINIC_CEQ_MTT_OFF_BASE_ADDR + (id) * HINIC_EQ_MTT_OFF_STRIDE)

#define HINIC_CSR_EQ_PAGE_OFF_STRIDE                    8

#define HINIC_CSR_AEQ_HI_PHYS_ADDR_REG(q_id, pg_num)    \
	(HINIC_CSR_AEQ_MTT_OFF(q_id) + \
	 (pg_num) * HINIC_CSR_EQ_PAGE_OFF_STRIDE)

#define HINIC_CSR_CEQ_HI_PHYS_ADDR_REG(q_id, pg_num)    \
	(HINIC_CSR_CEQ_MTT_OFF(q_id) +          \
	 (pg_num) * HINIC_CSR_EQ_PAGE_OFF_STRIDE)

#define HINIC_CSR_AEQ_LO_PHYS_ADDR_REG(q_id, pg_num)    \
	(HINIC_CSR_AEQ_MTT_OFF(q_id) + \
	 (pg_num) * HINIC_CSR_EQ_PAGE_OFF_STRIDE + 4)

#define HINIC_CSR_CEQ_LO_PHYS_ADDR_REG(q_id, pg_num)    \
	(HINIC_CSR_CEQ_MTT_OFF(q_id) +  \
	 (pg_num) * HINIC_CSR_EQ_PAGE_OFF_STRIDE + 4)

#define HINIC_AEQ_CTRL_0_ADDR_BASE                      0xE00
#define HINIC_AEQ_CTRL_1_ADDR_BASE                      0xE04
#define HINIC_AEQ_CONS_IDX_ADDR_BASE                    0xE08
#define HINIC_AEQ_PROD_IDX_ADDR_BASE                    0xE0C

#define HINIC_CEQ_CTRL_0_ADDR_BASE                      0x1000
#define HINIC_CEQ_CTRL_1_ADDR_BASE                      0x1004
#define HINIC_CEQ_CONS_IDX_ADDR_BASE                    0x1008
#define HINIC_CEQ_PROD_IDX_ADDR_BASE                    0x100C

#define HINIC_EQ_OFF_STRIDE                             0x80

#define HINIC_CSR_AEQ_CTRL_0_ADDR(idx)                  \
	(HINIC_AEQ_CTRL_0_ADDR_BASE + (idx) * HINIC_EQ_OFF_STRIDE)

#define HINIC_CSR_AEQ_CTRL_1_ADDR(idx)                  \
	(HINIC_AEQ_CTRL_1_ADDR_BASE + (idx) * HINIC_EQ_OFF_STRIDE)

#define HINIC_CSR_AEQ_CONS_IDX_ADDR(idx)                \
	(HINIC_AEQ_CONS_IDX_ADDR_BASE + (idx) * HINIC_EQ_OFF_STRIDE)

#define HINIC_CSR_AEQ_PROD_IDX_ADDR(idx)                \
	(HINIC_AEQ_PROD_IDX_ADDR_BASE + (idx) * HINIC_EQ_OFF_STRIDE)

#define HINIC_CSR_CEQ_CTRL_0_ADDR(idx)                  \
	(HINIC_CEQ_CTRL_0_ADDR_BASE + (idx) * HINIC_EQ_OFF_STRIDE)

#define HINIC_CSR_CEQ_CTRL_1_ADDR(idx)                  \
	(HINIC_CEQ_CTRL_1_ADDR_BASE + (idx) * HINIC_EQ_OFF_STRIDE)

#define HINIC_CSR_CEQ_CONS_IDX_ADDR(idx)                \
	(HINIC_CEQ_CONS_IDX_ADDR_BASE + (idx) * HINIC_EQ_OFF_STRIDE)

#define HINIC_CSR_CEQ_PROD_IDX_ADDR(idx)                \
	(HINIC_CEQ_PROD_IDX_ADDR_BASE + (idx) * HINIC_EQ_OFF_STRIDE)

#endif
