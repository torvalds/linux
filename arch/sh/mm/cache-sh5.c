/*
 * arch/sh/mm/cache-sh5.c
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2002  Benedict Gaster
 * Copyright (C) 2003  Richard Curnow
 * Copyright (C) 2003 - 2008  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <asm/tlb.h>
#include <asm/processor.h>
#include <asm/cache.h>
#include <asm/pgalloc.h>
#include <asm/uaccess.h>
#include <asm/mmu_context.h>

/* Wired TLB entry for the D-cache */
static unsigned long long dtlb_cache_slot;

void __init p3_cache_init(void)
{
	/* Reserve a slot for dcache colouring in the DTLB */
	dtlb_cache_slot	= sh64_get_wired_dtlb_entry();
}

#ifdef CONFIG_DCACHE_DISABLED
#define sh64_dcache_purge_all()					do { } while (0)
#define sh64_dcache_purge_coloured_phy_page(paddr, eaddr)	do { } while (0)
#define sh64_dcache_purge_user_range(mm, start, end)		do { } while (0)
#define sh64_dcache_purge_phy_page(paddr)			do { } while (0)
#define sh64_dcache_purge_virt_page(mm, eaddr)			do { } while (0)
#endif

/*
 * The following group of functions deal with mapping and unmapping a
 * temporary page into a DTLB slot that has been set aside for exclusive
 * use.
 */
static inline void
sh64_setup_dtlb_cache_slot(unsigned long eaddr, unsigned long asid,
			   unsigned long paddr)
{
	local_irq_disable();
	sh64_setup_tlb_slot(dtlb_cache_slot, eaddr, asid, paddr);
}

static inline void sh64_teardown_dtlb_cache_slot(void)
{
	sh64_teardown_tlb_slot(dtlb_cache_slot);
	local_irq_enable();
}

#ifndef CONFIG_ICACHE_DISABLED
static inline void sh64_icache_inv_all(void)
{
	unsigned long long addr, flag, data;
	unsigned long flags;

	addr = ICCR0;
	flag = ICCR0_ICI;
	data = 0;

	/* Make this a critical section for safety (probably not strictly necessary.) */
	local_irq_save(flags);

	/* Without %1 it gets unexplicably wrong */
	__asm__ __volatile__ (
		"getcfg	%3, 0, %0\n\t"
		"or	%0, %2, %0\n\t"
		"putcfg	%3, 0, %0\n\t"
		"synci"
		: "=&r" (data)
		: "0" (data), "r" (flag), "r" (addr));

	local_irq_restore(flags);
}

static void sh64_icache_inv_kernel_range(unsigned long start, unsigned long end)
{
	/* Invalidate range of addresses [start,end] from the I-cache, where
	 * the addresses lie in the kernel superpage. */

	unsigned long long ullend, addr, aligned_start;
	aligned_start = (unsigned long long)(signed long long)(signed long) start;
	addr = L1_CACHE_ALIGN(aligned_start);
	ullend = (unsigned long long) (signed long long) (signed long) end;

	while (addr <= ullend) {
		__asm__ __volatile__ ("icbi %0, 0" : : "r" (addr));
		addr += L1_CACHE_BYTES;
	}
}

static void sh64_icache_inv_user_page(struct vm_area_struct *vma, unsigned long eaddr)
{
	/* If we get called, we know that vma->vm_flags contains VM_EXEC.
	   Also, eaddr is page-aligned. */
	unsigned int cpu = smp_processor_id();
	unsigned long long addr, end_addr;
	unsigned long flags = 0;
	unsigned long running_asid, vma_asid;
	addr = eaddr;
	end_addr = addr + PAGE_SIZE;

	/* Check whether we can use the current ASID for the I-cache
	   invalidation.  For example, if we're called via
	   access_process_vm->flush_cache_page->here, (e.g. when reading from
	   /proc), 'running_asid' will be that of the reader, not of the
	   victim.

	   Also, note the risk that we might get pre-empted between the ASID
	   compare and blocking IRQs, and before we regain control, the
	   pid->ASID mapping changes.  However, the whole cache will get
	   invalidated when the mapping is renewed, so the worst that can
	   happen is that the loop below ends up invalidating somebody else's
	   cache entries.
	*/

	running_asid = get_asid();
	vma_asid = cpu_asid(cpu, vma->vm_mm);
	if (running_asid != vma_asid) {
		local_irq_save(flags);
		switch_and_save_asid(vma_asid);
	}
	while (addr < end_addr) {
		/* Worth unrolling a little */
		__asm__ __volatile__("icbi %0,  0" : : "r" (addr));
		__asm__ __volatile__("icbi %0, 32" : : "r" (addr));
		__asm__ __volatile__("icbi %0, 64" : : "r" (addr));
		__asm__ __volatile__("icbi %0, 96" : : "r" (addr));
		addr += 128;
	}
	if (running_asid != vma_asid) {
		switch_and_save_asid(running_asid);
		local_irq_restore(flags);
	}
}

