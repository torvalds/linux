/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_PGTABLE_BE_TYPES_H
#define _ASM_POWERPC_PGTABLE_BE_TYPES_H

#include <asm/cmpxchg.h>

/* PTE level */
typedef struct { __be64 pte; } pte_t;
#define __pte(x)	((pte_t) { cpu_to_be64(x) })
#define __pte_raw(x)	((pte_t) { (x) })
static inline unsigned long pte_val(pte_t x)
{
	return be64_to_cpu(x.pte);
}

static inline __be64 pte_raw(pte_t x)
{
	return x.pte;
}

/* PMD level */
#ifdef CONFIG_PPC64
typedef struct { __be64 pmd; } pmd_t;
#define __pmd(x)	((pmd_t) { cpu_to_be64(x) })
#define __pmd_raw(x)	((pmd_t) { (x) })
static inline unsigned long pmd_val(pmd_t x)
{
	return be64_to_cpu(x.pmd);
}

static inline __be64 pmd_raw(pmd_t x)
{
	return x.pmd;
}

/* 64 bit always use 4 level table. */
typedef struct { __be64 pud; } pud_t;
#define __pud(x)	((pud_t) { cpu_to_be64(x) })
#define __pud_raw(x)	((pud_t) { (x) })
static inline unsigned long pud_val(pud_t x)
{
	return be64_to_cpu(x.pud);
}

static inline __be64 pud_raw(pud_t x)
{
	return x.pud;
}

#endif /* CONFIG_PPC64 */

/* PGD level */
typedef struct { __be64 pgd; } pgd_t;
#define __pgd(x)	((pgd_t) { cpu_to_be64(x) })
#define __pgd_raw(x)	((pgd_t) { (x) })
static inline unsigned long pgd_val(pgd_t x)
{
	return be64_to_cpu(x.pgd);
}

static inline __be64 pgd_raw(pgd_t x)
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
#ifdef CONFIG_PPC_64K_PAGES
typedef struct { pte_t pte; unsigned long hidx; } real_pte_t;
#else
typedef struct { pte_t pte; } real_pte_t;
#endif

static inline bool pte_xchg(pte_t *ptep, pte_t old, pte_t new)
{
	unsigned long *p = (unsigned long *)ptep;
	__be64 prev;

	/* See comment in switch_mm_irqs_off() */
	prev = (__force __be64)__cmpxchg_u64(p, (__force unsigned long)pte_raw(old),
					     (__force unsigned long)pte_raw(new));

	return pte_raw(old) == prev;
}

static inline bool pmd_xchg(pmd_t *pmdp, pmd_t old, pmd_t new)
{
	unsigned long *p = (unsigned long *)pmdp;
	__be64 prev;

	prev = (__force __be64)__cmpxchg_u64(p, (__force unsigned long)pmd_raw(old),
					     (__force unsigned long)pmd_raw(new));

	return pmd_raw(old) == prev;
}

#ifdef CONFIG_ARCH_HAS_HUGEPD
typedef struct { __be64 pdbe; } hugepd_t;
#define __hugepd(x) ((hugepd_t) { cpu_to_be64(x) })

static inline unsigned long hpd_val(hugepd_t x)
{
	return be64_to_cpu(x.pdbe);
}
#endif

#endif /* _ASM_POWERPC_PGTABLE_BE_TYPES_H */
