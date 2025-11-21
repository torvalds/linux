// SPDX-License-Identifier: GPL-2.0
/*
 * Exception handling code
 *
 * Copyright (C) 2019 ARM Ltd.
 */

#include <linux/context_tracking.h>
#include <linux/irq-entry-common.h>
#include <linux/kasan.h>
#include <linux/linkage.h>
#include <linux/livepatch.h>
#include <linux/lockdep.h>
#include <linux/ptrace.h>
#include <linux/resume_user_mode.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/thread_info.h>

#include <asm/cpufeature.h>
#include <asm/daifflags.h>
#include <asm/esr.h>
#include <asm/exception.h>
#include <asm/irq_regs.h>
#include <asm/kprobes.h>
#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/sdei.h>
#include <asm/stacktrace.h>
#include <asm/sysreg.h>
#include <asm/system_misc.h>

/*
 * Handle IRQ/context state management when entering from kernel mode.
 * Before this function is called it is not safe to call regular kernel code,
 * instrumentable code, or any code which may trigger an exception.
 *
 * This is intended to match the logic in irqentry_enter(), handling the kernel
 * mode transitions only.
 */
static __always_inline irqentry_state_t __enter_from_kernel_mode(struct pt_regs *regs)
{
	return irqentry_enter(regs);
}

static noinstr irqentry_state_t enter_from_kernel_mode(struct pt_regs *regs)
{
	irqentry_state_t state;

	state = __enter_from_kernel_mode(regs);
	mte_check_tfsr_entry();
	mte_disable_tco_entry(current);

	return state;
}

/*
 * Handle IRQ/context state management when exiting to kernel mode.
 * After this function returns it is not safe to call regular kernel code,
 * instrumentable code, or any code which may trigger an exception.
 *
 * This is intended to match the logic in irqentry_exit(), handling the kernel
 * mode transitions only, and with preemption handled elsewhere.
 */
static __always_inline void __exit_to_kernel_mode(struct pt_regs *regs,
						  irqentry_state_t state)
{
	irqentry_exit(regs, state);
}

static void noinstr exit_to_kernel_mode(struct pt_regs *regs,
					irqentry_state_t state)
{
	mte_check_tfsr_exit();
	__exit_to_kernel_mode(regs, state);
}

/*
 * Handle IRQ/context state management when entering from user mode.
 * Before this function is called it is not safe to call regular kernel code,
 * instrumentable code, or any code which may trigger an exception.
 */
static __always_inline void __enter_from_user_mode(struct pt_regs *regs)
{
	enter_from_user_mode(regs);
	mte_disable_tco_entry(current);
}

static __always_inline void arm64_enter_from_user_mode(struct pt_regs *regs)
{
	__enter_from_user_mode(regs);
}

/*
 * Handle IRQ/context state management when exiting to user mode.
 * After this function returns it is not safe to call regular kernel code,
 * instrumentable code, or any code which may trigger an exception.
 */

static __always_inline void arm64_exit_to_user_mode(struct pt_regs *regs)
{
	local_irq_disable();
	exit_to_user_mode_prepare(regs);
	local_daif_mask();
	mte_check_tfsr_exit();
	exit_to_user_mode();
}

asmlinkage void noinstr asm_exit_to_user_mode(struct pt_regs *regs)
{
	arm64_exit_to_user_mode(regs);
}

/*
 * Handle IRQ/context state management when entering a debug exception from
 * kernel mode. Before this function is called it is not safe to call regular
 * kernel code, instrumentable code, or any code which may trigger an exception.
 */
static noinstr irqentry_state_t arm64_enter_el1_dbg(struct pt_regs *regs)
{
	irqentry_state_t state;

	state.lockdep = lockdep_hardirqs_enabled();

	lockdep_hardirqs_off(CALLER_ADDR0);
	ct_nmi_enter();

	trace_hardirqs_off_finish();

	return state;
}

/*
 * Handle IRQ/context state management when exiting a debug exception from
 * kernel mode. After this function returns it is not safe to call regular
 * kernel code, instrumentable code, or any code which may trigger an exception.
 */
