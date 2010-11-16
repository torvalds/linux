/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_PGTABLE_H
#define __ASM_AVR32_PGTABLE_H

#include <asm/addrspace.h>

#ifndef __ASSEMBLY__
#include <linux/sched.h>

#endif /* !__ASSEMBLY__ */

/*
 * Use two-level page tables just as the i386 (without PAE)
 */
#include <asm/pgtable-2level.h>

/*
 * The following code might need some cleanup when the values are
 * final...
 */
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

#define USER_PTRS_PER_PGD	(TASK_SIZE / PGDIR_SIZE)
#define FIRST_USER_ADDRESS	0

#ifndef __ASSEMBLY__
extern pgd_t swapper_pg_dir[PTRS_PER_PGD];
extern void paging_init(void);

/*
 * ZERO_PAGE is a global shared page that is always zero: used for
 * zero-mapped memory areas etc.
 */
extern struct page *empty_zero_page;
#define ZERO_PAGE(vaddr) (empty_zero_page)

/*
 * Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8 MiB value just means that there will be a 8 MiB "hole"
 * after the uncached physical memory (P2 segment) until the vmalloc
 * area starts. That means that any out-of-bounds memory accesses will
 * hopefully be caught; we don't know if the end of the P1/P2 segments
 * are actually used for anything, but it is anyway safer to let the
 * MMU catch these kinds of errors than to rely on the memory bus.
 *
 * A "hole" of the same size is added to the end of the P3 segment as
 * well. It might seem wasteful to use 16 MiB of virtual address space
 * on this, but we do have 512 MiB of it...
 *
 * The vmalloc() routines leave a hole of 4 KiB between each vmalloced
 * area for the same reason.
 */
#define VMALLOC_OFFSET	(8 * 1024 * 1024)
#define VMALLOC_START	(P3SEG + VMALLOC_OFFSET)
#define VMALLOC_END	(P4SEG - VMALLOC_OFFSET)
#endif /* !__ASSEMBLY__ */

/*
 * Page flags. Some of these flags are not directly supported by
 * hardware, so we have to emulate them.
 */
#define _TLBEHI_BIT_VALID	9
#define _TLBEHI_VALID		(1 << _TLBEHI_BIT_VALID)

#define _PAGE_BIT_WT		0  /* W-bit   : write-through */
#define _PAGE_BIT_DIRTY		1  /* D-bit   : page changed */
#define _PAGE_BIT_SZ0		2  /* SZ0-bit : Size of page */
#define _PAGE_BIT_SZ1		3  /* SZ1-bit : Size of page */
#define _PAGE_BIT_EXECUTE	4  /* X-bit   : execute access allowed */
#define _PAGE_BIT_RW		5  /* AP0-bit : write access allowed */
#define _PAGE_BIT_USER		6  /* AP1-bit : user space access allowed */
#define _PAGE_BIT_BUFFER	7  /* B-bit   : bufferable */
#define _PAGE_BIT_GLOBAL	8  /* G-bit   : global (ignore ASID) */
#define _PAGE_BIT_CACHABLE	9  /* C-bit   : cachable */

/* If we drop support for 1K pages, we get two extra bits */
#define _PAGE_BIT_PRESENT	10
#define _PAGE_BIT_ACCESSED	11 /* software: page was accessed */

/* The following flags are only valid when !PRESENT */
#define _PAGE_BIT_FILE		0 /* software: pagecache or swap? */

#define _PAGE_WT		(1 << _PAGE_BIT_WT)
#define _PAGE_DIRTY		(1 << _PAGE_BIT_DIRTY)
#define _PAGE_EXECUTE		(1 << _PAGE_BIT_EXECUTE)
#define _PAGE_RW		(1 << _PAGE_BIT_RW)
#define _PAGE_USER		(1 << _PAGE_BIT_USER)
#define _PAGE_BUFFER		(1 << _PAGE_BIT_BUFFER)
#define _PAGE_GLOBAL		(1 << _PAGE_BIT_GLOBAL)
#define _PAGE_CACHABLE		(1 << _PAGE_BIT_CACHABLE)

