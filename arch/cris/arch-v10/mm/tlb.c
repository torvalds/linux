/*
 *  linux/arch/cris/arch-v10/mm/tlb.c
 *
 *  Low level TLB handling
 *
 *
 *  Copyright (C) 2000-2002  Axis Communications AB
 *  
 *  Authors:   Bjorn Wesen (bjornw@axis.com)
 *
 */

#include <asm/tlb.h>
#include <asm/mmu_context.h>
#include <asm/arch/svinto.h>

#define D(x)

/* The TLB can host up to 64 different mm contexts at the same time.
 * The running context is R_MMU_CONTEXT, and each TLB entry contains a
 * page_id that has to match to give a hit. In page_id_map, we keep track
 * of which mm's we have assigned which page_id's, so that we know when
 * to invalidate TLB entries.
 *
 * The last page_id is never running - it is used as an invalid page_id
 * so we can make TLB entries that will never match.
 *
 * Notice that we need to make the flushes atomic, otherwise an interrupt
 * handler that uses vmalloced memory might cause a TLB load in the middle
 * of a flush causing.
 */

/* invalidate all TLB entries */

void
flush_tlb_all(void)
{
	int i;
	unsigned long flags;

	/* the vpn of i & 0xf is so we dont write similar TLB entries
	 * in the same 4-way entry group. details.. 
	 */

	local_save_flags(flags);
	local_irq_disable();
	for(i = 0; i < NUM_TLB_ENTRIES; i++) {
		*R_TLB_SELECT = ( IO_FIELD(R_TLB_SELECT, index, i) );
		*R_TLB_HI = ( IO_FIELD(R_TLB_HI, page_id, INVALID_PAGEID ) |
			      IO_FIELD(R_TLB_HI, vpn,     i & 0xf ) );
		
		*R_TLB_LO = ( IO_STATE(R_TLB_LO, global,no       ) |
			      IO_STATE(R_TLB_LO, valid, no       ) |
			      IO_STATE(R_TLB_LO, kernel,no	 ) |
			      IO_STATE(R_TLB_LO, we,    no       ) |
			      IO_FIELD(R_TLB_LO, pfn,   0        ) );
	}
	local_irq_restore(flags);
	D(printk("tlb: flushed all\n"));
}

/* invalidate the selected mm context only */

void
flush_tlb_mm(struct mm_struct *mm)
{
	int i;
	int page_id = mm->context.page_id;
	unsigned long flags;

	D(printk("tlb: flush mm context %d (%p)\n", page_id, mm));

	if(page_id == NO_CONTEXT)
		return;
	
	/* mark the TLB entries that match the page_id as invalid.
	 * here we could also check the _PAGE_GLOBAL bit and NOT flush
	 * global pages. is it worth the extra I/O ? 
	 */

	local_save_flags(flags);
	local_irq_disable();
	for(i = 0; i < NUM_TLB_ENTRIES; i++) {
		*R_TLB_SELECT = IO_FIELD(R_TLB_SELECT, index, i);
		if (IO_EXTRACT(R_TLB_HI, page_id, *R_TLB_HI) == page_id) {
			*R_TLB_HI = ( IO_FIELD(R_TLB_HI, page_id, INVALID_PAGEID ) |
				      IO_FIELD(R_TLB_HI, vpn,     i & 0xf ) );
			
			*R_TLB_LO = ( IO_STATE(R_TLB_LO, global,no  ) |
				      IO_STATE(R_TLB_LO, valid, no  ) |
				      IO_STATE(R_TLB_LO, kernel,no  ) |
				      IO_STATE(R_TLB_LO, we,    no  ) |
				      IO_FIELD(R_TLB_LO, pfn,   0   ) );
		}
	}
	local_irq_restore(flags);
}

/* invalidate a single page */

void
flush_tlb_page(struct vm_area_struct *vma, 
	       unsigned long addr)
{
	struct mm_struct *mm = vma->vm_mm;
	int page_id = mm->context.page_id;
	int i;
	unsigned long flags;

	D(printk("tlb: flush page %p in context %d (%p)\n", addr, page_id, mm));

	if(page_id == NO_CONTEXT)
		return;

	addr &= PAGE_MASK; /* perhaps not necessary */

	/* invalidate those TLB entries that match both the mm context
	 * and the virtual address requested 
	 */

	local_save_flags(flags);
	local_irq_disable();
	for(i = 0; i < NUM_TLB_ENTRIES; i++) {
		unsigned long tlb_hi;
		*R_TLB_SELECT = IO_FIELD(R_TLB_SELECT, index, i);
		tlb_hi = *R_TLB_HI;
		if (IO_EXTRACT(R_TLB_HI, page_id, tlb_hi) == page_id &&
		    (tlb_hi & PAGE_MASK) == addr) {
			*R_TLB_HI = IO_FIELD(R_TLB_HI, page_id, INVALID_PAGEID ) |
				addr; /* same addr as before works. */
			
			*R_TLB_LO = ( IO_STATE(R_TLB_LO, global,no  ) |
				      IO_STATE(R_TLB_LO, valid, no  ) |
				      IO_STATE(R_TLB_LO, kernel,no  ) |
				      IO_STATE(R_TLB_LO, we,    no  ) |
				      IO_FIELD(R_TLB_LO, pfn,   0   ) );
		}
	}
	local_irq_restore(flags);
}

/* dump the entire TLB for debug purposes */

#if 0
void
dump_tlb_all(void)
{
	int i;
	unsigned long flags;
	
	printk("TLB dump. LO is: pfn | reserved | global | valid | kernel | we  |\n");

	local_save_flags(flags);
	local_irq_disable();
	for(i = 0; i < NUM_TLB_ENTRIES; i++) {
		*R_TLB_SELECT = ( IO_FIELD(R_TLB_SELECT, index, i) );
		printk("Entry %d: HI 0x%08lx, LO 0x%08lx\n",
		       i, *R_TLB_HI, *R_TLB_LO);
	}
	local_irq_restore(flags);
}
#endif

/*
 * Initialize the context related info for a new mm_struct
 * instance.
 */

int
init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	mm->context.page_id = NO_CONTEXT;
	return 0;
}

/* called in schedule() just before actually doing the switch_to */

void 
switch_mm(struct mm_struct *prev, struct mm_struct *next,
	  struct task_struct *tsk)
{
	/* make sure we have a context */

	get_mmu_context(next);

	/* remember the pgd for the fault handlers
	 * this is similar to the pgd register in some other CPU's.
	 * we need our own copy of it because current and active_mm
	 * might be invalid at points where we still need to derefer
	 * the pgd.
	 */

	per_cpu(current_pgd, smp_processor_id()) = next->pgd;

	/* switch context in the MMU */
	
	D(printk("switching mmu_context to %d (%p)\n", next->context, next));

	*R_MMU_CONTEXT = IO_FIELD(R_MMU_CONTEXT, page_id, next->context.page_id);
}

