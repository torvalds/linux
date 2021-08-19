/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_PGTABLE_H
#define __ASM_PGTABLE_H

#include <asm/bug.h>
#include <asm/proc-fns.h>

#include <asm/memory.h>
#include <asm/mte.h>
#include <asm/pgtable-hwdef.h>
#include <asm/pgtable-prot.h>
#include <asm/tlbflush.h>

/*
 * VMALLOC range.
 *
 * VMALLOC_START: beginning of the kernel vmalloc space
 * VMALLOC_END: extends to the available space below vmemmap, PCI I/O space
 *	and fixed mappings
 */
#define VMALLOC_START		(MODULES_END)
#define VMALLOC_END		(VMEMMAP_START - SZ_256M)

#define vmemmap			((struct page *)VMEMMAP_START - (memstart_addr >> PAGE_SHIFT))

#define FIRST_USER_ADDRESS	0UL

#ifndef __ASSEMBLY__

#include <asm/cmpxchg.h>
#include <asm/fixmap.h>
#include <linux/mmdebug.h>
#include <linux/mm_types.h>
#include <linux/sched.h>

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
#define __HAVE_ARCH_FLUSH_PMD_TLB_RANGE

/* Set stride and tlb_level in flush_*_tlb_range */
#define flush_pmd_tlb_range(vma, addr, end)	\
	__flush_tlb_range(vma, addr, end, PMD_SIZE, false, 2)
#define flush_pud_tlb_range(vma, addr, end)	\
	__flush_tlb_range(vma, addr, end, PUD_SIZE, false, 1)
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

/*
 * Outside of a few very special situations (e.g. hibernation), we always
 * use broadcast TLB invalidation instructions, therefore a spurious page
 * fault on one CPU which has been handled concurrently by another CPU
 * does not need to perform additional invalidation.
 */
#define flush_tlb_fix_spurious_fault(vma, address) do { } while (0)

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern unsigned long empty_zero_page[PAGE_SIZE / sizeof(unsigned long)];
#define ZERO_PAGE(vaddr)	phys_to_page(__pa_symbol(empty_zero_page))

#define pte_ERROR(e)	\
	pr_err("%s:%d: bad pte %016llx.\n", __FILE__, __LINE__, pte_val(e))

/*
 * Macros to convert between a physical address and its placement in a
 * page table entry, taking care of 52-bit addresses.
 */
#ifdef CONFIG_ARM64_PA_BITS_52
#define __pte_to_phys(pte)	\
	((pte_val(pte) & PTE_ADDR_LOW) | ((pte_val(pte) & PTE_ADDR_HIGH) << 36))
#define __phys_to_pte_val(phys)	(((phys) | ((phys) >> 36)) & PTE_ADDR_MASK)
#else
#define __pte_to_phys(pte)	(pte_val(pte) & PTE_ADDR_MASK)
#define __phys_to_pte_val(phys)	(phys)
#endif

#define pte_pfn(pte)		(__pte_to_phys(pte) >> PAGE_SHIFT)
#define pfn_pte(pfn,prot)	\
	__pte(__phys_to_pte_val((phys_addr_t)(pfn) << PAGE_SHIFT) | pgprot_val(prot))

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
#define pte_devmap(pte)		(!!(pte_val(pte) & PTE_DEVMAP))
#define pte_tagged(pte)		((pte_val(pte) & PTE_ATTRINDX_MASK) == \
				 PTE_ATTRINDX(MT_NORMAL_TAGGED))

#define pte_cont_addr_end(addr, end)						\
({	unsigned long __boundary = ((addr) + CONT_PTE_SIZE) & CONT_PTE_MASK;	\
	(__boundary - 1 < (end) - 1) ? __boundary : (end);			\
})

#define pmd_cont_addr_end(addr, end)						\
({	unsigned long __boundary = ((addr) + CONT_PMD_SIZE) & CONT_PMD_MASK;	\
	(__boundary - 1 < (end) - 1) ? __boundary : (end);			\
})

