/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#ifndef _ASM_RISCV_PGTABLE_H
#define _ASM_RISCV_PGTABLE_H

#include <linux/mmzone.h>

#include <asm/pgtable-bits.h>

#ifndef __ASSEMBLY__

/* Page Upper Directory not used in RISC-V */
#include <asm-generic/pgtable-nopud.h>
#include <asm/page.h>
#include <asm/tlbflush.h>
#include <linux/mm_types.h>

#ifdef CONFIG_64BIT
#include <asm/pgtable-64.h>
#else
#include <asm/pgtable-32.h>
#endif /* CONFIG_64BIT */

/* Number of entries in the page global directory */
#define PTRS_PER_PGD    (PAGE_SIZE / sizeof(pgd_t))
/* Number of entries in the page table */
#define PTRS_PER_PTE    (PAGE_SIZE / sizeof(pte_t))

/* Number of PGD entries that a user-mode program can use */
#define USER_PTRS_PER_PGD   (TASK_SIZE / PGDIR_SIZE)
#define FIRST_USER_ADDRESS  0

/* Page protection bits */
#define _PAGE_BASE	(_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_USER)

#define PAGE_NONE		__pgprot(_PAGE_PROT_NONE)
#define PAGE_READ		__pgprot(_PAGE_BASE | _PAGE_READ)
#define PAGE_WRITE		__pgprot(_PAGE_BASE | _PAGE_READ | _PAGE_WRITE)
#define PAGE_EXEC		__pgprot(_PAGE_BASE | _PAGE_EXEC)
#define PAGE_READ_EXEC		__pgprot(_PAGE_BASE | _PAGE_READ | _PAGE_EXEC)
#define PAGE_WRITE_EXEC		__pgprot(_PAGE_BASE | _PAGE_READ |	\
					 _PAGE_EXEC | _PAGE_WRITE)

#define PAGE_COPY		PAGE_READ
#define PAGE_COPY_EXEC		PAGE_EXEC
#define PAGE_COPY_READ_EXEC	PAGE_READ_EXEC
#define PAGE_SHARED		PAGE_WRITE
#define PAGE_SHARED_EXEC	PAGE_WRITE_EXEC

#define _PAGE_KERNEL		(_PAGE_READ \
				| _PAGE_WRITE \
				| _PAGE_PRESENT \
				| _PAGE_ACCESSED \
				| _PAGE_DIRTY)

#define PAGE_KERNEL		__pgprot(_PAGE_KERNEL)
#define PAGE_KERNEL_EXEC	__pgprot(_PAGE_KERNEL | _PAGE_EXEC)

#define PAGE_TABLE		__pgprot(_PAGE_TABLE)

extern pgd_t swapper_pg_dir[];

/* MAP_PRIVATE permissions: xwr (copy-on-write) */
#define __P000	PAGE_NONE
#define __P001	PAGE_READ
#define __P010	PAGE_COPY
#define __P011	PAGE_COPY
#define __P100	PAGE_EXEC
#define __P101	PAGE_READ_EXEC
#define __P110	PAGE_COPY_EXEC
#define __P111	PAGE_COPY_READ_EXEC

/* MAP_SHARED permissions: xwr */
#define __S000	PAGE_NONE
#define __S001	PAGE_READ
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_EXEC
#define __S101	PAGE_READ_EXEC
#define __S110	PAGE_SHARED_EXEC
#define __S111	PAGE_SHARED_EXEC

/*
 * ZERO_PAGE is a global shared page that is always zero,
 * used for zero-mapped memory areas, etc.
 */
extern unsigned long empty_zero_page[PAGE_SIZE / sizeof(unsigned long)];
#define ZERO_PAGE(vaddr) (virt_to_page(empty_zero_page))

static inline int pmd_present(pmd_t pmd)
{
	return (pmd_val(pmd) & (_PAGE_PRESENT | _PAGE_PROT_NONE));
}

static inline int pmd_none(pmd_t pmd)
{
	return (pmd_val(pmd) == 0);
}

static inline int pmd_bad(pmd_t pmd)
{
	return !pmd_present(pmd);
}

static inline void set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	*pmdp = pmd;
}

static inline void pmd_clear(pmd_t *pmdp)
{
	set_pmd(pmdp, __pmd(0));
}

static inline pgd_t pfn_pgd(unsigned long pfn, pgprot_t prot)
{
	return __pgd((pfn << _PAGE_PFN_SHIFT) | pgprot_val(prot));
}

static inline unsigned long _pgd_pfn(pgd_t pgd)
{
	return pgd_val(pgd) >> _PAGE_PFN_SHIFT;
}

#define pgd_index(addr) (((addr) >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1))

/* Locate an entry in the page global directory */
static inline pgd_t *pgd_offset(const struct mm_struct *mm, unsigned long addr)
{
	return mm->pgd + pgd_index(addr);
}
/* Locate an entry in the kernel page global directory */
#define pgd_offset_k(addr)      pgd_offset(&init_mm, (addr))

