/*
 * CRIS pgtable.h - macros and functions to manipulate page tables.
 */

#ifndef _CRIS_PGTABLE_H
#define _CRIS_PGTABLE_H

#include <asm/page.h>
#define __ARCH_USE_5LEVEL_HACK
#include <asm-generic/pgtable-nopmd.h>

#ifndef __ASSEMBLY__
#include <linux/sched/mm.h>
#include <asm/mmu.h>
#endif
#include <arch/pgtable.h>

/*
 * The Linux memory management assumes a three-level page table setup. On
 * CRIS, we use that, but "fold" the mid level into the top-level page
 * table. Since the MMU TLB is software loaded through an interrupt, it
 * supports any page table structure, so we could have used a three-level
 * setup, but for the amounts of memory we normally use, a two-level is
 * probably more efficient.
 *
 * This file contains the functions and defines necessary to modify and use
 * the CRIS page table tree.
 */
#ifndef __ASSEMBLY__
extern void paging_init(void);
#endif

/* Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#define set_pte(pteptr, pteval) ((*(pteptr)) = (pteval))
#define set_pte_at(mm,addr,ptep,pteval) set_pte(ptep,pteval)

/*
 * (pmds are folded into pgds so this doesn't get actually called,
 * but the define is needed for a generic inline function.)
 */
#define set_pmd(pmdptr, pmdval) (*(pmdptr) = pmdval)
#define set_pgu(pudptr, pudval) (*(pudptr) = pudval)

/* PGDIR_SHIFT determines the size of the area a second-level page table can
 * map. It is equal to the page size times the number of PTE's that fit in
 * a PMD page. A PTE is 4-bytes in CRIS. Hence the following number.
 */

#define PGDIR_SHIFT	(PAGE_SHIFT + (PAGE_SHIFT-2))
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * entries per page directory level: we use a two-level, so
 * we don't really have any PMD directory physically.
 * pointers are 4 bytes so we can use the page size and 
 * divide it by 4 (shift by 2).
 */
#define PTRS_PER_PTE	(1UL << (PAGE_SHIFT-2))
#define PTRS_PER_PGD	(1UL << (PAGE_SHIFT-2))

/* calculate how many PGD entries a user-level program can use
 * the first mappable virtual address is 0
 * (TASK_SIZE is the maximum virtual address space)
 */

#define USER_PTRS_PER_PGD       (TASK_SIZE/PGDIR_SIZE)
#define FIRST_USER_ADDRESS      0UL

/* zero page used for uninitialized stuff */
#ifndef __ASSEMBLY__
extern unsigned long empty_zero_page;
#define ZERO_PAGE(vaddr) (virt_to_page(empty_zero_page))
#endif

/* number of bits that fit into a memory pointer */
#define BITS_PER_PTR			(8*sizeof(unsigned long))

/* to align the pointer to a pointer address */
#define PTR_MASK			(~(sizeof(void*)-1))

/* sizeof(void*)==1<<SIZEOF_PTR_LOG2 */
/* 64-bit machines, beware!  SRB. */
#define SIZEOF_PTR_LOG2			2

/* to find an entry in a page-table */
#define PAGE_PTR(address) \
((unsigned long)(address)>>(PAGE_SHIFT-SIZEOF_PTR_LOG2)&PTR_MASK&~PAGE_MASK)

/* to set the page-dir */
#define SET_PAGE_DIR(tsk,pgdir)

#define pte_none(x)	(!pte_val(x))
#define pte_present(x)	(pte_val(x) & _PAGE_PRESENT)
#define pte_clear(mm,addr,xp)	do { pte_val(*(xp)) = 0; } while (0)

#define pmd_none(x)     (!pmd_val(x))
/* by removing the _PAGE_KERNEL bit from the comparison, the same pmd_bad
 * works for both _PAGE_TABLE and _KERNPG_TABLE pmd entries.
 */
#define	pmd_bad(x)	((pmd_val(x) & (~PAGE_MASK & ~_PAGE_KERNEL)) != _PAGE_TABLE)
#define pmd_present(x)	(pmd_val(x) & _PAGE_PRESENT)
#define pmd_clear(xp)	do { pmd_val(*(xp)) = 0; } while (0)

#ifndef __ASSEMBLY__

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */

static inline int pte_write(pte_t pte)          { return pte_val(pte) & _PAGE_WRITE; }
static inline int pte_dirty(pte_t pte)          { return pte_val(pte) & _PAGE_MODIFIED; }
static inline int pte_young(pte_t pte)          { return pte_val(pte) & _PAGE_ACCESSED; }
static inline int pte_special(pte_t pte)	{ return 0; }

static inline pte_t pte_wrprotect(pte_t pte)
{
        pte_val(pte) &= ~(_PAGE_WRITE | _PAGE_SILENT_WRITE);
        return pte;
}

static inline pte_t pte_mkclean(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_MODIFIED | _PAGE_SILENT_WRITE); 
	return pte; 
}

static inline pte_t pte_mkold(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_ACCESSED | _PAGE_SILENT_READ);
	return pte;
}

static inline pte_t pte_mkwrite(pte_t pte)
{
        pte_val(pte) |= _PAGE_WRITE;
        if (pte_val(pte) & _PAGE_MODIFIED)
                pte_val(pte) |= _PAGE_SILENT_WRITE;
        return pte;
}

static inline pte_t pte_mkdirty(pte_t pte)
{
        pte_val(pte) |= _PAGE_MODIFIED;
        if (pte_val(pte) & _PAGE_WRITE)
                pte_val(pte) |= _PAGE_SILENT_WRITE;
        return pte;
}

