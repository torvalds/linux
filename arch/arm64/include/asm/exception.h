/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/include/asm/exception.h
 *
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_EXCEPTION_H
#define __ASM_EXCEPTION_H

#include <asm/esr.h>
#include <asm/ptrace.h>

#include <linux/interrupt.h>

#define __exception_irq_entry	__irq_entry

static inline u32 disr_to_esr(u64 disr)
{
	unsigned int esr = ESR_ELx_EC_SERROR << ESR_ELx_EC_SHIFT;

	if ((disr & DISR_EL1_IDS) == 0)
		esr |= (disr & DISR_EL1_ESR_MASK);
	else
		esr |= (disr & ESR_ELx_ISS_MASK);

	return esr;
}

asmlinkage void noinstr enter_el1_irq_or_nmi(struct pt_regs *regs);
asmlinkage void noinstr exit_el1_irq_or_nmi(struct pt_regs *regs);
asmlinkage void enter_from_user_mode(void);
asmlinkage void exit_to_user_mode(void);
void arm64_enter_nmi(struct pt_regs *regs);
void arm64_exit_nmi(struct pt_regs *regs);
void do_mem_abort(unsigned long addr, unsigned int esr, struct pt_regs *regs);
void do_undefinstr(struct pt_regs *regs);
void do_bti(struct pt_regs *regs);
asmlinkage void bad_mode(struct pt_regs *regs, int reason, unsigned int esr);
void do_debug_exception(unsigned long addr_if_watchpoint, unsigned int esr,
			struct pt_regs *regs);
void do_fpsimd_acc(unsigned int esr, struct pt_regs *regs);
void do_sve_acc(unsigned int esr, struct pt_regs *regs);
void do_fpsimd_exc(unsigned int esr, struct pt_regs *regs);
void do_sysinstr(unsigned int esr, struct pt_regs *regs);
void do_sp_pc_abort(unsigned long addr, unsigned int esr, struct pt_regs *regs);
void bad_el0_sync(struct pt_regs *regs, int reason, unsigned int esr);
void do_cp15instr(unsigned int esr, struct pt_regs *regs);
void do_el0_svc(struct pt_regs *regs);
void do_el0_svc_compat(struct pt_regs *regs);
void do_ptrauth_fault(struct pt_regs *regs, unsigned int esr);
#endif	/* __ASM_EXCEPTION_H */
