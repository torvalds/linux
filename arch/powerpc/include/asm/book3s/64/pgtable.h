#ifndef _ASM_POWERPC_BOOK3S_64_PGTABLE_H_
#define _ASM_POWERPC_BOOK3S_64_PGTABLE_H_
/*
 * This file contains the functions and defines necessary to modify and use
 * the ppc64 hashed page table.
 */

#include <asm/book3s/64/hash.h>
#include <asm/barrier.h>

/*
 * The second half of the kernel virtual space is used for IO mappings,
 * it's itself carved into the PIO region (ISA and PHB IO space) and
 * the ioremap space
 *
 *  ISA_IO_BASE = KERN_IO_START, 64K reserved area
 *  PHB_IO_BASE = ISA_IO_BASE + 64K to ISA_IO_BASE + 2G, PHB IO spaces
 * IOREMAP_BASE = ISA_IO_BASE + 2G to VMALLOC_START + PGTABLE_RANGE
 */
#define KERN_IO_START	(KERN_VIRT_START + (KERN_VIRT_SIZE >> 1))
#define FULL_IO_SIZE	0x80000000ul
#define  ISA_IO_BASE	(KERN_IO_START)
#define  ISA_IO_END	(KERN_IO_START + 0x10000ul)
#define  PHB_IO_BASE	(ISA_IO_END)
#define  PHB_IO_END	(KERN_IO_START + FULL_IO_SIZE)
#define IOREMAP_BASE	(PHB_IO_END)
#define IOREMAP_END	(KERN_VIRT_START + KERN_VIRT_SIZE)

#define vmemmap			((struct page *)VMEMMAP_BASE)

/* Advertise special mapping type for AGP */
#define HAVE_PAGE_AGP

/* Advertise support for _PAGE_SPECIAL */
#define __HAVE_ARCH_PTE_SPECIAL

#ifndef __ASSEMBLY__

/*
 * This is the default implementation of various PTE accessors, it's
 * used in all cases except Book3S with 64K pages where we have a
 * concept of sub-pages
 */
#ifndef __real_pte

#define __real_pte(e,p)		((real_pte_t){(e)})
#define __rpte_to_pte(r)	((r).pte)
#define __rpte_to_hidx(r,index)	(pte_val(__rpte_to_pte(r)) >>_PAGE_F_GIX_SHIFT)

#define pte_iterate_hashed_subpages(rpte, psize, va, index, shift)       \
	do {							         \
		index = 0;					         \
		shift = mmu_psize_defs[psize].shift;		         \

#define pte_iterate_hashed_end() } while(0)

/*
 * We expect this to be called only for user addresses or kernel virtual
 * addresses other than the linear mapping.
 */
#define pte_pagesize_index(mm, addr, pte)	MMU_PAGE_4K

#endif /* __real_pte */

static inline void pmd_set(pmd_t *pmdp, unsigned long val)
{
	*pmdp = __pmd(val);
}

static inline void pmd_clear(pmd_t *pmdp)
{
	*pmdp = __pmd(0);
}

#define pmd_none(pmd)		(!pmd_val(pmd))
#define	pmd_present(pmd)	(!pmd_none(pmd))

static inline void pud_set(pud_t *pudp, unsigned long val)
{
	*pudp = __pud(val);
}

static inline void pud_clear(pud_t *pudp)
{
	*pudp = __pud(0);
}

#define pud_none(pud)		(!pud_val(pud))
#define pud_present(pud)	(pud_val(pud) != 0)

extern struct page *pud_page(pud_t pud);
extern struct page *pmd_page(pmd_t pmd);
static inline pte_t pud_pte(pud_t pud)
{
	return __pte(pud_val(pud));
}

static inline pud_t pte_pud(pte_t pte)
{
	return __pud(pte_val(pte));
}
#define pud_write(pud)		pte_write(pud_pte(pud))
#define pgd_write(pgd)		pte_write(pgd_pte(pgd))
static inline void pgd_set(pgd_t *pgdp, unsigned long val)
{
	*pgdp = __pgd(val);
}

static inline void pgd_clear(pgd_t *pgdp)
{
	*pgdp = __pgd(0);
}

#define pgd_none(pgd)		(!pgd_val(pgd))
#define pgd_present(pgd)	(!pgd_none(pgd))

static inline pte_t pgd_pte(pgd_t pgd)
{
	return __pte(pgd_val(pgd));
}

static inline pgd_t pte_pgd(pte_t pte)
{
	return __pgd(pte_val(pte));
}

extern struct page *pgd_page(pgd_t pgd);

/*
 * Find an entry in a page-table-directory.  We combine the address region
 * (the high order N bits) and the pgd portion of the address.
 */

#define pgd_offset(mm, address)	 ((mm)->pgd + pgd_index(address))

#define pud_offset(pgdp, addr)	\
	(((pud_t *) pgd_page_vaddr(*(pgdp))) + pud_index(addr))
#define pmd_offset(pudp,addr) \
	(((pmd_t *) pud_page_vaddr(*(pudp))) + pmd_index(addr))
#define pte_offset_kernel(dir,addr) \
	(((pte_t *) pmd_page_vaddr(*(dir))) + pte_index(addr))

#define pte_offset_map(dir,addr)	pte_offset_kernel((dir), (addr))
#define pte_unmap(pte)			do { } while(0)

/* to find an entry in a kernel page-table-directory */
/* This now only contains the vmalloc pages */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

#define pte_ERROR(e) \
	pr_err("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, pte_val(e))
#define pmd_ERROR(e) \
	pr_err("%s:%d: bad pmd %08lx.\n", __FILE__, __LINE__, pmd_val(e))
#define pud_ERROR(e) \
	pr_err("%s:%d: bad pud %08lx.\n", __FILE__, __LINE__, pud_val(e))
#define pgd_ERROR(e) \
	pr_err("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

/* Encode and de-code a swap entry */
#define MAX_SWAPFILES_CHECK() do { \
	BUILD_BUG_ON(MAX_SWAPFILES_SHIFT > SWP_TYPE_BITS); \
	/*							\
	 * Don't have overlapping bits with _PAGE_HPTEFLAGS	\
	 * We filter HPTEFLAGS on set_pte.			\
	 */							\
	BUILD_BUG_ON(_PAGE_HPTEFLAGS & (0x1f << _PAGE_BIT_SWAP_TYPE)); \
	BUILD_BUG_ON(_PAGE_HPTEFLAGS & _PAGE_SWP_SOFT_DIRTY);	\
	} while (0)
/*
 * on pte we don't need handle RADIX_TREE_EXCEPTIONAL_SHIFT;
 */
#define SWP_TYPE_BITS 5
#define __swp_type(x)		(((x).val >> _PAGE_BIT_SWAP_TYPE) \
				& ((1UL << SWP_TYPE_BITS) - 1))
#define __swp_offset(x)		(((x).val & PTE_RPN_MASK) >> PAGE_SHIFT)
#define __swp_entry(type, offset)	((swp_entry_t) { \
				((type) << _PAGE_BIT_SWAP_TYPE) \
				| (((offset) << PAGE_SHIFT) & PTE_RPN_MASK)})
