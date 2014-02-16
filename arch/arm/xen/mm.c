#include <linux/bootmem.h>
#include <linux/gfp.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <linux/swiotlb.h>

#include <xen/xen.h>
#include <xen/interface/memory.h>
#include <xen/swiotlb-xen.h>

#include <asm/cacheflush.h>
#include <asm/xen/page.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/interface.h>

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
	if (!xen_initial_domain())
		return 0;
	xen_swiotlb_init(1, false);
	xen_dma_ops = &xen_swiotlb_dma_ops;
	return 0;
}
arch_initcall(xen_mm_init);
