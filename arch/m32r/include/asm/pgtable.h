#ifndef _ASM_M32R_PGTABLE_H
#define _ASM_M32R_PGTABLE_H

#include <asm-generic/4level-fixup.h>

#ifdef __KERNEL__
/*
 * The Linux memory management assumes a three-level page table setup. On
 * the M32R, we use that, but "fold" the mid level into the top-level page
 * table, so that we physically have the same two-level page table as the
 * M32R mmu expects.
 *
 * This file contains the functions and defines necessary to modify and use
 * the M32R page table tree.
 */

/* CAUTION!: If you change macro definitions in this file, you might have to
 * change arch/m32r/mmu.S manually.
 */

#ifndef __ASSEMBLY__

#include <linux/threads.h>
#include <linux/bitops.h>
#include <asm/processor.h>
#include <asm/addrspace.h>
#include <asm/page.h>

struct mm_struct;
struct vm_area_struct;

extern pgd_t swapper_pg_dir[1024];
extern void paging_init(void);

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern unsigned long empty_zero_page[1024];
#define ZERO_PAGE(vaddr)	(virt_to_page(empty_zero_page))

#endif /* !__ASSEMBLY__ */

#ifndef __ASSEMBLY__
#include <asm/pgtable-2level.h>
#endif

#define pgtable_cache_init()	do { } while (0)

#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE - 1))
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE - 1))

#define USER_PTRS_PER_PGD	(TASK_SIZE / PGDIR_SIZE)
#define FIRST_USER_ADDRESS	0

#ifndef __ASSEMBLY__
/* Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 */
#define VMALLOC_START		KSEG2
#define VMALLOC_END		KSEG3

/*
 *     M32R TLB format
 *
 *     [0]    [1:19]           [20:23]       [24:31]
 *     +-----------------------+----+-------------+
 *     |          VPN          |0000|    ASID     |
 *     +-----------------------+----+-------------+
 *     +-+---------------------+----+-+---+-+-+-+-+
 *     |0         PPN          |0000|N|AC |L|G|V| |
 *     +-+---------------------+----+-+---+-+-+-+-+
 *                                     RWX
 */

#define _PAGE_BIT_DIRTY		0	/* software: page changed */
#define _PAGE_BIT_FILE		0	/* when !present: nonlinear file
					   mapping */
#define _PAGE_BIT_PRESENT	1	/* Valid: page is valid */
#define _PAGE_BIT_GLOBAL	2	/* Global */
#define _PAGE_BIT_LARGE		3	/* Large */
#define _PAGE_BIT_EXEC		4	/* Execute */
#define _PAGE_BIT_WRITE		5	/* Write */
#define _PAGE_BIT_READ		6	/* Read */
#define _PAGE_BIT_NONCACHABLE	7	/* Non cachable */
#define _PAGE_BIT_ACCESSED	8	/* software: page referenced */
#define _PAGE_BIT_PROTNONE	9	/* software: if not present */

#define _PAGE_DIRTY		(1UL << _PAGE_BIT_DIRTY)
#define _PAGE_FILE		(1UL << _PAGE_BIT_FILE)
#define _PAGE_PRESENT		(1UL << _PAGE_BIT_PRESENT)
#define _PAGE_GLOBAL		(1UL << _PAGE_BIT_GLOBAL)
#define _PAGE_LARGE		(1UL << _PAGE_BIT_LARGE)
#define _PAGE_EXEC		(1UL << _PAGE_BIT_EXEC)
#define _PAGE_WRITE		(1UL << _PAGE_BIT_WRITE)
#define _PAGE_READ		(1UL << _PAGE_BIT_READ)
#define _PAGE_NONCACHABLE	(1UL << _PAGE_BIT_NONCACHABLE)
#define _PAGE_ACCESSED		(1UL << _PAGE_BIT_ACCESSED)
#define _PAGE_PROTNONE		(1UL << _PAGE_BIT_PROTNONE)

#define _PAGE_TABLE	\
	( _PAGE_PRESENT | _PAGE_WRITE | _PAGE_READ | _PAGE_ACCESSED \
	| _PAGE_DIRTY )
