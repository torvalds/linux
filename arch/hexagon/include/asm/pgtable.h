/*
 * Page table support for the Hexagon architecture
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef _ASM_PGTABLE_H
#define _ASM_PGTABLE_H

/*
 * Page table definitions for Qualcomm Hexagon processor.
 */
#include <linux/swap.h>
#include <asm/page.h>
#define __ARCH_USE_5LEVEL_HACK
#include <asm-generic/pgtable-nopmd.h>

/* A handy thing to have if one has the RAM. Declared in head.S */
extern unsigned long empty_zero_page;
extern unsigned long zero_page_mask;

/*
 * The PTE model described here is that of the Hexagon Virtual Machine,
 * which autonomously walks 2-level page tables.  At a lower level, we
 * also describe the RISCish software-loaded TLB entry structure of
 * the underlying Hexagon processor. A kernel built to run on the
 * virtual machine has no need to know about the underlying hardware.
 */
#include <asm/vm_mmu.h>

/*
 * To maximize the comfort level for the PTE manipulation macros,
 * define the "well known" architecture-specific bits.
 */
#define _PAGE_READ	__HVM_PTE_R
#define _PAGE_WRITE	__HVM_PTE_W
#define _PAGE_EXECUTE	__HVM_PTE_X
#define _PAGE_USER	__HVM_PTE_U

/*
 * We have a total of 4 "soft" bits available in the abstract PTE.
 * The two mandatory software bits are Dirty and Accessed.
 * To make nonlinear swap work according to the more recent
 * model, we want a low order "Present" bit to indicate whether
 * the PTE describes MMU programming or swap space.
 */
#define _PAGE_PRESENT	(1<<0)
#define _PAGE_DIRTY	(1<<1)
#define _PAGE_ACCESSED	(1<<2)

/*
 * For now, let's say that Valid and Present are the same thing.
 * Alternatively, we could say that it's the "or" of R, W, and X
 * permissions.
 */
#define _PAGE_VALID	_PAGE_PRESENT

/*
 * We're not defining _PAGE_GLOBAL here, since there's no concept
 * of global pages or ASIDs exposed to the Hexagon Virtual Machine,
 * and we want to use the same page table structures and macros in
 * the native kernel as we do in the virtual machine kernel.
 * So we'll put up with a bit of inefficiency for now...
 */

/*
 * Top "FOURTH" level (pgd), which for the Hexagon VM is really
 * only the second from the bottom, pgd and pud both being collapsed.
 * Each entry represents 4MB of virtual address space, 4K of table
 * thus maps the full 4GB.
 */
#define PGDIR_SHIFT 22
#define PTRS_PER_PGD 1024

#define PGDIR_SIZE (1UL << PGDIR_SHIFT)
#define PGDIR_MASK (~(PGDIR_SIZE-1))

#ifdef CONFIG_PAGE_SIZE_4KB
#define PTRS_PER_PTE 1024
#endif

#ifdef CONFIG_PAGE_SIZE_16KB
#define PTRS_PER_PTE 256
#endif

#ifdef CONFIG_PAGE_SIZE_64KB
#define PTRS_PER_PTE 64
#endif

#ifdef CONFIG_PAGE_SIZE_256KB
#define PTRS_PER_PTE 16
#endif

#ifdef CONFIG_PAGE_SIZE_1MB
#define PTRS_PER_PTE 4
#endif

/*  Any bigger and the PTE disappears.  */
#define pgd_ERROR(e) \
	printk(KERN_ERR "%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__,\
		pgd_val(e))

/*
 * Page Protection Constants. Includes (in this variant) cache attributes.
 */
extern unsigned long _dflt_cache_att;

#define PAGE_NONE	__pgprot(_PAGE_PRESENT | _PAGE_USER | \
				_dflt_cache_att)
#define PAGE_READONLY	__pgprot(_PAGE_PRESENT | _PAGE_USER | \
				_PAGE_READ | _PAGE_EXECUTE | _dflt_cache_att)
#define PAGE_COPY	PAGE_READONLY
#define PAGE_EXEC	__pgprot(_PAGE_PRESENT | _PAGE_USER | \
				_PAGE_READ | _PAGE_EXECUTE | _dflt_cache_att)
