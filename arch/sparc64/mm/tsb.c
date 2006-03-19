/* arch/sparc64/mm/tsb.c
 *
 * Copyright (C) 2006 David S. Miller <davem@davemloft.net>
 */

#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/tlbflush.h>
#include <asm/tlb.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/tsb.h>
#include <asm/oplib.h>

extern struct tsb swapper_tsb[KERNEL_TSB_NENTRIES];

static inline unsigned long tsb_hash(unsigned long vaddr, unsigned long nentries)
{
	vaddr >>= PAGE_SHIFT;
	return vaddr & (nentries - 1);
}

static inline int tag_compare(unsigned long tag, unsigned long vaddr)
{
	return (tag == (vaddr >> 22));
}

/* TSB flushes need only occur on the processor initiating the address
 * space modification, not on each cpu the address space has run on.
 * Only the TLB flush needs that treatment.
 */

void flush_tsb_kernel_range(unsigned long start, unsigned long end)
{
	unsigned long v;

	for (v = start; v < end; v += PAGE_SIZE) {
		unsigned long hash = tsb_hash(v, KERNEL_TSB_NENTRIES);
		struct tsb *ent = &swapper_tsb[hash];

		if (tag_compare(ent->tag, v)) {
			ent->tag = (1UL << TSB_TAG_INVALID_BIT);
			membar_storeload_storestore();
		}
	}
}

void flush_tsb_user(struct mmu_gather *mp)
{
	struct mm_struct *mm = mp->mm;
	unsigned long nentries, base, flags;
	struct tsb *tsb;
	int i;

	spin_lock_irqsave(&mm->context.lock, flags);

	tsb = mm->context.tsb;
	nentries = mm->context.tsb_nentries;

	if (tlb_type == cheetah_plus || tlb_type == hypervisor)
		base = __pa(tsb);
	else
		base = (unsigned long) tsb;
	
	for (i = 0; i < mp->tlb_nr; i++) {
		unsigned long v = mp->vaddrs[i];
		unsigned long tag, ent, hash;

		v &= ~0x1UL;

		hash = tsb_hash(v, nentries);
		ent = base + (hash * sizeof(struct tsb));
		tag = (v >> 22UL);

		tsb_flush(ent, tag);
	}

	spin_unlock_irqrestore(&mm->context.lock, flags);
}

static void setup_tsb_params(struct mm_struct *mm, unsigned long tsb_bytes)
{
	unsigned long tsb_reg, base, tsb_paddr;
	unsigned long page_sz, tte;

	mm->context.tsb_nentries = tsb_bytes / sizeof(struct tsb);

	base = TSBMAP_BASE;
	tte = pgprot_val(PAGE_KERNEL_LOCKED);
	tsb_paddr = __pa(mm->context.tsb);
	BUG_ON(tsb_paddr & (tsb_bytes - 1UL));

	/* Use the smallest page size that can map the whole TSB
	 * in one TLB entry.
	 */
	switch (tsb_bytes) {
	case 8192 << 0:
		tsb_reg = 0x0UL;
#ifdef DCACHE_ALIASING_POSSIBLE
		base += (tsb_paddr & 8192);
#endif
		page_sz = 8192;
		break;

	case 8192 << 1:
		tsb_reg = 0x1UL;
		page_sz = 64 * 1024;
		break;

	case 8192 << 2:
		tsb_reg = 0x2UL;
		page_sz = 64 * 1024;
		break;

	case 8192 << 3:
		tsb_reg = 0x3UL;
		page_sz = 64 * 1024;
		break;

	case 8192 << 4:
		tsb_reg = 0x4UL;
		page_sz = 512 * 1024;
		break;

	case 8192 << 5:
		tsb_reg = 0x5UL;
		page_sz = 512 * 1024;
		break;

	case 8192 << 6:
		tsb_reg = 0x6UL;
		page_sz = 512 * 1024;
		break;

	case 8192 << 7:
		tsb_reg = 0x7UL;
		page_sz = 4 * 1024 * 1024;
		break;

	default:
		BUG();
	};
	tte |= pte_sz_bits(page_sz);

	if (tlb_type == cheetah_plus || tlb_type == hypervisor) {
		/* Physical mapping, no locked TLB entry for TSB.  */
		tsb_reg |= tsb_paddr;

		mm->context.tsb_reg_val = tsb_reg;
		mm->context.tsb_map_vaddr = 0;
		mm->context.tsb_map_pte = 0;
	} else {
		tsb_reg |= base;
		tsb_reg |= (tsb_paddr & (page_sz - 1UL));
		tte |= (tsb_paddr & ~(page_sz - 1UL));

		mm->context.tsb_reg_val = tsb_reg;
		mm->context.tsb_map_vaddr = base;
		mm->context.tsb_map_pte = tte;
	}

	/* Setup the Hypervisor TSB descriptor.  */
	if (tlb_type == hypervisor) {
		struct hv_tsb_descr *hp = &mm->context.tsb_descr;

		switch (PAGE_SIZE) {
		case 8192:
		default:
			hp->pgsz_idx = HV_PGSZ_IDX_8K;
			break;

		case 64 * 1024:
			hp->pgsz_idx = HV_PGSZ_IDX_64K;
			break;

		case 512 * 1024:
			hp->pgsz_idx = HV_PGSZ_IDX_512K;
			break;

		case 4 * 1024 * 1024:
			hp->pgsz_idx = HV_PGSZ_IDX_4MB;
			break;
		};
		hp->assoc = 1;
		hp->num_ttes = tsb_bytes / 16;
		hp->ctx_idx = 0;
		switch (PAGE_SIZE) {
		case 8192:
		default:
			hp->pgsz_mask = HV_PGSZ_MASK_8K;
			break;

		case 64 * 1024:
			hp->pgsz_mask = HV_PGSZ_MASK_64K;
			break;

		case 512 * 1024:
			hp->pgsz_mask = HV_PGSZ_MASK_512K;
			break;

		case 4 * 1024 * 1024:
			hp->pgsz_mask = HV_PGSZ_MASK_4MB;
			break;
		};
		hp->tsb_base = tsb_paddr;
		hp->resv = 0;
	}
}