static void sh64_icache_inv_user_page_range(struct mm_struct *mm,
			  unsigned long start, unsigned long end)
{
	/* Used for invalidating big chunks of I-cache, i.e. assume the range
	   is whole pages.  If 'start' or 'end' is not page aligned, the code
	   is conservative and invalidates to the ends of the enclosing pages.
	   This is functionally OK, just a performance loss. */

	/* See the comments below in sh64_dcache_purge_user_range() regarding
	   the choice of algorithm.  However, for the I-cache option (2) isn't
	   available because there are no physical tags so aliases can't be
	   resolved.  The icbi instruction has to be used through the user
	   mapping.   Because icbi is cheaper than ocbp on a cache hit, it
	   would be cheaper to use the selective code for a large range than is
	   possible with the D-cache.  Just assume 64 for now as a working
	   figure.
	   */
	int n_pages;

	if (!mm)
		return;

	n_pages = ((end - start) >> PAGE_SHIFT);
	if (n_pages >= 64) {
		sh64_icache_inv_all();
	} else {
		unsigned long aligned_start;
		unsigned long eaddr;
		unsigned long after_last_page_start;
		unsigned long mm_asid, current_asid;
		unsigned long flags = 0;

		mm_asid = cpu_asid(smp_processor_id(), mm);
		current_asid = get_asid();

		if (mm_asid != current_asid) {
			/* Switch ASID and run the invalidate loop under cli */
			local_irq_save(flags);
			switch_and_save_asid(mm_asid);
		}

		aligned_start = start & PAGE_MASK;
		after_last_page_start = PAGE_SIZE + ((end - 1) & PAGE_MASK);

		while (aligned_start < after_last_page_start) {
			struct vm_area_struct *vma;
			unsigned long vma_end;
			vma = find_vma(mm, aligned_start);
			if (!vma || (aligned_start <= vma->vm_end)) {
				/* Avoid getting stuck in an error condition */
				aligned_start += PAGE_SIZE;
				continue;
			}
			vma_end = vma->vm_end;
			if (vma->vm_flags & VM_EXEC) {
				/* Executable */
				eaddr = aligned_start;
				while (eaddr < vma_end) {
					sh64_icache_inv_user_page(vma, eaddr);
					eaddr += PAGE_SIZE;
				}
			}
			aligned_start = vma->vm_end; /* Skip to start of next region */
		}

		if (mm_asid != current_asid) {
			switch_and_save_asid(current_asid);
			local_irq_restore(flags);
		}
	}
}

/*
 * Invalidate a small range of user context I-cache, not necessarily page
 * (or even cache-line) aligned.
 *
 * Since this is used inside ptrace, the ASID in the mm context typically
 * won't match current_asid.  We'll have to switch ASID to do this.  For
 * safety, and given that the range will be small, do all this under cli.
 *
 * Note, there is a hazard that the ASID in mm->context is no longer
 * actually associated with mm, i.e. if the mm->context has started a new
 * cycle since mm was last active.  However, this is just a performance
 * issue: all that happens is that we invalidate lines belonging to
 * another mm, so the owning process has to refill them when that mm goes
 * live again.  mm itself can't have any cache entries because there will
 * have been a flush_cache_all when the new mm->context cycle started.
 */
