/*
 * Copyright (c) 2004 Hewlett-Packard Development Company, L.P.
 *   Contributed by David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * This is a pseudo I/O MMU which dispatches to the hardware I/O MMU
 * whenever possible.  We assume that the hardware I/O MMU requires
 * full 32-bit addressability, as is the case, e.g., for HP zx1-based
 * systems (there, the I/O MMU window is mapped at 3-4GB).  If a
 * device doesn't provide full 32-bit addressability, we fall back on
 * the sw I/O TLB.  This is good enough to let us support broken
 * hardware such as soundcards which have a DMA engine that can
 * address only 28 bits.
 */

#include <linux/device.h>

#include <asm/machvec.h>

/* swiotlb declarations & definitions: */
extern int swiotlb_late_init_with_default_size (size_t size);
extern ia64_mv_dma_alloc_coherent	swiotlb_alloc_coherent;
extern ia64_mv_dma_free_coherent	swiotlb_free_coherent;
extern ia64_mv_dma_map_single_attrs	swiotlb_map_single_attrs;
extern ia64_mv_dma_unmap_single_attrs	swiotlb_unmap_single_attrs;
extern ia64_mv_dma_map_sg_attrs		swiotlb_map_sg_attrs;
extern ia64_mv_dma_unmap_sg_attrs	swiotlb_unmap_sg_attrs;
extern ia64_mv_dma_supported		swiotlb_dma_supported;
extern ia64_mv_dma_mapping_error	swiotlb_dma_mapping_error;

/* hwiommu declarations & definitions: */

extern ia64_mv_dma_alloc_coherent	sba_alloc_coherent;
extern ia64_mv_dma_free_coherent	sba_free_coherent;
extern ia64_mv_dma_map_single_attrs	sba_map_single_attrs;
extern ia64_mv_dma_unmap_single_attrs	sba_unmap_single_attrs;
extern ia64_mv_dma_map_sg_attrs		sba_map_sg_attrs;
extern ia64_mv_dma_unmap_sg_attrs	sba_unmap_sg_attrs;
extern ia64_mv_dma_supported		sba_dma_supported;
extern ia64_mv_dma_mapping_error	sba_dma_mapping_error;

#define hwiommu_alloc_coherent		sba_alloc_coherent
#define hwiommu_free_coherent		sba_free_coherent
#define hwiommu_map_single_attrs	sba_map_single_attrs
#define hwiommu_unmap_single_attrs	sba_unmap_single_attrs
#define hwiommu_map_sg_attrs		sba_map_sg_attrs
#define hwiommu_unmap_sg_attrs		sba_unmap_sg_attrs
#define hwiommu_dma_supported		sba_dma_supported
#define hwiommu_dma_mapping_error	sba_dma_mapping_error
#define hwiommu_sync_single_for_cpu	machvec_dma_sync_single
#define hwiommu_sync_sg_for_cpu		machvec_dma_sync_sg
#define hwiommu_sync_single_for_device	machvec_dma_sync_single
#define hwiommu_sync_sg_for_device	machvec_dma_sync_sg


/*
 * Note: we need to make the determination of whether or not to use
 * the sw I/O TLB based purely on the device structure.  Anything else
 * would be unreliable or would be too intrusive.
 */
static inline int
use_swiotlb (struct device *dev)
{
	return dev && dev->dma_mask && !hwiommu_dma_supported(dev, *dev->dma_mask);
}

void __init
hwsw_init (void)
{
	/* default to a smallish 2MB sw I/O TLB */
	if (swiotlb_late_init_with_default_size (2 * (1<<20)) != 0) {
#ifdef CONFIG_IA64_GENERIC
		/* Better to have normal DMA than panic */
		printk(KERN_WARNING "%s: Failed to initialize software I/O TLB,"
		       " reverting to hpzx1 platform vector\n", __func__);
		machvec_init("hpzx1");
#else
		panic("Unable to initialize software I/O TLB services");
#endif
	}
}

void *
hwsw_alloc_coherent (struct device *dev, size_t size, dma_addr_t *dma_handle, gfp_t flags)
{
	if (use_swiotlb(dev))
		return swiotlb_alloc_coherent(dev, size, dma_handle, flags);
	else
		return hwiommu_alloc_coherent(dev, size, dma_handle, flags);
}

