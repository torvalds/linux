/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_CKMMUV2_H
#define __ASM_CSKY_CKMMUV2_H

#include <abi/reg_ops.h>
#include <asm/barrier.h>

static inline int read_mmu_index(void)
{
	return mfcr("cr<0, 15>");
}

static inline void write_mmu_index(int value)
{
	mtcr("cr<0, 15>", value);
}

static inline int read_mmu_entrylo0(void)
{
	return mfcr("cr<2, 15>");
}

static inline int read_mmu_entrylo1(void)
{
	return mfcr("cr<3, 15>");
}

static inline void write_mmu_pagemask(int value)
{
	mtcr("cr<6, 15>", value);
}

static inline int read_mmu_entryhi(void)
{
	return mfcr("cr<4, 15>");
}

static inline void write_mmu_entryhi(int value)
{
	mtcr("cr<4, 15>", value);
}

static inline unsigned long read_mmu_msa0(void)
{
	return mfcr("cr<30, 15>");
}

static inline void write_mmu_msa0(unsigned long value)
{
	mtcr("cr<30, 15>", value);
}

static inline unsigned long read_mmu_msa1(void)
{
	return mfcr("cr<31, 15>");
}

static inline void write_mmu_msa1(unsigned long value)
{
	mtcr("cr<31, 15>", value);
}

/*
 * TLB operations.
 */
static inline void tlb_probe(void)
{
	mtcr("cr<8, 15>", 0x80000000);
}

static inline void tlb_read(void)
{
	mtcr("cr<8, 15>", 0x40000000);
}

static inline void tlb_invalid_all(void)
{
#ifdef CONFIG_CPU_HAS_TLBI
	sync_is();
	asm volatile(
		"tlbi.alls	\n"
		"sync.i		\n"
		:
		:
		: "memory");
#else
	mtcr("cr<8, 15>", 0x04000000);
#endif
}

static inline void local_tlb_invalid_all(void)
{
#ifdef CONFIG_CPU_HAS_TLBI
	sync_is();
	asm volatile(
		"tlbi.all	\n"
		"sync.i		\n"
		:
		:
		: "memory");
#else
	tlb_invalid_all();
#endif
}

static inline void tlb_invalid_indexed(void)
{
	mtcr("cr<8, 15>", 0x02000000);
}

#define NOP32 ".long 0x4820c400\n"

static inline void setup_pgd(pgd_t *pgd, int asid)
{
#ifdef CONFIG_CPU_HAS_TLBI
	sync_is();
#else
	mb();
#endif
	asm volatile(
#ifdef CONFIG_CPU_HAS_TLBI
		"mtcr %1, cr<28, 15>	\n"
#endif
		"mtcr %1, cr<29, 15>	\n"
		"mtcr %0, cr< 4, 15>	\n"
		".rept 64		\n"
		NOP32
		".endr			\n"
		:
		:"r"(asid), "r"(__pa(pgd) | BIT(0))
		:"memory");
}

static inline pgd_t *get_pgd(void)
{
	return __va(mfcr("cr<29, 15>") & ~BIT(0));
}
#endif /* __ASM_CSKY_CKMMUV2_H */
