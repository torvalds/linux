/* SPDX-License-Identifier: GPL-2.0 */
/*
 * GCC stack protector support.
 *
 * Stack protector works by putting a predefined pattern at the start of
 * the stack frame and verifying that it hasn't been overwritten when
 * returning from the function.  The pattern is called the stack canary
 * and is a unique value for each task.
 */

#ifndef _ASM_STACKPROTECTOR_H
#define _ASM_STACKPROTECTOR_H 1

#ifdef CONFIG_STACKPROTECTOR

#include <asm/tsc.h>
#include <asm/processor.h>
#include <asm/percpu.h>
#include <asm/desc.h>

#include <linux/sched.h>

DECLARE_PER_CPU_CACHE_HOT(unsigned long, __stack_chk_guard);

/*
 * Initialize the stackprotector canary value.
 *
 * NOTE: this must only be called from functions that never return
 * and it must always be inlined.
 *
 * In addition, it should be called from a compilation unit for which
 * stack protector is disabled. Alternatively, the caller should not end
 * with a function call which gets tail-call optimized as that would
 * lead to checking a modified canary value.
 */
static __always_inline void boot_init_stack_canary(void)
{
	unsigned long canary = get_random_canary();

	current->stack_canary = canary;
	this_cpu_write(__stack_chk_guard, canary);
}

static inline void cpu_init_stack_canary(int cpu, struct task_struct *idle)
{
	per_cpu(__stack_chk_guard, cpu) = idle->stack_canary;
}

#else	/* STACKPROTECTOR */

/* dummy boot_init_stack_canary() is defined in linux/stackprotector.h */

static inline void cpu_init_stack_canary(int cpu, struct task_struct *idle)
{ }

#endif	/* STACKPROTECTOR */
#endif	/* _ASM_STACKPROTECTOR_H */
