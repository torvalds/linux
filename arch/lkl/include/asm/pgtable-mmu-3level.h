/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Derived from arch/um/include/asm/pgtable.h
 */
#ifndef _LKL_PGTABLE_MMU_3_LEVEL_H
#define _LKL_PGTABLE_MMU_3_LEVEL_H

#include <asm/page.h>
#include <asm-generic/pgtable-nopud.h>
#include <linux/mm_types.h>

#ifndef CONFIG_64BIT
#error Not supported bittness for LKL MMU.
#endif

#define _PAGE_PRESENT	0x001
#define _PAGE_NEWPAGE	0x002
#define _PAGE_NEWPROT	0x004
#define _PAGE_RW	0x020
#define _PAGE_USER	0x040
#define _PAGE_ACCESSED	0x080
#define _PAGE_DIRTY	0x100
/* If _PAGE_PRESENT is clear, we use these: */
#define _PAGE_PROTNONE	0x010	/* if the user mapped it with PROT_NONE; pte_present gives true */
#define _PAGE_SWP_EXCLUSIVE	0x400

/*
 * although we don't distinguish between user space and kernel space
 * reserver half of PGD for user space
 */
#define USER_PTRS_PER_PGD 256
#define FIRST_USER_ADDRESS	0UL


#define PGDIR_SHIFT	30
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

#define PMD_SHIFT	21
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/*
 * entries per page directory level
 */
#define PTRS_PER_PTE 512
#define PTRS_PER_PMD 512
#define PTRS_PER_PGD 512

#define pte_ERROR(e) \
		pr_err("%s:%d: bad pte %p(%016lx).\n", __FILE__, __LINE__, &(e), \
			pte_val(e))
#define pmd_ERROR(e) \
		pr_err("%s:%d: bad pmd %p(%016lx).\n", __FILE__, __LINE__, &(e), \
			pmd_val(e))
#define pgd_ERROR(e) \
		pr_err("%s:%d: bad pgd %p(%016lx).\n", __FILE__, __LINE__, &(e), \
			pgd_val(e))

#define pud_none(x)	(!(pud_val(x) & ~_PAGE_NEWPAGE))
#define	pud_bad(x)	((pud_val(x) & (~PAGE_MASK & ~_PAGE_USER)) != _KERNPG_TABLE)
#define pud_present(x)	(pud_val(x) & _PAGE_PRESENT)
#define pud_populate(mm, pud, pmd) \
	set_pud(pud, __pud(_PAGE_TABLE + __pa(pmd)))

#define set_pud(pudptr, pudval) (*(pudptr) = (pudval))

static inline int pgd_newpage(pgd_t pgd)
{
	return(pgd_val(pgd) & _PAGE_NEWPAGE);
}

static inline void pgd_mkuptodate(pgd_t pgd) { pgd_val(pgd) &= ~_PAGE_NEWPAGE; }

#define set_pmd(pmdptr, pmdval) (*(pmdptr) = (pmdval))

static inline void pud_clear(pud_t *pud)
{
	set_pud(pud, __pud(_PAGE_NEWPAGE));
}

#define pud_page(pud) phys_to_page(pud_val(pud) & PAGE_MASK)
#define pud_pgtable(pud) ((pmd_t *) __va(pud_val(pud) & PAGE_MASK))

static inline unsigned long pte_pfn(pte_t pte)
{
	return phys_to_pfn(pte_val(pte));
}

typedef unsigned long phys_t;

static inline pte_t pfn_pte(unsigned long page_nr, pgprot_t pgprot)
{
	pte_t pte;
	phys_t phys = pfn_to_phys(page_nr);

	pte_set_val(pte, phys, pgprot);
	return pte;
}

static inline pmd_t pfn_pmd(unsigned long page_nr, pgprot_t pgprot)
{
	return __pmd((page_nr << PAGE_SHIFT) | pgprot_val(pgprot));
}

#define set_pmd(pmdptr, pmdval) (*(pmdptr) = (pmdval))

#define pte_pfn(x) phys_to_pfn(pte_val(x))
#define pfn_pte(pfn, prot) __pte(pfn_to_phys(pfn) | pgprot_val(prot))
#define pfn_pmd(pfn, prot) __pmd(pfn_to_phys(pfn) | pgprot_val(prot))

#define pmd_pfn(pmd) (pmd_val(pmd) >> PAGE_SHIFT)

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

/* zero page used for uninitialized stuff */
extern void *empty_zero_page;

/* Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 */

extern unsigned long memory_end;

#define __va_space (8*1024*1024)

#define VMALLOC_OFFSET	(__va_space)
#define VMALLOC_START ((memory_end + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1))
// TODO:
#define PKMAP_BASE (((TASK_SIZE - 2 * PAGE_SIZE) - LAST_PKMAP * PAGE_SIZE) & PMD_MASK)
#define VMALLOC_END	((TASK_SIZE - 4 * PAGE_SIZE))
#define MODULES_VADDR	VMALLOC_START
#define MODULES_END	VMALLOC_END
#define MODULES_LEN	(MODULES_VADDR - MODULES_END)