#define _KERNPG_TABLE	\
	( _PAGE_PRESENT | _PAGE_WRITE | _PAGE_READ | _PAGE_ACCESSED \
	| _PAGE_DIRTY )
#define _PAGE_CHG_MASK	\
	( PTE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY )

#ifdef CONFIG_MMU
#define PAGE_NONE	\
	__pgprot(_PAGE_PROTNONE | _PAGE_ACCESSED)
#define PAGE_SHARED	\
	__pgprot(_PAGE_PRESENT | _PAGE_WRITE | _PAGE_READ | _PAGE_ACCESSED)
#define PAGE_SHARED_EXEC \
	__pgprot(_PAGE_PRESENT | _PAGE_EXEC | _PAGE_WRITE | _PAGE_READ \
		| _PAGE_ACCESSED)
#define PAGE_COPY	\
	__pgprot(_PAGE_PRESENT | _PAGE_READ | _PAGE_ACCESSED)
#define PAGE_COPY_EXEC	\
	__pgprot(_PAGE_PRESENT | _PAGE_EXEC | _PAGE_READ | _PAGE_ACCESSED)
#define PAGE_READONLY	\
	__pgprot(_PAGE_PRESENT | _PAGE_READ | _PAGE_ACCESSED)
#define PAGE_READONLY_EXEC \
	__pgprot(_PAGE_PRESENT | _PAGE_EXEC | _PAGE_READ | _PAGE_ACCESSED)

#define __PAGE_KERNEL	\
	( _PAGE_PRESENT | _PAGE_EXEC | _PAGE_WRITE | _PAGE_READ | _PAGE_DIRTY \
	| _PAGE_ACCESSED )
#define __PAGE_KERNEL_RO	( __PAGE_KERNEL & ~_PAGE_WRITE )
#define __PAGE_KERNEL_NOCACHE	( __PAGE_KERNEL | _PAGE_NONCACHABLE)

#define MAKE_GLOBAL(x)	__pgprot((x) | _PAGE_GLOBAL)

#define PAGE_KERNEL		MAKE_GLOBAL(__PAGE_KERNEL)
#define PAGE_KERNEL_RO		MAKE_GLOBAL(__PAGE_KERNEL_RO)
#define PAGE_KERNEL_NOCACHE	MAKE_GLOBAL(__PAGE_KERNEL_NOCACHE)

#else
#define PAGE_NONE		__pgprot(0)
#define PAGE_SHARED		__pgprot(0)
#define PAGE_SHARED_EXEC	__pgprot(0)
#define PAGE_COPY		__pgprot(0)
#define PAGE_COPY_EXEC		__pgprot(0)
#define PAGE_READONLY		__pgprot(0)
#define PAGE_READONLY_EXEC	__pgprot(0)

#define PAGE_KERNEL		__pgprot(0)
#define PAGE_KERNEL_RO		__pgprot(0)
#define PAGE_KERNEL_NOCACHE	__pgprot(0)
#endif /* CONFIG_MMU */

	/* xwr */
#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY
#define __P010	PAGE_COPY
#define __P011	PAGE_COPY
#define __P100	PAGE_READONLY_EXEC
#define __P101	PAGE_READONLY_EXEC
#define __P110	PAGE_COPY_EXEC
#define __P111	PAGE_COPY_EXEC

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_READONLY_EXEC
#define __S101	PAGE_READONLY_EXEC
#define __S110	PAGE_SHARED_EXEC
#define __S111	PAGE_SHARED_EXEC

/* page table for 0-4MB for everybody */

#define pte_present(x)	(pte_val(x) & (_PAGE_PRESENT | _PAGE_PROTNONE))
#define pte_clear(mm,addr,xp)	do { set_pte_at(mm, addr, xp, __pte(0)); } while (0)

#define pmd_none(x)	(!pmd_val(x))
#define pmd_present(x)	(pmd_val(x) & _PAGE_PRESENT)
#define pmd_clear(xp)	do { set_pmd(xp, __pmd(0)); } while (0)
#define	pmd_bad(x)	((pmd_val(x) & ~PAGE_MASK) != _KERNPG_TABLE)

