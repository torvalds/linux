/*
 * Macros and functions to manipulate Meta page tables.
 */

#ifndef _METAG_PGTABLE_H
#define _METAG_PGTABLE_H

#include <asm-generic/pgtable-nopmd.h>

/* Invalid regions on Meta: 0x00000000-0x001FFFFF and 0xFFFF0000-0xFFFFFFFF */
#if PAGE_OFFSET >= LINGLOBAL_BASE
#define CONSISTENT_START	0xF7000000
#define CONSISTENT_END		0xF73FFFFF
#define VMALLOC_START		0xF8000000
#define VMALLOC_END		0xFFFEFFFF
#else
#define CONSISTENT_START	0x77000000
#define CONSISTENT_END		0x773FFFFF
#define VMALLOC_START		0x78000000
#define VMALLOC_END		0x7FFFFFFF
#endif

/*
 * Definitions for MMU descriptors
 *
 * These are the hardware bits in the MMCU pte entries.
 * Derived from the Meta toolkit headers.
 */
#define _PAGE_PRESENT		MMCU_ENTRY_VAL_BIT
#define _PAGE_WRITE		MMCU_ENTRY_WR_BIT
#define _PAGE_PRIV		MMCU_ENTRY_PRIV_BIT
/* Write combine bit - this can cause writes to occur out of order */
#define _PAGE_WR_COMBINE	MMCU_ENTRY_WRC_BIT
/* Sys coherent bit - this bit is never used by Linux */
#define _PAGE_SYS_COHERENT	MMCU_ENTRY_SYS_BIT
#define _PAGE_ALWAYS_ZERO_1	0x020
#define _PAGE_CACHE_CTRL0	0x040
#define _PAGE_CACHE_CTRL1	0x080
#define _PAGE_ALWAYS_ZERO_2	0x100
#define _PAGE_ALWAYS_ZERO_3	0x200
#define _PAGE_ALWAYS_ZERO_4	0x400
#define _PAGE_ALWAYS_ZERO_5	0x800

/* These are software bits that we stuff into the gaps in the hardware
 * pte entries that are not used.  Note, these DO get stored in the actual
 * hardware, but the hardware just does not use them.
 */
#define _PAGE_ACCESSED		_PAGE_ALWAYS_ZERO_1
#define _PAGE_DIRTY		_PAGE_ALWAYS_ZERO_2

/* Pages owned, and protected by, the kernel. */
#define _PAGE_KERNEL		_PAGE_PRIV

/* No cacheing of this page */
#define _PAGE_CACHE_WIN0	(MMCU_CWIN_UNCACHED << MMCU_ENTRY_CWIN_S)
/* burst cacheing - good for data streaming */
#define _PAGE_CACHE_WIN1	(MMCU_CWIN_BURST << MMCU_ENTRY_CWIN_S)
/* One cache way per thread */
#define _PAGE_CACHE_WIN2	(MMCU_CWIN_C1SET << MMCU_ENTRY_CWIN_S)
/* Full on cacheing */
#define _PAGE_CACHE_WIN3	(MMCU_CWIN_CACHED << MMCU_ENTRY_CWIN_S)

#define _PAGE_CACHEABLE		(_PAGE_CACHE_WIN3 | _PAGE_WR_COMBINE)

/* which bits are used for cache control ... */
#define _PAGE_CACHE_MASK	(_PAGE_CACHE_CTRL0 | _PAGE_CACHE_CTRL1 | \
				 _PAGE_WR_COMBINE)

/* This is a mask of the bits that pte_modify is allowed to change. */
#define _PAGE_CHG_MASK		(PAGE_MASK)

