#include <linux/cpu.h>
#include <linux/dma-mapping.h>
#include <linux/bootmem.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/export.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <linux/swiotlb.h>

#include <xen/xen.h>
#include <xen/interface/grant_table.h>
#include <xen/interface/memory.h>
#include <xen/swiotlb-xen.h>

#include <asm/cacheflush.h>
#include <asm/xen/page.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/interface.h>

enum dma_cache_op {
       DMA_UNMAP,
       DMA_MAP,
};
static bool hypercall_cflush = false;

/* functions called by SWIOTLB */

static void dma_cache_maint(dma_addr_t handle, unsigned long offset,
	size_t size, enum dma_data_direction dir, enum dma_cache_op op)
{
	struct gnttab_cache_flush cflush;
	unsigned long pfn;
	size_t left = size;

	pfn = (handle >> PAGE_SHIFT) + offset / PAGE_SIZE;
	offset %= PAGE_SIZE;

	do {
		size_t len = left;
	
		/* buffers in highmem or foreign pages cannot cross page
		 * boundaries */
		if (len + offset > PAGE_SIZE)
			len = PAGE_SIZE - offset;

		cflush.op = 0;
		cflush.a.dev_bus_addr = pfn << PAGE_SHIFT;
		cflush.offset = offset;
		cflush.length = len;

		if (op == DMA_UNMAP && dir != DMA_TO_DEVICE)
			cflush.op = GNTTAB_CACHE_INVAL;
		if (op == DMA_MAP) {
			if (dir == DMA_FROM_DEVICE)
				cflush.op = GNTTAB_CACHE_INVAL;
			else
				cflush.op = GNTTAB_CACHE_CLEAN;
		}
		if (cflush.op)
			HYPERVISOR_grant_table_op(GNTTABOP_cache_flush, &cflush, 1);

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

void __xen_dma_map_page(struct device *hwdev, struct page *page,
	     dma_addr_t dev_addr, unsigned long offset, size_t size,
	     enum dma_data_direction dir, struct dma_attrs *attrs)
{
	if (is_device_dma_coherent(hwdev))
		return;
	if (dma_get_attr(DMA_ATTR_SKIP_CPU_SYNC, attrs))
		return;

	__xen_dma_page_cpu_to_dev(hwdev, dev_addr, size, dir);
}

void __xen_dma_unmap_page(struct device *hwdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir,
		struct dma_attrs *attrs)

{
	if (is_device_dma_coherent(hwdev))
		return;
	if (dma_get_attr(DMA_ATTR_SKIP_CPU_SYNC, attrs))
		return;

	__xen_dma_page_dev_to_cpu(hwdev, handle, size, dir);
}

void __xen_dma_sync_single_for_cpu(struct device *hwdev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	if (is_device_dma_coherent(hwdev))
		return;
	__xen_dma_page_dev_to_cpu(hwdev, handle, size, dir);
}

void __xen_dma_sync_single_for_device(struct device *hwdev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	if (is_device_dma_coherent(hwdev))
		return;
	__xen_dma_page_cpu_to_dev(hwdev, handle, size, dir);
}

bool xen_arch_need_swiotlb(struct device *dev,
			   unsigned long pfn,
			   unsigned long mfn)
{
	return (!hypercall_cflush && (pfn != mfn) && !is_device_dma_coherent(dev));
}

int xen_create_contiguous_region(phys_addr_t pstart, unsigned int order,
				 unsigned int address_bits,
				 dma_addr_t *dma_handle)
{
	if (!xen_initial_domain())
		return -EINVAL;

	/* we assume that dom0 is mapped 1:1 for now */
	*dma_handle = pstart;
	return 0;
}
EXPORT_SYMBOL_GPL(xen_create_contiguous_region);

void xen_destroy_contiguous_region(phys_addr_t pstart, unsigned int order)
{
	return;
}
EXPORT_SYMBOL_GPL(xen_destroy_contiguous_region);

struct dma_map_ops *xen_dma_ops;
EXPORT_SYMBOL_GPL(xen_dma_ops);

static struct dma_map_ops xen_swiotlb_dma_ops = {
	.mapping_error = xen_swiotlb_dma_mapping_error,
	.alloc = xen_swiotlb_alloc_coherent,
	.free = xen_swiotlb_free_coherent,
	.sync_single_for_cpu = xen_swiotlb_sync_single_for_cpu,
	.sync_single_for_device = xen_swiotlb_sync_single_for_device,
	.sync_sg_for_cpu = xen_swiotlb_sync_sg_for_cpu,
	.sync_sg_for_device = xen_swiotlb_sync_sg_for_device,
	.map_sg = xen_swiotlb_map_sg_attrs,
	.unmap_sg = xen_swiotlb_unmap_sg_attrs,
	.map_page = xen_swiotlb_map_page,
	.unmap_page = xen_swiotlb_unmap_page,
	.dma_supported = xen_swiotlb_dma_supported,
	.set_dma_mask = xen_swiotlb_set_dma_mask,
};

int __init xen_mm_init(void)
{
	struct gnttab_cache_flush cflush;
	if (!xen_initial_domain())
		return 0;
	xen_swiotlb_init(1, false);
	xen_dma_ops = &xen_swiotlb_dma_ops;

	cflush.op = 0;
	cflush.a.dev_bus_addr = 0;
	cflush.offset = 0;
	cflush.length = 0;
	if (HYPERVISOR_grant_table_op(GNTTABOP_cache_flush, &cflush, 1) != -ENOSYS)
		hypercall_cflush = true;
	return 0;
}
arch_initcall(xen_mm_init);
