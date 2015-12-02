/*
 * Copyright (C) 2006 Benjamin Herrenschmidt, IBM Corporation
 *
 * Provide default implementations of the DMA mapping callbacks for
 * directly mapped busses.
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dma-debug.h>
#include <linux/gfp.h>
#include <linux/memblock.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <asm/vio.h>
#include <asm/bug.h>
#include <asm/machdep.h>
#include <asm/swiotlb.h>
#include <asm/iommu.h>

/*
 * Generic direct DMA implementation
 *
 * This implementation supports a per-device offset that can be applied if
 * the address at which memory is visible to devices is not 0. Platform code
 * can set archdata.dma_data to an unsigned long holding the offset. By
 * default the offset is PCI_DRAM_OFFSET.
 */

static u64 __maybe_unused get_pfn_limit(struct device *dev)
{
	u64 pfn = (dev->coherent_dma_mask >> PAGE_SHIFT) + 1;
	struct dev_archdata __maybe_unused *sd = &dev->archdata;

#ifdef CONFIG_SWIOTLB
	if (sd->max_direct_dma_addr && sd->dma_ops == &swiotlb_dma_ops)
		pfn = min_t(u64, pfn, sd->max_direct_dma_addr >> PAGE_SHIFT);
#endif

	return pfn;
}

static int dma_direct_dma_supported(struct device *dev, u64 mask)
{
#ifdef CONFIG_PPC64
	u64 limit = get_dma_offset(dev) + (memblock_end_of_DRAM() - 1);

	/* Limit fits in the mask, we are good */
	if (mask >= limit)
		return 1;

#ifdef CONFIG_FSL_SOC
	/* Freescale gets another chance via ZONE_DMA/ZONE_DMA32, however
	 * that will have to be refined if/when they support iommus
	 */
	return 1;
#endif
	/* Sorry ... */
	return 0;
#else
	return 1;
#endif
}

void *__dma_direct_alloc_coherent(struct device *dev, size_t size,
				  dma_addr_t *dma_handle, gfp_t flag,
				  struct dma_attrs *attrs)
{
	void *ret;
#ifdef CONFIG_NOT_COHERENT_CACHE
	ret = __dma_alloc_coherent(dev, size, dma_handle, flag);
	if (ret == NULL)
		return NULL;
	*dma_handle += get_dma_offset(dev);
	return ret;
#else
	struct page *page;
	int node = dev_to_node(dev);
#ifdef CONFIG_FSL_SOC
	u64 pfn = get_pfn_limit(dev);
	int zone;

	/*
	 * This code should be OK on other platforms, but we have drivers that
	 * don't set coherent_dma_mask. As a workaround we just ifdef it. This
	 * whole routine needs some serious cleanup.
	 */

	zone = dma_pfn_limit_to_zone(pfn);
	if (zone < 0) {
		dev_err(dev, "%s: No suitable zone for pfn %#llx\n",
			__func__, pfn);
		return NULL;
	}

	switch (zone) {
	case ZONE_DMA:
		flag |= GFP_DMA;
		break;
#ifdef CONFIG_ZONE_DMA32
	case ZONE_DMA32:
		flag |= GFP_DMA32;
		break;
#endif
	};
#endif /* CONFIG_FSL_SOC */

	/* ignore region specifiers */
	flag  &= ~(__GFP_HIGHMEM);

	page = alloc_pages_node(node, flag, get_order(size));
	if (page == NULL)
		return NULL;
	ret = page_address(page);
	memset(ret, 0, size);
	*dma_handle = __pa(ret) + get_dma_offset(dev);

	return ret;
#endif
}

void __dma_direct_free_coherent(struct device *dev, size_t size,
				void *vaddr, dma_addr_t dma_handle,
				struct dma_attrs *attrs)
{
#ifdef CONFIG_NOT_COHERENT_CACHE
	__dma_free_coherent(size, vaddr);
#else
	free_pages((unsigned long)vaddr, get_order(size));
#endif
}

static void *dma_direct_alloc_coherent(struct device *dev, size_t size,
				       dma_addr_t *dma_handle, gfp_t flag,
				       struct dma_attrs *attrs)
{
	struct iommu_table *iommu;

