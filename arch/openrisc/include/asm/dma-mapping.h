/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __ASM_OPENRISC_DMA_MAPPING_H
#define __ASM_OPENRISC_DMA_MAPPING_H

/*
 * See Documentation/DMA-API-HOWTO.txt and
 * Documentation/DMA-API.txt for documentation.
 */

#include <linux/dma-debug.h>
#include <linux/kmemcheck.h>
#include <linux/dma-mapping.h>

#define DMA_ERROR_CODE		(~(dma_addr_t)0x0)

extern const struct dma_map_ops or1k_dma_map_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return &or1k_dma_map_ops;
}

#define HAVE_ARCH_DMA_SUPPORTED 1
static inline int dma_supported(struct device *dev, u64 dma_mask)
{
	/* Support 32 bit DMA mask exclusively */
	return dma_mask == DMA_BIT_MASK(32);
}

#endif	/* __ASM_OPENRISC_DMA_MAPPING_H */