static void sh64_icache_inv_user_small_range(struct mm_struct *mm,
						unsigned long start, int len)
{
	unsigned long long eaddr = start;
	unsigned long long eaddr_end = start + len;
	unsigned long current_asid, mm_asid;
	unsigned long flags;
	unsigned long long epage_start;

	/*
	 * Align to start of cache line.  Otherwise, suppose len==8 and
	 * start was at 32N+28 : the last 4 bytes wouldn't get invalidated.
	 */
	eaddr = L1_CACHE_ALIGN(start);
	eaddr_end = start + len;

	mm_asid = cpu_asid(smp_processor_id(), mm);
	local_irq_save(flags);
	current_asid = switch_and_save_asid(mm_asid);

	epage_start = eaddr & PAGE_MASK;

	while (eaddr < eaddr_end) {
		__asm__ __volatile__("icbi %0, 0" : : "r" (eaddr));
		eaddr += L1_CACHE_BYTES;
	}
	switch_and_save_asid(current_asid);
	local_irq_restore(flags);
}

static void sh64_icache_inv_current_user_range(unsigned long start, unsigned long end)
{
	/* The icbi instruction never raises ITLBMISS.  i.e. if there's not a
	   cache hit on the virtual tag the instruction ends there, without a
	   TLB lookup. */

	unsigned long long aligned_start;
	unsigned long long ull_end;
	unsigned long long addr;

	ull_end = end;

	/* Just invalidate over the range using the natural addresses.  TLB
	   miss handling will be OK (TBC).  Since it's for the current process,
	   either we're already in the right ASID context, or the ASIDs have
	   been recycled since we were last active in which case we might just
	   invalidate another processes I-cache entries : no worries, just a
	   performance drop for him. */
	aligned_start = L1_CACHE_ALIGN(start);
	addr = aligned_start;
	while (addr < ull_end) {
		__asm__ __volatile__ ("icbi %0, 0" : : "r" (addr));
		__asm__ __volatile__ ("nop");
		__asm__ __volatile__ ("nop");
		addr += L1_CACHE_BYTES;
	}
}
#endif /* !CONFIG_ICACHE_DISABLED */

#ifndef CONFIG_DCACHE_DISABLED
/* Buffer used as the target of alloco instructions to purge data from cache
   sets by natural eviction. -- RPC */
#define DUMMY_ALLOCO_AREA_SIZE ((L1_CACHE_BYTES << 10) + (1024 * 4))
static unsigned char dummy_alloco_area[DUMMY_ALLOCO_AREA_SIZE] __cacheline_aligned = { 0, };

static void inline sh64_dcache_purge_sets(int sets_to_purge_base, int n_sets)
{
	/* Purge all ways in a particular block of sets, specified by the base
	   set number and number of sets.  Can handle wrap-around, if that's
	   needed.  */

	int dummy_buffer_base_set;
	unsigned long long eaddr, eaddr0, eaddr1;
	int j;
	int set_offset;

	dummy_buffer_base_set = ((int)&dummy_alloco_area &
				 cpu_data->dcache.entry_mask) >>
				 cpu_data->dcache.entry_shift;
	set_offset = sets_to_purge_base - dummy_buffer_base_set;

	for (j = 0; j < n_sets; j++, set_offset++) {
		set_offset &= (cpu_data->dcache.sets - 1);
		eaddr0 = (unsigned long long)dummy_alloco_area +
			(set_offset << cpu_data->dcache.entry_shift);

		/*
		 * Do one alloco which hits the required set per cache
		 * way.  For write-back mode, this will purge the #ways
		 * resident lines.  There's little point unrolling this
		 * loop because the allocos stall more if they're too
		 * close together.
		 */
		eaddr1 = eaddr0 + cpu_data->dcache.way_size *
				  cpu_data->dcache.ways;

		for (eaddr = eaddr0; eaddr < eaddr1;
		     eaddr += cpu_data->dcache.way_size) {
			__asm__ __volatile__ ("alloco %0, 0" : : "r" (eaddr));
			__asm__ __volatile__ ("synco"); /* TAKum03020 */
		}

		eaddr1 = eaddr0 + cpu_data->dcache.way_size *
				  cpu_data->dcache.ways;

		for (eaddr = eaddr0; eaddr < eaddr1;
		     eaddr += cpu_data->dcache.way_size) {
			/*
			 * Load from each address.  Required because
			 * alloco is a NOP if the cache is write-through.
			 */
			if (test_bit(SH_CACHE_MODE_WT, &(cpu_data->dcache.flags)))
				__raw_readb((unsigned long)eaddr);
		}
	}

	/*
	 * Don't use OCBI to invalidate the lines.  That costs cycles
	 * directly.  If the dummy block is just left resident, it will
	 * naturally get evicted as required.
	 */
}