static void noinstr arm64_exit_el1_dbg(struct pt_regs *regs,
				       irqentry_state_t state)
{
	if (state.lockdep) {
		trace_hardirqs_on_prepare();
		lockdep_hardirqs_on_prepare();
	}

	ct_nmi_exit();
	if (state.lockdep)
		lockdep_hardirqs_on(CALLER_ADDR0);
}

static void do_interrupt_handler(struct pt_regs *regs,
				 void (*handler)(struct pt_regs *))
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	if (on_thread_stack())
		call_on_irq_stack(regs, handler);
	else
		handler(regs);

	set_irq_regs(old_regs);
}

extern void (*handle_arch_irq)(struct pt_regs *);
extern void (*handle_arch_fiq)(struct pt_regs *);

static void noinstr __panic_unhandled(struct pt_regs *regs, const char *vector,
				      unsigned long esr)
{
	irqentry_nmi_enter(regs);

	console_verbose();

	pr_crit("Unhandled %s exception on CPU%d, ESR 0x%016lx -- %s\n",
		vector, smp_processor_id(), esr,
		esr_get_class_string(esr));

	__show_regs(regs);
	panic("Unhandled exception");
}

#define UNHANDLED(el, regsize, vector)							\
asmlinkage void noinstr el##_##regsize##_##vector##_handler(struct pt_regs *regs)	\
{											\
	const char *desc = #regsize "-bit " #el " " #vector;				\
	__panic_unhandled(regs, desc, read_sysreg(esr_el1));				\
}

#ifdef CONFIG_ARM64_ERRATUM_1463225
static DEFINE_PER_CPU(int, __in_cortex_a76_erratum_1463225_wa);

static void cortex_a76_erratum_1463225_svc_handler(void)
{
	u64 reg, val;

	if (!unlikely(test_thread_flag(TIF_SINGLESTEP)))
		return;

	if (!unlikely(this_cpu_has_cap(ARM64_WORKAROUND_1463225)))
		return;

	__this_cpu_write(__in_cortex_a76_erratum_1463225_wa, 1);
	reg = read_sysreg(mdscr_el1);
	val = reg | MDSCR_EL1_SS | MDSCR_EL1_KDE;
	write_sysreg(val, mdscr_el1);
	asm volatile("msr daifclr, #8");
	isb();

	/* We will have taken a single-step exception by this point */

	write_sysreg(reg, mdscr_el1);
	__this_cpu_write(__in_cortex_a76_erratum_1463225_wa, 0);
}

static __always_inline bool
cortex_a76_erratum_1463225_debug_handler(struct pt_regs *regs)
{
	if (!__this_cpu_read(__in_cortex_a76_erratum_1463225_wa))
		return false;

	/*
	 * We've taken a dummy step exception from the kernel to ensure
	 * that interrupts are re-enabled on the syscall path. Return back
	 * to cortex_a76_erratum_1463225_svc_handler() with debug exceptions
	 * masked so that we can safely restore the mdscr and get on with
	 * handling the syscall.
	 */
	regs->pstate |= PSR_D_BIT;
	return true;
}
#else /* CONFIG_ARM64_ERRATUM_1463225 */
static void cortex_a76_erratum_1463225_svc_handler(void) { }
static bool cortex_a76_erratum_1463225_debug_handler(struct pt_regs *regs)
{
	return false;
}
#endif /* CONFIG_ARM64_ERRATUM_1463225 */

/*
 * As per the ABI exit SME streaming mode and clear the SVE state not
 * shared with FPSIMD on syscall entry.
 */
static inline void fpsimd_syscall_enter(void)
{
	/* Ensure PSTATE.SM is clear, but leave PSTATE.ZA as-is. */
	if (system_supports_sme())
		sme_smstop_sm();

	/*
	 * The CPU is not in streaming mode. If non-streaming SVE is not
	 * supported, there is no SVE state that needs to be discarded.
	 */
	if (!system_supports_sve())
		return;

	if (test_thread_flag(TIF_SVE)) {
		unsigned int sve_vq_minus_one;

		sve_vq_minus_one = sve_vq_from_vl(task_get_sve_vl(current)) - 1;
		sve_flush_live(true, sve_vq_minus_one);
	}

	/*
	 * Any live non-FPSIMD SVE state has been zeroed. Allow
	 * fpsimd_save_user_state() to lazily discard SVE state until either
	 * the live state is unbound or fpsimd_syscall_exit() is called.
	 */
	__this_cpu_write(fpsimd_last_state.to_save, FP_STATE_FPSIMD);
}

