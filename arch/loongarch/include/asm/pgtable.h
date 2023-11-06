/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 *
 * Derived from MIPS:
 * Copyright (C) 1994, 95, 96, 97, 98, 99, 2000, 2003 Ralf Baechle
 * Copyright (C) 1999, 2000, 2001 Silicon Graphics, Inc.
 */
#ifndef _ASM_PGTABLE_H
#define _ASM_PGTABLE_H

#include <linux/compiler.h>
#include <asm/addrspace.h>
#include <asm/page.h>
#include <asm/pgtable-bits.h>

#if CONFIG_PGTABLE_LEVELS == 2
#include <asm-generic/pgtable-nopmd.h>
#elif CONFIG_PGTABLE_LEVELS == 3
#include <asm-generic/pgtable-nopud.h>
#else
#include <asm-generic/pgtable-nop4d.h>
#endif

#if CONFIG_PGTABLE_LEVELS == 2
#define PGDIR_SHIFT	(PAGE_SHIFT + (PAGE_SHIFT - 3))
#elif CONFIG_PGTABLE_LEVELS == 3
#define PMD_SHIFT	(PAGE_SHIFT + (PAGE_SHIFT - 3))
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))
#define PGDIR_SHIFT	(PMD_SHIFT + (PAGE_SHIFT - 3))
#elif CONFIG_PGTABLE_LEVELS == 4
#define PMD_SHIFT	(PAGE_SHIFT + (PAGE_SHIFT - 3))
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))
#define PUD_SHIFT	(PMD_SHIFT + (PAGE_SHIFT - 3))
#define PUD_SIZE	(1UL << PUD_SHIFT)
#define PUD_MASK	(~(PUD_SIZE-1))
#define PGDIR_SHIFT	(PUD_SHIFT + (PAGE_SHIFT - 3))
#endif

#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

#define VA_BITS		(PGDIR_SHIFT + (PAGE_SHIFT - 3))

#define PTRS_PER_PGD	(PAGE_SIZE >> 3)
#if CONFIG_PGTABLE_LEVELS > 3
#define PTRS_PER_PUD	(PAGE_SIZE >> 3)
#endif
#if CONFIG_PGTABLE_LEVELS > 2
#define PTRS_PER_PMD	(PAGE_SIZE >> 3)
#endif
#define PTRS_PER_PTE	(PAGE_SIZE >> 3)

#define USER_PTRS_PER_PGD       ((TASK_SIZE64 / PGDIR_SIZE)?(TASK_SIZE64 / PGDIR_SIZE):1)

#ifndef __ASSEMBLY__

#include <linux/mm_types.h>
#include <linux/mmzone.h>
#include <asm/fixmap.h>
#include <asm/sparsemem.h>

struct mm_struct;
struct vm_area_struct;

/*
 * ZERO_PAGE is a global shared page that is always zero; used
 * for zero-mapped memory areas etc..
 */

extern unsigned long empty_zero_page;
extern unsigned long zero_page_mask;

#define ZERO_PAGE(vaddr) \
	(virt_to_page((void *)(empty_zero_page + (((unsigned long)(vaddr)) & zero_page_mask))))
#define __HAVE_COLOR_ZERO_PAGE

/*
 * TLB refill handlers may also map the vmalloc area into xkvrange.
 * Avoid the first couple of pages so NULL pointer dereferences will
 * still reliably trap.
 */
#define MODULES_VADDR	(vm_map_base + PCI_IOSIZE + (2 * PAGE_SIZE))
#define MODULES_END	(MODULES_VADDR + SZ_256M)

#define VMALLOC_START	MODULES_END
#define VMALLOC_END	\
	(vm_map_base +	\
	 min(PTRS_PER_PGD * PTRS_PER_PUD * PTRS_PER_PMD * PTRS_PER_PTE * PAGE_SIZE, (1UL << cpu_vabits)) - PMD_SIZE - VMEMMAP_SIZE)

#define vmemmap		((struct page *)((VMALLOC_END + PMD_SIZE) & PMD_MASK))
#define VMEMMAP_END	((unsigned long)vmemmap + VMEMMAP_SIZE - 1)

#define pte_ERROR(e) \
	pr_err("%s:%d: bad pte %016lx.\n", __FILE__, __LINE__, pte_val(e))
#ifndef __PAGETABLE_PMD_FOLDED
#define pmd_ERROR(e) \
	pr_err("%s:%d: bad pmd %016lx.\n", __FILE__, __LINE__, pmd_val(e))