static kmem_cache_t *tsb_caches[8] __read_mostly;

static const char *tsb_cache_names[8] = {
	"tsb_8KB",
	"tsb_16KB",
	"tsb_32KB",
	"tsb_64KB",
	"tsb_128KB",
	"tsb_256KB",
	"tsb_512KB",
	"tsb_1MB",
};

void __init tsb_cache_init(void)
{
	unsigned long i;

	for (i = 0; i < 8; i++) {
		unsigned long size = 8192 << i;
		const char *name = tsb_cache_names[i];

		tsb_caches[i] = kmem_cache_create(name,
						  size, size,
						  SLAB_HWCACHE_ALIGN |
						  SLAB_MUST_HWCACHE_ALIGN,
						  NULL, NULL);
		if (!tsb_caches[i]) {
			prom_printf("Could not create %s cache\n", name);
			prom_halt();
		}
	}
}

/* When the RSS of an address space exceeds mm->context.tsb_rss_limit,
 * do_sparc64_fault() invokes this routine to try and grow the TSB.
 *
 * When we reach the maximum TSB size supported, we stick ~0UL into
 * mm->context.tsb_rss_limit so the grow checks in update_mmu_cache()
 * will not trigger any longer.
 *
 * The TSB can be anywhere from 8K to 1MB in size, in increasing powers
 * of two.  The TSB must be aligned to it's size, so f.e. a 512K TSB
 * must be 512K aligned.  It also must be physically contiguous, so we
 * cannot use vmalloc().
 *
 * The idea here is to grow the TSB when the RSS of the process approaches
 * the number of entries that the current TSB can hold at once.  Currently,
 * we trigger when the RSS hits 3/4 of the TSB capacity.
 */
