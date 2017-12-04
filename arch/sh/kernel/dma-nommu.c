/*
 * DMA mapping support for platforms lacking IOMMUs.
 *
 * Copyright (C) 2009  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <asm/cacheflush.h>

static dma_addr_t nommu_map_page(struct device *dev, struct page *page,
				 unsigned long offset, size_t size,
				 enum dma_data_direction dir,
				 unsigned long attrs)
{
	dma_addr_t addr = page_to_phys(page) + offset
		- PFN_PHYS(dev->dma_pfn_offset);

	WARN_ON(size == 0);

	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		sh_sync_dma_for_device(page_address(page) + offset, size, dir);

	return addr;
}

static int nommu_map_sg(struct device *dev, struct scatterlist *sg,
			int nents, enum dma_data_direction dir,
			unsigned long attrs)
{
	struct scatterlist *s;
	int i;

	WARN_ON(nents == 0 || sg[0].length == 0);

	for_each_sg(sg, s, nents, i) {
		dma_addr_t offset = PFN_PHYS(dev->dma_pfn_offset);

		BUG_ON(!sg_page(s));

		if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
			sh_sync_dma_for_device(sg_virt(s), s->length, dir);

		s->dma_address = sg_phys(s) - offset;
		s->dma_length = s->length;
	}

	return nents;
}

#ifdef CONFIG_DMA_NONCOHERENT
static void nommu_sync_single_for_device(struct device *dev, dma_addr_t addr,
			      size_t size, enum dma_data_direction dir)
{
	sh_sync_dma_for_device(phys_to_virt(addr), size, dir);
}

static void nommu_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
			  int nelems, enum dma_data_direction dir)
{
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nelems, i)
		sh_sync_dma_for_device(sg_virt(s), s->length, dir);
}
#endif

const struct dma_map_ops nommu_dma_ops = {
	.alloc			= dma_generic_alloc_coherent,
	.free			= dma_generic_free_coherent,
	.map_page		= nommu_map_page,
	.map_sg			= nommu_map_sg,
#ifdef CONFIG_DMA_NONCOHERENT
	.sync_single_for_device	= nommu_sync_single_for_device,
	.sync_sg_for_device	= nommu_sync_sg_for_device,
#endif
	.is_phys		= 1,
};

void __init no_iommu_init(void)
{
	if (dma_ops)
		return;
	dma_ops = &nommu_dma_ops;
}
