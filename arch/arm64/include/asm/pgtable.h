/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_PGTABLE_H
#define __ASM_PGTABLE_H

#include <asm/proc-fns.h>

#include <asm/memory.h>
#include <asm/pgtable-hwdef.h>

/*
 * Software defined PTE bits definition.
 */
#define PTE_VALID		(_AT(pteval_t, 1) << 0)
#define PTE_FILE		(_AT(pteval_t, 1) << 2)	/* only when !pte_present() */
#define PTE_DIRTY		(_AT(pteval_t, 1) << 55)
#define PTE_SPECIAL		(_AT(pteval_t, 1) << 56)
				/* bit 57 for PMD_SECT_SPLITTING */
#define PTE_PROT_NONE		(_AT(pteval_t, 1) << 58) /* only when !PTE_VALID */

/*
 * VMALLOC and SPARSEMEM_VMEMMAP ranges.
 */
#define VMALLOC_START		(UL(0xffffffffffffffff) << VA_BITS)
#define VMALLOC_END		(PAGE_OFFSET - UL(0x400000000) - SZ_64K)

#define vmemmap			((struct page *)(VMALLOC_END + SZ_64K))

#define FIRST_USER_ADDRESS	0

#ifndef __ASSEMBLY__
extern void __pte_error(const char *file, int line, unsigned long val);
extern void __pmd_error(const char *file, int line, unsigned long val);
extern void __pgd_error(const char *file, int line, unsigned long val);

#define pte_ERROR(pte)		__pte_error(__FILE__, __LINE__, pte_val(pte))
#ifndef CONFIG_ARM64_64K_PAGES
#define pmd_ERROR(pmd)		__pmd_error(__FILE__, __LINE__, pmd_val(pmd))
#endif
#define pgd_ERROR(pgd)		__pgd_error(__FILE__, __LINE__, pgd_val(pgd))

/*
 * The pgprot_* and protection_map entries will be fixed up at runtime to
 * include the cachable and bufferable bits based on memory policy, as well as
 * any architecture dependent bits like global/ASID and SMP shared mapping
 * bits.
 */
#define _PAGE_DEFAULT		PTE_TYPE_PAGE | PTE_AF

extern pgprot_t pgprot_default;

#define __pgprot_modify(prot,mask,bits) \
	__pgprot((pgprot_val(prot) & ~(mask)) | (bits))

#define _MOD_PROT(p, b)		__pgprot_modify(p, 0, b)

#define PAGE_NONE		__pgprot_modify(pgprot_default, PTE_TYPE_MASK, PTE_PROT_NONE | PTE_RDONLY | PTE_PXN | PTE_UXN)
#define PAGE_SHARED		_MOD_PROT(pgprot_default, PTE_USER | PTE_NG | PTE_PXN | PTE_UXN)
#define PAGE_SHARED_EXEC	_MOD_PROT(pgprot_default, PTE_USER | PTE_NG | PTE_PXN)
#define PAGE_COPY		_MOD_PROT(pgprot_default, PTE_USER | PTE_NG | PTE_PXN | PTE_UXN | PTE_RDONLY)
#define PAGE_COPY_EXEC		_MOD_PROT(pgprot_default, PTE_USER | PTE_NG | PTE_PXN | PTE_RDONLY)
#define PAGE_READONLY		_MOD_PROT(pgprot_default, PTE_USER | PTE_NG | PTE_PXN | PTE_UXN | PTE_RDONLY)
#define PAGE_READONLY_EXEC	_MOD_PROT(pgprot_default, PTE_USER | PTE_NG | PTE_PXN | PTE_RDONLY)
#define PAGE_KERNEL		_MOD_PROT(pgprot_default, PTE_PXN | PTE_UXN | PTE_DIRTY)
#define PAGE_KERNEL_EXEC	_MOD_PROT(pgprot_default, PTE_UXN | PTE_DIRTY)

#define PAGE_HYP		_MOD_PROT(pgprot_default, PTE_HYP)
#define PAGE_HYP_DEVICE		__pgprot(PROT_DEVICE_nGnRE | PTE_HYP)

#define PAGE_S2			__pgprot_modify(pgprot_default, PTE_S2_MEMATTR_MASK, PTE_S2_MEMATTR(MT_S2_NORMAL) | PTE_S2_RDONLY)
#define PAGE_S2_DEVICE		__pgprot(PROT_DEFAULT | PTE_S2_MEMATTR(MT_S2_DEVICE_nGnRE) | PTE_S2_RDWR | PTE_UXN)

