// SPDX-License-Identifier: GPL-2.0
/*
 * Exception handling code
 *
 * Copyright (C) 2019 ARM Ltd.
 */

#include <linux/context_tracking.h>
#include <linux/linkage.h>
#include <linux/lockdep.h>
#include <linux/ptrace.h>
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
 * intrumentable code, or any code which may trigger an exception.
 *
 * This is intended to match the logic in irqentry_enter(), handling the kernel
 * mode transitions only.
 */
static __always_inline void __enter_from_kernel_mode(struct pt_regs *regs)
{
	regs->exit_rcu = false;

	if (!IS_ENABLED(CONFIG_TINY_RCU) && is_idle_task(current)) {
		lockdep_hardirqs_off(CALLER_ADDR0);
		rcu_irq_enter();
		trace_hardirqs_off_finish();

		regs->exit_rcu = true;
		return;
	}

	lockdep_hardirqs_off(CALLER_ADDR0);
	rcu_irq_enter_check_tick();
	trace_hardirqs_off_finish();
}

static void noinstr enter_from_kernel_mode(struct pt_regs *regs)
{
	__enter_from_kernel_mode(regs);
	mte_check_tfsr_entry();
}

/*
 * Handle IRQ/context state management when exiting to kernel mode.
 * After this function returns it is not safe to call regular kernel code,
 * intrumentable code, or any code which may trigger an exception.
 *
 * This is intended to match the logic in irqentry_exit(), handling the kernel
 * mode transitions only, and with preemption handled elsewhere.
 */
static __always_inline void __exit_to_kernel_mode(struct pt_regs *regs)
{
	lockdep_assert_irqs_disabled();

	if (interrupts_enabled(regs)) {
		if (regs->exit_rcu) {
			trace_hardirqs_on_prepare();
			lockdep_hardirqs_on_prepare(CALLER_ADDR0);
			rcu_irq_exit();
			lockdep_hardirqs_on(CALLER_ADDR0);
			return;
		}

		trace_hardirqs_on();
	} else {
		if (regs->exit_rcu)
			rcu_irq_exit();
	}
}

static void noinstr exit_to_kernel_mode(struct pt_regs *regs)
{
	mte_check_tfsr_exit();
	__exit_to_kernel_mode(regs);
}

/*
 * Handle IRQ/context state management when entering from user mode.
 * Before this function is called it is not safe to call regular kernel code,
 * intrumentable code, or any code which may trigger an exception.
 */
static __always_inline void __enter_from_user_mode(void)
{
	lockdep_hardirqs_off(CALLER_ADDR0);
	CT_WARN_ON(ct_state() != CONTEXT_USER);
	user_exit_irqoff();
	trace_hardirqs_off_finish();
}

static __always_inline void enter_from_user_mode(struct pt_regs *regs)
{
	__enter_from_user_mode();
}

/*
 * Handle IRQ/context state management when exiting to user mode.
 * After this function returns it is not safe to call regular kernel code,
 * intrumentable code, or any code which may trigger an exception.
 */
static __always_inline void __exit_to_user_mode(void)
{
	trace_hardirqs_on_prepare();
	lockdep_hardirqs_on_prepare(CALLER_ADDR0);
	user_enter_irqoff();
	lockdep_hardirqs_on(CALLER_ADDR0);
}

static __always_inline void prepare_exit_to_user_mode(struct pt_regs *regs)
{
	unsigned long flags;

	local_daif_mask();

	flags = read_thread_flags();
	if (unlikely(flags & _TIF_WORK_MASK))
		do_notify_resume(regs, flags);
}

static __always_inline void exit_to_user_mode(struct pt_regs *regs)
{
	prepare_exit_to_user_mode(regs);
	mte_check_tfsr_exit();
	__exit_to_user_mode();
}

asmlinkage void noinstr asm_exit_to_user_mode(struct pt_regs *regs)
{
	exit_to_user_mode(regs);
}

/*
 * Handle IRQ/context state management when entering an NMI from user/kernel
 * mode. Before this function is called it is not safe to call regular kernel
 * code, intrumentable code, or any code which may trigger an exception.
 */
