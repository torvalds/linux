/*
 * Contains routines needed to support swiotlb for UniCore32.
 *
 * Copyright (C) 2010 Guan Xuetao
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/pci.h>
#include <linux/cache.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/swiotlb.h>
#include <linux/bootmem.h>

#include <asm/dma.h>

struct dma_map_ops swiotlb_dma_map_ops = {
	.alloc_coherent = swiotlb_alloc_coherent,
	.free_coherent = swiotlb_free_coherent,
	.map_sg = swiotlb_map_sg_attrs,
	.unmap_sg = swiotlb_unmap_sg_attrs,
	.dma_supported = swiotlb_dma_supported,
	.map_page = swiotlb_map_page,
	.unmap_page = swiotlb_unmap_page,
	.sync_single_for_cpu = swiotlb_sync_single_for_cpu,
	.sync_single_for_device = swiotlb_sync_single_for_device,
	.sync_sg_for_cpu = swiotlb_sync_sg_for_cpu,
	.sync_sg_for_device = swiotlb_sync_sg_for_device,
	.mapping_error = swiotlb_dma_mapping_error,
};
EXPORT_SYMBOL(swiotlb_dma_map_ops);
