/*
 * Dynamic DMA mapping support.
 *
 * On i386 there is no hardware dynamic DMA address translation,
 * so consistent alloc/free are merely page allocation/freeing.
 * The rest of the dynamic DMA mapping interface is implemented
 * in asm/pci.h.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <asm/io.h>

/* Dummy device used for NULL arguments (normally ISA). Better would
   be probably a smaller DMA mask, but this is bug-to-bug compatible
   to i386. */
struct device fallback_dev = {
	.bus_id = "fallback device",
	.coherent_dma_mask = DMA_32BIT_MASK,
	.dma_mask = &fallback_dev.coherent_dma_mask,
};


static int dma_alloc_from_coherent_mem(struct device *dev, ssize_t size,
				       dma_addr_t *dma_handle, void **ret)
{
	struct dma_coherent_mem *mem = dev ? dev->dma_mem : NULL;
	int order = get_order(size);

	if (mem) {
		int page = bitmap_find_free_region(mem->bitmap, mem->size,
						     order);
		if (page >= 0) {
			*dma_handle = mem->device_base + (page << PAGE_SHIFT);
			*ret = mem->virt_base + (page << PAGE_SHIFT);
			memset(*ret, 0, size);
		}
		if (mem->flags & DMA_MEMORY_EXCLUSIVE)
			*ret = NULL;
	}
	return (mem != NULL);
}

static int dma_release_coherent(struct device *dev, int order, void *vaddr)
{
	struct dma_coherent_mem *mem = dev ? dev->dma_mem : NULL;

	if (mem && vaddr >= mem->virt_base && vaddr <
		   (mem->virt_base + (mem->size << PAGE_SHIFT))) {
		int page = (vaddr - mem->virt_base) >> PAGE_SHIFT;

		bitmap_release_region(mem->bitmap, page, order);
		return 1;
	}
	return 0;
}

/* Allocate DMA memory on node near device */
noinline struct page *
dma_alloc_pages(struct device *dev, gfp_t gfp, unsigned order)
{
	int node;

	node = dev_to_node(dev);

	return alloc_pages_node(node, gfp, order);
}

void *dma_alloc_coherent(struct device *dev, size_t size,
			   dma_addr_t *dma_handle, gfp_t gfp)
{
	void *ret = NULL;
	struct page *page;
	dma_addr_t bus;
	int order = get_order(size);
	unsigned long dma_mask = 0;

	/* ignore region specifiers */
	gfp &= ~(__GFP_DMA | __GFP_HIGHMEM | __GFP_DMA32);

	if (dma_alloc_from_coherent_mem(dev, size, dma_handle, &ret))
		return ret;

	if (!dev)
		dev = &fallback_dev;

	dma_mask = dev->coherent_dma_mask;
	if (dma_mask == 0)
		dma_mask = DMA_32BIT_MASK;

	if (dev->dma_mask == NULL)
		return NULL;

	/* Don't invoke OOM killer */
	gfp |= __GFP_NORETRY;
again:
	page = dma_alloc_pages(dev, gfp, order);
	if (page == NULL)
		return NULL;

	{
		int high, mmu;
		bus = page_to_phys(page);
		ret = page_address(page);
		high = (bus + size) >= dma_mask;
		mmu = high;
		if (force_iommu && !(gfp & GFP_DMA))
			mmu = 1;
		else if (high) {
			free_pages((unsigned long)ret,
				   get_order(size));

			/* Don't use the 16MB ZONE_DMA unless absolutely
			   needed. It's better to use remapping first. */
			if (dma_mask < DMA_32BIT_MASK && !(gfp & GFP_DMA)) {
				gfp = (gfp & ~GFP_DMA32) | GFP_DMA;
				goto again;
			}

			/* Let low level make its own zone decisions */
			gfp &= ~(GFP_DMA32|GFP_DMA);

			if (dma_ops->alloc_coherent)
				return dma_ops->alloc_coherent(dev, size,
							   dma_handle, gfp);
			return NULL;

		}
		memset(ret, 0, size);
		if (!mmu) {
			*dma_handle = bus;
			return ret;
		}
	}

	if (dma_ops->alloc_coherent) {
		free_pages((unsigned long)ret, get_order(size));
		gfp &= ~(GFP_DMA|GFP_DMA32);
		return dma_ops->alloc_coherent(dev, size, dma_handle, gfp);
	}

	if (dma_ops->map_simple) {
		*dma_handle = dma_ops->map_simple(dev, virt_to_phys(ret),
					      size,
					      PCI_DMA_BIDIRECTIONAL);
		if (*dma_handle != bad_dma_address)
			return ret;
	}

	if (panic_on_overflow)
		panic("dma_alloc_coherent: IOMMU overflow by %lu bytes\n",
		      (unsigned long)size);
	free_pages((unsigned long)ret, get_order(size));
	return NULL;
}
EXPORT_SYMBOL(dma_alloc_coherent);

void dma_free_coherent(struct device *dev, size_t size,
			 void *vaddr, dma_addr_t dma_handle)
{
	int order = get_order(size);

	WARN_ON(irqs_disabled());	/* for portability */
	if (dma_release_coherent(dev, order, vaddr))
		return;
	if (dma_ops->unmap_single)
		dma_ops->unmap_single(dev, dma_handle, size, 0);
	free_pages((unsigned long)vaddr, order);
}
EXPORT_SYMBOL(dma_free_coherent);
