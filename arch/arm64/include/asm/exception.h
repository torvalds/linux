/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/include/asm/exception.h
 *
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_EXCEPTION_H
#define __ASM_EXCEPTION_H

#include <asm/esr.h>
#include <asm/kprobes.h>
#include <asm/ptrace.h>

#include <linux/interrupt.h>

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
#define __exception_irq_entry	__irq_entry
#else
#define __exception_irq_entry	__kprobes
#endif

static inline unsigned long disr_to_esr(u64 disr)
{
	unsigned long esr = ESR_ELx_EC_SERROR << ESR_ELx_EC_SHIFT;

	if ((disr & DISR_EL1_IDS) == 0)
		esr |= (disr & DISR_EL1_ESR_MASK);
	else
		esr |= (disr & ESR_ELx_ISS_MASK);

	return esr;
}

asmlinkage void handle_bad_stack(struct pt_regs *regs);

asmlinkage void el1t_64_sync_handler(struct pt_regs *regs);
asmlinkage void el1t_64_irq_handler(struct pt_regs *regs);
asmlinkage void el1t_64_fiq_handler(struct pt_regs *regs);
asmlinkage void el1t_64_error_handler(struct pt_regs *regs);

asmlinkage void el1h_64_sync_handler(struct pt_regs *regs);
asmlinkage void el1h_64_irq_handler(struct pt_regs *regs);
asmlinkage void el1h_64_fiq_handler(struct pt_regs *regs);
asmlinkage void el1h_64_error_handler(struct pt_regs *regs);

asmlinkage void el0t_64_sync_handler(struct pt_regs *regs);
asmlinkage void el0t_64_irq_handler(struct pt_regs *regs);
asmlinkage void el0t_64_fiq_handler(struct pt_regs *regs);
asmlinkage void el0t_64_error_handler(struct pt_regs *regs);

asmlinkage void el0t_32_sync_handler(struct pt_regs *regs);
asmlinkage void el0t_32_irq_handler(struct pt_regs *regs);
asmlinkage void el0t_32_fiq_handler(struct pt_regs *regs);
asmlinkage void el0t_32_error_handler(struct pt_regs *regs);

asmlinkage void call_on_irq_stack(struct pt_regs *regs,
				  void (*func)(struct pt_regs *));
asmlinkage void asm_exit_to_user_mode(struct pt_regs *regs);

void do_mem_abort(unsigned long far, unsigned long esr, struct pt_regs *regs);
void do_undefinstr(struct pt_regs *regs);
void do_bti(struct pt_regs *regs);
void do_debug_exception(unsigned long addr_if_watchpoint, unsigned long esr,
			struct pt_regs *regs);
void do_fpsimd_acc(unsigned long esr, struct pt_regs *regs);
void do_sve_acc(unsigned long esr, struct pt_regs *regs);
void do_sme_acc(unsigned long esr, struct pt_regs *regs);
void do_fpsimd_exc(unsigned long esr, struct pt_regs *regs);
void do_sysinstr(unsigned long esr, struct pt_regs *regs);
void do_sp_pc_abort(unsigned long addr, unsigned long esr, struct pt_regs *regs);
void bad_el0_sync(struct pt_regs *regs, int reason, unsigned long esr);
void do_cp15instr(unsigned long esr, struct pt_regs *regs);
void do_el0_svc(struct pt_regs *regs);
void do_el0_svc_compat(struct pt_regs *regs);
void do_ptrauth_fault(struct pt_regs *regs, unsigned long esr);
void do_serror(struct pt_regs *regs, unsigned long esr);
void do_notify_resume(struct pt_regs *regs, unsigned long thread_flags);

void panic_bad_stack(struct pt_regs *regs, unsigned long esr, unsigned long far);
#endif	/* __ASM_EXCEPTION_H */
