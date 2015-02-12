/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * vineetg: May 2011
 *  -Folded PAGE_PRESENT (used by VM) and PAGE_VALID (used by MMU) into 1.
 *     They are semantically the same although in different contexts
 *     VALID marks a TLB entry exists and it will only happen if PRESENT
 *  - Utilise some unused free bits to confine PTE flags to 12 bits
 *     This is a must for 4k pg-sz
 *
 * vineetg: Mar 2011 - changes to accomodate MMU TLB Page Descriptor mods
 *  -TLB Locking never really existed, except for initial specs
 *  -SILENT_xxx not needed for our port
 *  -Per my request, MMU V3 changes the layout of some of the bits
 *     to avoid a few shifts in TLB Miss handlers.
 *
 * vineetg: April 2010
 *  -PGD entry no longer contains any flags. If empty it is 0, otherwise has
 *   Pg-Tbl ptr. Thus pmd_present(), pmd_valid(), pmd_set( ) become simpler
 *
 * vineetg: April 2010
 *  -Switched form 8:11:13 split for page table lookup to 11:8:13
 *  -this speeds up page table allocation itself as we now have to memset 1K
 *    instead of 8k per page table.
 * -TODO: Right now page table alloc is 8K and rest 7K is unused
 *    need to optimise it
 *
 * Amit Bhor, Sameer Dhavale: Codito Technologies 2004
 */

#ifndef _ASM_ARC_PGTABLE_H
#define _ASM_ARC_PGTABLE_H

#include <asm/page.h>
#include <asm/mmu.h>
#include <asm-generic/pgtable-nopmd.h>

/**************************************************************************
 * Page Table Flags
 *
 * ARC700 MMU only deals with softare managed TLB entries.
 * Page Tables are purely for Linux VM's consumption and the bits below are
 * suited to that (uniqueness). Hence some are not implemented in the TLB and
 * some have different value in TLB.
 * e.g. MMU v2: K_READ bit is 8 and so is GLOBAL (possible becoz they live in
 *      seperate PD0 and PD1, which combined forms a translation entry)
 *      while for PTE perspective, they are 8 and 9 respectively
 * with MMU v3: Most bits (except SHARED) represent the exact hardware pos
 *      (saves some bit shift ops in TLB Miss hdlrs)
 */

#if (CONFIG_ARC_MMU_VER <= 2)

#define _PAGE_ACCESSED      (1<<1)	/* Page is accessed (S) */
#define _PAGE_CACHEABLE     (1<<2)	/* Page is cached (H) */
#define _PAGE_U_EXECUTE     (1<<3)	/* Page has user execute perm (H) */
#define _PAGE_U_WRITE       (1<<4)	/* Page has user write perm (H) */
#define _PAGE_U_READ        (1<<5)	/* Page has user read perm (H) */
#define _PAGE_K_EXECUTE     (1<<6)	/* Page has kernel execute perm (H) */
#define _PAGE_K_WRITE       (1<<7)	/* Page has kernel write perm (H) */
#define _PAGE_K_READ        (1<<8)	/* Page has kernel perm (H) */
#define _PAGE_GLOBAL        (1<<9)	/* Page is global (H) */
#define _PAGE_MODIFIED      (1<<10)	/* Page modified (dirty) (S) */
#define _PAGE_FILE          (1<<10)	/* page cache/ swap (S) */
#define _PAGE_PRESENT       (1<<11)	/* TLB entry is valid (H) */

#else

/* PD1 */
#define _PAGE_CACHEABLE     (1<<0)	/* Page is cached (H) */
#define _PAGE_U_EXECUTE     (1<<1)	/* Page has user execute perm (H) */
#define _PAGE_U_WRITE       (1<<2)	/* Page has user write perm (H) */
#define _PAGE_U_READ        (1<<3)	/* Page has user read perm (H) */
#define _PAGE_K_EXECUTE     (1<<4)	/* Page has kernel execute perm (H) */
#define _PAGE_K_WRITE       (1<<5)	/* Page has kernel write perm (H) */
#define _PAGE_K_READ        (1<<6)	/* Page has kernel perm (H) */
#define _PAGE_ACCESSED      (1<<7)	/* Page is accessed (S) */

/* PD0 */
#define _PAGE_GLOBAL        (1<<8)	/* Page is global (H) */
#define _PAGE_PRESENT       (1<<9)	/* TLB entry is valid (H) */
#define _PAGE_SHARED_CODE   (1<<10)	/* Shared Code page with cmn vaddr
					   usable for shared TLB entries (H) */

#define _PAGE_MODIFIED      (1<<11)	/* Page modified (dirty) (S) */
#define _PAGE_FILE          (1<<12)	/* page cache/ swap (S) */

