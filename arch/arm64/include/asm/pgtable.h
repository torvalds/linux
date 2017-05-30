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

#include <asm/bug.h>
#include <asm/proc-fns.h>

#include <asm/memory.h>
#include <asm/pgtable-hwdef.h>
#include <asm/pgtable-prot.h>

/*
 * VMALLOC range.
 *
 * VMALLOC_START: beginning of the kernel vmalloc space
 * VMALLOC_END: extends to the available space below vmmemmap, PCI I/O space
 *	and fixed mappings
 */
#define VMALLOC_START		(MODULES_END)
#define VMALLOC_END		(PAGE_OFFSET - PUD_SIZE - VMEMMAP_SIZE - SZ_64K)

#define vmemmap			((struct page *)VMEMMAP_START - (memstart_addr >> PAGE_SHIFT))

#define FIRST_USER_ADDRESS	0UL

#ifndef __ASSEMBLY__

#include <asm/fixmap.h>
#include <linux/mmdebug.h>

extern void __pte_error(const char *file, int line, unsigned long val);
extern void __pmd_error(const char *file, int line, unsigned long val);
extern void __pud_error(const char *file, int line, unsigned long val);
extern void __pgd_error(const char *file, int line, unsigned long val);

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern unsigned long empty_zero_page[PAGE_SIZE / sizeof(unsigned long)];
#define ZERO_PAGE(vaddr)	phys_to_page(__pa_symbol(empty_zero_page))

#define pte_ERROR(pte)		__pte_error(__FILE__, __LINE__, pte_val(pte))

#define pte_pfn(pte)		((pte_val(pte) & PHYS_MASK) >> PAGE_SHIFT)

#define pfn_pte(pfn,prot)	(__pte(((phys_addr_t)(pfn) << PAGE_SHIFT) | pgprot_val(prot)))

#define pte_none(pte)		(!pte_val(pte))
#define pte_clear(mm,addr,ptep)	set_pte(ptep, __pte(0))
#define pte_page(pte)		(pfn_to_page(pte_pfn(pte)))

/*
 * The following only work if pte_present(). Undefined behaviour otherwise.
 */
#define pte_present(pte)	(!!(pte_val(pte) & (PTE_VALID | PTE_PROT_NONE)))
#define pte_young(pte)		(!!(pte_val(pte) & PTE_AF))
#define pte_special(pte)	(!!(pte_val(pte) & PTE_SPECIAL))
#define pte_write(pte)		(!!(pte_val(pte) & PTE_WRITE))
#define pte_user_exec(pte)	(!(pte_val(pte) & PTE_UXN))
#define pte_cont(pte)		(!!(pte_val(pte) & PTE_CONT))

#define pte_cont_addr_end(addr, end)						\
({	unsigned long __boundary = ((addr) + CONT_PTE_SIZE) & CONT_PTE_MASK;	\
	(__boundary - 1 < (end) - 1) ? __boundary : (end);			\
})

#define pmd_cont_addr_end(addr, end)						\
({	unsigned long __boundary = ((addr) + CONT_PMD_SIZE) & CONT_PMD_MASK;	\
	(__boundary - 1 < (end) - 1) ? __boundary : (end);			\
})

#ifdef CONFIG_ARM64_HW_AFDBM
#define pte_hw_dirty(pte)	(pte_write(pte) && !(pte_val(pte) & PTE_RDONLY))
#else
#define pte_hw_dirty(pte)	(0)
#endif
#define pte_sw_dirty(pte)	(!!(pte_val(pte) & PTE_DIRTY))
#define pte_dirty(pte)		(pte_sw_dirty(pte) || pte_hw_dirty(pte))

#define pte_valid(pte)		(!!(pte_val(pte) & PTE_VALID))
/*
 * Execute-only user mappings do not have the PTE_USER bit set. All valid
 * kernel mappings have the PTE_UXN bit set.
 */
#define pte_valid_not_user(pte) \
	((pte_val(pte) & (PTE_VALID | PTE_USER | PTE_UXN)) == (PTE_VALID | PTE_UXN))
#define pte_valid_young(pte) \
	((pte_val(pte) & (PTE_VALID | PTE_AF)) == (PTE_VALID | PTE_AF))

