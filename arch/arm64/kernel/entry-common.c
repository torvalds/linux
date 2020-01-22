// SPDX-License-Identifier: GPL-2.0
/*
 * Exception handling code
 *
 * Copyright (C) 2019 ARM Ltd.
 */

#include <linux/context_tracking.h>
#include <linux/ptrace.h>
#include <linux/thread_info.h>

#include <asm/cpufeature.h>
#include <asm/daifflags.h>
#include <asm/esr.h>
#include <asm/exception.h>
#include <asm/kprobes.h>
#include <asm/mmu.h>
#include <asm/sysreg.h>

static void notrace el1_abort(struct pt_regs *regs, unsigned long esr)
{
	unsigned long far = read_sysreg(far_el1);

	local_daif_inherit(regs);
	far = untagged_addr(far);
	do_mem_abort(far, esr, regs);
}
NOKPROBE_SYMBOL(el1_abort);

static void notrace el1_pc(struct pt_regs *regs, unsigned long esr)
{
	unsigned long far = read_sysreg(far_el1);

	local_daif_inherit(regs);
	do_sp_pc_abort(far, esr, regs);
}
NOKPROBE_SYMBOL(el1_pc);

static void notrace el1_undef(struct pt_regs *regs)
{
	local_daif_inherit(regs);
	do_undefinstr(regs);
}
NOKPROBE_SYMBOL(el1_undef);

static void notrace el1_inv(struct pt_regs *regs, unsigned long esr)
{
	local_daif_inherit(regs);
	bad_mode(regs, 0, esr);
}
NOKPROBE_SYMBOL(el1_inv);

static void notrace el1_dbg(struct pt_regs *regs, unsigned long esr)
{
	unsigned long far = read_sysreg(far_el1);

	/*
	 * The CPU masked interrupts, and we are leaving them masked during
	 * do_debug_exception(). Update PMR as if we had called
	 * local_mask_daif().
	 */
	if (system_uses_irq_prio_masking())
		gic_write_pmr(GIC_PRIO_IRQON | GIC_PRIO_PSR_I_SET);

	do_debug_exception(far, esr, regs);
}
NOKPROBE_SYMBOL(el1_dbg);

asmlinkage void notrace el1_sync_handler(struct pt_regs *regs)
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
	default:
		el1_inv(regs, esr);
	};
}
NOKPROBE_SYMBOL(el1_sync_handler);

static void notrace el0_da(struct pt_regs *regs, unsigned long esr)
{
	unsigned long far = read_sysreg(far_el1);

	user_exit_irqoff();
	local_daif_restore(DAIF_PROCCTX);
	far = untagged_addr(far);
	do_mem_abort(far, esr, regs);
}
NOKPROBE_SYMBOL(el0_da);

static void notrace el0_ia(struct pt_regs *regs, unsigned long esr)
{
	unsigned long far = read_sysreg(far_el1);

	/*
	 * We've taken an instruction abort from userspace and not yet
	 * re-enabled IRQs. If the address is a kernel address, apply
	 * BP hardening prior to enabling IRQs and pre-emption.
	 */
	if (!is_ttbr0_addr(far))
		arm64_apply_bp_hardening();

	user_exit_irqoff();
	local_daif_restore(DAIF_PROCCTX);
	do_mem_abort(far, esr, regs);
}
NOKPROBE_SYMBOL(el0_ia);

static void notrace el0_fpsimd_acc(struct pt_regs *regs, unsigned long esr)
{
	user_exit_irqoff();
	local_daif_restore(DAIF_PROCCTX);
	do_fpsimd_acc(esr, regs);
}
NOKPROBE_SYMBOL(el0_fpsimd_acc);

static void notrace el0_sve_acc(struct pt_regs *regs, unsigned long esr)
{
	user_exit_irqoff();
	local_daif_restore(DAIF_PROCCTX);
	do_sve_acc(esr, regs);
}
NOKPROBE_SYMBOL(el0_sve_acc);