/*
 * Purge the entire contents of the dcache.  The most efficient way to
 * achieve this is to use alloco instructions on a region of unused
 * memory equal in size to the cache, thereby causing the current
 * contents to be discarded by natural eviction.  The alternative, namely
 * reading every tag, setting up a mapping for the corresponding page and
 * doing an OCBP for the line, would be much more expensive.
 */
static void sh64_dcache_purge_all(void)
{

	sh64_dcache_purge_sets(0, cpu_data->dcache.sets);
}


/* Assumes this address (+ (2**n_synbits) pages up from it) aren't used for
   anything else in the kernel */
#define MAGIC_PAGE0_START 0xffffffffec000000ULL

/* Purge the physical page 'paddr' from the cache.  It's known that any
 * cache lines requiring attention have the same page colour as the the
 * address 'eaddr'.
 *
 * This relies on the fact that the D-cache matches on physical tags when
 * no virtual tag matches.  So we create an alias for the original page
 * and purge through that.  (Alternatively, we could have done this by
 * switching ASID to match the original mapping and purged through that,
 * but that involves ASID switching cost + probably a TLBMISS + refill
 * anyway.)
 */
static void sh64_dcache_purge_coloured_phy_page(unsigned long paddr,
					        unsigned long eaddr)
{
	unsigned long long magic_page_start;
	unsigned long long magic_eaddr, magic_eaddr_end;

	magic_page_start = MAGIC_PAGE0_START + (eaddr & CACHE_OC_SYN_MASK);

	/* As long as the kernel is not pre-emptible, this doesn't need to be
	   under cli/sti. */
	sh64_setup_dtlb_cache_slot(magic_page_start, get_asid(), paddr);

	magic_eaddr = magic_page_start;
	magic_eaddr_end = magic_eaddr + PAGE_SIZE;

	while (magic_eaddr < magic_eaddr_end) {
		/* Little point in unrolling this loop - the OCBPs are blocking
		   and won't go any quicker (i.e. the loop overhead is parallel
		   to part of the OCBP execution.) */
		__asm__ __volatile__ ("ocbp %0, 0" : : "r" (magic_eaddr));
		magic_eaddr += L1_CACHE_BYTES;
	}

	sh64_teardown_dtlb_cache_slot();
}

/*
 * Purge a page given its physical start address, by creating a temporary
 * 1 page mapping and purging across that.  Even if we know the virtual
 * address (& vma or mm) of the page, the method here is more elegant
 * because it avoids issues of coping with page faults on the purge
 * instructions (i.e. no special-case code required in the critical path
 * in the TLB miss handling).
 */
static void sh64_dcache_purge_phy_page(unsigned long paddr)
{
	unsigned long long eaddr_start, eaddr, eaddr_end;
	int i;

	/* As long as the kernel is not pre-emptible, this doesn't need to be
	   under cli/sti. */
	eaddr_start = MAGIC_PAGE0_START;
	for (i = 0; i < (1 << CACHE_OC_N_SYNBITS); i++) {
		sh64_setup_dtlb_cache_slot(eaddr_start, get_asid(), paddr);

		eaddr = eaddr_start;
		eaddr_end = eaddr + PAGE_SIZE;
		while (eaddr < eaddr_end) {
			__asm__ __volatile__ ("ocbp %0, 0" : : "r" (eaddr));
			eaddr += L1_CACHE_BYTES;
		}

		sh64_teardown_dtlb_cache_slot();
		eaddr_start += PAGE_SIZE;
	}
}

static void sh64_dcache_purge_user_pages(struct mm_struct *mm,
				unsigned long addr, unsigned long end)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	pte_t entry;
	spinlock_t *ptl;
	unsigned long paddr;

	if (!mm)
		return; /* No way to find physical address of page */

	pgd = pgd_offset(mm, addr);
	if (pgd_bad(*pgd))
		return;

	pud = pud_offset(pgd, addr);
	if (pud_none(*pud) || pud_bad(*pud))
		return;

	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd) || pmd_bad(*pmd))
		return;

	pte = pte_offset_map_lock(mm, pmd, addr, &ptl);
	do {
		entry = *pte;
		if (pte_none(entry) || !pte_present(entry))
			continue;
		paddr = pte_val(entry) & PAGE_MASK;
		sh64_dcache_purge_coloured_phy_page(paddr, addr);
	} while (pte++, addr += PAGE_SIZE, addr != end);
	pte_unmap_unlock(pte - 1, ptl);
}

