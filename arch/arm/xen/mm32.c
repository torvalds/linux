#include <linux/cpu.h>
#include <linux/dma-mapping.h>
#include <linux/gfp.h>
#include <linux/highmem.h>

#include <xen/features.h>


/* functions called by SWIOTLB */

static void dma_cache_maint(dma_addr_t handle, unsigned long offset,
	size_t size, enum dma_data_direction dir,
	void (*op)(const void *, size_t, int))
{
	unsigned long pfn;
	size_t left = size;

	pfn = (handle >> PAGE_SHIFT) + offset / PAGE_SIZE;
	offset %= PAGE_SIZE;

	do {
		size_t len = left;
		void *vaddr;
	
		if (!pfn_valid(pfn))
		{
			/* TODO: cache flush */
		} else {
			struct page *page = pfn_to_page(pfn);

			if (PageHighMem(page)) {
				if (len + offset > PAGE_SIZE)
					len = PAGE_SIZE - offset;

				if (cache_is_vipt_nonaliasing()) {
					vaddr = kmap_atomic(page);
					op(vaddr + offset, len, dir);
					kunmap_atomic(vaddr);
				} else {
					vaddr = kmap_high_get(page);
					if (vaddr) {
						op(vaddr + offset, len, dir);
						kunmap_high(page);
					}
				}
			} else {
				vaddr = page_address(page) + offset;
				op(vaddr, len, dir);
			}
		}

		offset = 0;
		pfn++;
		left -= len;
	} while (left);
}

static void __xen_dma_page_dev_to_cpu(struct device *hwdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir)
{
	/* Cannot use __dma_page_dev_to_cpu because we don't have a
	 * struct page for handle */

	if (dir != DMA_TO_DEVICE)
		outer_inv_range(handle, handle + size);

	dma_cache_maint(handle & PAGE_MASK, handle & ~PAGE_MASK, size, dir, dmac_unmap_area);
}

static void __xen_dma_page_cpu_to_dev(struct device *hwdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir)
{

	dma_cache_maint(handle & PAGE_MASK, handle & ~PAGE_MASK, size, dir, dmac_map_area);

	if (dir == DMA_FROM_DEVICE) {
		outer_inv_range(handle, handle + size);
	} else {
		outer_clean_range(handle, handle + size);
	}
}

void xen_dma_unmap_page(struct device *hwdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir,
		struct dma_attrs *attrs)

{
	if (!__generic_dma_ops(hwdev)->unmap_page)
		return;
	if (dma_get_attr(DMA_ATTR_SKIP_CPU_SYNC, attrs))
		return;

	__xen_dma_page_dev_to_cpu(hwdev, handle, size, dir);
}

void xen_dma_sync_single_for_cpu(struct device *hwdev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	if (!__generic_dma_ops(hwdev)->sync_single_for_cpu)
		return;
	__xen_dma_page_dev_to_cpu(hwdev, handle, size, dir);
}

void xen_dma_sync_single_for_device(struct device *hwdev,
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