static void noinstr arm64_enter_nmi(struct pt_regs *regs)
{
	regs->lockdep_hardirqs = lockdep_hardirqs_enabled();

	__nmi_enter();
	lockdep_hardirqs_off(CALLER_ADDR0);
	lockdep_hardirq_enter();
	rcu_nmi_enter();

	trace_hardirqs_off_finish();
	ftrace_nmi_enter();
}

/*
 * Handle IRQ/context state management when exiting an NMI from user/kernel
 * mode. After this function returns it is not safe to call regular kernel
 * code, intrumentable code, or any code which may trigger an exception.
 */
static void noinstr arm64_exit_nmi(struct pt_regs *regs)
{
	bool restore = regs->lockdep_hardirqs;

	ftrace_nmi_exit();
	if (restore) {
		trace_hardirqs_on_prepare();
		lockdep_hardirqs_on_prepare(CALLER_ADDR0);
	}

	rcu_nmi_exit();
	lockdep_hardirq_exit();
	if (restore)
		lockdep_hardirqs_on(CALLER_ADDR0);
	__nmi_exit();
}

/*
 * Handle IRQ/context state management when entering a debug exception from
 * kernel mode. Before this function is called it is not safe to call regular
 * kernel code, intrumentable code, or any code which may trigger an exception.
 */
static void noinstr arm64_enter_el1_dbg(struct pt_regs *regs)
{
	regs->lockdep_hardirqs = lockdep_hardirqs_enabled();

	lockdep_hardirqs_off(CALLER_ADDR0);
	rcu_nmi_enter();

	trace_hardirqs_off_finish();
}

/*
 * Handle IRQ/context state management when exiting a debug exception from
 * kernel mode. After this function returns it is not safe to call regular
 * kernel code, intrumentable code, or any code which may trigger an exception.
 */
static void noinstr arm64_exit_el1_dbg(struct pt_regs *regs)
{
	bool restore = regs->lockdep_hardirqs;

	if (restore) {
		trace_hardirqs_on_prepare();
		lockdep_hardirqs_on_prepare(CALLER_ADDR0);
	}

	rcu_nmi_exit();
	if (restore)
		lockdep_hardirqs_on(CALLER_ADDR0);
}

#ifdef CONFIG_PREEMPT_DYNAMIC
DEFINE_STATIC_KEY_TRUE(sk_dynamic_irqentry_exit_cond_resched);
#define need_irq_preemption() \
	(static_branch_unlikely(&sk_dynamic_irqentry_exit_cond_resched))
#else
#define need_irq_preemption()	(IS_ENABLED(CONFIG_PREEMPTION))
#endif

