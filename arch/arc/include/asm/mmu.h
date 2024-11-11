/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef _ASM_ARC_MMU_H
#define _ASM_ARC_MMU_H

#ifndef __ASSEMBLY__

#include <linux/threads.h>	/* NR_CPUS */

typedef struct {
	unsigned long asid[NR_CPUS];	/* 8 bit MMU PID + Generation cycle */
} mm_context_t;

struct pt_regs;
extern void do_tlb_overlap_fault(unsigned long, unsigned long, struct pt_regs *);

#endif

#include <asm/mmu-arcv2.h>

#endif
