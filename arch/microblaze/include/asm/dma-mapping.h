/*
 * Implements the generic device dma API for microblaze and the pci
 *
 * Copyright (C) 2009-2010 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2009-2010 PetaLogix
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of this
 * archive for more details.
 *
 * This file is base on powerpc and x86 dma-mapping.h versions
 * Copyright (C) 2004 IBM
 */

#ifndef _ASM_MICROBLAZE_DMA_MAPPING_H
#define _ASM_MICROBLAZE_DMA_MAPPING_H

/*
 * See Documentation/PCI/PCI-DMA-mapping.txt and
 * Documentation/DMA-API.txt for documentation.
 */

#include <linux/types.h>
#include <linux/cache.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/dma-debug.h>
#include <linux/dma-attrs.h>
#include <asm/io.h>
#include <asm-generic/dma-coherent.h>

#define DMA_ERROR_CODE		(~(dma_addr_t)0x0)

#define __dma_alloc_coherent(dev, gfp, size, handle)	NULL
#define __dma_free_coherent(size, addr)		((void)0)
#define __dma_sync(addr, size, rw)		((void)0)

static inline unsigned long device_to_mask(struct device *dev)
{
	if (dev->dma_mask && *dev->dma_mask)
		return *dev->dma_mask;
	/* Assume devices without mask can take 32 bit addresses */
	return 0xfffffffful;
}

extern struct dma_map_ops *dma_ops;

/*
 * Available generic sets of operations
 */
extern struct dma_map_ops dma_direct_ops;

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	/* We don't handle the NULL dev case for ISA for now. We could
	 * do it via an out of line call but it is not needed for now. The
	 * only ISA DMA device we support is the floppy and we have a hack
	 * in the floppy driver directly to get a device for us.
	 */
	if (unlikely(!dev) || !dev->archdata.dma_ops)
		return NULL;

	return dev->archdata.dma_ops;
}

static inline void set_dma_ops(struct device *dev, struct dma_map_ops *ops)
{
	dev->archdata.dma_ops = ops;
}

static inline int dma_supported(struct device *dev, u64 mask)
{
	struct dma_map_ops *ops = get_dma_ops(dev);

	if (unlikely(!ops))
		return 0;
	if (!ops->dma_supported)
		return 1;
	return ops->dma_supported(dev, mask);
}

#ifdef CONFIG_PCI
/* We have our own implementation of pci_set_dma_mask() */
#define HAVE_ARCH_PCI_SET_DMA_MASK

#endif

static inline int dma_set_mask(struct device *dev, u64 dma_mask)
{
	struct dma_map_ops *ops = get_dma_ops(dev);

	if (unlikely(ops == NULL))
		return -EIO;
	if (ops->set_dma_mask)
		return ops->set_dma_mask(dev, dma_mask);
	if (!dev->dma_mask || !dma_supported(dev, dma_mask))
		return -EIO;
	*dev->dma_mask = dma_mask;
	return 0;
}

#include <asm-generic/dma-mapping-common.h>

static inline int dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	struct dma_map_ops *ops = get_dma_ops(dev);
	if (ops->mapping_error)
		return ops->mapping_error(dev, dma_addr);

	return (dma_addr == DMA_ERROR_CODE);
}

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)
#define dma_is_consistent(d, h)	(1)

static inline void *dma_alloc_coherent(struct device *dev, size_t size,
					dma_addr_t *dma_handle, gfp_t flag)
{
	struct dma_map_ops *ops = get_dma_ops(dev);
	void *memory;

	BUG_ON(!ops);

	memory = ops->alloc_coherent(dev, size, dma_handle, flag);

	debug_dma_alloc_coherent(dev, size, *dma_handle, memory);
	return memory;
}

static inline void dma_free_coherent(struct device *dev, size_t size,
				     void *cpu_addr, dma_addr_t dma_handle)
{
	struct dma_map_ops *ops = get_dma_ops(dev);

	BUG_ON(!ops);
	debug_dma_free_coherent(dev, size, cpu_addr, dma_handle);
	ops->free_coherent(dev, size, cpu_addr, dma_handle);
}

static inline int dma_get_cache_alignment(void)
{
	return L1_CACHE_BYTES;
}

static inline void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
		enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
	__dma_sync(vaddr, size, (int)direction);
}

#endif	/* _ASM_MICROBLAZE_DMA_MAPPING_H */
