/*
 * linux/arch/unicore32/include/asm/pgtable.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __UNICORE_PGTABLE_H__
#define __UNICORE_PGTABLE_H__

#include <asm-generic/pgtable-nopmd.h>
#include <asm/cpu-single.h>

#include <asm/memory.h>
#include <asm/pgtable-hwdef.h>

/*
 * Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 *
 * Note that platforms may override VMALLOC_START, but they must provide
 * VMALLOC_END.  VMALLOC_END defines the (exclusive) limit of this space,
 * which may not overlap IO space.
 */
#ifndef VMALLOC_START
#define VMALLOC_OFFSET		SZ_8M
#define VMALLOC_START		(((unsigned long)high_memory + VMALLOC_OFFSET) \
					& ~(VMALLOC_OFFSET-1))
#define VMALLOC_END		(0xff000000UL)
#endif

#define PTRS_PER_PTE		1024
#define PTRS_PER_PGD		1024

/*
 * PGDIR_SHIFT determines what a third-level page table entry can map
 */
#define PGDIR_SHIFT		22

#ifndef __ASSEMBLY__
extern void __pte_error(const char *file, int line, unsigned long val);
extern void __pgd_error(const char *file, int line, unsigned long val);

#define pte_ERROR(pte)		__pte_error(__FILE__, __LINE__, pte_val(pte))
#define pgd_ERROR(pgd)		__pgd_error(__FILE__, __LINE__, pgd_val(pgd))
#endif /* !__ASSEMBLY__ */

#define PGDIR_SIZE		(1UL << PGDIR_SHIFT)
#define PGDIR_MASK		(~(PGDIR_SIZE-1))

/*
 * This is the lowest virtual address we can permit any user space
 * mapping to be mapped at.  This is particularly important for
 * non-high vector CPUs.
 */
#define FIRST_USER_ADDRESS	PAGE_SIZE

#define FIRST_USER_PGD_NR	1
#define USER_PTRS_PER_PGD	((TASK_SIZE/PGDIR_SIZE) - FIRST_USER_PGD_NR)

/*
 * section address mask and size definitions.
 */
#define SECTION_SHIFT		22
#define SECTION_SIZE		(1UL << SECTION_SHIFT)
#define SECTION_MASK		(~(SECTION_SIZE-1))

#ifndef __ASSEMBLY__

/*
 * The pgprot_* and protection_map entries will be fixed up in runtime
 * to include the cachable bits based on memory policy, as well as any
 * architecture dependent bits.
 */
#define _PTE_DEFAULT		(PTE_PRESENT | PTE_YOUNG | PTE_CACHEABLE)

extern pgprot_t pgprot_user;
extern pgprot_t pgprot_kernel;

#define PAGE_NONE		pgprot_user
#define PAGE_SHARED		__pgprot(pgprot_val(pgprot_user | PTE_READ \
								| PTE_WRITE)
#define PAGE_SHARED_EXEC	__pgprot(pgprot_val(pgprot_user | PTE_READ \
								| PTE_WRITE \
								| PTE_EXEC)
#define PAGE_COPY		__pgprot(pgprot_val(pgprot_user | PTE_READ)
#define PAGE_COPY_EXEC		__pgprot(pgprot_val(pgprot_user | PTE_READ \
								| PTE_EXEC)
#define PAGE_READONLY		__pgprot(pgprot_val(pgprot_user | PTE_READ)
#define PAGE_READONLY_EXEC	__pgprot(pgprot_val(pgprot_user | PTE_READ \
								| PTE_EXEC)
#define PAGE_KERNEL		pgprot_kernel
#define PAGE_KERNEL_EXEC	__pgprot(pgprot_val(pgprot_kernel | PTE_EXEC))

#define __PAGE_NONE		__pgprot(_PTE_DEFAULT)
#define __PAGE_SHARED		__pgprot(_PTE_DEFAULT | PTE_READ \
							| PTE_WRITE)
#define __PAGE_SHARED_EXEC	__pgprot(_PTE_DEFAULT | PTE_READ \
							| PTE_WRITE \
							| PTE_EXEC)
