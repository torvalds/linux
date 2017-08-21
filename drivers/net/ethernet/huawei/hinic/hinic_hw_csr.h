/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */

#ifndef HINIC_HW_CSR_H
#define HINIC_HW_CSR_H

/* HW interface registers */
#define HINIC_CSR_FUNC_ATTR0_ADDR                       0x0
#define HINIC_CSR_FUNC_ATTR1_ADDR                       0x4

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

#endif
