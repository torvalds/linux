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

#include <xen/xen.h>
#include <asm/cacheflush.h>
#include <asm/xen/hypervisor.h>

#define DMA_ERROR_CODE	(~(dma_addr_t)0)
extern struct dma_map_ops dummy_dma_ops;

static inline struct dma_map_ops *__generic_dma_ops(struct device *dev)
{
	if (dev && dev->archdata.dma_ops)
		return dev->archdata.dma_ops;

	/*
	 * We expect no ISA devices, and all other DMA masters are expected to
	 * have someone call arch_setup_dma_ops at device creation time.
	 */
	return &dummy_dma_ops;
}

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	if (xen_initial_domain())
		return xen_dma_ops;
	else
		return __generic_dma_ops(dev);
}

static inline void arch_set_dma_ops(struct device *dev, struct dma_map_ops *ops)
{
	dev->archdata.dma_ops = ops;
}

void arch_setup_dma_ops(struct device *dev, u64 dma_base, u64 size,
			struct iommu_ops *iommu, bool coherent);
#define arch_setup_dma_ops	arch_setup_dma_ops

void arch_teardown_dma_ops(struct device *dev);
#define arch_teardown_dma_ops	arch_teardown_dma_ops

/* do not use this function in a driver */
static inline bool is_device_dma_coherent(struct device *dev)
{
	if (!dev)
		return false;
	return dev->archdata.dma_coherent;
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

static inline bool dma_capable(struct device *dev, dma_addr_t addr, size_t size)
{
	if (!dev->dma_mask)
		return false;

	return addr + size - 1 <= *dev->dma_mask;
}

static inline void dma_mark_clean(void *addr, size_t size)
{
}

static inline void arch_flush_page(struct device *dev, const void *virt,
				   phys_addr_t phys)
{
	__dma_flush_range(virt, virt + PAGE_SIZE);
}

static inline void arch_dma_map_area(phys_addr_t phys, size_t size,
				     enum dma_data_direction dir)
{
	__dma_map_area(phys_to_virt(phys), size, dir);
}

static inline void arch_dma_unmap_area(phys_addr_t phys, size_t size,
				       enum dma_data_direction dir)
{
	__dma_unmap_area(phys_to_virt(phys), size, dir);
}

static inline pgprot_t arch_get_dma_pgprot(struct dma_attrs *attrs,
					pgprot_t prot, bool coherent)
{
	if (!coherent || dma_get_attr(DMA_ATTR_WRITE_COMBINE, attrs))
		return pgprot_writecombine(prot);
	return prot;
}

extern void *arch_alloc_from_atomic_pool(size_t size, struct page **ret_page,
					 gfp_t flags);
extern bool arch_in_atomic_pool(void *start, size_t size);
extern int arch_free_from_atomic_pool(void *start, size_t size);

#endif	/* __KERNEL__ */
#endif	/* __ASM_DMA_MAPPING_H */