#endif
#ifndef __PAGETABLE_PUD_FOLDED
#define pud_ERROR(e) \
	pr_err("%s:%d: bad pud %016lx.\n", __FILE__, __LINE__, pud_val(e))
#endif
#define pgd_ERROR(e) \
	pr_err("%s:%d: bad pgd %016lx.\n", __FILE__, __LINE__, pgd_val(e))

extern pte_t invalid_pte_table[PTRS_PER_PTE];

#ifndef __PAGETABLE_PUD_FOLDED

typedef struct { unsigned long pud; } pud_t;
#define pud_val(x)	((x).pud)
#define __pud(x)	((pud_t) { (x) })

extern pud_t invalid_pud_table[PTRS_PER_PUD];

/*
 * Empty pgd/p4d entries point to the invalid_pud_table.
 */
static inline int p4d_none(p4d_t p4d)
{
	return p4d_val(p4d) == (unsigned long)invalid_pud_table;
}

static inline int p4d_bad(p4d_t p4d)
{
	return p4d_val(p4d) & ~PAGE_MASK;
}

static inline int p4d_present(p4d_t p4d)
{
	return p4d_val(p4d) != (unsigned long)invalid_pud_table;
}

static inline void p4d_clear(p4d_t *p4dp)
{
	p4d_val(*p4dp) = (unsigned long)invalid_pud_table;
}

static inline pud_t *p4d_pgtable(p4d_t p4d)
{
	return (pud_t *)p4d_val(p4d);
}

static inline void set_p4d(p4d_t *p4d, p4d_t p4dval)
{
	*p4d = p4dval;
}

#define p4d_phys(p4d)		PHYSADDR(p4d_val(p4d))
#define p4d_page(p4d)		(pfn_to_page(p4d_phys(p4d) >> PAGE_SHIFT))

#endif

#ifndef __PAGETABLE_PMD_FOLDED

typedef struct { unsigned long pmd; } pmd_t;
#define pmd_val(x)	((x).pmd)
#define __pmd(x)	((pmd_t) { (x) })

extern pmd_t invalid_pmd_table[PTRS_PER_PMD];

/*
 * Empty pud entries point to the invalid_pmd_table.
 */
static inline int pud_none(pud_t pud)
{
	return pud_val(pud) == (unsigned long)invalid_pmd_table;
}

static inline int pud_bad(pud_t pud)
{
	return pud_val(pud) & ~PAGE_MASK;
}

static inline int pud_present(pud_t pud)
{
	return pud_val(pud) != (unsigned long)invalid_pmd_table;
}

static inline void pud_clear(pud_t *pudp)
{
	pud_val(*pudp) = ((unsigned long)invalid_pmd_table);
}

static inline pmd_t *pud_pgtable(pud_t pud)
{
	return (pmd_t *)pud_val(pud);
}

#define set_pud(pudptr, pudval) do { *(pudptr) = (pudval); } while (0)

#define pud_phys(pud)		PHYSADDR(pud_val(pud))
#define pud_page(pud)		(pfn_to_page(pud_phys(pud) >> PAGE_SHIFT))

#endif

/*
 * Empty pmd entries point to the invalid_pte_table.
 */
static inline int pmd_none(pmd_t pmd)
{
	return pmd_val(pmd) == (unsigned long)invalid_pte_table;
}

static inline int pmd_bad(pmd_t pmd)
{
	return (pmd_val(pmd) & ~PAGE_MASK);
}

static inline int pmd_present(pmd_t pmd)
{
	if (unlikely(pmd_val(pmd) & _PAGE_HUGE))
		return !!(pmd_val(pmd) & (_PAGE_PRESENT | _PAGE_PROTNONE | _PAGE_PRESENT_INVALID));

	return pmd_val(pmd) != (unsigned long)invalid_pte_table;
}

static inline void pmd_clear(pmd_t *pmdp)
{
	pmd_val(*pmdp) = ((unsigned long)invalid_pte_table);
}

#define set_pmd(pmdptr, pmdval) do { *(pmdptr) = (pmdval); } while (0)

#define pmd_phys(pmd)		PHYSADDR(pmd_val(pmd))

#ifndef CONFIG_TRANSPARENT_HUGEPAGE
#define pmd_page(pmd)		(pfn_to_page(pmd_phys(pmd) >> PAGE_SHIFT))
#endif /* CONFIG_TRANSPARENT_HUGEPAGE  */