static inline pte_t pte_mkyoung(pte_t pte)
{
        pte_val(pte) |= _PAGE_ACCESSED;
        if (pte_val(pte) & _PAGE_READ)
        {
                pte_val(pte) |= _PAGE_SILENT_READ;
                if ((pte_val(pte) & (_PAGE_WRITE | _PAGE_MODIFIED)) ==
		    (_PAGE_WRITE | _PAGE_MODIFIED))
                        pte_val(pte) |= _PAGE_SILENT_WRITE;
        }
        return pte;
}
static inline pte_t pte_mkspecial(pte_t pte)	{ return pte; }

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */

/* What actually goes as arguments to the various functions is less than
 * obvious, but a rule of thumb is that struct page's goes as struct page *,
 * really physical DRAM addresses are unsigned long's, and DRAM "virtual"
 * addresses (the 0xc0xxxxxx's) goes as void *'s.
 */

static inline pte_t __mk_pte(void * page, pgprot_t pgprot)
{
	pte_t pte;
	/* the PTE needs a physical address */
	pte_val(pte) = __pa(page) | pgprot_val(pgprot);
	return pte;
}

#define mk_pte(page, pgprot) __mk_pte(page_address(page), (pgprot))

#define mk_pte_phys(physpage, pgprot) \
({                                                                      \
        pte_t __pte;                                                    \
                                                                        \
        pte_val(__pte) = (physpage) + pgprot_val(pgprot);               \
        __pte;                                                          \
})

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{ pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot); return pte; }

#define pgprot_noncached(prot) __pgprot((pgprot_val(prot) | _PAGE_NO_CACHE))


/* pte_val refers to a page in the 0x4xxxxxxx physical DRAM interval
 * __pte_page(pte_val) refers to the "virtual" DRAM interval
 * pte_pagenr refers to the page-number counted starting from the virtual DRAM start
 */

static inline unsigned long __pte_page(pte_t pte)
{
	/* the PTE contains a physical address */
	return (unsigned long)__va(pte_val(pte) & PAGE_MASK);
}

#define pte_pagenr(pte)         ((__pte_page(pte) - PAGE_OFFSET) >> PAGE_SHIFT)

/* permanent address of a page */

#define __page_address(page)    (PAGE_OFFSET + (((page) - mem_map) << PAGE_SHIFT))
#define pte_page(pte)           (mem_map+pte_pagenr(pte))

/* only the pte's themselves need to point to physical DRAM (see above)
 * the pagetable links are purely handled within the kernel SW and thus
 * don't need the __pa and __va transformations.
 */

static inline void pmd_set(pmd_t * pmdp, pte_t * ptep)
{ pmd_val(*pmdp) = _PAGE_TABLE | (unsigned long) ptep; }

#define pmd_page(pmd)		(pfn_to_page(pmd_val(pmd) >> PAGE_SHIFT))
#define pmd_page_vaddr(pmd)	((unsigned long) __va(pmd_val(pmd) & PAGE_MASK))

/* to find an entry in a page-table-directory. */
#define pgd_index(address) (((address) >> PGDIR_SHIFT) & (PTRS_PER_PGD-1))

/* to find an entry in a page-table-directory */
static inline pgd_t * pgd_offset(const struct mm_struct *mm, unsigned long address)
{
	return mm->pgd + pgd_index(address);
}

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/* Find an entry in the third-level page table.. */
#define __pte_offset(address) \
	(((address) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#define pte_offset_kernel(dir, address) \
	((pte_t *) pmd_page_vaddr(*(dir)) +  __pte_offset(address))
#define pte_offset_map(dir, address) \
	((pte_t *)page_address(pmd_page(*(dir))) + __pte_offset(address))

#define pte_unmap(pte) do { } while (0)
#define pte_pfn(x)		((unsigned long)(__va((x).pte)) >> PAGE_SHIFT)
#define pfn_pte(pfn, prot)	__pte(((pfn) << PAGE_SHIFT) | pgprot_val(prot))

#define pte_ERROR(e) \
        printk("%s:%d: bad pte %p(%08lx).\n", __FILE__, __LINE__, &(e), pte_val(e))
#define pgd_ERROR(e) \
        printk("%s:%d: bad pgd %p(%08lx).\n", __FILE__, __LINE__, &(e), pgd_val(e))


extern pgd_t swapper_pg_dir[PTRS_PER_PGD]; /* defined in head.S */

/*
 * CRIS doesn't have any external MMU info: the kernel page
 * tables contain all the necessary information.
 * 
 * Actually I am not sure on what this could be used for.
 */
static inline void update_mmu_cache(struct vm_area_struct * vma,
	unsigned long address, pte_t *ptep)
{
}

/* Encode and de-code a swap entry (must be !pte_none(e) && !pte_present(e)) */
/* Since the PAGE_PRESENT bit is bit 4, we can use the bits above */

#define __swp_type(x)			(((x).val >> 5) & 0x7f)
#define __swp_offset(x)			((x).val >> 12)
#define __swp_entry(type, offset)	((swp_entry_t) { ((type) << 5) | ((offset) << 12) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val })

#define kern_addr_valid(addr)   (1)

#include <asm-generic/pgtable.h>

/*
 * No page table caches to initialise
 */
#define pgtable_cache_init()   do { } while (0)

typedef pte_t *pte_addr_t;

#endif /* __ASSEMBLY__ */
#endif /* _CRIS_PGTABLE_H */
