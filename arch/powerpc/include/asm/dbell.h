/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2009 Freescale Semiconductor, Inc.
 *
 * provides masks and opcode images for use by code generation, emulation
 * and for instructions that older assemblers might not know about
 */
#ifndef _ASM_POWERPC_DBELL_H
#define _ASM_POWERPC_DBELL_H

#include <linux/smp.h>
#include <linux/threads.h>

#include <asm/cputhreads.h>
#include <asm/ppc-opcode.h>
#include <asm/feature-fixups.h>
#include <asm/kvm_ppc.h>

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

extern void doorbell_exception(struct pt_regs *regs);

static inline void ppc_msgsnd(enum ppc_dbell type, u32 flags, u32 tag)
{
	u32 msg = PPC_DBELL_TYPE(type) | (flags & PPC_DBELL_MSG_BRDCAST) |
			(tag & 0x07ffffff);

	_ppc_msgsnd(msg);
}

#ifdef CONFIG_SMP

/*
 * Doorbells must only be used if CPU_FTR_DBELL is available.
 * msgsnd is used in HV, and msgsndp is used in !HV.
 *
 * These should be used by platform code that is aware of restrictions.
 * Other arch code should use ->cause_ipi.
 *
 * doorbell_global_ipi() sends a dbell to any target CPU.
 * Must be used only by architectures that address msgsnd target
 * by PIR/get_hard_smp_processor_id.
 */
static inline void doorbell_global_ipi(int cpu)
{
	u32 tag = get_hard_smp_processor_id(cpu);

	kvmppc_set_host_ipi(cpu);
	/* Order previous accesses vs. msgsnd, which is treated as a store */
	ppc_msgsnd_sync();
	ppc_msgsnd(PPC_DBELL_MSGTYPE, 0, tag);
}

/*
 * doorbell_core_ipi() sends a dbell to a target CPU in the same core.
 * Must be used only by architectures that address msgsnd target
 * by TIR/cpu_thread_in_core.
 */
static inline void doorbell_core_ipi(int cpu)
{
	u32 tag = cpu_thread_in_core(cpu);

	kvmppc_set_host_ipi(cpu);
	/* Order previous accesses vs. msgsnd, which is treated as a store */
	ppc_msgsnd_sync();
	ppc_msgsnd(PPC_DBELL_MSGTYPE, 0, tag);
}

/*
 * Attempt to cause a core doorbell if destination is on the same core.
 * Returns 1 on success, 0 on failure.
 */
static inline int doorbell_try_core_ipi(int cpu)
{
	int this_cpu = get_cpu();
	int ret = 0;

	if (cpumask_test_cpu(cpu, cpu_sibling_mask(this_cpu))) {
		doorbell_core_ipi(cpu);
		ret = 1;
	}

	put_cpu();

	return ret;
}

#endif /* CONFIG_SMP */

#endif /* _ASM_POWERPC_DBELL_H */
