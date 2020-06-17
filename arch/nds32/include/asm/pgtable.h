/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef _ASMNDS32_PGTABLE_H
#define _ASMNDS32_PGTABLE_H

#include <asm-generic/pgtable-nopmd.h>
#include <linux/sizes.h>

#include <asm/memory.h>
#include <asm/nds32.h>
#ifndef __ASSEMBLY__
#include <asm/fixmap.h>
#include <nds32_intrinsic.h>
#endif

#ifdef CONFIG_ANDES_PAGE_SIZE_4KB
#define PGDIR_SHIFT      22
#define PTRS_PER_PGD     1024
#define PTRS_PER_PTE     1024
#endif

#ifdef CONFIG_ANDES_PAGE_SIZE_8KB
#define PGDIR_SHIFT      24
#define PTRS_PER_PGD     256
#define PTRS_PER_PTE     2048
#endif

#ifndef __ASSEMBLY__
extern void __pte_error(const char *file, int line, unsigned long val);
extern void __pgd_error(const char *file, int line, unsigned long val);

#define pte_ERROR(pte)		__pte_error(__FILE__, __LINE__, pte_val(pte))
#define pgd_ERROR(pgd)		__pgd_error(__FILE__, __LINE__, pgd_val(pgd))
#endif /* !__ASSEMBLY__ */

#define PMD_SIZE		(1UL << PMD_SHIFT)
#define PMD_MASK		(~(PMD_SIZE-1))
#define PGDIR_SIZE		(1UL << PGDIR_SHIFT)
#define PGDIR_MASK		(~(PGDIR_SIZE-1))

/*
 * This is the lowest virtual address we can permit any user space
 * mapping to be mapped at.  This is particularly important for
 * non-high vector CPUs.
 */
#define FIRST_USER_ADDRESS	0x8000

#ifdef CONFIG_HIGHMEM
#define CONSISTENT_BASE		((PKMAP_BASE) - (SZ_2M))
#define CONSISTENT_END		(PKMAP_BASE)
#else
#define CONSISTENT_BASE		(FIXADDR_START - SZ_2M)
#define CONSISTENT_END		(FIXADDR_START)
#endif
#define CONSISTENT_OFFSET(x)	(((unsigned long)(x) - CONSISTENT_BASE) >> PAGE_SHIFT)

#ifdef CONFIG_HIGHMEM
#ifndef __ASSEMBLY__
#include <asm/highmem.h>
#endif
#endif

#define VMALLOC_RESERVE 	SZ_128M
#define VMALLOC_END		(CONSISTENT_BASE - PAGE_SIZE)
#define VMALLOC_START		((VMALLOC_END) - VMALLOC_RESERVE)
#define VMALLOC_VMADDR(x)	((unsigned long)(x))
#define MAXMEM			__pa(VMALLOC_START)
#define MAXMEM_PFN		PFN_DOWN(MAXMEM)

#define FIRST_USER_PGD_NR	0
#define USER_PTRS_PER_PGD	((TASK_SIZE/PGDIR_SIZE) + FIRST_USER_PGD_NR)

/* L2 PTE */
#define _PAGE_V			(1UL << 0)

#define _PAGE_M_XKRW            (0UL << 1)
#define _PAGE_M_UR_KR		(1UL << 1)
#define _PAGE_M_UR_KRW		(2UL << 1)
#define _PAGE_M_URW_KRW		(3UL << 1)
#define _PAGE_M_KR		(5UL << 1)
#define _PAGE_M_KRW		(7UL << 1)

#define _PAGE_D			(1UL << 4)
#define _PAGE_E			(1UL << 5)
#define _PAGE_A			(1UL << 6)
#define _PAGE_G			(1UL << 7)

#define _PAGE_C_DEV		(0UL << 8)
#define _PAGE_C_DEV_WB		(1UL << 8)
#define _PAGE_C_MEM		(2UL << 8)
#define _PAGE_C_MEM_SHRD_WB	(4UL << 8)
#define _PAGE_C_MEM_SHRD_WT	(5UL << 8)
#define _PAGE_C_MEM_WB		(6UL << 8)
#define _PAGE_C_MEM_WT		(7UL << 8)

#define _PAGE_L			(1UL << 11)

#define _HAVE_PAGE_L		(_PAGE_L)
#define _PAGE_FILE		(1UL << 1)
#define _PAGE_YOUNG		0
#define _PAGE_M_MASK		_PAGE_M_KRW
#define _PAGE_C_MASK		_PAGE_C_MEM_WT

