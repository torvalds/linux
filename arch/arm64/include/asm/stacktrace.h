/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_STACKTRACE_H
#define __ASM_STACKTRACE_H

#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/llist.h>

#include <asm/memory.h>
#include <asm/pointer_auth.h>
#include <asm/ptrace.h>
#include <asm/sdei.h>

#include <asm/stacktrace/common.h>

extern void dump_backtrace(struct pt_regs *regs, struct task_struct *tsk,
			   const char *loglvl);

DECLARE_PER_CPU(unsigned long *, irq_stack_ptr);

static inline bool on_irq_stack(unsigned long sp, unsigned long size,
				struct stack_info *info)
{
	unsigned long low = (unsigned long)raw_cpu_read(irq_stack_ptr);
	unsigned long high = low + IRQ_STACK_SIZE;

	return on_stack(sp, size, low, high, STACK_TYPE_IRQ, info);
}

static inline bool on_task_stack(const struct task_struct *tsk,
				 unsigned long sp, unsigned long size,
				 struct stack_info *info)
{
	unsigned long low = (unsigned long)task_stack_page(tsk);
	unsigned long high = low + THREAD_SIZE;

	return on_stack(sp, size, low, high, STACK_TYPE_TASK, info);
}

#ifdef CONFIG_VMAP_STACK
DECLARE_PER_CPU(unsigned long [OVERFLOW_STACK_SIZE/sizeof(long)], overflow_stack);

static inline bool on_overflow_stack(unsigned long sp, unsigned long size,
				struct stack_info *info)
{
	unsigned long low = (unsigned long)raw_cpu_ptr(overflow_stack);
	unsigned long high = low + OVERFLOW_STACK_SIZE;

	return on_stack(sp, size, low, high, STACK_TYPE_OVERFLOW, info);
}
#else
static inline bool on_overflow_stack(unsigned long sp, unsigned long size,
			struct stack_info *info) { return false; }
#endif

#endif	/* __ASM_STACKTRACE_H */