static void notrace el0_fpsimd_exc(struct pt_regs *regs, unsigned long esr)
{
	user_exit_irqoff();
	local_daif_restore(DAIF_PROCCTX);
	do_fpsimd_exc(esr, regs);
}
NOKPROBE_SYMBOL(el0_fpsimd_exc);

static void notrace el0_sys(struct pt_regs *regs, unsigned long esr)
{
	user_exit_irqoff();
	local_daif_restore(DAIF_PROCCTX);
	do_sysinstr(esr, regs);
}
NOKPROBE_SYMBOL(el0_sys);

static void notrace el0_pc(struct pt_regs *regs, unsigned long esr)
{
	unsigned long far = read_sysreg(far_el1);

	if (!is_ttbr0_addr(instruction_pointer(regs)))
		arm64_apply_bp_hardening();

	user_exit_irqoff();
	local_daif_restore(DAIF_PROCCTX);
	do_sp_pc_abort(far, esr, regs);
}
NOKPROBE_SYMBOL(el0_pc);

static void notrace el0_sp(struct pt_regs *regs, unsigned long esr)
{
	user_exit_irqoff();
	local_daif_restore(DAIF_PROCCTX_NOIRQ);
	do_sp_pc_abort(regs->sp, esr, regs);
}
NOKPROBE_SYMBOL(el0_sp);

static void notrace el0_undef(struct pt_regs *regs)
{
	user_exit_irqoff();
	local_daif_restore(DAIF_PROCCTX);
	do_undefinstr(regs);
}
NOKPROBE_SYMBOL(el0_undef);

static void notrace el0_inv(struct pt_regs *regs, unsigned long esr)
{
	user_exit_irqoff();
	local_daif_restore(DAIF_PROCCTX);
	bad_el0_sync(regs, 0, esr);
}
NOKPROBE_SYMBOL(el0_inv);

static void notrace el0_dbg(struct pt_regs *regs, unsigned long esr)
{
	/* Only watchpoints write FAR_EL1, otherwise its UNKNOWN */
	unsigned long far = read_sysreg(far_el1);

	if (system_uses_irq_prio_masking())
		gic_write_pmr(GIC_PRIO_IRQON | GIC_PRIO_PSR_I_SET);

	user_exit_irqoff();
	do_debug_exception(far, esr, regs);
	local_daif_restore(DAIF_PROCCTX_NOIRQ);
}
NOKPROBE_SYMBOL(el0_dbg);

static void notrace el0_svc(struct pt_regs *regs)
{
	if (system_uses_irq_prio_masking())
		gic_write_pmr(GIC_PRIO_IRQON | GIC_PRIO_PSR_I_SET);

	do_el0_svc(regs);
}
NOKPROBE_SYMBOL(el0_svc);

asmlinkage void notrace el0_sync_handler(struct pt_regs *regs)
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
	case ESR_ELx_EC_BREAKPT_LOW:
	case ESR_ELx_EC_SOFTSTP_LOW:
	case ESR_ELx_EC_WATCHPT_LOW:
	case ESR_ELx_EC_BRK64:
		el0_dbg(regs, esr);
		break;
	default:
		el0_inv(regs, esr);
	}
}
NOKPROBE_SYMBOL(el0_sync_handler);

#ifdef CONFIG_COMPAT
static void notrace el0_cp15(struct pt_regs *regs, unsigned long esr)
{
	user_exit_irqoff();
	local_daif_restore(DAIF_PROCCTX);
	do_cp15instr(esr, regs);
}
NOKPROBE_SYMBOL(el0_cp15);

static void notrace el0_svc_compat(struct pt_regs *regs)
{
	if (system_uses_irq_prio_masking())
		gic_write_pmr(GIC_PRIO_IRQON | GIC_PRIO_PSR_I_SET);

	do_el0_svc_compat(regs);
}
NOKPROBE_SYMBOL(el0_svc_compat);

asmlinkage void notrace el0_sync_compat_handler(struct pt_regs *regs)
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
NOKPROBE_SYMBOL(el0_sync_compat_handler);
#endif /* CONFIG_COMPAT */