#define __PAGE_NONE		__pgprot(((_PAGE_DEFAULT) & ~PTE_TYPE_MASK) | PTE_PROT_NONE | PTE_RDONLY | PTE_PXN | PTE_UXN)
#define __PAGE_SHARED		__pgprot(_PAGE_DEFAULT | PTE_USER | PTE_NG | PTE_PXN | PTE_UXN)
#define __PAGE_SHARED_EXEC	__pgprot(_PAGE_DEFAULT | PTE_USER | PTE_NG | PTE_PXN)
#define __PAGE_COPY		__pgprot(_PAGE_DEFAULT | PTE_USER | PTE_NG | PTE_PXN | PTE_UXN | PTE_RDONLY)
#define __PAGE_COPY_EXEC	__pgprot(_PAGE_DEFAULT | PTE_USER | PTE_NG | PTE_PXN | PTE_RDONLY)
#define __PAGE_READONLY		__pgprot(_PAGE_DEFAULT | PTE_USER | PTE_NG | PTE_PXN | PTE_UXN | PTE_RDONLY)
#define __PAGE_READONLY_EXEC	__pgprot(_PAGE_DEFAULT | PTE_USER | PTE_NG | PTE_PXN | PTE_RDONLY)

#endif /* __ASSEMBLY__ */

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
#define ZERO_PAGE(vaddr)	(empty_zero_page)

#define pte_pfn(pte)		((pte_val(pte) & PHYS_MASK) >> PAGE_SHIFT)

#define pfn_pte(pfn,prot)	(__pte(((phys_addr_t)(pfn) << PAGE_SHIFT) | pgprot_val(prot)))

#define pte_none(pte)		(!pte_val(pte))
#define pte_clear(mm,addr,ptep)	set_pte(ptep, __pte(0))
#define pte_page(pte)		(pfn_to_page(pte_pfn(pte)))
#define pte_offset_kernel(dir,addr)	(pmd_page_vaddr(*(dir)) + pte_index(addr))

#define pte_offset_map(dir,addr)	pte_offset_kernel((dir), (addr))
#define pte_offset_map_nested(dir,addr)	pte_offset_kernel((dir), (addr))
#define pte_unmap(pte)			do { } while (0)
#define pte_unmap_nested(pte)		do { } while (0)

/*
 * The following only work if pte_present(). Undefined behaviour otherwise.
 */
#define pte_present(pte)	(pte_val(pte) & (PTE_VALID | PTE_PROT_NONE))
#define pte_dirty(pte)		(pte_val(pte) & PTE_DIRTY)
#define pte_young(pte)		(pte_val(pte) & PTE_AF)
#define pte_special(pte)	(pte_val(pte) & PTE_SPECIAL)
#define pte_write(pte)		(!(pte_val(pte) & PTE_RDONLY))
#define pte_exec(pte)		(!(pte_val(pte) & PTE_UXN))

#define pte_valid_user(pte) \
	((pte_val(pte) & (PTE_VALID | PTE_USER)) == (PTE_VALID | PTE_USER))

#define PTE_BIT_FUNC(fn,op) \
static inline pte_t pte_##fn(pte_t pte) { pte_val(pte) op; return pte; }

PTE_BIT_FUNC(wrprotect, |= PTE_RDONLY);
PTE_BIT_FUNC(mkwrite,   &= ~PTE_RDONLY);
PTE_BIT_FUNC(mkclean,   &= ~PTE_DIRTY);
PTE_BIT_FUNC(mkdirty,   |= PTE_DIRTY);
PTE_BIT_FUNC(mkold,     &= ~PTE_AF);
PTE_BIT_FUNC(mkyoung,   |= PTE_AF);
PTE_BIT_FUNC(mkspecial, |= PTE_SPECIAL);

static inline void set_pte(pte_t *ptep, pte_t pte)
{
	*ptep = pte;
}

extern void __sync_icache_dcache(pte_t pteval, unsigned long addr);

static inline void set_pte_at(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, pte_t pte)
{
	if (pte_valid_user(pte)) {
		if (pte_exec(pte))
			__sync_icache_dcache(pte, addr);
		if (!pte_dirty(pte))
			pte = pte_wrprotect(pte);
	}

	set_pte(ptep, pte);
}

/*
 * Huge pte definitions.
 */
#define pte_huge(pte)		(!(pte_val(pte) & PTE_TABLE_BIT))
#define pte_mkhuge(pte)		(__pte(pte_val(pte) & ~PTE_TABLE_BIT))

/*
 * Hugetlb definitions.
 */
