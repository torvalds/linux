// SPDX-License-Identifier: GPL-2.0-only
/*
 * common.c - C code for kernel entry and exit
 * Copyright (c) 2015 Andrew Lutomirski
 *
 * Based on asm and ptrace code by many authors.  The code here originated
 * in ptrace.c and signal.c.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/entry-common.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/export.h>
#include <linux/nospec.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>

#ifdef CONFIG_XEN_PV
#include <xen/xen-ops.h>
#include <xen/events.h>
#endif

#include <asm/desc.h>
#include <asm/traps.h>
#include <asm/vdso.h>
#include <asm/cpufeature.h>
#include <asm/fpu/api.h>
#include <asm/nospec-branch.h>
#include <asm/io_bitmap.h>
#include <asm/syscall.h>
#include <asm/irq_stack.h>

#ifdef CONFIG_X86_64
__visible noinstr void do_syscall_64(unsigned long nr, struct pt_regs *regs)
{
	nr = syscall_enter_from_user_mode(regs, nr);

	instrumentation_begin();
	if (likely(nr < NR_syscalls)) {
		nr = array_index_nospec(nr, NR_syscalls);
		regs->ax = sys_call_table[nr](regs);
#ifdef CONFIG_X86_X32_ABI
	} else if (likely((nr & __X32_SYSCALL_BIT) &&
			  (nr & ~__X32_SYSCALL_BIT) < X32_NR_syscalls)) {
		nr = array_index_nospec(nr & ~__X32_SYSCALL_BIT,
					X32_NR_syscalls);
		regs->ax = x32_sys_call_table[nr](regs);
#endif
	}
	instrumentation_end();
	syscall_exit_to_user_mode(regs);
}
#endif

#if defined(CONFIG_X86_32) || defined(CONFIG_IA32_EMULATION)
static __always_inline unsigned int syscall_32_enter(struct pt_regs *regs)
{
	unsigned int nr = (unsigned int)regs->orig_ax;

	if (IS_ENABLED(CONFIG_IA32_EMULATION))
		current_thread_info()->status |= TS_COMPAT;
	/*
	 * Subtlety here: if ptrace pokes something larger than 2^32-1 into
	 * orig_ax, the unsigned int return value truncates it.  This may
	 * or may not be necessary, but it matches the old asm behavior.
	 */
	return (unsigned int)syscall_enter_from_user_mode(regs, nr);
}

/*
 * Invoke a 32-bit syscall.  Called with IRQs on in CONTEXT_KERNEL.
 */
static __always_inline void do_syscall_32_irqs_on(struct pt_regs *regs,
						  unsigned int nr)
{
	if (likely(nr < IA32_NR_syscalls)) {
		instrumentation_begin();
		nr = array_index_nospec(nr, IA32_NR_syscalls);
		regs->ax = ia32_sys_call_table[nr](regs);
		instrumentation_end();
	}
}

/* Handles int $0x80 */
__visible noinstr void do_int80_syscall_32(struct pt_regs *regs)
{
	unsigned int nr = syscall_32_enter(regs);

	do_syscall_32_irqs_on(regs, nr);
	syscall_exit_to_user_mode(regs);
}

static noinstr bool __do_fast_syscall_32(struct pt_regs *regs)
{
	unsigned int nr	= syscall_32_enter(regs);
	int res;

	instrumentation_begin();
	/* Fetch EBP from where the vDSO stashed it. */
	if (IS_ENABLED(CONFIG_X86_64)) {
		/*
		 * Micro-optimization: the pointer we're following is
		 * explicitly 32 bits, so it can't be out of range.
		 */
		res = __get_user(*(u32 *)&regs->bp,
			 (u32 __user __force *)(unsigned long)(u32)regs->sp);
	} else {
		res = get_user(*(u32 *)&regs->bp,
		       (u32 __user __force *)(unsigned long)(u32)regs->sp);
	}
	instrumentation_end();

	if (res) {
		/* User code screwed up. */
		regs->ax = -EFAULT;
		syscall_exit_to_user_mode(regs);
		return false;
	}

	/* Now this is just like a normal syscall. */
	do_syscall_32_irqs_on(regs, nr);
	syscall_exit_to_user_mode(regs);
	return true;
}

