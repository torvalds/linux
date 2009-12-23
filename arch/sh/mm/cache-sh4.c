/*
 * arch/sh/mm/cache-sh4.c
 *
 * Copyright (C) 1999, 2000, 2002  Niibe Yutaka
 * Copyright (C) 2001 - 2007  Paul Mundt
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
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>

/*
 * The maximum number of pages we support up to when doing ranged dcache
 * flushing. Anything exceeding this will simply flush the dcache in its
 * entirety.
 */
#define MAX_DCACHE_PAGES	64	/* XXX: Tune for ways */
#define MAX_ICACHE_PAGES	32

static void __flush_cache_one(unsigned long addr, unsigned long phys,
			       unsigned long exec_offset);

/*
 * This is initialised here to ensure that it is not placed in the BSS.  If
 * that were to happen, note that cache_init gets called before the BSS is
 * cleared, so this would get nulled out which would be hopeless.
 */
static void (*__flush_dcache_segment_fn)(unsigned long, unsigned long) =
	(void (*)(unsigned long, unsigned long))0xdeadbeef;

/*
 * Write back the range of D-cache, and purge the I-cache.
 *
 * Called from kernel/module.c:sys_init_module and routine for a.out format,
 * signal handler code and kprobes code
 */
static void __uses_jump_to_uncached sh4_flush_icache_range(void *args)
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
	 * All types of SH-4 require PC to be in P2 to operate on the I-cache.
	 * Some types of SH-4 require PC to be in P2 to operate on the D-cache.
	 */
	if ((boot_cpu_data.flags & CPU_HAS_P2_FLUSH_BUG) ||
	    (start < CACHE_OC_ADDRESS_ARRAY))
		exec_offset = 0x20000000;

	local_irq_save(flags);
	__flush_cache_one(start | SH_CACHE_ASSOC, P1SEGADDR(phys), exec_offset);
	local_irq_restore(flags);
}

/*
 * Write back & invalidate the D-cache of the page.
 * (To avoid "alias" issues)
 */
static void sh4_flush_dcache_page(void *arg)
{
	struct page *page = arg;
#ifndef CONFIG_SMP
	struct address_space *mapping = page_mapping(page);

	if (mapping && !mapping_mapped(mapping))
		set_bit(PG_dcache_dirty, &page->flags);
	else
#endif
	{
		unsigned long phys = PHYSADDR(page_address(page));
		unsigned long addr = CACHE_OC_ADDRESS_ARRAY;
		int i, n;

		/* Loop all the D-cache */
		n = boot_cpu_data.dcache.n_aliases;
		for (i = 0; i < n; i++, addr += PAGE_SIZE)
			flush_cache_one(addr, phys);
	}

	wmb();
}

/* TODO: Selective icache invalidation through IC address array.. */
static void __uses_jump_to_uncached flush_icache_all(void)
{
	unsigned long flags, ccr;

	local_irq_save(flags);
	jump_to_uncached();

	/* Flush I-cache */
	ccr = ctrl_inl(CCR);
	ccr |= CCR_CACHE_ICI;
	ctrl_outl(ccr, CCR);

	/*
	 * back_to_cached() will take care of the barrier for us, don't add
	 * another one!
	 */

	back_to_cached();
	local_irq_restore(flags);
}

static inline void flush_dcache_all(void)
{
	(*__flush_dcache_segment_fn)(0UL, boot_cpu_data.dcache.way_size);
	wmb();
}

static void sh4_flush_cache_all(void *unused)
{
	flush_dcache_all();
	flush_icache_all();
}

