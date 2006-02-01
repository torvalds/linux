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

/* We use an 8K TSB for the whole kernel, this allows to
 * handle about 4MB of modules and vmalloc mappings without
 * incurring many hash conflicts.
 */
#define KERNEL_TSB_SIZE_BYTES	8192
#define KERNEL_TSB_NENTRIES \
	(KERNEL_TSB_SIZE_BYTES / sizeof(struct tsb))

extern struct tsb swapper_tsb[KERNEL_TSB_NENTRIES];

static inline unsigned long tsb_hash(unsigned long vaddr, unsigned long nentries)
{
	vaddr >>= PAGE_SHIFT;
	return vaddr & (nentries - 1);
}

static inline int tag_compare(struct tsb *entry, unsigned long vaddr, unsigned long context)
{
	if (context == ~0UL)
		return 1;

	return (entry->tag == ((vaddr >> 22) | (context << 48)));
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

		if (tag_compare(ent, v, 0)) {
			ent->tag = 0UL;
			membar_storeload_storestore();
		}
	}
}

void flush_tsb_user(struct mmu_gather *mp)
{
	struct mm_struct *mm = mp->mm;
	struct tsb *tsb = mm->context.tsb;
	unsigned long ctx = ~0UL;
	unsigned long nentries = mm->context.tsb_nentries;
	int i;

	if (CTX_VALID(mm->context))
		ctx = CTX_HWBITS(mm->context);

	for (i = 0; i < mp->tlb_nr; i++) {
		unsigned long v = mp->vaddrs[i];
		struct tsb *ent;

		v &= ~0x1UL;

		ent = &tsb[tsb_hash(v, nentries)];
		if (tag_compare(ent, v, ctx)) {
			ent->tag = 0UL;
			membar_storeload_storestore();
		}
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
	};

	tsb_reg |= base;
	tsb_reg |= (tsb_paddr & (page_sz - 1UL));
	tte |= (tsb_paddr & ~(page_sz - 1UL));

	mm->context.tsb_reg_val = tsb_reg;
	mm->context.tsb_map_vaddr = base;
	mm->context.tsb_map_pte = tte;
}

int init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	unsigned long page = get_zeroed_page(GFP_KERNEL);

	mm->context.sparc64_ctx_val = 0UL;
	if (unlikely(!page))
		return -ENOMEM;

	mm->context.tsb = (struct tsb *) page;
	setup_tsb_params(mm, PAGE_SIZE);

	return 0;
}

void destroy_context(struct mm_struct *mm)
{
	free_page((unsigned long) mm->context.tsb);

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
