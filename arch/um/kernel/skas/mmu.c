// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015 Thomas Meyer (thomas@m3y3r.de)
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>

#include <asm/pgalloc.h>
#include <asm/sections.h>
#include <asm/mmu_context.h>
#include <as-layout.h>
#include <os.h>
#include <skas.h>
#include <stub-data.h>

/* Ensure the stub_data struct covers the allocated area */
static_assert(sizeof(struct stub_data) == STUB_DATA_PAGES * UM_KERN_PAGE_SIZE);

int init_new_context(struct task_struct *task, struct mm_struct *mm)
{
	struct mm_id *new_id = &mm->context.id;
	unsigned long stack = 0;
	int ret = -ENOMEM;

	stack = __get_free_pages(GFP_KERNEL | __GFP_ZERO, ilog2(STUB_DATA_PAGES));
	if (stack == 0)
		goto out;

	new_id->stack = stack;

	block_signals_trace();
	new_id->pid = start_userspace(stack);
	unblock_signals_trace();

	if (new_id->pid < 0) {
		ret = new_id->pid;
		goto out_free;
	}

	/*
	 * Ensure the new MM is clean and nothing unwanted is mapped.
	 *
	 * TODO: We should clear the memory up to STUB_START to ensure there is
	 * nothing mapped there, i.e. we (currently) have:
	 *
	 * |- user memory -|- unused        -|- stub        -|- unused    -|
	 *                 ^ TASK_SIZE      ^ STUB_START
	 *
	 * Meaning we have two unused areas where we may still have valid
	 * mappings from our internal clone(). That isn't really a problem as
	 * userspace is not going to access them, but it is definitely not
	 * correct.
	 *
	 * However, we are "lucky" and if rseq is configured, then on 32 bit
	 * it will fall into the first empty range while on 64 bit it is going
	 * to use an anonymous mapping in the second range. As such, things
	 * continue to work for now as long as we don't start unmapping these
	 * areas.
	 *
	 * Change this to STUB_START once we have a clean userspace.
	 */
	unmap(new_id, 0, TASK_SIZE);

	return 0;

 out_free:
	if (new_id->stack != 0)
		free_pages(new_id->stack, ilog2(STUB_DATA_PAGES));
 out:
	return ret;
}

void destroy_context(struct mm_struct *mm)
{
	struct mm_context *mmu = &mm->context;

	/*
	 * If init_new_context wasn't called, this will be
	 * zero, resulting in a kill(0), which will result in the
	 * whole UML suddenly dying.  Also, cover negative and
	 * 1 cases, since they shouldn't happen either.
	 */
	if (mmu->id.pid < 2) {
		printk(KERN_ERR "corrupt mm_context - pid = %d\n",
		       mmu->id.pid);
		return;
	}
	os_kill_ptraced_process(mmu->id.pid, 1);

	free_pages(mmu->id.stack, ilog2(STUB_DATA_PAGES));
}
