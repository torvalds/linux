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
#include <asm/sections.h>

struct pt_regs;

struct undef_hook {
	struct list_head node;
	u32 instr_mask;
	u32 instr_val;
	u64 pstate_mask;
	u64 pstate_val;
	int (*fn)(struct pt_regs *regs, u32 instr);
};

void register_undef_hook(struct undef_hook *hook);
void unregister_undef_hook(struct undef_hook *hook);
void force_signal_inject(int signal, int code, unsigned long address);
void arm64_notify_segfault(unsigned long addr);
void arm64_force_sig_fault(int signo, int code, void __user *addr, const char *str);
void arm64_force_sig_mceerr(int code, void __user *addr, short lsb, const char *str);
void arm64_force_sig_ptrace_errno_trap(int errno, void __user *addr, const char *str);

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

static inline int in_exception_text(unsigned long ptr)
{
	int in;

	in = ptr >= (unsigned long)&__exception_text_start &&
	     ptr < (unsigned long)&__exception_text_end;

	return in ? : __in_irqentry_text(ptr);
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
static inline bool arm64_is_ras_serror(u32 esr)
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
static inline u32 arm64_ras_serror_get_severity(u32 esr)
{
	u32 aet = esr & ESR_ELx_AET;

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

bool arm64_is_fatal_ras_serror(struct pt_regs *regs, unsigned int esr);
void __noreturn arm64_serror_panic(struct pt_regs *regs, u32 esr);
#endif