#define __PAGE_COPY		__pgprot(_PTE_DEFAULT | PTE_READ)
#define __PAGE_COPY_EXEC	__pgprot(_PTE_DEFAULT | PTE_READ \
							| PTE_EXEC)
#define __PAGE_READONLY		__pgprot(_PTE_DEFAULT | PTE_READ)
#define __PAGE_READONLY_EXEC	__pgprot(_PTE_DEFAULT | PTE_READ \
							| PTE_EXEC)

#endif /* __ASSEMBLY__ */

/*
 * The table below defines the page protection levels that we insert into our
 * Linux page table version.  These get translated into the best that the
 * architecture can perform.  Note that on UniCore hardware:
 *  1) We cannot do execute protection
 *  2) If we could do execute protection, then read is implied
 *  3) write implies read permissions
 */
#define __P000  __PAGE_NONE
#define __P001  __PAGE_READONLY
#define __P010  __PAGE_COPY
#define __P011  __PAGE_COPY
#define __P100  __PAGE_READONLY_EXEC
#define __P101  __PAGE_READONLY_EXEC
#define __P110  __PAGE_COPY_EXEC
#define __P111  __PAGE_COPY_EXEC

#define __S000  __PAGE_NONE
#define __S001  __PAGE_READONLY
#define __S010  __PAGE_SHARED
#define __S011  __PAGE_SHARED
#define __S100  __PAGE_READONLY_EXEC
#define __S101  __PAGE_READONLY_EXEC
#define __S110  __PAGE_SHARED_EXEC
#define __S111  __PAGE_SHARED_EXEC

#ifndef __ASSEMBLY__
/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern struct page *empty_zero_page;
#define ZERO_PAGE(vaddr)		(empty_zero_page)

#define pte_pfn(pte)			(pte_val(pte) >> PAGE_SHIFT)
#define pfn_pte(pfn, prot)		(__pte(((pfn) << PAGE_SHIFT) \
						| pgprot_val(prot)))

#define pte_none(pte)			(!pte_val(pte))
#define pte_clear(mm, addr, ptep)	set_pte(ptep, __pte(0))
#define pte_page(pte)			(pfn_to_page(pte_pfn(pte)))
#define pte_offset_kernel(dir, addr)	(pmd_page_vaddr(*(dir)) \
						+ __pte_index(addr))

#define pte_offset_map(dir, addr)	(pmd_page_vaddr(*(dir)) \
						+ __pte_index(addr))
#define pte_unmap(pte)			do { } while (0)

#define set_pte(ptep, pte)	cpu_set_pte(ptep, pte)

#define set_pte_at(mm, addr, ptep, pteval)	\
	do {					\
		set_pte(ptep, pteval);          \
	} while (0)

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
#define pte_present(pte)	(pte_val(pte) & PTE_PRESENT)
#define pte_write(pte)		(pte_val(pte) & PTE_WRITE)
#define pte_dirty(pte)		(pte_val(pte) & PTE_DIRTY)
#define pte_young(pte)		(pte_val(pte) & PTE_YOUNG)
#define pte_exec(pte)		(pte_val(pte) & PTE_EXEC)
#define pte_special(pte)	(0)

#define PTE_BIT_FUNC(fn, op) \
static inline pte_t pte_##fn(pte_t pte) { pte_val(pte) op; return pte; }

PTE_BIT_FUNC(wrprotect, &= ~PTE_WRITE);
PTE_BIT_FUNC(mkwrite,   |= PTE_WRITE);
PTE_BIT_FUNC(mkclean,   &= ~PTE_DIRTY);
PTE_BIT_FUNC(mkdirty,   |= PTE_DIRTY);
PTE_BIT_FUNC(mkold,     &= ~PTE_YOUNG);
PTE_BIT_FUNC(mkyoung,   |= PTE_YOUNG);

static inline pte_t pte_mkspecial(pte_t pte) { return pte; }

/*
 * Mark the prot value as uncacheable.
 */
#define pgprot_noncached(prot)		\
	__pgprot(pgprot_val(prot) & ~PTE_CACHEABLE)
#define pgprot_writecombine(prot)	\
	__pgprot(pgprot_val(prot) & ~PTE_CACHEABLE)
