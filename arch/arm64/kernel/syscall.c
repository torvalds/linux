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

	cortex_a76_erratum_1463225_svc_handler();
	user_exit_irqoff();
	local_daif_restore(DAIF_PROCCTX);

	if (has_syscall_work(flags)) {
		/* set default errno for user-issued syscall(-1) */
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

asmlinkage void el0_svc_handler(struct pt_regs *regs)
{
	sve_user_discard();
	el0_svc_common(regs, regs->regs[8], __NR_syscalls, sys_call_table);
}

#ifdef CONFIG_COMPAT
asmlinkage void el0_svc_compat_handler(struct pt_regs *regs)
{
	el0_svc_common(regs, regs->regs[7], __NR_compat_syscalls,
		       compat_sys_call_table);
}
#endif