static void __flush_cache_mm(struct mm_struct *mm, unsigned long start,
			     unsigned long end)
{
	unsigned long d = 0, p = start & PAGE_MASK;
	unsigned long alias_mask = boot_cpu_data.dcache.alias_mask;
	unsigned long n_aliases = boot_cpu_data.dcache.n_aliases;
	unsigned long select_bit;
	unsigned long all_aliases_mask;
	unsigned long addr_offset;
	pgd_t *dir;
	pmd_t *pmd;
	pud_t *pud;
	pte_t *pte;
	int i;

	dir = pgd_offset(mm, p);
	pud = pud_offset(dir, p);
	pmd = pmd_offset(pud, p);
	end = PAGE_ALIGN(end);

	all_aliases_mask = (1 << n_aliases) - 1;

	do {
		if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd))) {
			p &= PMD_MASK;
			p += PMD_SIZE;
			pmd++;

			continue;
		}

		pte = pte_offset_kernel(pmd, p);

		do {
			unsigned long phys;
			pte_t entry = *pte;

			if (!(pte_val(entry) & _PAGE_PRESENT)) {
				pte++;
				p += PAGE_SIZE;
				continue;
			}

			phys = pte_val(entry) & PTE_PHYS_MASK;

			if ((p ^ phys) & alias_mask) {
				d |= 1 << ((p & alias_mask) >> PAGE_SHIFT);
				d |= 1 << ((phys & alias_mask) >> PAGE_SHIFT);

				if (d == all_aliases_mask)
					goto loop_exit;
			}

			pte++;
			p += PAGE_SIZE;
		} while (p < end && ((unsigned long)pte & ~PAGE_MASK));
		pmd++;
	} while (p < end);

loop_exit:
	addr_offset = 0;
	select_bit = 1;

	for (i = 0; i < n_aliases; i++) {
		if (d & select_bit) {
			(*__flush_dcache_segment_fn)(addr_offset, PAGE_SIZE);
			wmb();
		}

		select_bit <<= 1;
		addr_offset += PAGE_SIZE;
	}
}

/*
 * Note : (RPC) since the caches are physically tagged, the only point
 * of flush_cache_mm for SH-4 is to get rid of aliases from the
 * D-cache.  The assumption elsewhere, e.g. flush_cache_range, is that
 * lines can stay resident so long as the virtual address they were
 * accessed with (hence cache set) is in accord with the physical
 * address (i.e. tag).  It's no different here.  So I reckon we don't
 * need to flush the I-cache, since aliases don't matter for that.  We
 * should try that.
 *
 * Caller takes mm->mmap_sem.
 */
