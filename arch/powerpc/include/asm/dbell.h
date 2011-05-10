/*
 * Copyright 2009 Freescale Semicondutor, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * provides masks and opcode images for use by code generation, emulation
 * and for instructions that older assemblers might not know about
 */
#ifndef _ASM_POWERPC_DBELL_H
#define _ASM_POWERPC_DBELL_H

#include <linux/smp.h>
#include <linux/threads.h>

#include <asm/ppc-opcode.h>

#define PPC_DBELL_MSG_BRDCAST	(0x04000000)
#define PPC_DBELL_TYPE(x)	(((x) & 0xf) << 28)
enum ppc_dbell {
	PPC_DBELL = 0,		/* doorbell */
	PPC_DBELL_CRIT = 1,	/* critical doorbell */
	PPC_G_DBELL = 2,	/* guest doorbell */
	PPC_G_DBELL_CRIT = 3,	/* guest critical doorbell */
	PPC_G_DBELL_MC = 4,	/* guest mcheck doorbell */
};

extern void doorbell_cause_ipi(int cpu, unsigned long data);
extern void doorbell_exception(struct pt_regs *regs);
extern void doorbell_setup_this_cpu(void);

static inline void ppc_msgsnd(enum ppc_dbell type, u32 flags, u32 tag)
{
	u32 msg = PPC_DBELL_TYPE(type) | (flags & PPC_DBELL_MSG_BRDCAST) |
			(tag & 0x07ffffff);

	__asm__ __volatile__ (PPC_MSGSND(%0) : : "r" (msg));
}

#endif /* _ASM_POWERPC_DBELL_H */