#ifdef CONFIG_SMP
#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
#define _PAGE_CACHE_SHRD	_PAGE_C_MEM_SHRD_WT
#else
#define _PAGE_CACHE_SHRD	_PAGE_C_MEM_SHRD_WB
#endif
#else
#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
#define _PAGE_CACHE_SHRD	_PAGE_C_MEM_WT
#else
#define _PAGE_CACHE_SHRD	_PAGE_C_MEM_WB
#endif
#endif

#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
#define _PAGE_CACHE		_PAGE_C_MEM_WT
#else
#define _PAGE_CACHE		_PAGE_C_MEM_WB
#endif

#define _PAGE_IOREMAP \
	(_PAGE_V | _PAGE_M_KRW | _PAGE_D | _PAGE_G | _PAGE_C_DEV)

/*
 * + Level 1 descriptor (PMD)
 */
#define PMD_TYPE_TABLE		0

#ifndef __ASSEMBLY__

#define _PAGE_USER_TABLE     PMD_TYPE_TABLE
#define _PAGE_KERNEL_TABLE   PMD_TYPE_TABLE

#define PAGE_EXEC	__pgprot(_PAGE_V | _PAGE_M_XKRW | _PAGE_E)
#define PAGE_NONE	__pgprot(_PAGE_V | _PAGE_M_KRW | _PAGE_A)
#define PAGE_READ	__pgprot(_PAGE_V | _PAGE_M_UR_KR)
#define PAGE_RDWR	__pgprot(_PAGE_V | _PAGE_M_URW_KRW | _PAGE_D)
#define PAGE_COPY	__pgprot(_PAGE_V | _PAGE_M_UR_KR)

#define PAGE_UXKRWX_V1	__pgprot(_PAGE_V | _PAGE_M_KRW | _PAGE_D | _PAGE_E | _PAGE_G | _PAGE_CACHE_SHRD)
#define PAGE_UXKRWX_V2	__pgprot(_PAGE_V | _PAGE_M_XKRW | _PAGE_D | _PAGE_E | _PAGE_G | _PAGE_CACHE_SHRD)
#define PAGE_URXKRWX_V2	__pgprot(_PAGE_V | _PAGE_M_UR_KRW | _PAGE_D | _PAGE_E | _PAGE_G | _PAGE_CACHE_SHRD)
#define PAGE_CACHE_L1	__pgprot(_HAVE_PAGE_L | _PAGE_V | _PAGE_M_KRW | _PAGE_D | _PAGE_E | _PAGE_G | _PAGE_CACHE)
#define PAGE_MEMORY	__pgprot(_HAVE_PAGE_L | _PAGE_V | _PAGE_M_KRW | _PAGE_D | _PAGE_E | _PAGE_G | _PAGE_CACHE_SHRD)
#define PAGE_KERNEL	__pgprot(_PAGE_V | _PAGE_M_KRW | _PAGE_D | _PAGE_E | _PAGE_G | _PAGE_CACHE_SHRD)
#define PAGE_SHARED	__pgprot(_PAGE_V | _PAGE_M_URW_KRW | _PAGE_D | _PAGE_CACHE_SHRD)
#define PAGE_DEVICE    __pgprot(_PAGE_V | _PAGE_M_KRW | _PAGE_D | _PAGE_G | _PAGE_C_DEV)
#endif /* __ASSEMBLY__ */

/*         xwr */
#define __P000  (PAGE_NONE | _PAGE_CACHE_SHRD)
#define __P001  (PAGE_READ | _PAGE_CACHE_SHRD)
#define __P010  (PAGE_COPY | _PAGE_CACHE_SHRD)
#define __P011  (PAGE_COPY | _PAGE_CACHE_SHRD)
#define __P100  (PAGE_EXEC | _PAGE_CACHE_SHRD)
#define __P101  (PAGE_READ | _PAGE_E | _PAGE_CACHE_SHRD)
#define __P110  (PAGE_COPY | _PAGE_E | _PAGE_CACHE_SHRD)
#define __P111  (PAGE_COPY | _PAGE_E | _PAGE_CACHE_SHRD)

