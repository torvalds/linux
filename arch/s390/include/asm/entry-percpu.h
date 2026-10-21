/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_S390_ENTRY_PERCPU_H
#define ARCH_S390_ENTRY_PERCPU_H

#include <linux/bitfield.h>
#include <linux/percpu.h>
#include <asm/lowcore.h>
#include <asm/ptrace.h>

static __always_inline void percpu_entry(struct pt_regs *regs)
{
	struct lowcore *lc = get_lowcore();

	regs->cpu = lc->cpu_nr;
	regs->percpu_register = lc->percpu_register;
	lc->percpu_register = 0;
}

static __always_inline void percpu_exit(struct pt_regs *regs)
{
	unsigned char regval, regpcp, regoff, regptr;
	struct lowcore *lc = get_lowcore();

	if (!regs->percpu_register)
		return;
	regval = regs->percpu_register;
	lc->percpu_register = regval;
	/* Migrated to a different CPU? */
	if (regs->cpu == lc->cpu_nr)
		return;
	regpcp = FIELD_GET(PCPU_REG_PCP, regval);
	regoff = FIELD_GET(PCPU_REG_OFF, regval);
	regptr = regoff + 1;
	/*
	 * Update register 'regoff' with the current CPU's percpu offset,
	 * and recalculate and update current CPU's percpu variable address
	 * contained in register 'regptr'.
	 */
	regs->gprs[regoff] = lc->percpu_offset;
	regs->gprs[regptr] = regs->gprs[regpcp] + regs->gprs[regoff];
}

#endif
