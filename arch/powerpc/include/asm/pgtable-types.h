/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_PGTABLE_TYPES_H
#define _ASM_POWERPC_PGTABLE_TYPES_H

/* PTE level */
#if defined(CONFIG_PPC_8xx) && defined(CONFIG_PPC_16K_PAGES)
typedef struct { pte_basic_t pte, pte1, pte2, pte3; } pte_t;
#else
typedef struct { pte_basic_t pte; } pte_t;
#endif
#define __pte(x)	((pte_t) { (x) })
static inline pte_basic_t pte_val(pte_t x)
{
	return x.pte;
}

/* PMD level */
#ifdef CONFIG_PPC64
typedef struct { unsigned long pmd; } pmd_t;
#define __pmd(x)	((pmd_t) { (x) })
static inline unsigned long pmd_val(pmd_t x)
{
	return x.pmd;
}

/*
 * 64 bit hash always use 4 level table. Everybody else use 4 level
 * only for 4K page size.
 */
#if defined(CONFIG_PPC_BOOK3S_64) || !defined(CONFIG_PPC_64K_PAGES)
typedef struct { unsigned long pud; } pud_t;
#define __pud(x)	((pud_t) { (x) })
static inline unsigned long pud_val(pud_t x)
{
	return x.pud;
}
#endif /* CONFIG_PPC_BOOK3S_64 || !CONFIG_PPC_64K_PAGES */
#endif /* CONFIG_PPC64 */

/* PGD level */
typedef struct { unsigned long pgd; } pgd_t;
#define __pgd(x)	((pgd_t) { (x) })
static inline unsigned long pgd_val(pgd_t x)
{
	return x.pgd;
}

/* Page protection bits */
typedef struct { unsigned long pgprot; } pgprot_t;
#define pgprot_val(x)	((x).pgprot)
#define __pgprot(x)	((pgprot_t) { (x) })

/*
 * With hash config 64k pages additionally define a bigger "real PTE" type that
 * gathers the "second half" part of the PTE for pseudo 64k pages
 */
#if defined(CONFIG_PPC_64K_PAGES) && defined(CONFIG_PPC_BOOK3S_64)
typedef struct { pte_t pte; unsigned long hidx; } real_pte_t;
#else
typedef struct { pte_t pte; } real_pte_t;
#endif

#ifdef CONFIG_PPC_BOOK3S_64
#include <asm/cmpxchg.h>

static inline bool pte_xchg(pte_t *ptep, pte_t old, pte_t new)
{
	unsigned long *p = (unsigned long *)ptep;

	/* See comment in switch_mm_irqs_off() */
	return pte_val(old) == __cmpxchg_u64(p, pte_val(old), pte_val(new));
}
#endif

typedef struct { unsigned long pd; } hugepd_t;
#define __hugepd(x) ((hugepd_t) { (x) })
static inline unsigned long hpd_val(hugepd_t x)
{
	return x.pd;
}

#endif /* _ASM_POWERPC_PGTABLE_TYPES_H */
