/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */





#include <mali_kbase.h>

#include <linux/io.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/dma-mapping.h>
#include <linux/mutex.h>
#include <asm/cacheflush.h>

void kbase_sync_to_memory(phys_addr_t paddr, void *vaddr, size_t sz)
{
#ifdef CONFIG_ARM
	__cpuc_flush_dcache_area(vaddr, sz);
	outer_flush_range(paddr, paddr + sz);
#elif defined(CONFIG_ARM64)
	/* TODO (MID64-46): There's no other suitable cache flush function for ARM64 */
	flush_cache_all();
#elif defined(CONFIG_X86)
	struct scatterlist scl = { 0, };
	sg_set_page(&scl, pfn_to_page(PFN_DOWN(paddr)), sz, paddr & (PAGE_SIZE - 1));
	dma_sync_sg_for_cpu(NULL, &scl, 1, DMA_TO_DEVICE);
	mb();			/* for outer_sync (if needed) */
#else
#error Implement cache maintenance for your architecture here
#endif
}

void kbase_sync_to_cpu(phys_addr_t paddr, void *vaddr, size_t sz)
{
#ifdef CONFIG_ARM
	__cpuc_flush_dcache_area(vaddr, sz);
	outer_flush_range(paddr, paddr + sz);
#elif defined(CONFIG_ARM64)
	/* TODO (MID64-46): There's no other suitable cache flush function for ARM64 */
	flush_cache_all();
#elif defined(CONFIG_X86)
	struct scatterlist scl = { 0, };
	sg_set_page(&scl, pfn_to_page(PFN_DOWN(paddr)), sz, paddr & (PAGE_SIZE - 1));
	dma_sync_sg_for_cpu(NULL, &scl, 1, DMA_FROM_DEVICE);
#else
#error Implement cache maintenance for your architecture here
#endif
}
