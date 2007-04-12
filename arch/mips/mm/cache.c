/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 2003, 07 by Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2007 MIPS Technologies, Inc.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/cacheflush.h>
#include <asm/processor.h>
#include <asm/cpu.h>
#include <asm/cpu-features.h>

/* Cache operations. */
void (*flush_cache_all)(void);
void (*__flush_cache_all)(void);
void (*flush_cache_mm)(struct mm_struct *mm);
void (*flush_cache_range)(struct vm_area_struct *vma, unsigned long start,
	unsigned long end);
void (*flush_cache_page)(struct vm_area_struct *vma, unsigned long page,
	unsigned long pfn);
void (*flush_icache_range)(unsigned long start, unsigned long end);

/* MIPS specific cache operations */
void (*flush_cache_sigtramp)(unsigned long addr);
void (*local_flush_data_cache_page)(void * addr);
void (*flush_data_cache_page)(unsigned long addr);
void (*flush_icache_all)(void);

EXPORT_SYMBOL_GPL(local_flush_data_cache_page);
EXPORT_SYMBOL(flush_data_cache_page);

#ifdef CONFIG_DMA_NONCOHERENT

/* DMA cache operations. */
void (*_dma_cache_wback_inv)(unsigned long start, unsigned long size);
void (*_dma_cache_wback)(unsigned long start, unsigned long size);
void (*_dma_cache_inv)(unsigned long start, unsigned long size);

EXPORT_SYMBOL(_dma_cache_wback_inv);
EXPORT_SYMBOL(_dma_cache_wback);
EXPORT_SYMBOL(_dma_cache_inv);

#endif /* CONFIG_DMA_NONCOHERENT */

/*
 * We could optimize the case where the cache argument is not BCACHE but
 * that seems very atypical use ...
 */
asmlinkage int sys_cacheflush(unsigned long addr,
	unsigned long bytes, unsigned int cache)
{
	if (bytes == 0)
		return 0;
	if (!access_ok(VERIFY_WRITE, (void __user *) addr, bytes))
		return -EFAULT;

	flush_icache_range(addr, addr + bytes);

	return 0;
}

void __flush_dcache_page(struct page *page)
{
	struct address_space *mapping = page_mapping(page);
	unsigned long addr;

	if (PageHighMem(page))
		return;
	if (mapping && !mapping_mapped(mapping)) {
		SetPageDcacheDirty(page);
		return;
	}

	/*
	 * We could delay the flush for the !page_mapping case too.  But that
	 * case is for exec env/arg pages and those are %99 certainly going to
	 * get faulted into the tlb (and thus flushed) anyways.
	 */
	addr = (unsigned long) page_address(page);
	flush_data_cache_page(addr);
}

EXPORT_SYMBOL(__flush_dcache_page);

void __flush_anon_page(struct page *page, unsigned long vmaddr)
{
	if (pages_do_alias((unsigned long)page_address(page), vmaddr)) {
		void *kaddr;

		kaddr = kmap_coherent(page, vmaddr);
		flush_data_cache_page((unsigned long)kaddr);
		kunmap_coherent(kaddr);
	}
}

EXPORT_SYMBOL(__flush_anon_page);

void __update_cache(struct vm_area_struct *vma, unsigned long address,
	pte_t pte)
{
	struct page *page;
	unsigned long pfn, addr;
	int exec = (vma->vm_flags & VM_EXEC) && !cpu_has_ic_fills_f_dc;

	pfn = pte_pfn(pte);
	if (unlikely(!pfn_valid(pfn)))
		return;
	page = pfn_to_page(pfn);
	if (page_mapping(page) && Page_dcache_dirty(page)) {
		addr = (unsigned long) page_address(page);
		if (exec || pages_do_alias(addr, address & PAGE_MASK))
			flush_data_cache_page(addr);
		ClearPageDcacheDirty(page);
	}
}

static char cache_panic[] __initdata = "Yeee, unsupported cache architecture.";

void __init cpu_cache_init(void)
{
	if (cpu_has_3k_cache) {
		extern void __weak r3k_cache_init(void);

		r3k_cache_init();
		return;
	}
	if (cpu_has_6k_cache) {
		extern void __weak r6k_cache_init(void);

		r6k_cache_init();
		return;
	}
	if (cpu_has_4k_cache) {
		extern void __weak r4k_cache_init(void);

		r4k_cache_init();
		return;
	}
	if (cpu_has_8k_cache) {
		extern void __weak r8k_cache_init(void);

		r8k_cache_init();
		return;
	}
	if (cpu_has_tx39_cache) {
		extern void __weak tx39_cache_init(void);

		tx39_cache_init();
		return;
	}
	if (cpu_has_sb1_cache) {
		extern void __weak sb1_cache_init(void);

		sb1_cache_init();
		return;
	}

	panic(cache_panic);
}