/*
 * Could the pte be present in the TLB? We must check mm_tlb_flush_pending
 * so that we don't erroneously return false for pages that have been
 * remapped as PROT_NONE but are yet to be flushed from the TLB.
 */
#define pte_accessible(mm, pte)	\
	(mm_tlb_flush_pending(mm) ? pte_present(pte) : pte_valid_young(pte))

static inline pte_t clear_pte_bit(pte_t pte, pgprot_t prot)
{
	pte_val(pte) &= ~pgprot_val(prot);
	return pte;
}

static inline pte_t set_pte_bit(pte_t pte, pgprot_t prot)
{
	pte_val(pte) |= pgprot_val(prot);
	return pte;
}

static inline pte_t pte_wrprotect(pte_t pte)
{
	return clear_pte_bit(pte, __pgprot(PTE_WRITE));
}

static inline pte_t pte_mkwrite(pte_t pte)
{
	return set_pte_bit(pte, __pgprot(PTE_WRITE));
}

static inline pte_t pte_mkclean(pte_t pte)
{
	return clear_pte_bit(pte, __pgprot(PTE_DIRTY));
}

static inline pte_t pte_mkdirty(pte_t pte)
{
	return set_pte_bit(pte, __pgprot(PTE_DIRTY));
}

static inline pte_t pte_mkold(pte_t pte)
{
	return clear_pte_bit(pte, __pgprot(PTE_AF));
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	return set_pte_bit(pte, __pgprot(PTE_AF));
}

static inline pte_t pte_mkspecial(pte_t pte)
{
	return set_pte_bit(pte, __pgprot(PTE_SPECIAL));
}

static inline pte_t pte_mkcont(pte_t pte)
{
	pte = set_pte_bit(pte, __pgprot(PTE_CONT));
	return set_pte_bit(pte, __pgprot(PTE_TYPE_PAGE));
}

static inline pte_t pte_mknoncont(pte_t pte)
{
	return clear_pte_bit(pte, __pgprot(PTE_CONT));
}

static inline pte_t pte_clear_rdonly(pte_t pte)
{
	return clear_pte_bit(pte, __pgprot(PTE_RDONLY));
}

static inline pte_t pte_mkpresent(pte_t pte)
{
	return set_pte_bit(pte, __pgprot(PTE_VALID));
}

static inline pmd_t pmd_mkcont(pmd_t pmd)
{
	return __pmd(pmd_val(pmd) | PMD_SECT_CONT);
}

static inline void set_pte(pte_t *ptep, pte_t pte)
{
	*ptep = pte;

	/*
	 * Only if the new pte is valid and kernel, otherwise TLB maintenance
	 * or update_mmu_cache() have the necessary barriers.
	 */
	if (pte_valid_not_user(pte)) {
		dsb(ishst);
		isb();
	}
}

struct mm_struct;
struct vm_area_struct;

extern void __sync_icache_dcache(pte_t pteval, unsigned long addr);

/*
 * PTE bits configuration in the presence of hardware Dirty Bit Management
 * (PTE_WRITE == PTE_DBM):
 *
 * Dirty  Writable | PTE_RDONLY  PTE_WRITE  PTE_DIRTY (sw)
 *   0      0      |   1           0          0
 *   0      1      |   1           1          0
 *   1      0      |   1           0          1
 *   1      1      |   0           1          x
 *
 * When hardware DBM is not present, the sofware PTE_DIRTY bit is updated via
 * the page fault mechanism. Checking the dirty status of a pte becomes:
 *
 *   PTE_DIRTY || (PTE_WRITE && !PTE_RDONLY)
 */
static inline void set_pte_at(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, pte_t pte)
{
	if (pte_present(pte)) {
		if (pte_sw_dirty(pte) && pte_write(pte))
			pte_val(pte) &= ~PTE_RDONLY;
		else
			pte_val(pte) |= PTE_RDONLY;
		if (pte_user_exec(pte) && !pte_special(pte))
			__sync_icache_dcache(pte, addr);
	}

	/*
	 * If the existing pte is valid, check for potential race with
	 * hardware updates of the pte (ptep_set_access_flags safely changes
	 * valid ptes without going through an invalid entry).
	 */
	if (IS_ENABLED(CONFIG_ARM64_HW_AFDBM) &&
	    pte_valid(*ptep) && pte_valid(pte)) {
		VM_WARN_ONCE(!pte_young(pte),
			     "%s: racy access flag clearing: 0x%016llx -> 0x%016llx",
			     __func__, pte_val(*ptep), pte_val(pte));
		VM_WARN_ONCE(pte_write(*ptep) && !pte_dirty(pte),
			     "%s: racy dirty state clearing: 0x%016llx -> 0x%016llx",
			     __func__, pte_val(*ptep), pte_val(pte));
	}

	set_pte(ptep, pte);
}

