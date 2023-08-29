/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 * et al.
 */

/* or1k pgtable.h - macros and functions to manipulate page tables
 *
 * Based on:
 * include/asm-cris/pgtable.h
 */

#ifndef __ASM_OPENRISC_PGTABLE_H
#define __ASM_OPENRISC_PGTABLE_H

#include <asm-generic/pgtable-nopmd.h>

#ifndef __ASSEMBLY__
#include <asm/mmu.h>
#include <asm/fixmap.h>

/*
 * The Linux memory management assumes a three-level page table setup. On
 * or1k, we use that, but "fold" the mid level into the top-level page
 * table. Since the MMU TLB is software loaded through an interrupt, it
 * supports any page table structure, so we could have used a three-level
 * setup, but for the amounts of memory we normally use, a two-level is
 * probably more efficient.
 *
 * This file contains the functions and defines necessary to modify and use
 * the or1k page table tree.
 */

extern void paging_init(void);

/* Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#define set_pte(pteptr, pteval) ((*(pteptr)) = (pteval))

/*
 * (pmds are folded into pgds so this doesn't get actually called,
 * but the define is needed for a generic inline function.)
 */
#define set_pmd(pmdptr, pmdval) (*(pmdptr) = pmdval)

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

#define PTRS_PER_PGD	(1UL << (32-PGDIR_SHIFT))

/* calculate how many PGD entries a user-level program can use
 * the first mappable virtual address is 0
 * (TASK_SIZE is the maximum virtual address space)
 */

#define USER_PTRS_PER_PGD       (TASK_SIZE/PGDIR_SIZE)

/*
 * Kernels own virtual memory area.
 */

/*
 * The size and location of the vmalloc area are chosen so that modules
 * placed in this area aren't more than a 28-bit signed offset from any
 * kernel functions that they may need.  This greatly simplifies handling
 * of the relocations for l.j and l.jal instructions as we don't need to
 * introduce any trampolines for reaching "distant" code.
 *
 * 64 MB of vmalloc area is comparable to what's available on other arches.
 */

#define VMALLOC_START	(PAGE_OFFSET-0x04000000UL)
#define VMALLOC_END	(PAGE_OFFSET)
#define VMALLOC_VMADDR(x) ((unsigned long)(x))

/* Define some higher level generic page attributes.
 *
 * If you change _PAGE_CI definition be sure to change it in
 * io.h for ioremap() too.
 */

/*
 * An OR32 PTE looks like this:
 *
 * |  31 ... 10 |  9  |  8 ... 6  |  5  |  4  |  3  |  2  |  1  |  0  |
 *  Phys pg.num    L     PP Index    D     A    WOM   WBC   CI    CC
 *
 *  L  : link
 *  PPI: Page protection index
 *  D  : Dirty
 *  A  : Accessed
 *  WOM: Weakly ordered memory
 *  WBC: Write-back cache
 *  CI : Cache inhibit
 *  CC : Cache coherent
 *
 * The protection bits below should correspond to the layout of the actual
 * PTE as per above
 */

#define _PAGE_CC       0x001 /* software: pte contains a translation */
#define _PAGE_CI       0x002 /* cache inhibit          */
#define _PAGE_WBC      0x004 /* write back cache       */
#define _PAGE_WOM      0x008 /* weakly ordered memory  */

#define _PAGE_A        0x010 /* accessed               */
#define _PAGE_D        0x020 /* dirty                  */
#define _PAGE_URE      0x040 /* user read enable       */
#define _PAGE_UWE      0x080 /* user write enable      */

#define _PAGE_SRE      0x100 /* superuser read enable  */
#define _PAGE_SWE      0x200 /* superuser write enable */
#define _PAGE_EXEC     0x400 /* software: page is executable */
#define _PAGE_U_SHARED 0x800 /* software: page is shared in user space */

/* 0x001 is cache coherency bit, which should always be set to
 *       1 - for SMP (when we support it)
 *       0 - otherwise
 *
 * we just reuse this bit in software for _PAGE_PRESENT and
 * force it to 0 when loading it into TLB.
 */
#define _PAGE_PRESENT  _PAGE_CC
#define _PAGE_USER     _PAGE_URE
#define _PAGE_WRITE    (_PAGE_UWE | _PAGE_SWE)
#define _PAGE_DIRTY    _PAGE_D
#define _PAGE_ACCESSED _PAGE_A
#define _PAGE_NO_CACHE _PAGE_CI
#define _PAGE_SHARED   _PAGE_U_SHARED
#define _PAGE_READ     (_PAGE_URE | _PAGE_SRE)

#define _PAGE_CHG_MASK	(PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)
#define _PAGE_BASE     (_PAGE_PRESENT | _PAGE_ACCESSED)
#define _PAGE_ALL      (_PAGE_PRESENT | _PAGE_ACCESSED)
#define _KERNPG_TABLE \
	(_PAGE_BASE | _PAGE_SRE | _PAGE_SWE | _PAGE_ACCESSED | _PAGE_DIRTY)