static inline struct page *pmd_page(pmd_t pmd)
{
	return pfn_to_page(pmd_val(pmd) >> _PAGE_PFN_SHIFT);
}

static inline unsigned long pmd_page_vaddr(pmd_t pmd)
{
	return (unsigned long)pfn_to_virt(pmd_val(pmd) >> _PAGE_PFN_SHIFT);
}

/* Yields the page frame number (PFN) of a page table entry */
static inline unsigned long pte_pfn(pte_t pte)
{
	return (pte_val(pte) >> _PAGE_PFN_SHIFT);
}

#define pte_page(x)     pfn_to_page(pte_pfn(x))

/* Constructs a page table entry */
static inline pte_t pfn_pte(unsigned long pfn, pgprot_t prot)
{
	return __pte((pfn << _PAGE_PFN_SHIFT) | pgprot_val(prot));
}

static inline pte_t mk_pte(struct page *page, pgprot_t prot)
{
	return pfn_pte(page_to_pfn(page), prot);
}

#define pte_index(addr) (((addr) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))

static inline pte_t *pte_offset_kernel(pmd_t *pmd, unsigned long addr)
{
	return (pte_t *)pmd_page_vaddr(*pmd) + pte_index(addr);
}

#define pte_offset_map(dir, addr)	pte_offset_kernel((dir), (addr))
#define pte_unmap(pte)			((void)(pte))

static inline int pte_present(pte_t pte)
{
	return (pte_val(pte) & (_PAGE_PRESENT | _PAGE_PROT_NONE));
}

static inline int pte_none(pte_t pte)
{
	return (pte_val(pte) == 0);
}

static inline int pte_write(pte_t pte)
{
	return pte_val(pte) & _PAGE_WRITE;
}

static inline int pte_exec(pte_t pte)
{
	return pte_val(pte) & _PAGE_EXEC;
}

static inline int pte_huge(pte_t pte)
{
	return pte_present(pte)
		&& (pte_val(pte) & (_PAGE_READ | _PAGE_WRITE | _PAGE_EXEC));
}

static inline int pte_dirty(pte_t pte)
{
	return pte_val(pte) & _PAGE_DIRTY;
}

static inline int pte_young(pte_t pte)
{
	return pte_val(pte) & _PAGE_ACCESSED;
}

static inline int pte_special(pte_t pte)
{
	return pte_val(pte) & _PAGE_SPECIAL;
}

/* static inline pte_t pte_rdprotect(pte_t pte) */

static inline pte_t pte_wrprotect(pte_t pte)
{
	return __pte(pte_val(pte) & ~(_PAGE_WRITE));
}

/* static inline pte_t pte_mkread(pte_t pte) */

static inline pte_t pte_mkwrite(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_WRITE);
}

/* static inline pte_t pte_mkexec(pte_t pte) */

static inline pte_t pte_mkdirty(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_DIRTY);
}

static inline pte_t pte_mkclean(pte_t pte)
{
	return __pte(pte_val(pte) & ~(_PAGE_DIRTY));
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_ACCESSED);
}

static inline pte_t pte_mkold(pte_t pte)
{
	return __pte(pte_val(pte) & ~(_PAGE_ACCESSED));
}

static inline pte_t pte_mkspecial(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_SPECIAL);
}

static inline pte_t pte_mkhuge(pte_t pte)
{
	return pte;
}

/* Modify page protection bits */
static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot));
}

#define pgd_ERROR(e) \
	pr_err("%s:%d: bad pgd " PTE_FMT ".\n", __FILE__, __LINE__, pgd_val(e))


/* Commit new configuration to MMU hardware */
static inline void update_mmu_cache(struct vm_area_struct *vma,
	unsigned long address, pte_t *ptep)
{
	/*
	 * The kernel assumes that TLBs don't cache invalid entries, but
	 * in RISC-V, SFENCE.VMA specifies an ordering constraint, not a
	 * cache flush; it is necessary even after writing invalid entries.
	 * Relying on flush_tlb_fix_spurious_fault would suffice, but
	 * the extra traps reduce performance.  So, eagerly SFENCE.VMA.
	 */
	local_flush_tlb_page(address);
}

#define __HAVE_ARCH_PTE_SAME
static inline int pte_same(pte_t pte_a, pte_t pte_b)
{
	return pte_val(pte_a) == pte_val(pte_b);
}

/*
 * Certain architectures need to do special things when PTEs within
 * a page table are directly modified.  Thus, the following hook is
 * made available.
 */
static inline void set_pte(pte_t *ptep, pte_t pteval)
{
	*ptep = pteval;
}

void flush_icache_pte(pte_t pte);

static inline void set_pte_at(struct mm_struct *mm,
	unsigned long addr, pte_t *ptep, pte_t pteval)
{
	if (pte_present(pteval) && pte_exec(pteval))
		flush_icache_pte(pteval);

	set_pte(ptep, pteval);
}

