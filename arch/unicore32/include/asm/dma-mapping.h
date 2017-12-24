/*
 * linux/arch/unicore32/include/asm/dma-mapping.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __UNICORE_DMA_MAPPING_H__
#define __UNICORE_DMA_MAPPING_H__

#include <linux/swiotlb.h>

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return &swiotlb_dma_ops;
}

#endif
