/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/dma-map-ops.h>
#include <linux/kernel.h>
#include <asm/cacheflush.h>

#ifndef CONFIG_COLDFIRE
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
