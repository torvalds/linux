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

int init_new_context(struct task_struct *task, struct mm_struct *mm)
{
 	struct mm_context *from_mm = NULL;
	struct mm_context *to_mm = &mm->context;
	unsigned long stack = 0;
	int ret = -ENOMEM;

	stack = __get_free_pages(GFP_KERNEL | __GFP_ZERO, ilog2(STUB_DATA_PAGES));
	if (stack == 0)
		goto out;

	to_mm->id.stack = stack;
	if (current->mm != NULL && current->mm != &init_mm)
		from_mm = &current->mm->context;

	block_signals_trace();
	if (from_mm)
		to_mm->id.u.pid = copy_context_skas0(stack,
						     from_mm->id.u.pid);
	else to_mm->id.u.pid = start_userspace(stack);
	unblock_signals_trace();

	if (to_mm->id.u.pid < 0) {
		ret = to_mm->id.u.pid;
		goto out_free;
	}

	ret = init_new_ldt(to_mm, from_mm);
	if (ret < 0) {
		printk(KERN_ERR "init_new_context_skas - init_ldt"
		       " failed, errno = %d\n", ret);
		goto out_free;
	}

	return 0;

 out_free:
	if (to_mm->id.stack != 0)
		free_pages(to_mm->id.stack, ilog2(STUB_DATA_PAGES));
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
	if (mmu->id.u.pid < 2) {
		printk(KERN_ERR "corrupt mm_context - pid = %d\n",
		       mmu->id.u.pid);
		return;
	}
	os_kill_ptraced_process(mmu->id.u.pid, 1);

	free_pages(mmu->id.stack, ilog2(STUB_DATA_PAGES));
	free_ldt(mmu);
}