#define __HAVE_ARCH_PTE_SAME
static inline int pte_same(pte_t pte_a, pte_t pte_b)
{
	pteval_t lhs, rhs;

	lhs = pte_val(pte_a);
	rhs = pte_val(pte_b);

	if (pte_present(pte_a))
		lhs &= ~PTE_RDONLY;

	if (pte_present(pte_b))
		rhs &= ~PTE_RDONLY;

	return (lhs == rhs);
}

/*
 * Huge pte definitions.
 */
#define pte_huge(pte)		(!(pte_val(pte) & PTE_TABLE_BIT))
#define pte_mkhuge(pte)		(__pte(pte_val(pte) & ~PTE_TABLE_BIT))

/*
 * Hugetlb definitions.
 */
#define HUGE_MAX_HSTATE		4
#define HPAGE_SHIFT		PMD_SHIFT
#define HPAGE_SIZE		(_AC(1, UL) << HPAGE_SHIFT)
#define HPAGE_MASK		(~(HPAGE_SIZE - 1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)

#define __HAVE_ARCH_PTE_SPECIAL

static inline pte_t pud_pte(pud_t pud)
{
	return __pte(pud_val(pud));
}

static inline pmd_t pud_pmd(pud_t pud)
{
	return __pmd(pud_val(pud));
}

static inline pte_t pmd_pte(pmd_t pmd)
{
	return __pte(pmd_val(pmd));
}

static inline pmd_t pte_pmd(pte_t pte)
{
	return __pmd(pte_val(pte));
}

static inline pgprot_t mk_sect_prot(pgprot_t prot)
{
	return __pgprot(pgprot_val(prot) & ~PTE_TABLE_BIT);
}

#ifdef CONFIG_NUMA_BALANCING
/*
 * See the comment in include/asm-generic/pgtable.h
 */
static inline int pte_protnone(pte_t pte)
{
	return (pte_val(pte) & (PTE_VALID | PTE_PROT_NONE)) == PTE_PROT_NONE;
}

static inline int pmd_protnone(pmd_t pmd)
{
	return pte_protnone(pmd_pte(pmd));
}
#endif

/*
 * THP definitions.
 */

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
#define pmd_trans_huge(pmd)	(pmd_val(pmd) && !(pmd_val(pmd) & PMD_TABLE_BIT))
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

#define pmd_present(pmd)	pte_present(pmd_pte(pmd))
#define pmd_dirty(pmd)		pte_dirty(pmd_pte(pmd))
#define pmd_young(pmd)		pte_young(pmd_pte(pmd))
#define pmd_wrprotect(pmd)	pte_pmd(pte_wrprotect(pmd_pte(pmd)))
#define pmd_mkold(pmd)		pte_pmd(pte_mkold(pmd_pte(pmd)))
#define pmd_mkwrite(pmd)	pte_pmd(pte_mkwrite(pmd_pte(pmd)))
#define pmd_mkclean(pmd)	pte_pmd(pte_mkclean(pmd_pte(pmd)))
#define pmd_mkdirty(pmd)	pte_pmd(pte_mkdirty(pmd_pte(pmd)))
#define pmd_mkyoung(pmd)	pte_pmd(pte_mkyoung(pmd_pte(pmd)))
#define pmd_mknotpresent(pmd)	(__pmd(pmd_val(pmd) & ~PMD_SECT_VALID))

#define pmd_thp_or_huge(pmd)	(pmd_huge(pmd) || pmd_trans_huge(pmd))

#define __HAVE_ARCH_PMD_WRITE
#define pmd_write(pmd)		pte_write(pmd_pte(pmd))