#define HUGE_MAX_HSTATE		2
#define HPAGE_SHIFT		PMD_SHIFT
#define HPAGE_SIZE		(_AC(1, UL) << HPAGE_SHIFT)
#define HPAGE_MASK		(~(HPAGE_SIZE - 1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)

#define __HAVE_ARCH_PTE_SPECIAL

/*
 * Software PMD bits for THP
 */

#define PMD_SECT_DIRTY		(_AT(pmdval_t, 1) << 55)
#define PMD_SECT_SPLITTING	(_AT(pmdval_t, 1) << 57)

/*
 * THP definitions.
 */
#define pmd_young(pmd)		(pmd_val(pmd) & PMD_SECT_AF)

#define __HAVE_ARCH_PMD_WRITE
#define pmd_write(pmd)		(!(pmd_val(pmd) & PMD_SECT_RDONLY))

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
#define pmd_trans_huge(pmd)	(pmd_val(pmd) && !(pmd_val(pmd) & PMD_TABLE_BIT))
#define pmd_trans_splitting(pmd) (pmd_val(pmd) & PMD_SECT_SPLITTING)
#endif

#define PMD_BIT_FUNC(fn,op) \
static inline pmd_t pmd_##fn(pmd_t pmd) { pmd_val(pmd) op; return pmd; }

PMD_BIT_FUNC(wrprotect,	|= PMD_SECT_RDONLY);
PMD_BIT_FUNC(mkold,	&= ~PMD_SECT_AF);
PMD_BIT_FUNC(mksplitting, |= PMD_SECT_SPLITTING);
PMD_BIT_FUNC(mkwrite,   &= ~PMD_SECT_RDONLY);
PMD_BIT_FUNC(mkdirty,   |= PMD_SECT_DIRTY);
PMD_BIT_FUNC(mkyoung,   |= PMD_SECT_AF);
PMD_BIT_FUNC(mknotpresent, &= ~PMD_TYPE_MASK);

#define pmd_mkhuge(pmd)		(__pmd(pmd_val(pmd) & ~PMD_TABLE_BIT))

#define pmd_pfn(pmd)		(((pmd_val(pmd) & PMD_MASK) & PHYS_MASK) >> PAGE_SHIFT)
#define pfn_pmd(pfn,prot)	(__pmd(((phys_addr_t)(pfn) << PAGE_SHIFT) | pgprot_val(prot)))
#define mk_pmd(page,prot)	pfn_pmd(page_to_pfn(page),prot)

#define pmd_page(pmd)           pfn_to_page(__phys_to_pfn(pmd_val(pmd) & PHYS_MASK))

static inline pmd_t pmd_modify(pmd_t pmd, pgprot_t newprot)
{
	const pmdval_t mask = PMD_SECT_USER | PMD_SECT_PXN | PMD_SECT_UXN |
			      PMD_SECT_RDONLY | PMD_SECT_PROT_NONE |
			      PMD_SECT_VALID;
	pmd_val(pmd) = (pmd_val(pmd) & ~mask) | (pgprot_val(newprot) & mask);
	return pmd;
}

#define set_pmd_at(mm, addr, pmdp, pmd)	set_pmd(pmdp, pmd)

static inline int has_transparent_hugepage(void)
{
	return 1;
}

/*
 * Mark the prot value as uncacheable and unbufferable.
 */
#define pgprot_noncached(prot) \
	__pgprot_modify(prot, PTE_ATTRINDX_MASK, PTE_ATTRINDX(MT_DEVICE_nGnRnE))
#define pgprot_writecombine(prot) \
	__pgprot_modify(prot, PTE_ATTRINDX_MASK, PTE_ATTRINDX(MT_NORMAL_NC))
#define pgprot_dmacoherent(prot) \
	__pgprot_modify(prot, PTE_ATTRINDX_MASK, PTE_ATTRINDX(MT_NORMAL_NC))
#define __HAVE_PHYS_MEM_ACCESS_PROT
struct file;
extern pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
				     unsigned long size, pgprot_t vma_prot);

#define pmd_none(pmd)		(!pmd_val(pmd))
#define pmd_present(pmd)	(pmd_val(pmd))

#define pmd_bad(pmd)		(!(pmd_val(pmd) & 2))

#define pmd_table(pmd)		((pmd_val(pmd) & PMD_TYPE_MASK) == \
				 PMD_TYPE_TABLE)
#define pmd_sect(pmd)		((pmd_val(pmd) & PMD_TYPE_MASK) == \
				 PMD_TYPE_SECT)


static inline void set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	*pmdp = pmd;
	dsb();
}

