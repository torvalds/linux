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

extern struct tsb swapper_tsb[KERNEL_TSB_NENTRIES];

static inline unsigned long tsb_hash(unsigned long vaddr, unsigned long nentries)
{
	vaddr >>= PAGE_SHIFT;
	return vaddr & (nentries - 1);
}

static inline int tag_compare(unsigned long tag, unsigned long vaddr, unsigned long context)
{
	return (tag == ((vaddr >> 22) | (context << 48)));
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

		if (tag_compare(ent->tag, v, 0)) {
			ent->tag = 0UL;
			membar_storeload_storestore();
		}
	}
}

void flush_tsb_user(struct mmu_gather *mp)
{
	struct mm_struct *mm = mp->mm;
	struct tsb *tsb = mm->context.tsb;
	unsigned long nentries = mm->context.tsb_nentries;
	unsigned long ctx, base;
	int i;

	if (unlikely(!CTX_VALID(mm->context)))
		return;

	ctx = CTX_HWBITS(mm->context);

	if (tlb_type == cheetah_plus)
		base = __pa(tsb);
	else
		base = (unsigned long) tsb;
	
	for (i = 0; i < mp->tlb_nr; i++) {
		unsigned long v = mp->vaddrs[i];
		unsigned long tag, ent, hash;

		v &= ~0x1UL;

		hash = tsb_hash(v, nentries);
		ent = base + (hash * sizeof(struct tsb));
		tag = (v >> 22UL) | (ctx << 48UL);

		tsb_flush(ent, tag);
	}
}

static void setup_tsb_params(struct mm_struct *mm, unsigned long tsb_bytes)
{
	unsigned long tsb_reg, base, tsb_paddr;
	unsigned long page_sz, tte;

	mm->context.tsb_nentries = tsb_bytes / sizeof(struct tsb);

	base = TSBMAP_BASE;
	tte = (_PAGE_VALID | _PAGE_L | _PAGE_CP |
	       _PAGE_CV    | _PAGE_P | _PAGE_W);
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
		tte |= _PAGE_SZ8K;
		page_sz = 8192;
		break;

	case 8192 << 1:
		tsb_reg = 0x1UL;
		tte |= _PAGE_SZ64K;
		page_sz = 64 * 1024;
		break;

	case 8192 << 2:
		tsb_reg = 0x2UL;
		tte |= _PAGE_SZ64K;
		page_sz = 64 * 1024;
		break;

	case 8192 << 3:
		tsb_reg = 0x3UL;
		tte |= _PAGE_SZ64K;
		page_sz = 64 * 1024;
		break;

	case 8192 << 4:
		tsb_reg = 0x4UL;
		tte |= _PAGE_SZ512K;
		page_sz = 512 * 1024;
		break;

	case 8192 << 5:
		tsb_reg = 0x5UL;
		tte |= _PAGE_SZ512K;
		page_sz = 512 * 1024;
		break;

	case 8192 << 6:
		tsb_reg = 0x6UL;
		tte |= _PAGE_SZ512K;
		page_sz = 512 * 1024;
		break;

	case 8192 << 7:
		tsb_reg = 0x7UL;
		tte |= _PAGE_SZ4MB;
		page_sz = 4 * 1024 * 1024;
		break;

	default:
		BUG();
	};

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

/* The page tables are locked against modifications while this
 * runs.
 *
 * XXX do some prefetching...
 */
static void copy_tsb(struct tsb *old_tsb, unsigned long old_size,
		     struct tsb *new_tsb, unsigned long new_size)
{
	unsigned long old_nentries = old_size / sizeof(struct tsb);
	unsigned long new_nentries = new_size / sizeof(struct tsb);
	unsigned long i;

	for (i = 0; i < old_nentries; i++) {
		register unsigned long tag asm("o4");
		register unsigned long pte asm("o5");
		unsigned long v, hash;

		if (tlb_type == cheetah_plus) {
			__asm__ __volatile__(
				"ldda [%2] %3, %0"
				: "=r" (tag), "=r" (pte)
				: "r" (__pa(&old_tsb[i])),
				  "i" (ASI_QUAD_LDD_PHYS));
		} else {
			__asm__ __volatile__(
				"ldda [%2] %3, %0"
				: "=r" (tag), "=r" (pte)
				: "r" (&old_tsb[i]),
				  "i" (ASI_NUCLEUS_QUAD_LDD));
		}

		if (!tag || (tag & (1UL << TSB_TAG_LOCK_BIT)))
			continue;

		/* We only put base page size PTEs into the TSB,
		 * but that might change in the future.  This code
		 * would need to be changed if we start putting larger
		 * page size PTEs into there.
		 */
		WARN_ON((pte & _PAGE_ALL_SZ_BITS) != _PAGE_SZBITS);

		/* The tag holds bits 22 to 63 of the virtual address
		 * and the context.  Clear out the context, and shift
		 * up to make a virtual address.
		 */
		v = (tag & ((1UL << 42UL) - 1UL)) << 22UL;

		/* The implied bits of the tag (bits 13 to 21) are
		 * determined by the TSB entry index, so fill that in.
		 */
		v |= (i & (512UL - 1UL)) << 13UL;

		hash = tsb_hash(v, new_nentries);
		if (tlb_type == cheetah_plus) {
			__asm__ __volatile__(
				"stxa	%0, [%1] %2\n\t"
				"stxa	%3, [%4] %2"
				: /* no outputs */
				: "r" (tag),
				  "r" (__pa(&new_tsb[hash].tag)),
				  "i" (ASI_PHYS_USE_EC),
				  "r" (pte),
				  "r" (__pa(&new_tsb[hash].pte)));
		} else {
			new_tsb[hash].tag = tag;
			new_tsb[hash].pte = pte;
		}
	}
}

