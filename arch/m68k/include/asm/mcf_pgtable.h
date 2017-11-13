/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MCF_PGTABLE_H
#define _MCF_PGTABLE_H

#include <asm/mcfmmu.h>
#include <asm/page.h>

/*
 * MMUDR bits, in proper place. We write these directly into the MMUDR
 * after masking from the pte.
 */
#define CF_PAGE_LOCKED		MMUDR_LK	/* 0x00000002 */
#define CF_PAGE_EXEC		MMUDR_X		/* 0x00000004 */
#define CF_PAGE_WRITABLE	MMUDR_W		/* 0x00000008 */
#define CF_PAGE_READABLE	MMUDR_R		/* 0x00000010 */
#define CF_PAGE_SYSTEM		MMUDR_SP	/* 0x00000020 */
#define CF_PAGE_COPYBACK	MMUDR_CM_CCB	/* 0x00000040 */
#define CF_PAGE_NOCACHE		MMUDR_CM_NCP	/* 0x00000080 */

#define CF_CACHEMASK		(~MMUDR_CM_CCB)
#define CF_PAGE_MMUDR_MASK	0x000000fe

#define _PAGE_NOCACHE030	CF_PAGE_NOCACHE

/*
 * MMUTR bits, need shifting down.
 */
#define CF_PAGE_MMUTR_MASK	0x00000c00
#define CF_PAGE_MMUTR_SHIFT	10

#define CF_PAGE_VALID		(MMUTR_V << CF_PAGE_MMUTR_SHIFT)
#define CF_PAGE_SHARED		(MMUTR_SG << CF_PAGE_MMUTR_SHIFT)

/*
 * Fake bits, not implemented in CF, will get masked out before
 * hitting hardware.
 */
#define CF_PAGE_DIRTY		0x00000001
#define CF_PAGE_ACCESSED	0x00001000

#define _PAGE_CACHE040		0x020   /* 68040 cache mode, cachable, copyback */
#define _PAGE_NOCACHE_S		0x040   /* 68040 no-cache mode, serialized */
#define _PAGE_NOCACHE		0x060   /* 68040 cache mode, non-serialized */
#define _PAGE_CACHE040W		0x000   /* 68040 cache mode, cachable, write-through */
#define _DESCTYPE_MASK		0x003
#define _CACHEMASK040		(~0x060)
#define _PAGE_GLOBAL040		0x400   /* 68040 global bit, used for kva descs */

/*
 * Externally used page protection values.
 */
#define _PAGE_PRESENT	(CF_PAGE_VALID)
#define _PAGE_ACCESSED	(CF_PAGE_ACCESSED)
#define _PAGE_DIRTY	(CF_PAGE_DIRTY)
#define _PAGE_READWRITE (CF_PAGE_READABLE \
				| CF_PAGE_WRITABLE \
				| CF_PAGE_SYSTEM \
				| CF_PAGE_SHARED)

/*
 * Compound page protection values.
 */
#define PAGE_NONE	__pgprot(CF_PAGE_VALID \
				 | CF_PAGE_ACCESSED)

#define PAGE_SHARED     __pgprot(CF_PAGE_VALID \
				 | CF_PAGE_ACCESSED \
				 | CF_PAGE_SHARED)

#define PAGE_INIT	__pgprot(CF_PAGE_VALID \
				 | CF_PAGE_READABLE \
				 | CF_PAGE_WRITABLE \
				 | CF_PAGE_EXEC \
				 | CF_PAGE_SYSTEM)

#define PAGE_KERNEL	__pgprot(CF_PAGE_VALID \
				 | CF_PAGE_ACCESSED \
				 | CF_PAGE_READABLE \
				 | CF_PAGE_WRITABLE \
				 | CF_PAGE_EXEC \
				 | CF_PAGE_SYSTEM \
				 | CF_PAGE_SHARED)

#define PAGE_COPY	__pgprot(CF_PAGE_VALID \
				 | CF_PAGE_ACCESSED \
				 | CF_PAGE_READABLE \
				 | CF_PAGE_DIRTY)

/*
 * Page protections for initialising protection_map. See mm/mmap.c
 * for use. In general, the bit positions are xwr, and P-items are
 * private, the S-items are shared.
 */
#define __P000		PAGE_NONE
#define __P001		__pgprot(CF_PAGE_VALID \
				 | CF_PAGE_ACCESSED \
				 | CF_PAGE_READABLE)
#define __P010		__pgprot(CF_PAGE_VALID \
				 | CF_PAGE_ACCESSED \
				 | CF_PAGE_WRITABLE)
#define __P011		__pgprot(CF_PAGE_VALID \
				 | CF_PAGE_ACCESSED \
				 | CF_PAGE_READABLE \
				 | CF_PAGE_WRITABLE)
#define __P100		__pgprot(CF_PAGE_VALID \
				 | CF_PAGE_ACCESSED \
				 | CF_PAGE_EXEC)
