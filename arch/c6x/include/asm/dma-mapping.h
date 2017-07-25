/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2010, 2011 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot <aurelien.jacquiot@ti.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */
#ifndef _ASM_C6X_DMA_MAPPING_H
#define _ASM_C6X_DMA_MAPPING_H

extern const struct dma_map_ops c6x_dma_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return &c6x_dma_ops;
}

extern void coherent_mem_init(u32 start, u32 size);
void *c6x_dma_alloc(struct device *dev, size_t size, dma_addr_t *handle,
		gfp_t gfp, unsigned long attrs);
void c6x_dma_free(struct device *dev, size_t size, void *vaddr,
		dma_addr_t dma_handle, unsigned long attrs);

#endif	/* _ASM_C6X_DMA_MAPPING_H */
