#include <linux/mm.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/swiotlb.h>
#include <linux/bootmem.h>

#include <asm/bootinfo.h>
#include <boot_param.h>
#include <dma-coherence.h>

static void *loongson_dma_alloc_coherent(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp, unsigned long attrs)
{
	void *ret;

	/* ignore region specifiers */
	gfp &= ~(__GFP_DMA | __GFP_DMA32 | __GFP_HIGHMEM);

	if ((IS_ENABLED(CONFIG_ISA) && dev == NULL) ||
	    (IS_ENABLED(CONFIG_ZONE_DMA) &&
	     dev->coherent_dma_mask < DMA_BIT_MASK(32)))
		gfp |= __GFP_DMA;
	else if (IS_ENABLED(CONFIG_ZONE_DMA32) &&
		 dev->coherent_dma_mask < DMA_BIT_MASK(40))
		gfp |= __GFP_DMA32;

	gfp |= __GFP_NORETRY;

	ret = swiotlb_alloc_coherent(dev, size, dma_handle, gfp);
	mb();
	return ret;
}

static void loongson_dma_free_coherent(struct device *dev, size_t size,
		void *vaddr, dma_addr_t dma_handle, unsigned long attrs)
{
	swiotlb_free_coherent(dev, size, vaddr, dma_handle);
}

static dma_addr_t loongson_dma_map_page(struct device *dev, struct page *page,
				unsigned long offset, size_t size,
				enum dma_data_direction dir,
				unsigned long attrs)
{
	dma_addr_t daddr = swiotlb_map_page(dev, page, offset, size,
					dir, attrs);
	mb();
	return daddr;
}

static int loongson_dma_map_sg(struct device *dev, struct scatterlist *sg,
				int nents, enum dma_data_direction dir,
				unsigned long attrs)
{
	int r = swiotlb_map_sg_attrs(dev, sg, nents, dir, attrs);
	mb();

	return r;
}

static void loongson_dma_sync_single_for_device(struct device *dev,
				dma_addr_t dma_handle, size_t size,
				enum dma_data_direction dir)
{
	swiotlb_sync_single_for_device(dev, dma_handle, size, dir);
	mb();
}

static void loongson_dma_sync_sg_for_device(struct device *dev,
				struct scatterlist *sg, int nents,
				enum dma_data_direction dir)
{
	swiotlb_sync_sg_for_device(dev, sg, nents, dir);
	mb();
}

static int loongson_dma_set_mask(struct device *dev, u64 mask)
{
	if (!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;

	if (mask > DMA_BIT_MASK(loongson_sysconf.dma_mask_bits)) {
		*dev->dma_mask = DMA_BIT_MASK(loongson_sysconf.dma_mask_bits);
		return -EIO;
	}

	*dev->dma_mask = mask;

	return 0;
}

dma_addr_t phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	long nid;
#ifdef CONFIG_PHYS48_TO_HT40
	/* We extract 2bit node id (bit 44~47, only bit 44~45 used now) from
	 * Loongson-3's 48bit address space and embed it into 40bit */
	nid = (paddr >> 44) & 0x3;
	paddr = ((nid << 44) ^ paddr) | (nid << 37);
#endif
	return paddr;
}

phys_addr_t dma_to_phys(struct device *dev, dma_addr_t daddr)
{
	long nid;
#ifdef CONFIG_PHYS48_TO_HT40
	/* We extract 2bit node id (bit 44~47, only bit 44~45 used now) from
	 * Loongson-3's 48bit address space and embed it into 40bit */
	nid = (daddr >> 37) & 0x3;
	daddr = ((nid << 37) ^ daddr) | (nid << 44);
#endif
	return daddr;
}

static const struct dma_map_ops loongson_dma_map_ops = {
	.alloc = loongson_dma_alloc_coherent,
	.free = loongson_dma_free_coherent,
	.map_page = loongson_dma_map_page,
	.unmap_page = swiotlb_unmap_page,
	.map_sg = loongson_dma_map_sg,
	.unmap_sg = swiotlb_unmap_sg_attrs,
	.sync_single_for_cpu = swiotlb_sync_single_for_cpu,
	.sync_single_for_device = loongson_dma_sync_single_for_device,
	.sync_sg_for_cpu = swiotlb_sync_sg_for_cpu,
	.sync_sg_for_device = loongson_dma_sync_sg_for_device,
	.mapping_error = swiotlb_dma_mapping_error,
	.dma_supported = swiotlb_dma_supported,
	.set_dma_mask = loongson_dma_set_mask
};

void __init plat_swiotlb_setup(void)
{
	swiotlb_init(1);
	mips_dma_map_ops = &loongson_dma_map_ops;
}
