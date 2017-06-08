/*
 * DMA Mapping glue for ARC
 *
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ASM_ARC_DMA_MAPPING_H
#define ASM_ARC_DMA_MAPPING_H

#ifndef CONFIG_ARC_PLAT_NEEDS_PHYS_TO_DMA
#define plat_dma_to_phys(dev, dma_handle) ((phys_addr_t)(dma_handle))
#define plat_phys_to_dma(dev, paddr) ((dma_addr_t)(paddr))
#else
#include <plat/dma.h>
#endif

extern const struct dma_map_ops arc_dma_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return &arc_dma_ops;
}

#endif