#define __P101		__pgprot(CF_PAGE_VALID \
				 | CF_PAGE_ACCESSED \
				 | CF_PAGE_READABLE \
				 | CF_PAGE_EXEC)
#define __P110		__pgprot(CF_PAGE_VALID \
				 | CF_PAGE_ACCESSED \
				 | CF_PAGE_WRITABLE \
				 | CF_PAGE_EXEC)
#define __P111		__pgprot(CF_PAGE_VALID \
				 | CF_PAGE_ACCESSED \
				 | CF_PAGE_READABLE \
				 | CF_PAGE_WRITABLE \
				 | CF_PAGE_EXEC)

#define __S000		PAGE_NONE
#define __S001		__pgprot(CF_PAGE_VALID \
				 | CF_PAGE_ACCESSED \
				 | CF_PAGE_READABLE)
#define __S010		PAGE_SHARED
#define __S011		__pgprot(CF_PAGE_VALID \
				 | CF_PAGE_ACCESSED \
				 | CF_PAGE_SHARED \
				 | CF_PAGE_READABLE)
#define __S100		__pgprot(CF_PAGE_VALID \
				 | CF_PAGE_ACCESSED \
				 | CF_PAGE_EXEC)
#define __S101		__pgprot(CF_PAGE_VALID \
				 | CF_PAGE_ACCESSED \
				 | CF_PAGE_READABLE \
				 | CF_PAGE_EXEC)
#define __S110		__pgprot(CF_PAGE_VALID \
				 | CF_PAGE_ACCESSED \
				 | CF_PAGE_SHARED \
				 | CF_PAGE_EXEC)
#define __S111		__pgprot(CF_PAGE_VALID \
				 | CF_PAGE_ACCESSED \
				 | CF_PAGE_SHARED \
				 | CF_PAGE_READABLE \
				 | CF_PAGE_EXEC)

#define PTE_MASK	PAGE_MASK
#define CF_PAGE_CHG_MASK (PTE_MASK | CF_PAGE_ACCESSED | CF_PAGE_DIRTY)

#ifndef __ASSEMBLY__

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define mk_pte(page, pgprot) pfn_pte(page_to_pfn(page), (pgprot))

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	pte_val(pte) = (pte_val(pte) & CF_PAGE_CHG_MASK) | pgprot_val(newprot);
	return pte;
}

#define pmd_set(pmdp, ptep) do {} while (0)

static inline void pgd_set(pgd_t *pgdp, pmd_t *pmdp)
{
	pgd_val(*pgdp) = virt_to_phys(pmdp);
}

#define __pte_page(pte)	((unsigned long) (pte_val(pte) & PAGE_MASK))
#define __pmd_page(pmd)	((unsigned long) (pmd_val(pmd)))

static inline int pte_none(pte_t pte)
{
	return !pte_val(pte);
}

static inline int pte_present(pte_t pte)
{
	return pte_val(pte) & CF_PAGE_VALID;
}

static inline void pte_clear(struct mm_struct *mm, unsigned long addr,
	pte_t *ptep)
{
	pte_val(*ptep) = 0;
}

#define pte_pagenr(pte)	((__pte_page(pte) - PAGE_OFFSET) >> PAGE_SHIFT)
#define pte_page(pte)	virt_to_page(__pte_page(pte))

static inline int pmd_none2(pmd_t *pmd) { return !pmd_val(*pmd); }
#define pmd_none(pmd) pmd_none2(&(pmd))
static inline int pmd_bad2(pmd_t *pmd) { return 0; }
#define pmd_bad(pmd) pmd_bad2(&(pmd))
#define pmd_present(pmd) (!pmd_none2(&(pmd)))
static inline void pmd_clear(pmd_t *pmdp) { pmd_val(*pmdp) = 0; }

static inline int pgd_none(pgd_t pgd) { return 0; }
static inline int pgd_bad(pgd_t pgd) { return 0; }
static inline int pgd_present(pgd_t pgd) { return 1; }
static inline void pgd_clear(pgd_t *pgdp) {}

#define pte_ERROR(e) \
	printk(KERN_ERR "%s:%d: bad pte %08lx.\n",	\
	__FILE__, __LINE__, pte_val(e))
#define pmd_ERROR(e) \
	printk(KERN_ERR "%s:%d: bad pmd %08lx.\n",	\
	__FILE__, __LINE__, pmd_val(e))