static void __sched arm64_preempt_schedule_irq(void)
{
	if (!need_irq_preemption())
		return;

	/*
	 * Note: thread_info::preempt_count includes both thread_info::count
	 * and thread_info::need_resched, and is not equivalent to
	 * preempt_count().
	 */
	if (READ_ONCE(current_thread_info()->preempt_count) != 0)
		return;

	/*
	 * DAIF.DA are cleared at the start of IRQ/FIQ handling, and when GIC
	 * priority masking is used the GIC irqchip driver will clear DAIF.IF
	 * using gic_arch_enable_irqs() for normal IRQs. If anything is set in
	 * DAIF we must have handled an NMI, so skip preemption.
	 */
	if (system_uses_irq_prio_masking() && read_sysreg(daif))
		return;

	/*
	 * Preempting a task from an IRQ means we leave copies of PSTATE
	 * on the stack. cpufeature's enable calls may modify PSTATE, but
	 * resuming one of these preempted tasks would undo those changes.
	 *
	 * Only allow a task to be preempted once cpufeatures have been
	 * enabled.
	 */
	if (system_capabilities_finalized())
		preempt_schedule_irq();
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
				      unsigned int esr)
{
	arm64_enter_nmi(regs);

	console_verbose();

	pr_crit("Unhandled %s exception on CPU%d, ESR 0x%08x -- %s\n",
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

static bool cortex_a76_erratum_1463225_debug_handler(struct pt_regs *regs)
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

UNHANDLED(el1t, 64, sync)
UNHANDLED(el1t, 64, irq)
UNHANDLED(el1t, 64, fiq)
UNHANDLED(el1t, 64, error)

static void noinstr el1_abort(struct pt_regs *regs, unsigned long esr)
{
	unsigned long far = read_sysreg(far_el1);

	enter_from_kernel_mode(regs);
	local_daif_inherit(regs);
	do_mem_abort(far, esr, regs);
	local_daif_mask();
	exit_to_kernel_mode(regs);
}

static void noinstr el1_pc(struct pt_regs *regs, unsigned long esr)
{
	unsigned long far = read_sysreg(far_el1);

	enter_from_kernel_mode(regs);
	local_daif_inherit(regs);
	do_sp_pc_abort(far, esr, regs);
	local_daif_mask();
	exit_to_kernel_mode(regs);
}

static void noinstr el1_undef(struct pt_regs *regs)
{
	enter_from_kernel_mode(regs);
	local_daif_inherit(regs);
	do_undefinstr(regs);
	local_daif_mask();
	exit_to_kernel_mode(regs);
}

static void noinstr el1_dbg(struct pt_regs *regs, unsigned long esr)
{
	unsigned long far = read_sysreg(far_el1);

	arm64_enter_el1_dbg(regs);
	if (!cortex_a76_erratum_1463225_debug_handler(regs))
		do_debug_exception(far, esr, regs);
	arm64_exit_el1_dbg(regs);
}

static void noinstr el1_fpac(struct pt_regs *regs, unsigned long esr)
{
	enter_from_kernel_mode(regs);
	local_daif_inherit(regs);
	do_ptrauth_fault(regs, esr);
	local_daif_mask();
	exit_to_kernel_mode(regs);
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
		el1_undef(regs);
		break;
	case ESR_ELx_EC_BREAKPT_CUR:
	case ESR_ELx_EC_SOFTSTP_CUR:
	case ESR_ELx_EC_WATCHPT_CUR:
	case ESR_ELx_EC_BRK64:
		el1_dbg(regs, esr);
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
	arm64_enter_nmi(regs);
	do_interrupt_handler(regs, handler);
	arm64_exit_nmi(regs);
}

static __always_inline void __el1_irq(struct pt_regs *regs,
				      void (*handler)(struct pt_regs *))
{
	enter_from_kernel_mode(regs);

	irq_enter_rcu();
	do_interrupt_handler(regs, handler);
	irq_exit_rcu();

	arm64_preempt_schedule_irq();

	exit_to_kernel_mode(regs);
}
static void noinstr el1_interrupt(struct pt_regs *regs,
				  void (*handler)(struct pt_regs *))
{
	write_sysreg(DAIF_PROCCTX_NOIRQ, daif);

	if (IS_ENABLED(CONFIG_ARM64_PSEUDO_NMI) && !interrupts_enabled(regs))
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

	local_daif_restore(DAIF_ERRCTX);
	arm64_enter_nmi(regs);
	do_serror(regs, esr);
	arm64_exit_nmi(regs);
}

static void noinstr el0_da(struct pt_regs *regs, unsigned long esr)
{
	unsigned long far = read_sysreg(far_el1);

	enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_mem_abort(far, esr, regs);
	exit_to_user_mode(regs);
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

	enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_mem_abort(far, esr, regs);
	exit_to_user_mode(regs);
}

static void noinstr el0_fpsimd_acc(struct pt_regs *regs, unsigned long esr)
{
	enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_fpsimd_acc(esr, regs);
	exit_to_user_mode(regs);
}

static void noinstr el0_sve_acc(struct pt_regs *regs, unsigned long esr)
{
	enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_sve_acc(esr, regs);
	exit_to_user_mode(regs);
}

static void noinstr el0_fpsimd_exc(struct pt_regs *regs, unsigned long esr)
{
	enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_fpsimd_exc(esr, regs);
	exit_to_user_mode(regs);
}

static void noinstr el0_sys(struct pt_regs *regs, unsigned long esr)
{
	enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_sysinstr(esr, regs);
	exit_to_user_mode(regs);
}

static void noinstr el0_pc(struct pt_regs *regs, unsigned long esr)
{
	unsigned long far = read_sysreg(far_el1);

	if (!is_ttbr0_addr(instruction_pointer(regs)))
		arm64_apply_bp_hardening();

	enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_sp_pc_abort(far, esr, regs);
	exit_to_user_mode(regs);
}

static void noinstr el0_sp(struct pt_regs *regs, unsigned long esr)
{
	enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_sp_pc_abort(regs->sp, esr, regs);
	exit_to_user_mode(regs);
}

static void noinstr el0_undef(struct pt_regs *regs)
{
	enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_undefinstr(regs);
	exit_to_user_mode(regs);
}

static void noinstr el0_bti(struct pt_regs *regs)
{
	enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_bti(regs);
	exit_to_user_mode(regs);
}

static void noinstr el0_inv(struct pt_regs *regs, unsigned long esr)
{
	enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	bad_el0_sync(regs, 0, esr);
	exit_to_user_mode(regs);
}

static void noinstr el0_dbg(struct pt_regs *regs, unsigned long esr)
{
	/* Only watchpoints write FAR_EL1, otherwise its UNKNOWN */
	unsigned long far = read_sysreg(far_el1);

	enter_from_user_mode(regs);
	do_debug_exception(far, esr, regs);
	local_daif_restore(DAIF_PROCCTX);
	exit_to_user_mode(regs);
}

static void noinstr el0_svc(struct pt_regs *regs)
{
	enter_from_user_mode(regs);
	cortex_a76_erratum_1463225_svc_handler();
	do_el0_svc(regs);
	exit_to_user_mode(regs);
}

static void noinstr el0_fpac(struct pt_regs *regs, unsigned long esr)
{
	enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_ptrauth_fault(regs, esr);
	exit_to_user_mode(regs);
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
		el0_undef(regs);
		break;
	case ESR_ELx_EC_BTI:
		el0_bti(regs);
		break;
	case ESR_ELx_EC_BREAKPT_LOW:
	case ESR_ELx_EC_SOFTSTP_LOW:
	case ESR_ELx_EC_WATCHPT_LOW:
	case ESR_ELx_EC_BRK64:
		el0_dbg(regs, esr);
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
	enter_from_user_mode(regs);

	write_sysreg(DAIF_PROCCTX_NOIRQ, daif);

	if (regs->pc & BIT(55))
		arm64_apply_bp_hardening();

	irq_enter_rcu();
	do_interrupt_handler(regs, handler);
	irq_exit_rcu();

	exit_to_user_mode(regs);
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

	enter_from_user_mode(regs);
	local_daif_restore(DAIF_ERRCTX);
	arm64_enter_nmi(regs);
	do_serror(regs, esr);
	arm64_exit_nmi(regs);
	local_daif_restore(DAIF_PROCCTX);
	exit_to_user_mode(regs);
}

asmlinkage void noinstr el0t_64_error_handler(struct pt_regs *regs)
{
	__el0_error_handler_common(regs);
}

#ifdef CONFIG_COMPAT
static void noinstr el0_cp15(struct pt_regs *regs, unsigned long esr)
{
	enter_from_user_mode(regs);
	local_daif_restore(DAIF_PROCCTX);
	do_cp15instr(esr, regs);
	exit_to_user_mode(regs);
}

static void noinstr el0_svc_compat(struct pt_regs *regs)
{
	enter_from_user_mode(regs);
	cortex_a76_erratum_1463225_svc_handler();
	do_el0_svc_compat(regs);
	exit_to_user_mode(regs);
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
		el0_undef(regs);
		break;
	case ESR_ELx_EC_CP15_32:
	case ESR_ELx_EC_CP15_64:
		el0_cp15(regs, esr);
		break;
	case ESR_ELx_EC_BREAKPT_LOW:
	case ESR_ELx_EC_SOFTSTP_LOW:
	case ESR_ELx_EC_WATCHPT_LOW:
	case ESR_ELx_EC_BKPT32:
		el0_dbg(regs, esr);
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

#ifdef CONFIG_VMAP_STACK
asmlinkage void noinstr handle_bad_stack(struct pt_regs *regs)
{
	unsigned int esr = read_sysreg(esr_el1);
	unsigned long far = read_sysreg(far_el1);

	arm64_enter_nmi(regs);
	panic_bad_stack(regs, esr, far);
}
#endif /* CONFIG_VMAP_STACK */

#ifdef CONFIG_ARM_SDE_INTERFACE
asmlinkage noinstr unsigned long
__sdei_handler(struct pt_regs *regs, struct sdei_registered_event *arg)
{
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

	arm64_enter_nmi(regs);
	ret = do_sdei_event(regs, arg);
	arm64_exit_nmi(regs);

	return ret;
}
#endif /* CONFIG_ARM_SDE_INTERFACE */