static void sh4_flush_cache_mm(void *arg)
{
	struct mm_struct *mm = arg;

	if (cpu_context(smp_processor_id(), mm) == NO_CONTEXT)
		return;

	/*
	 * If cache is only 4k-per-way, there are never any 'aliases'.  Since
	 * the cache is physically tagged, the data can just be left in there.
	 */
	if (boot_cpu_data.dcache.n_aliases == 0)
		return;

	/*
	 * Don't bother groveling around the dcache for the VMA ranges
	 * if there are too many PTEs to make it worthwhile.
	 */
	if (mm->nr_ptes >= MAX_DCACHE_PAGES)
		flush_dcache_all();
	else {
		struct vm_area_struct *vma;

		/*
		 * In this case there are reasonably sized ranges to flush,
		 * iterate through the VMA list and take care of any aliases.
		 */
		for (vma = mm->mmap; vma; vma = vma->vm_next)
			__flush_cache_mm(mm, vma->vm_start, vma->vm_end);
	}

	/* Only touch the icache if one of the VMAs has VM_EXEC set. */
	if (mm->exec_vm)
		flush_icache_all();
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
	unsigned long address, pfn, phys;
	unsigned int alias_mask;

	vma = data->vma;
	address = data->addr1;
	pfn = data->addr2;
	phys = pfn << PAGE_SHIFT;

	if (cpu_context(smp_processor_id(), vma->vm_mm) == NO_CONTEXT)
		return;

	alias_mask = boot_cpu_data.dcache.alias_mask;

	/* We only need to flush D-cache when we have alias */
	if ((address^phys) & alias_mask) {
		/* Loop 4K of the D-cache */
		flush_cache_one(
			CACHE_OC_ADDRESS_ARRAY | (address & alias_mask),
			phys);
		/* Loop another 4K of the D-cache */
		flush_cache_one(
			CACHE_OC_ADDRESS_ARRAY | (phys & alias_mask),
			phys);
	}

	alias_mask = boot_cpu_data.icache.alias_mask;
	if (vma->vm_flags & VM_EXEC) {
		/*
		 * Evict entries from the portion of the cache from which code
		 * may have been executed at this address (virtual).  There's
		 * no need to evict from the portion corresponding to the
		 * physical address as for the D-cache, because we know the
		 * kernel has never executed the code through its identity
		 * translation.
		 */
		flush_cache_one(
			CACHE_IC_ADDRESS_ARRAY | (address & alias_mask),
			phys);
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

	/*
	 * Don't bother with the lookup and alias check if we have a
	 * wide range to cover, just blow away the dcache in its
	 * entirety instead. -- PFM.
	 */
	if (((end - start) >> PAGE_SHIFT) >= MAX_DCACHE_PAGES)
		flush_dcache_all();
	else
		__flush_cache_mm(vma->vm_mm, start, end);

	if (vma->vm_flags & VM_EXEC) {
		/*
		 * TODO: Is this required???  Need to look at how I-cache
		 * coherency is assured when new programs are loaded to see if
		 * this matters.
		 */
		flush_icache_all();
	}
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
 * Break the 1, 2 and 4 way variants of this out into separate functions to
 * avoid nearly all the overhead of having the conditional stuff in the function
 * bodies (+ the 1 and 2 way cases avoid saving any registers too).
 *
 * We want to eliminate unnecessary bus transactions, so this code uses
 * a non-obvious technique.
 *
 * Loop over a cache way sized block of, one cache line at a time. For each
 * line, use movca.a to cause the current cache line contents to be written
 * back, but without reading anything from main memory. However this has the
 * side effect that the cache is now caching that memory location. So follow
 * this with a cache invalidate to mark the cache line invalid. And do all
 * this with interrupts disabled, to avoid the cache line being accidently
 * evicted while it is holding garbage.
 *
 * This also breaks in a number of circumstances:
 * - if there are modifications to the region of memory just above
 *   empty_zero_page (for example because a breakpoint has been placed
 *   there), then these can be lost.
 *
 *   This is because the the memory address which the cache temporarily
 *   caches in the above description is empty_zero_page. So the
 *   movca.l hits the cache (it is assumed that it misses, or at least
 *   isn't dirty), modifies the line and then invalidates it, losing the
 *   required change.
 *
 * - If caches are disabled or configured in write-through mode, then
 *   the movca.l writes garbage directly into memory.
 */
static void __flush_dcache_segment_writethrough(unsigned long start,
					        unsigned long extent_per_way)
{
	unsigned long addr;
	int i;

	addr = CACHE_OC_ADDRESS_ARRAY | (start & cpu_data->dcache.entry_mask);

	while (extent_per_way) {
		for (i = 0; i < cpu_data->dcache.ways; i++)
			__raw_writel(0, addr + cpu_data->dcache.way_incr * i);

		addr += cpu_data->dcache.linesz;
		extent_per_way -= cpu_data->dcache.linesz;
	}
}

static void __flush_dcache_segment_1way(unsigned long start,
					unsigned long extent_per_way)
{
	unsigned long orig_sr, sr_with_bl;
	unsigned long base_addr;
	unsigned long way_incr, linesz, way_size;
	struct cache_info *dcache;
	register unsigned long a0, a0e;

	asm volatile("stc sr, %0" : "=r" (orig_sr));
	sr_with_bl = orig_sr | (1<<28);
	base_addr = ((unsigned long)&empty_zero_page[0]);

	/*
	 * The previous code aligned base_addr to 16k, i.e. the way_size of all
	 * existing SH-4 D-caches.  Whilst I don't see a need to have this
	 * aligned to any better than the cache line size (which it will be
	 * anyway by construction), let's align it to at least the way_size of
	 * any existing or conceivable SH-4 D-cache.  -- RPC
	 */
	base_addr = ((base_addr >> 16) << 16);
	base_addr |= start;

	dcache = &boot_cpu_data.dcache;
	linesz = dcache->linesz;
	way_incr = dcache->way_incr;
	way_size = dcache->way_size;

	a0 = base_addr;
	a0e = base_addr + extent_per_way;
	do {
		asm volatile("ldc %0, sr" : : "r" (sr_with_bl));
		asm volatile("movca.l r0, @%0\n\t"
			     "ocbi @%0" : : "r" (a0));
		a0 += linesz;
		asm volatile("movca.l r0, @%0\n\t"
			     "ocbi @%0" : : "r" (a0));
		a0 += linesz;
		asm volatile("movca.l r0, @%0\n\t"
			     "ocbi @%0" : : "r" (a0));
		a0 += linesz;
		asm volatile("movca.l r0, @%0\n\t"
			     "ocbi @%0" : : "r" (a0));
		asm volatile("ldc %0, sr" : : "r" (orig_sr));
		a0 += linesz;
	} while (a0 < a0e);
}

static void __flush_dcache_segment_2way(unsigned long start,
					unsigned long extent_per_way)
{
	unsigned long orig_sr, sr_with_bl;
	unsigned long base_addr;
	unsigned long way_incr, linesz, way_size;
	struct cache_info *dcache;
	register unsigned long a0, a1, a0e;

	asm volatile("stc sr, %0" : "=r" (orig_sr));
	sr_with_bl = orig_sr | (1<<28);
	base_addr = ((unsigned long)&empty_zero_page[0]);

	/* See comment under 1-way above */
	base_addr = ((base_addr >> 16) << 16);
	base_addr |= start;

	dcache = &boot_cpu_data.dcache;
	linesz = dcache->linesz;
	way_incr = dcache->way_incr;
	way_size = dcache->way_size;

	a0 = base_addr;
	a1 = a0 + way_incr;
	a0e = base_addr + extent_per_way;
	do {
		asm volatile("ldc %0, sr" : : "r" (sr_with_bl));
		asm volatile("movca.l r0, @%0\n\t"
			     "movca.l r0, @%1\n\t"
			     "ocbi @%0\n\t"
			     "ocbi @%1" : :
			     "r" (a0), "r" (a1));
		a0 += linesz;
		a1 += linesz;
		asm volatile("movca.l r0, @%0\n\t"
			     "movca.l r0, @%1\n\t"
			     "ocbi @%0\n\t"
			     "ocbi @%1" : :
			     "r" (a0), "r" (a1));
		a0 += linesz;
		a1 += linesz;
		asm volatile("movca.l r0, @%0\n\t"
			     "movca.l r0, @%1\n\t"
			     "ocbi @%0\n\t"
			     "ocbi @%1" : :
			     "r" (a0), "r" (a1));
		a0 += linesz;
		a1 += linesz;
		asm volatile("movca.l r0, @%0\n\t"
			     "movca.l r0, @%1\n\t"
			     "ocbi @%0\n\t"
			     "ocbi @%1" : :
			     "r" (a0), "r" (a1));
		asm volatile("ldc %0, sr" : : "r" (orig_sr));
		a0 += linesz;
		a1 += linesz;
	} while (a0 < a0e);
}

static void __flush_dcache_segment_4way(unsigned long start,
					unsigned long extent_per_way)
{
	unsigned long orig_sr, sr_with_bl;
	unsigned long base_addr;
	unsigned long way_incr, linesz, way_size;
	struct cache_info *dcache;
	register unsigned long a0, a1, a2, a3, a0e;

	asm volatile("stc sr, %0" : "=r" (orig_sr));
	sr_with_bl = orig_sr | (1<<28);
	base_addr = ((unsigned long)&empty_zero_page[0]);

	/* See comment under 1-way above */
	base_addr = ((base_addr >> 16) << 16);
	base_addr |= start;

	dcache = &boot_cpu_data.dcache;
	linesz = dcache->linesz;
	way_incr = dcache->way_incr;
	way_size = dcache->way_size;

	a0 = base_addr;
	a1 = a0 + way_incr;
	a2 = a1 + way_incr;
	a3 = a2 + way_incr;
	a0e = base_addr + extent_per_way;
	do {
		asm volatile("ldc %0, sr" : : "r" (sr_with_bl));
		asm volatile("movca.l r0, @%0\n\t"
			     "movca.l r0, @%1\n\t"
			     "movca.l r0, @%2\n\t"
			     "movca.l r0, @%3\n\t"
			     "ocbi @%0\n\t"
			     "ocbi @%1\n\t"
			     "ocbi @%2\n\t"
			     "ocbi @%3\n\t" : :
			     "r" (a0), "r" (a1), "r" (a2), "r" (a3));
		a0 += linesz;
		a1 += linesz;
		a2 += linesz;
		a3 += linesz;
		asm volatile("movca.l r0, @%0\n\t"
			     "movca.l r0, @%1\n\t"
			     "movca.l r0, @%2\n\t"
			     "movca.l r0, @%3\n\t"
			     "ocbi @%0\n\t"
			     "ocbi @%1\n\t"
			     "ocbi @%2\n\t"
			     "ocbi @%3\n\t" : :
			     "r" (a0), "r" (a1), "r" (a2), "r" (a3));
		a0 += linesz;
		a1 += linesz;
		a2 += linesz;
		a3 += linesz;
		asm volatile("movca.l r0, @%0\n\t"
			     "movca.l r0, @%1\n\t"
			     "movca.l r0, @%2\n\t"
			     "movca.l r0, @%3\n\t"
			     "ocbi @%0\n\t"
			     "ocbi @%1\n\t"
			     "ocbi @%2\n\t"
			     "ocbi @%3\n\t" : :
			     "r" (a0), "r" (a1), "r" (a2), "r" (a3));
		a0 += linesz;
		a1 += linesz;
		a2 += linesz;
		a3 += linesz;
		asm volatile("movca.l r0, @%0\n\t"
			     "movca.l r0, @%1\n\t"
			     "movca.l r0, @%2\n\t"
			     "movca.l r0, @%3\n\t"
			     "ocbi @%0\n\t"
			     "ocbi @%1\n\t"
			     "ocbi @%2\n\t"
			     "ocbi @%3\n\t" : :
			     "r" (a0), "r" (a1), "r" (a2), "r" (a3));
		asm volatile("ldc %0, sr" : : "r" (orig_sr));
		a0 += linesz;
		a1 += linesz;
		a2 += linesz;
		a3 += linesz;
	} while (a0 < a0e);
}

extern void __weak sh4__flush_region_init(void);

/*
 * SH-4 has virtually indexed and physically tagged cache.
 */
void __init sh4_cache_init(void)
{
	unsigned int wt_enabled = !!(__raw_readl(CCR) & CCR_CACHE_WT);

	printk("PVR=%08x CVR=%08x PRR=%08x\n",
		ctrl_inl(CCN_PVR),
		ctrl_inl(CCN_CVR),
		ctrl_inl(CCN_PRR));

	if (wt_enabled)
		__flush_dcache_segment_fn = __flush_dcache_segment_writethrough;
	else {
		switch (boot_cpu_data.dcache.ways) {
		case 1:
			__flush_dcache_segment_fn = __flush_dcache_segment_1way;
			break;
		case 2:
			__flush_dcache_segment_fn = __flush_dcache_segment_2way;
			break;
		case 4:
			__flush_dcache_segment_fn = __flush_dcache_segment_4way;
			break;
		default:
			panic("unknown number of cache ways\n");
			break;
		}
	}

	local_flush_icache_range	= sh4_flush_icache_range;
	local_flush_dcache_page		= sh4_flush_dcache_page;
	local_flush_cache_all		= sh4_flush_cache_all;
	local_flush_cache_mm		= sh4_flush_cache_mm;
	local_flush_cache_dup_mm	= sh4_flush_cache_mm;
	local_flush_cache_page		= sh4_flush_cache_page;
	local_flush_cache_range		= sh4_flush_cache_range;

	sh4__flush_region_init();
}