static __always_inline void fpsimd_syscall_exit(void)
{
	if (!system_supports_sve())
		return;

	/*
	 * The current task's user FPSIMD/SVE/SME state is now bound to this
	 * CPU. The fpsimd_last_state.to_save value is either:
	 *
	 * - FP_STATE_FPSIMD, if the state has not been reloaded on this CPU
	 *   since fpsimd_syscall_enter().
	 *
	 * - FP_STATE_CURRENT, if the state has been reloaded on this CPU at
	 *   any point.
	 *
	 * Reset this to FP_STATE_CURRENT to stop lazy discarding.
	 */
	__this_cpu_write(fpsimd_last_state.to_save, FP_STATE_CURRENT);
}

/*
 * In debug exception context, we explicitly disable preemption despite
 * having interrupts disabled.
 * This serves two purposes: it makes it much less likely that we would
 * accidentally schedule in exception context and it will force a warning
 * if we somehow manage to schedule by accident.
 */
static void debug_exception_enter(struct pt_regs *regs)
{
	preempt_disable();

	/* This code is a bit fragile.  Test it. */
	RCU_LOCKDEP_WARN(!rcu_is_watching(), "exception_enter didn't work");
}
NOKPROBE_SYMBOL(debug_exception_enter);

static void debug_exception_exit(struct pt_regs *regs)
{
	preempt_enable_no_resched();
}
NOKPROBE_SYMBOL(debug_exception_exit);

UNHANDLED(el1t, 64, sync)
UNHANDLED(el1t, 64, irq)
UNHANDLED(el1t, 64, fiq)
UNHANDLED(el1t, 64, error)

static void noinstr el1_abort(struct pt_regs *regs, unsigned long esr)
{
	unsigned long far = read_sysreg(far_el1);
	irqentry_state_t state;

	state = enter_from_kernel_mode(regs);
	local_daif_inherit(regs);
	do_mem_abort(far, esr, regs);
	local_daif_mask();
	exit_to_kernel_mode(regs, state);
}

static void noinstr el1_pc(struct pt_regs *regs, unsigned long esr)
{
	unsigned long far = read_sysreg(far_el1);
	irqentry_state_t state;

	state = enter_from_kernel_mode(regs);
	local_daif_inherit(regs);
	do_sp_pc_abort(far, esr, regs);
	local_daif_mask();
	exit_to_kernel_mode(regs, state);
}

static void noinstr el1_undef(struct pt_regs *regs, unsigned long esr)
{
	irqentry_state_t state;

	state = enter_from_kernel_mode(regs);
	local_daif_inherit(regs);
	do_el1_undef(regs, esr);
	local_daif_mask();
	exit_to_kernel_mode(regs, state);
}

static void noinstr el1_bti(struct pt_regs *regs, unsigned long esr)
{
	irqentry_state_t state;

	state = enter_from_kernel_mode(regs);
	local_daif_inherit(regs);
	do_el1_bti(regs, esr);
	local_daif_mask();
	exit_to_kernel_mode(regs, state);
}

static void noinstr el1_gcs(struct pt_regs *regs, unsigned long esr)
{
	irqentry_state_t state;

	state = enter_from_kernel_mode(regs);
	local_daif_inherit(regs);
	do_el1_gcs(regs, esr);
	local_daif_mask();
	exit_to_kernel_mode(regs, state);
}

static void noinstr el1_mops(struct pt_regs *regs, unsigned long esr)
{
	irqentry_state_t state;

	state = enter_from_kernel_mode(regs);
	local_daif_inherit(regs);
	do_el1_mops(regs, esr);
	local_daif_mask();
	exit_to_kernel_mode(regs, state);
}

