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

#endif
