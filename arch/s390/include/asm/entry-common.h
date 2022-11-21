/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_S390_ENTRY_COMMON_H
#define ARCH_S390_ENTRY_COMMON_H

#include <linux/sched.h>
#include <linux/audit.h>
#include <linux/randomize_kstack.h>
#include <linux/processor.h>
#include <linux/uaccess.h>
#include <asm/timex.h>
#include <asm/fpu/api.h>

#define ARCH_EXIT_TO_USER_MODE_WORK (_TIF_GUARDED_STORAGE | _TIF_PER_TRAP)

void do_per_trap(struct pt_regs *regs);

#ifdef CONFIG_DEBUG_ENTRY
static __always_inline void arch_check_user_regs(struct pt_regs *regs)
{
	debug_user_asce(0);
}

#define arch_check_user_regs arch_check_user_regs
#endif /* CONFIG_DEBUG_ENTRY */

static __always_inline void arch_exit_to_user_mode_work(struct pt_regs *regs,
							unsigned long ti_work)
{
	if (ti_work & _TIF_PER_TRAP) {
		clear_thread_flag(TIF_PER_TRAP);
		do_per_trap(regs);
	}

	if (ti_work & _TIF_GUARDED_STORAGE)
		gs_load_bc_cb(regs);
}

#define arch_exit_to_user_mode_work arch_exit_to_user_mode_work

static __always_inline void arch_exit_to_user_mode(void)
{
	if (test_cpu_flag(CIF_FPU))
		__load_fpu_regs();

	if (IS_ENABLED(CONFIG_DEBUG_ENTRY))
		debug_user_asce(1);
}

#define arch_exit_to_user_mode arch_exit_to_user_mode

static inline void arch_exit_to_user_mode_prepare(struct pt_regs *regs,
						  unsigned long ti_work)
{
	choose_random_kstack_offset(get_tod_clock_fast() & 0xff);
}

#define arch_exit_to_user_mode_prepare arch_exit_to_user_mode_prepare

static inline bool on_thread_stack(void)
{
	return !(((unsigned long)(current->stack) ^ current_stack_pointer) & ~(THREAD_SIZE - 1));
}

#endif