#define pmd_mkhuge(pmd)		(__pmd(pmd_val(pmd) & ~PMD_TABLE_BIT))

#define pmd_pfn(pmd)		(((pmd_val(pmd) & PMD_MASK) & PHYS_MASK) >> PAGE_SHIFT)
#define pfn_pmd(pfn,prot)	(__pmd(((phys_addr_t)(pfn) << PAGE_SHIFT) | pgprot_val(prot)))
#define mk_pmd(page,prot)	pfn_pmd(page_to_pfn(page),prot)

#define pud_write(pud)		pte_write(pud_pte(pud))
#define pud_pfn(pud)		(((pud_val(pud) & PUD_MASK) & PHYS_MASK) >> PAGE_SHIFT)

#define set_pmd_at(mm, addr, pmdp, pmd)	set_pte_at(mm, addr, (pte_t *)pmdp, pmd_pte(pmd))

#define __pgprot_modify(prot,mask,bits) \
	__pgprot((pgprot_val(prot) & ~(mask)) | (bits))

/*
 * Mark the prot value as uncacheable and unbufferable.
 */
#define pgprot_noncached(prot) \
	__pgprot_modify(prot, PTE_ATTRINDX_MASK, PTE_ATTRINDX(MT_DEVICE_nGnRnE) | PTE_PXN | PTE_UXN)
#define pgprot_writecombine(prot) \
	__pgprot_modify(prot, PTE_ATTRINDX_MASK, PTE_ATTRINDX(MT_NORMAL_NC) | PTE_PXN | PTE_UXN)
#define pgprot_device(prot) \
	__pgprot_modify(prot, PTE_ATTRINDX_MASK, PTE_ATTRINDX(MT_DEVICE_nGnRE) | PTE_PXN | PTE_UXN)
#define __HAVE_PHYS_MEM_ACCESS_PROT
struct file;
extern pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
				     unsigned long size, pgprot_t vma_prot);

#define pmd_none(pmd)		(!pmd_val(pmd))

#define pmd_bad(pmd)		(!(pmd_val(pmd) & PMD_TABLE_BIT))

#define pmd_table(pmd)		((pmd_val(pmd) & PMD_TYPE_MASK) == \
				 PMD_TYPE_TABLE)
#define pmd_sect(pmd)		((pmd_val(pmd) & PMD_TYPE_MASK) == \
				 PMD_TYPE_SECT)

#if defined(CONFIG_ARM64_64K_PAGES) || CONFIG_PGTABLE_LEVELS < 3
#define pud_sect(pud)		(0)
#define pud_table(pud)		(1)
#else
#define pud_sect(pud)		((pud_val(pud) & PUD_TYPE_MASK) == \
				 PUD_TYPE_SECT)
#define pud_table(pud)		((pud_val(pud) & PUD_TYPE_MASK) == \
				 PUD_TYPE_TABLE)
#endif

static inline void set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	*pmdp = pmd;
	dsb(ishst);
	isb();
}

static inline void pmd_clear(pmd_t *pmdp)
{
	set_pmd(pmdp, __pmd(0));
}

static inline phys_addr_t pmd_page_paddr(pmd_t pmd)
{
	return pmd_val(pmd) & PHYS_MASK & (s32)PAGE_MASK;
}

/* Find an entry in the third-level page table. */
#define pte_index(addr)		(((addr) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))

#define pte_offset_phys(dir,addr)	(pmd_page_paddr(*(dir)) + pte_index(addr) * sizeof(pte_t))
#define pte_offset_kernel(dir,addr)	((pte_t *)__va(pte_offset_phys((dir), (addr))))

#define pte_offset_map(dir,addr)	pte_offset_kernel((dir), (addr))
#define pte_offset_map_nested(dir,addr)	pte_offset_kernel((dir), (addr))
#define pte_unmap(pte)			do { } while (0)
#define pte_unmap_nested(pte)		do { } while (0)

#define pte_set_fixmap(addr)		((pte_t *)set_fixmap_offset(FIX_PTE, addr))
#define pte_set_fixmap_offset(pmd, addr)	pte_set_fixmap(pte_offset_phys(pmd, addr))
#define pte_clear_fixmap()		clear_fixmap(FIX_PTE)