#define __S000  (PAGE_NONE | _PAGE_CACHE_SHRD)
#define __S001  (PAGE_READ | _PAGE_CACHE_SHRD)
#define __S010  (PAGE_RDWR | _PAGE_CACHE_SHRD)
#define __S011  (PAGE_RDWR | _PAGE_CACHE_SHRD)
#define __S100  (PAGE_EXEC | _PAGE_CACHE_SHRD)
#define __S101  (PAGE_READ | _PAGE_E | _PAGE_CACHE_SHRD)
#define __S110  (PAGE_RDWR | _PAGE_E | _PAGE_CACHE_SHRD)
#define __S111  (PAGE_RDWR | _PAGE_E | _PAGE_CACHE_SHRD)

#ifndef __ASSEMBLY__
/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern struct page *empty_zero_page;
extern void paging_init(void);
#define ZERO_PAGE(vaddr)	(empty_zero_page)

#define pte_pfn(pte)		(pte_val(pte) >> PAGE_SHIFT)
#define pfn_pte(pfn,prot)	(__pte(((pfn) << PAGE_SHIFT) | pgprot_val(prot)))

#define pte_none(pte)	        !(pte_val(pte))
#define pte_clear(mm,addr,ptep)	set_pte_at((mm),(addr),(ptep), __pte(0))
#define pte_page(pte)		(pfn_to_page(pte_pfn(pte)))

static unsigned long pmd_page_vaddr(pmd_t pmd)
{
	return ((unsigned long) __va(pmd_val(pmd) & PAGE_MASK));
}

#define set_pte_at(mm,addr,ptep,pteval) set_pte(ptep,pteval)
/*
 * Set a level 1 translation table entry, and clean it out of
 * any caches such that the MMUs can load it correctly.
 */
static inline void set_pmd(pmd_t * pmdp, pmd_t pmd)
{

	*pmdp = pmd;
#if !defined(CONFIG_CPU_DCACHE_DISABLE) && !defined(CONFIG_CPU_DCACHE_WRITETHROUGH)
	__asm__ volatile ("\n\tcctl %0, L1D_VA_WB"::"r" (pmdp):"memory");
	__nds32__msync_all();
	__nds32__dsb();
#endif
}

/*
 * Set a PTE and flush it out
 */
static inline void set_pte(pte_t * ptep, pte_t pte)
{

	*ptep = pte;
#if !defined(CONFIG_CPU_DCACHE_DISABLE) && !defined(CONFIG_CPU_DCACHE_WRITETHROUGH)
	__asm__ volatile ("\n\tcctl %0, L1D_VA_WB"::"r" (ptep):"memory");
	__nds32__msync_all();
	__nds32__dsb();
#endif
}

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */

/*
 * pte_write: 	     this page is writeable for user mode
 * pte_read:         this page is readable for user mode
 * pte_kernel_write: this page is writeable for kernel mode
 *
 * We don't have pte_kernel_read because kernel always can read.
 *
 * */

#define pte_present(pte)        (pte_val(pte) & _PAGE_V)
#define pte_write(pte)          ((pte_val(pte) & _PAGE_M_MASK) == _PAGE_M_URW_KRW)
#define pte_read(pte)		(((pte_val(pte) & _PAGE_M_MASK) == _PAGE_M_UR_KR) || \
				((pte_val(pte) & _PAGE_M_MASK) == _PAGE_M_UR_KRW) || \
				((pte_val(pte) & _PAGE_M_MASK) == _PAGE_M_URW_KRW))
#define pte_kernel_write(pte)   (((pte_val(pte) & _PAGE_M_MASK) == _PAGE_M_URW_KRW) || \
				((pte_val(pte) & _PAGE_M_MASK) == _PAGE_M_UR_KRW) || \
				((pte_val(pte) & _PAGE_M_MASK) == _PAGE_M_KRW) || \
				(((pte_val(pte) & _PAGE_M_MASK) == _PAGE_M_XKRW) && pte_exec(pte)))
#define pte_exec(pte)		(pte_val(pte) & _PAGE_E)
#define pte_dirty(pte)		(pte_val(pte) & _PAGE_D)
#define pte_young(pte)		(pte_val(pte) & _PAGE_YOUNG)

/*
 * The following only works if pte_present() is not true.
 */
#define pte_file(pte)		(pte_val(pte) & _PAGE_FILE)
#define pte_to_pgoff(x)		(pte_val(x) >> 2)
#define pgoff_to_pte(x)		__pte(((x) << 2) | _PAGE_FILE)

#define PTE_FILE_MAX_BITS	29

#define PTE_BIT_FUNC(fn,op) \
static inline pte_t pte_##fn(pte_t pte) { pte_val(pte) op; return pte; }