#define _PAGE_SHARED_CODE_H (1<<31)	/* Hardware counterpart of above */
#endif

/* Kernel allowed all permissions for all pages */
#define _K_PAGE_PERMS  (_PAGE_K_EXECUTE | _PAGE_K_WRITE | _PAGE_K_READ | \
			_PAGE_GLOBAL | _PAGE_PRESENT)

#ifdef CONFIG_ARC_CACHE_PAGES
#define _PAGE_DEF_CACHEABLE _PAGE_CACHEABLE
#else
#define _PAGE_DEF_CACHEABLE (0)
#endif

/* Helper for every "user" page
 * -kernel can R/W/X
 * -by default cached, unless config otherwise
 * -present in memory
 */
#define ___DEF (_PAGE_PRESENT | _PAGE_DEF_CACHEABLE)

#define _PAGE_READ	(_PAGE_U_READ    | _PAGE_K_READ)
#define _PAGE_WRITE	(_PAGE_U_WRITE   | _PAGE_K_WRITE)
#define _PAGE_EXECUTE	(_PAGE_U_EXECUTE | _PAGE_K_EXECUTE)

/* Set of bits not changed in pte_modify */
#define _PAGE_CHG_MASK	(PAGE_MASK | _PAGE_ACCESSED | _PAGE_MODIFIED)

/* More Abbrevaited helpers */
#define PAGE_U_NONE     __pgprot(___DEF)
#define PAGE_U_R        __pgprot(___DEF | _PAGE_READ)
#define PAGE_U_W_R      __pgprot(___DEF | _PAGE_READ | _PAGE_WRITE)
#define PAGE_U_X_R      __pgprot(___DEF | _PAGE_READ | _PAGE_EXECUTE)
#define PAGE_U_X_W_R    __pgprot(___DEF | _PAGE_READ | _PAGE_WRITE | \
						       _PAGE_EXECUTE)

#define PAGE_SHARED	PAGE_U_W_R

/* While kernel runs out of unstrslated space, vmalloc/modules use a chunk of
 * kernel vaddr space - visible in all addr spaces, but kernel mode only
 * Thus Global, all-kernel-access, no-user-access, cached
 */
#define PAGE_KERNEL          __pgprot(_K_PAGE_PERMS | _PAGE_DEF_CACHEABLE)

/* ioremap */
#define PAGE_KERNEL_NO_CACHE __pgprot(_K_PAGE_PERMS)

/**************************************************************************
 * Mapping of vm_flags (Generic VM) to PTE flags (arch specific)
 *
 * Certain cases have 1:1 mapping
 *  e.g. __P101 means VM_READ, VM_EXEC and !VM_SHARED
 *       which directly corresponds to  PAGE_U_X_R
 *
 * Other rules which cause the divergence from 1:1 mapping
 *
 *  1. Although ARC700 can do exclusive execute/write protection (meaning R
 *     can be tracked independet of X/W unlike some other CPUs), still to
 *     keep things consistent with other archs:
 *      -Write implies Read:   W => R
 *      -Execute implies Read: X => R
 *
 *  2. Pvt Writable doesn't have Write Enabled initially: Pvt-W => !W
 *     This is to enable COW mechanism
 */
	/* xwr */
#define __P000  PAGE_U_NONE
#define __P001  PAGE_U_R
#define __P010  PAGE_U_R	/* Pvt-W => !W */
#define __P011  PAGE_U_R	/* Pvt-W => !W */
#define __P100  PAGE_U_X_R	/* X => R */
#define __P101  PAGE_U_X_R
#define __P110  PAGE_U_X_R	/* Pvt-W => !W and X => R */
#define __P111  PAGE_U_X_R	/* Pvt-W => !W */

#define __S000  PAGE_U_NONE
#define __S001  PAGE_U_R
#define __S010  PAGE_U_W_R	/* W => R */
#define __S011  PAGE_U_W_R
#define __S100  PAGE_U_X_R	/* X => R */
#define __S101  PAGE_U_X_R
#define __S110  PAGE_U_X_W_R	/* X => R */
#define __S111  PAGE_U_X_W_R

/****************************************************************
 * Page Table Lookup split
 *
 * We implement 2 tier paging and since this is all software, we are free
 * to customize the span of a PGD / PTE entry to suit us
 *
 *			32 bit virtual address
 * -------------------------------------------------------
 * | BITS_FOR_PGD    |  BITS_FOR_PTE    |  BITS_IN_PAGE  |
 * -------------------------------------------------------
 *       |                  |                |
 *       |                  |                --> off in page frame
 *       |		    |
 *       |                  ---> index into Page Table
 *       |
 *       ----> index into Page Directory
 */

