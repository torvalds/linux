/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_STACKTRACE_H
#define __ASM_STACKTRACE_H

#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/types.h>
#include <linux/llist.h>

#include <asm/memory.h>
#include <asm/ptrace.h>
#include <asm/sdei.h>

enum stack_type {
	STACK_TYPE_UNKNOWN,
	STACK_TYPE_TASK,
	STACK_TYPE_IRQ,
	STACK_TYPE_OVERFLOW,
	STACK_TYPE_SDEI_NORMAL,
	STACK_TYPE_SDEI_CRITICAL,
	__NR_STACK_TYPES
};

struct stack_info {
	unsigned long low;
	unsigned long high;
	enum stack_type type;
};

extern void dump_backtrace(struct pt_regs *regs, struct task_struct *tsk,
			   const char *loglvl);

DECLARE_PER_CPU(unsigned long *, irq_stack_ptr);

static inline bool on_stack(unsigned long sp, unsigned long size,
			    unsigned long low, unsigned long high,
			    enum stack_type type, struct stack_info *info)
{
	if (!low)
		return false;

	if (sp < low || sp + size < sp || sp + size > high)
		return false;

	if (info) {
		info->low = low;
		info->high = high;
		info->type = type;
	}
	return true;
}

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


/*
 * We can only safely access per-cpu stacks from current in a non-preemptible
 * context.
 */
static inline bool on_accessible_stack(const struct task_struct *tsk,
				       unsigned long sp, unsigned long size,
				       struct stack_info *info)
{
	if (info)
		info->type = STACK_TYPE_UNKNOWN;

	if (on_task_stack(tsk, sp, size, info))
		return true;
	if (tsk != current || preemptible())
		return false;
	if (on_irq_stack(sp, size, info))
		return true;
	if (on_overflow_stack(sp, size, info))
		return true;
	if (on_sdei_stack(sp, size, info))
		return true;

	return false;
}

#endif	/* __ASM_STACKTRACE_H */
