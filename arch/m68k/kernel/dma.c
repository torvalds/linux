/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#undef DEBUG

#include <linux/dma-noncoherent.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/export.h>

#include <asm/pgalloc.h>

#if defined(CONFIG_MMU) && !defined(CONFIG_COLDFIRE)

void *arch_dma_alloc(struct device *dev, size_t size, dma_addr_t *handle,
		gfp_t flag, unsigned long attrs)
{
	struct page *page, **map;
	pgprot_t pgprot;
	void *addr;
	int i, order;

	pr_debug("dma_alloc_coherent: %d,%x\n", size, flag);

	size = PAGE_ALIGN(size);
	order = get_order(size);

	page = alloc_pages(flag | __GFP_ZERO, order);
	if (!page)
		return NULL;

	*handle = page_to_phys(page);
	map = kmalloc(sizeof(struct page *) << order, flag & ~__GFP_DMA);
	if (!map) {
		__free_pages(page, order);
		return NULL;
	}
	split_page(page, order);

	order = 1 << order;
	size >>= PAGE_SHIFT;
	map[0] = page;
	for (i = 1; i < size; i++)
		map[i] = page + i;
	for (; i < order; i++)
		__free_page(page + i);
	pgprot = __pgprot(_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_DIRTY);
	if (CPU_IS_040_OR_060)
		pgprot_val(pgprot) |= _PAGE_GLOBAL040 | _PAGE_NOCACHE_S;
	else
		pgprot_val(pgprot) |= _PAGE_NOCACHE030;
	addr = vmap(map, size, VM_MAP, pgprot);
	kfree(map);

	return addr;
}

void arch_dma_free(struct device *dev, size_t size, void *addr,
		dma_addr_t handle, unsigned long attrs)
{
	pr_debug("dma_free_coherent: %p, %x\n", addr, handle);
	vfree(addr);
}

#else

#include <asm/cacheflush.h>

void *arch_dma_alloc(struct device *dev, size_t size, dma_addr_t *dma_handle,
		gfp_t gfp, unsigned long attrs)
{
	void *ret;

	if (dev == NULL || (*dev->dma_mask < 0xffffffff))
		gfp |= GFP_DMA;
	ret = (void *)__get_free_pages(gfp, get_order(size));

	if (ret != NULL) {
		memset(ret, 0, size);
		*dma_handle = virt_to_phys(ret);
	}
	return ret;
}

void arch_dma_free(struct device *dev, size_t size, void *vaddr,
		dma_addr_t dma_handle, unsigned long attrs)
{
	free_pages((unsigned long)vaddr, get_order(size));
}

#endif /* CONFIG_MMU && !CONFIG_COLDFIRE */

void arch_sync_dma_for_device(struct device *dev, phys_addr_t handle,
		size_t size, enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_BIDIRECTIONAL:
	case DMA_TO_DEVICE:
		cache_push(handle, size);
		break;
	case DMA_FROM_DEVICE:
		cache_clear(handle, size);
		break;
	default:
		pr_err_ratelimited("dma_sync_single_for_device: unsupported dir %u\n",
				   dir);
		break;
	}
}

void arch_setup_pdev_archdata(struct platform_device *pdev)
{
	if (pdev->dev.coherent_dma_mask == DMA_MASK_NONE &&
	    pdev->dev.dma_mask == NULL) {
		pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
		pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;
	}
}