#define BITS_IN_PAGE	PAGE_SHIFT

/* Optimal Sizing of Pg Tbl - based on MMU page size */
#if defined(CONFIG_ARC_PAGE_SIZE_8K)
#define BITS_FOR_PTE	8
#elif defined(CONFIG_ARC_PAGE_SIZE_16K)
#define BITS_FOR_PTE	8
#elif defined(CONFIG_ARC_PAGE_SIZE_4K)
#define BITS_FOR_PTE	9
#endif

#define BITS_FOR_PGD	(32 - BITS_FOR_PTE - BITS_IN_PAGE)

#define PGDIR_SHIFT	(BITS_FOR_PTE + BITS_IN_PAGE)
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)	/* vaddr span, not PDG sz */
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

#ifdef __ASSEMBLY__
#define	PTRS_PER_PTE	(1 << BITS_FOR_PTE)
#define	PTRS_PER_PGD	(1 << BITS_FOR_PGD)
#else
#define	PTRS_PER_PTE	(1UL << BITS_FOR_PTE)
#define	PTRS_PER_PGD	(1UL << BITS_FOR_PGD)
#endif
/*
 * Number of entries a user land program use.
 * TASK_SIZE is the maximum vaddr that can be used by a userland program.
 */
#define	USER_PTRS_PER_PGD	(TASK_SIZE / PGDIR_SIZE)

/*
 * No special requirements for lowest virtual address we permit any user space
 * mapping to be mapped at.
 */
#define FIRST_USER_ADDRESS      0


/****************************************************************
 * Bucket load of VM Helpers
 */

#ifndef __ASSEMBLY__

#define pte_ERROR(e) \
	pr_crit("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, pte_val(e))
#define pgd_ERROR(e) \
	pr_crit("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

/* the zero page used for uninitialized and anonymous pages */
extern char empty_zero_page[PAGE_SIZE];
#define ZERO_PAGE(vaddr)	(virt_to_page(empty_zero_page))

#define pte_unmap(pte)		do { } while (0)
#define pte_unmap_nested(pte)		do { } while (0)

#define set_pte(pteptr, pteval)	((*(pteptr)) = (pteval))
#define set_pmd(pmdptr, pmdval)	(*(pmdptr) = pmdval)

/* find the page descriptor of the Page Tbl ref by PMD entry */
#define pmd_page(pmd)		virt_to_page(pmd_val(pmd) & PAGE_MASK)

/* find the logical addr (phy for ARC) of the Page Tbl ref by PMD entry */
#define pmd_page_vaddr(pmd)	(pmd_val(pmd) & PAGE_MASK)

/* In a 2 level sys, setup the PGD entry with PTE value */
static inline void pmd_set(pmd_t *pmdp, pte_t *ptep)
{
	pmd_val(*pmdp) = (unsigned long)ptep;
}

#define pte_none(x)			(!pte_val(x))
#define pte_present(x)			(pte_val(x) & _PAGE_PRESENT)
#define pte_clear(mm, addr, ptep)	set_pte_at(mm, addr, ptep, __pte(0))

#define pmd_none(x)			(!pmd_val(x))
#define	pmd_bad(x)			((pmd_val(x) & ~PAGE_MASK))
#define pmd_present(x)			(pmd_val(x))
#define pmd_clear(xp)			do { pmd_val(*(xp)) = 0; } while (0)

#define pte_page(x) (mem_map + \
		(unsigned long)(((pte_val(x) - CONFIG_LINUX_LINK_BASE) >> \
				PAGE_SHIFT)))

#define mk_pte(page, pgprot)						\
({									\
	pte_t pte;							\
	pte_val(pte) = __pa(page_address(page)) + pgprot_val(pgprot);	\
	pte;								\
})

/* TBD: Non linear mapping stuff */
static inline int pte_file(pte_t pte)
{
	return pte_val(pte) & _PAGE_FILE;
}

#define PTE_FILE_MAX_BITS	30
#define pgoff_to_pte(x)         __pte(x)
#define pte_to_pgoff(x)		(pte_val(x) >> 2)
#define pte_pfn(pte)		(pte_val(pte) >> PAGE_SHIFT)
#define pfn_pte(pfn, prot)	(__pte(((pfn) << PAGE_SHIFT) | pgprot_val(prot)))
#define __pte_index(addr)	(((addr) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))

/*
 * pte_offset gets a @ptr to PMD entry (PGD in our 2-tier paging system)
 * and returns ptr to PTE entry corresponding to @addr
 */
#define pte_offset(dir, addr) ((pte_t *)(pmd_page_vaddr(*dir)) +\
					 __pte_index(addr))