#define pmd_page_vaddr(pmd)	pmd_val(pmd)

extern pmd_t mk_pmd(struct page *page, pgprot_t prot);
extern void set_pmd_at(struct mm_struct *mm, unsigned long addr, pmd_t *pmdp, pmd_t pmd);

#define pte_page(x)		pfn_to_page(pte_pfn(x))
#define pte_pfn(x)		((unsigned long)(((x).pte & _PFN_MASK) >> _PFN_SHIFT))
#define pfn_pte(pfn, prot)	__pte(((pfn) << _PFN_SHIFT) | pgprot_val(prot))
#define pfn_pmd(pfn, prot)	__pmd(((pfn) << _PFN_SHIFT) | pgprot_val(prot))

/*
 * Initialize a new pgd / pud / pmd table with invalid pointers.
 */
extern void pgd_init(void *addr);
extern void pud_init(void *addr);
extern void pmd_init(void *addr);

/*
 * Encode/decode swap entries and swap PTEs. Swap PTEs are all PTEs that
 * are !pte_none() && !pte_present().
 *
 * Format of swap PTEs:
 *
 *   6 6 6 6 5 5 5 5 5 5 5 5 5 5 4 4 4 4 4 4 4 4 4 4 3 3 3 3 3 3 3 3
 *   3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2
 *   <--------------------------- offset ---------------------------
 *
 *   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
 *   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *   --------------> E <--- type ---> <---------- zeroes ---------->
 *
 *   E is the exclusive marker that is not stored in swap entries.
 *   The zero'ed bits include _PAGE_PRESENT and _PAGE_PROTNONE.
 */
static inline pte_t mk_swap_pte(unsigned long type, unsigned long offset)
{ pte_t pte; pte_val(pte) = ((type & 0x7f) << 16) | (offset << 24); return pte; }

#define __swp_type(x)		(((x).val >> 16) & 0x7f)
#define __swp_offset(x)		((x).val >> 24)
#define __swp_entry(type, offset) ((swp_entry_t) { pte_val(mk_swap_pte((type), (offset))) })
#define __pte_to_swp_entry(pte) ((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val })
#define __pmd_to_swp_entry(pmd) ((swp_entry_t) { pmd_val(pmd) })
#define __swp_entry_to_pmd(x)	((pmd_t) { (x).val | _PAGE_HUGE })

static inline int pte_swp_exclusive(pte_t pte)
{
	return pte_val(pte) & _PAGE_SWP_EXCLUSIVE;
}

static inline pte_t pte_swp_mkexclusive(pte_t pte)
{
	pte_val(pte) |= _PAGE_SWP_EXCLUSIVE;
	return pte;
}

static inline pte_t pte_swp_clear_exclusive(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_SWP_EXCLUSIVE;
	return pte;
}

extern void paging_init(void);

#define pte_none(pte)		(!(pte_val(pte) & ~_PAGE_GLOBAL))
#define pte_present(pte)	(pte_val(pte) & (_PAGE_PRESENT | _PAGE_PROTNONE))
#define pte_no_exec(pte)	(pte_val(pte) & _PAGE_NO_EXEC)

static inline void set_pte(pte_t *ptep, pte_t pteval)
{
	*ptep = pteval;
	if (pte_val(pteval) & _PAGE_GLOBAL) {
		pte_t *buddy = ptep_buddy(ptep);
		/*
		 * Make sure the buddy is global too (if it's !none,
		 * it better already be global)
		 */
#ifdef CONFIG_SMP
		/*
		 * For SMP, multiple CPUs can race, so we need to do
		 * this atomically.
		 */
		unsigned long page_global = _PAGE_GLOBAL;
		unsigned long tmp;

		__asm__ __volatile__ (
		"1:"	__LL	"%[tmp], %[buddy]		\n"
		"	bnez	%[tmp], 2f			\n"
		"	 or	%[tmp], %[tmp], %[global]	\n"
			__SC	"%[tmp], %[buddy]		\n"
		"	beqz	%[tmp], 1b			\n"
		"	nop					\n"
		"2:						\n"
		__WEAK_LLSC_MB
		: [buddy] "+m" (buddy->pte), [tmp] "=&r" (tmp)
		: [global] "r" (page_global));
#else /* !CONFIG_SMP */
		if (pte_none(*buddy))
			pte_val(*buddy) = pte_val(*buddy) | _PAGE_GLOBAL;
#endif /* CONFIG_SMP */
	}
}

