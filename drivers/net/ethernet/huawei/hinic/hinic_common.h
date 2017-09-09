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
