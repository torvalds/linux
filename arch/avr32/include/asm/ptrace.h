/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_PTRACE_H
#define __ASM_AVR32_PTRACE_H

#include <uapi/asm/ptrace.h>

#ifndef __ASSEMBLY__

#include <asm/ocd.h>

#define arch_has_single_step()		(1)

#define arch_ptrace_attach(child)       ocd_enable(child)

#define user_mode(regs)                 (((regs)->sr & MODE_MASK) == MODE_USER)
#define instruction_pointer(regs)       ((regs)->pc)
#define profile_pc(regs)                instruction_pointer(regs)

static __inline__ int valid_user_regs(struct pt_regs *regs)
{
	/*
	 * Some of the Java bits might be acceptable if/when we
	 * implement some support for that stuff...
	 */
	if ((regs->sr & 0xffff0000) == 0)
		return 1;

	/*
	 * Force status register flags to be sane and report this
	 * illegal behaviour...
	 */
	regs->sr &= 0x0000ffff;
	return 0;
}


#endif /* ! __ASSEMBLY__ */
#endif /* __ASM_AVR32_PTRACE_H */
