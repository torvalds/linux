/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MOTOROLA_PGTABLE_H
#define _MOTOROLA_PGTABLE_H


/*
 * Definitions for MMU descriptors
 */
#define _PAGE_PRESENT	0x001
#define _PAGE_SHORT	0x002
#define _PAGE_RONLY	0x004
#define _PAGE_READWRITE	0x000
#define _PAGE_ACCESSED	0x008
#define _PAGE_DIRTY	0x010
#define _PAGE_SUPER	0x080	/* 68040 supervisor only */
#define _PAGE_GLOBAL040	0x400	/* 68040 global bit, used for kva descs */
#define _PAGE_NOCACHE030 0x040	/* 68030 no-cache mode */
#define _PAGE_NOCACHE	0x060	/* 68040 cache mode, non-serialized */
#define _PAGE_NOCACHE_S	0x040	/* 68040 no-cache mode, serialized */
#define _PAGE_CACHE040	0x020	/* 68040 cache mode, cachable, copyback */
#define _PAGE_CACHE040W	0x000	/* 68040 cache mode, cachable, write-through */

#define _DESCTYPE_MASK	0x003

#define _CACHEMASK040	(~0x060)

/*
 * Currently set to the minimum alignment of table pointers (256 bytes).
 * The hardware only uses the low 4 bits for state:
 *
 *    3 - Used
 *    2 - Write Protected
 *  0,1 - Descriptor Type
 *
 * and has the rest of the bits reserved.
 */
#define _TABLE_MASK	(0xffffff00)

#define _PAGE_TABLE	(_PAGE_SHORT)
#define _PAGE_CHG_MASK  (PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_NOCACHE)

#define _PAGE_PROTNONE	0x004

#ifndef __ASSEMBLY__

/* This is the cache mode to be used for pages containing page descriptors for
 * processors >= '040. It is in pte_mknocache(), and the variable is defined
 * and initialized in head.S */
extern int m68k_pgtable_cachemode;

/* This is the cache mode for normal pages, for supervisor access on
 * processors >= '040. It is used in pte_mkcache(), and the variable is
 * defined and initialized in head.S */

#if defined(CPU_M68060_ONLY) && defined(CONFIG_060_WRITETHROUGH)
#define m68k_supervisor_cachemode _PAGE_CACHE040W
#elif defined(CPU_M68040_OR_M68060_ONLY)
#define m68k_supervisor_cachemode _PAGE_CACHE040
#elif defined(CPU_M68020_OR_M68030_ONLY)
#define m68k_supervisor_cachemode 0
#else
extern int m68k_supervisor_cachemode;
#endif

#if defined(CPU_M68040_OR_M68060_ONLY)
#define mm_cachebits _PAGE_CACHE040
#elif defined(CPU_M68020_OR_M68030_ONLY)
#define mm_cachebits 0
#else
extern unsigned long mm_cachebits;
#endif

#define PAGE_NONE	__pgprot(_PAGE_PROTNONE | _PAGE_ACCESSED | mm_cachebits)
#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_ACCESSED | mm_cachebits)
#define PAGE_COPY	__pgprot(_PAGE_PRESENT | _PAGE_RONLY | _PAGE_ACCESSED | mm_cachebits)
#define PAGE_READONLY	__pgprot(_PAGE_PRESENT | _PAGE_RONLY | _PAGE_ACCESSED | mm_cachebits)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | _PAGE_DIRTY | _PAGE_ACCESSED | mm_cachebits)

/* Alternate definitions that are compile time constants, for
   initializing protection_map.  The cachebits are fixed later.  */
#define PAGE_NONE_C	__pgprot(_PAGE_PROTNONE | _PAGE_ACCESSED)
#define PAGE_SHARED_C	__pgprot(_PAGE_PRESENT | _PAGE_ACCESSED)
#define PAGE_COPY_C	__pgprot(_PAGE_PRESENT | _PAGE_RONLY | _PAGE_ACCESSED)
#define PAGE_READONLY_C	__pgprot(_PAGE_PRESENT | _PAGE_RONLY | _PAGE_ACCESSED)

/*
 * The m68k can't do page protection for execute, and considers that the same are read.
 * Also, write permissions imply read permissions. This is the closest we can get..
 */
#define __P000	PAGE_NONE_C
#define __P001	PAGE_READONLY_C
#define __P010	PAGE_COPY_C
#define __P011	PAGE_COPY_C
#define __P100	PAGE_READONLY_C
#define __P101	PAGE_READONLY_C
#define __P110	PAGE_COPY_C
#define __P111	PAGE_COPY_C

#define __S000	PAGE_NONE_C
#define __S001	PAGE_READONLY_C
#define __S010	PAGE_SHARED_C
#define __S011	PAGE_SHARED_C
#define __S100	PAGE_READONLY_C
#define __S101	PAGE_READONLY_C
#define __S110	PAGE_SHARED_C
#define __S111	PAGE_SHARED_C

