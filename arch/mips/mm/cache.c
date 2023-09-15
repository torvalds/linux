/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 2003, 06, 07 by Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2007 MIPS Technologies, Inc.
 */
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/kernel.h>
#include <linux/linkage.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>

#include <asm/bcache.h>
#include <asm/cacheflush.h>
#include <asm/processor.h>
#include <asm/cpu.h>
#include <asm/cpu-features.h>
#include <asm/setup.h>
#include <asm/pgtable.h>

/* Cache operations. */
void (*flush_cache_all)(void);
void (*__flush_cache_all)(void);
EXPORT_SYMBOL_GPL(__flush_cache_all);
void (*flush_cache_mm)(struct mm_struct *mm);
void (*flush_cache_range)(struct vm_area_struct *vma, unsigned long start,
	unsigned long end);
void (*flush_cache_page)(struct vm_area_struct *vma, unsigned long page,
	unsigned long pfn);
void (*flush_icache_range)(unsigned long start, unsigned long end);
EXPORT_SYMBOL_GPL(flush_icache_range);
void (*local_flush_icache_range)(unsigned long start, unsigned long end);
EXPORT_SYMBOL_GPL(local_flush_icache_range);
void (*__flush_icache_user_range)(unsigned long start, unsigned long end);
void (*__local_flush_icache_user_range)(unsigned long start, unsigned long end);
EXPORT_SYMBOL_GPL(__local_flush_icache_user_range);

void (*__flush_cache_vmap)(void);
void (*__flush_cache_vunmap)(void);

void (*__flush_kernel_vmap_range)(unsigned long vaddr, int size);
EXPORT_SYMBOL_GPL(__flush_kernel_vmap_range);

/* MIPS specific cache operations */
void (*flush_data_cache_page)(unsigned long addr);
void (*flush_icache_all)(void);

EXPORT_SYMBOL(flush_data_cache_page);
EXPORT_SYMBOL(flush_icache_all);

/*
 * Dummy cache handling routine
 */

void cache_noop(void) {}

#ifdef CONFIG_BOARD_SCACHE

static struct bcache_ops no_sc_ops = {
	.bc_enable = (void *)cache_noop,
	.bc_disable = (void *)cache_noop,
	.bc_wback_inv = (void *)cache_noop,
	.bc_inv = (void *)cache_noop
};

struct bcache_ops *bcops = &no_sc_ops;
#endif

#ifdef CONFIG_DMA_NONCOHERENT

/* DMA cache operations. */
void (*_dma_cache_wback_inv)(unsigned long start, unsigned long size);
void (*_dma_cache_wback)(unsigned long start, unsigned long size);
void (*_dma_cache_inv)(unsigned long start, unsigned long size);

#endif /* CONFIG_DMA_NONCOHERENT */

/*
 * We could optimize the case where the cache argument is not BCACHE but
 * that seems very atypical use ...
 */
SYSCALL_DEFINE3(cacheflush, unsigned long, addr, unsigned long, bytes,
	unsigned int, cache)
{
	if (bytes == 0)
		return 0;
	if (!access_ok((void __user *) addr, bytes))
		return -EFAULT;

	__flush_icache_user_range(addr, addr + bytes);

	return 0;
}

void __flush_dcache_pages(struct page *page, unsigned int nr)
{
	struct folio *folio = page_folio(page);
	struct address_space *mapping = folio_flush_mapping(folio);
	unsigned long addr;
	unsigned int i;

	if (mapping && !mapping_mapped(mapping)) {
		folio_set_dcache_dirty(folio);
		return;
	}

	/*
	 * We could delay the flush for the !page_mapping case too.  But that
	 * case is for exec env/arg pages and those are %99 certainly going to
	 * get faulted into the tlb (and thus flushed) anyways.
	 */
	for (i = 0; i < nr; i++) {
		addr = (unsigned long)kmap_local_page(page + i);
		flush_data_cache_page(addr);
		kunmap_local((void *)addr);
	}
}
EXPORT_SYMBOL(__flush_dcache_pages);

