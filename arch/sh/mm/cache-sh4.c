/*
 * arch/sh/mm/cache-sh4.c
 *
 * Copyright (C) 1999, 2000, 2002  Niibe Yutaka
 * Copyright (C) 2001 - 2009  Paul Mundt
 * Copyright (C) 2003  Richard Curnow
 * Copyright (c) 2007 STMicroelectronics (R&D) Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <asm/mmu_context.h>
#include <asm/cache_insns.h>
#include <asm/cacheflush.h>

/*
 * The maximum number of pages we support up to when doing ranged dcache
 * flushing. Anything exceeding this will simply flush the dcache in its
 * entirety.
 */
#define MAX_ICACHE_PAGES	32

static void __flush_cache_one(unsigned long addr, unsigned long phys,
			       unsigned long exec_offset);

/*
 * Write back the range of D-cache, and purge the I-cache.
 *
 * Called from kernel/module.c:sys_init_module and routine for a.out format,
 * signal handler code and kprobes code
 */
static void sh4_flush_icache_range(void *args)
{
	struct flusher_data *data = args;
	unsigned long start, end;
	unsigned long flags, v;
	int i;

	start = data->addr1;
	end = data->addr2;

	/* If there are too many pages then just blow away the caches */
	if (((end - start) >> PAGE_SHIFT) >= MAX_ICACHE_PAGES) {
		local_flush_cache_all(NULL);
		return;
	}

	/*
	 * Selectively flush d-cache then invalidate the i-cache.
	 * This is inefficient, so only use this for small ranges.
	 */
	start &= ~(L1_CACHE_BYTES-1);
	end += L1_CACHE_BYTES-1;
	end &= ~(L1_CACHE_BYTES-1);

	local_irq_save(flags);
	jump_to_uncached();

	for (v = start; v < end; v += L1_CACHE_BYTES) {
		unsigned long icacheaddr;
		int j, n;

		__ocbwb(v);

		icacheaddr = CACHE_IC_ADDRESS_ARRAY | (v &
				cpu_data->icache.entry_mask);

		/* Clear i-cache line valid-bit */
		n = boot_cpu_data.icache.n_aliases;
		for (i = 0; i < cpu_data->icache.ways; i++) {
			for (j = 0; j < n; j++)
				__raw_writel(0, icacheaddr + (j * PAGE_SIZE));
			icacheaddr += cpu_data->icache.way_incr;
		}
	}

	back_to_cached();
	local_irq_restore(flags);
}

static inline void flush_cache_one(unsigned long start, unsigned long phys)
{
	unsigned long flags, exec_offset = 0;

	/*
	 * All types of SH-4 require PC to be uncached to operate on the I-cache.
	 * Some types of SH-4 require PC to be uncached to operate on the D-cache.
	 */
	if ((boot_cpu_data.flags & CPU_HAS_P2_FLUSH_BUG) ||
	    (start < CACHE_OC_ADDRESS_ARRAY))
		exec_offset = cached_to_uncached;

	local_irq_save(flags);
	__flush_cache_one(start, phys, exec_offset);
	local_irq_restore(flags);
}

/*
 * Write back & invalidate the D-cache of the page.
 * (To avoid "alias" issues)
 */
static void sh4_flush_dcache_folio(void *arg)
{
	struct folio *folio = arg;
#ifndef CONFIG_SMP
	struct address_space *mapping = folio_flush_mapping(folio);

	if (mapping && !mapping_mapped(mapping))
		clear_bit(PG_dcache_clean, &folio->flags.f);
	else
#endif
	{
		unsigned long pfn = folio_pfn(folio);
		unsigned long addr = (unsigned long)folio_address(folio);
		unsigned int i, nr = folio_nr_pages(folio);

		for (i = 0; i < nr; i++) {
			flush_cache_one(CACHE_OC_ADDRESS_ARRAY |
						(addr & shm_align_mask),
					pfn * PAGE_SIZE);
			addr += PAGE_SIZE;
			pfn++;
		}
	}

	wmb();
}

