/*
 * Copyright 2009 Freescale Semiconductor, Inc.
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
#define PPC_DBELL_TYPE(x)	(((x) & 0xf) << (63-36))
#define PPC_DBELL_TYPE_MASK	PPC_DBELL_TYPE(0xf)
#define PPC_DBELL_LPID(x)	((x) << (63 - 49))
#define PPC_DBELL_PIR_MASK	0x3fff
enum ppc_dbell {
	PPC_DBELL = 0,		/* doorbell */
	PPC_DBELL_CRIT = 1,	/* critical doorbell */
	PPC_G_DBELL = 2,	/* guest doorbell */
	PPC_G_DBELL_CRIT = 3,	/* guest critical doorbell */
	PPC_G_DBELL_MC = 4,	/* guest mcheck doorbell */
	PPC_DBELL_SERVER = 5,	/* doorbell on server */
};

#ifdef CONFIG_PPC_BOOK3S

#define PPC_DBELL_MSGTYPE		PPC_DBELL_SERVER
#define SPRN_DOORBELL_CPUTAG		SPRN_TIR
#define PPC_DBELL_TAG_MASK		0x7f

static inline void _ppc_msgsnd(u32 msg)
{
	if (cpu_has_feature(CPU_FTR_HVMODE))
		__asm__ __volatile__ (PPC_MSGSND(%0) : : "r" (msg));
	else
		__asm__ __volatile__ (PPC_MSGSNDP(%0) : : "r" (msg));
}

#else /* CONFIG_PPC_BOOK3S */

#define PPC_DBELL_MSGTYPE		PPC_DBELL
#define SPRN_DOORBELL_CPUTAG		SPRN_PIR
#define PPC_DBELL_TAG_MASK		0x3fff

static inline void _ppc_msgsnd(u32 msg)
{
	__asm__ __volatile__ (PPC_MSGSND(%0) : : "r" (msg));
}

#endif /* CONFIG_PPC_BOOK3S */

extern void doorbell_cause_ipi(int cpu, unsigned long data);
extern void doorbell_exception(struct pt_regs *regs);
extern void doorbell_setup_this_cpu(void);

static inline void ppc_msgsnd(enum ppc_dbell type, u32 flags, u32 tag)
{
	u32 msg = PPC_DBELL_TYPE(type) | (flags & PPC_DBELL_MSG_BRDCAST) |
			(tag & 0x07ffffff);

	_ppc_msgsnd(msg);
}

#endif /* _ASM_POWERPC_DBELL_H */
