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

#define TSB_ENTRY_ALIGNMENT	16

struct tsb {
	unsigned long tag;
	unsigned long pte;
} __attribute__((aligned(TSB_ENTRY_ALIGNMENT)));

/* We use an 8K TSB for the whole kernel, this allows to
 * handle about 4MB of modules and vmalloc mappings without
 * incurring many hash conflicts.
 */
#define KERNEL_TSB_SIZE_BYTES	8192
#define KERNEL_TSB_NENTRIES \
	(KERNEL_TSB_SIZE_BYTES / sizeof(struct tsb))

extern struct tsb swapper_tsb[KERNEL_TSB_NENTRIES];

static inline unsigned long tsb_hash(unsigned long vaddr)
{
	vaddr >>= PAGE_SHIFT;
	return vaddr & (KERNEL_TSB_NENTRIES - 1);
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
		struct tsb *ent = &swapper_tsb[tsb_hash(v)];

		if (tag_compare(ent, v, 0)) {
			ent->tag = 0UL;
			membar_storeload_storestore();
		}
	}
}

void flush_tsb_user(struct mmu_gather *mp)
{
	struct mm_struct *mm = mp->mm;
	struct tsb *tsb = (struct tsb *) mm->context.sparc64_tsb;
	unsigned long ctx = ~0UL;
	int i;

	if (CTX_VALID(mm->context))
		ctx = CTX_HWBITS(mm->context);

	for (i = 0; i < mp->tlb_nr; i++) {
		unsigned long v = mp->vaddrs[i];
		struct tsb *ent;

		v &= ~0x1UL;

		ent = &tsb[tsb_hash(v)];
		if (tag_compare(ent, v, ctx)) {
			ent->tag = 0UL;
			membar_storeload_storestore();
		}
	}
}

int init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	unsigned long page = get_zeroed_page(GFP_KERNEL);

	mm->context.sparc64_ctx_val = 0UL;
	if (unlikely(!page))
		return -ENOMEM;

	mm->context.sparc64_tsb = (unsigned long *) page;

	return 0;
}

void destroy_context(struct mm_struct *mm)
{
	free_page((unsigned long) mm->context.sparc64_tsb);

	spin_lock(&ctx_alloc_lock);

	if (CTX_VALID(mm->context)) {
		unsigned long nr = CTX_NRBITS(mm->context);
		mmu_context_bmap[nr>>6] &= ~(1UL << (nr & 63));
	}

	spin_unlock(&ctx_alloc_lock);
}