static void noinstr el1_breakpt(struct pt_regs *regs, unsigned long esr)
{
	irqentry_state_t state;

	state = arm64_enter_el1_dbg(regs);
	debug_exception_enter(regs);
	do_breakpoint(esr, regs);
	debug_exception_exit(regs);
	arm64_exit_el1_dbg(regs, state);
}

static void noinstr el1_softstp(struct pt_regs *regs, unsigned long esr)
{
	irqentry_state_t state;

	state = arm64_enter_el1_dbg(regs);
	if (!cortex_a76_erratum_1463225_debug_handler(regs)) {
		debug_exception_enter(regs);
		/*
		 * After handling a breakpoint, we suspend the breakpoint
		 * and use single-step to move to the next instruction.
		 * If we are stepping a suspended breakpoint there's nothing more to do:
		 * the single-step is complete.
		 */
		if (!try_step_suspended_breakpoints(regs))
			do_el1_softstep(esr, regs);
		debug_exception_exit(regs);
	}
	arm64_exit_el1_dbg(regs, state);
}

static void noinstr el1_watchpt(struct pt_regs *regs, unsigned long esr)
{
	/* Watchpoints are the only debug exception to write FAR_EL1 */
	unsigned long far = read_sysreg(far_el1);
	irqentry_state_t state;

	state = arm64_enter_el1_dbg(regs);
	debug_exception_enter(regs);
	do_watchpoint(far, esr, regs);
	debug_exception_exit(regs);
	arm64_exit_el1_dbg(regs, state);
}

static void noinstr el1_brk64(struct pt_regs *regs, unsigned long esr)
{
	irqentry_state_t state;

	state = arm64_enter_el1_dbg(regs);
	debug_exception_enter(regs);
	do_el1_brk64(esr, regs);
	debug_exception_exit(regs);
	arm64_exit_el1_dbg(regs, state);
}

static void noinstr el1_fpac(struct pt_regs *regs, unsigned long esr)
{
	irqentry_state_t state;

	state = enter_from_kernel_mode(regs);
	local_daif_inherit(regs);
	do_el1_fpac(regs, esr);
	local_daif_mask();
	exit_to_kernel_mode(regs, state);
}

asmlinkage void noinstr el1h_64_sync_handler(struct pt_regs *regs)
{
	unsigned long esr = read_sysreg(esr_el1);

	switch (ESR_ELx_EC(esr)) {
	case ESR_ELx_EC_DABT_CUR:
	case ESR_ELx_EC_IABT_CUR:
		el1_abort(regs, esr);
		break;
	/*
	 * We don't handle ESR_ELx_EC_SP_ALIGN, since we will have hit a
	 * recursive exception when trying to push the initial pt_regs.
	 */
	case ESR_ELx_EC_PC_ALIGN:
		el1_pc(regs, esr);
		break;
	case ESR_ELx_EC_SYS64:
	case ESR_ELx_EC_UNKNOWN:
		el1_undef(regs, esr);
		break;
	case ESR_ELx_EC_BTI:
		el1_bti(regs, esr);
		break;
	case ESR_ELx_EC_GCS:
		el1_gcs(regs, esr);
		break;
	case ESR_ELx_EC_MOPS:
		el1_mops(regs, esr);
		break;
	case ESR_ELx_EC_BREAKPT_CUR:
		el1_breakpt(regs, esr);
		break;
	case ESR_ELx_EC_SOFTSTP_CUR:
		el1_softstp(regs, esr);
		break;
	case ESR_ELx_EC_WATCHPT_CUR:
		el1_watchpt(regs, esr);
		break;
	case ESR_ELx_EC_BRK64:
		el1_brk64(regs, esr);
		break;
	case ESR_ELx_EC_FPAC:
		el1_fpac(regs, esr);
		break;
	default:
		__panic_unhandled(regs, "64-bit el1h sync", esr);
	}
}

