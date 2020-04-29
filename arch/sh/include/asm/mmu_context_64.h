/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_MMU_CONTEXT_64_H
#define __ASM_SH_MMU_CONTEXT_64_H

/*
 * sh64-specific mmu_context interface.
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003 - 2007  Paul Mundt
 */
#include <cpu/registers.h>
#include <asm/cacheflush.h>

#define SR_ASID_MASK		0xffffffffff00ffffULL
#define SR_ASID_SHIFT		16

/*
 * Destroy context related info for an mm_struct that is about
 * to be put to rest.
 */
static inline void destroy_context(struct mm_struct *mm)
{
	/* Well, at least free TLB entries */
	flush_tlb_mm(mm);
}

static inline unsigned long get_asid(void)
{
	unsigned long long sr;

	asm volatile ("getcon   " __SR ", %0\n\t"
		      : "=r" (sr));

	sr = (sr >> SR_ASID_SHIFT) & MMU_CONTEXT_ASID_MASK;
	return (unsigned long) sr;
}

/* Set ASID into SR */
static inline void set_asid(unsigned long asid)
{
	unsigned long long sr, pc;

	asm volatile ("getcon	" __SR ", %0" : "=r" (sr));

	sr = (sr & SR_ASID_MASK) | (asid << SR_ASID_SHIFT);

	/*
	 * It is possible that this function may be inlined and so to avoid
	 * the assembler reporting duplicate symbols we make use of the
	 * gas trick of generating symbols using numerics and forward
	 * reference.
	 */
	asm volatile ("movi	1, %1\n\t"
		      "shlli	%1, 28, %1\n\t"
		      "or	%0, %1, %1\n\t"
		      "putcon	%1, " __SR "\n\t"
		      "putcon	%0, " __SSR "\n\t"
		      "movi	1f, %1\n\t"
		      "ori	%1, 1 , %1\n\t"
		      "putcon	%1, " __SPC "\n\t"
		      "rte\n"
		      "1:\n\t"
		      : "=r" (sr), "=r" (pc) : "0" (sr));
}

/* arch/sh/kernel/cpu/sh5/entry.S */
extern unsigned long switch_and_save_asid(unsigned long new_asid);

/* No spare register to twiddle, so use a software cache */
extern pgd_t *mmu_pdtp_cache;

#define set_TTB(pgd)	(mmu_pdtp_cache = (pgd))
#define get_TTB()	(mmu_pdtp_cache)

#endif /* __ASM_SH_MMU_CONTEXT_64_H */