#define pte_hw_dirty(pte)	(pte_write(pte) && !(pte_val(pte) & PTE_RDONLY))
#define pte_sw_dirty(pte)	(!!(pte_val(pte) & PTE_DIRTY))
#define pte_dirty(pte)		(pte_sw_dirty(pte) || pte_hw_dirty(pte))

#define pte_valid(pte)		(!!(pte_val(pte) & PTE_VALID))
/*
 * Execute-only user mappings do not have the PTE_USER bit set. All valid
 * kernel mappings have the PTE_UXN bit set.
 */
#define pte_valid_not_user(pte) \
	((pte_val(pte) & (PTE_VALID | PTE_USER | PTE_UXN)) == (PTE_VALID | PTE_UXN))
/*
 * Could the pte be present in the TLB? We must check mm_tlb_flush_pending
 * so that we don't erroneously return false for pages that have been
 * remapped as PROT_NONE but are yet to be flushed from the TLB.
 * Note that we can't make any assumptions based on the state of the access
 * flag, since ptep_clear_flush_young() elides a DSB when invalidating the
 * TLB.
 */
#define pte_accessible(mm, pte)	\
	(mm_tlb_flush_pending(mm) ? pte_present(pte) : pte_valid(pte))

/*
 * p??_access_permitted() is true for valid user mappings (PTE_USER
 * bit set, subject to the write permission check). For execute-only
 * mappings, like PROT_EXEC with EPAN (both PTE_USER and PTE_UXN bits
 * not set) must return false. PROT_NONE mappings do not have the
 * PTE_VALID bit set.
 */
#define pte_access_permitted(pte, write) \
	(((pte_val(pte) & (PTE_VALID | PTE_USER)) == (PTE_VALID | PTE_USER)) && (!(write) || pte_write(pte)))
#define pmd_access_permitted(pmd, write) \
	(pte_access_permitted(pmd_pte(pmd), (write)))
#define pud_access_permitted(pud, write) \
	(pte_access_permitted(pud_pte(pud), (write)))

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

static inline pmd_t clear_pmd_bit(pmd_t pmd, pgprot_t prot)
{
	pmd_val(pmd) &= ~pgprot_val(prot);
	return pmd;
}

static inline pmd_t set_pmd_bit(pmd_t pmd, pgprot_t prot)
{
	pmd_val(pmd) |= pgprot_val(prot);
	return pmd;
}

static inline pte_t pte_mkwrite(pte_t pte)
{
	pte = set_pte_bit(pte, __pgprot(PTE_WRITE));
	pte = clear_pte_bit(pte, __pgprot(PTE_RDONLY));
	return pte;
}

static inline pte_t pte_mkclean(pte_t pte)
{
	pte = clear_pte_bit(pte, __pgprot(PTE_DIRTY));
	pte = set_pte_bit(pte, __pgprot(PTE_RDONLY));

	return pte;
}

static inline pte_t pte_mkdirty(pte_t pte)
{
	pte = set_pte_bit(pte, __pgprot(PTE_DIRTY));

	if (pte_write(pte))
		pte = clear_pte_bit(pte, __pgprot(PTE_RDONLY));

	return pte;
}

