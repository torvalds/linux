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
#endif

/* MMU Management regs */
#define ARC_REG_MMU_BCR		0x06f
#define ARC_REG_TLBPD0		0x405
#define ARC_REG_TLBPD1		0x406
#define ARC_REG_TLBINDEX	0x407
#define ARC_REG_TLBCOMMAND	0x408
#define ARC_REG_PID		0x409
#define ARC_REG_SCRATCH_DATA0	0x418

/* Bits in MMU PID register */
#define MMU_ENABLE		(1 << 31)	/* Enable MMU for process */

/* Error code if probe fails */
#define TLB_LKUP_ERR		0x80000000

/* TLB Commands */
#define TLBWrite    0x1
#define TLBRead     0x2
#define TLBGetIndex 0x3
#define TLBProbe    0x4

#if (CONFIG_ARC_MMU_VER >= 2)
#define TLBWriteNI  0x5		/* write JTLB without inv uTLBs */
#define TLBIVUTLB   0x6		/* explicitly inv uTLBs */
#endif

#ifndef __ASSEMBLY__

typedef struct {
	unsigned long asid;	/* Pvt Addr-Space ID for mm */
#ifdef CONFIG_ARC_TLB_DBG
	struct task_struct *tsk;
#endif
} mm_context_t;

#ifdef CONFIG_ARC_DBG_TLB_PARANOIA
void tlb_paranoid_check(unsigned int pid_sw, unsigned long address);
#else
#define tlb_paranoid_check(a, b)
#endif

void arc_mmu_init(void);
extern char *arc_mmu_mumbojumbo(int cpu_id, char *buf, int len);
void __init read_decode_mmu_bcr(void);

#endif	/* !__ASSEMBLY__ */

#endif
