/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_X86_ENTRY_COMMON_H
#define _ASM_X86_ENTRY_COMMON_H

#include <linux/randomize_kstack.h>
#include <linux/user-return-notifier.h>

#include <asm/nospec-branch.h>
#include <asm/io_bitmap.h>
#include <asm/fpu/api.h>

/* Check that the stack and regs on entry from user mode are sane. */
static __always_inline void arch_check_user_regs(struct pt_regs *regs)
{
	if (IS_ENABLED(CONFIG_DEBUG_ENTRY)) {
		/*
		 * Make sure that the entry code gave us a sensible EFLAGS
		 * register.  Native because we want to check the actual CPU
		 * state, not the interrupt state as imagined by Xen.
		 */
		unsigned long flags = native_save_fl();
		unsigned long mask = X86_EFLAGS_DF | X86_EFLAGS_NT;

		/*
		 * For !SMAP hardware we patch out CLAC on entry.
		 */
		if (boot_cpu_has(X86_FEATURE_SMAP) ||
		    (IS_ENABLED(CONFIG_64_BIT) && boot_cpu_has(X86_FEATURE_XENPV)))
			mask |= X86_EFLAGS_AC;

		WARN_ON_ONCE(flags & mask);

		/* We think we came from user mode. Make sure pt_regs agrees. */
		WARN_ON_ONCE(!user_mode(regs));

		/*
		 * All entries from user mode (except #DF) should be on the
		 * normal thread stack and should have user pt_regs in the
		 * correct location.
		 */
		WARN_ON_ONCE(!on_thread_stack());
		WARN_ON_ONCE(regs != task_pt_regs(current));
	}
}
#define arch_check_user_regs arch_check_user_regs

static inline void arch_exit_to_user_mode_prepare(struct pt_regs *regs,
						  unsigned long ti_work)
{
	if (ti_work & _TIF_USER_RETURN_NOTIFY)
		fire_user_return_notifiers();

	if (unlikely(ti_work & _TIF_IO_BITMAP))
		tss_update_io_bitmap();

	fpregs_assert_state_consistent();
	if (unlikely(ti_work & _TIF_NEED_FPU_LOAD))
		switch_fpu_return();

#ifdef CONFIG_COMPAT
	/*
	 * Compat syscalls set TS_COMPAT.  Make sure we clear it before
	 * returning to user mode.  We need to clear it *after* signal
	 * handling, because syscall restart has a fixup for compat
	 * syscalls.  The fixup is exercised by the ptrace_syscall_32
	 * selftest.
	 *
	 * We also need to clear TS_REGS_POKED_I386: the 32-bit tracer
	 * special case only applies after poking regs and before the
	 * very next return to user mode.
	 */
	current_thread_info()->status &= ~(TS_COMPAT | TS_I386_REGS_POKED);
#endif

	/*
	 * Ultimately, this value will get limited by KSTACK_OFFSET_MAX(),
	 * but not enough for x86 stack utilization comfort. To keep
	 * reasonable stack head room, reduce the maximum offset to 8 bits.
	 *
	 * The actual entropy will be further reduced by the compiler when
	 * applying stack alignment constraints (see cc_stack_align4/8 in
	 * arch/x86/Makefile), which will remove the 3 (x86_64) or 2 (ia32)
	 * low bits from any entropy chosen here.
	 *
	 * Therefore, final stack offset entropy will be 5 (x86_64) or
	 * 6 (ia32) bits.
	 */
	choose_random_kstack_offset(rdtsc() & 0xFF);
}
#define arch_exit_to_user_mode_prepare arch_exit_to_user_mode_prepare

static __always_inline void arch_exit_to_user_mode(void)
{
	mds_user_clear_cpu_buffers();
}
#define arch_exit_to_user_mode arch_exit_to_user_mode

#endif