static __always_inline void __el1_pnmi(struct pt_regs *regs,
				       void (*handler)(struct pt_regs *))
{
	irqentry_state_t state;

	state = irqentry_nmi_enter(regs);
	do_interrupt_handler(regs, handler);
	irqentry_nmi_exit(regs, state);
}

static __always_inline void __el1_irq(struct pt_regs *regs,
				      void (*handler)(struct pt_regs *))
{
	irqentry_state_t state;

	state = enter_from_kernel_mode(regs);

	irq_enter_rcu();
	do_interrupt_handler(regs, handler);
	irq_exit_rcu();

	exit_to_kernel_mode(regs, state);
}
static void noinstr el1_interrupt(struct pt_regs *regs,
				  void (*handler)(struct pt_regs *))
{
	write_sysreg(DAIF_PROCCTX_NOIRQ, daif);

	if (IS_ENABLED(CONFIG_ARM64_PSEUDO_NMI) && regs_irqs_disabled(regs))
		__el1_pnmi(regs, handler);
	else
		__el1_irq(regs, handler);
}

asmlinkage void noinstr el1h_64_irq_handler(struct pt_regs *regs)
{
	el1_interrupt(regs, handle_arch_irq);
}

asmlinkage void noinstr el1h_64_fiq_handler(struct pt_regs *regs)
{
	el1_interrupt(regs, handle_arch_fiq);
}

asmlinkage void noinstr el1h_64_error_handler(struct pt_regs *regs)
{
	unsigned long esr = read_sysreg(esr_el1);
	irqentry_state_t state;

	local_daif_restore(DAIF_ERRCTX);
	state = irqentry_nmi_enter(regs);
	do_serror(regs, esr);
	irqentry_nmi_exit(regs, state);
}

static void noinstr el0_da(struct pt_regs *regs, unsigned long esr)
{
	unsigned long far = read_sysreg(far_el1);

	arm64_enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_mem_abort(far, esr, regs);
	arm64_exit_to_user_mode(regs);
}

static void noinstr el0_ia(struct pt_regs *regs, unsigned long esr)
{
	unsigned long far = read_sysreg(far_el1);

	/*
	 * We've taken an instruction abort from userspace and not yet
	 * re-enabled IRQs. If the address is a kernel address, apply
	 * BP hardening prior to enabling IRQs and pre-emption.
	 */
	if (!is_ttbr0_addr(far))
		arm64_apply_bp_hardening();

	arm64_enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_mem_abort(far, esr, regs);
	arm64_exit_to_user_mode(regs);
}

static void noinstr el0_fpsimd_acc(struct pt_regs *regs, unsigned long esr)
{
	arm64_enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_fpsimd_acc(esr, regs);
	arm64_exit_to_user_mode(regs);
}

static void noinstr el0_sve_acc(struct pt_regs *regs, unsigned long esr)
{
	arm64_enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_sve_acc(esr, regs);
	arm64_exit_to_user_mode(regs);
}

static void noinstr el0_sme_acc(struct pt_regs *regs, unsigned long esr)
{
	arm64_enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_sme_acc(esr, regs);
	arm64_exit_to_user_mode(regs);
}

static void noinstr el0_fpsimd_exc(struct pt_regs *regs, unsigned long esr)
{
	arm64_enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_fpsimd_exc(esr, regs);
	arm64_exit_to_user_mode(regs);
}

static void noinstr el0_sys(struct pt_regs *regs, unsigned long esr)
{
	arm64_enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_el0_sys(esr, regs);
	arm64_exit_to_user_mode(regs);
}

static void noinstr el0_pc(struct pt_regs *regs, unsigned long esr)
{
	unsigned long far = read_sysreg(far_el1);

	if (!is_ttbr0_addr(instruction_pointer(regs)))
		arm64_apply_bp_hardening();

	arm64_enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_sp_pc_abort(far, esr, regs);
	arm64_exit_to_user_mode(regs);
}

static void noinstr el0_sp(struct pt_regs *regs, unsigned long esr)
{
	arm64_enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_sp_pc_abort(regs->sp, esr, regs);
	arm64_exit_to_user_mode(regs);
}

