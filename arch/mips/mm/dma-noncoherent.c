// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2000  Ani Joshi <ajoshi@unixbox.com>
 * Copyright (C) 2000, 2001, 06	 Ralf Baechle <ralf@linux-mips.org>
 * swiped from i386, and cloned for MIPS by Geert, polished by Ralf.
 */
#include <linux/dma-direct.h>
#include <linux/dma-noncoherent.h>
#include <linux/dma-contiguous.h>
#include <linux/highmem.h>

#include <asm/cache.h>
#include <asm/cpu-type.h>
#include <asm/dma-coherence.h>
#include <asm/io.h>

/*
 * The affected CPUs below in 'cpu_needs_post_dma_flush()' can speculatively
 * fill random cachelines with stale data at any time, requiring an extra
 * flush post-DMA.
 *
 * Warning on the terminology - Linux calls an uncached area coherent;  MIPS
 * terminology calls memory areas with hardware maintained coherency coherent.
 *
 * Note that the R14000 and R16000 should also be checked for in this condition.
 * However this function is only called on non-I/O-coherent systems and only the
 * R10000 and R12000 are used in such systems, the SGI IP28 Indigo² rsp.
 * SGI IP32 aka O2.
 */
static inline bool cpu_needs_post_dma_flush(struct device *dev)
{
	switch (boot_cpu_type()) {
	case CPU_R10000:
	case CPU_R12000:
	case CPU_BMIPS5000:
		return true;
	default:
		/*
		 * Presence of MAARs suggests that the CPU supports
		 * speculatively prefetching data, and therefore requires
		 * the post-DMA flush/invalidate.
		 */
		return cpu_has_maar;
	}
}

void arch_dma_prep_coherent(struct page *page, size_t size)
{
	dma_cache_wback_inv((unsigned long)page_address(page), size);
}

void *uncached_kernel_address(void *addr)
{
	return (void *)(__pa(addr) + UNCAC_BASE);
}

void *cached_kernel_address(void *addr)
{
	return __va(addr) - UNCAC_BASE;
}

long arch_dma_coherent_to_pfn(struct device *dev, void *cpu_addr,
		dma_addr_t dma_addr)
{
	return page_to_pfn(virt_to_page(cached_kernel_address(cpu_addr)));
}

pgprot_t arch_dma_mmap_pgprot(struct device *dev, pgprot_t prot,
		unsigned long attrs)
{
	if (attrs & DMA_ATTR_WRITE_COMBINE)
		return pgprot_writecombine(prot);
	return pgprot_noncached(prot);
}

static inline void dma_sync_virt(void *addr, size_t size,
		enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_TO_DEVICE:
		dma_cache_wback((unsigned long)addr, size);
		break;

	case DMA_FROM_DEVICE:
		dma_cache_inv((unsigned long)addr, size);
		break;

	case DMA_BIDIRECTIONAL:
		dma_cache_wback_inv((unsigned long)addr, size);
		break;

	default:
		BUG();
	}
}

/*
 * A single sg entry may refer to multiple physically contiguous pages.  But
 * we still need to process highmem pages individually.  If highmem is not
 * configured then the bulk of this loop gets optimized out.
 */
static inline void dma_sync_phys(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
	struct page *page = pfn_to_page(paddr >> PAGE_SHIFT);
	unsigned long offset = paddr & ~PAGE_MASK;
	size_t left = size;

	do {
		size_t len = left;

		if (PageHighMem(page)) {
			void *addr;

			if (offset + len > PAGE_SIZE)
				len = PAGE_SIZE - offset;

			addr = kmap_atomic(page);
			dma_sync_virt(addr + offset, len, dir);
			kunmap_atomic(addr);
		} else
			dma_sync_virt(page_address(page) + offset, size, dir);
		offset = 0;
		page++;
		left -= len;
	} while (left);
}

void arch_sync_dma_for_device(struct device *dev, phys_addr_t paddr,
		size_t size, enum dma_data_direction dir)
{
	dma_sync_phys(paddr, size, dir);
}

#ifdef CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU
void arch_sync_dma_for_cpu(struct device *dev, phys_addr_t paddr,
		size_t size, enum dma_data_direction dir)
{
	if (cpu_needs_post_dma_flush(dev))
		dma_sync_phys(paddr, size, dir);
}
#endif

void arch_dma_cache_sync(struct device *dev, void *vaddr, size_t size,
		enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);

	dma_sync_virt(vaddr, size, direction);
}

#ifdef CONFIG_DMA_PERDEV_COHERENT
void arch_setup_dma_ops(struct device *dev, u64 dma_base, u64 size,
		const struct iommu_ops *iommu, bool coherent)
{
	dev->dma_coherent = coherent;
}
#endif