#define _PAGE_SZ_SHIFT		1
#define _PAGE_SZ_4K		(0x0)
#define _PAGE_SZ_8K		(0x1 << _PAGE_SZ_SHIFT)
#define _PAGE_SZ_16K		(0x2 << _PAGE_SZ_SHIFT)
#define _PAGE_SZ_32K		(0x3 << _PAGE_SZ_SHIFT)
#define _PAGE_SZ_64K		(0x4 << _PAGE_SZ_SHIFT)
#define _PAGE_SZ_128K		(0x5 << _PAGE_SZ_SHIFT)
#define _PAGE_SZ_256K		(0x6 << _PAGE_SZ_SHIFT)
#define _PAGE_SZ_512K		(0x7 << _PAGE_SZ_SHIFT)
#define _PAGE_SZ_1M		(0x8 << _PAGE_SZ_SHIFT)
#define _PAGE_SZ_2M		(0x9 << _PAGE_SZ_SHIFT)
#define _PAGE_SZ_4M		(0xa << _PAGE_SZ_SHIFT)
#define _PAGE_SZ_MASK		(0xf << _PAGE_SZ_SHIFT)

#if defined(CONFIG_PAGE_SIZE_4K)
#define _PAGE_SZ		(_PAGE_SZ_4K)
#elif defined(CONFIG_PAGE_SIZE_8K)
#define _PAGE_SZ		(_PAGE_SZ_8K)
#elif defined(CONFIG_PAGE_SIZE_16K)
#define _PAGE_SZ		(_PAGE_SZ_16K)
#endif
#define _PAGE_TABLE		(_PAGE_SZ | _PAGE_PRESENT)

#if defined(CONFIG_HUGETLB_PAGE_SIZE_8K)
# define _PAGE_SZHUGE		(_PAGE_SZ_8K)
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_16K)
# define _PAGE_SZHUGE		(_PAGE_SZ_16K)
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_32K)
# define _PAGE_SZHUGE		(_PAGE_SZ_32K)
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_64K)
# define _PAGE_SZHUGE		(_PAGE_SZ_64K)
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_128K)
# define _PAGE_SZHUGE		(_PAGE_SZ_128K)
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_256K)
# define _PAGE_SZHUGE		(_PAGE_SZ_256K)
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_512K)
# define _PAGE_SZHUGE		(_PAGE_SZ_512K)
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_1M)
# define _PAGE_SZHUGE		(_PAGE_SZ_1M)
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_2M)
# define _PAGE_SZHUGE		(_PAGE_SZ_2M)
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_4M)
# define _PAGE_SZHUGE		(_PAGE_SZ_4M)
#endif

/*
 * The Linux memory management assumes a three-level page table setup. On
 * Meta, we use that, but "fold" the mid level into the top-level page
 * table.
 */

/* PGDIR_SHIFT determines the size of the area a second-level page table can
 * map. This is always 4MB.
 */

#define PGDIR_SHIFT	22
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * Entries per page directory level: we use a two-level, so
 * we don't really have any PMD directory physically. First level tables
 * always map 2Gb (local or global) at a granularity of 4MB, second-level
 * tables map 4MB with a granularity between 4MB and 4kB (between 1 and
 * 1024 entries).
 */
#define PTRS_PER_PTE	(PGDIR_SIZE/PAGE_SIZE)
#define HPTRS_PER_PTE	(PGDIR_SIZE/HPAGE_SIZE)
#define PTRS_PER_PGD	512

#define USER_PTRS_PER_PGD	256
#define FIRST_USER_ADDRESS	META_MEMORY_BASE
#define FIRST_USER_PGD_NR	pgd_index(FIRST_USER_ADDRESS)

#define PAGE_NONE	__pgprot(_PAGE_PRESENT | _PAGE_ACCESSED | \
				 _PAGE_CACHEABLE)

#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_WRITE | \
				 _PAGE_ACCESSED | _PAGE_CACHEABLE)
#define PAGE_SHARED_C	PAGE_SHARED
#define PAGE_COPY	__pgprot(_PAGE_PRESENT | _PAGE_ACCESSED | \
				 _PAGE_CACHEABLE)
#define PAGE_COPY_C	PAGE_COPY

#define PAGE_READONLY	__pgprot(_PAGE_PRESENT | _PAGE_ACCESSED | \
				 _PAGE_CACHEABLE)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | _PAGE_DIRTY | \
				 _PAGE_ACCESSED | _PAGE_WRITE | \
				 _PAGE_CACHEABLE | _PAGE_KERNEL)