#define _PAGE_TABLE	(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY)
#define _KERNPG_TABLE	(_PAGE_PRESENT | _PAGE_RW | _PAGE_ACCESSED | _PAGE_DIRTY)
#define _PAGE_CHG_MASK	(PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)
#define __PAGE_KERNEL_EXEC                                              \
	 (_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED)
#define PAGE_NONE	__pgprot(_PAGE_PROTNONE | _PAGE_ACCESSED)
#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_COPY	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_READONLY	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED)
#define PAGE_KERNEL_EXEC	__pgprot(__PAGE_KERNEL_EXEC)

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
#define ZERO_PAGE(vaddr) virt_to_page(empty_zero_page)


#define pmd_none(x)	(!((unsigned long)pmd_val(x) & ~_PAGE_NEWPAGE))
#define	pmd_bad(x)	((pmd_val(x) & (~PAGE_MASK & ~_PAGE_USER)) != _KERNPG_TABLE)

#define pmd_present(x)	(pmd_val(x) & _PAGE_PRESENT)
#define pmd_clear(xp)	do { pmd_val(*(xp)) = _PAGE_NEWPAGE; } while (0)

#define pmd_newpage(x)  (pmd_val(x) & _PAGE_NEWPAGE)
#define pmd_mkuptodate(x) (pmd_val(x) &= ~_PAGE_NEWPAGE)

#define pud_newpage(x)  (pud_val(x) & _PAGE_NEWPAGE)
#define pud_mkuptodate(x) (pud_val(x) &= ~_PAGE_NEWPAGE)

#define p4d_newpage(x)  (p4d_val(x) & _PAGE_NEWPAGE)
#define p4d_mkuptodate(x) (p4d_val(x) &= ~_PAGE_NEWPAGE)

#define pmd_page(pmd) phys_to_page(pmd_val(pmd) & PAGE_MASK)

#define pte_page(x) pfn_to_page(pte_pfn(x))

#define pte_present(x)	pte_get_bits(x, (_PAGE_PRESENT | _PAGE_PROTNONE))

void mmap_pages_for_ptes(unsigned long va, unsigned int nr, pte_t pte);
void munmap_page_for_pte(unsigned long addr, pte_t *xp);

static inline void pte_clear(struct mm_struct *mm, unsigned long addr, pte_t *xp)
{
	if (pte_present(*xp))
		munmap_page_for_pte(addr, xp);

	pte_set_val(*(xp), 0, __pgprot(_PAGE_NEWPAGE));
}

/*
 * =================================
 * Flags checking section.
 * =================================
 */

static inline int pte_none(pte_t pte)
{
	return pte_is_zero(pte);
}

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
static inline int pte_read(pte_t pte)
{
	return ((pte_get_bits(pte, _PAGE_USER)) &&
	       !(pte_get_bits(pte, _PAGE_PROTNONE)));
}

static inline int pte_exec(pte_t pte)
{
	return ((pte_get_bits(pte, _PAGE_USER)) &&
	       !(pte_get_bits(pte, _PAGE_PROTNONE)));
}

static inline int pte_write(pte_t pte)
{
	return ((pte_get_bits(pte, _PAGE_RW)) &&
	       !(pte_get_bits(pte, _PAGE_PROTNONE)));
}

static inline int pte_dirty(pte_t pte)
{
	return pte_get_bits(pte, _PAGE_DIRTY);
}

static inline int pte_young(pte_t pte)
{
	return pte_get_bits(pte, _PAGE_ACCESSED);
}

static inline int pte_newpage(pte_t pte)
{
	return pte_get_bits(pte, _PAGE_NEWPAGE);
}

static inline int pte_newprot(pte_t pte)
{
	return(pte_present(pte) && (pte_get_bits(pte, _PAGE_NEWPROT)));
}

/*
 * =================================
 * Flags setting section.
 * =================================
 */

static inline pte_t pte_mknewprot(pte_t pte)
{
	pte_set_bits(pte, _PAGE_NEWPROT);
	return pte;
}

static inline pte_t pte_mkclean(pte_t pte)
{
	pte_clear_bits(pte, _PAGE_DIRTY);
	return pte;
}

static inline pte_t pte_mkold(pte_t pte)
{
	pte_clear_bits(pte, _PAGE_ACCESSED);
	return pte;
}

static inline pte_t pte_wrprotect(pte_t pte)
{
	if (likely(pte_get_bits(pte, _PAGE_RW)))
		pte_clear_bits(pte, _PAGE_RW);
	else
		return pte;
	return pte_mknewprot(pte);
}

static inline pte_t pte_mkread(pte_t pte)
{
	if (unlikely(pte_get_bits(pte, _PAGE_USER)))
		return pte;
	pte_set_bits(pte, _PAGE_USER);
	return pte_mknewprot(pte);
}

