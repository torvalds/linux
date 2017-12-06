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

#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/byteorder.h>

#include "hinic_common.h"

/**
 * hinic_cpu_to_be32 - convert data to big endian 32 bit format
 * @data: the data to convert
 * @len: length of data to convert
 **/
void hinic_cpu_to_be32(void *data, int len)
{
	u32 *mem = data;
	int i;

	len = len / sizeof(u32);

	for (i = 0; i < len; i++) {
		*mem = cpu_to_be32(*mem);
		mem++;
	}
}

/**
 * hinic_be32_to_cpu - convert data from big endian 32 bit format
 * @data: the data to convert
 * @len: length of data to convert
 **/
void hinic_be32_to_cpu(void *data, int len)
{
	u32 *mem = data;
	int i;

	len = len / sizeof(u32);

	for (i = 0; i < len; i++) {
		*mem = be32_to_cpu(*mem);
		mem++;
	}
}

/**
 * hinic_set_sge - set dma area in scatter gather entry
 * @sge: scatter gather entry
 * @addr: dma address
 * @len: length of relevant data in the dma address
 **/
void hinic_set_sge(struct hinic_sge *sge, dma_addr_t addr, int len)
{
	sge->hi_addr = upper_32_bits(addr);
	sge->lo_addr = lower_32_bits(addr);
	sge->len  = len;
}

/**
 * hinic_sge_to_dma - get dma address from scatter gather entry
 * @sge: scatter gather entry
 *
 * Return dma address of sg entry
 **/
dma_addr_t hinic_sge_to_dma(struct hinic_sge *sge)
{
	return (dma_addr_t)((((u64)sge->hi_addr) << 32) | sge->lo_addr);
}