#define pmd_page(pmd)		pfn_to_page(__phys_to_pfn(pmd_val(pmd) & PHYS_MASK))

/* use ONLY for statically allocated translation tables */
#define pte_offset_kimg(dir,addr)	((pte_t *)__phys_to_kimg(pte_offset_phys((dir), (addr))))

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define mk_pte(page,prot)	pfn_pte(page_to_pfn(page),prot)

#if CONFIG_PGTABLE_LEVELS > 2

#define pmd_ERROR(pmd)		__pmd_error(__FILE__, __LINE__, pmd_val(pmd))

#define pud_none(pud)		(!pud_val(pud))
#define pud_bad(pud)		(!(pud_val(pud) & PUD_TABLE_BIT))
#define pud_present(pud)	(pud_val(pud))

static inline void set_pud(pud_t *pudp, pud_t pud)
{
	*pudp = pud;
	dsb(ishst);
	isb();
}

static inline void pud_clear(pud_t *pudp)
{
	set_pud(pudp, __pud(0));
}

static inline phys_addr_t pud_page_paddr(pud_t pud)
{
	return pud_val(pud) & PHYS_MASK & (s32)PAGE_MASK;
}

/* Find an entry in the second-level page table. */
#define pmd_index(addr)		(((addr) >> PMD_SHIFT) & (PTRS_PER_PMD - 1))

#define pmd_offset_phys(dir, addr)	(pud_page_paddr(*(dir)) + pmd_index(addr) * sizeof(pmd_t))
#define pmd_offset(dir, addr)		((pmd_t *)__va(pmd_offset_phys((dir), (addr))))

#define pmd_set_fixmap(addr)		((pmd_t *)set_fixmap_offset(FIX_PMD, addr))
#define pmd_set_fixmap_offset(pud, addr)	pmd_set_fixmap(pmd_offset_phys(pud, addr))
#define pmd_clear_fixmap()		clear_fixmap(FIX_PMD)

#define pud_page(pud)		pfn_to_page(__phys_to_pfn(pud_val(pud) & PHYS_MASK))

/* use ONLY for statically allocated translation tables */
#define pmd_offset_kimg(dir,addr)	((pmd_t *)__phys_to_kimg(pmd_offset_phys((dir), (addr))))

#else

#define pud_page_paddr(pud)	({ BUILD_BUG(); 0; })

/* Match pmd_offset folding in <asm/generic/pgtable-nopmd.h> */
#define pmd_set_fixmap(addr)		NULL
#define pmd_set_fixmap_offset(pudp, addr)	((pmd_t *)pudp)
#define pmd_clear_fixmap()

#define pmd_offset_kimg(dir,addr)	((pmd_t *)dir)

#endif	/* CONFIG_PGTABLE_LEVELS > 2 */

#if CONFIG_PGTABLE_LEVELS > 3

#define pud_ERROR(pud)		__pud_error(__FILE__, __LINE__, pud_val(pud))

#define pgd_none(pgd)		(!pgd_val(pgd))
#define pgd_bad(pgd)		(!(pgd_val(pgd) & 2))
#define pgd_present(pgd)	(pgd_val(pgd))

static inline void set_pgd(pgd_t *pgdp, pgd_t pgd)
{
	*pgdp = pgd;
	dsb(ishst);
}

static inline void pgd_clear(pgd_t *pgdp)
{
	set_pgd(pgdp, __pgd(0));
}

static inline phys_addr_t pgd_page_paddr(pgd_t pgd)
{
	return pgd_val(pgd) & PHYS_MASK & (s32)PAGE_MASK;
}

/* Find an entry in the frst-level page table. */
#define pud_index(addr)		(((addr) >> PUD_SHIFT) & (PTRS_PER_PUD - 1))

#define pud_offset_phys(dir, addr)	(pgd_page_paddr(*(dir)) + pud_index(addr) * sizeof(pud_t))
#define pud_offset(dir, addr)		((pud_t *)__va(pud_offset_phys((dir), (addr))))

#define pud_set_fixmap(addr)		((pud_t *)set_fixmap_offset(FIX_PUD, addr))
#define pud_set_fixmap_offset(pgd, addr)	pud_set_fixmap(pud_offset_phys(pgd, addr))
#define pud_clear_fixmap()		clear_fixmap(FIX_PUD)

