#ifndef _ASM_POWERPC_PGTABLE_TYPES_H
#define _ASM_POWERPC_PGTABLE_TYPES_H

#ifdef CONFIG_STRICT_MM_TYPECHECKS
/* These are used to make use of C type-checking. */

/* PTE level */
typedef struct { pte_basic_t pte; } pte_t;
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

#else

/*
 * .. while these make it easier on the compiler
 */

typedef pte_basic_t pte_t;
#define __pte(x)	(x)
static inline pte_basic_t pte_val(pte_t pte)
{
	return pte;
}

#ifdef CONFIG_PPC64
typedef unsigned long pmd_t;
#define __pmd(x)	(x)
static inline unsigned long pmd_val(pmd_t pmd)
{
	return pmd;
}

#if defined(CONFIG_PPC_BOOK3S_64) || !defined(CONFIG_PPC_64K_PAGES)
typedef unsigned long pud_t;
#define __pud(x)	(x)
static inline unsigned long pud_val(pud_t pud)
{
	return pud;
}
#endif /* CONFIG_PPC_BOOK3S_64 || !CONFIG_PPC_64K_PAGES */
#endif /* CONFIG_PPC64 */

typedef unsigned long pgd_t;
#define __pgd(x)	(x)
static inline unsigned long pgd_val(pgd_t pgd)
{
	return pgd;
}

typedef unsigned long pgprot_t;
#define pgprot_val(x)	(x)
#define __pgprot(x)	(x)

#endif /* CONFIG_STRICT_MM_TYPECHECKS */
/*
 * With hash config 64k pages additionally define a bigger "real PTE" type that
 * gathers the "second half" part of the PTE for pseudo 64k pages
 */
#if defined(CONFIG_PPC_64K_PAGES) && defined(CONFIG_PPC_STD_MMU_64)
typedef struct { pte_t pte; unsigned long hidx; } real_pte_t;
#else
typedef struct { pte_t pte; } real_pte_t;
#endif
#endif /* _ASM_POWERPC_PGTABLE_TYPES_H */