void __flush_anon_page(struct page *page, unsigned long vmaddr)
{
	unsigned long addr = (unsigned long) page_address(page);
	struct folio *folio = page_folio(page);

	if (pages_do_alias(addr, vmaddr)) {
		if (folio_mapped(folio) && !folio_test_dcache_dirty(folio)) {
			void *kaddr;

			kaddr = kmap_coherent(page, vmaddr);
			flush_data_cache_page((unsigned long)kaddr);
			kunmap_coherent();
		} else
			flush_data_cache_page(addr);
	}
}

EXPORT_SYMBOL(__flush_anon_page);

void __update_cache(unsigned long address, pte_t pte)
{
	struct folio *folio;
	unsigned long pfn, addr;
	int exec = !pte_no_exec(pte) && !cpu_has_ic_fills_f_dc;
	unsigned int i;

	pfn = pte_pfn(pte);
	if (unlikely(!pfn_valid(pfn)))
		return;

	folio = page_folio(pfn_to_page(pfn));
	address &= PAGE_MASK;
	address -= offset_in_folio(folio, pfn << PAGE_SHIFT);

	if (folio_test_dcache_dirty(folio)) {
		for (i = 0; i < folio_nr_pages(folio); i++) {
			addr = (unsigned long)kmap_local_folio(folio, i);

			if (exec || pages_do_alias(addr, address))
				flush_data_cache_page(addr);
			kunmap_local((void *)addr);
			address += PAGE_SIZE;
		}
		folio_clear_dcache_dirty(folio);
	}
}

unsigned long _page_cachable_default;
EXPORT_SYMBOL(_page_cachable_default);

#define PM(p)	__pgprot(_page_cachable_default | (p))

static pgprot_t protection_map[16] __ro_after_init;
DECLARE_VM_GET_PAGE_PROT

static inline void setup_protection_map(void)
{
	protection_map[0]  = PM(_PAGE_PRESENT | _PAGE_NO_EXEC | _PAGE_NO_READ);
	protection_map[1]  = PM(_PAGE_PRESENT | _PAGE_NO_EXEC);
	protection_map[2]  = PM(_PAGE_PRESENT | _PAGE_NO_EXEC | _PAGE_NO_READ);
	protection_map[3]  = PM(_PAGE_PRESENT | _PAGE_NO_EXEC);
	protection_map[4]  = PM(_PAGE_PRESENT);
	protection_map[5]  = PM(_PAGE_PRESENT);
	protection_map[6]  = PM(_PAGE_PRESENT);
	protection_map[7]  = PM(_PAGE_PRESENT);

	protection_map[8]  = PM(_PAGE_PRESENT | _PAGE_NO_EXEC | _PAGE_NO_READ);
	protection_map[9]  = PM(_PAGE_PRESENT | _PAGE_NO_EXEC);
	protection_map[10] = PM(_PAGE_PRESENT | _PAGE_NO_EXEC | _PAGE_WRITE |
				_PAGE_NO_READ);
	protection_map[11] = PM(_PAGE_PRESENT | _PAGE_NO_EXEC | _PAGE_WRITE);
	protection_map[12] = PM(_PAGE_PRESENT);
	protection_map[13] = PM(_PAGE_PRESENT);
	protection_map[14] = PM(_PAGE_PRESENT | _PAGE_WRITE);
	protection_map[15] = PM(_PAGE_PRESENT | _PAGE_WRITE);
}

#undef PM

void cpu_cache_init(void)
{
	if (cpu_has_3k_cache) {
		extern void __weak r3k_cache_init(void);

		r3k_cache_init();
	}
	if (cpu_has_4k_cache) {
		extern void __weak r4k_cache_init(void);

		r4k_cache_init();
	}

	if (cpu_has_octeon_cache) {
		extern void __weak octeon_cache_init(void);

		octeon_cache_init();
	}

	setup_protection_map();
}
