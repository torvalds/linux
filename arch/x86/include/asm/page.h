#ifndef _ASM_X86_PAGE_H
#define _ASM_X86_PAGE_H

#ifdef __KERNEL__

#include <asm/page_types.h>

#ifdef CONFIG_X86_64
#include <asm/page_64.h>
#else
#include <asm/page_32.h>
#endif	/* CONFIG_X86_64 */

#ifndef __ASSEMBLY__

struct page;

static inline void clear_user_page(void *page, unsigned long vaddr,
				   struct page *pg)
{
	clear_page(page);
}

static inline void copy_user_page(void *to, void *from, unsigned long vaddr,
				  struct page *topage)
{
	copy_page(to, from);
}

#define __alloc_zeroed_user_highpage(movableflags, vma, vaddr) \
	alloc_page_vma(GFP_HIGHUSER | __GFP_ZERO | movableflags, vma, vaddr)
#define __HAVE_ARCH_ALLOC_ZEROED_USER_HIGHPAGE

static inline pgd_t native_make_pgd(pgdval_t val)
{
	return (pgd_t) { val };
}

static inline pgdval_t native_pgd_val(pgd_t pgd)
{
	return pgd.pgd;
}

static inline pgdval_t pgd_flags(pgd_t pgd)
{
	return native_pgd_val(pgd) & PTE_FLAGS_MASK;
}

#if PAGETABLE_LEVELS >= 3
#if PAGETABLE_LEVELS == 4
static inline pud_t native_make_pud(pmdval_t val)
{
	return (pud_t) { val };
}

static inline pudval_t native_pud_val(pud_t pud)
{
	return pud.pud;
}
#else	/* PAGETABLE_LEVELS == 3 */
static inline pudval_t native_pud_val(pud_t pud)
{
	return native_pgd_val(pud.pgd);
}
#endif	/* PAGETABLE_LEVELS == 4 */

static inline pudval_t pud_flags(pud_t pud)
{
	return native_pud_val(pud) & PTE_FLAGS_MASK;
}

static inline pmd_t native_make_pmd(pmdval_t val)
{
	return (pmd_t) { val };
}

static inline pmdval_t native_pmd_val(pmd_t pmd)
{
	return pmd.pmd;
}

#else  /* PAGETABLE_LEVELS == 2 */
static inline pmdval_t native_pmd_val(pmd_t pmd)
{
	return native_pgd_val(pmd.pud.pgd);
}
#endif	/* PAGETABLE_LEVELS >= 3 */

static inline pmdval_t pmd_flags(pmd_t pmd)
{
	return native_pmd_val(pmd) & PTE_FLAGS_MASK;
}

static inline pte_t native_make_pte(pteval_t val)
{
	return (pte_t) { .pte = val };
}

static inline pteval_t native_pte_val(pte_t pte)
{
	return pte.pte;
}

static inline pteval_t pte_flags(pte_t pte)
{
	return native_pte_val(pte) & PTE_FLAGS_MASK;
}

#define pgprot_val(x)	((x).pgprot)
#define __pgprot(x)	((pgprot_t) { (x) } )

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else  /* !CONFIG_PARAVIRT */

#define pgd_val(x)	native_pgd_val(x)
#define __pgd(x)	native_make_pgd(x)

#ifndef __PAGETABLE_PUD_FOLDED
#define pud_val(x)	native_pud_val(x)
#define __pud(x)	native_make_pud(x)
#endif

#ifndef __PAGETABLE_PMD_FOLDED
#define pmd_val(x)	native_pmd_val(x)
#define __pmd(x)	native_make_pmd(x)
#endif

#define pte_val(x)	native_pte_val(x)
#define __pte(x)	native_make_pte(x)

#endif	/* CONFIG_PARAVIRT */

#define __pa(x)		__phys_addr((unsigned long)(x))
#define __pa_nodebug(x)	__phys_addr_nodebug((unsigned long)(x))
/* __pa_symbol should be used for C visible symbols.
   This seems to be the official gcc blessed way to do such arithmetic. */
#define __pa_symbol(x)	__pa(__phys_reloc_hide((unsigned long)(x)))

#define __va(x)			((void *)((unsigned long)(x)+PAGE_OFFSET))

#define __boot_va(x)		__va(x)
#define __boot_pa(x)		__pa(x)

/*
 * virt_to_page(kaddr) returns a valid pointer if and only if
 * virt_addr_valid(kaddr) returns true.
 */
#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)
#define pfn_to_kaddr(pfn)      __va((pfn) << PAGE_SHIFT)
extern bool __virt_addr_valid(unsigned long kaddr);
#define virt_addr_valid(kaddr)	__virt_addr_valid((unsigned long) (kaddr))

#endif	/* __ASSEMBLY__ */

#include <asm-generic/memory_model.h>
#include <asm-generic/page.h>

#define __HAVE_ARCH_GATE_AREA 1

#endif	/* __KERNEL__ */
#endif /* _ASM_X86_PAGE_H */