#define PAGE_COPY_EXEC	PAGE_EXEC
#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_READ | \
				_PAGE_EXECUTE | _PAGE_WRITE | _dflt_cache_att)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | _PAGE_READ | \
				_PAGE_WRITE | _PAGE_EXECUTE | _dflt_cache_att)


/*
 * Aliases for mapping mmap() protection bits to page protections.
 * These get used for static initialization, so using the _dflt_cache_att
 * variable for the default cache attribute isn't workable. If the
 * default gets changed at boot time, the boot option code has to
 * update data structures like the protaction_map[] array.
 */
#define CACHEDEF	(CACHE_DEFAULT << 6)

/* Private (copy-on-write) page protections. */
#define __P000 __pgprot(_PAGE_PRESENT | _PAGE_USER | CACHEDEF)
#define __P001 __pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_READ | CACHEDEF)
#define __P010 __P000	/* Write-only copy-on-write */
#define __P011 __P001	/* Read/Write copy-on-write */
#define __P100 __pgprot(_PAGE_PRESENT | _PAGE_USER | \
			_PAGE_EXECUTE | CACHEDEF)
#define __P101 __pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_EXECUTE | \
			_PAGE_READ | CACHEDEF)
#define __P110 __P100	/* Write/execute copy-on-write */
#define __P111 __P101	/* Read/Write/Execute, copy-on-write */

/* Shared page protections. */
#define __S000 __P000
#define __S001 __P001
#define __S010 __pgprot(_PAGE_PRESENT | _PAGE_USER | \
			_PAGE_WRITE | CACHEDEF)
#define __S011 __pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_READ | \
			_PAGE_WRITE | CACHEDEF)
#define __S100 __pgprot(_PAGE_PRESENT | _PAGE_USER | \
			_PAGE_EXECUTE | CACHEDEF)
#define __S101 __P101
#define __S110 __pgprot(_PAGE_PRESENT | _PAGE_USER | \
			_PAGE_EXECUTE | _PAGE_WRITE | CACHEDEF)
#define __S111 __pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_READ | \
			_PAGE_EXECUTE | _PAGE_WRITE | CACHEDEF)

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];  /* located in head.S */

/* Seems to be zero even in architectures where the zero page is firewalled? */
#define FIRST_USER_ADDRESS 0UL
#define pte_special(pte)	0
#define pte_mkspecial(pte)	(pte)

/*  HUGETLB not working currently  */
#ifdef CONFIG_HUGETLB_PAGE
#define pte_mkhuge(pte) __pte((pte_val(pte) & ~0x3) | HVM_HUGEPAGE_SIZE)
#endif

/*
 * For now, assume that higher-level code will do TLB/MMU invalidations
 * and don't insert that overhead into this low-level function.
 */
extern void sync_icache_dcache(pte_t pte);

#define pte_present_exec_user(pte) \
	((pte_val(pte) & (_PAGE_EXECUTE | _PAGE_USER)) == \
	(_PAGE_EXECUTE | _PAGE_USER))

static inline void set_pte(pte_t *ptep, pte_t pteval)
{
	/*  should really be using pte_exec, if it weren't declared later. */
	if (pte_present_exec_user(pteval))
		sync_icache_dcache(pteval);

	*ptep = pteval;
}

/*
 * For the Hexagon Virtual Machine MMU (or its emulation), a null/invalid
 * L1 PTE (PMD/PGD) has 7 in the least significant bits. For the L2 PTE
 * (Linux PTE), the key is to have bits 11..9 all zero.  We'd use 0x7
 * as a universal null entry, but some of those least significant bits
 * are interpreted by software.
 */
#define _NULL_PMD	0x7
#define _NULL_PTE	0x0

static inline void pmd_clear(pmd_t *pmd_entry_ptr)
{
	 pmd_val(*pmd_entry_ptr) = _NULL_PMD;
}

/*
 * Conveniently, a null PTE value is invalid.
 */
static inline void pte_clear(struct mm_struct *mm, unsigned long addr,
				pte_t *ptep)
{
	pte_val(*ptep) = _NULL_PTE;
}

#ifdef NEED_PMD_INDEX_DESPITE_BEING_2_LEVEL
/**
 * pmd_index - returns the index of the entry in the PMD page
 * which would control the given virtual address
 */
