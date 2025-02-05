/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2015, Intel Corporation.
 */

#ifndef __DMA_PRIVATE_H_INCLUDED__
#define __DMA_PRIVATE_H_INCLUDED__

#include "dma_public.h"

#include "device_access.h"

#include "assert_support.h"

STORAGE_CLASS_DMA_C void dma_reg_store(const dma_ID_t ID,
				       const unsigned int reg,
				       const hrt_data value)
{
	assert(ID < N_DMA_ID);
	assert(DMA_BASE[ID] != (hrt_address) - 1);
	ia_css_device_store_uint32(DMA_BASE[ID] + reg * sizeof(hrt_data), value);
}

STORAGE_CLASS_DMA_C hrt_data dma_reg_load(const dma_ID_t ID,
	const unsigned int reg)
{
	assert(ID < N_DMA_ID);
	assert(DMA_BASE[ID] != (hrt_address) - 1);
	return ia_css_device_load_uint32(DMA_BASE[ID] + reg * sizeof(hrt_data));
}

#endif /* __DMA_PRIVATE_H_INCLUDED__ */
