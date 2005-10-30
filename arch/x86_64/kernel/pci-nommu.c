/* Fallback functions when the main IOMMU code is not compiled in. This
   code is roughly equivalent to i386. */
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <asm/proto.h>
#include <asm/processor.h>

int iommu_merge = 0;
EXPORT_SYMBOL(iommu_merge);

dma_addr_t bad_dma_address;
EXPORT_SYMBOL(bad_dma_address);

int iommu_bio_merge = 0;
EXPORT_SYMBOL(iommu_bio_merge);

int iommu_sac_force = 0;
EXPORT_SYMBOL(iommu_sac_force);

/* 
 * Dummy IO MMU functions
 */

void *dma_alloc_coherent(struct device *hwdev, size_t size,
			 dma_addr_t *dma_handle, gfp_t gfp)
{
	void *ret;
	u64 mask;
	int order = get_order(size);

	if (hwdev)
		mask = hwdev->coherent_dma_mask & *hwdev->dma_mask;
	else
		mask = 0xffffffff;
	for (;;) {
		ret = (void *)__get_free_pages(gfp, order);
		if (ret == NULL)
			return NULL;
		*dma_handle = virt_to_bus(ret);
		if ((*dma_handle & ~mask) == 0)
			break;
		free_pages((unsigned long)ret, order);
		if (gfp & GFP_DMA)
			return NULL;
		gfp |= GFP_DMA;
	}

	memset(ret, 0, size);
	return ret;
}
EXPORT_SYMBOL(dma_alloc_coherent);

void dma_free_coherent(struct device *hwdev, size_t size,
			 void *vaddr, dma_addr_t dma_handle)
{
	free_pages((unsigned long)vaddr, get_order(size));
}
EXPORT_SYMBOL(dma_free_coherent);

int dma_supported(struct device *hwdev, u64 mask)
{
        /*
         * we fall back to GFP_DMA when the mask isn't all 1s,
         * so we can't guarantee allocations that must be
         * within a tighter range than GFP_DMA..
	 * RED-PEN this won't work for pci_map_single. Caller has to
	 * use GFP_DMA in the first place.
         */
        if (mask < 0x00ffffff)
                return 0;

	return 1;
} 
EXPORT_SYMBOL(dma_supported);

int dma_get_cache_alignment(void)
{
	return boot_cpu_data.x86_clflush_size;
}
EXPORT_SYMBOL(dma_get_cache_alignment);

static int __init check_ram(void) 
{ 
	if (end_pfn >= 0xffffffff>>PAGE_SHIFT) { 
		printk(
		KERN_ERR "WARNING more than 4GB of memory but IOMMU not compiled in.\n"
		KERN_ERR "WARNING 32bit PCI may malfunction.\n");
	} 
	return 0;
} 
__initcall(check_ram);

