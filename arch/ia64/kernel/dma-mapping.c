// SPDX-License-Identifier: GPL-2.0
#include <linux/dma-direct.h>
#include <linux/swiotlb.h>
#include <linux/export.h>

/* Set this to 1 if there is a HW IOMMU in the system */
int iommu_detected __read_mostly;

const struct dma_map_ops *dma_ops;
EXPORT_SYMBOL(dma_ops);

const struct dma_map_ops *dma_get_ops(struct device *dev)
{
	return dma_ops;
}
EXPORT_SYMBOL(dma_get_ops);

#ifdef CONFIG_SWIOTLB
void *arch_dma_alloc(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp, unsigned long attrs)
{
	return dma_direct_alloc_pages(dev, size, dma_handle, gfp, attrs);
}

void arch_dma_free(struct device *dev, size_t size, void *cpu_addr,
		dma_addr_t dma_addr, unsigned long attrs)
{
	dma_direct_free_pages(dev, size, cpu_addr, dma_addr, attrs);
}

long arch_dma_coherent_to_pfn(struct device *dev, void *cpu_addr,
		dma_addr_t dma_addr)
{
	return page_to_pfn(virt_to_page(cpu_addr));
}

void __init swiotlb_dma_init(void)
{
	swiotlb_init(1);
}
#endif
