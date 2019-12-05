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
void arch_dma_prep_coherent(struct page *page, size_t size)
{
	cache_push(page_to_phys(page), size);
}

pgprot_t pgprot_dmacoherent(pgprot_t prot)
{
	if (CPU_IS_040_OR_060) {
		pgprot_val(prot) &= ~_PAGE_CACHE040;
		pgprot_val(prot) |= _PAGE_GLOBAL040 | _PAGE_NOCACHE_S;
	} else {
		pgprot_val(prot) |= _PAGE_NOCACHE030;
	}
	return prot;
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

void arch_sync_dma_for_device(phys_addr_t handle, size_t size,
		enum dma_data_direction dir)
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