#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY
#define __P010	PAGE_COPY
#define __P011	PAGE_COPY
#define __P100	PAGE_READONLY
#define __P101	PAGE_READONLY
#define __P110	PAGE_COPY_C
#define __P111	PAGE_COPY_C

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_READONLY
#define __S101	PAGE_READONLY
#define __S110	PAGE_SHARED_C
#define __S111	PAGE_SHARED_C

#ifndef __ASSEMBLY__

#include <asm/page.h>

/* zero page used for uninitialized stuff */
extern unsigned long empty_zero_page;
#define ZERO_PAGE(vaddr)	(virt_to_page(empty_zero_page))

/* Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#define set_pte(pteptr, pteval) ((*(pteptr)) = (pteval))
#define set_pte_at(mm, addr, ptep, pteval) set_pte(ptep, pteval)

#define set_pmd(pmdptr, pmdval) (*(pmdptr) = pmdval)

#define pte_pfn(pte)		(pte_val(pte) >> PAGE_SHIFT)

#define pfn_pte(pfn, prot)	__pte(((pfn) << PAGE_SHIFT) | pgprot_val(prot))

#define pte_none(x)		(!pte_val(x))
#define pte_present(x)		(pte_val(x) & _PAGE_PRESENT)
#define pte_clear(mm, addr, xp)	do { pte_val(*(xp)) = 0; } while (0)

#define pmd_none(x)		(!pmd_val(x))
#define pmd_bad(x)		((pmd_val(x) & ~(PAGE_MASK | _PAGE_SZ_MASK)) \
					!= (_PAGE_TABLE & ~_PAGE_SZ_MASK))
#define pmd_present(x)		(pmd_val(x) & _PAGE_PRESENT)
#define pmd_clear(xp)		do { pmd_val(*(xp)) = 0; } while (0)

#define pte_page(x)		pfn_to_page(pte_pfn(x))

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */

static inline int pte_write(pte_t pte)   { return pte_val(pte) & _PAGE_WRITE; }
static inline int pte_dirty(pte_t pte)   { return pte_val(pte) & _PAGE_DIRTY; }
static inline int pte_young(pte_t pte)   { return pte_val(pte) & _PAGE_ACCESSED; }
static inline int pte_special(pte_t pte) { return 0; }

static inline pte_t pte_wrprotect(pte_t pte) { pte_val(pte) &= (~_PAGE_WRITE); return pte; }
static inline pte_t pte_mkclean(pte_t pte)   { pte_val(pte) &= ~_PAGE_DIRTY; return pte; }
static inline pte_t pte_mkold(pte_t pte)     { pte_val(pte) &= ~_PAGE_ACCESSED; return pte; }
static inline pte_t pte_mkwrite(pte_t pte)   { pte_val(pte) |= _PAGE_WRITE; return pte; }
static inline pte_t pte_mkdirty(pte_t pte)   { pte_val(pte) |= _PAGE_DIRTY; return pte; }
static inline pte_t pte_mkyoung(pte_t pte)   { pte_val(pte) |= _PAGE_ACCESSED; return pte; }
static inline pte_t pte_mkspecial(pte_t pte) { return pte; }
static inline pte_t pte_mkhuge(pte_t pte)    { return pte; }

/*
 * Macro and implementation to make a page protection as uncacheable.
 */
#define pgprot_writecombine(prot)					\
	__pgprot(pgprot_val(prot) & ~(_PAGE_CACHE_CTRL1 | _PAGE_CACHE_CTRL0))

#define pgprot_noncached(prot)						\
	__pgprot(pgprot_val(prot) & ~_PAGE_CACHEABLE)


/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */

#define mk_pte(page, pgprot)	pfn_pte(page_to_pfn(page), (pgprot))

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot);
	return pte;
}

static inline unsigned long pmd_page_vaddr(pmd_t pmd)
{
	unsigned long paddr = pmd_val(pmd) & PAGE_MASK;
	if (!paddr)
		return 0;
	return (unsigned long)__va(paddr);
}