#define pmd_index(address) (((address) >> PMD_SHIFT) & (PTRS_PER_PMD-1))

#endif

/**
 * pgd_index - returns the index of the entry in the PGD page
 * which would control the given virtual address
 *
 * This returns the *index* for the address in the pgd_t
 */
#define pgd_index(address) (((address) >> PGDIR_SHIFT) & (PTRS_PER_PGD-1))

/*
 * pgd_offset - find an offset in a page-table-directory
 */
#define pgd_offset(mm, addr) ((mm)->pgd + pgd_index(addr))

/*
 * pgd_offset_k - get kernel (init_mm) pgd entry pointer for addr
 */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/**
 * pmd_none - check if pmd_entry is mapped
 * @pmd_entry:  pmd entry
 *
 * MIPS checks it against that "invalid pte table" thing.
 */
static inline int pmd_none(pmd_t pmd)
{
	return pmd_val(pmd) == _NULL_PMD;
}

/**
 * pmd_present - is there a page table behind this?
 * Essentially the inverse of pmd_none.  We maybe
 * save an inline instruction by defining it this
 * way, instead of simply "!pmd_none".
 */
static inline int pmd_present(pmd_t pmd)
{
	return pmd_val(pmd) != (unsigned long)_NULL_PMD;
}

/**
 * pmd_bad - check if a PMD entry is "bad". That might mean swapped out.
 * As we have no known cause of badness, it's null, as it is for many
 * architectures.
 */
static inline int pmd_bad(pmd_t pmd)
{
	return 0;
}

/*
 * pmd_page - converts a PMD entry to a page pointer
 */
#define pmd_page(pmd)  (pfn_to_page(pmd_val(pmd) >> PAGE_SHIFT))
#define pmd_pgtable(pmd) pmd_page(pmd)

/**
 * pte_none - check if pte is mapped
 * @pte: pte_t entry
 */
static inline int pte_none(pte_t pte)
{
	return pte_val(pte) == _NULL_PTE;
};

/*
 * pte_present - check if page is present
 */
static inline int pte_present(pte_t pte)
{
	return pte_val(pte) & _PAGE_PRESENT;
}

/* mk_pte - make a PTE out of a page pointer and protection bits */
#define mk_pte(page, pgprot) pfn_pte(page_to_pfn(page), (pgprot))

/* pte_page - returns a page (frame pointer/descriptor?) based on a PTE */
#define pte_page(x) pfn_to_page(pte_pfn(x))

/* pte_mkold - mark PTE as not recently accessed */
static inline pte_t pte_mkold(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_ACCESSED;
	return pte;
}

/* pte_mkyoung - mark PTE as recently accessed */
static inline pte_t pte_mkyoung(pte_t pte)
{
	pte_val(pte) |= _PAGE_ACCESSED;
	return pte;
}

/* pte_mkclean - mark page as in sync with backing store */
static inline pte_t pte_mkclean(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_DIRTY;
	return pte;
}

/* pte_mkdirty - mark page as modified */
static inline pte_t pte_mkdirty(pte_t pte)
{
	pte_val(pte) |= _PAGE_DIRTY;
	return pte;
}

/* pte_young - "is PTE marked as accessed"? */
static inline int pte_young(pte_t pte)
{
	return pte_val(pte) & _PAGE_ACCESSED;
}

/* pte_dirty - "is PTE dirty?" */
static inline int pte_dirty(pte_t pte)
{
	return pte_val(pte) & _PAGE_DIRTY;
}

/* pte_modify - set protection bits on PTE */
static inline pte_t pte_modify(pte_t pte, pgprot_t prot)
{
	pte_val(pte) &= PAGE_MASK;
	pte_val(pte) |= pgprot_val(prot);
	return pte;
}

/* pte_wrprotect - mark page as not writable */
static inline pte_t pte_wrprotect(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_WRITE;
	return pte;
}

/* pte_mkwrite - mark page as writable */
static inline pte_t pte_mkwrite(pte_t pte)
{
	pte_val(pte) |= _PAGE_WRITE;
	return pte;
}

/* pte_mkexec - mark PTE as executable */
static inline pte_t pte_mkexec(pte_t pte)
{
	pte_val(pte) |= _PAGE_EXECUTE;
	return pte;
}