/* Returns 0 to return using IRET or 1 to return using SYSEXIT/SYSRETL. */
__visible noinstr long do_fast_syscall_32(struct pt_regs *regs)
{
	/*
	 * Called using the internal vDSO SYSENTER/SYSCALL32 calling
	 * convention.  Adjust regs so it looks like we entered using int80.
	 */
	unsigned long landing_pad = (unsigned long)current->mm->context.vdso +
					vdso_image_32.sym_int80_landing_pad;

	/*
	 * SYSENTER loses EIP, and even SYSCALL32 needs us to skip forward
	 * so that 'regs->ip -= 2' lands back on an int $0x80 instruction.
	 * Fix it up.
	 */
	regs->ip = landing_pad;

	/* Invoke the syscall. If it failed, keep it simple: use IRET. */
	if (!__do_fast_syscall_32(regs))
		return 0;

#ifdef CONFIG_X86_64
	/*
	 * Opportunistic SYSRETL: if possible, try to return using SYSRETL.
	 * SYSRETL is available on all 64-bit CPUs, so we don't need to
	 * bother with SYSEXIT.
	 *
	 * Unlike 64-bit opportunistic SYSRET, we can't check that CX == IP,
	 * because the ECX fixup above will ensure that this is essentially
	 * never the case.
	 */
	return regs->cs == __USER32_CS && regs->ss == __USER_DS &&
		regs->ip == landing_pad &&
		(regs->flags & (X86_EFLAGS_RF | X86_EFLAGS_TF)) == 0;
#else
	/*
	 * Opportunistic SYSEXIT: if possible, try to return using SYSEXIT.
	 *
	 * Unlike 64-bit opportunistic SYSRET, we can't check that CX == IP,
	 * because the ECX fixup above will ensure that this is essentially
	 * never the case.
	 *
	 * We don't allow syscalls at all from VM86 mode, but we still
	 * need to check VM, because we might be returning from sys_vm86.
	 */
	return static_cpu_has(X86_FEATURE_SEP) &&
		regs->cs == __USER_CS && regs->ss == __USER_DS &&
		regs->ip == landing_pad &&
		(regs->flags & (X86_EFLAGS_RF | X86_EFLAGS_TF | X86_EFLAGS_VM)) == 0;
#endif
}

/* Returns 0 to return using IRET or 1 to return using SYSEXIT/SYSRETL. */
__visible noinstr long do_SYSENTER_32(struct pt_regs *regs)
{
	/* SYSENTER loses RSP, but the vDSO saved it in RBP. */
	regs->sp = regs->bp;

	/* SYSENTER clobbers EFLAGS.IF.  Assume it was set in usermode. */
	regs->flags |= X86_EFLAGS_IF;

	return do_fast_syscall_32(regs);
}
#endif

SYSCALL_DEFINE0(ni_syscall)
{
	return -ENOSYS;
}

/**
 * idtentry_enter - Handle state tracking on ordinary idtentries
 * @regs:	Pointer to pt_regs of interrupted context
 *
 * Invokes:
 *  - lockdep irqflag state tracking as low level ASM entry disabled
 *    interrupts.
 *
 *  - Context tracking if the exception hit user mode.
 *
 *  - The hardirq tracer to keep the state consistent as low level ASM
 *    entry disabled interrupts.
 *
 * As a precondition, this requires that the entry came from user mode,
 * idle, or a kernel context in which RCU is watching.
 *
 * For kernel mode entries RCU handling is done conditional. If RCU is
 * watching then the only RCU requirement is to check whether the tick has
 * to be restarted. If RCU is not watching then rcu_irq_enter() has to be
 * invoked on entry and rcu_irq_exit() on exit.
 *
 * Avoiding the rcu_irq_enter/exit() calls is an optimization but also
 * solves the problem of kernel mode pagefaults which can schedule, which
 * is not possible after invoking rcu_irq_enter() without undoing it.
 *
 * For user mode entries irqentry_enter_from_user_mode() must be invoked to
 * establish the proper context for NOHZ_FULL. Otherwise scheduling on exit
 * would not be possible.
 *
 * Returns: An opaque object that must be passed to idtentry_exit()
 *
 * The return value must be fed into the state argument of
 * idtentry_exit().
 */
idtentry_state_t noinstr idtentry_enter(struct pt_regs *regs)
{
	idtentry_state_t ret = {
		.exit_rcu = false,
	};

	if (user_mode(regs)) {
		irqentry_enter_from_user_mode(regs);
		return ret;
	}

	/*
	 * If this entry hit the idle task invoke rcu_irq_enter() whether
	 * RCU is watching or not.
	 *
	 * Interupts can nest when the first interrupt invokes softirq
	 * processing on return which enables interrupts.
	 *
	 * Scheduler ticks in the idle task can mark quiescent state and
	 * terminate a grace period, if and only if the timer interrupt is
	 * not nested into another interrupt.
	 *
	 * Checking for __rcu_is_watching() here would prevent the nesting
	 * interrupt to invoke rcu_irq_enter(). If that nested interrupt is
	 * the tick then rcu_flavor_sched_clock_irq() would wrongfully
	 * assume that it is the first interupt and eventually claim
	 * quiescient state and end grace periods prematurely.
	 *
	 * Unconditionally invoke rcu_irq_enter() so RCU state stays
	 * consistent.
	 *
	 * TINY_RCU does not support EQS, so let the compiler eliminate
	 * this part when enabled.
	 */
	if (!IS_ENABLED(CONFIG_TINY_RCU) && is_idle_task(current)) {
		/*
		 * If RCU is not watching then the same careful
		 * sequence vs. lockdep and tracing is required
		 * as in irqentry_enter_from_user_mode().
		 */
		lockdep_hardirqs_off(CALLER_ADDR0);
		rcu_irq_enter();
		instrumentation_begin();
		trace_hardirqs_off_finish();
		instrumentation_end();

		ret.exit_rcu = true;
		return ret;
	}

	/*
	 * If RCU is watching then RCU only wants to check whether it needs
	 * to restart the tick in NOHZ mode. rcu_irq_enter_check_tick()
	 * already contains a warning when RCU is not watching, so no point
	 * in having another one here.
	 */
	instrumentation_begin();
	rcu_irq_enter_check_tick();
	/* Use the combo lockdep/tracing function */
	trace_hardirqs_off();
	instrumentation_end();

	return ret;
}