void tsb_grow(struct mm_struct *mm, unsigned long rss)
{
	unsigned long max_tsb_size = 1 * 1024 * 1024;
	unsigned long new_size, old_size, flags;
	struct tsb *old_tsb, *new_tsb;
	unsigned long new_cache_index, old_cache_index;
	unsigned long new_rss_limit;
	gfp_t gfp_flags;

	if (max_tsb_size > (PAGE_SIZE << MAX_ORDER))
		max_tsb_size = (PAGE_SIZE << MAX_ORDER);

	new_cache_index = 0;
	for (new_size = 8192; new_size < max_tsb_size; new_size <<= 1UL) {
		unsigned long n_entries = new_size / sizeof(struct tsb);

		n_entries = (n_entries * 3) / 4;
		if (n_entries > rss)
			break;

		new_cache_index++;
	}

	if (new_size == max_tsb_size)
		new_rss_limit = ~0UL;
	else
		new_rss_limit = ((new_size / sizeof(struct tsb)) * 3) / 4;

retry_tsb_alloc:
	gfp_flags = GFP_KERNEL;
	if (new_size > (PAGE_SIZE * 2))
		gfp_flags = __GFP_NOWARN | __GFP_NORETRY;

	new_tsb = kmem_cache_alloc(tsb_caches[new_cache_index], gfp_flags);
	if (unlikely(!new_tsb)) {
		/* Not being able to fork due to a high-order TSB
		 * allocation failure is very bad behavior.  Just back
		 * down to a 0-order allocation and force no TSB
		 * growing for this address space.
		 */
		if (mm->context.tsb == NULL && new_cache_index > 0) {
			new_cache_index = 0;
			new_size = 8192;
			new_rss_limit = ~0UL;
			goto retry_tsb_alloc;
		}

		/* If we failed on a TSB grow, we are under serious
		 * memory pressure so don't try to grow any more.
		 */
		if (mm->context.tsb != NULL)
			mm->context.tsb_rss_limit = ~0UL;
		return;
	}

	/* Mark all tags as invalid.  */
	memset(new_tsb, 0x40, new_size);

	/* Ok, we are about to commit the changes.  If we are
	 * growing an existing TSB the locking is very tricky,
	 * so WATCH OUT!
	 *
	 * We have to hold mm->context.lock while committing to the
	 * new TSB, this synchronizes us with processors in
	 * flush_tsb_user() and switch_mm() for this address space.
	 *
	 * But even with that lock held, processors run asynchronously
	 * accessing the old TSB via TLB miss handling.  This is OK
	 * because those actions are just propagating state from the
	 * Linux page tables into the TSB, page table mappings are not
	 * being changed.  If a real fault occurs, the processor will
	 * synchronize with us when it hits flush_tsb_user(), this is
	 * also true for the case where vmscan is modifying the page
	 * tables.  The only thing we need to be careful with is to
	 * skip any locked TSB entries during copy_tsb().
	 *
	 * When we finish committing to the new TSB, we have to drop
	 * the lock and ask all other cpus running this address space
	 * to run tsb_context_switch() to see the new TSB table.
	 */
	spin_lock_irqsave(&mm->context.lock, flags);

	old_tsb = mm->context.tsb;
	old_cache_index = (mm->context.tsb_reg_val & 0x7UL);
	old_size = mm->context.tsb_nentries * sizeof(struct tsb);


	/* Handle multiple threads trying to grow the TSB at the same time.
	 * One will get in here first, and bump the size and the RSS limit.
	 * The others will get in here next and hit this check.
	 */
	if (unlikely(old_tsb && (rss < mm->context.tsb_rss_limit))) {
		spin_unlock_irqrestore(&mm->context.lock, flags);

		kmem_cache_free(tsb_caches[new_cache_index], new_tsb);
		return;
	}

	mm->context.tsb_rss_limit = new_rss_limit;

	if (old_tsb) {
		extern void copy_tsb(unsigned long old_tsb_base,
				     unsigned long old_tsb_size,
				     unsigned long new_tsb_base,
				     unsigned long new_tsb_size);
		unsigned long old_tsb_base = (unsigned long) old_tsb;
		unsigned long new_tsb_base = (unsigned long) new_tsb;

		if (tlb_type == cheetah_plus || tlb_type == hypervisor) {
			old_tsb_base = __pa(old_tsb_base);
			new_tsb_base = __pa(new_tsb_base);
		}
		copy_tsb(old_tsb_base, old_size, new_tsb_base, new_size);
	}

	mm->context.tsb = new_tsb;
	setup_tsb_params(mm, new_size);

	spin_unlock_irqrestore(&mm->context.lock, flags);

	/* If old_tsb is NULL, we're being invoked for the first time
	 * from init_new_context().
	 */
	if (old_tsb) {
		/* Reload it on the local cpu.  */
		tsb_context_switch(mm);

		/* Now force other processors to do the same.  */
		smp_tsb_sync(mm);

		/* Now it is safe to free the old tsb.  */
		kmem_cache_free(tsb_caches[old_cache_index], old_tsb);
	}
}

int init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	spin_lock_init(&mm->context.lock);

	mm->context.sparc64_ctx_val = 0UL;

	/* copy_mm() copies over the parent's mm_struct before calling
	 * us, so we need to zero out the TSB pointer or else tsb_grow()
	 * will be confused and think there is an older TSB to free up.
	 */
	mm->context.tsb = NULL;

	/* If this is fork, inherit the parent's TSB size.  We would
	 * grow it to that size on the first page fault anyways.
	 */
	tsb_grow(mm, get_mm_rss(mm));

	if (unlikely(!mm->context.tsb))
		return -ENOMEM;

	return 0;
}

void destroy_context(struct mm_struct *mm)
{
	unsigned long flags, cache_index;

	cache_index = (mm->context.tsb_reg_val & 0x7UL);
	kmem_cache_free(tsb_caches[cache_index], mm->context.tsb);

	/* We can remove these later, but for now it's useful
	 * to catch any bogus post-destroy_context() references
	 * to the TSB.
	 */
	mm->context.tsb = NULL;
	mm->context.tsb_reg_val = 0UL;

	spin_lock_irqsave(&ctx_alloc_lock, flags);

	if (CTX_VALID(mm->context)) {
		unsigned long nr = CTX_NRBITS(mm->context);
		mmu_context_bmap[nr>>6] &= ~(1UL << (nr & 63));
	}

	spin_unlock_irqrestore(&ctx_alloc_lock, flags);
}
