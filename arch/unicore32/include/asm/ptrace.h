/*
 * linux/arch/unicore32/include/asm/ptrace.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __UNICORE_PTRACE_H__
#define __UNICORE_PTRACE_H__

#include <uapi/asm/ptrace.h>

#ifndef __ASSEMBLY__

#define user_mode(regs)	\
	(processor_mode(regs) == USER_MODE)

#define processor_mode(regs) \
	((regs)->UCreg_asr & MODE_MASK)

#define interrupts_enabled(regs) \
	(!((regs)->UCreg_asr & PSR_I_BIT))

#define fast_interrupts_enabled(regs) \
	(!((regs)->UCreg_asr & PSR_R_BIT))

/* Are the current registers suitable for user mode?
 * (used to maintain security in signal handlers)
 */
static inline int valid_user_regs(struct pt_regs *regs)
{
	unsigned long mode = regs->UCreg_asr & MODE_MASK;

	/*
	 * Always clear the R (REAL) bits
	 */
	regs->UCreg_asr &= ~(PSR_R_BIT);

	if ((regs->UCreg_asr & PSR_I_BIT) == 0) {
		if (mode == USER_MODE)
			return 1;
	}

	/*
	 * Force ASR to something logical...
	 */
	regs->UCreg_asr &= PSR_f | USER_MODE;

	return 0;
}

#define instruction_pointer(regs)	((regs)->UCreg_pc)
#define user_stack_pointer(regs)	((regs)->UCreg_sp)
#define profile_pc(regs)		instruction_pointer(regs)

#endif /* __ASSEMBLY__ */
#endif