/* pte_read - "is PTE marked as readable?" */
static inline int pte_read(pte_t pte)
{
	return pte_val(pte) & _PAGE_READ;
}

/* pte_write - "is PTE marked as writable?" */
static inline int pte_write(pte_t pte)
{
	return pte_val(pte) & _PAGE_WRITE;
}


/* pte_exec - "is PTE marked as executable?" */
static inline int pte_exec(pte_t pte)
{
	return pte_val(pte) & _PAGE_EXECUTE;
}

/* __pte_to_swp_entry - extract swap entry from PTE */
#define __pte_to_swp_entry(pte) ((swp_entry_t) { pte_val(pte) })

/* __swp_entry_to_pte - extract PTE from swap entry */
#define __swp_entry_to_pte(x) ((pte_t) { (x).val })

/* pfn_pte - convert page number and protection value to page table entry */
#define pfn_pte(pfn, pgprot) __pte((pfn << PAGE_SHIFT) | pgprot_val(pgprot))

/* pte_pfn - convert pte to page frame number */
#define pte_pfn(pte) (pte_val(pte) >> PAGE_SHIFT)
#define set_pmd(pmdptr, pmdval) (*(pmdptr) = (pmdval))

/*
 * set_pte_at - update page table and do whatever magic may be
 * necessary to make the underlying hardware/firmware take note.
 *
 * VM may require a virtual instruction to alert the MMU.
 */
#define set_pte_at(mm, addr, ptep, pte) set_pte(ptep, pte)

/*
 * May need to invoke the virtual machine as well...
 */
#define pte_unmap(pte)		do { } while (0)
#define pte_unmap_nested(pte)	do { } while (0)

/*
 * pte_offset_map - returns the linear address of the page table entry
 * corresponding to an address
 */
#define pte_offset_map(dir, address)                                    \
	((pte_t *)page_address(pmd_page(*(dir))) + __pte_offset(address))

#define pte_offset_map_nested(pmd, addr) pte_offset_map(pmd, addr)

/* pte_offset_kernel - kernel version of pte_offset */
#define pte_offset_kernel(dir, address) \
	((pte_t *) (unsigned long) __va(pmd_val(*dir) & PAGE_MASK) \
				+  __pte_offset(address))

/* ZERO_PAGE - returns the globally shared zero page */
#define ZERO_PAGE(vaddr) (virt_to_page(&empty_zero_page))

#define __pte_offset(address) (((address) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))

/*  I think this is in case we have page table caches; needed by init/main.c  */
#define pgtable_cache_init()    do { } while (0)

/*
 * Swap/file PTE definitions.  If _PAGE_PRESENT is zero, the rest of the PTE is
 * interpreted as swap information.  The remaining free bits are interpreted as
 * swap type/offset tuple.  Rather than have the TLB fill handler test
 * _PAGE_PRESENT, we're going to reserve the permissions bits and set them to
 * all zeros for swap entries, which speeds up the miss handler at the cost of
 * 3 bits of offset.  That trade-off can be revisited if necessary, but Hexagon
 * processor architecture and target applications suggest a lot of TLB misses
 * and not much swap space.
 *
 * Format of swap PTE:
 *	bit	0:	Present (zero)
 *	bits	1-5:	swap type (arch independent layer uses 5 bits max)
 *	bits	6-9:	bits 3:0 of offset
 *	bits	10-12:	effectively _PAGE_PROTNONE (all zero)
 *	bits	13-31:  bits 22:4 of swap offset
 *
 * The split offset makes some of the following macros a little gnarly,
 * but there's plenty of precedent for this sort of thing.
 */

/* Used for swap PTEs */
#define __swp_type(swp_pte)		(((swp_pte).val >> 1) & 0x1f)

#define __swp_offset(swp_pte) \
	((((swp_pte).val >> 6) & 0xf) | (((swp_pte).val >> 9) & 0x7ffff0))

#define __swp_entry(type, offset) \
	((swp_entry_t)	{ \
		((type << 1) | \
		 ((offset & 0x7ffff0) << 9) | ((offset & 0xf) << 6)) })

/*  Oh boy.  There are a lot of possible arch overrides found in this file.  */
#include <asm-generic/pgtable.h>

#endif