static inline pte_t pte_mkdirty(pte_t pte)
{
	pte_set_bits(pte, _PAGE_DIRTY);
	return pte;
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	pte_set_bits(pte, _PAGE_ACCESSED);
	return pte;
}

static inline pte_t pte_mkwrited(pte_t pte, struct vm_area_struct *vma)
{
	if (unlikely(pte_get_bits(pte,  _PAGE_RW)))
		return pte;
	pte_set_bits(pte, _PAGE_RW);
	return pte_mknewprot(pte);
}

static inline pte_t pte_mkwrite_novma(pte_t pte)
{
	if (unlikely(pte_get_bits(pte,  _PAGE_RW)))
		return pte;
	pte_set_bits(pte, _PAGE_RW);
	return pte_mknewprot(pte);
}

static inline int pte_swp_exclusive(pte_t pte)
{
	return pte_get_bits(pte, _PAGE_SWP_EXCLUSIVE);
}

static inline pte_t pte_swp_mkexclusive(pte_t pte)
{
	pte_set_bits(pte, _PAGE_SWP_EXCLUSIVE);
	return pte;
}

static inline pte_t pte_swp_clear_exclusive(pte_t pte)
{
	pte_clear_bits(pte, _PAGE_SWP_EXCLUSIVE);
	return pte;
}

static inline void update_mmu_cache_range(struct vm_fault *vmf,
		struct vm_area_struct *vma, unsigned long address,
		pte_t *ptep, unsigned int nr)
{
}

static inline pte_t pte_mkuptodate(pte_t pte)
{
	pte_clear_bits(pte, _PAGE_NEWPAGE);
	if (pte_present(pte))
		pte_clear_bits(pte, _PAGE_NEWPROT);
	return pte;
}

static inline pte_t pte_mknewpage(pte_t pte)
{
	pte_set_bits(pte, _PAGE_NEWPAGE);
	return pte;
}

static inline void __set_pte(pte_t *pteptr, pte_t pteval)
{
	pte_copy(*pteptr, pteval);

	/* If it's a swap entry, it needs to be marked _PAGE_NEWPAGE so
	 * fix_range knows to unmap it.  _PAGE_NEWPROT is specific to
	 * mapped pages.
	 */

	*pteptr = pte_mknewpage(*pteptr);
	if (pte_present(*pteptr))
		*pteptr = pte_mknewprot(*pteptr);
}

static inline pte_t __pte_next_pfn(pte_t pte)
{
	return __pte(pte_val(pte) + (1UL << PFN_PTE_SHIFT));
}

static inline void __set_ptes(struct mm_struct *mm, unsigned long addr,
		pte_t *ptep, pte_t pte, unsigned int nr)
{
	if (pte_present(pte))
		mmap_pages_for_ptes(addr, nr, pte);

	for (;;) {
		__set_pte(ptep, pte);
		if (--nr == 0)
			break;
		ptep++;
		pte = __pte_next_pfn(pte);
	}
}
#define set_ptes(mm, addr, ptep, pte, nr) __set_ptes(mm, addr, ptep, pte, nr)

#define __HAVE_ARCH_PTE_SAME
static inline int pte_same(pte_t pte_a, pte_t pte_b)
{
	return !((pte_val(pte_a) ^ pte_val(pte_b)) & ~_PAGE_NEWPAGE);
}

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */

#define phys_to_page(phys) pfn_to_page(phys_to_pfn(phys))
#define __virt_to_page(virt) phys_to_page(__pa(virt))
#define page_to_phys(page) pfn_to_phys(page_to_pfn(page))
#define virt_to_page(addr) __virt_to_page((const unsigned long) addr)

#define mk_pte(page, pgprot) \
	({ pte_t pte;					\
							\
	pte_set_val(pte, page_to_phys(page), (pgprot));	\
	if (pte_present(pte))				\
		pte_mknewprot(pte_mknewpage(pte));	\
	pte; })

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	pte_set_val(pte, (pte_val(pte) & _PAGE_CHG_MASK), newprot);
	return pte;
}

/*
 * the pmd page can be thought of an array like this: pmd_t[PTRS_PER_PMD]
 *
 * this macro returns the index of the entry in the pmd page which would
 * control the given virtual address
 */
#define pmd_page_vaddr(pmd) ((unsigned long) __va(pmd_val(pmd) & PAGE_MASK))

#define update_mmu_cache(vma, address, ptep) do { } while (0)

/* Encode and de-code a swap entry */
#define __swp_type(x)			(((x).val >> 5) & 0x1f)
#define __swp_offset(x)			((x).val >> 11)

#define __swp_entry(type, offset) \
	((swp_entry_t) { ((type) << 5) | ((offset) << 11) })
#define __pte_to_swp_entry(pte) \
	((swp_entry_t) { pte_val(pte_mkuptodate(pte)) })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val })

#endif // _LKL_PGTABLE_MMU_3_LEVEL_H
