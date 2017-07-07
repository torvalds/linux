/* DMA mapping routines for the MN10300 arch
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_DMA_MAPPING_H
#define _ASM_DMA_MAPPING_H

#include <asm/cache.h>
#include <asm/io.h>

extern const struct dma_map_ops mn10300_dma_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return &mn10300_dma_ops;
}

static inline
void dma_cache_sync(void *vaddr, size_t size,
		    enum dma_data_direction direction)
{
	mn10300_dcache_flush_inv();
}

#endif
