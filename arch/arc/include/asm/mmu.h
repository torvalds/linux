/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef _ASM_ARC_MMU_H
#define _ASM_ARC_MMU_H

#ifndef __ASSEMBLY__
#include <linux/threads.h>	/* NR_CPUS */
#endif

/* MMU Management regs */
#define ARC_REG_MMU_BCR		0x06f

#ifdef CONFIG_ARC_MMU_V3
#define ARC_REG_TLBPD0		0x405
#define ARC_REG_TLBPD1		0x406
#define ARC_REG_TLBPD1HI	0	/* Dummy: allows code sharing with ARC700 */
#define ARC_REG_TLBINDEX	0x407
#define ARC_REG_TLBCOMMAND	0x408
#define ARC_REG_PID		0x409
#define ARC_REG_SCRATCH_DATA0	0x418
#else
#define ARC_REG_TLBPD0		0x460
#define ARC_REG_TLBPD1		0x461
#define ARC_REG_TLBPD1HI	0x463
#define ARC_REG_TLBINDEX	0x464
#define ARC_REG_TLBCOMMAND	0x465
#define ARC_REG_PID		0x468
#define ARC_REG_SCRATCH_DATA0	0x46c
#endif

/* Bits in MMU PID register */
#define __TLB_ENABLE		(1 << 31)
#define __PROG_ENABLE		(1 << 30)
#define MMU_ENABLE		(__TLB_ENABLE | __PROG_ENABLE)

/* Error code if probe fails */
#define TLB_LKUP_ERR		0x80000000

#ifdef CONFIG_ARC_MMU_V3
#define TLB_DUP_ERR	(TLB_LKUP_ERR | 0x00000001)
#else
#define TLB_DUP_ERR	(TLB_LKUP_ERR | 0x40000000)
#endif

/* TLB Commands */
#define TLBWrite    0x1
#define TLBRead     0x2
#define TLBGetIndex 0x3
#define TLBProbe    0x4
#define TLBWriteNI  0x5		/* write JTLB without inv uTLBs */
#define TLBIVUTLB   0x6		/* explicitly inv uTLBs */

#ifdef CONFIG_ARC_MMU_V4
#define TLBInsertEntry	0x7
#define TLBDeleteEntry	0x8
#endif

#ifndef __ASSEMBLY__

typedef struct {
	unsigned long asid[NR_CPUS];	/* 8 bit MMU PID + Generation cycle */
} mm_context_t;

static inline void mmu_setup_asid(struct mm_struct *mm, unsigned int asid)
{
	write_aux_reg(ARC_REG_PID, asid | MMU_ENABLE);
}

static inline void mmu_setup_pgd(struct mm_struct *mm, void *pgd)
{
	/* PGD cached in MMU reg to avoid 3 mem lookups: task->mm->pgd */
#ifdef CONFIG_ISA_ARCV2
	write_aux_reg(ARC_REG_SCRATCH_DATA0, (unsigned int)pgd);
#endif
}

static inline int is_pae40_enabled(void)
{
	return IS_ENABLED(CONFIG_ARC_HAS_PAE40);
}

extern int pae40_exist_but_not_enab(void);

#endif	/* !__ASSEMBLY__ */

#endif