/*
 * swp_entry_t must be independent of pte bits. We build a swp_entry_t from
 * swap type and offset we get from swap and convert that to pte to find a
 * matching pte in linux page table.
 * Clear bits not found in swap entries here.
 */
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val((pte)) & ~_PAGE_PTE })
#define __swp_entry_to_pte(x)	__pte((x).val | _PAGE_PTE)

static inline bool pte_user(pte_t pte)
{
	return !(pte_val(pte) & _PAGE_PRIVILEGED);
}

#ifdef CONFIG_MEM_SOFT_DIRTY
#define _PAGE_SWP_SOFT_DIRTY   (1UL << (SWP_TYPE_BITS + _PAGE_BIT_SWAP_TYPE))
#else
#define _PAGE_SWP_SOFT_DIRTY	0UL
#endif /* CONFIG_MEM_SOFT_DIRTY */

#ifdef CONFIG_HAVE_ARCH_SOFT_DIRTY
static inline pte_t pte_swp_mksoft_dirty(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_SWP_SOFT_DIRTY);
}
static inline bool pte_swp_soft_dirty(pte_t pte)
{
	return !!(pte_val(pte) & _PAGE_SWP_SOFT_DIRTY);
}
static inline pte_t pte_swp_clear_soft_dirty(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_SWP_SOFT_DIRTY);
}
#endif /* CONFIG_HAVE_ARCH_SOFT_DIRTY */

static inline bool check_pte_access(unsigned long access, unsigned long ptev)
{
	/*
	 * This check for _PAGE_RWX and _PAGE_PRESENT bits
	 */
	if (access & ~ptev)
		return false;
	/*
	 * This check for access to privilege space
	 */
	if ((access & _PAGE_PRIVILEGED) != (ptev & _PAGE_PRIVILEGED))
		return false;

	return true;
}

void pgtable_cache_add(unsigned shift, void (*ctor)(void *));
void pgtable_cache_init(void);

struct page *realmode_pfn_to_page(unsigned long pfn);

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
extern pmd_t pfn_pmd(unsigned long pfn, pgprot_t pgprot);
extern pmd_t mk_pmd(struct page *page, pgprot_t pgprot);
extern pmd_t pmd_modify(pmd_t pmd, pgprot_t newprot);
extern void set_pmd_at(struct mm_struct *mm, unsigned long addr,
		       pmd_t *pmdp, pmd_t pmd);
extern void update_mmu_cache_pmd(struct vm_area_struct *vma, unsigned long addr,
				 pmd_t *pmd);
