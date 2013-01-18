/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASM_ARC_TLB_H
#define _ASM_ARC_TLB_H

#ifdef __KERNEL__

#include <asm/pgtable.h>

/* Masks for actual TLB "PD"s */
#define PTE_BITS_IN_PD0	(_PAGE_GLOBAL | _PAGE_PRESENT)
#define PTE_BITS_IN_PD1	(PAGE_MASK | _PAGE_CACHEABLE | \
			 _PAGE_EXECUTE | _PAGE_WRITE | _PAGE_READ | \
			 _PAGE_K_EXECUTE | _PAGE_K_WRITE | _PAGE_K_READ)

#ifndef __ASSEMBLY__

#include <linux/pagemap.h>
#include <asm-generic/tlb.h>

#ifdef CONFIG_ARC_DBG_TLB_PARANOIA
void tlb_paranoid_check(unsigned int pid_sw, unsigned long address);
#else
#define tlb_paranoid_check(a, b)
#endif

void arc_mmu_init(void);
extern char *arc_mmu_mumbojumbo(int cpu_id, char *buf, int len);
void __init read_decode_mmu_bcr(void);

#endif	/* __ASSEMBLY__ */

#endif	/* __KERNEL__ */

#endif /* _ASM_ARC_TLB_H */