static inline void pmd_clear(pmd_t *pmdp)
{
	set_pmd(pmdp, __pmd(0));
}

static inline pte_t *pmd_page_vaddr(pmd_t pmd)
{
	return __va(pmd_val(pmd) & PHYS_MASK & (s32)PAGE_MASK);
}

#define pmd_page(pmd)		pfn_to_page(__phys_to_pfn(pmd_val(pmd) & PHYS_MASK))

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define mk_pte(page,prot)	pfn_pte(page_to_pfn(page),prot)

#ifndef CONFIG_ARM64_64K_PAGES

#define pud_none(pud)		(!pud_val(pud))
#define pud_bad(pud)		(!(pud_val(pud) & 2))
#define pud_present(pud)	(pud_val(pud))

static inline void set_pud(pud_t *pudp, pud_t pud)
{
	*pudp = pud;
	dsb();
}

static inline void pud_clear(pud_t *pudp)
{
	set_pud(pudp, __pud(0));
}

static inline pmd_t *pud_page_vaddr(pud_t pud)
{
	return __va(pud_val(pud) & PHYS_MASK & (s32)PAGE_MASK);
}

#endif	/* CONFIG_ARM64_64K_PAGES */

/* to find an entry in a page-table-directory */
#define pgd_index(addr)		(((addr) >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1))

#define pgd_offset(mm, addr)	((mm)->pgd+pgd_index(addr))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(addr)	pgd_offset(&init_mm, addr)

/* Find an entry in the second-level page table.. */
#ifndef CONFIG_ARM64_64K_PAGES
#define pmd_index(addr)		(((addr) >> PMD_SHIFT) & (PTRS_PER_PMD - 1))
static inline pmd_t *pmd_offset(pud_t *pud, unsigned long addr)
{
	return (pmd_t *)pud_page_vaddr(*pud) + pmd_index(addr);
}
#endif

/* Find an entry in the third-level page table.. */
#define pte_index(addr)		(((addr) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	const pteval_t mask = PTE_USER | PTE_PXN | PTE_UXN | PTE_RDONLY |
			      PTE_PROT_NONE | PTE_VALID;
	pte_val(pte) = (pte_val(pte) & ~mask) | (pgprot_val(newprot) & mask);
	return pte;
}

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];
extern pgd_t idmap_pg_dir[PTRS_PER_PGD];

#define SWAPPER_DIR_SIZE	(3 * PAGE_SIZE)
#define IDMAP_DIR_SIZE		(2 * PAGE_SIZE)

/*
 * Encode and decode a swap entry:
 *	bits 0-1:	present (must be zero)
 *	bit  2:		PTE_FILE
 *	bits 3-8:	swap type
 *	bits 9-57:	swap offset
 */
#define __SWP_TYPE_SHIFT	3
#define __SWP_TYPE_BITS		6
#define __SWP_OFFSET_BITS	49
#define __SWP_TYPE_MASK		((1 << __SWP_TYPE_BITS) - 1)
#define __SWP_OFFSET_SHIFT	(__SWP_TYPE_BITS + __SWP_TYPE_SHIFT)
#define __SWP_OFFSET_MASK	((1UL << __SWP_OFFSET_BITS) - 1)

#define __swp_type(x)		(((x).val >> __SWP_TYPE_SHIFT) & __SWP_TYPE_MASK)
#define __swp_offset(x)		(((x).val >> __SWP_OFFSET_SHIFT) & __SWP_OFFSET_MASK)
#define __swp_entry(type,offset) ((swp_entry_t) { ((type) << __SWP_TYPE_SHIFT) | ((offset) << __SWP_OFFSET_SHIFT) })

#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(swp)	((pte_t) { (swp).val })

/*
 * Ensure that there are not more swap files than can be encoded in the kernel
 * the PTEs.
 */
#define MAX_SWAPFILES_CHECK() BUILD_BUG_ON(MAX_SWAPFILES_SHIFT > __SWP_TYPE_BITS)

/*
 * Encode and decode a file entry:
 *	bits 0-1:	present (must be zero)
 *	bit  2:		PTE_FILE
 *	bits 3-57:	file offset / PAGE_SIZE
 */
#define pte_file(pte)		(pte_val(pte) & PTE_FILE)
#define pte_to_pgoff(x)		(pte_val(x) >> 3)
#define pgoff_to_pte(x)		__pte(((x) << 3) | PTE_FILE)

#define PTE_FILE_MAX_BITS	55

extern int kern_addr_valid(unsigned long addr);

#include <asm-generic/pgtable.h>

#define pgtable_cache_init() do { } while (0)

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_PGTABLE_H */