#define pgd_page(pgd)		pfn_to_page(__phys_to_pfn(pgd_val(pgd) & PHYS_MASK))

/* use ONLY for statically allocated translation tables */
#define pud_offset_kimg(dir,addr)	((pud_t *)__phys_to_kimg(pud_offset_phys((dir), (addr))))

#else

#define pgd_page_paddr(pgd)	({ BUILD_BUG(); 0;})

/* Match pud_offset folding in <asm/generic/pgtable-nopud.h> */
#define pud_set_fixmap(addr)		NULL
#define pud_set_fixmap_offset(pgdp, addr)	((pud_t *)pgdp)
#define pud_clear_fixmap()

#define pud_offset_kimg(dir,addr)	((pud_t *)dir)

#endif  /* CONFIG_PGTABLE_LEVELS > 3 */

#define pgd_ERROR(pgd)		__pgd_error(__FILE__, __LINE__, pgd_val(pgd))

/* to find an entry in a page-table-directory */
#define pgd_index(addr)		(((addr) >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1))

#define pgd_offset_raw(pgd, addr)	((pgd) + pgd_index(addr))

#define pgd_offset(mm, addr)	(pgd_offset_raw((mm)->pgd, (addr)))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(addr)	pgd_offset(&init_mm, addr)

#define pgd_set_fixmap(addr)	((pgd_t *)set_fixmap_offset(FIX_PGD, addr))
#define pgd_clear_fixmap()	clear_fixmap(FIX_PGD)

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	const pteval_t mask = PTE_USER | PTE_PXN | PTE_UXN | PTE_RDONLY |
			      PTE_PROT_NONE | PTE_VALID | PTE_WRITE;
	/* preserve the hardware dirty information */
	if (pte_hw_dirty(pte))
		pte = pte_mkdirty(pte);
	pte_val(pte) = (pte_val(pte) & ~mask) | (pgprot_val(newprot) & mask);
	return pte;
}

static inline pmd_t pmd_modify(pmd_t pmd, pgprot_t newprot)
{
	return pte_pmd(pte_modify(pmd_pte(pmd), newprot));
}

#ifdef CONFIG_ARM64_HW_AFDBM
#define __HAVE_ARCH_PTEP_SET_ACCESS_FLAGS
extern int ptep_set_access_flags(struct vm_area_struct *vma,
				 unsigned long address, pte_t *ptep,
				 pte_t entry, int dirty);

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
#define __HAVE_ARCH_PMDP_SET_ACCESS_FLAGS
static inline int pmdp_set_access_flags(struct vm_area_struct *vma,
					unsigned long address, pmd_t *pmdp,
					pmd_t entry, int dirty)
{
	return ptep_set_access_flags(vma, address, (pte_t *)pmdp, pmd_pte(entry), dirty);
}
#endif

/*
 * Atomic pte/pmd modifications.
 */
#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
static inline int __ptep_test_and_clear_young(pte_t *ptep)
{
	pteval_t pteval;
	unsigned int tmp, res;

	asm volatile("//	__ptep_test_and_clear_young\n"
	"	prfm	pstl1strm, %2\n"
	"1:	ldxr	%0, %2\n"
	"	ubfx	%w3, %w0, %5, #1	// extract PTE_AF (young)\n"
	"	and	%0, %0, %4		// clear PTE_AF\n"
	"	stxr	%w1, %0, %2\n"
	"	cbnz	%w1, 1b\n"
	: "=&r" (pteval), "=&r" (tmp), "+Q" (pte_val(*ptep)), "=&r" (res)
	: "L" (~PTE_AF), "I" (ilog2(PTE_AF)));

	return res;
}

