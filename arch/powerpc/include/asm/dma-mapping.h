/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2004 IBM
 *
 * Implements the generic device dma API for powerpc.
 * the pci and vio busses
 */
#ifndef _ASM_DMA_MAPPING_H
#define _ASM_DMA_MAPPING_H
#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/cache.h>
/* need struct page definitions */
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/dma-debug.h>
#include <asm/io.h>
#include <asm/swiotlb.h>

static inline unsigned long device_to_mask(struct device *dev)
{
	if (dev->dma_mask && *dev->dma_mask)
		return *dev->dma_mask;
	/* Assume devices without mask can take 32 bit addresses */
	return 0xfffffffful;
}

/*
 * Available generic sets of operations
 */
#ifdef CONFIG_PPC64
extern const struct dma_map_ops dma_iommu_ops;
#endif

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	/* We don't handle the NULL dev case for ISA for now. We could
	 * do it via an out of line call but it is not needed for now. The
	 * only ISA DMA device we support is the floppy and we have a hack
	 * in the floppy driver directly to get a device for us.
	 */
	return NULL;
}

#endif /* __KERNEL__ */
#endif	/* _ASM_DMA_MAPPING_H */