#define pages_to_mb(x)	((x) >> (20 - PAGE_SHIFT))

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
static inline int pte_dirty(pte_t pte)
{
	return pte_val(pte) & _PAGE_DIRTY;
}

static inline int pte_young(pte_t pte)
{
	return pte_val(pte) & _PAGE_ACCESSED;
}

static inline int pte_write(pte_t pte)
{
	return pte_val(pte) & _PAGE_WRITE;
}

/*
 * The following only works if pte_present() is not true.
 */
static inline int pte_file(pte_t pte)
{
	return pte_val(pte) & _PAGE_FILE;
}

static inline int pte_special(pte_t pte)
{
	return 0;
}

static inline pte_t pte_mkclean(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_DIRTY;
	return pte;
}

static inline pte_t pte_mkold(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_ACCESSED;
	return pte;
}

static inline pte_t pte_wrprotect(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_WRITE;
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

static inline pte_t pte_mkwrite(pte_t pte)
{
	pte_val(pte) |= _PAGE_WRITE;
	return pte;
}

static inline pte_t pte_mkspecial(pte_t pte)
{
	return pte;
}

static inline  int ptep_test_and_clear_young(struct vm_area_struct *vma, unsigned long addr, pte_t *ptep)
{
	return test_and_clear_bit(_PAGE_BIT_ACCESSED, ptep);
}

static inline void ptep_set_wrprotect(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	clear_bit(_PAGE_BIT_WRITE, ptep);
}

/*
 * Macro and implementation to make a page protection as uncachable.
 */
static inline pgprot_t pgprot_noncached(pgprot_t _prot)
{
	unsigned long prot = pgprot_val(_prot);

	prot |= _PAGE_NONCACHABLE;
	return __pgprot(prot);
}

#define pgprot_writecombine(prot) pgprot_noncached(prot)

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define mk_pte(page, pgprot)	pfn_pte(page_to_pfn(page), pgprot)

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	set_pte(&pte, __pte((pte_val(pte) & _PAGE_CHG_MASK) \
		| pgprot_val(newprot)));

	return pte;
}

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */

static inline void pmd_set(pmd_t * pmdp, pte_t * ptep)
{
	pmd_val(*pmdp) = (((unsigned long) ptep) & PAGE_MASK);
}

#define pmd_page_vaddr(pmd)	\
	((unsigned long) __va(pmd_val(pmd) & PAGE_MASK))

#ifndef CONFIG_DISCONTIGMEM
#define pmd_page(pmd)	(mem_map + ((pmd_val(pmd) >> PAGE_SHIFT) - PFN_BASE))
#endif /* !CONFIG_DISCONTIGMEM */

/* to find an entry in a page-table-directory. */
#define pgd_index(address)	\
	(((address) >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1))

#define pgd_offset(mm, address)	((mm)->pgd + pgd_index(address))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address)	pgd_offset(&init_mm, address)

#define pmd_index(address)	\
	(((address) >> PMD_SHIFT) & (PTRS_PER_PMD - 1))

#define pte_index(address)	\
	(((address) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#define pte_offset_kernel(dir, address)	\
	((pte_t *)pmd_page_vaddr(*(dir)) + pte_index(address))
#define pte_offset_map(dir, address)	\
	((pte_t *)page_address(pmd_page(*(dir))) + pte_index(address))
#define pte_unmap(pte)		do { } while (0)

/* Encode and de-code a swap entry */
#define __swp_type(x)			(((x).val >> 2) & 0x1f)
#define __swp_offset(x)			((x).val >> 10)
#define __swp_entry(type, offset)	\
	((swp_entry_t) { ((type) << 2) | ((offset) << 10) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val })

#endif /* !__ASSEMBLY__ */

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
#define kern_addr_valid(addr)	(1)

#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
#define __HAVE_ARCH_PTEP_SET_WRPROTECT
#define __HAVE_ARCH_PTE_SAME
#include <asm-generic/pgtable.h>

#endif /* __KERNEL__ */

#endif /* _ASM_M32R_PGTABLE_H */
