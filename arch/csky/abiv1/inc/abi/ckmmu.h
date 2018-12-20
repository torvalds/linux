/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#ifndef __ASM_CSKY_CKMMUV1_H
#define __ASM_CSKY_CKMMUV1_H
#include <abi/reg_ops.h>

static inline int read_mmu_index(void)
{
	return cprcr("cpcr0");
}

static inline void write_mmu_index(int value)
{
	cpwcr("cpcr0", value);
}

static inline int read_mmu_entrylo0(void)
{
	return cprcr("cpcr2") << 6;
}

static inline int read_mmu_entrylo1(void)
{
	return cprcr("cpcr3") << 6;
}

static inline void write_mmu_pagemask(int value)
{
	cpwcr("cpcr6", value);
}

static inline int read_mmu_entryhi(void)
{
	return cprcr("cpcr4");
}

static inline void write_mmu_entryhi(int value)
{
	cpwcr("cpcr4", value);
}

/*
 * TLB operations.
 */
static inline void tlb_probe(void)
{
	cpwcr("cpcr8", 0x80000000);
}

static inline void tlb_read(void)
{
	cpwcr("cpcr8", 0x40000000);
}

static inline void tlb_invalid_all(void)
{
	cpwcr("cpcr8", 0x04000000);
}

static inline void tlb_invalid_indexed(void)
{
	cpwcr("cpcr8", 0x02000000);
}

static inline void setup_pgd(unsigned long pgd, bool kernel)
{
	cpwcr("cpcr29", pgd);
}

static inline unsigned long get_pgd(void)
{
	return cprcr("cpcr29");
}
#endif /* __ASM_CSKY_CKMMUV1_H */