#define pmd_page(pmd)		(pfn_to_page(pmd_val(pmd) >> PAGE_SHIFT))
#define pmd_page_shift(pmd)	(12 + ((pmd_val(pmd) & _PAGE_SZ_MASK) \
					>> _PAGE_SZ_SHIFT))
#define pmd_num_ptrs(pmd)	(PGDIR_SIZE >> pmd_page_shift(pmd))

/*
 * Each pgd is only 2k, mapping 2Gb (local or global). If we're in global
 * space drop the top bit before indexing the pgd.
 */
#if PAGE_OFFSET >= LINGLOBAL_BASE
#define pgd_index(address)	((((address) & ~0x80000000) >> PGDIR_SHIFT) \
							& (PTRS_PER_PGD-1))
#else
#define pgd_index(address)	(((address) >> PGDIR_SHIFT) & (PTRS_PER_PGD-1))
#endif

#define pgd_offset(mm, address)	((mm)->pgd + pgd_index(address))

#define pgd_offset_k(address)	pgd_offset(&init_mm, address)

#define pmd_index(address)	(((address) >> PMD_SHIFT) & (PTRS_PER_PMD-1))

/* Find an entry in the second-level page table.. */
#if !defined(CONFIG_HUGETLB_PAGE)
  /* all pages are of size (1 << PAGE_SHIFT), so no need to read 1st level pt */
# define pte_index(pmd, address) \
	(((address) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#else
  /* some pages are huge, so read 1st level pt to find out */
# define pte_index(pmd, address) \
	(((address) >> pmd_page_shift(pmd)) & (pmd_num_ptrs(pmd) - 1))
#endif
#define pte_offset_kernel(dir, address) \
	((pte_t *) pmd_page_vaddr(*(dir)) + pte_index(*(dir), address))
#define pte_offset_map(dir, address)		pte_offset_kernel(dir, address)
#define pte_offset_map_nested(dir, address)	pte_offset_kernel(dir, address)

#define pte_unmap(pte)		do { } while (0)
#define pte_unmap_nested(pte)	do { } while (0)

#define pte_ERROR(e) \
	pr_err("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, pte_val(e))
#define pgd_ERROR(e) \
	pr_err("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

/*
 * Meta doesn't have any external MMU info: the kernel page
 * tables contain all the necessary information.
 */
static inline void update_mmu_cache(struct vm_area_struct *vma,
				    unsigned long address, pte_t *pte)
{
}

/*
 * Encode and decode a swap entry (must be !pte_none(e) && !pte_present(e))
 * Since PAGE_PRESENT is bit 1, we can use the bits above that.
 */
#define __swp_type(x)			(((x).val >> 1) & 0xff)
#define __swp_offset(x)			((x).val >> 10)
#define __swp_entry(type, offset)	((swp_entry_t) { ((type) << 1) | \
					 ((offset) << 10) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val })

#define kern_addr_valid(addr)	(1)

/*
 * No page table caches to initialise
 */
#define pgtable_cache_init()	do { } while (0)

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];
void paging_init(unsigned long mem_end);

#ifdef CONFIG_METAG_META12
/* This is a workaround for an issue in Meta 1 cores. These cores cache
 * invalid entries in the TLB so we always need to flush whenever we add
 * a new pte. Unfortunately we can only flush the whole TLB not shoot down
 * single entries so this is sub-optimal. This implementation ensures that
 * we will get a flush at the second attempt, so we may still get repeated
 * faults, we just don't overflow the kernel stack handling them.
 */
#define __HAVE_ARCH_PTEP_SET_ACCESS_FLAGS
#define ptep_set_access_flags(__vma, __address, __ptep, __entry, __dirty) \
({									  \
	int __changed = !pte_same(*(__ptep), __entry);			  \
	if (__changed) {						  \
		set_pte_at((__vma)->vm_mm, (__address), __ptep, __entry); \
	}								  \
	flush_tlb_page(__vma, __address);				  \
	__changed;							  \
})
#endif

#include <asm-generic/pgtable.h>

#endif /* __ASSEMBLY__ */
#endif /* _METAG_PGTABLE_H */