static inline void set_pte_at(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, pte_t pteval)
{
	set_pte(ptep, pteval);
}

static inline void pte_clear(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	/* Preserve global status for the pair */
	if (pte_val(*ptep_buddy(ptep)) & _PAGE_GLOBAL)
		set_pte_at(mm, addr, ptep, __pte(_PAGE_GLOBAL));
	else
		set_pte_at(mm, addr, ptep, __pte(0));
}

#define PGD_T_LOG2	(__builtin_ffs(sizeof(pgd_t)) - 1)
#define PMD_T_LOG2	(__builtin_ffs(sizeof(pmd_t)) - 1)
#define PTE_T_LOG2	(__builtin_ffs(sizeof(pte_t)) - 1)

extern pgd_t swapper_pg_dir[];
extern pgd_t invalid_pg_dir[];

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
static inline int pte_write(pte_t pte)	{ return pte_val(pte) & _PAGE_WRITE; }
static inline int pte_young(pte_t pte)	{ return pte_val(pte) & _PAGE_ACCESSED; }
static inline int pte_dirty(pte_t pte)	{ return pte_val(pte) & (_PAGE_DIRTY | _PAGE_MODIFIED); }

static inline pte_t pte_mkold(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_ACCESSED;
	return pte;
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	pte_val(pte) |= _PAGE_ACCESSED;
	return pte;
}

static inline pte_t pte_mkclean(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_DIRTY | _PAGE_MODIFIED);
	return pte;
}

static inline pte_t pte_mkdirty(pte_t pte)
{
	pte_val(pte) |= _PAGE_MODIFIED;
	if (pte_val(pte) & _PAGE_WRITE)
		pte_val(pte) |= _PAGE_DIRTY;
	return pte;
}

static inline pte_t pte_mkwrite(pte_t pte)
{
	pte_val(pte) |= _PAGE_WRITE;
	if (pte_val(pte) & _PAGE_MODIFIED)
		pte_val(pte) |= _PAGE_DIRTY;
	return pte;
}

static inline pte_t pte_wrprotect(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_WRITE | _PAGE_DIRTY);
	return pte;
}

static inline int pte_huge(pte_t pte)	{ return pte_val(pte) & _PAGE_HUGE; }

static inline pte_t pte_mkhuge(pte_t pte)
{
	pte_val(pte) |= _PAGE_HUGE;
	return pte;
}

#if defined(CONFIG_ARCH_HAS_PTE_SPECIAL)
static inline int pte_special(pte_t pte)	{ return pte_val(pte) & _PAGE_SPECIAL; }
static inline pte_t pte_mkspecial(pte_t pte)	{ pte_val(pte) |= _PAGE_SPECIAL; return pte; }
#endif /* CONFIG_ARCH_HAS_PTE_SPECIAL */

#define pte_accessible pte_accessible
static inline unsigned long pte_accessible(struct mm_struct *mm, pte_t a)
{
	if (pte_val(a) & _PAGE_PRESENT)
		return true;

	if ((pte_val(a) & _PAGE_PROTNONE) &&
			atomic_read(&mm->tlb_flush_pending))
		return true;

	return false;
}

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define mk_pte(page, pgprot)	pfn_pte(page_to_pfn(page), (pgprot))

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & _PAGE_CHG_MASK) |
		     (pgprot_val(newprot) & ~_PAGE_CHG_MASK));
}

extern void __update_tlb(struct vm_area_struct *vma,
			unsigned long address, pte_t *ptep);

static inline void update_mmu_cache(struct vm_area_struct *vma,
			unsigned long address, pte_t *ptep)
{
	__update_tlb(vma, address, ptep);
}

#define __HAVE_ARCH_UPDATE_MMU_TLB
#define update_mmu_tlb	update_mmu_cache

static inline void update_mmu_cache_pmd(struct vm_area_struct *vma,
			unsigned long address, pmd_t *pmdp)
{
	__update_tlb(vma, address, (pte_t *)pmdp);
}

