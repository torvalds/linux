/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * arch/sh64/mm/cache.c
 *
 * Original version Copyright (C) 2000, 2001  Paolo Alberelli
 * Second version Copyright (C) benedict.gaster@superh.com 2002
 * Third version Copyright Richard.Curnow@superh.com 2003
 * Hacks to third version Copyright (C) 2003 Paul Mundt
 */

/****************************************************************************/

#include <linux/config.h>
#include <linux/init.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/threads.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/cache.h>
#include <asm/tlb.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/pgalloc.h> /* for flush_itlb_range */

#include <linux/proc_fs.h>

/* This function is in entry.S */
extern unsigned long switch_and_save_asid(unsigned long new_asid);

/* Wired TLB entry for the D-cache */
static unsigned long long dtlb_cache_slot;

/**
 * sh64_cache_init()
 *
 * This is pretty much just a straightforward clone of the SH
 * detect_cpu_and_cache_system().
 *
 * This function is responsible for setting up all of the cache
 * info dynamically as well as taking care of CPU probing and
 * setting up the relevant subtype data.
 *
 * FIXME: For the time being, we only really support the SH5-101
 * out of the box, and don't support dynamic probing for things
 * like the SH5-103 or even cut2 of the SH5-101. Implement this
 * later!
 */
int __init sh64_cache_init(void)
{
	/*
	 * First, setup some sane values for the I-cache.
	 */
	cpu_data->icache.ways		= 4;
	cpu_data->icache.sets		= 256;
	cpu_data->icache.linesz		= L1_CACHE_BYTES;

	/*
	 * FIXME: This can probably be cleaned up a bit as well.. for example,
	 * do we really need the way shift _and_ the way_step_shift ?? Judging
	 * by the existing code, I would guess no.. is there any valid reason
	 * why we need to be tracking this around?
	 */
	cpu_data->icache.way_shift	= 13;
	cpu_data->icache.entry_shift	= 5;
	cpu_data->icache.set_shift	= 4;
	cpu_data->icache.way_step_shift	= 16;
	cpu_data->icache.asid_shift	= 2;

	/*
	 * way offset = cache size / associativity, so just don't factor in
	 * associativity in the first place..
	 */
	cpu_data->icache.way_ofs	= cpu_data->icache.sets *
					  cpu_data->icache.linesz;

	cpu_data->icache.asid_mask	= 0x3fc;
	cpu_data->icache.idx_mask	= 0x1fe0;
	cpu_data->icache.epn_mask	= 0xffffe000;
	cpu_data->icache.flags		= 0;

	/*
	 * Next, setup some sane values for the D-cache.
	 *
	 * On the SH5, these are pretty consistent with the I-cache settings,
	 * so we just copy over the existing definitions.. these can be fixed
	 * up later, especially if we add runtime CPU probing.
	 *
	 * Though in the meantime it saves us from having to duplicate all of
	 * the above definitions..
	 */
	cpu_data->dcache		= cpu_data->icache;

	/*
	 * Setup any cache-related flags here
	 */
#if defined(CONFIG_DCACHE_WRITE_THROUGH)
	set_bit(SH_CACHE_MODE_WT, &(cpu_data->dcache.flags));
#elif defined(CONFIG_DCACHE_WRITE_BACK)
	set_bit(SH_CACHE_MODE_WB, &(cpu_data->dcache.flags));
#endif

	/*
	 * We also need to reserve a slot for the D-cache in the DTLB, so we
	 * do this now ..
	 */
	dtlb_cache_slot			= sh64_get_wired_dtlb_entry();

	return 0;
}

#ifdef CONFIG_DCACHE_DISABLED
#define sh64_dcache_purge_all()					do { } while (0)
#define sh64_dcache_purge_coloured_phy_page(paddr, eaddr)	do { } while (0)
#define sh64_dcache_purge_user_range(mm, start, end)		do { } while (0)
#define sh64_dcache_purge_phy_page(paddr)			do { } while (0)
#define sh64_dcache_purge_virt_page(mm, eaddr)			do { } while (0)
#define sh64_dcache_purge_kernel_range(start, end)		do { } while (0)
#define sh64_dcache_wback_current_user_range(start, end)	do { } while (0)
#endif

