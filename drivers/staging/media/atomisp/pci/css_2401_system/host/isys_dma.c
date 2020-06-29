// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "isys_dma.h"
#include "assert_support.h"

#ifndef __INLINE_ISYS2401_DMA__
/*
 * Include definitions for isys dma register access functions. isys_dma.h
 * includes declarations of these functions by including isys_dma_public.h.
 */
#include "isys_dma_private.h"
#endif

const isys2401_dma_channel N_ISYS2401_DMA_CHANNEL_PROCS[N_ISYS2401_DMA_ID] = {
	N_ISYS2401_DMA_CHANNEL
};

void isys2401_dma_set_max_burst_size(
    const isys2401_dma_ID_t	dma_id,
    uint32_t		max_burst_size)
{
	assert(dma_id < N_ISYS2401_DMA_ID);
	assert((max_burst_size > 0x00) && (max_burst_size <= 0xFF));

	isys2401_dma_reg_store(dma_id,
			       DMA_DEV_INFO_REG_IDX(_DMA_V2_DEV_INTERF_MAX_BURST_IDX, HIVE_DMA_BUS_DDR_CONN),
			       (max_burst_size - 1));
}