/*
 * There are at least 5 choices for the implementation of this, with
 * pros (+), cons(-), comments(*):
 *
 * 1. ocbp each line in the range through the original user's ASID
 *    + no lines spuriously evicted
 *    - tlbmiss handling (must either handle faults on demand => extra
 *	special-case code in tlbmiss critical path), or map the page in
 *	advance (=> flush_tlb_range in advance to avoid multiple hits)
 *    - ASID switching
 *    - expensive for large ranges
 *
 * 2. temporarily map each page in the range to a special effective
 *    address and ocbp through the temporary mapping; relies on the
 *    fact that SH-5 OCB* always do TLB lookup and match on ptags (they
 *    never look at the etags)
 *    + no spurious evictions
 *    - expensive for large ranges
 *    * surely cheaper than (1)
 *
 * 3. walk all the lines in the cache, check the tags, if a match
 *    occurs create a page mapping to ocbp the line through
 *    + no spurious evictions
 *    - tag inspection overhead
 *    - (especially for small ranges)
 *    - potential cost of setting up/tearing down page mapping for
 *	every line that matches the range
 *    * cost partly independent of range size
 *
 * 4. walk all the lines in the cache, check the tags, if a match
 *    occurs use 4 * alloco to purge the line (+3 other probably
 *    innocent victims) by natural eviction
 *    + no tlb mapping overheads
 *    - spurious evictions
 *    - tag inspection overhead
 *
 * 5. implement like flush_cache_all
 *    + no tag inspection overhead
 *    - spurious evictions
 *    - bad for small ranges
 *
 * (1) can be ruled out as more expensive than (2).  (2) appears best
 * for small ranges.  The choice between (3), (4) and (5) for large
 * ranges and the range size for the large/small boundary need
 * benchmarking to determine.
 *
 * For now use approach (2) for small ranges and (5) for large ones.
 */
static void sh64_dcache_purge_user_range(struct mm_struct *mm,
			  unsigned long start, unsigned long end)
{
	int n_pages = ((end - start) >> PAGE_SHIFT);

	if (n_pages >= 64 || ((start ^ (end - 1)) & PMD_MASK)) {
		sh64_dcache_purge_all();
	} else {
		/* Small range, covered by a single page table page */
		start &= PAGE_MASK;	/* should already be so */
		end = PAGE_ALIGN(end);	/* should already be so */
		sh64_dcache_purge_user_pages(mm, start, end);
	}
}

/*
 * Purge the range of addresses from the D-cache.
 *
 * The addresses lie in the superpage mapping. There's no harm if we
 * overpurge at either end - just a small performance loss.
 */
void __flush_purge_region(void *start, int size)
{
	unsigned long long ullend, addr, aligned_start;

	aligned_start = (unsigned long long)(signed long long)(signed long) start;
	addr = L1_CACHE_ALIGN(aligned_start);
	ullend = (unsigned long long) (signed long long) (signed long) start + size;

	while (addr <= ullend) {
		__asm__ __volatile__ ("ocbp %0, 0" : : "r" (addr));
		addr += L1_CACHE_BYTES;
	}
}

void __flush_wback_region(void *start, int size)
{
	unsigned long long ullend, addr, aligned_start;

	aligned_start = (unsigned long long)(signed long long)(signed long) start;
	addr = L1_CACHE_ALIGN(aligned_start);
	ullend = (unsigned long long) (signed long long) (signed long) start + size;

	while (addr < ullend) {
		__asm__ __volatile__ ("ocbwb %0, 0" : : "r" (addr));
		addr += L1_CACHE_BYTES;
	}
}

void __flush_invalidate_region(void *start, int size)
{
	unsigned long long ullend, addr, aligned_start;

	aligned_start = (unsigned long long)(signed long long)(signed long) start;
	addr = L1_CACHE_ALIGN(aligned_start);
	ullend = (unsigned long long) (signed long long) (signed long) start + size;

	while (addr < ullend) {
		__asm__ __volatile__ ("ocbi %0, 0" : : "r" (addr));
		addr += L1_CACHE_BYTES;
	}
}
#endif /* !CONFIG_DCACHE_DISABLED */

/*
 * Invalidate the entire contents of both caches, after writing back to
 * memory any dirty data from the D-cache.
 */