/* TODO: Selective icache invalidation through IC address array.. */
static void flush_icache_all(void)
{
	unsigned long flags, ccr;

	local_irq_save(flags);
	jump_to_uncached();

	/* Flush I-cache */
	ccr = __raw_readl(SH_CCR);
	ccr |= CCR_CACHE_ICI;
	__raw_writel(ccr, SH_CCR);

	/*
	 * back_to_cached() will take care of the barrier for us, don't add
	 * another one!
	 */

	back_to_cached();
	local_irq_restore(flags);
}

static void flush_dcache_all(void)
{
	unsigned long addr, end_addr, entry_offset;

	end_addr = CACHE_OC_ADDRESS_ARRAY +
		(current_cpu_data.dcache.sets <<
		 current_cpu_data.dcache.entry_shift) *
			current_cpu_data.dcache.ways;

	entry_offset = 1 << current_cpu_data.dcache.entry_shift;

	for (addr = CACHE_OC_ADDRESS_ARRAY; addr < end_addr; ) {
		__raw_writel(0, addr); addr += entry_offset;
		__raw_writel(0, addr); addr += entry_offset;
		__raw_writel(0, addr); addr += entry_offset;
		__raw_writel(0, addr); addr += entry_offset;
		__raw_writel(0, addr); addr += entry_offset;
		__raw_writel(0, addr); addr += entry_offset;
		__raw_writel(0, addr); addr += entry_offset;
		__raw_writel(0, addr); addr += entry_offset;
	}
}

static void sh4_flush_cache_all(void *unused)
{
	flush_dcache_all();
	flush_icache_all();
}

/*
 * Note : (RPC) since the caches are physically tagged, the only point
 * of flush_cache_mm for SH-4 is to get rid of aliases from the
 * D-cache.  The assumption elsewhere, e.g. flush_cache_range, is that
 * lines can stay resident so long as the virtual address they were
 * accessed with (hence cache set) is in accord with the physical
 * address (i.e. tag).  It's no different here.
 *
 * Caller takes mm->mmap_lock.
 */
static void sh4_flush_cache_mm(void *arg)
{
	struct mm_struct *mm = arg;

	if (cpu_context(smp_processor_id(), mm) == NO_CONTEXT)
		return;

	flush_dcache_all();
}

/*
 * Write back and invalidate I/D-caches for the page.
 *
 * ADDR: Virtual Address (U0 address)
 * PFN: Physical page number
 */
static void sh4_flush_cache_page(void *args)
{
	struct flusher_data *data = args;
	struct vm_area_struct *vma;
	struct page *page;
	unsigned long address, pfn, phys;
	int map_coherent = 0;
	pmd_t *pmd;
	pte_t *pte;
	void *vaddr;

	vma = data->vma;
	address = data->addr1 & PAGE_MASK;
	pfn = data->addr2;
	phys = pfn << PAGE_SHIFT;
	page = pfn_to_page(pfn);

	if (cpu_context(smp_processor_id(), vma->vm_mm) == NO_CONTEXT)
		return;

	pmd = pmd_off(vma->vm_mm, address);
	pte = pte_offset_kernel(pmd, address);

	/* If the page isn't present, there is nothing to do here. */
	if (!(pte_val(*pte) & _PAGE_PRESENT))
		return;

	if ((vma->vm_mm == current->active_mm))
		vaddr = NULL;
	else {
		struct folio *folio = page_folio(page);
		/*
		 * Use kmap_coherent or kmap_atomic to do flushes for
		 * another ASID than the current one.
		 */
		map_coherent = (current_cpu_data.dcache.n_aliases &&
			test_bit(PG_dcache_clean, folio_flags(folio, 0)) &&
			page_mapped(page));
		if (map_coherent)
			vaddr = kmap_coherent(page, address);
		else
			vaddr = kmap_atomic(page);

		address = (unsigned long)vaddr;
	}

	flush_cache_one(CACHE_OC_ADDRESS_ARRAY |
			(address & shm_align_mask), phys);

	if (vma->vm_flags & VM_EXEC)
		flush_icache_all();

	if (vaddr) {
		if (map_coherent)
			kunmap_coherent(vaddr);
		else
			kunmap_atomic(vaddr);
	}
}

