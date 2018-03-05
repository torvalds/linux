/*
 * common.c - C code for kernel entry and exit
 * Copyright (c) 2015 Andrew Lutomirski
 * GPL v2
 *
 * Based on asm and ptrace code by many authors.  The code here originated
 * in ptrace.c and signal.c.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/tracehook.h>
#include <linux/audit.h>
#include <linux/seccomp.h>
#include <linux/signal.h>
#include <linux/export.h>
#include <linux/context_tracking.h>
#include <linux/user-return-notifier.h>
#include <linux/nospec.h>
#include <linux/uprobes.h>

#include <asm/desc.h>
#include <asm/traps.h>
#include <asm/vdso.h>
#include <asm/uaccess.h>

#define CREATE_TRACE_POINTS
#include <trace/events/syscalls.h>

static struct thread_info *pt_regs_to_thread_info(struct pt_regs *regs)
{
	unsigned long top_of_stack =
		(unsigned long)(regs + 1) + TOP_OF_KERNEL_STACK_PADDING;
	return (struct thread_info *)(top_of_stack - THREAD_SIZE);
}

#ifdef CONFIG_CONTEXT_TRACKING
/* Called on entry from user mode with IRQs off. */
__visible void enter_from_user_mode(void)
{
	CT_WARN_ON(ct_state() != CONTEXT_USER);
	user_exit();
}
#endif

static void do_audit_syscall_entry(struct pt_regs *regs, u32 arch)
{
#ifdef CONFIG_X86_64
	if (arch == AUDIT_ARCH_X86_64) {
		audit_syscall_entry(regs->orig_ax, regs->di,
				    regs->si, regs->dx, regs->r10);
	} else
#endif
	{
		audit_syscall_entry(regs->orig_ax, regs->bx,
				    regs->cx, regs->dx, regs->si);
	}
}

/*
 * We can return 0 to resume the syscall or anything else to go to phase
 * 2.  If we resume the syscall, we need to put something appropriate in
 * regs->orig_ax.
 *
 * NB: We don't have full pt_regs here, but regs->orig_ax and regs->ax
 * are fully functional.
 *
 * For phase 2's benefit, our return value is:
 * 0:			resume the syscall
 * 1:			go to phase 2; no seccomp phase 2 needed
 * anything else:	go to phase 2; pass return value to seccomp
 */
unsigned long syscall_trace_enter_phase1(struct pt_regs *regs, u32 arch)
{
	struct thread_info *ti = pt_regs_to_thread_info(regs);
	unsigned long ret = 0;
	u32 work;

	if (IS_ENABLED(CONFIG_DEBUG_ENTRY))
		BUG_ON(regs != task_pt_regs(current));

	work = ACCESS_ONCE(ti->flags) & _TIF_WORK_SYSCALL_ENTRY;

#ifdef CONFIG_CONTEXT_TRACKING
	/*
	 * If TIF_NOHZ is set, we are required to call user_exit() before
	 * doing anything that could touch RCU.
	 */
	if (work & _TIF_NOHZ) {
		enter_from_user_mode();
		work &= ~_TIF_NOHZ;
	}
#endif

#ifdef CONFIG_SECCOMP
	/*
	 * Do seccomp first -- it should minimize exposure of other
	 * code, and keeping seccomp fast is probably more valuable
	 * than the rest of this.
	 */
	if (work & _TIF_SECCOMP) {
		struct seccomp_data sd;

		sd.arch = arch;
		sd.nr = regs->orig_ax;
		sd.instruction_pointer = regs->ip;
#ifdef CONFIG_X86_64
		if (arch == AUDIT_ARCH_X86_64) {
			sd.args[0] = regs->di;
			sd.args[1] = regs->si;
			sd.args[2] = regs->dx;
			sd.args[3] = regs->r10;
			sd.args[4] = regs->r8;
			sd.args[5] = regs->r9;
		} else
#endif
		{
			sd.args[0] = regs->bx;
			sd.args[1] = regs->cx;
			sd.args[2] = regs->dx;
			sd.args[3] = regs->si;
			sd.args[4] = regs->di;
			sd.args[5] = regs->bp;
		}

		BUILD_BUG_ON(SECCOMP_PHASE1_OK != 0);
		BUILD_BUG_ON(SECCOMP_PHASE1_SKIP != 1);

		ret = seccomp_phase1(&sd);
		if (ret == SECCOMP_PHASE1_SKIP) {
			regs->orig_ax = -1;
			ret = 0;
		} else if (ret != SECCOMP_PHASE1_OK) {
			return ret;  /* Go directly to phase 2 */
		}

		work &= ~_TIF_SECCOMP;
	}
#endif

	/* Do our best to finish without phase 2. */
	if (work == 0)
		return ret;  /* seccomp and/or nohz only (ret == 0 here) */

#ifdef CONFIG_AUDITSYSCALL
	if (work == _TIF_SYSCALL_AUDIT) {
		/*
		 * If there is no more work to be done except auditing,
		 * then audit in phase 1.  Phase 2 always audits, so, if
		 * we audit here, then we can't go on to phase 2.
		 */
		do_audit_syscall_entry(regs, arch);
		return 0;
	}
#endif

	return 1;  /* Something is enabled that we can't handle in phase 1 */
}