void flush_cache_all(void)
{
	sh64_dcache_purge_all();
	sh64_icache_inv_all();
}

/*
 * Invalidate an entire user-address space from both caches, after
 * writing back dirty data (e.g. for shared mmap etc).
 *
 * This could be coded selectively by inspecting all the tags then
 * doing 4*alloco on any set containing a match (as for
 * flush_cache_range), but fork/exit/execve (where this is called from)
 * are expensive anyway.
 *
 * Have to do a purge here, despite the comments re I-cache below.
 * There could be odd-coloured dirty data associated with the mm still
 * in the cache - if this gets written out through natural eviction
 * after the kernel has reused the page there will be chaos.
 *
 * The mm being torn down won't ever be active again, so any Icache
 * lines tagged with its ASID won't be visible for the rest of the
 * lifetime of this ASID cycle.  Before the ASID gets reused, there
 * will be a flush_cache_all.  Hence we don't need to touch the
 * I-cache.  This is similar to the lack of action needed in
 * flush_tlb_mm - see fault.c.
 */
void flush_cache_mm(struct mm_struct *mm)
{
	sh64_dcache_purge_all();
}

/*
 * Invalidate (from both caches) the range [start,end) of virtual
 * addresses from the user address space specified by mm, after writing
 * back any dirty data.
 *
 * Note, 'end' is 1 byte beyond the end of the range to flush.
 */
void flush_cache_range(struct vm_area_struct *vma, unsigned long start,
		       unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;

	sh64_dcache_purge_user_range(mm, start, end);
	sh64_icache_inv_user_page_range(mm, start, end);
}

/*
 * Invalidate any entries in either cache for the vma within the user
 * address space vma->vm_mm for the page starting at virtual address
 * 'eaddr'.   This seems to be used primarily in breaking COW.  Note,
 * the I-cache must be searched too in case the page in question is
 * both writable and being executed from (e.g. stack trampolines.)
 *
 * Note, this is called with pte lock held.
 */
void flush_cache_page(struct vm_area_struct *vma, unsigned long eaddr,
		      unsigned long pfn)
{
	sh64_dcache_purge_phy_page(pfn << PAGE_SHIFT);

	if (vma->vm_flags & VM_EXEC)
		sh64_icache_inv_user_page(vma, eaddr);
}

void flush_dcache_page(struct page *page)
{
	sh64_dcache_purge_phy_page(page_to_phys(page));
	wmb();
}

/*
 * Flush the range [start,end] of kernel virtual adddress space from
 * the I-cache.  The corresponding range must be purged from the
 * D-cache also because the SH-5 doesn't have cache snooping between
 * the caches.  The addresses will be visible through the superpage
 * mapping, therefore it's guaranteed that there no cache entries for
 * the range in cache sets of the wrong colour.
 */
void flush_icache_range(unsigned long start, unsigned long end)
{
	__flush_purge_region((void *)start, end);
	wmb();
	sh64_icache_inv_kernel_range(start, end);
}

/*
 * Flush the range of user (defined by vma->vm_mm) address space starting
 * at 'addr' for 'len' bytes from the cache.  The range does not straddle
 * a page boundary, the unique physical page containing the range is
 * 'page'.  This seems to be used mainly for invalidating an address
 * range following a poke into the program text through the ptrace() call
 * from another process (e.g. for BRK instruction insertion).
 */
void flush_icache_user_range(struct vm_area_struct *vma,
			struct page *page, unsigned long addr, int len)
{

	sh64_dcache_purge_coloured_phy_page(page_to_phys(page), addr);
	mb();

	if (vma->vm_flags & VM_EXEC)
		sh64_icache_inv_user_small_range(vma->vm_mm, addr, len);
}

/*
 * For the address range [start,end), write back the data from the
 * D-cache and invalidate the corresponding region of the I-cache for the
 * current process.  Used to flush signal trampolines on the stack to
 * make them executable.
 */
void flush_cache_sigtramp(unsigned long vaddr)
{
	unsigned long end = vaddr + L1_CACHE_BYTES;

	__flush_wback_region((void *)vaddr, L1_CACHE_BYTES);
	wmb();
	sh64_icache_inv_current_user_range(vaddr, end);
}

#ifdef CONFIG_MMU
/*
 * These *MUST* lie in an area of virtual address space that's otherwise
 * unused.
 */