static void noinstr el0_undef(struct pt_regs *regs, unsigned long esr)
{
	arm64_enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_el0_undef(regs, esr);
	arm64_exit_to_user_mode(regs);
}

static void noinstr el0_bti(struct pt_regs *regs)
{
	arm64_enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_el0_bti(regs);
	arm64_exit_to_user_mode(regs);
}

static void noinstr el0_mops(struct pt_regs *regs, unsigned long esr)
{
	arm64_enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_el0_mops(regs, esr);
	arm64_exit_to_user_mode(regs);
}

static void noinstr el0_gcs(struct pt_regs *regs, unsigned long esr)
{
	arm64_enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_el0_gcs(regs, esr);
	arm64_exit_to_user_mode(regs);
}

static void noinstr el0_inv(struct pt_regs *regs, unsigned long esr)
{
	arm64_enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	bad_el0_sync(regs, 0, esr);
	arm64_exit_to_user_mode(regs);
}

static void noinstr el0_breakpt(struct pt_regs *regs, unsigned long esr)
{
	if (!is_ttbr0_addr(regs->pc))
		arm64_apply_bp_hardening();

	arm64_enter_from_user_mode(regs);
	debug_exception_enter(regs);
	do_breakpoint(esr, regs);
	debug_exception_exit(regs);
	local_daif_restore(DAIF_PROCCTX);
	arm64_exit_to_user_mode(regs);
}

static void noinstr el0_softstp(struct pt_regs *regs, unsigned long esr)
{
	bool step_done;

	if (!is_ttbr0_addr(regs->pc))
		arm64_apply_bp_hardening();

	arm64_enter_from_user_mode(regs);
	/*
	 * After handling a breakpoint, we suspend the breakpoint
	 * and use single-step to move to the next instruction.
	 * If we are stepping a suspended breakpoint there's nothing more to do:
	 * the single-step is complete.
	 */
	step_done = try_step_suspended_breakpoints(regs);
	local_daif_restore(DAIF_PROCCTX);
	if (!step_done)
		do_el0_softstep(esr, regs);
	arm64_exit_to_user_mode(regs);
}

static void noinstr el0_watchpt(struct pt_regs *regs, unsigned long esr)
{
	/* Watchpoints are the only debug exception to write FAR_EL1 */
	unsigned long far = read_sysreg(far_el1);

	arm64_enter_from_user_mode(regs);
	debug_exception_enter(regs);
	do_watchpoint(far, esr, regs);
	debug_exception_exit(regs);
	local_daif_restore(DAIF_PROCCTX);
	arm64_exit_to_user_mode(regs);
}

static void noinstr el0_brk64(struct pt_regs *regs, unsigned long esr)
{
	arm64_enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_el0_brk64(esr, regs);
	arm64_exit_to_user_mode(regs);
}

static void noinstr el0_svc(struct pt_regs *regs)
{
	arm64_enter_from_user_mode(regs);
	cortex_a76_erratum_1463225_svc_handler();
	fpsimd_syscall_enter();
	local_daif_restore(DAIF_PROCCTX);
	do_el0_svc(regs);
	arm64_exit_to_user_mode(regs);
	fpsimd_syscall_exit();
}

static void noinstr el0_fpac(struct pt_regs *regs, unsigned long esr)
{
	arm64_enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_el0_fpac(regs, esr);
	arm64_exit_to_user_mode(regs);
}

