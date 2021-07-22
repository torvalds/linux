/* SPDX-License-Identifier: GPL-2.0 */

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

static inline unsigned long read_mmu_msa0(void)
{
	return cprcr("cpcr30");
}

static inline void write_mmu_msa0(unsigned long value)
{
	cpwcr("cpcr30", value);
}

static inline unsigned long read_mmu_msa1(void)
{
	return cprcr("cpcr31");
}

static inline void write_mmu_msa1(unsigned long value)
{
	cpwcr("cpcr31", value);
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


static inline void local_tlb_invalid_all(void)
{
	tlb_invalid_all();
}

static inline void tlb_invalid_indexed(void)
{
	cpwcr("cpcr8", 0x02000000);
}

static inline void setup_pgd(pgd_t *pgd, int asid)
{
	cpwcr("cpcr29", __pa(pgd) | BIT(0));
	write_mmu_entryhi(asid);
}

static inline pgd_t *get_pgd(void)
{
	return __va(cprcr("cpcr29") & ~BIT(0));
}
#endif /* __ASM_CSKY_CKMMUV1_H */
