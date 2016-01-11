/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASM_ARC_MMU_H
#define _ASM_ARC_MMU_H

#if defined(CONFIG_ARC_MMU_V1)
#define CONFIG_ARC_MMU_VER 1
#elif defined(CONFIG_ARC_MMU_V2)
#define CONFIG_ARC_MMU_VER 2
#elif defined(CONFIG_ARC_MMU_V3)
#define CONFIG_ARC_MMU_VER 3
#elif defined(CONFIG_ARC_MMU_V4)
#define CONFIG_ARC_MMU_VER 4
#endif

/* MMU Management regs */
#define ARC_REG_MMU_BCR		0x06f
#if (CONFIG_ARC_MMU_VER < 4)
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

#if (CONFIG_ARC_MMU_VER < 4)
#define TLB_DUP_ERR	(TLB_LKUP_ERR | 0x00000001)
#else
#define TLB_DUP_ERR	(TLB_LKUP_ERR | 0x40000000)
#endif

/* TLB Commands */
#define TLBWrite    0x1
#define TLBRead     0x2
#define TLBGetIndex 0x3
#define TLBProbe    0x4

#if (CONFIG_ARC_MMU_VER >= 2)
#define TLBWriteNI  0x5		/* write JTLB without inv uTLBs */
#define TLBIVUTLB   0x6		/* explicitly inv uTLBs */
#endif

#if (CONFIG_ARC_MMU_VER >= 4)
#define TLBInsertEntry	0x7
#define TLBDeleteEntry	0x8
#endif

#ifndef __ASSEMBLY__

typedef struct {
	unsigned long asid[NR_CPUS];	/* 8 bit MMU PID + Generation cycle */
} mm_context_t;

#ifdef CONFIG_ARC_DBG_TLB_PARANOIA
void tlb_paranoid_check(unsigned int mm_asid, unsigned long address);
#else
#define tlb_paranoid_check(a, b)
#endif

void arc_mmu_init(void);
extern char *arc_mmu_mumbojumbo(int cpu_id, char *buf, int len);
void read_decode_mmu_bcr(void);

static inline int is_pae40_enabled(void)
{
	return IS_ENABLED(CONFIG_ARC_HAS_PAE40);
}

#endif	/* !__ASSEMBLY__ */

#endif
