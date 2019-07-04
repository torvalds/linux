/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/include/asm/cpu-single.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */
#ifndef __UNICORE_CPU_SINGLE_H__
#define __UNICORE_CPU_SINGLE_H__

#include <asm/page.h>
#include <asm/memory.h>

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#define cpu_switch_mm(pgd, mm) cpu_do_switch_mm(virt_to_phys(pgd), mm)

#define cpu_get_pgd()					\
	({						\
		unsigned long pg;			\
		__asm__("movc	%0, p0.c2, #0"		\
			 : "=r" (pg) : : "cc");		\
		pg &= ~0x0fff;				\
		(pgd_t *)phys_to_virt(pg);		\
	})

struct mm_struct;

/* declare all the functions as extern */
extern void cpu_proc_fin(void);
extern int cpu_do_idle(void);
extern void cpu_dcache_clean_area(void *, int);
extern void cpu_do_switch_mm(unsigned long pgd_phys, struct mm_struct *mm);
extern void cpu_set_pte(pte_t *ptep, pte_t pte);
extern void cpu_reset(unsigned long addr) __attribute__((noreturn));

#endif /* __ASSEMBLY__ */
#endif /* __KERNEL__ */

#endif /* __UNICORE_CPU_SINGLE_H__ */