static inline void pte_clear(struct mm_struct *mm,
	unsigned long addr, pte_t *ptep)
{
	set_pte_at(mm, addr, ptep, __pte(0));
}

#define __HAVE_ARCH_PTEP_SET_ACCESS_FLAGS
static inline int ptep_set_access_flags(struct vm_area_struct *vma,
					unsigned long address, pte_t *ptep,
					pte_t entry, int dirty)
{
	if (!pte_same(*ptep, entry))
		set_pte_at(vma->vm_mm, address, ptep, entry);
	/*
	 * update_mmu_cache will unconditionally execute, handling both
	 * the case that the PTE changed and the spurious fault case.
	 */
	return true;
}

#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
static inline pte_t ptep_get_and_clear(struct mm_struct *mm,
				       unsigned long address, pte_t *ptep)
{
	return __pte(atomic_long_xchg((atomic_long_t *)ptep, 0));
}

#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
static inline int ptep_test_and_clear_young(struct vm_area_struct *vma,
					    unsigned long address,
					    pte_t *ptep)
{
	if (!pte_young(*ptep))
		return 0;
	return test_and_clear_bit(_PAGE_ACCESSED_OFFSET, &pte_val(*ptep));
}

#define __HAVE_ARCH_PTEP_SET_WRPROTECT
static inline void ptep_set_wrprotect(struct mm_struct *mm,
				      unsigned long address, pte_t *ptep)
{
	atomic_long_and(~(unsigned long)_PAGE_WRITE, (atomic_long_t *)ptep);
}

#define __HAVE_ARCH_PTEP_CLEAR_YOUNG_FLUSH
static inline int ptep_clear_flush_young(struct vm_area_struct *vma,
					 unsigned long address, pte_t *ptep)
{
	/*
	 * This comment is borrowed from x86, but applies equally to RISC-V:
	 *
	 * Clearing the accessed bit without a TLB flush
	 * doesn't cause data corruption. [ It could cause incorrect
	 * page aging and the (mistaken) reclaim of hot pages, but the
	 * chance of that should be relatively low. ]
	 *
	 * So as a performance optimization don't flush the TLB when
	 * clearing the accessed bit, it will eventually be flushed by
	 * a context switch or a VM operation anyway. [ In the rare
	 * event of it not getting flushed for a long time the delay
	 * shouldn't really matter because there's no real memory
	 * pressure for swapout to react to. ]
	 */
	return ptep_test_and_clear_young(vma, address, ptep);
}

/*
 * Encode and decode a swap entry
 *
 * Format of swap PTE:
 *	bit            0:	_PAGE_PRESENT (zero)
 *	bit            1:	_PAGE_PROT_NONE (zero)
 *	bits      2 to 6:	swap type
 *	bits 7 to XLEN-1:	swap offset
 */
#define __SWP_TYPE_SHIFT	2
#define __SWP_TYPE_BITS		5
#define __SWP_TYPE_MASK		((1UL << __SWP_TYPE_BITS) - 1)
#define __SWP_OFFSET_SHIFT	(__SWP_TYPE_BITS + __SWP_TYPE_SHIFT)

#define MAX_SWAPFILES_CHECK()	\
	BUILD_BUG_ON(MAX_SWAPFILES_SHIFT > __SWP_TYPE_BITS)

#define __swp_type(x)	(((x).val >> __SWP_TYPE_SHIFT) & __SWP_TYPE_MASK)
#define __swp_offset(x)	((x).val >> __SWP_OFFSET_SHIFT)
#define __swp_entry(type, offset) ((swp_entry_t) \
	{ ((type) << __SWP_TYPE_SHIFT) | ((offset) << __SWP_OFFSET_SHIFT) })

#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val })

#ifdef CONFIG_FLATMEM
#define kern_addr_valid(addr)   (1) /* FIXME */
#endif

extern void *dtb_early_va;
extern void setup_bootmem(void);
extern void paging_init(void);

static inline void pgtable_cache_init(void)
{
	/* No page table caches to initialize */
}

#define VMALLOC_SIZE     (KERN_VIRT_SIZE >> 1)
#define VMALLOC_END      (PAGE_OFFSET - 1)
#define VMALLOC_START    (PAGE_OFFSET - VMALLOC_SIZE)

#define FIXADDR_TOP      VMALLOC_START
#ifdef CONFIG_64BIT
#define FIXADDR_SIZE     PMD_SIZE
#else
#define FIXADDR_SIZE     PGDIR_SIZE
#endif
#define FIXADDR_START    (FIXADDR_TOP - FIXADDR_SIZE)

/*
 * Task size is 0x4000000000 for RV64 or 0x9fc00000 for RV32.
 * Note that PGDIR_SIZE must evenly divide TASK_SIZE.
 */
#ifdef CONFIG_64BIT
#define TASK_SIZE (PGDIR_SIZE * PTRS_PER_PGD / 2)
#else
#define TASK_SIZE FIXADDR_START
#endif

#include <asm-generic/pgtable.h>

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_RISCV_PGTABLE_H */