#define pgd_ERROR(e) \
	printk(KERN_ERR "%s:%d: bad pgd %08lx.\n",	\
	__FILE__, __LINE__, pgd_val(e))

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not...
 * [we have the full set here even if they don't change from m68k]
 */
static inline int pte_read(pte_t pte)
{
	return pte_val(pte) & CF_PAGE_READABLE;
}

static inline int pte_write(pte_t pte)
{
	return pte_val(pte) & CF_PAGE_WRITABLE;
}

static inline int pte_exec(pte_t pte)
{
	return pte_val(pte) & CF_PAGE_EXEC;
}

static inline int pte_dirty(pte_t pte)
{
	return pte_val(pte) & CF_PAGE_DIRTY;
}

static inline int pte_young(pte_t pte)
{
	return pte_val(pte) & CF_PAGE_ACCESSED;
}

static inline int pte_special(pte_t pte)
{
	return 0;
}

static inline pte_t pte_wrprotect(pte_t pte)
{
	pte_val(pte) &= ~CF_PAGE_WRITABLE;
	return pte;
}

static inline pte_t pte_rdprotect(pte_t pte)
{
	pte_val(pte) &= ~CF_PAGE_READABLE;
	return pte;
}

static inline pte_t pte_exprotect(pte_t pte)
{
	pte_val(pte) &= ~CF_PAGE_EXEC;
	return pte;
}

static inline pte_t pte_mkclean(pte_t pte)
{
	pte_val(pte) &= ~CF_PAGE_DIRTY;
	return pte;
}

static inline pte_t pte_mkold(pte_t pte)
{
	pte_val(pte) &= ~CF_PAGE_ACCESSED;
	return pte;
}

static inline pte_t pte_mkwrite(pte_t pte)
{
	pte_val(pte) |= CF_PAGE_WRITABLE;
	return pte;
}

static inline pte_t pte_mkread(pte_t pte)
{
	pte_val(pte) |= CF_PAGE_READABLE;
	return pte;
}

static inline pte_t pte_mkexec(pte_t pte)
{
	pte_val(pte) |= CF_PAGE_EXEC;
	return pte;
}

static inline pte_t pte_mkdirty(pte_t pte)
{
	pte_val(pte) |= CF_PAGE_DIRTY;
	return pte;
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	pte_val(pte) |= CF_PAGE_ACCESSED;
	return pte;
}

static inline pte_t pte_mknocache(pte_t pte)
{
	pte_val(pte) |= 0x80 | (pte_val(pte) & ~0x40);
	return pte;
}

static inline pte_t pte_mkcache(pte_t pte)
{
	pte_val(pte) &= ~CF_PAGE_NOCACHE;
	return pte;
}

static inline pte_t pte_mkspecial(pte_t pte)
{
	return pte;
}

#define swapper_pg_dir kernel_pg_dir
extern pgd_t kernel_pg_dir[PTRS_PER_PGD];

/*
 * Find an entry in a pagetable directory.
 */
#define pgd_index(address)	((address) >> PGDIR_SHIFT)
#define pgd_offset(mm, address)	((mm)->pgd + pgd_index(address))

/*
 * Find an entry in a kernel pagetable directory.
 */
#define pgd_offset_k(address)	pgd_offset(&init_mm, address)

/*
 * Find an entry in the second-level pagetable.
 */
static inline pmd_t *pmd_offset(pgd_t *pgd, unsigned long address)
{
	return (pmd_t *) pgd;
}

/*
 * Find an entry in the third-level pagetable.
 */
#define __pte_offset(address)	((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#define pte_offset_kernel(dir, address) \
	((pte_t *) __pmd_page(*(dir)) + __pte_offset(address))

/*
 * Disable caching for page at given kernel virtual address.
 */
static inline void nocache_page(void *vaddr)
{
	pgd_t *dir;
	pmd_t *pmdp;
	pte_t *ptep;
	unsigned long addr = (unsigned long) vaddr;

	dir = pgd_offset_k(addr);
	pmdp = pmd_offset(dir, addr);
	ptep = pte_offset_kernel(pmdp, addr);
	*ptep = pte_mknocache(*ptep);
}

/*
 * Enable caching for page at given kernel virtual address.
 */
static inline void cache_page(void *vaddr)
{
	pgd_t *dir;
	pmd_t *pmdp;
	pte_t *ptep;
	unsigned long addr = (unsigned long) vaddr;

	dir = pgd_offset_k(addr);
	pmdp = pmd_offset(dir, addr);
	ptep = pte_offset_kernel(pmdp, addr);
	*ptep = pte_mkcache(*ptep);
}

/*
 * Encode and de-code a swap entry (must be !pte_none(e) && !pte_present(e))
 */
#define __swp_type(x)		((x).val & 0xFF)
#define __swp_offset(x)		((x).val >> 11)
#define __swp_entry(typ, off)	((swp_entry_t) { (typ) | \
					(off << 11) })
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	(__pte((x).val))

#define pmd_page(pmd)		(pfn_to_page(pmd_val(pmd) >> PAGE_SHIFT))

#define pte_offset_map(pmdp, addr) ((pte_t *)__pmd_page(*pmdp) + \
				       __pte_offset(addr))
#define pte_unmap(pte)		((void) 0)
#define pfn_pte(pfn, prot)	__pte(((pfn) << PAGE_SHIFT) | pgprot_val(prot))
#define pte_pfn(pte)		(pte_val(pte) >> PAGE_SHIFT)

#endif	/* !__ASSEMBLY__ */
#endif	/* _MCF_PGTABLE_H */