/* Software flags */
#define _PAGE_ACCESSED		(1 << _PAGE_BIT_ACCESSED)
#define _PAGE_PRESENT		(1 << _PAGE_BIT_PRESENT)
#define _PAGE_FILE		(1 << _PAGE_BIT_FILE)

/*
 * Page types, i.e. sizes. _PAGE_TYPE_NONE corresponds to what is
 * usually called _PAGE_PROTNONE on other architectures.
 *
 * XXX: Find out if _PAGE_PROTNONE is equivalent with !_PAGE_USER. If
 * so, we can encode all possible page sizes (although we can't really
 * support 1K pages anyway due to the _PAGE_PRESENT and _PAGE_ACCESSED
 * bits)
 *
 */
#define _PAGE_TYPE_MASK		((1 << _PAGE_BIT_SZ0) | (1 << _PAGE_BIT_SZ1))
#define _PAGE_TYPE_NONE		(0 << _PAGE_BIT_SZ0)
#define _PAGE_TYPE_SMALL	(1 << _PAGE_BIT_SZ0)
#define _PAGE_TYPE_MEDIUM	(2 << _PAGE_BIT_SZ0)
#define _PAGE_TYPE_LARGE	(3 << _PAGE_BIT_SZ0)

/*
 * Mask which drop software flags. We currently can't handle more than
 * 512 MiB of physical memory, so we can use bits 29-31 for other
 * stuff.  With a fixed 4K page size, we can use bits 10-11 as well as
 * bits 2-3 (SZ)
 */
#define _PAGE_FLAGS_HARDWARE_MASK	0xfffff3ff

#define _PAGE_FLAGS_CACHE_MASK	(_PAGE_CACHABLE | _PAGE_BUFFER | _PAGE_WT)

/* Flags that may be modified by software */
#define _PAGE_CHG_MASK		(PTE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY \
				 | _PAGE_FLAGS_CACHE_MASK)

#define _PAGE_FLAGS_READ	(_PAGE_CACHABLE	| _PAGE_BUFFER)
#define _PAGE_FLAGS_WRITE	(_PAGE_FLAGS_READ | _PAGE_RW | _PAGE_DIRTY)

#define _PAGE_NORMAL(x)	__pgprot((x) | _PAGE_PRESENT | _PAGE_TYPE_SMALL	\
				 | _PAGE_ACCESSED)

#define PAGE_NONE	(_PAGE_ACCESSED | _PAGE_TYPE_NONE)
#define PAGE_READ	(_PAGE_FLAGS_READ | _PAGE_USER)
#define PAGE_EXEC	(_PAGE_FLAGS_READ | _PAGE_EXECUTE | _PAGE_USER)
#define PAGE_WRITE	(_PAGE_FLAGS_WRITE | _PAGE_USER)
#define PAGE_KERNEL	_PAGE_NORMAL(_PAGE_FLAGS_WRITE | _PAGE_EXECUTE | _PAGE_GLOBAL)
#define PAGE_KERNEL_RO	_PAGE_NORMAL(_PAGE_FLAGS_READ | _PAGE_EXECUTE | _PAGE_GLOBAL)

#define _PAGE_P(x)	_PAGE_NORMAL((x) & ~(_PAGE_RW | _PAGE_DIRTY))
#define _PAGE_S(x)	_PAGE_NORMAL(x)

#define PAGE_COPY	_PAGE_P(PAGE_WRITE | PAGE_READ)
#define PAGE_SHARED	_PAGE_S(PAGE_WRITE | PAGE_READ)