/*##########################################################################*/

/* From here onwards, a rewrite of the implementation,
   by Richard.Curnow@superh.com.

   The major changes in this compared to the old version are;
   1. use more selective purging through OCBP instead of using ALLOCO to purge
      by natural replacement.  This avoids purging out unrelated cache lines
      that happen to be in the same set.
   2. exploit the APIs copy_user_page and clear_user_page better
   3. be more selective about I-cache purging, in particular use invalidate_all
      more sparingly.

   */

/*##########################################################################
			       SUPPORT FUNCTIONS
  ##########################################################################*/

/****************************************************************************/
/* The following group of functions deal with mapping and unmapping a temporary
   page into the DTLB slot that have been set aside for our exclusive use. */
/* In order to accomplish this, we use the generic interface for adding and
   removing a wired slot entry as defined in arch/sh64/mm/tlb.c */
/****************************************************************************/

static unsigned long slot_own_flags;

static inline void sh64_setup_dtlb_cache_slot(unsigned long eaddr, unsigned long asid, unsigned long paddr)
{
	local_irq_save(slot_own_flags);
	sh64_setup_tlb_slot(dtlb_cache_slot, eaddr, asid, paddr);
}

static inline void sh64_teardown_dtlb_cache_slot(void)
{
	sh64_teardown_tlb_slot(dtlb_cache_slot);
	local_irq_restore(slot_own_flags);
}

/****************************************************************************/

#ifndef CONFIG_ICACHE_DISABLED

