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
#include <asm/cpu_has_feature.h>

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

static inline void _ppc_msgsnd(u32 msg)
{
	__asm__ __volatile__ (ASM_FTR_IFSET(PPC_MSGSND(%1), PPC_MSGSNDP(%1), %0)
				: : "i" (CPU_FTR_HVMODE), "r" (msg));
}

/* sync before sending message */
static inline void ppc_msgsnd_sync(void)
{
	__asm__ __volatile__ ("sync" : : : "memory");
}

/* sync after taking message interrupt */
static inline void ppc_msgsync(void)
{
	/* sync is not required when taking messages from the same core */
	__asm__ __volatile__ (ASM_FTR_IFSET(PPC_MSGSYNC " ; lwsync", "", %0)
				: : "i" (CPU_FTR_HVMODE|CPU_FTR_ARCH_300));
}

static inline void _ppc_msgclr(u32 msg)
{
	__asm__ __volatile__ (ASM_FTR_IFSET(PPC_MSGCLR(%1), PPC_MSGCLRP(%1), %0)
				: : "i" (CPU_FTR_HVMODE), "r" (msg));
}

static inline void ppc_msgclr(enum ppc_dbell type)
{
	u32 msg = PPC_DBELL_TYPE(type);

	_ppc_msgclr(msg);
}

#else /* CONFIG_PPC_BOOK3S */

#define PPC_DBELL_MSGTYPE		PPC_DBELL

static inline void _ppc_msgsnd(u32 msg)
{
	__asm__ __volatile__ (PPC_MSGSND(%0) : : "r" (msg));
}

/* sync before sending message */
static inline void ppc_msgsnd_sync(void)
{
	__asm__ __volatile__ ("sync" : : : "memory");
}

/* sync after taking message interrupt */
static inline void ppc_msgsync(void)
{
}

#endif /* CONFIG_PPC_BOOK3S */

extern void doorbell_global_ipi(int cpu);
extern void doorbell_core_ipi(int cpu);
extern int doorbell_try_core_ipi(int cpu);
extern void doorbell_exception(struct pt_regs *regs);

static inline void ppc_msgsnd(enum ppc_dbell type, u32 flags, u32 tag)
{
	u32 msg = PPC_DBELL_TYPE(type) | (flags & PPC_DBELL_MSG_BRDCAST) |
			(tag & 0x07ffffff);

	_ppc_msgsnd(msg);
}

#endif /* _ASM_POWERPC_DBELL_H */
