/*
 * Copyright (C) 2006 Benjamin Herrenschmidt, IBM Corporation
 *
 * Provide default implementations of the DMA mapping callbacks for
 * directly mapped busses.
 */

#include <linux/device.h>
#include <linux/dma-direct.h>
#include <linux/dma-debug.h>
#include <linux/gfp.h>
#include <linux/memblock.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <asm/vio.h>
#include <asm/bug.h>
#include <asm/machdep.h>
#include <asm/swiotlb.h>
#include <asm/iommu.h>

/*
 * Generic direct DMA implementation
 *
 * This implementation supports a per-device offset that can be applied if
 * the address at which memory is visible to devices is not 0. Platform code
 * can set archdata.dma_data to an unsigned long holding the offset. By
 * default the offset is PCI_DRAM_OFFSET.
 */

const struct dma_map_ops dma_nommu_ops = {
#ifdef CONFIG_NOT_COHERENT_CACHE
	.alloc				= __dma_nommu_alloc_coherent,
	.free				= __dma_nommu_free_coherent,
#else
	.alloc				= dma_direct_alloc,
	.free				= dma_direct_free,
#endif
	.map_sg				= dma_direct_map_sg,
	.dma_supported			= dma_direct_supported,
	.map_page			= dma_direct_map_page,
	.get_required_mask		= dma_direct_get_required_mask,
#ifdef CONFIG_NOT_COHERENT_CACHE
	.unmap_sg			= dma_direct_unmap_sg,
	.unmap_page			= dma_direct_unmap_page,
	.sync_single_for_cpu 		= dma_direct_sync_single_for_cpu,
	.sync_single_for_device 	= dma_direct_sync_single_for_device,
	.sync_sg_for_cpu 		= dma_direct_sync_sg_for_cpu,
	.sync_sg_for_device 		= dma_direct_sync_sg_for_device,
#endif
};
EXPORT_SYMBOL(dma_nommu_ops);

static int __init dma_init(void)
{
#ifdef CONFIG_IBMVIO
	dma_debug_add_bus(&vio_bus_type);
#endif

       return 0;
}
fs_initcall(dma_init);