/* Returns the syscall nr to run (which should match regs->orig_ax). */
long syscall_trace_enter_phase2(struct pt_regs *regs, u32 arch,
				unsigned long phase1_result)
{
	struct thread_info *ti = pt_regs_to_thread_info(regs);
	long ret = 0;
	u32 work = ACCESS_ONCE(ti->flags) & _TIF_WORK_SYSCALL_ENTRY;

	if (IS_ENABLED(CONFIG_DEBUG_ENTRY))
		BUG_ON(regs != task_pt_regs(current));

	/*
	 * If we stepped into a sysenter/syscall insn, it trapped in
	 * kernel mode; do_debug() cleared TF and set TIF_SINGLESTEP.
	 * If user-mode had set TF itself, then it's still clear from
	 * do_debug() and we need to set it again to restore the user
	 * state.  If we entered on the slow path, TF was already set.
	 */
	if (work & _TIF_SINGLESTEP)
		regs->flags |= X86_EFLAGS_TF;

#ifdef CONFIG_SECCOMP
	/*
	 * Call seccomp_phase2 before running the other hooks so that
	 * they can see any changes made by a seccomp tracer.
	 */
	if (phase1_result > 1 && seccomp_phase2(phase1_result)) {
		/* seccomp failures shouldn't expose any additional code. */
		return -1;
	}
#endif

	if (unlikely(work & _TIF_SYSCALL_EMU))
		ret = -1L;

	if ((ret || test_thread_flag(TIF_SYSCALL_TRACE)) &&
	    tracehook_report_syscall_entry(regs))
		ret = -1L;

	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_enter(regs, regs->orig_ax);

	do_audit_syscall_entry(regs, arch);

	return ret ?: regs->orig_ax;
}

long syscall_trace_enter(struct pt_regs *regs)
{
	u32 arch = is_ia32_task() ? AUDIT_ARCH_I386 : AUDIT_ARCH_X86_64;
	unsigned long phase1_result = syscall_trace_enter_phase1(regs, arch);

	if (phase1_result == 0)
		return regs->orig_ax;
	else
		return syscall_trace_enter_phase2(regs, arch, phase1_result);
}

#define EXIT_TO_USERMODE_LOOP_FLAGS				\
	(_TIF_SIGPENDING | _TIF_NOTIFY_RESUME | _TIF_UPROBE |	\
	 _TIF_NEED_RESCHED | _TIF_USER_RETURN_NOTIFY)