/* We borrow bit 11 to store the exclusive marker in swap PTEs. */
#define _PAGE_SWP_EXCLUSIVE	_PAGE_U_SHARED

#define PAGE_NONE       __pgprot(_PAGE_ALL)
#define PAGE_READONLY   __pgprot(_PAGE_ALL | _PAGE_URE | _PAGE_SRE)
#define PAGE_READONLY_X __pgprot(_PAGE_ALL | _PAGE_URE | _PAGE_SRE | _PAGE_EXEC)
#define PAGE_SHARED \
	__pgprot(_PAGE_ALL | _PAGE_URE | _PAGE_SRE | _PAGE_UWE | _PAGE_SWE \
		 | _PAGE_SHARED)
#define PAGE_SHARED_X \
	__pgprot(_PAGE_ALL | _PAGE_URE | _PAGE_SRE | _PAGE_UWE | _PAGE_SWE \
		 | _PAGE_SHARED | _PAGE_EXEC)
#define PAGE_COPY       __pgprot(_PAGE_ALL | _PAGE_URE | _PAGE_SRE)
#define PAGE_COPY_X     __pgprot(_PAGE_ALL | _PAGE_URE | _PAGE_SRE | _PAGE_EXEC)

#define PAGE_KERNEL \
	__pgprot(_PAGE_ALL | _PAGE_SRE | _PAGE_SWE \
		 | _PAGE_SHARED | _PAGE_DIRTY | _PAGE_EXEC)
#define PAGE_KERNEL_RO \
	__pgprot(_PAGE_ALL | _PAGE_SRE \
		 | _PAGE_SHARED | _PAGE_DIRTY | _PAGE_EXEC)
#define PAGE_KERNEL_NOCACHE \
	__pgprot(_PAGE_ALL | _PAGE_SRE | _PAGE_SWE \
		 | _PAGE_SHARED | _PAGE_DIRTY | _PAGE_EXEC | _PAGE_CI)

/* zero page used for uninitialized stuff */
extern unsigned long empty_zero_page[2048];
#define ZERO_PAGE(vaddr) (virt_to_page(empty_zero_page))

/* number of bits that fit into a memory pointer */
#define BITS_PER_PTR			(8*sizeof(unsigned long))

/* to align the pointer to a pointer address */
#define PTR_MASK			(~(sizeof(void *)-1))

/* sizeof(void*)==1<<SIZEOF_PTR_LOG2 */
/* 64-bit machines, beware!  SRB. */
#define SIZEOF_PTR_LOG2			2

/* to find an entry in a page-table */
#define PAGE_PTR(address) \
((unsigned long)(address)>>(PAGE_SHIFT-SIZEOF_PTR_LOG2)&PTR_MASK&~PAGE_MASK)

/* to set the page-dir */
#define SET_PAGE_DIR(tsk, pgdir)

#define pte_none(x)	(!pte_val(x))
#define pte_present(x)	(pte_val(x) & _PAGE_PRESENT)
#define pte_clear(mm, addr, xp)	do { pte_val(*(xp)) = 0; } while (0)

#define pmd_none(x)	(!pmd_val(x))
#define	pmd_bad(x)	((pmd_val(x) & (~PAGE_MASK)) != _KERNPG_TABLE)
#define pmd_present(x)	(pmd_val(x) & _PAGE_PRESENT)
#define pmd_clear(xp)	do { pmd_val(*(xp)) = 0; } while (0)

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */

static inline int pte_read(pte_t pte)  { return pte_val(pte) & _PAGE_READ; }
static inline int pte_write(pte_t pte) { return pte_val(pte) & _PAGE_WRITE; }
static inline int pte_exec(pte_t pte)  { return pte_val(pte) & _PAGE_EXEC; }
static inline int pte_dirty(pte_t pte) { return pte_val(pte) & _PAGE_DIRTY; }
static inline int pte_young(pte_t pte) { return pte_val(pte) & _PAGE_ACCESSED; }

static inline pte_t pte_wrprotect(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_WRITE);
	return pte;
}

static inline pte_t pte_rdprotect(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_READ);
	return pte;
}

static inline pte_t pte_exprotect(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_EXEC);
	return pte;
}

static inline pte_t pte_mkclean(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_DIRTY);
	return pte;
}

static inline pte_t pte_mkold(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_ACCESSED);
	return pte;
}

static inline pte_t pte_mkwrite(pte_t pte)
{
	pte_val(pte) |= _PAGE_WRITE;
	return pte;
}

static inline pte_t pte_mkread(pte_t pte)
{
	pte_val(pte) |= _PAGE_READ;
	return pte;
}