static void idtentry_exit_cond_resched(struct pt_regs *regs, bool may_sched)
{
	if (may_sched && !preempt_count()) {
		/* Sanity check RCU and thread stack */
		rcu_irq_exit_check_preempt();
		if (IS_ENABLED(CONFIG_DEBUG_ENTRY))
			WARN_ON_ONCE(!on_thread_stack());
		if (need_resched())
			preempt_schedule_irq();
	}
	/* Covers both tracing and lockdep */
	trace_hardirqs_on();
}

/**
 * idtentry_exit - Handle return from exception that used idtentry_enter()
 * @regs:	Pointer to pt_regs (exception entry regs)
 * @state:	Return value from matching call to idtentry_enter()
 *
 * Depending on the return target (kernel/user) this runs the necessary
 * preemption and work checks if possible and reguired and returns to
 * the caller with interrupts disabled and no further work pending.
 *
 * This is the last action before returning to the low level ASM code which
 * just needs to return to the appropriate context.
 *
 * Counterpart to idtentry_enter(). The return value of the entry
 * function must be fed into the @state argument.
 */
void noinstr idtentry_exit(struct pt_regs *regs, idtentry_state_t state)
{
	lockdep_assert_irqs_disabled();

	/* Check whether this returns to user mode */
	if (user_mode(regs)) {
		irqentry_exit_to_user_mode(regs);
	} else if (regs->flags & X86_EFLAGS_IF) {
		/*
		 * If RCU was not watching on entry this needs to be done
		 * carefully and needs the same ordering of lockdep/tracing
		 * and RCU as the return to user mode path.
		 */
		if (state.exit_rcu) {
			instrumentation_begin();
			/* Tell the tracer that IRET will enable interrupts */
			trace_hardirqs_on_prepare();
			lockdep_hardirqs_on_prepare(CALLER_ADDR0);
			instrumentation_end();
			rcu_irq_exit();
			lockdep_hardirqs_on(CALLER_ADDR0);
			return;
		}

		instrumentation_begin();
		idtentry_exit_cond_resched(regs, IS_ENABLED(CONFIG_PREEMPTION));
		instrumentation_end();
	} else {
		/*
		 * IRQ flags state is correct already. Just tell RCU if it
		 * was not watching on entry.
		 */
		if (state.exit_rcu)
			rcu_irq_exit();
	}
}

#ifdef CONFIG_XEN_PV
#ifndef CONFIG_PREEMPTION
/*
 * Some hypercalls issued by the toolstack can take many 10s of
 * seconds. Allow tasks running hypercalls via the privcmd driver to
 * be voluntarily preempted even if full kernel preemption is
 * disabled.
 *
 * Such preemptible hypercalls are bracketed by
 * xen_preemptible_hcall_begin() and xen_preemptible_hcall_end()
 * calls.
 */
DEFINE_PER_CPU(bool, xen_in_preemptible_hcall);
EXPORT_SYMBOL_GPL(xen_in_preemptible_hcall);

/*
 * In case of scheduling the flag must be cleared and restored after
 * returning from schedule as the task might move to a different CPU.
 */
static __always_inline bool get_and_clear_inhcall(void)
{
	bool inhcall = __this_cpu_read(xen_in_preemptible_hcall);

	__this_cpu_write(xen_in_preemptible_hcall, false);
	return inhcall;
}

static __always_inline void restore_inhcall(bool inhcall)
{
	__this_cpu_write(xen_in_preemptible_hcall, inhcall);
}
#else
static __always_inline bool get_and_clear_inhcall(void) { return false; }
static __always_inline void restore_inhcall(bool inhcall) { }
#endif

static void __xen_pv_evtchn_do_upcall(void)
{
	irq_enter_rcu();
	inc_irq_stat(irq_hv_callback_count);

	xen_hvm_evtchn_do_upcall();

	irq_exit_rcu();
}

__visible noinstr void xen_pv_evtchn_do_upcall(struct pt_regs *regs)
{
	struct pt_regs *old_regs;
	bool inhcall;
	idtentry_state_t state;

	state = idtentry_enter(regs);
	old_regs = set_irq_regs(regs);

	instrumentation_begin();
	run_on_irqstack_cond(__xen_pv_evtchn_do_upcall, NULL, regs);
	instrumentation_begin();

	set_irq_regs(old_regs);

	inhcall = get_and_clear_inhcall();
	if (inhcall && !WARN_ON_ONCE(state.exit_rcu)) {
		instrumentation_begin();
		idtentry_exit_cond_resched(regs, true);
		instrumentation_end();
		restore_inhcall(inhcall);
	} else {
		idtentry_exit(regs, state);
	}
}
#endif /* CONFIG_XEN_PV */
