/*
 * This file contains the routines for handling the MMU on those
 * PowerPC implementations where the MMU substantially follows the
 * architecture specification.  This includes the 6xx, 7xx, 7xxx,
 * and 8260 implementations but excludes the 8xx and 4xx.
 *  -- paulus
 *
 *  Derived from arch/ppc/mm/init.c:
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/export.h>

#include <asm/mmu_context.h>
#include <asm/tlbflush.h>

/*
 * On 32-bit PowerPC 6xx/7xx/7xxx CPUs, we use a set of 16 VSIDs
 * (virtual segment identifiers) for each context.  Although the
 * hardware supports 24-bit VSIDs, and thus >1 million contexts,
 * we only use 32,768 of them.  That is ample, since there can be
 * at most around 30,000 tasks in the system anyway, and it means
 * that we can use a bitmap to indicate which contexts are in use.
 * Using a bitmap means that we entirely avoid all of the problems
 * that we used to have when the context number overflowed,
 * particularly on SMP systems.
 *  -- paulus.
 */
#define NO_CONTEXT      	((unsigned long) -1)
#define LAST_CONTEXT    	32767
#define FIRST_CONTEXT    	1

/*
 * This function defines the mapping from contexts to VSIDs (virtual
 * segment IDs).  We use a skew on both the context and the high 4 bits
 * of the 32-bit virtual address (the "effective segment ID") in order
 * to spread out the entries in the MMU hash table.  Note, if this
 * function is changed then arch/ppc/mm/hashtable.S will have to be
 * changed to correspond.
 *
 *
 * CTX_TO_VSID(ctx, va)	(((ctx) * (897 * 16) + ((va) >> 28) * 0x111) \
 *				 & 0xffffff)
 */

static unsigned long next_mmu_context;
static unsigned long context_map[LAST_CONTEXT / BITS_PER_LONG + 1];

unsigned long __init_new_context(void)
{
	unsigned long ctx = next_mmu_context;

	while (test_and_set_bit(ctx, context_map)) {
		ctx = find_next_zero_bit(context_map, LAST_CONTEXT+1, ctx);
		if (ctx > LAST_CONTEXT)
			ctx = 0;
	}
	next_mmu_context = (ctx + 1) & LAST_CONTEXT;

	return ctx;
}
EXPORT_SYMBOL_GPL(__init_new_context);

/*
 * Set up the context for a new address space.
 */
int init_new_context(struct task_struct *t, struct mm_struct *mm)
{
	mm->context.id = __init_new_context();

	return 0;
}

/*
 * Free a context ID. Make sure to call this with preempt disabled!
 */
void __destroy_context(unsigned long ctx)
{
	clear_bit(ctx, context_map);
}
EXPORT_SYMBOL_GPL(__destroy_context);

/*
 * We're finished using the context for an address space.
 */
void destroy_context(struct mm_struct *mm)
{
	preempt_disable();
	if (mm->context.id != NO_CONTEXT) {
		__destroy_context(mm->context.id);
		mm->context.id = NO_CONTEXT;
	}
	preempt_enable();
}

/*
 * Initialize the context management stuff.
 */
void __init mmu_context_init(void)
{
	/* Reserve context 0 for kernel use */
	context_map[0] = (1 << FIRST_CONTEXT) - 1;
	next_mmu_context = FIRST_CONTEXT;
}