void
hwsw_free_coherent (struct device *dev, size_t size, void *vaddr, dma_addr_t dma_handle)
{
	if (use_swiotlb(dev))
		swiotlb_free_coherent(dev, size, vaddr, dma_handle);
	else
		hwiommu_free_coherent(dev, size, vaddr, dma_handle);
}

dma_addr_t
hwsw_map_single_attrs(struct device *dev, void *addr, size_t size, int dir,
		       struct dma_attrs *attrs)
{
	if (use_swiotlb(dev))
		return swiotlb_map_single_attrs(dev, addr, size, dir, attrs);
	else
		return hwiommu_map_single_attrs(dev, addr, size, dir, attrs);
}
EXPORT_SYMBOL(hwsw_map_single_attrs);

void
hwsw_unmap_single_attrs(struct device *dev, dma_addr_t iova, size_t size,
			 int dir, struct dma_attrs *attrs)
{
	if (use_swiotlb(dev))
		return swiotlb_unmap_single_attrs(dev, iova, size, dir, attrs);
	else
		return hwiommu_unmap_single_attrs(dev, iova, size, dir, attrs);
}
EXPORT_SYMBOL(hwsw_unmap_single_attrs);

int
hwsw_map_sg_attrs(struct device *dev, struct scatterlist *sglist, int nents,
		   int dir, struct dma_attrs *attrs)
{
	if (use_swiotlb(dev))
		return swiotlb_map_sg_attrs(dev, sglist, nents, dir, attrs);
	else
		return hwiommu_map_sg_attrs(dev, sglist, nents, dir, attrs);
}
EXPORT_SYMBOL(hwsw_map_sg_attrs);

void
hwsw_unmap_sg_attrs(struct device *dev, struct scatterlist *sglist, int nents,
		     int dir, struct dma_attrs *attrs)
{
	if (use_swiotlb(dev))
		return swiotlb_unmap_sg_attrs(dev, sglist, nents, dir, attrs);
	else
		return hwiommu_unmap_sg_attrs(dev, sglist, nents, dir, attrs);
}
EXPORT_SYMBOL(hwsw_unmap_sg_attrs);

void
hwsw_sync_single_for_cpu (struct device *dev, dma_addr_t addr, size_t size, int dir)
{
	if (use_swiotlb(dev))
		swiotlb_sync_single_for_cpu(dev, addr, size, dir);
	else
		hwiommu_sync_single_for_cpu(dev, addr, size, dir);
}

void
hwsw_sync_sg_for_cpu (struct device *dev, struct scatterlist *sg, int nelems, int dir)
{
	if (use_swiotlb(dev))
		swiotlb_sync_sg_for_cpu(dev, sg, nelems, dir);
	else
		hwiommu_sync_sg_for_cpu(dev, sg, nelems, dir);
}

void
hwsw_sync_single_for_device (struct device *dev, dma_addr_t addr, size_t size, int dir)
{
	if (use_swiotlb(dev))
		swiotlb_sync_single_for_device(dev, addr, size, dir);
	else
		hwiommu_sync_single_for_device(dev, addr, size, dir);
}

void
hwsw_sync_sg_for_device (struct device *dev, struct scatterlist *sg, int nelems, int dir)
{
	if (use_swiotlb(dev))
		swiotlb_sync_sg_for_device(dev, sg, nelems, dir);
	else
		hwiommu_sync_sg_for_device(dev, sg, nelems, dir);
}

int
hwsw_dma_supported (struct device *dev, u64 mask)
{
	if (hwiommu_dma_supported(dev, mask))
		return 1;
	return swiotlb_dma_supported(dev, mask);
}

int
hwsw_dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return hwiommu_dma_mapping_error(dev, dma_addr) ||
		swiotlb_dma_mapping_error(dev, dma_addr);
}

EXPORT_SYMBOL(hwsw_dma_mapping_error);
EXPORT_SYMBOL(hwsw_dma_supported);
EXPORT_SYMBOL(hwsw_alloc_coherent);
EXPORT_SYMBOL(hwsw_free_coherent);
EXPORT_SYMBOL(hwsw_sync_single_for_cpu);
EXPORT_SYMBOL(hwsw_sync_single_for_device);
EXPORT_SYMBOL(hwsw_sync_sg_for_cpu);
EXPORT_SYMBOL(hwsw_sync_sg_for_device);
