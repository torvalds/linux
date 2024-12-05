// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <linux/init.h>
#include <linux/sched/mm.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/task.h>

#include <asm/tlbflush.h>

#include <as-layout.h>
#include <kern.h>
#include <os.h>
#include <skas.h>
#include <kern_util.h>

extern void start_kernel(void);

static int __init start_kernel_proc(void *unused)
{
	block_signals_trace();

	start_kernel();
	return 0;
}

extern int userspace_pid[];

static char cpu0_irqstack[THREAD_SIZE] __aligned(THREAD_SIZE);

int __init start_uml(void)
{
	stack_protections((unsigned long) &cpu0_irqstack);
	set_sigstack(cpu0_irqstack, THREAD_SIZE);

	init_new_thread_signals();

	init_task.thread.request.thread.proc = start_kernel_proc;
	init_task.thread.request.thread.arg = NULL;
	return start_idle_thread(task_stack_page(&init_task),
				 &init_task.thread.switch_buf);
}

unsigned long current_stub_stack(void)
{
	if (current->mm == NULL)
		return 0;

	return current->mm->context.id.stack;
}

struct mm_id *current_mm_id(void)
{
	if (current->mm == NULL)
		return NULL;

	return &current->mm->context.id;
}

void current_mm_sync(void)
{
	if (current->mm == NULL)
		return;

	um_tlb_sync(current->mm);
}