static inline pte_t pte_mkexec(pte_t pte)
{
	pte_val(pte) |= _PAGE_EXEC;
	return pte;
}

static inline pte_t pte_mkdirty(pte_t pte)
{
	pte_val(pte) |= _PAGE_DIRTY;
	return pte;
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	pte_val(pte) |= _PAGE_ACCESSED;
	return pte;
}

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */

/* What actually goes as arguments to the various functions is less than
 * obvious, but a rule of thumb is that struct page's goes as struct page *,
 * really physical DRAM addresses are unsigned long's, and DRAM "virtual"
 * addresses (the 0xc0xxxxxx's) goes as void *'s.
 */

static inline pte_t __mk_pte(void *page, pgprot_t pgprot)
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
{
	pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot);
	return pte;
}


/*
 * pte_val refers to a page in the 0x0xxxxxxx physical DRAM interval
 * __pte_page(pte_val) refers to the "virtual" DRAM interval
 * pte_pagenr refers to the page-number counted starting from the virtual
 * DRAM start
 */

static inline unsigned long __pte_page(pte_t pte)
{
	/* the PTE contains a physical address */
	return (unsigned long)__va(pte_val(pte) & PAGE_MASK);
}

#define pte_pagenr(pte)         ((__pte_page(pte) - PAGE_OFFSET) >> PAGE_SHIFT)

/* permanent address of a page */

#define __page_address(page) (PAGE_OFFSET + (((page) - mem_map) << PAGE_SHIFT))
#define pte_page(pte)		(mem_map+pte_pagenr(pte))

/*
 * only the pte's themselves need to point to physical DRAM (see above)
 * the pagetable links are purely handled within the kernel SW and thus
 * don't need the __pa and __va transformations.
 */
static inline void pmd_set(pmd_t *pmdp, pte_t *ptep)
{
	pmd_val(*pmdp) = _KERNPG_TABLE | (unsigned long) ptep;
}

#define pmd_pfn(pmd)		(pmd_val(pmd) >> PAGE_SHIFT)
#define pmd_page(pmd)		(pfn_to_page(pmd_val(pmd) >> PAGE_SHIFT))

static inline unsigned long pmd_page_vaddr(pmd_t pmd)
{
	return ((unsigned long) __va(pmd_val(pmd) & PAGE_MASK));
}

#define __pmd_offset(address) \
	(((address) >> PMD_SHIFT) & (PTRS_PER_PMD-1))

#define PFN_PTE_SHIFT		PAGE_SHIFT
#define pte_pfn(x)		((unsigned long)(((x).pte)) >> PAGE_SHIFT)
#define pfn_pte(pfn, prot)  __pte((((pfn) << PAGE_SHIFT)) | pgprot_val(prot))

#define pte_ERROR(e) \
	printk(KERN_ERR "%s:%d: bad pte %p(%08lx).\n", \
	       __FILE__, __LINE__, &(e), pte_val(e))
#define pgd_ERROR(e) \
	printk(KERN_ERR "%s:%d: bad pgd %p(%08lx).\n", \
	       __FILE__, __LINE__, &(e), pgd_val(e))

extern pgd_t swapper_pg_dir[PTRS_PER_PGD]; /* defined in head.S */

struct vm_area_struct;

static inline void update_tlb(struct vm_area_struct *vma,
	unsigned long address, pte_t *pte)
{
}

extern void update_cache(struct vm_area_struct *vma,
	unsigned long address, pte_t *pte);

static inline void update_mmu_cache_range(struct vm_fault *vmf,
		struct vm_area_struct *vma, unsigned long address,
		pte_t *ptep, unsigned int nr)
{
	update_tlb(vma, address, ptep);
	update_cache(vma, address, ptep);
}

#define update_mmu_cache(vma, addr, ptep) \
	update_mmu_cache_range(NULL, vma, addr, ptep, 1)

/* __PHX__ FIXME, SWAP, this probably doesn't work */

/*
 * Encode/decode swap entries and swap PTEs. Swap PTEs are all PTEs that
 * are !pte_none() && !pte_present().
 *
 * Format of swap PTEs:
 *
 *   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
 *   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *   <-------------- offset ---------------> E <- type --> 0 0 0 0 0
 *
 *   E is the exclusive marker that is not stored in swap entries.
 *   The zero'ed bits include _PAGE_PRESENT.
 */
#define __swp_type(x)			(((x).val >> 5) & 0x3f)
#define __swp_offset(x)			((x).val >> 12)
#define __swp_entry(type, offset) \
	((swp_entry_t) { (((type) & 0x3f) << 5) | ((offset) << 12) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val })

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

typedef pte_t *pte_addr_t;

#endif /* __ASSEMBLY__ */
#endif /* __ASM_OPENRISC_PGTABLE_H */