static inline pte_t pte_wrprotect(pte_t pte)
{
	/*
	 * If hardware-dirty (PTE_WRITE/DBM bit set and PTE_RDONLY
	 * clear), set the PTE_DIRTY bit.
	 */
	if (pte_hw_dirty(pte))
		pte = pte_mkdirty(pte);

	pte = clear_pte_bit(pte, __pgprot(PTE_WRITE));
	pte = set_pte_bit(pte, __pgprot(PTE_RDONLY));
	return pte;
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

static inline pte_t pte_mkpresent(pte_t pte)
{
	return set_pte_bit(pte, __pgprot(PTE_VALID));
}

static inline pmd_t pmd_mkcont(pmd_t pmd)
{
	return __pmd(pmd_val(pmd) | PMD_SECT_CONT);
}

static inline pte_t pte_mkdevmap(pte_t pte)
{
	return set_pte_bit(pte, __pgprot(PTE_DEVMAP | PTE_SPECIAL));
}

static inline void set_pte(pte_t *ptep, pte_t pte)
{
	WRITE_ONCE(*ptep, pte);

	/*
	 * Only if the new pte is valid and kernel, otherwise TLB maintenance
	 * or update_mmu_cache() have the necessary barriers.
	 */
	if (pte_valid_not_user(pte)) {
		dsb(ishst);
		isb();
	}
}

extern void __sync_icache_dcache(pte_t pteval);

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

static inline void __check_racy_pte_update(struct mm_struct *mm, pte_t *ptep,
					   pte_t pte)
{
	pte_t old_pte;

	if (!IS_ENABLED(CONFIG_DEBUG_VM))
		return;

	old_pte = READ_ONCE(*ptep);

	if (!pte_valid(old_pte) || !pte_valid(pte))
		return;
	if (mm != current->active_mm && atomic_read(&mm->mm_users) <= 1)
		return;

	/*
	 * Check for potential race with hardware updates of the pte
	 * (ptep_set_access_flags safely changes valid ptes without going
	 * through an invalid entry).
	 */
	VM_WARN_ONCE(!pte_young(pte),
		     "%s: racy access flag clearing: 0x%016llx -> 0x%016llx",
		     __func__, pte_val(old_pte), pte_val(pte));
	VM_WARN_ONCE(pte_write(old_pte) && !pte_dirty(pte),
		     "%s: racy dirty state clearing: 0x%016llx -> 0x%016llx",
		     __func__, pte_val(old_pte), pte_val(pte));
}

static inline void set_pte_at(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, pte_t pte)
{
	if (pte_present(pte) && pte_user_exec(pte) && !pte_special(pte))
		__sync_icache_dcache(pte);

	if (system_supports_mte() &&
	    pte_present(pte) && pte_tagged(pte) && !pte_special(pte))
		mte_sync_tags(ptep, pte);

	__check_racy_pte_update(mm, ptep, pte);

	set_pte(ptep, pte);
}

/*
 * Huge pte definitions.
 */
#define pte_mkhuge(pte)		(__pte(pte_val(pte) & ~PTE_TABLE_BIT))

/*
 * Hugetlb definitions.
 */
#define HUGE_MAX_HSTATE		4
#define HPAGE_SHIFT		PMD_SHIFT
#define HPAGE_SIZE		(_AC(1, UL) << HPAGE_SHIFT)
#define HPAGE_MASK		(~(HPAGE_SIZE - 1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)

static inline pte_t pgd_pte(pgd_t pgd)
{
	return __pte(pgd_val(pgd));
}

static inline pte_t p4d_pte(p4d_t p4d)
{
	return __pte(p4d_val(p4d));
}

static inline pte_t pud_pte(pud_t pud)
{
	return __pte(pud_val(pud));
}

static inline pud_t pte_pud(pte_t pte)
{
	return __pud(pte_val(pte));
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

static inline pgprot_t mk_pud_sect_prot(pgprot_t prot)
{
	return __pgprot((pgprot_val(prot) & ~PUD_TABLE_BIT) | PUD_TYPE_SECT);
}

static inline pgprot_t mk_pmd_sect_prot(pgprot_t prot)
{
	return __pgprot((pgprot_val(prot) & ~PMD_TABLE_BIT) | PMD_TYPE_SECT);
}

#ifdef CONFIG_NUMA_BALANCING
/*
 * See the comment in include/linux/pgtable.h
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

#define pmd_present_invalid(pmd)     (!!(pmd_val(pmd) & PMD_PRESENT_INVALID))

static inline int pmd_present(pmd_t pmd)
{
	return pte_present(pmd_pte(pmd)) || pmd_present_invalid(pmd);
}

/*
 * THP definitions.
 */

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static inline int pmd_trans_huge(pmd_t pmd)
{
	return pmd_val(pmd) && pmd_present(pmd) && !(pmd_val(pmd) & PMD_TABLE_BIT);
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

#define pmd_dirty(pmd)		pte_dirty(pmd_pte(pmd))
#define pmd_young(pmd)		pte_young(pmd_pte(pmd))
#define pmd_valid(pmd)		pte_valid(pmd_pte(pmd))
#define pmd_cont(pmd)		pte_cont(pmd_pte(pmd))
#define pmd_wrprotect(pmd)	pte_pmd(pte_wrprotect(pmd_pte(pmd)))
#define pmd_mkold(pmd)		pte_pmd(pte_mkold(pmd_pte(pmd)))
#define pmd_mkwrite(pmd)	pte_pmd(pte_mkwrite(pmd_pte(pmd)))
#define pmd_mkclean(pmd)	pte_pmd(pte_mkclean(pmd_pte(pmd)))
#define pmd_mkdirty(pmd)	pte_pmd(pte_mkdirty(pmd_pte(pmd)))
#define pmd_mkyoung(pmd)	pte_pmd(pte_mkyoung(pmd_pte(pmd)))

static inline pmd_t pmd_mkinvalid(pmd_t pmd)
{
	pmd = set_pmd_bit(pmd, __pgprot(PMD_PRESENT_INVALID));
	pmd = clear_pmd_bit(pmd, __pgprot(PMD_SECT_VALID));

	return pmd;
}

#define pmd_thp_or_huge(pmd)	(pmd_huge(pmd) || pmd_trans_huge(pmd))

#define pmd_write(pmd)		pte_write(pmd_pte(pmd))

#define pmd_mkhuge(pmd)		(__pmd(pmd_val(pmd) & ~PMD_TABLE_BIT))

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
#define pmd_devmap(pmd)		pte_devmap(pmd_pte(pmd))
#endif
static inline pmd_t pmd_mkdevmap(pmd_t pmd)
{
	return pte_pmd(set_pte_bit(pmd_pte(pmd), __pgprot(PTE_DEVMAP)));
}

#define __pmd_to_phys(pmd)	__pte_to_phys(pmd_pte(pmd))
#define __phys_to_pmd_val(phys)	__phys_to_pte_val(phys)
#define pmd_pfn(pmd)		((__pmd_to_phys(pmd) & PMD_MASK) >> PAGE_SHIFT)
#define pfn_pmd(pfn,prot)	__pmd(__phys_to_pmd_val((phys_addr_t)(pfn) << PAGE_SHIFT) | pgprot_val(prot))
#define mk_pmd(page,prot)	pfn_pmd(page_to_pfn(page),prot)

#define pud_young(pud)		pte_young(pud_pte(pud))
#define pud_mkyoung(pud)	pte_pud(pte_mkyoung(pud_pte(pud)))
#define pud_write(pud)		pte_write(pud_pte(pud))

#define pud_mkhuge(pud)		(__pud(pud_val(pud) & ~PUD_TABLE_BIT))

#define __pud_to_phys(pud)	__pte_to_phys(pud_pte(pud))
#define __phys_to_pud_val(phys)	__phys_to_pte_val(phys)
#define pud_pfn(pud)		((__pud_to_phys(pud) & PUD_MASK) >> PAGE_SHIFT)
#define pfn_pud(pfn,prot)	__pud(__phys_to_pud_val((phys_addr_t)(pfn) << PAGE_SHIFT) | pgprot_val(prot))

#define set_pmd_at(mm, addr, pmdp, pmd)	set_pte_at(mm, addr, (pte_t *)pmdp, pmd_pte(pmd))
#define set_pud_at(mm, addr, pudp, pud)	set_pte_at(mm, addr, (pte_t *)pudp, pud_pte(pud))

#define __p4d_to_phys(p4d)	__pte_to_phys(p4d_pte(p4d))
#define __phys_to_p4d_val(phys)	__phys_to_pte_val(phys)

#define __pgd_to_phys(pgd)	__pte_to_phys(pgd_pte(pgd))
#define __phys_to_pgd_val(phys)	__phys_to_pte_val(phys)

#define __pgprot_modify(prot,mask,bits) \
	__pgprot((pgprot_val(prot) & ~(mask)) | (bits))

#define pgprot_nx(prot) \
	__pgprot_modify(prot, PTE_MAYBE_GP, PTE_PXN)

/*
 * Mark the prot value as uncacheable and unbufferable.
 */
#define pgprot_noncached(prot) \
	__pgprot_modify(prot, PTE_ATTRINDX_MASK, PTE_ATTRINDX(MT_DEVICE_nGnRnE) | PTE_PXN | PTE_UXN)
#define pgprot_writecombine(prot) \
	__pgprot_modify(prot, PTE_ATTRINDX_MASK, PTE_ATTRINDX(MT_NORMAL_NC) | PTE_PXN | PTE_UXN)
#define pgprot_device(prot) \
	__pgprot_modify(prot, PTE_ATTRINDX_MASK, PTE_ATTRINDX(MT_DEVICE_nGnRE) | PTE_PXN | PTE_UXN)
#define pgprot_tagged(prot) \
	__pgprot_modify(prot, PTE_ATTRINDX_MASK, PTE_ATTRINDX(MT_NORMAL_TAGGED))
#define pgprot_mhp	pgprot_tagged
/*
 * DMA allocations for non-coherent devices use what the Arm architecture calls
 * "Normal non-cacheable" memory, which permits speculation, unaligned accesses
 * and merging of writes.  This is different from "Device-nGnR[nE]" memory which
 * is intended for MMIO and thus forbids speculation, preserves access size,
 * requires strict alignment and can also force write responses to come from the
 * endpoint.
 */
#define pgprot_dmacoherent(prot) \
	__pgprot_modify(prot, PTE_ATTRINDX_MASK, \
			PTE_ATTRINDX(MT_NORMAL_NC) | PTE_PXN | PTE_UXN)

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
#define pmd_leaf(pmd)		pmd_sect(pmd)

#define pmd_leaf_size(pmd)	(pmd_cont(pmd) ? CONT_PMD_SIZE : PMD_SIZE)
#define pte_leaf_size(pte)	(pte_cont(pte) ? CONT_PTE_SIZE : PAGE_SIZE)

#if defined(CONFIG_ARM64_64K_PAGES) || CONFIG_PGTABLE_LEVELS < 3
static inline bool pud_sect(pud_t pud) { return false; }
static inline bool pud_table(pud_t pud) { return true; }
#else
#define pud_sect(pud)		((pud_val(pud) & PUD_TYPE_MASK) == \
				 PUD_TYPE_SECT)
#define pud_table(pud)		((pud_val(pud) & PUD_TYPE_MASK) == \
				 PUD_TYPE_TABLE)
#endif

extern pgd_t init_pg_dir[PTRS_PER_PGD];
extern pgd_t init_pg_end[];
extern pgd_t swapper_pg_dir[PTRS_PER_PGD];
extern pgd_t idmap_pg_dir[PTRS_PER_PGD];
extern pgd_t idmap_pg_end[];
extern pgd_t tramp_pg_dir[PTRS_PER_PGD];
extern pgd_t reserved_pg_dir[PTRS_PER_PGD];

extern void set_swapper_pgd(pgd_t *pgdp, pgd_t pgd);

static inline bool in_swapper_pgdir(void *addr)
{
	return ((unsigned long)addr & PAGE_MASK) ==
	        ((unsigned long)swapper_pg_dir & PAGE_MASK);
}

static inline void set_pmd(pmd_t *pmdp, pmd_t pmd)
{
#ifdef __PAGETABLE_PMD_FOLDED
	if (in_swapper_pgdir(pmdp)) {
		set_swapper_pgd((pgd_t *)pmdp, __pgd(pmd_val(pmd)));
		return;
	}
#endif /* __PAGETABLE_PMD_FOLDED */

	WRITE_ONCE(*pmdp, pmd);

	if (pmd_valid(pmd)) {
		dsb(ishst);
		isb();
	}
}

static inline void pmd_clear(pmd_t *pmdp)
{
	set_pmd(pmdp, __pmd(0));
}

static inline phys_addr_t pmd_page_paddr(pmd_t pmd)
{
	return __pmd_to_phys(pmd);
}

static inline unsigned long pmd_page_vaddr(pmd_t pmd)
{
	return (unsigned long)__va(pmd_page_paddr(pmd));
}

/* Find an entry in the third-level page table. */
#define pte_offset_phys(dir,addr)	(pmd_page_paddr(READ_ONCE(*(dir))) + pte_index(addr) * sizeof(pte_t))

#define pte_set_fixmap(addr)		((pte_t *)set_fixmap_offset(FIX_PTE, addr))
#define pte_set_fixmap_offset(pmd, addr)	pte_set_fixmap(pte_offset_phys(pmd, addr))
#define pte_clear_fixmap()		clear_fixmap(FIX_PTE)

#define pmd_page(pmd)			phys_to_page(__pmd_to_phys(pmd))

/* use ONLY for statically allocated translation tables */
#define pte_offset_kimg(dir,addr)	((pte_t *)__phys_to_kimg(pte_offset_phys((dir), (addr))))

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define mk_pte(page,prot)	pfn_pte(page_to_pfn(page),prot)

#if CONFIG_PGTABLE_LEVELS > 2

#define pmd_ERROR(e)	\
	pr_err("%s:%d: bad pmd %016llx.\n", __FILE__, __LINE__, pmd_val(e))

#define pud_none(pud)		(!pud_val(pud))
#define pud_bad(pud)		(!(pud_val(pud) & PUD_TABLE_BIT))
#define pud_present(pud)	pte_present(pud_pte(pud))
#define pud_leaf(pud)		pud_sect(pud)
#define pud_valid(pud)		pte_valid(pud_pte(pud))

static inline void set_pud(pud_t *pudp, pud_t pud)
{
#ifdef __PAGETABLE_PUD_FOLDED
	if (in_swapper_pgdir(pudp)) {
		set_swapper_pgd((pgd_t *)pudp, __pgd(pud_val(pud)));
		return;
	}
#endif /* __PAGETABLE_PUD_FOLDED */

	WRITE_ONCE(*pudp, pud);

	if (pud_valid(pud)) {
		dsb(ishst);
		isb();
	}
}

static inline void pud_clear(pud_t *pudp)
{
	set_pud(pudp, __pud(0));
}

static inline phys_addr_t pud_page_paddr(pud_t pud)
{
	return __pud_to_phys(pud);
}

static inline unsigned long pud_page_vaddr(pud_t pud)
{
	return (unsigned long)__va(pud_page_paddr(pud));
}

/* Find an entry in the second-level page table. */
#define pmd_offset_phys(dir, addr)	(pud_page_paddr(READ_ONCE(*(dir))) + pmd_index(addr) * sizeof(pmd_t))

#define pmd_set_fixmap(addr)		((pmd_t *)set_fixmap_offset(FIX_PMD, addr))
#define pmd_set_fixmap_offset(pud, addr)	pmd_set_fixmap(pmd_offset_phys(pud, addr))
#define pmd_clear_fixmap()		clear_fixmap(FIX_PMD)

#define pud_page(pud)			phys_to_page(__pud_to_phys(pud))

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

#define pud_ERROR(e)	\
	pr_err("%s:%d: bad pud %016llx.\n", __FILE__, __LINE__, pud_val(e))

#define p4d_none(p4d)		(!p4d_val(p4d))
#define p4d_bad(p4d)		(!(p4d_val(p4d) & 2))
#define p4d_present(p4d)	(p4d_val(p4d))

static inline void set_p4d(p4d_t *p4dp, p4d_t p4d)
{
	if (in_swapper_pgdir(p4dp)) {
		set_swapper_pgd((pgd_t *)p4dp, __pgd(p4d_val(p4d)));
		return;
	}

	WRITE_ONCE(*p4dp, p4d);
	dsb(ishst);
	isb();
}

static inline void p4d_clear(p4d_t *p4dp)
{
	set_p4d(p4dp, __p4d(0));
}

static inline phys_addr_t p4d_page_paddr(p4d_t p4d)
{
	return __p4d_to_phys(p4d);
}

static inline unsigned long p4d_page_vaddr(p4d_t p4d)
{
	return (unsigned long)__va(p4d_page_paddr(p4d));
}

/* Find an entry in the frst-level page table. */
#define pud_offset_phys(dir, addr)	(p4d_page_paddr(READ_ONCE(*(dir))) + pud_index(addr) * sizeof(pud_t))

#define pud_set_fixmap(addr)		((pud_t *)set_fixmap_offset(FIX_PUD, addr))
#define pud_set_fixmap_offset(p4d, addr)	pud_set_fixmap(pud_offset_phys(p4d, addr))
#define pud_clear_fixmap()		clear_fixmap(FIX_PUD)

#define p4d_page(p4d)		pfn_to_page(__phys_to_pfn(__p4d_to_phys(p4d)))

/* use ONLY for statically allocated translation tables */
#define pud_offset_kimg(dir,addr)	((pud_t *)__phys_to_kimg(pud_offset_phys((dir), (addr))))

#else

#define p4d_page_paddr(p4d)	({ BUILD_BUG(); 0;})
#define pgd_page_paddr(pgd)	({ BUILD_BUG(); 0;})

/* Match pud_offset folding in <asm/generic/pgtable-nopud.h> */
#define pud_set_fixmap(addr)		NULL
#define pud_set_fixmap_offset(pgdp, addr)	((pud_t *)pgdp)
#define pud_clear_fixmap()

#define pud_offset_kimg(dir,addr)	((pud_t *)dir)

#endif  /* CONFIG_PGTABLE_LEVELS > 3 */

#define pgd_ERROR(e)	\
	pr_err("%s:%d: bad pgd %016llx.\n", __FILE__, __LINE__, pgd_val(e))

#define pgd_set_fixmap(addr)	((pgd_t *)set_fixmap_offset(FIX_PGD, addr))
#define pgd_clear_fixmap()	clear_fixmap(FIX_PGD)

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	/*
	 * Normal and Normal-Tagged are two different memory types and indices
	 * in MAIR_EL1. The mask below has to include PTE_ATTRINDX_MASK.
	 */
	const pteval_t mask = PTE_USER | PTE_PXN | PTE_UXN | PTE_RDONLY |
			      PTE_PROT_NONE | PTE_VALID | PTE_WRITE | PTE_GP |
			      PTE_ATTRINDX_MASK;
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

static inline int pud_devmap(pud_t pud)
{
	return 0;
}

static inline int pgd_devmap(pgd_t pgd)
{
	return 0;
}
#endif

/*
 * Atomic pte/pmd modifications.
 */
#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
static inline int __ptep_test_and_clear_young(pte_t *ptep)
{
	pte_t old_pte, pte;

	pte = READ_ONCE(*ptep);
	do {
		old_pte = pte;
		pte = pte_mkold(pte);
		pte_val(pte) = cmpxchg_relaxed(&pte_val(*ptep),
					       pte_val(old_pte), pte_val(pte));
	} while (pte_val(pte) != pte_val(old_pte));

	return pte_young(pte);
}

static inline int ptep_test_and_clear_young(struct vm_area_struct *vma,
					    unsigned long address,
					    pte_t *ptep)
{
	return __ptep_test_and_clear_young(ptep);
}

#define __HAVE_ARCH_PTEP_CLEAR_YOUNG_FLUSH
static inline int ptep_clear_flush_young(struct vm_area_struct *vma,
					 unsigned long address, pte_t *ptep)
{
	int young = ptep_test_and_clear_young(vma, address, ptep);

	if (young) {
		/*
		 * We can elide the trailing DSB here since the worst that can
		 * happen is that a CPU continues to use the young entry in its
		 * TLB and we mistakenly reclaim the associated page. The
		 * window for such an event is bounded by the next
		 * context-switch, which provides a DSB to complete the TLB
		 * invalidation.
		 */
		flush_tlb_page_nosync(vma, address);
	}

	return young;
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
	return __pte(xchg_relaxed(&pte_val(*ptep), 0));
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
	pte_t old_pte, pte;

	pte = READ_ONCE(*ptep);
	do {
		old_pte = pte;
		pte = pte_wrprotect(pte);
		pte_val(pte) = cmpxchg_relaxed(&pte_val(*ptep),
					       pte_val(old_pte), pte_val(pte));
	} while (pte_val(pte) != pte_val(old_pte));
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
#define __HAVE_ARCH_PMDP_SET_WRPROTECT
static inline void pmdp_set_wrprotect(struct mm_struct *mm,
				      unsigned long address, pmd_t *pmdp)
{
	ptep_set_wrprotect(mm, address, (pte_t *)pmdp);
}

#define pmdp_establish pmdp_establish
static inline pmd_t pmdp_establish(struct vm_area_struct *vma,
		unsigned long address, pmd_t *pmdp, pmd_t pmd)
{
	return __pmd(xchg_relaxed(&pmd_val(*pmdp), pmd_val(pmd)));
}
#endif

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

#ifdef CONFIG_ARCH_ENABLE_THP_MIGRATION
#define __pmd_to_swp_entry(pmd)		((swp_entry_t) { pmd_val(pmd) })
#define __swp_entry_to_pmd(swp)		__pmd((swp).val)
#endif /* CONFIG_ARCH_ENABLE_THP_MIGRATION */

/*
 * Ensure that there are not more swap files than can be encoded in the kernel
 * PTEs.
 */
#define MAX_SWAPFILES_CHECK() BUILD_BUG_ON(MAX_SWAPFILES_SHIFT > __SWP_TYPE_BITS)

extern int kern_addr_valid(unsigned long addr);

#ifdef CONFIG_ARM64_MTE

#define __HAVE_ARCH_PREPARE_TO_SWAP
static inline int arch_prepare_to_swap(struct page *page)
{
	if (system_supports_mte())
		return mte_save_tags(page);
	return 0;
}

#define __HAVE_ARCH_SWAP_INVALIDATE
static inline void arch_swap_invalidate_page(int type, pgoff_t offset)
{
	if (system_supports_mte())
		mte_invalidate_tags(type, offset);
}

static inline void arch_swap_invalidate_area(int type)
{
	if (system_supports_mte())
		mte_invalidate_tags_area(type);
}

#define __HAVE_ARCH_SWAP_RESTORE
static inline void arch_swap_restore(swp_entry_t entry, struct page *page)
{
	if (system_supports_mte() && mte_restore_tags(entry, page))
		set_bit(PG_mte_tagged, &page->flags);
}

#endif /* CONFIG_ARM64_MTE */

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

#ifdef CONFIG_ARM64_PA_BITS_52
#define phys_to_ttbr(addr)	(((addr) | ((addr) >> 46)) & TTBR_BADDR_MASK_52)
#else
#define phys_to_ttbr(addr)	(addr)
#endif

/*
 * On arm64 without hardware Access Flag, copying from user will fail because
 * the pte is old and cannot be marked young. So we always end up with zeroed
 * page after fork() + CoW for pfn mappings. We don't always have a
 * hardware-managed access flag on arm64.
 */
static inline bool arch_faults_on_old_pte(void)
{
	WARN_ON(preemptible());

	return !cpu_has_hw_af();
}
#define arch_faults_on_old_pte		arch_faults_on_old_pte

/*
 * Experimentally, it's cheap to set the access flag in hardware and we
 * benefit from prefaulting mappings as 'old' to start with.
 */
static inline bool arch_wants_old_prefaulted_pte(void)
{
	return !arch_faults_on_old_pte();
}
#define arch_wants_old_prefaulted_pte	arch_wants_old_prefaulted_pte

static inline pgprot_t arch_filter_pgprot(pgprot_t prot)
{
	if (cpus_have_const_cap(ARM64_HAS_EPAN))
		return prot;

	if (pgprot_val(prot) != pgprot_val(PAGE_EXECONLY))
		return prot;

	return PAGE_READONLY_EXEC;
}


#endif /* !__ASSEMBLY__ */

#endif /* __ASM_PGTABLE_H */