#ifndef __ASSEMBLY__
/*
 * The hardware supports flags for write- and execute access. Read is
 * always allowed if the page is loaded into the TLB, so the "-w-",
 * "--x" and "-wx" mappings are implemented as "rw-", "r-x" and "rwx",
 * respectively.
 *
 * The "---" case is handled by software; the page will simply not be
 * loaded into the TLB if the page type is _PAGE_TYPE_NONE.
 */

#define __P000	__pgprot(PAGE_NONE)
#define __P001	_PAGE_P(PAGE_READ)
#define __P010	_PAGE_P(PAGE_WRITE)
#define __P011	_PAGE_P(PAGE_WRITE | PAGE_READ)
#define __P100	_PAGE_P(PAGE_EXEC)
#define __P101	_PAGE_P(PAGE_EXEC | PAGE_READ)
#define __P110	_PAGE_P(PAGE_EXEC | PAGE_WRITE)
#define __P111	_PAGE_P(PAGE_EXEC | PAGE_WRITE | PAGE_READ)

#define __S000	__pgprot(PAGE_NONE)
#define __S001	_PAGE_S(PAGE_READ)
#define __S010	_PAGE_S(PAGE_WRITE)
#define __S011	_PAGE_S(PAGE_WRITE | PAGE_READ)
#define __S100	_PAGE_S(PAGE_EXEC)
#define __S101	_PAGE_S(PAGE_EXEC | PAGE_READ)
#define __S110	_PAGE_S(PAGE_EXEC | PAGE_WRITE)
#define __S111	_PAGE_S(PAGE_EXEC | PAGE_WRITE | PAGE_READ)

#define pte_none(x)	(!pte_val(x))
#define pte_present(x)	(pte_val(x) & _PAGE_PRESENT)

#define pte_clear(mm,addr,xp)					\
	do {							\
		set_pte_at(mm, addr, xp, __pte(0));		\
	} while (0)

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
static inline int pte_write(pte_t pte)
{
	return pte_val(pte) & _PAGE_RW;
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
	return 0;
}

/*
 * The following only work if pte_present() is not true.
 */
static inline int pte_file(pte_t pte)
{
	return pte_val(pte) & _PAGE_FILE;
}

/* Mutator functions for PTE bits */
static inline pte_t pte_wrprotect(pte_t pte)
{
	set_pte(&pte, __pte(pte_val(pte) & ~_PAGE_RW));
	return pte;
}
static inline pte_t pte_mkclean(pte_t pte)
{
	set_pte(&pte, __pte(pte_val(pte) & ~_PAGE_DIRTY));
	return pte;
}
static inline pte_t pte_mkold(pte_t pte)
{
	set_pte(&pte, __pte(pte_val(pte) & ~_PAGE_ACCESSED));
	return pte;
}
static inline pte_t pte_mkwrite(pte_t pte)
{
	set_pte(&pte, __pte(pte_val(pte) | _PAGE_RW));
	return pte;
}
static inline pte_t pte_mkdirty(pte_t pte)
{
	set_pte(&pte, __pte(pte_val(pte) | _PAGE_DIRTY));
	return pte;
}
static inline pte_t pte_mkyoung(pte_t pte)
{
	set_pte(&pte, __pte(pte_val(pte) | _PAGE_ACCESSED));
	return pte;
}
static inline pte_t pte_mkspecial(pte_t pte)
{
	return pte;
}

#define pmd_none(x)	(!pmd_val(x))
#define pmd_present(x)	(pmd_val(x))

static inline void pmd_clear(pmd_t *pmdp)
{
	set_pmd(pmdp, __pmd(0));
}

#define	pmd_bad(x)	(pmd_val(x) & ~PAGE_MASK)

/*
 * Permanent address of a page. We don't support highmem, so this is
 * trivial.
 */
#define pages_to_mb(x)	((x) >> (20-PAGE_SHIFT))
#define pte_page(x)	(pfn_to_page(pte_pfn(x)))

/*
 * Mark the prot value as uncacheable and unbufferable
 */
