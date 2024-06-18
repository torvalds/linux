// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2016, Intel Corporation.
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

#include <linux/kernel.h>

#include "dma.h"

#include "assert_support.h"

#ifndef __INLINE_DMA__
#include "dma_private.h"
#endif /* __INLINE_DMA__ */

void
dma_set_max_burst_size(const dma_ID_t ID, dma_connection conn,
		       uint32_t max_burst_size)
{
	assert(ID < N_DMA_ID);
	assert(max_burst_size > 0);
	dma_reg_store(ID, DMA_DEV_INFO_REG_IDX(_DMA_DEV_INTERF_MAX_BURST_IDX, conn),
		      max_burst_size - 1);
}