static inline pte_t pte_wrprotect(pte_t pte)
{
	pte_val(pte) = pte_val(pte) & ~_PAGE_M_MASK;
	pte_val(pte) = pte_val(pte) | _PAGE_M_UR_KR;
	return pte;
}

static inline pte_t pte_mkwrite(pte_t pte)
{
	pte_val(pte) = pte_val(pte) & ~_PAGE_M_MASK;
	pte_val(pte) = pte_val(pte) | _PAGE_M_URW_KRW;
	return pte;
}

PTE_BIT_FUNC(exprotect, &=~_PAGE_E);
PTE_BIT_FUNC(mkexec, |=_PAGE_E);
PTE_BIT_FUNC(mkclean, &=~_PAGE_D);
PTE_BIT_FUNC(mkdirty, |=_PAGE_D);
PTE_BIT_FUNC(mkold, &=~_PAGE_YOUNG);
PTE_BIT_FUNC(mkyoung, |=_PAGE_YOUNG);

/*
 * Mark the prot value as uncacheable and unbufferable.
 */
#define pgprot_noncached(prot)	   __pgprot((pgprot_val(prot)&~_PAGE_C_MASK) | _PAGE_C_DEV)
#define pgprot_writecombine(prot)  __pgprot((pgprot_val(prot)&~_PAGE_C_MASK) | _PAGE_C_DEV_WB)

#define pmd_none(pmd)         (pmd_val(pmd)&0x1)
#define pmd_present(pmd)      (!pmd_none(pmd))
#define	pmd_bad(pmd)	      pmd_none(pmd)

#define copy_pmd(pmdpd,pmdps)	set_pmd((pmdpd), *(pmdps))
#define pmd_clear(pmdp)		set_pmd((pmdp), __pmd(1))

static inline pmd_t __mk_pmd(pte_t * ptep, unsigned long prot)
{
	unsigned long ptr = (unsigned long)ptep;
	pmd_t pmd;

	/*
	 * The pmd must be loaded with the physical
	 * address of the PTE table
	 */

	pmd_val(pmd) = __virt_to_phys(ptr) | prot;
	return pmd;
}

#define pmd_page(pmd)        virt_to_page(__va(pmd_val(pmd)))

/*
 * Permanent address of a page. We never have highmem, so this is trivial.
 */
#define pages_to_mb(x)       ((x) >> (20 - PAGE_SHIFT))

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define mk_pte(page,prot)	pfn_pte(page_to_pfn(page),prot)

/*
 * The "pgd_xxx()" functions here are trivial for a folded two-level
 * setup: the pgd is never bad, and a pmd always exists (as it's folded
 * into the pgd entry)
 */
#define pgd_none(pgd)		(0)
#define pgd_bad(pgd)		(0)
#define pgd_present(pgd)  	(1)
#define pgd_clear(pgdp)		do { } while (0)

#define page_pte_prot(page,prot)     	mk_pte(page, prot)
#define page_pte(page)		        mk_pte(page, __pgprot(0))
/*
 *     L1PTE = $mr1 + ((virt >> PMD_SHIFT) << 2);
 *     L2PTE = (((virt >> PAGE_SHIFT) & (PTRS_PER_PTE -1 )) << 2);
 *     PPN = (phys & 0xfffff000);
 *
*/

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	const unsigned long mask = 0xfff;
	pte_val(pte) = (pte_val(pte) & ~mask) | (pgprot_val(newprot) & mask);
	return pte;
}

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

/* Encode and decode a swap entry.
 *
 * We support up to 32GB of swap on 4k machines
 */
#define __swp_type(x)	 	     (((x).val >> 2) & 0x7f)
#define __swp_offset(x)	   	     ((x).val >> 9)
#define __swp_entry(type,offset)     ((swp_entry_t) { ((type) << 2) | ((offset) << 9) })
#define __pte_to_swp_entry(pte)	     ((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(swp)	     ((pte_t) { (swp).val })

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
#define kern_addr_valid(addr)	(1)

/*
 * We provide our own arch_get_unmapped_area to cope with VIPT caches.
 */
#define HAVE_ARCH_UNMAPPED_AREA

/*
 * remap a physical address `phys' of size `size' with page protection `prot'
 * into virtual address `from'
 */

#endif /* !__ASSEMBLY__ */

#endif /* _ASMNDS32_PGTABLE_H */