/* When the RSS of an address space exceeds mm->context.tsb_rss_limit,
 * update_mmu_cache() invokes this routine to try and grow the TSB.
 * When we reach the maximum TSB size supported, we stick ~0UL into
 * mm->context.tsb_rss_limit so the grow checks in update_mmu_cache()
 * will not trigger any longer.
 *
 * The TSB can be anywhere from 8K to 1MB in size, in increasing powers
 * of two.  The TSB must be aligned to it's size, so f.e. a 512K TSB
 * must be 512K aligned.
 *
 * The idea here is to grow the TSB when the RSS of the process approaches
 * the number of entries that the current TSB can hold at once.  Currently,
 * we trigger when the RSS hits 3/4 of the TSB capacity.
 */
void tsb_grow(struct mm_struct *mm, unsigned long rss, gfp_t gfp_flags)
{
	unsigned long max_tsb_size = 1 * 1024 * 1024;
	unsigned long size, old_size;
	struct page *page;
	struct tsb *old_tsb;

	if (max_tsb_size > (PAGE_SIZE << MAX_ORDER))
		max_tsb_size = (PAGE_SIZE << MAX_ORDER);

	for (size = PAGE_SIZE; size < max_tsb_size; size <<= 1UL) {
		unsigned long n_entries = size / sizeof(struct tsb);

		n_entries = (n_entries * 3) / 4;
		if (n_entries > rss)
			break;
	}

	page = alloc_pages(gfp_flags | __GFP_ZERO, get_order(size));
	if (unlikely(!page))
		return;

	if (size == max_tsb_size)
		mm->context.tsb_rss_limit = ~0UL;
	else
		mm->context.tsb_rss_limit =
			((size / sizeof(struct tsb)) * 3) / 4;

	old_tsb = mm->context.tsb;
	old_size = mm->context.tsb_nentries * sizeof(struct tsb);

	if (old_tsb)
		copy_tsb(old_tsb, old_size, page_address(page), size);

	mm->context.tsb = page_address(page);
	setup_tsb_params(mm, size);

	/* If old_tsb is NULL, we're being invoked for the first time
	 * from init_new_context().
	 */
	if (old_tsb) {
		/* Now force all other processors to reload the new
		 * TSB state.
		 */
		smp_tsb_sync(mm);

		/* Finally reload it on the local cpu.  No further
		 * references will remain to the old TSB and we can
		 * thus free it up.
		 */
		tsb_context_switch(mm);

		free_pages((unsigned long) old_tsb, get_order(old_size));
	}
}

int init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{

	mm->context.sparc64_ctx_val = 0UL;

	/* copy_mm() copies over the parent's mm_struct before calling
	 * us, so we need to zero out the TSB pointer or else tsb_grow()
	 * will be confused and think there is an older TSB to free up.
	 */
	mm->context.tsb = NULL;
	tsb_grow(mm, 0, GFP_KERNEL);

	if (unlikely(!mm->context.tsb))
		return -ENOMEM;

	return 0;
}

void destroy_context(struct mm_struct *mm)
{
	unsigned long size = mm->context.tsb_nentries * sizeof(struct tsb);

	free_pages((unsigned long) mm->context.tsb, get_order(size));

	/* We can remove these later, but for now it's useful
	 * to catch any bogus post-destroy_context() references
	 * to the TSB.
	 */
	mm->context.tsb = NULL;
	mm->context.tsb_reg_val = 0UL;

	spin_lock(&ctx_alloc_lock);

	if (CTX_VALID(mm->context)) {
		unsigned long nr = CTX_NRBITS(mm->context);
		mmu_context_bmap[nr>>6] &= ~(1UL << (nr & 63));
	}

	spin_unlock(&ctx_alloc_lock);
}