static inline int ptep_test_and_clear_young(struct vm_area_struct *vma,
					    unsigned long address,
					    pte_t *ptep)
{
	return __ptep_test_and_clear_young(ptep);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
#define __HAVE_ARCH_PMDP_TEST_AND_CLEAR_YOUNG
static inline int pmdp_test_and_clear_young(struct vm_area_struct *vma,
					    unsigned long address,
					    pmd_t *pmdp)
{
	return ptep_test_and_clear_young(vma, address, (pte_t *)pmdp);
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
static inline pte_t ptep_get_and_clear(struct mm_struct *mm,
				       unsigned long address, pte_t *ptep)
{
	pteval_t old_pteval;
	unsigned int tmp;

	asm volatile("//	ptep_get_and_clear\n"
	"	prfm	pstl1strm, %2\n"
	"1:	ldxr	%0, %2\n"
	"	stxr	%w1, xzr, %2\n"
	"	cbnz	%w1, 1b\n"
	: "=&r" (old_pteval), "=&r" (tmp), "+Q" (pte_val(*ptep)));

	return __pte(old_pteval);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
#define __HAVE_ARCH_PMDP_HUGE_GET_AND_CLEAR
static inline pmd_t pmdp_huge_get_and_clear(struct mm_struct *mm,
					    unsigned long address, pmd_t *pmdp)
{
	return pte_pmd(ptep_get_and_clear(mm, address, (pte_t *)pmdp));
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

/*
 * ptep_set_wrprotect - mark read-only while trasferring potential hardware
 * dirty status (PTE_DBM && !PTE_RDONLY) to the software PTE_DIRTY bit.
 */
#define __HAVE_ARCH_PTEP_SET_WRPROTECT
static inline void ptep_set_wrprotect(struct mm_struct *mm, unsigned long address, pte_t *ptep)
{
	pteval_t pteval;
	unsigned long tmp;

	asm volatile("//	ptep_set_wrprotect\n"
	"	prfm	pstl1strm, %2\n"
	"1:	ldxr	%0, %2\n"
	"	tst	%0, %4			// check for hw dirty (!PTE_RDONLY)\n"
	"	csel	%1, %3, xzr, eq		// set PTE_DIRTY|PTE_RDONLY if dirty\n"
	"	orr	%0, %0, %1		// if !dirty, PTE_RDONLY is already set\n"
	"	and	%0, %0, %5		// clear PTE_WRITE/PTE_DBM\n"
	"	stxr	%w1, %0, %2\n"
	"	cbnz	%w1, 1b\n"
	: "=&r" (pteval), "=&r" (tmp), "+Q" (pte_val(*ptep))
	: "r" (PTE_DIRTY|PTE_RDONLY), "L" (PTE_RDONLY), "L" (~PTE_WRITE)
	: "cc");
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
#define __HAVE_ARCH_PMDP_SET_WRPROTECT
static inline void pmdp_set_wrprotect(struct mm_struct *mm,
				      unsigned long address, pmd_t *pmdp)
{
	ptep_set_wrprotect(mm, address, (pte_t *)pmdp);
}
#endif
#endif	/* CONFIG_ARM64_HW_AFDBM */

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];
extern pgd_t idmap_pg_dir[PTRS_PER_PGD];

/*
 * Encode and decode a swap entry:
 *	bits 0-1:	present (must be zero)
 *	bits 2-7:	swap type
 *	bits 8-57:	swap offset
 *	bit  58:	PTE_PROT_NONE (must be zero)
 */
#define __SWP_TYPE_SHIFT	2
#define __SWP_TYPE_BITS		6
#define __SWP_OFFSET_BITS	50
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
 * PTEs.
 */
#define MAX_SWAPFILES_CHECK() BUILD_BUG_ON(MAX_SWAPFILES_SHIFT > __SWP_TYPE_BITS)

extern int kern_addr_valid(unsigned long addr);

#include <asm-generic/pgtable.h>

void pgd_cache_init(void);
#define pgtable_cache_init	pgd_cache_init

/*
 * On AArch64, the cache coherency is handled via the set_pte_at() function.
 */
static inline void update_mmu_cache(struct vm_area_struct *vma,
				    unsigned long addr, pte_t *ptep)
{
	/*
	 * We don't do anything here, so there's a very small chance of
	 * us retaking a user fault which we just fixed up. The alternative
	 * is doing a dsb(ishst), but that penalises the fastpath.
	 */
}

#define update_mmu_cache_pmd(vma, address, pmd) do { } while (0)

#define kc_vaddr_to_offset(v)	((v) & ~VA_START)
#define kc_offset_to_vaddr(o)	((o) | VA_START)

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_PGTABLE_H */