/* No mapping of Page Tables in high mem etc, so following same as above */
#define pte_offset_kernel(dir, addr)		pte_offset(dir, addr)
#define pte_offset_map(dir, addr)		pte_offset(dir, addr)

/* Zoo of pte_xxx function */
#define pte_read(pte)		(pte_val(pte) & _PAGE_READ)
#define pte_write(pte)		(pte_val(pte) & _PAGE_WRITE)
#define pte_dirty(pte)		(pte_val(pte) & _PAGE_MODIFIED)
#define pte_young(pte)		(pte_val(pte) & _PAGE_ACCESSED)
#define pte_special(pte)	(0)

#define PTE_BIT_FUNC(fn, op) \
	static inline pte_t pte_##fn(pte_t pte) { pte_val(pte) op; return pte; }

PTE_BIT_FUNC(wrprotect,	&= ~(_PAGE_WRITE));
PTE_BIT_FUNC(mkwrite,	|= (_PAGE_WRITE));
PTE_BIT_FUNC(mkclean,	&= ~(_PAGE_MODIFIED));
PTE_BIT_FUNC(mkdirty,	|= (_PAGE_MODIFIED));
PTE_BIT_FUNC(mkold,	&= ~(_PAGE_ACCESSED));
PTE_BIT_FUNC(mkyoung,	|= (_PAGE_ACCESSED));
PTE_BIT_FUNC(exprotect,	&= ~(_PAGE_EXECUTE));
PTE_BIT_FUNC(mkexec,	|= (_PAGE_EXECUTE));

static inline pte_t pte_mkspecial(pte_t pte) { return pte; }

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot));
}

/* Macro to mark a page protection as uncacheable */
#define pgprot_noncached(prot)	(__pgprot(pgprot_val(prot) & ~_PAGE_CACHEABLE))

static inline void set_pte_at(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, pte_t pteval)
{
	set_pte(ptep, pteval);
}

/*
 * All kernel related VM pages are in init's mm.
 */
#define pgd_offset_k(address)	pgd_offset(&init_mm, address)
#define pgd_index(addr)		((addr) >> PGDIR_SHIFT)
#define pgd_offset(mm, addr)	(((mm)->pgd)+pgd_index(addr))

/*
 * Macro to quickly access the PGD entry, utlising the fact that some
 * arch may cache the pointer to Page Directory of "current" task
 * in a MMU register
 *
 * Thus task->mm->pgd (3 pointer dereferences, cache misses etc simply
 * becomes read a register
 *
 * ********CAUTION*******:
 * Kernel code might be dealing with some mm_struct of NON "current"
 * Thus use this macro only when you are certain that "current" is current
 * e.g. when dealing with signal frame setup code etc
 */
#ifndef CONFIG_SMP
#define pgd_offset_fast(mm, addr)	\
({					\
	pgd_t *pgd_base = (pgd_t *) read_aux_reg(ARC_REG_SCRATCH_DATA0);  \
	pgd_base + pgd_index(addr);	\
})
#else
#define pgd_offset_fast(mm, addr)	pgd_offset(mm, addr)
#endif

extern void paging_init(void);
extern pgd_t swapper_pg_dir[] __aligned(PAGE_SIZE);
void update_mmu_cache(struct vm_area_struct *vma, unsigned long address,
		      pte_t *ptep);

/* Encode swap {type,off} tuple into PTE
 * We reserve 13 bits for 5-bit @type, keeping bits 12-5 zero, ensuring that
 * both PAGE_FILE and PAGE_PRESENT are zero in a PTE holding swap "identifier"
 */
#define __swp_entry(type, off)	((swp_entry_t) { \
					((type) & 0x1f) | ((off) << 13) })

/* Decode a PTE containing swap "identifier "into constituents */
#define __swp_type(pte_lookalike)	(((pte_lookalike).val) & 0x1f)
#define __swp_offset(pte_lookalike)	((pte_lookalike).val << 13)

/* NOPs, to keep generic kernel happy */
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val })

#define kern_addr_valid(addr)	(1)

/*
 * remap a physical page `pfn' of size `size' with page protection `prot'
 * into virtual address `from'
 */
#define io_remap_pfn_range(vma, from, pfn, size, prot) \
			remap_pfn_range(vma, from, pfn, size, prot)

#include <asm-generic/pgtable.h>

/* to cope with aliasing VIPT cache */
#define HAVE_ARCH_UNMAPPED_AREA

/*
 * No page table caches to initialise
 */
#define pgtable_cache_init()   do { } while (0)

#endif /* __ASSEMBLY__ */

#endif