/*
 * Write back and invalidate D-caches.
 *
 * START, END: Virtual Address (U0 address)
 *
 * NOTE: We need to flush the _physical_ page entry.
 * Flushing the cache lines for U0 only isn't enough.
 * We need to flush for P1 too, which may contain aliases.
 */
static void sh4_flush_cache_range(void *args)
{
	struct flusher_data *data = args;
	struct vm_area_struct *vma;
	unsigned long start, end;

	vma = data->vma;
	start = data->addr1;
	end = data->addr2;

	if (cpu_context(smp_processor_id(), vma->vm_mm) == NO_CONTEXT)
		return;

	/*
	 * If cache is only 4k-per-way, there are never any 'aliases'.  Since
	 * the cache is physically tagged, the data can just be left in there.
	 */
	if (boot_cpu_data.dcache.n_aliases == 0)
		return;

	flush_dcache_all();

	if (vma->vm_flags & VM_EXEC)
		flush_icache_all();
}

/**
 * __flush_cache_one
 *
 * @addr:  address in memory mapped cache array
 * @phys:  P1 address to flush (has to match tags if addr has 'A' bit
 *         set i.e. associative write)
 * @exec_offset: set to 0x20000000 if flush has to be executed from P2
 *               region else 0x0
 *
 * The offset into the cache array implied by 'addr' selects the
 * 'colour' of the virtual address range that will be flushed.  The
 * operation (purge/write-back) is selected by the lower 2 bits of
 * 'phys'.
 */
static void __flush_cache_one(unsigned long addr, unsigned long phys,
			       unsigned long exec_offset)
{
	int way_count;
	unsigned long base_addr = addr;
	struct cache_info *dcache;
	unsigned long way_incr;
	unsigned long a, ea, p;
	unsigned long temp_pc;

	dcache = &boot_cpu_data.dcache;
	/* Write this way for better assembly. */
	way_count = dcache->ways;
	way_incr = dcache->way_incr;

	/*
	 * Apply exec_offset (i.e. branch to P2 if required.).
	 *
	 * FIXME:
	 *
	 *	If I write "=r" for the (temp_pc), it puts this in r6 hence
	 *	trashing exec_offset before it's been added on - why?  Hence
	 *	"=&r" as a 'workaround'
	 */
	asm volatile("mov.l 1f, %0\n\t"
		     "add   %1, %0\n\t"
		     "jmp   @%0\n\t"
		     "nop\n\t"
		     ".balign 4\n\t"
		     "1:  .long 2f\n\t"
		     "2:\n" : "=&r" (temp_pc) : "r" (exec_offset));

	/*
	 * We know there will be >=1 iteration, so write as do-while to avoid
	 * pointless nead-of-loop check for 0 iterations.
	 */
	do {
		ea = base_addr + PAGE_SIZE;
		a = base_addr;
		p = phys;

		do {
			*(volatile unsigned long *)a = p;
			/*
			 * Next line: intentionally not p+32, saves an add, p
			 * will do since only the cache tag bits need to
			 * match.
			 */
			*(volatile unsigned long *)(a+32) = p;
			a += 64;
			p += 64;
		} while (a < ea);

		base_addr += way_incr;
	} while (--way_count != 0);
}

/*
 * SH-4 has virtually indexed and physically tagged cache.
 */
void __init sh4_cache_init(void)
{
	printk("PVR=%08x CVR=%08x PRR=%08x\n",
		__raw_readl(CCN_PVR),
		__raw_readl(CCN_CVR),
		__raw_readl(CCN_PRR));

	local_flush_icache_range	= sh4_flush_icache_range;
	local_flush_dcache_folio	= sh4_flush_dcache_folio;
	local_flush_cache_all		= sh4_flush_cache_all;
	local_flush_cache_mm		= sh4_flush_cache_mm;
	local_flush_cache_dup_mm	= sh4_flush_cache_mm;
	local_flush_cache_page		= sh4_flush_cache_page;
	local_flush_cache_range		= sh4_flush_cache_range;

	sh4__flush_region_init();
}