static void exit_to_usermode_loop(struct pt_regs *regs, u32 cached_flags)
{
	/*
	 * In order to return to user mode, we need to have IRQs off with
	 * none of _TIF_SIGPENDING, _TIF_NOTIFY_RESUME, _TIF_USER_RETURN_NOTIFY,
	 * _TIF_UPROBE, or _TIF_NEED_RESCHED set.  Several of these flags
	 * can be set at any time on preemptable kernels if we have IRQs on,
	 * so we need to loop.  Disabling preemption wouldn't help: doing the
	 * work to clear some of the flags can sleep.
	 */
	while (true) {
		/* We have work to do. */
		local_irq_enable();

		if (cached_flags & _TIF_NEED_RESCHED)
			schedule();

		if (cached_flags & _TIF_UPROBE)
			uprobe_notify_resume(regs);

		/* deal with pending signal delivery */
		if (cached_flags & _TIF_SIGPENDING)
			do_signal(regs);

		if (cached_flags & _TIF_NOTIFY_RESUME) {
			clear_thread_flag(TIF_NOTIFY_RESUME);
			tracehook_notify_resume(regs);
		}

		if (cached_flags & _TIF_USER_RETURN_NOTIFY)
			fire_user_return_notifiers();

		/* Disable IRQs and retry */
		local_irq_disable();

		cached_flags = READ_ONCE(pt_regs_to_thread_info(regs)->flags);

		if (!(cached_flags & EXIT_TO_USERMODE_LOOP_FLAGS))
			break;

	}
}

/* Called with IRQs disabled. */
__visible inline void prepare_exit_to_usermode(struct pt_regs *regs)
{
	struct thread_info *ti = pt_regs_to_thread_info(regs);
	u32 cached_flags;

	if (IS_ENABLED(CONFIG_PROVE_LOCKING) && WARN_ON(!irqs_disabled()))
		local_irq_disable();

	lockdep_sys_exit();

	cached_flags = READ_ONCE(ti->flags);

	if (unlikely(cached_flags & EXIT_TO_USERMODE_LOOP_FLAGS))
		exit_to_usermode_loop(regs, cached_flags);

#ifdef CONFIG_COMPAT
	/*
	 * Compat syscalls set TS_COMPAT.  Make sure we clear it before
	 * returning to user mode.  We need to clear it *after* signal
	 * handling, because syscall restart has a fixup for compat
	 * syscalls.  The fixup is exercised by the ptrace_syscall_32
	 * selftest.
	 */
	ti->status &= ~TS_COMPAT;
#endif

	user_enter();
}

#define SYSCALL_EXIT_WORK_FLAGS				\
	(_TIF_SYSCALL_TRACE | _TIF_SYSCALL_AUDIT |	\
	 _TIF_SINGLESTEP | _TIF_SYSCALL_TRACEPOINT)

static void syscall_slow_exit_work(struct pt_regs *regs, u32 cached_flags)
{
	bool step;

	audit_syscall_exit(regs);

	if (cached_flags & _TIF_SYSCALL_TRACEPOINT)
		trace_sys_exit(regs, regs->ax);

	/*
	 * If TIF_SYSCALL_EMU is set, we only get here because of
	 * TIF_SINGLESTEP (i.e. this is PTRACE_SYSEMU_SINGLESTEP).
	 * We already reported this syscall instruction in
	 * syscall_trace_enter().
	 */
	step = unlikely(
		(cached_flags & (_TIF_SINGLESTEP | _TIF_SYSCALL_EMU))
		== _TIF_SINGLESTEP);
	if (step || cached_flags & _TIF_SYSCALL_TRACE)
		tracehook_report_syscall_exit(regs, step);
}

/*
 * Called with IRQs on and fully valid regs.  Returns with IRQs off in a
 * state such that we can immediately switch to user mode.
 */