asmlinkage void noinstr el0t_64_sync_handler(struct pt_regs *regs)
{
	unsigned long esr = read_sysreg(esr_el1);

	switch (ESR_ELx_EC(esr)) {
	case ESR_ELx_EC_SVC64:
		el0_svc(regs);
		break;
	case ESR_ELx_EC_DABT_LOW:
		el0_da(regs, esr);
		break;
	case ESR_ELx_EC_IABT_LOW:
		el0_ia(regs, esr);
		break;
	case ESR_ELx_EC_FP_ASIMD:
		el0_fpsimd_acc(regs, esr);
		break;
	case ESR_ELx_EC_SVE:
		el0_sve_acc(regs, esr);
		break;
	case ESR_ELx_EC_SME:
		el0_sme_acc(regs, esr);
		break;
	case ESR_ELx_EC_FP_EXC64:
		el0_fpsimd_exc(regs, esr);
		break;
	case ESR_ELx_EC_SYS64:
	case ESR_ELx_EC_WFx:
		el0_sys(regs, esr);
		break;
	case ESR_ELx_EC_SP_ALIGN:
		el0_sp(regs, esr);
		break;
	case ESR_ELx_EC_PC_ALIGN:
		el0_pc(regs, esr);
		break;
	case ESR_ELx_EC_UNKNOWN:
		el0_undef(regs, esr);
		break;
	case ESR_ELx_EC_BTI:
		el0_bti(regs);
		break;
	case ESR_ELx_EC_MOPS:
		el0_mops(regs, esr);
		break;
	case ESR_ELx_EC_GCS:
		el0_gcs(regs, esr);
		break;
	case ESR_ELx_EC_BREAKPT_LOW:
		el0_breakpt(regs, esr);
		break;
	case ESR_ELx_EC_SOFTSTP_LOW:
		el0_softstp(regs, esr);
		break;
	case ESR_ELx_EC_WATCHPT_LOW:
		el0_watchpt(regs, esr);
		break;
	case ESR_ELx_EC_BRK64:
		el0_brk64(regs, esr);
		break;
	case ESR_ELx_EC_FPAC:
		el0_fpac(regs, esr);
		break;
	default:
		el0_inv(regs, esr);
	}
}

static void noinstr el0_interrupt(struct pt_regs *regs,
				  void (*handler)(struct pt_regs *))
{
	arm64_enter_from_user_mode(regs);

	write_sysreg(DAIF_PROCCTX_NOIRQ, daif);

	if (regs->pc & BIT(55))
		arm64_apply_bp_hardening();

	irq_enter_rcu();
	do_interrupt_handler(regs, handler);
	irq_exit_rcu();

	arm64_exit_to_user_mode(regs);
}

static void noinstr __el0_irq_handler_common(struct pt_regs *regs)
{
	el0_interrupt(regs, handle_arch_irq);
}

asmlinkage void noinstr el0t_64_irq_handler(struct pt_regs *regs)
{
	__el0_irq_handler_common(regs);
}

static void noinstr __el0_fiq_handler_common(struct pt_regs *regs)
{
	el0_interrupt(regs, handle_arch_fiq);
}

asmlinkage void noinstr el0t_64_fiq_handler(struct pt_regs *regs)
{
	__el0_fiq_handler_common(regs);
}

static void noinstr __el0_error_handler_common(struct pt_regs *regs)
{
	unsigned long esr = read_sysreg(esr_el1);
	irqentry_state_t state;

	arm64_enter_from_user_mode(regs);
	local_daif_restore(DAIF_ERRCTX);
	state = irqentry_nmi_enter(regs);
	do_serror(regs, esr);
	irqentry_nmi_exit(regs, state);
	local_daif_restore(DAIF_PROCCTX);
	arm64_exit_to_user_mode(regs);
}

asmlinkage void noinstr el0t_64_error_handler(struct pt_regs *regs)
{
	__el0_error_handler_common(regs);
}

#ifdef CONFIG_COMPAT
static void noinstr el0_cp15(struct pt_regs *regs, unsigned long esr)
{
	arm64_enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_el0_cp15(esr, regs);
	arm64_exit_to_user_mode(regs);
}

static void noinstr el0_svc_compat(struct pt_regs *regs)
{
	arm64_enter_from_user_mode(regs);
	cortex_a76_erratum_1463225_svc_handler();
	local_daif_restore(DAIF_PROCCTX);
	do_el0_svc_compat(regs);
	arm64_exit_to_user_mode(regs);
}

static void noinstr el0_bkpt32(struct pt_regs *regs, unsigned long esr)
{
	arm64_enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_bkpt32(esr, regs);
	arm64_exit_to_user_mode(regs);
}

