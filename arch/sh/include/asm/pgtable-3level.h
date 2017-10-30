#ifndef __ASM_SH_PGTABLE_3LEVEL_H
#define __ASM_SH_PGTABLE_3LEVEL_H

#define __ARCH_USE_5LEVEL_HACK
#include <asm-generic/pgtable-nopud.h>

/*
 * Some cores need a 3-level page table layout, for example when using
 * 64-bit PTEs and 4K pages.
 */
#define PAGETABLE_LEVELS	3

#define PTE_MAGNITUDE		3	/* 64-bit PTEs on SH-X2 TLB */

/* PGD bits */
#define PGDIR_SHIFT		30

#define PTRS_PER_PGD		4
#define USER_PTRS_PER_PGD	2

/* PMD bits */
#define PMD_SHIFT	(PAGE_SHIFT + (PAGE_SHIFT - PTE_MAGNITUDE))
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

#define PTRS_PER_PMD	((1 << PGDIR_SHIFT) / PMD_SIZE)

#define pmd_ERROR(e) \
	printk("%s:%d: bad pmd %016llx.\n", __FILE__, __LINE__, pmd_val(e))

typedef struct { unsigned long long pmd; } pmd_t;
#define pmd_val(x)	((x).pmd)
#define __pmd(x)	((pmd_t) { (x) } )

static inline unsigned long pud_page_vaddr(pud_t pud)
{
	return pud_val(pud);
}

#define pmd_index(address)	(((address) >> PMD_SHIFT) & (PTRS_PER_PMD-1))
static inline pmd_t *pmd_offset(pud_t *pud, unsigned long address)
{
	return (pmd_t *)pud_page_vaddr(*pud) + pmd_index(address);
}

#define pud_none(x)	(!pud_val(x))
#define pud_present(x)	(pud_val(x))
#define pud_clear(xp)	do { set_pud(xp, __pud(0)); } while (0)
#define	pud_bad(x)	(pud_val(x) & ~PAGE_MASK)

/*
 * (puds are folded into pgds so this doesn't get actually called,
 * but the define is needed for a generic inline function.)
 */
#define set_pud(pudptr, pudval) do { *(pudptr) = (pudval); } while(0)

#endif /* __ASM_SH_PGTABLE_3LEVEL_H */
