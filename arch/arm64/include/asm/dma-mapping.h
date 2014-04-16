/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_DMA_MAPPING_H
#define __ASM_DMA_MAPPING_H

#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/vmalloc.h>

#include <asm-generic/dma-coherent.h>

#define ARCH_HAS_DMA_GET_REQUIRED_MASK

#define DMA_ERROR_CODE	(~(dma_addr_t)0)
extern struct dma_map_ops *dma_ops;
extern struct dma_map_ops coherent_swiotlb_dma_ops;
extern struct dma_map_ops noncoherent_swiotlb_dma_ops;

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	if (unlikely(!dev) || !dev->archdata.dma_ops)
		return dma_ops;
	else
		return dev->archdata.dma_ops;
}

static inline void set_dma_ops(struct device *dev, struct dma_map_ops *ops)
{
	dev->archdata.dma_ops = ops;
}

#include <asm-generic/dma-mapping-common.h>

static inline dma_addr_t phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	return (dma_addr_t)paddr;
}

static inline phys_addr_t dma_to_phys(struct device *dev, dma_addr_t dev_addr)
{
	return (phys_addr_t)dev_addr;
}

static inline int dma_mapping_error(struct device *dev, dma_addr_t dev_addr)
{
	struct dma_map_ops *ops = get_dma_ops(dev);
	debug_dma_mapping_error(dev, dev_addr);
	return ops->mapping_error(dev, dev_addr);
}

static inline int dma_supported(struct device *dev, u64 mask)
{
	struct dma_map_ops *ops = get_dma_ops(dev);
	return ops->dma_supported(dev, mask);
}

static inline int dma_set_mask(struct device *dev, u64 mask)
{
	if (!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;
	*dev->dma_mask = mask;

	return 0;
}

static inline bool dma_capable(struct device *dev, dma_addr_t addr, size_t size)
{
	if (!dev->dma_mask)
		return 0;

	return addr + size - 1 <= *dev->dma_mask;
}

static inline void dma_mark_clean(void *addr, size_t size)
{
}

#define dma_alloc_coherent(d, s, h, f)	dma_alloc_attrs(d, s, h, f, NULL)
#define dma_free_coherent(d, s, h, f)	dma_free_attrs(d, s, h, f, NULL)

static inline void *dma_alloc_attrs(struct device *dev, size_t size,
				    dma_addr_t *dma_handle, gfp_t flags,
				    struct dma_attrs *attrs)
{
	struct dma_map_ops *ops = get_dma_ops(dev);
	void *vaddr;

	if (dma_alloc_from_coherent(dev, size, dma_handle, &vaddr))
		return vaddr;

	vaddr = ops->alloc(dev, size, dma_handle, flags, attrs);
	debug_dma_alloc_coherent(dev, size, *dma_handle, vaddr);
	return vaddr;
}

static inline void dma_free_attrs(struct device *dev, size_t size,
				  void *vaddr, dma_addr_t dev_addr,
				  struct dma_attrs *attrs)
{
	struct dma_map_ops *ops = get_dma_ops(dev);

	if (dma_release_from_coherent(dev, get_order(size), vaddr))
		return;

	debug_dma_free_coherent(dev, size, vaddr, dev_addr);
	ops->free(dev, size, vaddr, dev_addr, attrs);
}

/*
 * There is no dma_cache_sync() implementation, so just return NULL here.
 */
static inline void *dma_alloc_noncoherent(struct device *dev, size_t size,
					  dma_addr_t *handle, gfp_t flags)
{
	return NULL;
}

static inline void dma_free_noncoherent(struct device *dev, size_t size,
					void *cpu_addr, dma_addr_t handle)
{
}

#endif	/* __KERNEL__ */
#endif	/* __ASM_DMA_MAPPING_H */