__visible inline void syscall_return_slowpath(struct pt_regs *regs)
{
	struct thread_info *ti = pt_regs_to_thread_info(regs);
	u32 cached_flags = READ_ONCE(ti->flags);

	CT_WARN_ON(ct_state() != CONTEXT_KERNEL);

	if (IS_ENABLED(CONFIG_PROVE_LOCKING) &&
	    WARN(irqs_disabled(), "syscall %ld left IRQs disabled", regs->orig_ax))
		local_irq_enable();

	/*
	 * First do one-time work.  If these work items are enabled, we
	 * want to run them exactly once per syscall exit with IRQs on.
	 */
	if (unlikely(cached_flags & SYSCALL_EXIT_WORK_FLAGS))
		syscall_slow_exit_work(regs, cached_flags);

	local_irq_disable();
	prepare_exit_to_usermode(regs);
}

#if defined(CONFIG_X86_32) || defined(CONFIG_IA32_EMULATION)
/*
 * Does a 32-bit syscall.  Called with IRQs on and does all entry and
 * exit work and returns with IRQs off.  This function is extremely hot
 * in workloads that use it, and it's usually called from
 * do_fast_syscall_32, so forcibly inline it to improve performance.
 */
#ifdef CONFIG_X86_32
/* 32-bit kernels use a trap gate for INT80, and the asm code calls here. */
__visible
#else
/* 64-bit kernels use do_syscall_32_irqs_off() instead. */
static
#endif
__always_inline void do_syscall_32_irqs_on(struct pt_regs *regs)
{
	struct thread_info *ti = pt_regs_to_thread_info(regs);
	unsigned int nr = (unsigned int)regs->orig_ax;

#ifdef CONFIG_IA32_EMULATION
	ti->status |= TS_COMPAT;
#endif

	if (READ_ONCE(ti->flags) & _TIF_WORK_SYSCALL_ENTRY) {
		/*
		 * Subtlety here: if ptrace pokes something larger than
		 * 2^32-1 into orig_ax, this truncates it.  This may or
		 * may not be necessary, but it matches the old asm
		 * behavior.
		 */
		nr = syscall_trace_enter(regs);
	}

	if (likely(nr < IA32_NR_syscalls)) {
		nr = array_index_nospec(nr, IA32_NR_syscalls);
		/*
		 * It's possible that a 32-bit syscall implementation
		 * takes a 64-bit parameter but nonetheless assumes that
		 * the high bits are zero.  Make sure we zero-extend all
		 * of the args.
		 */
		regs->ax = ia32_sys_call_table[nr](
			(unsigned int)regs->bx, (unsigned int)regs->cx,
			(unsigned int)regs->dx, (unsigned int)regs->si,
			(unsigned int)regs->di, (unsigned int)regs->bp);
	}

	syscall_return_slowpath(regs);
}

#ifdef CONFIG_X86_64
/* Handles INT80 on 64-bit kernels */
__visible void do_syscall_32_irqs_off(struct pt_regs *regs)
{
	local_irq_enable();
	do_syscall_32_irqs_on(regs);
}
#endif

/* Returns 0 to return using IRET or 1 to return using SYSEXIT/SYSRETL. */
__visible long do_fast_syscall_32(struct pt_regs *regs)
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

	/*
	 * Fetch EBP from where the vDSO stashed it.
	 *
	 * WARNING: We are in CONTEXT_USER and RCU isn't paying attention!
	 */
	local_irq_enable();
	if (
#ifdef CONFIG_X86_64
		/*
		 * Micro-optimization: the pointer we're following is explicitly
		 * 32 bits, so it can't be out of range.
		 */
		__get_user(*(u32 *)&regs->bp,
			    (u32 __user __force *)(unsigned long)(u32)regs->sp)
#else
		get_user(*(u32 *)&regs->bp,
			 (u32 __user __force *)(unsigned long)(u32)regs->sp)
#endif
		) {

		/* User code screwed up. */
		local_irq_disable();
		regs->ax = -EFAULT;
#ifdef CONFIG_CONTEXT_TRACKING
		enter_from_user_mode();
#endif
		prepare_exit_to_usermode(regs);
		return 0;	/* Keep it simple: use IRET. */
	}

	/* Now this is just like a normal syscall. */
	do_syscall_32_irqs_on(regs);

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
#endif
