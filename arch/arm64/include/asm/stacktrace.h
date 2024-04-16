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

static inline struct stack_info stackinfo_get_irq(void)
{
	unsigned long low = (unsigned long)raw_cpu_read(irq_stack_ptr);
	unsigned long high = low + IRQ_STACK_SIZE;

	return (struct stack_info) {
		.low = low,
		.high = high,
	};
}

static inline bool on_irq_stack(unsigned long sp, unsigned long size)
{
	struct stack_info info = stackinfo_get_irq();
	return stackinfo_on_stack(&info, sp, size);
}

static inline struct stack_info stackinfo_get_task(const struct task_struct *tsk)
{
	unsigned long low = (unsigned long)task_stack_page(tsk);
	unsigned long high = low + THREAD_SIZE;

	return (struct stack_info) {
		.low = low,
		.high = high,
	};
}

static inline bool on_task_stack(const struct task_struct *tsk,
				 unsigned long sp, unsigned long size)
{
	struct stack_info info = stackinfo_get_task(tsk);
	return stackinfo_on_stack(&info, sp, size);
}

#ifdef CONFIG_VMAP_STACK
DECLARE_PER_CPU(unsigned long [OVERFLOW_STACK_SIZE/sizeof(long)], overflow_stack);

static inline struct stack_info stackinfo_get_overflow(void)
{
	unsigned long low = (unsigned long)raw_cpu_ptr(overflow_stack);
	unsigned long high = low + OVERFLOW_STACK_SIZE;

	return (struct stack_info) {
		.low = low,
		.high = high,
	};
}
#else
#define stackinfo_get_overflow()	stackinfo_get_unknown()
#endif

#if defined(CONFIG_ARM_SDE_INTERFACE) && defined(CONFIG_VMAP_STACK)
DECLARE_PER_CPU(unsigned long *, sdei_stack_normal_ptr);
DECLARE_PER_CPU(unsigned long *, sdei_stack_critical_ptr);

static inline struct stack_info stackinfo_get_sdei_normal(void)
{
	unsigned long low = (unsigned long)raw_cpu_read(sdei_stack_normal_ptr);
	unsigned long high = low + SDEI_STACK_SIZE;

	return (struct stack_info) {
		.low = low,
		.high = high,
	};
}

static inline struct stack_info stackinfo_get_sdei_critical(void)
{
	unsigned long low = (unsigned long)raw_cpu_read(sdei_stack_critical_ptr);
	unsigned long high = low + SDEI_STACK_SIZE;

	return (struct stack_info) {
		.low = low,
		.high = high,
	};
}
#else
#define stackinfo_get_sdei_normal()	stackinfo_get_unknown()
#define stackinfo_get_sdei_critical()	stackinfo_get_unknown()
#endif

#ifdef CONFIG_EFI
extern u64 *efi_rt_stack_top;

static inline struct stack_info stackinfo_get_efi(void)
{
	unsigned long high = (u64)efi_rt_stack_top;
	unsigned long low = high - THREAD_SIZE;

	return (struct stack_info) {
		.low = low,
		.high = high,
	};
}
#endif

#endif	/* __ASM_STACKTRACE_H */