static void __inline__ sh64_icache_inv_all(void)
{
	unsigned long long addr, flag, data;
	unsigned int flags;

	addr=ICCR0;
	flag=ICCR0_ICI;
	data=0;

	/* Make this a critical section for safety (probably not strictly necessary.) */
	local_irq_save(flags);

	/* Without %1 it gets unexplicably wrong */
	asm volatile("getcfg	%3, 0, %0\n\t"
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
#if (NEFF == 32)
	aligned_start = (unsigned long long)(signed long long)(signed long) start;
#else
#error "NEFF != 32"
#endif
	aligned_start &= L1_CACHE_ALIGN_MASK;
	addr = aligned_start;
#if (NEFF == 32)
	ullend = (unsigned long long) (signed long long) (signed long) end;
#else
#error "NEFF != 32"
#endif
	while (addr <= ullend) {
		asm __volatile__ ("icbi %0, 0" : : "r" (addr));
		addr += L1_CACHE_BYTES;
	}
}

static void sh64_icache_inv_user_page(struct vm_area_struct *vma, unsigned long eaddr)
{
	/* If we get called, we know that vma->vm_flags contains VM_EXEC.
	   Also, eaddr is page-aligned. */

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
	vma_asid = (vma->vm_mm->context & MMU_CONTEXT_ASID_MASK);
	if (running_asid != vma_asid) {
		local_irq_save(flags);
		switch_and_save_asid(vma_asid);
	}
	while (addr < end_addr) {
		/* Worth unrolling a little */
		asm __volatile__("icbi %0,  0" : : "r" (addr));
		asm __volatile__("icbi %0, 32" : : "r" (addr));
		asm __volatile__("icbi %0, 64" : : "r" (addr));
		asm __volatile__("icbi %0, 96" : : "r" (addr));
		addr += 128;
	}
	if (running_asid != vma_asid) {
		switch_and_save_asid(running_asid);
		local_irq_restore(flags);
	}
}

/****************************************************************************/

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

	if (!mm) return;

	n_pages = ((end - start) >> PAGE_SHIFT);
	if (n_pages >= 64) {
		sh64_icache_inv_all();
	} else {
		unsigned long aligned_start;
		unsigned long eaddr;
		unsigned long after_last_page_start;
		unsigned long mm_asid, current_asid;
		unsigned long long flags = 0ULL;

		mm_asid = mm->context & MMU_CONTEXT_ASID_MASK;
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

static void sh64_icache_inv_user_small_range(struct mm_struct *mm,
						unsigned long start, int len)
{

	/* Invalidate a small range of user context I-cache, not necessarily
	   page (or even cache-line) aligned. */

	unsigned long long eaddr = start;
	unsigned long long eaddr_end = start + len;
	unsigned long current_asid, mm_asid;
	unsigned long long flags;
	unsigned long long epage_start;

	/* Since this is used inside ptrace, the ASID in the mm context
	   typically won't match current_asid.  We'll have to switch ASID to do
	   this.  For safety, and given that the range will be small, do all
	   this under cli.

	   Note, there is a hazard that the ASID in mm->context is no longer
	   actually associated with mm, i.e. if the mm->context has started a
	   new cycle since mm was last active.  However, this is just a
	   performance issue: all that happens is that we invalidate lines
	   belonging to another mm, so the owning process has to refill them
	   when that mm goes live again.  mm itself can't have any cache
	   entries because there will have been a flush_cache_all when the new
	   mm->context cycle started. */

	/* Align to start of cache line.  Otherwise, suppose len==8 and start
	   was at 32N+28 : the last 4 bytes wouldn't get invalidated. */
	eaddr = start & L1_CACHE_ALIGN_MASK;
	eaddr_end = start + len;

	local_irq_save(flags);
	mm_asid = mm->context & MMU_CONTEXT_ASID_MASK;
	current_asid = switch_and_save_asid(mm_asid);

	epage_start = eaddr & PAGE_MASK;

	while (eaddr < eaddr_end)
	{
		asm __volatile__("icbi %0, 0" : : "r" (eaddr));
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
	aligned_start = start & L1_CACHE_ALIGN_MASK;
	addr = aligned_start;
	while (addr < ull_end) {
		asm __volatile__ ("icbi %0, 0" : : "r" (addr));
		asm __volatile__ ("nop");
		asm __volatile__ ("nop");
		addr += L1_CACHE_BYTES;
	}
}

#endif /* !CONFIG_ICACHE_DISABLED */

/****************************************************************************/

#ifndef CONFIG_DCACHE_DISABLED

/* Buffer used as the target of alloco instructions to purge data from cache
   sets by natural eviction. -- RPC */
#define DUMMY_ALLOCO_AREA_SIZE L1_CACHE_SIZE_BYTES + (1024 * 4)
static unsigned char dummy_alloco_area[DUMMY_ALLOCO_AREA_SIZE] __cacheline_aligned = { 0, };

/****************************************************************************/

static void __inline__ sh64_dcache_purge_sets(int sets_to_purge_base, int n_sets)
{
	/* Purge all ways in a particular block of sets, specified by the base
	   set number and number of sets.  Can handle wrap-around, if that's
	   needed.  */

	int dummy_buffer_base_set;
	unsigned long long eaddr, eaddr0, eaddr1;
	int j;
	int set_offset;

	dummy_buffer_base_set = ((int)&dummy_alloco_area & cpu_data->dcache.idx_mask) >> cpu_data->dcache.entry_shift;
	set_offset = sets_to_purge_base - dummy_buffer_base_set;

	for (j=0; j<n_sets; j++, set_offset++) {
		set_offset &= (cpu_data->dcache.sets - 1);
		eaddr0 = (unsigned long long)dummy_alloco_area + (set_offset << cpu_data->dcache.entry_shift);

		/* Do one alloco which hits the required set per cache way.  For
		   write-back mode, this will purge the #ways resident lines.   There's
		   little point unrolling this loop because the allocos stall more if
		   they're too close together. */
		eaddr1 = eaddr0 + cpu_data->dcache.way_ofs * cpu_data->dcache.ways;
		for (eaddr=eaddr0; eaddr<eaddr1; eaddr+=cpu_data->dcache.way_ofs) {
			asm __volatile__ ("alloco %0, 0" : : "r" (eaddr));
			asm __volatile__ ("synco"); /* TAKum03020 */
		}

		eaddr1 = eaddr0 + cpu_data->dcache.way_ofs * cpu_data->dcache.ways;
		for (eaddr=eaddr0; eaddr<eaddr1; eaddr+=cpu_data->dcache.way_ofs) {
			/* Load from each address.  Required because alloco is a NOP if
			   the cache is write-through.  Write-through is a config option. */
			if (test_bit(SH_CACHE_MODE_WT, &(cpu_data->dcache.flags)))
				*(volatile unsigned char *)(int)eaddr;
		}
	}

	/* Don't use OCBI to invalidate the lines.  That costs cycles directly.
	   If the dummy block is just left resident, it will naturally get
	   evicted as required.  */

	return;
}

/****************************************************************************/

static void sh64_dcache_purge_all(void)
{
	/* Purge the entire contents of the dcache.  The most efficient way to
	   achieve this is to use alloco instructions on a region of unused
	   memory equal in size to the cache, thereby causing the current
	   contents to be discarded by natural eviction.  The alternative,
	   namely reading every tag, setting up a mapping for the corresponding
	   page and doing an OCBP for the line, would be much more expensive.
	   */

	sh64_dcache_purge_sets(0, cpu_data->dcache.sets);

	return;

}

/****************************************************************************/

static void sh64_dcache_purge_kernel_range(unsigned long start, unsigned long end)
{
	/* Purge the range of addresses [start,end] from the D-cache.  The
	   addresses lie in the superpage mapping.  There's no harm if we
	   overpurge at either end - just a small performance loss. */
	unsigned long long ullend, addr, aligned_start;
#if (NEFF == 32)
	aligned_start = (unsigned long long)(signed long long)(signed long) start;
#else
#error "NEFF != 32"
#endif
	aligned_start &= L1_CACHE_ALIGN_MASK;
	addr = aligned_start;
#if (NEFF == 32)
	ullend = (unsigned long long) (signed long long) (signed long) end;
#else
#error "NEFF != 32"
#endif
	while (addr <= ullend) {
		asm __volatile__ ("ocbp %0, 0" : : "r" (addr));
		addr += L1_CACHE_BYTES;
	}
	return;
}

/* Assumes this address (+ (2**n_synbits) pages up from it) aren't used for
   anything else in the kernel */
#define MAGIC_PAGE0_START 0xffffffffec000000ULL

static void sh64_dcache_purge_coloured_phy_page(unsigned long paddr, unsigned long eaddr)
{
	/* Purge the physical page 'paddr' from the cache.  It's known that any
	   cache lines requiring attention have the same page colour as the the
	   address 'eaddr'.

	   This relies on the fact that the D-cache matches on physical tags
	   when no virtual tag matches.  So we create an alias for the original
	   page and purge through that.  (Alternatively, we could have done
	   this by switching ASID to match the original mapping and purged
	   through that, but that involves ASID switching cost + probably a
	   TLBMISS + refill anyway.)
	   */

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
		asm __volatile__ ("ocbp %0, 0" : : "r" (magic_eaddr));
		magic_eaddr += L1_CACHE_BYTES;
	}

	sh64_teardown_dtlb_cache_slot();
}

/****************************************************************************/

static void sh64_dcache_purge_phy_page(unsigned long paddr)
{
	/* Pure a page given its physical start address, by creating a
	   temporary 1 page mapping and purging across that.  Even if we know
	   the virtual address (& vma or mm) of the page, the method here is
	   more elegant because it avoids issues of coping with page faults on
	   the purge instructions (i.e. no special-case code required in the
	   critical path in the TLB miss handling). */

	unsigned long long eaddr_start, eaddr, eaddr_end;
	int i;

	/* As long as the kernel is not pre-emptible, this doesn't need to be
	   under cli/sti. */

	eaddr_start = MAGIC_PAGE0_START;
	for (i=0; i < (1 << CACHE_OC_N_SYNBITS); i++) {
		sh64_setup_dtlb_cache_slot(eaddr_start, get_asid(), paddr);

		eaddr = eaddr_start;
		eaddr_end = eaddr + PAGE_SIZE;
		while (eaddr < eaddr_end) {
			asm __volatile__ ("ocbp %0, 0" : : "r" (eaddr));
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

	pmd = pmd_offset(pgd, addr);
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
/****************************************************************************/

static void sh64_dcache_purge_user_range(struct mm_struct *mm,
			  unsigned long start, unsigned long end)
{
	/* There are at least 5 choices for the implementation of this, with
	   pros (+), cons(-), comments(*):

	   1. ocbp each line in the range through the original user's ASID
	      + no lines spuriously evicted
	      - tlbmiss handling (must either handle faults on demand => extra
		special-case code in tlbmiss critical path), or map the page in
		advance (=> flush_tlb_range in advance to avoid multiple hits)
	      - ASID switching
	      - expensive for large ranges

	   2. temporarily map each page in the range to a special effective
	      address and ocbp through the temporary mapping; relies on the
	      fact that SH-5 OCB* always do TLB lookup and match on ptags (they
	      never look at the etags)
	      + no spurious evictions
	      - expensive for large ranges
	      * surely cheaper than (1)

	   3. walk all the lines in the cache, check the tags, if a match
	      occurs create a page mapping to ocbp the line through
	      + no spurious evictions
	      - tag inspection overhead
	      - (especially for small ranges)
	      - potential cost of setting up/tearing down page mapping for
		every line that matches the range
	      * cost partly independent of range size

	   4. walk all the lines in the cache, check the tags, if a match
	      occurs use 4 * alloco to purge the line (+3 other probably
	      innocent victims) by natural eviction
	      + no tlb mapping overheads
	      - spurious evictions
	      - tag inspection overhead

	   5. implement like flush_cache_all
	      + no tag inspection overhead
	      - spurious evictions
	      - bad for small ranges

	   (1) can be ruled out as more expensive than (2).  (2) appears best
	   for small ranges.  The choice between (3), (4) and (5) for large
	   ranges and the range size for the large/small boundary need
	   benchmarking to determine.

	   For now use approach (2) for small ranges and (5) for large ones.

	   */

	int n_pages;

	n_pages = ((end - start) >> PAGE_SHIFT);
	if (n_pages >= 64 || ((start ^ (end - 1)) & PMD_MASK)) {
#if 1
		sh64_dcache_purge_all();
#else
		unsigned long long set, way;
		unsigned long mm_asid = mm->context & MMU_CONTEXT_ASID_MASK;
		for (set = 0; set < cpu_data->dcache.sets; set++) {
			unsigned long long set_base_config_addr = CACHE_OC_ADDRESS_ARRAY + (set << cpu_data->dcache.set_shift);
			for (way = 0; way < cpu_data->dcache.ways; way++) {
				unsigned long long config_addr = set_base_config_addr + (way << cpu_data->dcache.way_step_shift);
				unsigned long long tag0;
				unsigned long line_valid;

				asm __volatile__("getcfg %1, 0, %0" : "=r" (tag0) : "r" (config_addr));
				line_valid = tag0 & SH_CACHE_VALID;
				if (line_valid) {
					unsigned long cache_asid;
					unsigned long epn;

					cache_asid = (tag0 & cpu_data->dcache.asid_mask) >> cpu_data->dcache.asid_shift;
					/* The next line needs some
					   explanation.  The virtual tags
					   encode bits [31:13] of the virtual
					   address, bit [12] of the 'tag' being
					   implied by the cache set index. */
					epn = (tag0 & cpu_data->dcache.epn_mask) | ((set & 0x80) << cpu_data->dcache.entry_shift);

					if ((cache_asid == mm_asid) && (start <= epn) && (epn < end)) {
						/* TODO : could optimise this
						   call by batching multiple
						   adjacent sets together. */
						sh64_dcache_purge_sets(set, 1);
						break; /* Don't waste time inspecting other ways for this set */
					}
				}
			}
		}
#endif
	} else {
		/* Small range, covered by a single page table page */
		start &= PAGE_MASK;	/* should already be so */
		end = PAGE_ALIGN(end);	/* should already be so */
		sh64_dcache_purge_user_pages(mm, start, end);
	}
	return;
}

static void sh64_dcache_wback_current_user_range(unsigned long start, unsigned long end)
{
	unsigned long long aligned_start;
	unsigned long long ull_end;
	unsigned long long addr;

	ull_end = end;

	/* Just wback over the range using the natural addresses.  TLB miss
	   handling will be OK (TBC) : the range has just been written to by
	   the signal frame setup code, so the PTEs must exist.

	   Note, if we have CONFIG_PREEMPT and get preempted inside this loop,
	   it doesn't matter, even if the pid->ASID mapping changes whilst
	   we're away.  In that case the cache will have been flushed when the
	   mapping was renewed.  So the writebacks below will be nugatory (and
	   we'll doubtless have to fault the TLB entry/ies in again with the
	   new ASID), but it's a rare case.
	   */
	aligned_start = start & L1_CACHE_ALIGN_MASK;
	addr = aligned_start;
	while (addr < ull_end) {
		asm __volatile__ ("ocbwb %0, 0" : : "r" (addr));
		addr += L1_CACHE_BYTES;
	}
}

/****************************************************************************/

/* These *MUST* lie in an area of virtual address space that's otherwise unused. */
#define UNIQUE_EADDR_START 0xe0000000UL
#define UNIQUE_EADDR_END   0xe8000000UL

static unsigned long sh64_make_unique_eaddr(unsigned long user_eaddr, unsigned long paddr)
{
	/* Given a physical address paddr, and a user virtual address
	   user_eaddr which will eventually be mapped to it, create a one-off
	   kernel-private eaddr mapped to the same paddr.  This is used for
	   creating special destination pages for copy_user_page and
	   clear_user_page */

	static unsigned long current_pointer = UNIQUE_EADDR_START;
	unsigned long coloured_pointer;

	if (current_pointer == UNIQUE_EADDR_END) {
		sh64_dcache_purge_all();
		current_pointer = UNIQUE_EADDR_START;
	}

	coloured_pointer = (current_pointer & ~CACHE_OC_SYN_MASK) | (user_eaddr & CACHE_OC_SYN_MASK);
	sh64_setup_dtlb_cache_slot(coloured_pointer, get_asid(), paddr);

	current_pointer += (PAGE_SIZE << CACHE_OC_N_SYNBITS);

	return coloured_pointer;
}

/****************************************************************************/

static void sh64_copy_user_page_coloured(void *to, void *from, unsigned long address)
{
	void *coloured_to;

	/* Discard any existing cache entries of the wrong colour.  These are
	   present quite often, if the kernel has recently used the page
	   internally, then given it up, then it's been allocated to the user.
	   */
	sh64_dcache_purge_coloured_phy_page(__pa(to), (unsigned long) to);

	coloured_to = (void *) sh64_make_unique_eaddr(address, __pa(to));
	sh64_page_copy(from, coloured_to);

	sh64_teardown_dtlb_cache_slot();
}

static void sh64_clear_user_page_coloured(void *to, unsigned long address)
{
	void *coloured_to;

	/* Discard any existing kernel-originated lines of the wrong colour (as
	   above) */
	sh64_dcache_purge_coloured_phy_page(__pa(to), (unsigned long) to);

	coloured_to = (void *) sh64_make_unique_eaddr(address, __pa(to));
	sh64_page_clear(coloured_to);

	sh64_teardown_dtlb_cache_slot();
}

#endif /* !CONFIG_DCACHE_DISABLED */

/****************************************************************************/

/*##########################################################################
			    EXTERNALLY CALLABLE API.
  ##########################################################################*/

/* These functions are described in Documentation/cachetlb.txt.
   Each one of these functions varies in behaviour depending on whether the
   I-cache and/or D-cache are configured out.

   Note that the Linux term 'flush' corresponds to what is termed 'purge' in
   the sh/sh64 jargon for the D-cache, i.e. write back dirty data then
   invalidate the cache lines, and 'invalidate' for the I-cache.
   */

#undef FLUSH_TRACE

void flush_cache_all(void)
{
	/* Invalidate the entire contents of both caches, after writing back to
	   memory any dirty data from the D-cache. */
	sh64_dcache_purge_all();
	sh64_icache_inv_all();
}

/****************************************************************************/

void flush_cache_mm(struct mm_struct *mm)
{
	/* Invalidate an entire user-address space from both caches, after
	   writing back dirty data (e.g. for shared mmap etc). */

	/* This could be coded selectively by inspecting all the tags then
	   doing 4*alloco on any set containing a match (as for
	   flush_cache_range), but fork/exit/execve (where this is called from)
	   are expensive anyway. */

	/* Have to do a purge here, despite the comments re I-cache below.
	   There could be odd-coloured dirty data associated with the mm still
	   in the cache - if this gets written out through natural eviction
	   after the kernel has reused the page there will be chaos.
	   */

	sh64_dcache_purge_all();

	/* The mm being torn down won't ever be active again, so any Icache
	   lines tagged with its ASID won't be visible for the rest of the
	   lifetime of this ASID cycle.  Before the ASID gets reused, there
	   will be a flush_cache_all.  Hence we don't need to touch the
	   I-cache.  This is similar to the lack of action needed in
	   flush_tlb_mm - see fault.c. */
}

/****************************************************************************/

void flush_cache_range(struct vm_area_struct *vma, unsigned long start,
		       unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;

	/* Invalidate (from both caches) the range [start,end) of virtual
	   addresses from the user address space specified by mm, after writing
	   back any dirty data.

	   Note, 'end' is 1 byte beyond the end of the range to flush. */

	sh64_dcache_purge_user_range(mm, start, end);
	sh64_icache_inv_user_page_range(mm, start, end);
}

/****************************************************************************/

void flush_cache_page(struct vm_area_struct *vma, unsigned long eaddr, unsigned long pfn)
{
	/* Invalidate any entries in either cache for the vma within the user
	   address space vma->vm_mm for the page starting at virtual address
	   'eaddr'.   This seems to be used primarily in breaking COW.  Note,
	   the I-cache must be searched too in case the page in question is
	   both writable and being executed from (e.g. stack trampolines.)

	   Note, this is called with pte lock held.
	   */

	sh64_dcache_purge_phy_page(pfn << PAGE_SHIFT);

	if (vma->vm_flags & VM_EXEC) {
		sh64_icache_inv_user_page(vma, eaddr);
	}
}

/****************************************************************************/

#ifndef CONFIG_DCACHE_DISABLED

void copy_user_page(void *to, void *from, unsigned long address, struct page *page)
{
	/* 'from' and 'to' are kernel virtual addresses (within the superpage
	   mapping of the physical RAM).  'address' is the user virtual address
	   where the copy 'to' will be mapped after.  This allows a custom
	   mapping to be used to ensure that the new copy is placed in the
	   right cache sets for the user to see it without having to bounce it
	   out via memory.  Note however : the call to flush_page_to_ram in
	   (generic)/mm/memory.c:(break_cow) undoes all this good work in that one
	   very important case!

	   TBD : can we guarantee that on every call, any cache entries for
	   'from' are in the same colour sets as 'address' also?  i.e. is this
	   always used just to deal with COW?  (I suspect not). */

	/* There are two possibilities here for when the page 'from' was last accessed:
	   * by the kernel : this is OK, no purge required.
	   * by the/a user (e.g. for break_COW) : need to purge.

	   If the potential user mapping at 'address' is the same colour as
	   'from' there is no need to purge any cache lines from the 'from'
	   page mapped into cache sets of colour 'address'.  (The copy will be
	   accessing the page through 'from').
	   */

	if (((address ^ (unsigned long) from) & CACHE_OC_SYN_MASK) != 0) {
		sh64_dcache_purge_coloured_phy_page(__pa(from), address);
	}

	if (((address ^ (unsigned long) to) & CACHE_OC_SYN_MASK) == 0) {
		/* No synonym problem on destination */
		sh64_page_copy(from, to);
	} else {
		sh64_copy_user_page_coloured(to, from, address);
	}

	/* Note, don't need to flush 'from' page from the cache again - it's
	   done anyway by the generic code */
}

void clear_user_page(void *to, unsigned long address, struct page *page)
{
	/* 'to' is a kernel virtual address (within the superpage
	   mapping of the physical RAM).  'address' is the user virtual address
	   where the 'to' page will be mapped after.  This allows a custom
	   mapping to be used to ensure that the new copy is placed in the
	   right cache sets for the user to see it without having to bounce it
	   out via memory.
	*/

	if (((address ^ (unsigned long) to) & CACHE_OC_SYN_MASK) == 0) {
		/* No synonym problem on destination */
		sh64_page_clear(to);
	} else {
		sh64_clear_user_page_coloured(to, address);
	}
}

#endif /* !CONFIG_DCACHE_DISABLED */

/****************************************************************************/

void flush_dcache_page(struct page *page)
{
	sh64_dcache_purge_phy_page(page_to_phys(page));
	wmb();
}

/****************************************************************************/

void flush_icache_range(unsigned long start, unsigned long end)
{
	/* Flush the range [start,end] of kernel virtual adddress space from
	   the I-cache.  The corresponding range must be purged from the
	   D-cache also because the SH-5 doesn't have cache snooping between
	   the caches.  The addresses will be visible through the superpage
	   mapping, therefore it's guaranteed that there no cache entries for
	   the range in cache sets of the wrong colour.

	   Primarily used for cohering the I-cache after a module has
	   been loaded.  */

	/* We also make sure to purge the same range from the D-cache since
	   flush_page_to_ram() won't be doing this for us! */

	sh64_dcache_purge_kernel_range(start, end);
	wmb();
	sh64_icache_inv_kernel_range(start, end);
}

/****************************************************************************/

void flush_icache_user_range(struct vm_area_struct *vma,
			struct page *page, unsigned long addr, int len)
{
	/* Flush the range of user (defined by vma->vm_mm) address space
	   starting at 'addr' for 'len' bytes from the cache.  The range does
	   not straddle a page boundary, the unique physical page containing
	   the range is 'page'.  This seems to be used mainly for invalidating
	   an address range following a poke into the program text through the
	   ptrace() call from another process (e.g. for BRK instruction
	   insertion). */

	sh64_dcache_purge_coloured_phy_page(page_to_phys(page), addr);
	mb();

	if (vma->vm_flags & VM_EXEC) {
		sh64_icache_inv_user_small_range(vma->vm_mm, addr, len);
	}
}

/*##########################################################################
			ARCH/SH64 PRIVATE CALLABLE API.
  ##########################################################################*/

void flush_cache_sigtramp(unsigned long start, unsigned long end)
{
	/* For the address range [start,end), write back the data from the
	   D-cache and invalidate the corresponding region of the I-cache for
	   the current process.  Used to flush signal trampolines on the stack
	   to make them executable. */

	sh64_dcache_wback_current_user_range(start, end);
	wmb();
	sh64_icache_inv_current_user_range(start, end);
}