asmlinkage void noinstr el0t_32_sync_handler(struct pt_regs *regs)
{
	unsigned long esr = read_sysreg(esr_el1);

	switch (ESR_ELx_EC(esr)) {
	case ESR_ELx_EC_SVC32:
		el0_svc_compat(regs);
		break;
	case ESR_ELx_EC_DABT_LOW:
		el0_da(regs, esr);
		break;
	case ESR_ELx_EC_IABT_LOW:
		el0_ia(regs, esr);
		break;
	case ESR_ELx_EC_FP_ASIMD:
		el0_fpsimd_acc(regs, esr);
		break;
	case ESR_ELx_EC_FP_EXC32:
		el0_fpsimd_exc(regs, esr);
		break;
	case ESR_ELx_EC_PC_ALIGN:
		el0_pc(regs, esr);
		break;
	case ESR_ELx_EC_UNKNOWN:
	case ESR_ELx_EC_CP14_MR:
	case ESR_ELx_EC_CP14_LS:
	case ESR_ELx_EC_CP14_64:
		el0_undef(regs, esr);
		break;
	case ESR_ELx_EC_CP15_32:
	case ESR_ELx_EC_CP15_64:
		el0_cp15(regs, esr);
		break;
	case ESR_ELx_EC_BREAKPT_LOW:
		el0_breakpt(regs, esr);
		break;
	case ESR_ELx_EC_SOFTSTP_LOW:
		el0_softstp(regs, esr);
		break;
	case ESR_ELx_EC_WATCHPT_LOW:
		el0_watchpt(regs, esr);
		break;
	case ESR_ELx_EC_BKPT32:
		el0_bkpt32(regs, esr);
		break;
	default:
		el0_inv(regs, esr);
	}
}

asmlinkage void noinstr el0t_32_irq_handler(struct pt_regs *regs)
{
	__el0_irq_handler_common(regs);
}

asmlinkage void noinstr el0t_32_fiq_handler(struct pt_regs *regs)
{
	__el0_fiq_handler_common(regs);
}

asmlinkage void noinstr el0t_32_error_handler(struct pt_regs *regs)
{
	__el0_error_handler_common(regs);
}
#else /* CONFIG_COMPAT */
UNHANDLED(el0t, 32, sync)
UNHANDLED(el0t, 32, irq)
UNHANDLED(el0t, 32, fiq)
UNHANDLED(el0t, 32, error)
#endif /* CONFIG_COMPAT */

asmlinkage void noinstr __noreturn handle_bad_stack(struct pt_regs *regs)
{
	unsigned long esr = read_sysreg(esr_el1);
	unsigned long far = read_sysreg(far_el1);

	irqentry_nmi_enter(regs);
	panic_bad_stack(regs, esr, far);
}

#ifdef CONFIG_ARM_SDE_INTERFACE
asmlinkage noinstr unsigned long
__sdei_handler(struct pt_regs *regs, struct sdei_registered_event *arg)
{
	irqentry_state_t state;
	unsigned long ret;

	/*
	 * We didn't take an exception to get here, so the HW hasn't
	 * set/cleared bits in PSTATE that we may rely on.
	 *
	 * The original SDEI spec (ARM DEN 0054A) can be read ambiguously as to
	 * whether PSTATE bits are inherited unchanged or generated from
	 * scratch, and the TF-A implementation always clears PAN and always
	 * clears UAO. There are no other known implementations.
	 *
	 * Subsequent revisions (ARM DEN 0054B) follow the usual rules for how
	 * PSTATE is modified upon architectural exceptions, and so PAN is
	 * either inherited or set per SCTLR_ELx.SPAN, and UAO is always
	 * cleared.
	 *
	 * We must explicitly reset PAN to the expected state, including
	 * clearing it when the host isn't using it, in case a VM had it set.
	 */
	if (system_uses_hw_pan())
		set_pstate_pan(1);
	else if (cpu_has_pan())
		set_pstate_pan(0);

	state = irqentry_nmi_enter(regs);
	ret = do_sdei_event(regs, arg);
	irqentry_nmi_exit(regs, state);

	return ret;
}
#endif /* CONFIG_ARM_SDE_INTERFACE */
