/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_DMA_MAPPING_H
#define _ASM_X86_DMA_MAPPING_H

/*
 * IOMMU interface. See Documentation/DMA-API-HOWTO.txt and
 * Documentation/DMA-API.txt for documentation.
 */

#include <linux/scatterlist.h>
#include <linux/dma-debug.h>
#include <asm/io.h>
#include <asm/swiotlb.h>
#include <linux/dma-contiguous.h>
#include <linux/mem_encrypt.h>

#ifdef CONFIG_ISA
# define ISA_DMA_BIT_MASK DMA_BIT_MASK(24)
#else
# define ISA_DMA_BIT_MASK DMA_BIT_MASK(32)
#endif

extern int iommu_merge;
extern struct device x86_dma_fallback_dev;
extern int panic_on_overflow;

extern const struct dma_map_ops *dma_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return dma_ops;
}

bool arch_dma_alloc_attrs(struct device **dev, gfp_t *gfp);
#define arch_dma_alloc_attrs arch_dma_alloc_attrs

extern void *dma_generic_alloc_coherent(struct device *dev, size_t size,
					dma_addr_t *dma_addr, gfp_t flag,
					unsigned long attrs);

extern void dma_generic_free_coherent(struct device *dev, size_t size,
				      void *vaddr, dma_addr_t dma_addr,
				      unsigned long attrs);

#ifdef CONFIG_X86_DMA_REMAP /* Platform code defines bridge-specific code */
extern bool dma_capable(struct device *dev, dma_addr_t addr, size_t size);
extern dma_addr_t phys_to_dma(struct device *dev, phys_addr_t paddr);
extern phys_addr_t dma_to_phys(struct device *dev, dma_addr_t daddr);
#else

static inline bool dma_capable(struct device *dev, dma_addr_t addr, size_t size)
{
	if (!dev->dma_mask)
		return 0;

	return addr + size - 1 <= *dev->dma_mask;
}

static inline dma_addr_t phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	return __sme_set(paddr);
}

static inline phys_addr_t dma_to_phys(struct device *dev, dma_addr_t daddr)
{
	return __sme_clr(daddr);
}
#endif /* CONFIG_X86_DMA_REMAP */

static inline unsigned long dma_alloc_coherent_mask(struct device *dev,
						    gfp_t gfp)
{
	unsigned long dma_mask = 0;

	dma_mask = dev->coherent_dma_mask;
	if (!dma_mask)
		dma_mask = (gfp & GFP_DMA) ? DMA_BIT_MASK(24) : DMA_BIT_MASK(32);

	return dma_mask;
}

static inline gfp_t dma_alloc_coherent_gfp_flags(struct device *dev, gfp_t gfp)
{
	unsigned long dma_mask = dma_alloc_coherent_mask(dev, gfp);

	if (dma_mask <= DMA_BIT_MASK(24))
		gfp |= GFP_DMA;
#ifdef CONFIG_X86_64
	if (dma_mask <= DMA_BIT_MASK(32) && !(gfp & GFP_DMA))
		gfp |= GFP_DMA32;
#endif
       return gfp;
}

#endif