	/* The coherent mask may be smaller than the real mask, check if
	 * we can really use the direct ops
	 */
	if (dma_direct_dma_supported(dev, dev->coherent_dma_mask))
		return __dma_direct_alloc_coherent(dev, size, dma_handle,
						   flag, attrs);

	/* Ok we can't ... do we have an iommu ? If not, fail */
	iommu = get_iommu_table_base(dev);
	if (!iommu)
		return NULL;

	/* Try to use the iommu */
	return iommu_alloc_coherent(dev, iommu, size, dma_handle,
				    dev->coherent_dma_mask, flag,
				    dev_to_node(dev));
}

static void dma_direct_free_coherent(struct device *dev, size_t size,
				     void *vaddr, dma_addr_t dma_handle,
				     struct dma_attrs *attrs)
{
	struct iommu_table *iommu;

	/* See comments in dma_direct_alloc_coherent() */
	if (dma_direct_dma_supported(dev, dev->coherent_dma_mask))
		return __dma_direct_free_coherent(dev, size, vaddr, dma_handle,
						  attrs);
	/* Maybe we used an iommu ... */
	iommu = get_iommu_table_base(dev);

	/* If we hit that we should have never allocated in the first
	 * place so how come we are freeing ?
	 */
	if (WARN_ON(!iommu))
		return;
	iommu_free_coherent(iommu, size, vaddr, dma_handle);
}

int dma_direct_mmap_coherent(struct device *dev, struct vm_area_struct *vma,
			     void *cpu_addr, dma_addr_t handle, size_t size,
			     struct dma_attrs *attrs)
{
	unsigned long pfn;

#ifdef CONFIG_NOT_COHERENT_CACHE
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	pfn = __dma_get_coherent_pfn((unsigned long)cpu_addr);
#else
	pfn = page_to_pfn(virt_to_page(cpu_addr));
#endif
	return remap_pfn_range(vma, vma->vm_start,
			       pfn + vma->vm_pgoff,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot);
}

static int dma_direct_map_sg(struct device *dev, struct scatterlist *sgl,
			     int nents, enum dma_data_direction direction,
			     struct dma_attrs *attrs)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i) {
		sg->dma_address = sg_phys(sg) + get_dma_offset(dev);
		sg->dma_length = sg->length;
		__dma_sync_page(sg_page(sg), sg->offset, sg->length, direction);
	}

	return nents;
}

static void dma_direct_unmap_sg(struct device *dev, struct scatterlist *sg,
				int nents, enum dma_data_direction direction,
				struct dma_attrs *attrs)
{
}

static u64 dma_direct_get_required_mask(struct device *dev)
{
	u64 end, mask;

	end = memblock_end_of_DRAM() + get_dma_offset(dev);

	mask = 1ULL << (fls64(end) - 1);
	mask += mask - 1;

	return mask;
}

static inline dma_addr_t dma_direct_map_page(struct device *dev,
					     struct page *page,
					     unsigned long offset,
					     size_t size,
					     enum dma_data_direction dir,
					     struct dma_attrs *attrs)
{
	BUG_ON(dir == DMA_NONE);
	__dma_sync_page(page, offset, size, dir);
	return page_to_phys(page) + offset + get_dma_offset(dev);
}

static inline void dma_direct_unmap_page(struct device *dev,
					 dma_addr_t dma_address,
					 size_t size,
					 enum dma_data_direction direction,
					 struct dma_attrs *attrs)
{
}

#ifdef CONFIG_NOT_COHERENT_CACHE
static inline void dma_direct_sync_sg(struct device *dev,
		struct scatterlist *sgl, int nents,
		enum dma_data_direction direction)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i)
		__dma_sync_page(sg_page(sg), sg->offset, sg->length, direction);
}

static inline void dma_direct_sync_single(struct device *dev,
					  dma_addr_t dma_handle, size_t size,
					  enum dma_data_direction direction)
{
	__dma_sync(bus_to_virt(dma_handle), size, direction);
}
#endif

