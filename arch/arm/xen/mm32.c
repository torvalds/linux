#include <linux/cpu.h>
#include <linux/dma-mapping.h>
#include <linux/gfp.h>
#include <linux/highmem.h>

#include <xen/features.h>
enum dma_cache_op {
       DMA_UNMAP,
       DMA_MAP,
};

/* functions called by SWIOTLB */

static void dma_cache_maint(dma_addr_t handle, unsigned long offset,
	size_t size, enum dma_data_direction dir, enum dma_cache_op op)
{
	unsigned long pfn;
	size_t left = size;

	pfn = (handle >> PAGE_SHIFT) + offset / PAGE_SIZE;
	offset %= PAGE_SIZE;

	do {
		size_t len = left;
	
		/* TODO: cache flush */

		offset = 0;
		pfn++;
		left -= len;
	} while (left);
}

static void __xen_dma_page_dev_to_cpu(struct device *hwdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir)
{
	dma_cache_maint(handle & PAGE_MASK, handle & ~PAGE_MASK, size, dir, DMA_UNMAP);
}

static void __xen_dma_page_cpu_to_dev(struct device *hwdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir)
{
	dma_cache_maint(handle & PAGE_MASK, handle & ~PAGE_MASK, size, dir, DMA_MAP);
}

void __xen_dma_unmap_page(struct device *hwdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir,
		struct dma_attrs *attrs)

{
	if (!__generic_dma_ops(hwdev)->unmap_page)
		return;
	if (dma_get_attr(DMA_ATTR_SKIP_CPU_SYNC, attrs))
		return;

	__xen_dma_page_dev_to_cpu(hwdev, handle, size, dir);
}

void __xen_dma_sync_single_for_cpu(struct device *hwdev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	if (!__generic_dma_ops(hwdev)->sync_single_for_cpu)
		return;
	__xen_dma_page_dev_to_cpu(hwdev, handle, size, dir);
}

void __xen_dma_sync_single_for_device(struct device *hwdev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	if (!__generic_dma_ops(hwdev)->sync_single_for_device)
		return;
	__xen_dma_page_cpu_to_dev(hwdev, handle, size, dir);
}

int __init xen_mm32_init(void)
{
	if (!xen_initial_domain())
		return 0;

	return 0;
}
arch_initcall(xen_mm32_init);
