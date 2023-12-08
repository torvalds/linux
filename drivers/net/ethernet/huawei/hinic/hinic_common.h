/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#ifndef HINIC_COMMON_H
#define HINIC_COMMON_H

#include <linux/types.h>

#define UPPER_8_BITS(data)      (((data) >> 8) & 0xFF)
#define LOWER_8_BITS(data)      ((data) & 0xFF)

struct hinic_sge {
	u32             hi_addr;
	u32             lo_addr;
	u32             len;
};

void hinic_cpu_to_be32(void *data, int len);

void hinic_be32_to_cpu(void *data, int len);

void hinic_set_sge(struct hinic_sge *sge, dma_addr_t addr, int len);

dma_addr_t hinic_sge_to_dma(struct hinic_sge *sge);

#endif