struct dma_map_ops dma_direct_ops = {
	.alloc				= dma_direct_alloc_coherent,
	.free				= dma_direct_free_coherent,
	.mmap				= dma_direct_mmap_coherent,
	.map_sg				= dma_direct_map_sg,
	.unmap_sg			= dma_direct_unmap_sg,
	.dma_supported			= dma_direct_dma_supported,
	.map_page			= dma_direct_map_page,
	.unmap_page			= dma_direct_unmap_page,
	.get_required_mask		= dma_direct_get_required_mask,
#ifdef CONFIG_NOT_COHERENT_CACHE
	.sync_single_for_cpu 		= dma_direct_sync_single,
	.sync_single_for_device 	= dma_direct_sync_single,
	.sync_sg_for_cpu 		= dma_direct_sync_sg,
	.sync_sg_for_device 		= dma_direct_sync_sg,
#endif
};
EXPORT_SYMBOL(dma_direct_ops);

int dma_set_coherent_mask(struct device *dev, u64 mask)
{
	if (!dma_supported(dev, mask)) {
		/*
		 * We need to special case the direct DMA ops which can
		 * support a fallback for coherent allocations. There
		 * is no dma_op->set_coherent_mask() so we have to do
		 * things the hard way:
		 */
		if (get_dma_ops(dev) != &dma_direct_ops ||
		    get_iommu_table_base(dev) == NULL ||
		    !dma_iommu_dma_supported(dev, mask))
			return -EIO;
	}
	dev->coherent_dma_mask = mask;
	return 0;
}
EXPORT_SYMBOL(dma_set_coherent_mask);

#define PREALLOC_DMA_DEBUG_ENTRIES (1 << 16)

int __dma_set_mask(struct device *dev, u64 dma_mask)
{
	struct dma_map_ops *dma_ops = get_dma_ops(dev);

	if ((dma_ops != NULL) && (dma_ops->set_dma_mask != NULL))
		return dma_ops->set_dma_mask(dev, dma_mask);
	if (!dev->dma_mask || !dma_supported(dev, dma_mask))
		return -EIO;
	*dev->dma_mask = dma_mask;
	return 0;
}

int dma_set_mask(struct device *dev, u64 dma_mask)
{
	if (ppc_md.dma_set_mask)
		return ppc_md.dma_set_mask(dev, dma_mask);

	if (dev_is_pci(dev)) {
		struct pci_dev *pdev = to_pci_dev(dev);
		struct pci_controller *phb = pci_bus_to_host(pdev->bus);
		if (phb->controller_ops.dma_set_mask)
			return phb->controller_ops.dma_set_mask(pdev, dma_mask);
	}

	return __dma_set_mask(dev, dma_mask);
}
EXPORT_SYMBOL(dma_set_mask);

u64 __dma_get_required_mask(struct device *dev)
{
	struct dma_map_ops *dma_ops = get_dma_ops(dev);

	if (unlikely(dma_ops == NULL))
		return 0;

	if (dma_ops->get_required_mask)
		return dma_ops->get_required_mask(dev);

	return DMA_BIT_MASK(8 * sizeof(dma_addr_t));
}

u64 dma_get_required_mask(struct device *dev)
{
	if (ppc_md.dma_get_required_mask)
		return ppc_md.dma_get_required_mask(dev);

	if (dev_is_pci(dev)) {
		struct pci_dev *pdev = to_pci_dev(dev);
		struct pci_controller *phb = pci_bus_to_host(pdev->bus);
		if (phb->controller_ops.dma_get_required_mask)
			return phb->controller_ops.dma_get_required_mask(pdev);
	}

	return __dma_get_required_mask(dev);
}
EXPORT_SYMBOL_GPL(dma_get_required_mask);

static int __init dma_init(void)
{
	dma_debug_init(PREALLOC_DMA_DEBUG_ENTRIES);
#ifdef CONFIG_PCI
	dma_debug_add_bus(&pci_bus_type);
#endif
#ifdef CONFIG_IBMVIO
	dma_debug_add_bus(&vio_bus_type);
#endif

       return 0;
}
fs_initcall(dma_init);