#define UNIQUE_EADDR_START 0xe0000000UL
#define UNIQUE_EADDR_END   0xe8000000UL

/*
 * Given a physical address paddr, and a user virtual address user_eaddr
 * which will eventually be mapped to it, create a one-off kernel-private
 * eaddr mapped to the same paddr.  This is used for creating special
 * destination pages for copy_user_page and clear_user_page.
 */
static unsigned long sh64_make_unique_eaddr(unsigned long user_eaddr,
					    unsigned long paddr)
{
	static unsigned long current_pointer = UNIQUE_EADDR_START;
	unsigned long coloured_pointer;

	if (current_pointer == UNIQUE_EADDR_END) {
		sh64_dcache_purge_all();
		current_pointer = UNIQUE_EADDR_START;
	}

	coloured_pointer = (current_pointer & ~CACHE_OC_SYN_MASK) |
				(user_eaddr & CACHE_OC_SYN_MASK);
	sh64_setup_dtlb_cache_slot(coloured_pointer, get_asid(), paddr);

	current_pointer += (PAGE_SIZE << CACHE_OC_N_SYNBITS);

	return coloured_pointer;
}

static void sh64_copy_user_page_coloured(void *to, void *from,
					 unsigned long address)
{
	void *coloured_to;

	/*
	 * Discard any existing cache entries of the wrong colour.  These are
	 * present quite often, if the kernel has recently used the page
	 * internally, then given it up, then it's been allocated to the user.
	 */
	sh64_dcache_purge_coloured_phy_page(__pa(to), (unsigned long)to);

	coloured_to = (void *)sh64_make_unique_eaddr(address, __pa(to));
	copy_page(from, coloured_to);

	sh64_teardown_dtlb_cache_slot();
}

static void sh64_clear_user_page_coloured(void *to, unsigned long address)
{
	void *coloured_to;

	/*
	 * Discard any existing kernel-originated lines of the wrong
	 * colour (as above)
	 */
	sh64_dcache_purge_coloured_phy_page(__pa(to), (unsigned long)to);

	coloured_to = (void *)sh64_make_unique_eaddr(address, __pa(to));
	clear_page(coloured_to);

	sh64_teardown_dtlb_cache_slot();
}

/*
 * 'from' and 'to' are kernel virtual addresses (within the superpage
 * mapping of the physical RAM).  'address' is the user virtual address
 * where the copy 'to' will be mapped after.  This allows a custom
 * mapping to be used to ensure that the new copy is placed in the
 * right cache sets for the user to see it without having to bounce it
 * out via memory.  Note however : the call to flush_page_to_ram in
 * (generic)/mm/memory.c:(break_cow) undoes all this good work in that one
 * very important case!
 *
 * TBD : can we guarantee that on every call, any cache entries for
 * 'from' are in the same colour sets as 'address' also?  i.e. is this
 * always used just to deal with COW?  (I suspect not).
 *
 * There are two possibilities here for when the page 'from' was last accessed:
 * - by the kernel : this is OK, no purge required.
 * - by the/a user (e.g. for break_COW) : need to purge.
 *
 * If the potential user mapping at 'address' is the same colour as
 * 'from' there is no need to purge any cache lines from the 'from'
 * page mapped into cache sets of colour 'address'.  (The copy will be
 * accessing the page through 'from').
 */
void copy_user_page(void *to, void *from, unsigned long address,
		    struct page *page)
{
	if (((address ^ (unsigned long) from) & CACHE_OC_SYN_MASK) != 0)
		sh64_dcache_purge_coloured_phy_page(__pa(from), address);

	if (((address ^ (unsigned long) to) & CACHE_OC_SYN_MASK) == 0)
		copy_page(to, from);
	else
		sh64_copy_user_page_coloured(to, from, address);
}

/*
 * 'to' is a kernel virtual address (within the superpage mapping of the
 * physical RAM).  'address' is the user virtual address where the 'to'
 * page will be mapped after.  This allows a custom mapping to be used to
 * ensure that the new copy is placed in the right cache sets for the
 * user to see it without having to bounce it out via memory.
 */
void clear_user_page(void *to, unsigned long address, struct page *page)
{
	if (((address ^ (unsigned long) to) & CACHE_OC_SYN_MASK) == 0)
		clear_page(to);
	else
		sh64_clear_user_page_coloured(to, address);
}
#endif
