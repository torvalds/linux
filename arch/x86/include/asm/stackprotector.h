/* SPDX-License-Identifier: GPL-2.0 */
/*
 * GCC stack protector support.
 *
 * Stack protector works by putting predefined pattern at the start of
 * the stack frame and verifying that it hasn't been overwritten when
 * returning from the function.  The pattern is called stack canary
 * and unfortunately gcc historically required it to be at a fixed offset
 * from the percpu segment base.  On x86_64, the offset is 40 bytes.
 *
 * The same segment is shared by percpu area and stack canary.  On
 * x86_64, percpu symbols are zero based and %gs (64-bit) points to the
 * base of percpu area.  The first occupant of the percpu area is always
 * fixed_percpu_data which contains stack_canary at the approproate
 * offset.  On x86_32, the stack canary is just a regular percpu
 * variable.
 *
 * Putting percpu data in %fs on 32-bit is a minor optimization compared to
 * using %gs.  Since 32-bit userspace normally has %fs == 0, we are likely
 * to load 0 into %fs on exit to usermode, whereas with percpu data in
 * %gs, we are likely to load a non-null %gs on return to user mode.
 *
 * Once we are willing to require GCC 8.1 or better for 64-bit stackprotector
 * support, we can remove some of this complexity.
 */

#ifndef _ASM_STACKPROTECTOR_H
#define _ASM_STACKPROTECTOR_H 1

#ifdef CONFIG_STACKPROTECTOR

#include <asm/tsc.h>
#include <asm/processor.h>
#include <asm/percpu.h>
#include <asm/desc.h>

#include <linux/random.h>
#include <linux/sched.h>

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
	u64 canary;
	u64 tsc;

#ifdef CONFIG_X86_64
	BUILD_BUG_ON(offsetof(struct fixed_percpu_data, stack_canary) != 40);
#endif
	/*
	 * We both use the random pool and the current TSC as a source
	 * of randomness. The TSC only matters for very early init,
	 * there it already has some randomness on most systems. Later
	 * on during the bootup the random pool has true entropy too.
	 */
	get_random_bytes(&canary, sizeof(canary));
	tsc = rdtsc();
	canary += tsc + (tsc << 32UL);
	canary &= CANARY_MASK;

	current->stack_canary = canary;
#ifdef CONFIG_X86_64
	this_cpu_write(fixed_percpu_data.stack_canary, canary);
#else
	this_cpu_write(__stack_chk_guard, canary);
#endif
}

static inline void cpu_init_stack_canary(int cpu, struct task_struct *idle)
{
#ifdef CONFIG_X86_64
	per_cpu(fixed_percpu_data.stack_canary, cpu) = idle->stack_canary;
#else
	per_cpu(__stack_chk_guard, cpu) = idle->stack_canary;
#endif
}

#else	/* STACKPROTECTOR */

/* dummy boot_init_stack_canary() is defined in linux/stackprotector.h */

static inline void cpu_init_stack_canary(int cpu, struct task_struct *idle)
{ }

#endif	/* STACKPROTECTOR */
#endif	/* _ASM_STACKPROTECTOR_H */
