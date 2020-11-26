// SPDX-License-Identifier: GPL-2.0

#include <linux/compiler.h>
#include <linux/context_tracking.h>
#include <linux/errno.h>
#include <linux/nospec.h>
#include <linux/ptrace.h>
#include <linux/syscalls.h>

#include <asm/daifflags.h>
#include <asm/debug-monitors.h>
#include <asm/fpsimd.h>
#include <asm/syscall.h>
#include <asm/thread_info.h>
#include <asm/unistd.h>

long compat_arm_syscall(struct pt_regs *regs, int scno);
long sys_ni_syscall(void);

static long do_ni_syscall(struct pt_regs *regs, int scno)
{
#ifdef CONFIG_COMPAT
	long ret;
	if (is_compat_task()) {
		ret = compat_arm_syscall(regs, scno);
		if (ret != -ENOSYS)
			return ret;
	}
#endif

	return sys_ni_syscall();
}

static long __invoke_syscall(struct pt_regs *regs, syscall_fn_t syscall_fn)
{
	return syscall_fn(regs);
}

static void invoke_syscall(struct pt_regs *regs, unsigned int scno,
			   unsigned int sc_nr,
			   const syscall_fn_t syscall_table[])
{
	long ret;

	if (scno < sc_nr) {
		syscall_fn_t syscall_fn;
		syscall_fn = syscall_table[array_index_nospec(scno, sc_nr)];
		ret = __invoke_syscall(regs, syscall_fn);
	} else {
		ret = do_ni_syscall(regs, scno);
	}

	if (is_compat_task())
		ret = lower_32_bits(ret);

	regs->regs[0] = ret;
}

static inline bool has_syscall_work(unsigned long flags)
{
	return unlikely(flags & _TIF_SYSCALL_WORK);
}

int syscall_trace_enter(struct pt_regs *regs);
void syscall_trace_exit(struct pt_regs *regs);

#ifdef CONFIG_ARM64_ERRATUM_1463225
DECLARE_PER_CPU(int, __in_cortex_a76_erratum_1463225_wa);

static void cortex_a76_erratum_1463225_svc_handler(void)
{
	u32 reg, val;

	if (!unlikely(test_thread_flag(TIF_SINGLESTEP)))
		return;

	if (!unlikely(this_cpu_has_cap(ARM64_WORKAROUND_1463225)))
		return;

	__this_cpu_write(__in_cortex_a76_erratum_1463225_wa, 1);
	reg = read_sysreg(mdscr_el1);
	val = reg | DBG_MDSCR_SS | DBG_MDSCR_KDE;
	write_sysreg(val, mdscr_el1);
	asm volatile("msr daifclr, #8");
	isb();

	/* We will have taken a single-step exception by this point */

	write_sysreg(reg, mdscr_el1);
	__this_cpu_write(__in_cortex_a76_erratum_1463225_wa, 0);
}
#else
static void cortex_a76_erratum_1463225_svc_handler(void) { }
#endif /* CONFIG_ARM64_ERRATUM_1463225 */

static void el0_svc_common(struct pt_regs *regs, int scno, int sc_nr,
			   const syscall_fn_t syscall_table[])
{
	unsigned long flags = current_thread_info()->flags;

	regs->orig_x0 = regs->regs[0];
	regs->syscallno = scno;

	/*
	 * BTI note:
	 * The architecture does not guarantee that SPSR.BTYPE is zero
	 * on taking an SVC, so we could return to userspace with a
	 * non-zero BTYPE after the syscall.
	 *
	 * This shouldn't matter except when userspace is explicitly
	 * doing something stupid, such as setting PROT_BTI on a page
	 * that lacks conforming BTI/PACIxSP instructions, falling
	 * through from one executable page to another with differing
	 * PROT_BTI, or messing with BTYPE via ptrace: in such cases,
	 * userspace should not be surprised if a SIGILL occurs on
	 * syscall return.
	 *
	 * So, don't touch regs->pstate & PSR_BTYPE_MASK here.
	 * (Similarly for HVC and SMC elsewhere.)
	 */

	cortex_a76_erratum_1463225_svc_handler();
	local_daif_restore(DAIF_PROCCTX);
	user_exit();

	if (system_supports_mte() && (flags & _TIF_MTE_ASYNC_FAULT)) {
		/*
		 * Process the asynchronous tag check fault before the actual
		 * syscall. do_notify_resume() will send a signal to userspace
		 * before the syscall is restarted.
		 */
		regs->regs[0] = -ERESTARTNOINTR;
		return;
	}

	if (has_syscall_work(flags)) {
		/*
		 * The de-facto standard way to skip a system call using ptrace
		 * is to set the system call to -1 (NO_SYSCALL) and set x0 to a
		 * suitable error code for consumption by userspace. However,
		 * this cannot be distinguished from a user-issued syscall(-1)
		 * and so we must set x0 to -ENOSYS here in case the tracer doesn't
		 * issue the skip and we fall into trace_exit with x0 preserved.
		 *
		 * This is slightly odd because it also means that if a tracer
		 * sets the system call number to -1 but does not initialise x0,
		 * then x0 will be preserved for all system calls apart from a
		 * user-issued syscall(-1). However, requesting a skip and not
		 * setting the return value is unlikely to do anything sensible
		 * anyway.
		 */
		if (scno == NO_SYSCALL)
			regs->regs[0] = -ENOSYS;
		scno = syscall_trace_enter(regs);
		if (scno == NO_SYSCALL)
			goto trace_exit;
	}

	invoke_syscall(regs, scno, sc_nr, syscall_table);

	/*
	 * The tracing status may have changed under our feet, so we have to
	 * check again. However, if we were tracing entry, then we always trace
	 * exit regardless, as the old entry assembly did.
	 */
	if (!has_syscall_work(flags) && !IS_ENABLED(CONFIG_DEBUG_RSEQ)) {
		local_daif_mask();
		flags = current_thread_info()->flags;
		if (!has_syscall_work(flags) && !(flags & _TIF_SINGLESTEP)) {
			/*
			 * We're off to userspace, where interrupts are
			 * always enabled after we restore the flags from
			 * the SPSR.
			 */
			trace_hardirqs_on();
			return;
		}
		local_daif_restore(DAIF_PROCCTX);
	}

trace_exit:
	syscall_trace_exit(regs);
}

static inline void sve_user_discard(void)
{
	if (!system_supports_sve())
		return;

	clear_thread_flag(TIF_SVE);

	/*
	 * task_fpsimd_load() won't be called to update CPACR_EL1 in
	 * ret_to_user unless TIF_FOREIGN_FPSTATE is still set, which only
	 * happens if a context switch or kernel_neon_begin() or context
	 * modification (sigreturn, ptrace) intervenes.
	 * So, ensure that CPACR_EL1 is already correct for the fast-path case.
	 */
	sve_user_disable();
}

void do_el0_svc(struct pt_regs *regs)
{
	sve_user_discard();
	el0_svc_common(regs, regs->regs[8], __NR_syscalls, sys_call_table);
}

#ifdef CONFIG_COMPAT
void do_el0_svc_compat(struct pt_regs *regs)
{
	el0_svc_common(regs, regs->regs[7], __NR_compat_syscalls,
		       compat_sys_call_table);
}
#endif