extern int has_transparent_hugepage(void);
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */


static inline pte_t pmd_pte(pmd_t pmd)
{
	return __pte(pmd_val(pmd));
}

static inline pmd_t pte_pmd(pte_t pte)
{
	return __pmd(pte_val(pte));
}

static inline pte_t *pmdp_ptep(pmd_t *pmd)
{
	return (pte_t *)pmd;
}

#define pmd_pfn(pmd)		pte_pfn(pmd_pte(pmd))
#define pmd_dirty(pmd)		pte_dirty(pmd_pte(pmd))
#define pmd_young(pmd)		pte_young(pmd_pte(pmd))
#define pmd_mkold(pmd)		pte_pmd(pte_mkold(pmd_pte(pmd)))
#define pmd_wrprotect(pmd)	pte_pmd(pte_wrprotect(pmd_pte(pmd)))
#define pmd_mkdirty(pmd)	pte_pmd(pte_mkdirty(pmd_pte(pmd)))
#define pmd_mkclean(pmd)	pte_pmd(pte_mkclean(pmd_pte(pmd)))
#define pmd_mkyoung(pmd)	pte_pmd(pte_mkyoung(pmd_pte(pmd)))
#define pmd_mkwrite(pmd)	pte_pmd(pte_mkwrite(pmd_pte(pmd)))

#ifdef CONFIG_HAVE_ARCH_SOFT_DIRTY
#define pmd_soft_dirty(pmd)    pte_soft_dirty(pmd_pte(pmd))
#define pmd_mksoft_dirty(pmd)  pte_pmd(pte_mksoft_dirty(pmd_pte(pmd)))
#define pmd_clear_soft_dirty(pmd) pte_pmd(pte_clear_soft_dirty(pmd_pte(pmd)))
#endif /* CONFIG_HAVE_ARCH_SOFT_DIRTY */

#ifdef CONFIG_NUMA_BALANCING
static inline int pmd_protnone(pmd_t pmd)
{
	return pte_protnone(pmd_pte(pmd));
}
#endif /* CONFIG_NUMA_BALANCING */

#define __HAVE_ARCH_PMD_WRITE
#define pmd_write(pmd)		pte_write(pmd_pte(pmd))

static inline pmd_t pmd_mkhuge(pmd_t pmd)
{
	return __pmd(pmd_val(pmd) | (_PAGE_PTE | _PAGE_THP_HUGE));
}

#define __HAVE_ARCH_PMDP_SET_ACCESS_FLAGS
extern int pmdp_set_access_flags(struct vm_area_struct *vma,
				 unsigned long address, pmd_t *pmdp,
				 pmd_t entry, int dirty);

#define __HAVE_ARCH_PMDP_TEST_AND_CLEAR_YOUNG
extern int pmdp_test_and_clear_young(struct vm_area_struct *vma,
				     unsigned long address, pmd_t *pmdp);
#define __HAVE_ARCH_PMDP_CLEAR_YOUNG_FLUSH
extern int pmdp_clear_flush_young(struct vm_area_struct *vma,
				  unsigned long address, pmd_t *pmdp);

#define __HAVE_ARCH_PMDP_HUGE_GET_AND_CLEAR
extern pmd_t pmdp_huge_get_and_clear(struct mm_struct *mm,
				     unsigned long addr, pmd_t *pmdp);

extern pmd_t pmdp_collapse_flush(struct vm_area_struct *vma,
				 unsigned long address, pmd_t *pmdp);
#define pmdp_collapse_flush pmdp_collapse_flush

#define __HAVE_ARCH_PGTABLE_DEPOSIT
extern void pgtable_trans_huge_deposit(struct mm_struct *mm, pmd_t *pmdp,
				       pgtable_t pgtable);
#define __HAVE_ARCH_PGTABLE_WITHDRAW
extern pgtable_t pgtable_trans_huge_withdraw(struct mm_struct *mm, pmd_t *pmdp);

#define __HAVE_ARCH_PMDP_INVALIDATE
extern void pmdp_invalidate(struct vm_area_struct *vma, unsigned long address,
			    pmd_t *pmdp);

#define __HAVE_ARCH_PMDP_HUGE_SPLIT_PREPARE
extern void pmdp_huge_split_prepare(struct vm_area_struct *vma,
				    unsigned long address, pmd_t *pmdp);

#define pmd_move_must_withdraw pmd_move_must_withdraw
struct spinlock;
static inline int pmd_move_must_withdraw(struct spinlock *new_pmd_ptl,
					 struct spinlock *old_pmd_ptl)
{
	/*
	 * Archs like ppc64 use pgtable to store per pmd
	 * specific information. So when we switch the pmd,
	 * we should also withdraw and deposit the pgtable
	 */
	return true;
}
#endif /* __ASSEMBLY__ */
#endif /* _ASM_POWERPC_BOOK3S_64_PGTABLE_H_ */