static inline unsigned long pmd_pfn(pmd_t pmd)
{
	return (pmd_val(pmd) & _PFN_MASK) >> _PFN_SHIFT;
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE

/* We don't have hardware dirty/accessed bits, generic_pmdp_establish is fine.*/
#define pmdp_establish generic_pmdp_establish

static inline int pmd_trans_huge(pmd_t pmd)
{
	return !!(pmd_val(pmd) & _PAGE_HUGE) && pmd_present(pmd);
}

static inline pmd_t pmd_mkhuge(pmd_t pmd)
{
	pmd_val(pmd) = (pmd_val(pmd) & ~(_PAGE_GLOBAL)) |
		((pmd_val(pmd) & _PAGE_GLOBAL) << (_PAGE_HGLOBAL_SHIFT - _PAGE_GLOBAL_SHIFT));
	pmd_val(pmd) |= _PAGE_HUGE;

	return pmd;
}

#define pmd_write pmd_write
static inline int pmd_write(pmd_t pmd)
{
	return !!(pmd_val(pmd) & _PAGE_WRITE);
}

static inline pmd_t pmd_mkwrite(pmd_t pmd)
{
	pmd_val(pmd) |= _PAGE_WRITE;
	if (pmd_val(pmd) & _PAGE_MODIFIED)
		pmd_val(pmd) |= _PAGE_DIRTY;
	return pmd;
}

static inline pmd_t pmd_wrprotect(pmd_t pmd)
{
	pmd_val(pmd) &= ~(_PAGE_WRITE | _PAGE_DIRTY);
	return pmd;
}

static inline int pmd_dirty(pmd_t pmd)
{
	return !!(pmd_val(pmd) & (_PAGE_DIRTY | _PAGE_MODIFIED));
}

static inline pmd_t pmd_mkclean(pmd_t pmd)
{
	pmd_val(pmd) &= ~(_PAGE_DIRTY | _PAGE_MODIFIED);
	return pmd;
}

static inline pmd_t pmd_mkdirty(pmd_t pmd)
{
	pmd_val(pmd) |= _PAGE_MODIFIED;
	if (pmd_val(pmd) & _PAGE_WRITE)
		pmd_val(pmd) |= _PAGE_DIRTY;
	return pmd;
}

#define pmd_young pmd_young
static inline int pmd_young(pmd_t pmd)
{
	return !!(pmd_val(pmd) & _PAGE_ACCESSED);
}

static inline pmd_t pmd_mkold(pmd_t pmd)
{
	pmd_val(pmd) &= ~_PAGE_ACCESSED;
	return pmd;
}

static inline pmd_t pmd_mkyoung(pmd_t pmd)
{
	pmd_val(pmd) |= _PAGE_ACCESSED;
	return pmd;
}

static inline struct page *pmd_page(pmd_t pmd)
{
	if (pmd_trans_huge(pmd))
		return pfn_to_page(pmd_pfn(pmd));

	return pfn_to_page(pmd_phys(pmd) >> PAGE_SHIFT);
}

static inline pmd_t pmd_modify(pmd_t pmd, pgprot_t newprot)
{
	pmd_val(pmd) = (pmd_val(pmd) & _HPAGE_CHG_MASK) |
				(pgprot_val(newprot) & ~_HPAGE_CHG_MASK);
	return pmd;
}

static inline pmd_t pmd_mkinvalid(pmd_t pmd)
{
	pmd_val(pmd) |= _PAGE_PRESENT_INVALID;
	pmd_val(pmd) &= ~(_PAGE_PRESENT | _PAGE_VALID | _PAGE_DIRTY | _PAGE_PROTNONE);

	return pmd;
}

/*
 * The generic version pmdp_huge_get_and_clear uses a version of pmd_clear() with a
 * different prototype.
 */
#define __HAVE_ARCH_PMDP_HUGE_GET_AND_CLEAR
static inline pmd_t pmdp_huge_get_and_clear(struct mm_struct *mm,
					    unsigned long address, pmd_t *pmdp)
{
	pmd_t old = *pmdp;

	pmd_clear(pmdp);

	return old;
}

#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

#ifdef CONFIG_NUMA_BALANCING
static inline long pte_protnone(pte_t pte)
{
	return (pte_val(pte) & _PAGE_PROTNONE);
}

static inline long pmd_protnone(pmd_t pmd)
{
	return (pmd_val(pmd) & _PAGE_PROTNONE);
}
#endif /* CONFIG_NUMA_BALANCING */

/*
 * We provide our own get_unmapped area to cope with the virtual aliasing
 * constraints placed on us by the cache architecture.
 */
#define HAVE_ARCH_UNMAPPED_AREA
#define HAVE_ARCH_UNMAPPED_AREA_TOPDOWN

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_PGTABLE_H */
