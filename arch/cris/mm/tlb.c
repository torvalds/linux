/*
 *  linux/arch/cris/mm/tlb.c
 *
 *  Copyright (C) 2000, 2001  Axis Communications AB
 *  
 *  Authors:   Bjorn Wesen (bjornw@axis.com)
 *
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/tlb.h>

#define D(x)

/* The TLB can host up to 64 different mm contexts at the same time.
 * The running context is R_MMU_CONTEXT, and each TLB entry contains a
 * page_id that has to match to give a hit. In page_id_map, we keep track
 * of which mm we have assigned to which page_id, so that we know when
 * to invalidate TLB entries.
 *
 * The last page_id is never running - it is used as an invalid page_id
 * so we can make TLB entries that will never match.
 *
 * Notice that we need to make the flushes atomic, otherwise an interrupt
 * handler that uses vmalloced memory might cause a TLB load in the middle
 * of a flush causing.
 */

struct mm_struct *page_id_map[NUM_PAGEID];
static int map_replace_ptr = 1;  /* which page_id_map entry to replace next */

/* the following functions are similar to those used in the PPC port */

static inline void
alloc_context(struct mm_struct *mm)
{
	struct mm_struct *old_mm;

	D(printk("tlb: alloc context %d (%p)\n", map_replace_ptr, mm));

	/* did we replace an mm ? */

	old_mm = page_id_map[map_replace_ptr];

	if(old_mm) {
		/* throw out any TLB entries belonging to the mm we replace
		 * in the map
		 */
		flush_tlb_mm(old_mm);

		old_mm->context.page_id = NO_CONTEXT;
	}

	/* insert it into the page_id_map */

	mm->context.page_id = map_replace_ptr;
	page_id_map[map_replace_ptr] = mm;

	map_replace_ptr++;

	if(map_replace_ptr == INVALID_PAGEID)
		map_replace_ptr = 0;         /* wrap around */	
}

/* 
 * if needed, get a new MMU context for the mm. otherwise nothing is done.
 */

void
get_mmu_context(struct mm_struct *mm)
{
	if(mm->context.page_id == NO_CONTEXT)
		alloc_context(mm);
}

/* called by __exit_mm to destroy the used MMU context if any before
 * destroying the mm itself. this is only called when the last user of the mm
 * drops it.
 *
 * the only thing we really need to do here is mark the used PID slot
 * as empty.
 */

void
destroy_context(struct mm_struct *mm)
{
	if(mm->context.page_id != NO_CONTEXT) {
		D(printk("destroy_context %d (%p)\n", mm->context.page_id, mm));
		flush_tlb_mm(mm);  /* TODO this might be redundant ? */
		page_id_map[mm->context.page_id] = NULL;
	}
}

/* called once during VM initialization, from init.c */

void __init
tlb_init(void)
{
	int i;

	/* clear the page_id map */

	for (i = 1; i < ARRAY_SIZE(page_id_map); i++)
		page_id_map[i] = NULL;
	
	/* invalidate the entire TLB */

	flush_tlb_all();

	/* the init_mm has context 0 from the boot */

	page_id_map[0] = &init_mm;
}