#define pmd_pgtable(pmd) ((pgtable_t)pmd_page_vaddr(pmd))

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define mk_pte(page, pgprot) pfn_pte(page_to_pfn(page), (pgprot))

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot);
	return pte;
}

static inline void pmd_set(pmd_t *pmdp, pte_t *ptep)
{
	pmd_val(*pmdp) = virt_to_phys(ptep) | _PAGE_TABLE | _PAGE_ACCESSED;
}

static inline void pud_set(pud_t *pudp, pmd_t *pmdp)
{
	pud_val(*pudp) = _PAGE_TABLE | _PAGE_ACCESSED | __pa(pmdp);
}

#define __pte_page(pte) ((unsigned long)__va(pte_val(pte) & PAGE_MASK))
#define pmd_page_vaddr(pmd) ((unsigned long)__va(pmd_val(pmd) & _TABLE_MASK))
#define pud_pgtable(pud) ((pmd_t *)__va(pud_val(pud) & _TABLE_MASK))


#define pte_none(pte)		(!pte_val(pte))
#define pte_present(pte)	(pte_val(pte) & (_PAGE_PRESENT | _PAGE_PROTNONE))
#define pte_clear(mm,addr,ptep)		({ pte_val(*(ptep)) = 0; })

#define pte_page(pte)		virt_to_page(__va(pte_val(pte)))
#define pte_pfn(pte)		(pte_val(pte) >> PAGE_SHIFT)
#define pfn_pte(pfn, prot)	__pte(((pfn) << PAGE_SHIFT) | pgprot_val(prot))

#define pmd_none(pmd)		(!pmd_val(pmd))
#define pmd_bad(pmd)		((pmd_val(pmd) & _DESCTYPE_MASK) != _PAGE_TABLE)
#define pmd_present(pmd)	(pmd_val(pmd) & _PAGE_TABLE)
#define pmd_clear(pmdp)		({ pmd_val(*pmdp) = 0; })

/*
 * m68k does not have huge pages (020/030 actually could), but generic code
 * expects pmd_page() to exists, only to then DCE it all. Provide a dummy to
 * make the compiler happy.
 */
#define pmd_page(pmd)		NULL


#define pud_none(pud)		(!pud_val(pud))
#define pud_bad(pud)		((pud_val(pud) & _DESCTYPE_MASK) != _PAGE_TABLE)
#define pud_present(pud)	(pud_val(pud) & _PAGE_TABLE)
#define pud_clear(pudp)		({ pud_val(*pudp) = 0; })
#define pud_page(pud)		(mem_map + ((unsigned long)(__va(pud_val(pud)) - PAGE_OFFSET) >> PAGE_SHIFT))

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, pte_val(e))
#define pmd_ERROR(e) \
	printk("%s:%d: bad pmd %08lx.\n", __FILE__, __LINE__, pmd_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))


/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
static inline int pte_write(pte_t pte)		{ return !(pte_val(pte) & _PAGE_RONLY); }
static inline int pte_dirty(pte_t pte)		{ return pte_val(pte) & _PAGE_DIRTY; }
static inline int pte_young(pte_t pte)		{ return pte_val(pte) & _PAGE_ACCESSED; }

static inline pte_t pte_wrprotect(pte_t pte)	{ pte_val(pte) |= _PAGE_RONLY; return pte; }
static inline pte_t pte_mkclean(pte_t pte)	{ pte_val(pte) &= ~_PAGE_DIRTY; return pte; }
static inline pte_t pte_mkold(pte_t pte)	{ pte_val(pte) &= ~_PAGE_ACCESSED; return pte; }
static inline pte_t pte_mkwrite(pte_t pte)	{ pte_val(pte) &= ~_PAGE_RONLY; return pte; }
static inline pte_t pte_mkdirty(pte_t pte)	{ pte_val(pte) |= _PAGE_DIRTY; return pte; }
static inline pte_t pte_mkyoung(pte_t pte)	{ pte_val(pte) |= _PAGE_ACCESSED; return pte; }
static inline pte_t pte_mknocache(pte_t pte)
{
	pte_val(pte) = (pte_val(pte) & _CACHEMASK040) | m68k_pgtable_cachemode;
	return pte;
}
static inline pte_t pte_mkcache(pte_t pte)
{
	pte_val(pte) = (pte_val(pte) & _CACHEMASK040) | m68k_supervisor_cachemode;
	return pte;
}

#define swapper_pg_dir kernel_pg_dir
extern pgd_t kernel_pg_dir[128];

/* Encode and de-code a swap entry (must be !pte_none(e) && !pte_present(e)) */
#define __swp_type(x)		(((x).val >> 4) & 0xff)
#define __swp_offset(x)		((x).val >> 12)
#define __swp_entry(type, offset) ((swp_entry_t) { ((type) << 4) | ((offset) << 12) })
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val })

#endif	/* !__ASSEMBLY__ */
#endif /* _MOTOROLA_PGTABLE_H */
