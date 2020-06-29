/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __ISYS_DMA_PRIVATE_H_INCLUDED__
#define __ISYS_DMA_PRIVATE_H_INCLUDED__

#include "isys_dma_public.h"
#include "device_access.h"
#include "assert_support.h"
#include "dma.h"
#include "dma_v2_defs.h"
#include "print_support.h"

STORAGE_CLASS_ISYS2401_DMA_C void isys2401_dma_reg_store(
    const isys2401_dma_ID_t	dma_id,
    const unsigned int	reg,
    const hrt_data		value)
{
	unsigned int reg_loc;

	assert(dma_id < N_ISYS2401_DMA_ID);
	assert(ISYS2401_DMA_BASE[dma_id] != (hrt_address) - 1);

	reg_loc = ISYS2401_DMA_BASE[dma_id] + (reg * sizeof(hrt_data));

	ia_css_print("isys dma store at addr(0x%x) val(%u)\n", reg_loc,
		     (unsigned int)value);
	ia_css_device_store_uint32(reg_loc, value);
}

STORAGE_CLASS_ISYS2401_DMA_C hrt_data isys2401_dma_reg_load(
    const isys2401_dma_ID_t	dma_id,
    const unsigned int	reg)
{
	unsigned int reg_loc;
	hrt_data value;

	assert(dma_id < N_ISYS2401_DMA_ID);
	assert(ISYS2401_DMA_BASE[dma_id] != (hrt_address) - 1);

	reg_loc = ISYS2401_DMA_BASE[dma_id] + (reg * sizeof(hrt_data));

	value = ia_css_device_load_uint32(reg_loc);
	ia_css_print("isys dma load from addr(0x%x) val(%u)\n", reg_loc,
		     (unsigned int)value);

	return value;
}

#endif /* __ISYS_DMA_PRIVATE_H_INCLUDED__ */
