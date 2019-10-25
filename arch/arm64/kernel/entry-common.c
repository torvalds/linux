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

static void el1_undef(struct pt_regs *regs)
{
	local_daif_inherit(regs);
	do_undefinstr(regs);
}
NOKPROBE_SYMBOL(el1_undef);

static void el1_inv(struct pt_regs *regs, unsigned long esr)
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