#define pgprot_noncached(prot)						\
	__pgprot(pgprot_val(prot) & ~(_PAGE_BUFFER | _PAGE_CACHABLE))

/*
 * Mark the prot value as uncacheable but bufferable
 */
#define pgprot_writecombine(prot)					\
	__pgprot((pgprot_val(prot) & ~_PAGE_CACHABLE) | _PAGE_BUFFER)

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 *
 * extern pte_t mk_pte(struct page *page, pgprot_t pgprot)
 */
#define mk_pte(page, pgprot)	pfn_pte(page_to_pfn(page), (pgprot))

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	set_pte(&pte, __pte((pte_val(pte) & _PAGE_CHG_MASK)
			    | pgprot_val(newprot)));
	return pte;
}

#define page_pte(page)	page_pte_prot(page, __pgprot(0))

#define pmd_page_vaddr(pmd)	pmd_val(pmd)
#define pmd_page(pmd)		(virt_to_page(pmd_val(pmd)))

/* to find an entry in a page-table-directory. */
#define pgd_index(address)	(((address) >> PGDIR_SHIFT)	\
				 & (PTRS_PER_PGD - 1))
#define pgd_offset(mm, address)	((mm)->pgd + pgd_index(address))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address)	pgd_offset(&init_mm, address)

/* Find an entry in the third-level page table.. */
#define pte_index(address)				\
	((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#define pte_offset(dir, address)					\
	((pte_t *) pmd_page_vaddr(*(dir)) + pte_index(address))
#define pte_offset_kernel(dir, address)					\
	((pte_t *) pmd_page_vaddr(*(dir)) + pte_index(address))
#define pte_offset_map(dir, address) pte_offset_kernel(dir, address)
#define pte_unmap(pte)		do { } while (0)

struct vm_area_struct;
extern void update_mmu_cache(struct vm_area_struct * vma,
			     unsigned long address, pte_t *ptep);

/*
 * Encode and decode a swap entry
 *
 * Constraints:
 *   _PAGE_FILE at bit 0
 *   _PAGE_TYPE_* at bits 2-3 (for emulating _PAGE_PROTNONE)
 *   _PAGE_PRESENT at bit 10
 *
 * We encode the type into bits 4-9 and offset into bits 11-31. This
 * gives us a 21 bits offset, or 2**21 * 4K = 8G usable swap space per
 * device, and 64 possible types.
 *
 * NOTE: We should set ZEROs at the position of _PAGE_PRESENT
 *       and _PAGE_PROTNONE bits
 */
#define __swp_type(x)		(((x).val >> 4) & 0x3f)
#define __swp_offset(x)		((x).val >> 11)
#define __swp_entry(type, offset) ((swp_entry_t) { ((type) << 4) | ((offset) << 11) })
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val })

/*
 * Encode and decode a nonlinear file mapping entry. We have to
 * preserve _PAGE_FILE and _PAGE_PRESENT here. _PAGE_TYPE_* isn't
 * necessary, since _PAGE_FILE implies !_PAGE_PROTNONE (?)
 */
#define PTE_FILE_MAX_BITS	30
#define pte_to_pgoff(pte)	(((pte_val(pte) >> 1) & 0x1ff)		\
				 | ((pte_val(pte) >> 11) << 9))
#define pgoff_to_pte(off)	((pte_t) { ((((off) & 0x1ff) << 1)	\
					    | (((off) >> 9) << 11)	\
					    | _PAGE_FILE) })

typedef pte_t *pte_addr_t;

#define kern_addr_valid(addr)	(1)

#define io_remap_pfn_range(vma, vaddr, pfn, size, prot)	\
	remap_pfn_range(vma, vaddr, pfn, size, prot)

/* No page table caches to initialize (?) */
#define pgtable_cache_init()	do { } while(0)

#include <asm-generic/pgtable.h>

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_AVR32_PGTABLE_H */
