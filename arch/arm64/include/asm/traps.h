/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/include/asm/traps.h
 *
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_TRAP_H
#define __ASM_TRAP_H

#include <linux/list.h>
#include <asm/esr.h>
#include <asm/ptrace.h>
#include <asm/sections.h>

#ifdef CONFIG_ARMV8_DEPRECATED
bool try_emulate_armv8_deprecated(struct pt_regs *regs, u32 insn);
#else
static inline bool
try_emulate_armv8_deprecated(struct pt_regs *regs, u32 insn)
{
	return false;
}
#endif /* CONFIG_ARMV8_DEPRECATED */

void force_signal_inject(int signal, int code, unsigned long address, unsigned long err);
void arm64_notify_segfault(unsigned long addr);
void arm64_force_sig_fault(int signo, int code, unsigned long far, const char *str);
void arm64_force_sig_mceerr(int code, unsigned long far, short lsb, const char *str);
void arm64_force_sig_ptrace_errno_trap(int errno, unsigned long far, const char *str);

int early_brk64(unsigned long addr, unsigned long esr, struct pt_regs *regs);

/*
 * Move regs->pc to next instruction and do necessary setup before it
 * is executed.
 */
void arm64_skip_faulting_instruction(struct pt_regs *regs, unsigned long size);

static inline int __in_irqentry_text(unsigned long ptr)
{
	return ptr >= (unsigned long)&__irqentry_text_start &&
	       ptr < (unsigned long)&__irqentry_text_end;
}

static inline int in_entry_text(unsigned long ptr)
{
	return ptr >= (unsigned long)&__entry_text_start &&
	       ptr < (unsigned long)&__entry_text_end;
}

/*
 * CPUs with the RAS extensions have an Implementation-Defined-Syndrome bit
 * to indicate whether this ESR has a RAS encoding. CPUs without this feature
 * have a ISS-Valid bit in the same position.
 * If this bit is set, we know its not a RAS SError.
 * If its clear, we need to know if the CPU supports RAS. Uncategorized RAS
 * errors share the same encoding as an all-zeros encoding from a CPU that
 * doesn't support RAS.
 */
static inline bool arm64_is_ras_serror(unsigned long esr)
{
	WARN_ON(preemptible());

	if (esr & ESR_ELx_IDS)
		return false;

	if (this_cpu_has_cap(ARM64_HAS_RAS_EXTN))
		return true;
	else
		return false;
}

/*
 * Return the AET bits from a RAS SError's ESR.
 *
 * It is implementation defined whether Uncategorized errors are containable.
 * We treat them as Uncontainable.
 * Non-RAS SError's are reported as Uncontained/Uncategorized.
 */
static inline unsigned long arm64_ras_serror_get_severity(unsigned long esr)
{
	unsigned long aet = esr & ESR_ELx_AET;

	if (!arm64_is_ras_serror(esr)) {
		/* Not a RAS error, we can't interpret the ESR. */
		return ESR_ELx_AET_UC;
	}

	/*
	 * AET is RES0 if 'the value returned in the DFSC field is not
	 * [ESR_ELx_FSC_SERROR]'
	 */
	if ((esr & ESR_ELx_FSC) != ESR_ELx_FSC_SERROR) {
		/* No severity information : Uncategorized */
		return ESR_ELx_AET_UC;
	}

	return aet;
}

bool arm64_is_fatal_ras_serror(struct pt_regs *regs, unsigned long esr);
void __noreturn arm64_serror_panic(struct pt_regs *regs, unsigned long esr);

static inline void arm64_mops_reset_regs(struct user_pt_regs *regs, unsigned long esr)
{
	bool wrong_option = esr & ESR_ELx_MOPS_ISS_WRONG_OPTION;
	bool option_a = esr & ESR_ELx_MOPS_ISS_OPTION_A;
	int dstreg = ESR_ELx_MOPS_ISS_DESTREG(esr);
	int srcreg = ESR_ELx_MOPS_ISS_SRCREG(esr);
	int sizereg = ESR_ELx_MOPS_ISS_SIZEREG(esr);
	unsigned long dst, src, size;

	dst = regs->regs[dstreg];
	src = regs->regs[srcreg];
	size = regs->regs[sizereg];

	/*
	 * Put the registers back in the original format suitable for a
	 * prologue instruction, using the generic return routine from the
	 * Arm ARM (DDI 0487I.a) rules CNTMJ and MWFQH.
	 */
	if (esr & ESR_ELx_MOPS_ISS_MEM_INST) {
		/* SET* instruction */
		if (option_a ^ wrong_option) {
			/* Format is from Option A; forward set */
			regs->regs[dstreg] = dst + size;
			regs->regs[sizereg] = -size;
		}
	} else {
		/* CPY* instruction */
		if (!(option_a ^ wrong_option)) {
			/* Format is from Option B */
			if (regs->pstate & PSR_N_BIT) {
				/* Backward copy */
				regs->regs[dstreg] = dst - size;
				regs->regs[srcreg] = src - size;
			}
		} else {
			/* Format is from Option A */
			if (size & BIT(63)) {
				/* Forward copy */
				regs->regs[dstreg] = dst + size;
				regs->regs[srcreg] = src + size;
				regs->regs[sizereg] = -size;
			}
		}
	}

	if (esr & ESR_ELx_MOPS_ISS_FROM_EPILOGUE)
		regs->pc -= 8;
	else
		regs->pc -= 4;
}
#endif