#define pgprot_dmacoherent(prot)	\
	__pgprot(pgprot_val(prot) & ~PTE_CACHEABLE)

#define pmd_none(pmd)		(!pmd_val(pmd))
#define pmd_present(pmd)	(pmd_val(pmd) & PMD_PRESENT)
#define pmd_bad(pmd)		(((pmd_val(pmd) &		\
				(PMD_PRESENT | PMD_TYPE_MASK))	\
				!= (PMD_PRESENT | PMD_TYPE_TABLE)))

#define set_pmd(pmdpd, pmdval)		\
	do {				\
		*(pmdpd) = pmdval;	\
	} while (0)

#define pmd_clear(pmdp)			\
	do {				\
		set_pmd(pmdp, __pmd(0));\
		clean_pmd_entry(pmdp);	\
	} while (0)

#define pmd_page_vaddr(pmd) ((pte_t *)__va(pmd_val(pmd) & PAGE_MASK))
#define pmd_page(pmd)		pfn_to_page(__phys_to_pfn(pmd_val(pmd)))

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define mk_pte(page, prot)	pfn_pte(page_to_pfn(page), prot)

/* to find an entry in a page-table-directory */
#define pgd_index(addr)		((addr) >> PGDIR_SHIFT)

#define pgd_offset(mm, addr)	((mm)->pgd+pgd_index(addr))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(addr)	pgd_offset(&init_mm, addr)

/* Find an entry in the third-level page table.. */
#define __pte_index(addr)	(((addr) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	const unsigned long mask = PTE_EXEC | PTE_WRITE | PTE_READ;
	pte_val(pte) = (pte_val(pte) & ~mask) | (pgprot_val(newprot) & mask);
	return pte;
}

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

/*
 * Encode and decode a swap entry.  Swap entries are stored in the Linux
 * page tables as follows:
 *
 *   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
 *   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *   <--------------- offset --------------> <--- type --> 0 0 0 0 0
 *
 * This gives us up to 127 swap files and 32GB per swap file.  Note that
 * the offset field is always non-zero.
 */
#define __SWP_TYPE_SHIFT	5
#define __SWP_TYPE_BITS		7
#define __SWP_TYPE_MASK		((1 << __SWP_TYPE_BITS) - 1)
#define __SWP_OFFSET_SHIFT	(__SWP_TYPE_BITS + __SWP_TYPE_SHIFT)

#define __swp_type(x)		(((x).val >> __SWP_TYPE_SHIFT)		\
				& __SWP_TYPE_MASK)
#define __swp_offset(x)		((x).val >> __SWP_OFFSET_SHIFT)
#define __swp_entry(type, offset) ((swp_entry_t) {			\
				((type) << __SWP_TYPE_SHIFT) |		\
				((offset) << __SWP_OFFSET_SHIFT) })

#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(swp)	((pte_t) { (swp).val })

/*
 * It is an error for the kernel to have more swap files than we can
 * encode in the PTEs.  This ensures that we know when MAX_SWAPFILES
 * is increased beyond what we presently support.
 */
#define MAX_SWAPFILES_CHECK()	\
	BUILD_BUG_ON(MAX_SWAPFILES_SHIFT > __SWP_TYPE_BITS)

/*
 * Encode and decode a file entry.  File entries are stored in the Linux
 * page tables as follows:
 *
 *   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
 *   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *   <----------------------- offset ----------------------> 1 0 0 0
 */
#define pte_file(pte)		(pte_val(pte) & PTE_FILE)
#define pte_to_pgoff(x)		(pte_val(x) >> 4)
#define pgoff_to_pte(x)		__pte(((x) << 4) | PTE_FILE)

#define PTE_FILE_MAX_BITS	28

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
/* FIXME: this is not correct */
#define kern_addr_valid(addr)	(1)

#include <asm-generic/pgtable.h>

/*
 * remap a physical page `pfn' of size `size' with page protection `prot'
 * into virtual address `from'
 */
#define io_remap_pfn_range(vma, from, pfn, size, prot)	\
		remap_pfn_range(vma, from, pfn, size, prot)

#define pgtable_cache_init() do { } while (0)

#endif /* !__ASSEMBLY__ */

#endif /* __UNICORE_PGTABLE_H__ */
