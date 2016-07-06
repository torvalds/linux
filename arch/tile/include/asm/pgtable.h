/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * This file contains the functions and defines necessary to modify and use
 * the TILE page table tree.
 */

#ifndef _ASM_TILE_PGTABLE_H
#define _ASM_TILE_PGTABLE_H

#include <hv/hypervisor.h>

#ifndef __ASSEMBLY__

#include <linux/bitops.h>
#include <linux/threads.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/pfn.h>
#include <asm/processor.h>
#include <asm/fixmap.h>
#include <asm/page.h>

struct mm_struct;
struct vm_area_struct;

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern unsigned long empty_zero_page[PAGE_SIZE/sizeof(unsigned long)];
#define ZERO_PAGE(vaddr) (virt_to_page(empty_zero_page))

extern pgd_t swapper_pg_dir[];
extern pgprot_t swapper_pgprot;
extern struct kmem_cache *pgd_cache;
extern spinlock_t pgd_lock;
extern struct list_head pgd_list;

/*
 * The very last slots in the pgd_t are for addresses unusable by Linux
 * (pgd_addr_invalid() returns true).  So we use them for the list structure.
 * The x86 code we are modelled on uses the page->private/index fields
 * (older 2.6 kernels) or the lru list (newer 2.6 kernels), but since
 * our pgds are so much smaller than a page, it seems a waste to
 * spend a whole page on each pgd.
 */
#define PGD_LIST_OFFSET \
  ((PTRS_PER_PGD * sizeof(pgd_t)) - sizeof(struct list_head))
#define pgd_to_list(pgd) \
  ((struct list_head *)((char *)(pgd) + PGD_LIST_OFFSET))
#define list_to_pgd(list) \
  ((pgd_t *)((char *)(list) - PGD_LIST_OFFSET))

extern void pgtable_cache_init(void);
extern void paging_init(void);
extern void set_page_homes(void);

#define FIRST_USER_ADDRESS	0UL

#define _PAGE_PRESENT           HV_PTE_PRESENT
#define _PAGE_HUGE_PAGE         HV_PTE_PAGE
#define _PAGE_SUPER_PAGE        HV_PTE_SUPER
#define _PAGE_READABLE          HV_PTE_READABLE
#define _PAGE_WRITABLE          HV_PTE_WRITABLE
#define _PAGE_EXECUTABLE        HV_PTE_EXECUTABLE
#define _PAGE_ACCESSED          HV_PTE_ACCESSED
#define _PAGE_DIRTY             HV_PTE_DIRTY
#define _PAGE_GLOBAL            HV_PTE_GLOBAL
#define _PAGE_USER              HV_PTE_USER

/*
 * All the "standard" bits.  Cache-control bits are managed elsewhere.
 * This is used to test for valid level-2 page table pointers by checking
 * all the bits, and to mask away the cache control bits for mprotect.
 */
#define _PAGE_ALL (\
  _PAGE_PRESENT | \
  _PAGE_HUGE_PAGE | \
  _PAGE_SUPER_PAGE | \
  _PAGE_READABLE | \
  _PAGE_WRITABLE | \
  _PAGE_EXECUTABLE | \
  _PAGE_ACCESSED | \
  _PAGE_DIRTY | \
  _PAGE_GLOBAL | \
  _PAGE_USER \
)

#define PAGE_NONE \
	__pgprot(_PAGE_PRESENT | _PAGE_ACCESSED)
#define PAGE_SHARED \
	__pgprot(_PAGE_PRESENT | _PAGE_READABLE | _PAGE_WRITABLE | \
		 _PAGE_USER | _PAGE_ACCESSED)

#define PAGE_SHARED_EXEC \
	__pgprot(_PAGE_PRESENT | _PAGE_READABLE | _PAGE_WRITABLE | \
		 _PAGE_EXECUTABLE | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_COPY_NOEXEC \
	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED | _PAGE_READABLE)
#define PAGE_COPY_EXEC \
	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED | \
		 _PAGE_READABLE | _PAGE_EXECUTABLE)
#define PAGE_COPY \
	PAGE_COPY_NOEXEC
#define PAGE_READONLY \
	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED | _PAGE_READABLE)
#define PAGE_READONLY_EXEC \
	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED | \
		 _PAGE_READABLE | _PAGE_EXECUTABLE)

#define _PAGE_KERNEL_RO \
 (_PAGE_PRESENT | _PAGE_GLOBAL | _PAGE_READABLE | _PAGE_ACCESSED)
#define _PAGE_KERNEL \
 (_PAGE_KERNEL_RO | _PAGE_WRITABLE | _PAGE_DIRTY)
#define _PAGE_KERNEL_EXEC       (_PAGE_KERNEL_RO | _PAGE_EXECUTABLE)

#define PAGE_KERNEL		__pgprot(_PAGE_KERNEL)
#define PAGE_KERNEL_RO		__pgprot(_PAGE_KERNEL_RO)
#define PAGE_KERNEL_EXEC	__pgprot(_PAGE_KERNEL_EXEC)

#define page_to_kpgprot(p) PAGE_KERNEL

/*
 * We could tighten these up, but for now writable or executable
 * implies readable.
 */
#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY
#define __P010	PAGE_COPY      /* this is write-only, which we won't support */
#define __P011	PAGE_COPY
#define __P100	PAGE_READONLY_EXEC
#define __P101	PAGE_READONLY_EXEC
#define __P110	PAGE_COPY_EXEC
#define __P111	PAGE_COPY_EXEC

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_READONLY_EXEC
#define __S101	PAGE_READONLY_EXEC
#define __S110	PAGE_SHARED_EXEC
#define __S111	PAGE_SHARED_EXEC

/*
 * All the normal _PAGE_ALL bits are ignored for PMDs, except PAGE_PRESENT
 * and PAGE_HUGE_PAGE, which must be one and zero, respectively.
 * We set the ignored bits to zero.
 */
#define _PAGE_TABLE     _PAGE_PRESENT

/* Inherit the caching flags from the old protection bits. */
#define pgprot_modify(oldprot, newprot) \
  (pgprot_t) { ((oldprot).val & ~_PAGE_ALL) | (newprot).val }

/* Just setting the PFN to zero suffices. */
#define pte_pgprot(x) hv_pte_set_pa((x), 0)

/*
 * For PTEs and PDEs, we must clear the Present bit first when
 * clearing a page table entry, so clear the bottom half first and
 * enforce ordering with a barrier.
 */
static inline void __pte_clear(pte_t *ptep)
{
#ifdef __tilegx__
	ptep->val = 0;
#else
	u32 *tmp = (u32 *)ptep;
	tmp[0] = 0;
	barrier();
	tmp[1] = 0;
#endif
}
#define pte_clear(mm, addr, ptep) __pte_clear(ptep)

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
#define pte_present hv_pte_get_present
#define pte_mknotpresent hv_pte_clear_present
#define pte_user hv_pte_get_user
#define pte_read hv_pte_get_readable
#define pte_dirty hv_pte_get_dirty
#define pte_young hv_pte_get_accessed
#define pte_write hv_pte_get_writable
#define pte_exec hv_pte_get_executable
#define pte_huge hv_pte_get_page
#define pte_super hv_pte_get_super
#define pte_rdprotect hv_pte_clear_readable
#define pte_exprotect hv_pte_clear_executable
#define pte_mkclean hv_pte_clear_dirty
#define pte_mkold hv_pte_clear_accessed
#define pte_wrprotect hv_pte_clear_writable
#define pte_mksmall hv_pte_clear_page
#define pte_mkread hv_pte_set_readable
#define pte_mkexec hv_pte_set_executable
#define pte_mkdirty hv_pte_set_dirty
#define pte_mkyoung hv_pte_set_accessed
#define pte_mkwrite hv_pte_set_writable
#define pte_mkhuge hv_pte_set_page
#define pte_mksuper hv_pte_set_super

#define pte_special(pte) 0
#define pte_mkspecial(pte) (pte)

/*
 * Use some spare bits in the PTE for user-caching tags.
 */
#define pte_set_forcecache hv_pte_set_client0
#define pte_get_forcecache hv_pte_get_client0
#define pte_clear_forcecache hv_pte_clear_client0
#define pte_set_anyhome hv_pte_set_client1
#define pte_get_anyhome hv_pte_get_client1
#define pte_clear_anyhome hv_pte_clear_client1

/*
 * A migrating PTE has PAGE_PRESENT clear but all the other bits preserved.
 */
#define pte_migrating hv_pte_get_migrating
#define pte_mkmigrate(x) hv_pte_set_migrating(hv_pte_clear_present(x))
#define pte_donemigrate(x) hv_pte_set_present(hv_pte_clear_migrating(x))

#define pte_ERROR(e) \
	pr_err("%s:%d: bad pte 0x%016llx\n", __FILE__, __LINE__, pte_val(e))
#define pgd_ERROR(e) \
	pr_err("%s:%d: bad pgd 0x%016llx\n", __FILE__, __LINE__, pgd_val(e))

/* Return PA and protection info for a given kernel VA. */
int va_to_cpa_and_pte(void *va, phys_addr_t *cpa, pte_t *pte);

/*
 * __set_pte() ensures we write the 64-bit PTE with 32-bit words in
 * the right order on 32-bit platforms and also allows us to write
 * hooks to check valid PTEs, etc., if we want.
 */
void __set_pte(pte_t *ptep, pte_t pte);

/*
 * set_pte() sets the given PTE and also sanity-checks the
 * requested PTE against the page homecaching.  Unspecified parts
 * of the PTE are filled in when it is written to memory, i.e. all
 * caching attributes if "!forcecache", or the home cpu if "anyhome".
 */
extern void set_pte(pte_t *ptep, pte_t pte);
#define set_pte_at(mm, addr, ptep, pteval) set_pte(ptep, pteval)
#define set_pte_atomic(pteptr, pteval) set_pte(pteptr, pteval)

#define pte_page(x)		pfn_to_page(pte_pfn(x))

static inline int pte_none(pte_t pte)
{
	return !pte.val;
}

static inline unsigned long pte_pfn(pte_t pte)
{
	return PFN_DOWN(hv_pte_get_pa(pte));
}

/* Set or get the remote cache cpu in a pgprot with remote caching. */
extern pgprot_t set_remote_cache_cpu(pgprot_t prot, int cpu);
extern int get_remote_cache_cpu(pgprot_t prot);

static inline pte_t pfn_pte(unsigned long pfn, pgprot_t prot)
{
	return hv_pte_set_pa(prot, PFN_PHYS(pfn));
}

/* Support for priority mappings. */
extern void start_mm_caching(struct mm_struct *mm);
extern void check_mm_caching(struct mm_struct *prev, struct mm_struct *next);

/*
 * Encode and de-code a swap entry (see <linux/swapops.h>).
 * We put the swap file type+offset in the 32 high bits;
 * I believe we can just leave the low bits clear.
 */
#define __swp_type(swp)		((swp).val & 0x1f)
#define __swp_offset(swp)	((swp).val >> 5)
#define __swp_entry(type, off)	((swp_entry_t) { (type) | ((off) << 5) })
#define __pte_to_swp_entry(pte)	((swp_entry_t) { (pte).val >> 32 })
#define __swp_entry_to_pte(swp)	((pte_t) { (((long long) ((swp).val)) << 32) })

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */

#define mk_pte(page, pgprot)	pfn_pte(page_to_pfn(page), (pgprot))

/*
 * If we are doing an mprotect(), just accept the new vma->vm_page_prot
 * value and combine it with the PFN from the old PTE to get a new PTE.
 */
static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	return pfn_pte(pte_pfn(pte), newprot);
}

/*
 * The pgd page can be thought of an array like this: pgd_t[PTRS_PER_PGD]
 *
 * This macro returns the index of the entry in the pgd page which would
 * control the given virtual address.
 */
#define pgd_index(address) (((address) >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1))

/*
 * pgd_offset() returns a (pgd_t *)
 * pgd_index() is used get the offset into the pgd page's array of pgd_t's.
 */
#define pgd_offset(mm, address) ((mm)->pgd + pgd_index(address))

/*
 * A shortcut which implies the use of the kernel's pgd, instead
 * of a process's.
 */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

#define pte_offset_map(dir, address) pte_offset_kernel(dir, address)
#define pte_unmap(pte) do { } while (0)

/* Clear a non-executable kernel PTE and flush it from the TLB. */
#define kpte_clear_flush(ptep, vaddr)		\
do {						\
	pte_clear(&init_mm, (vaddr), (ptep));	\
	local_flush_tlb_page(FLUSH_NONEXEC, (vaddr), PAGE_SIZE); \
} while (0)

/*
 * The kernel page tables contain what we need, and we flush when we
 * change specific page table entries.
 */
#define update_mmu_cache(vma, address, pte) do { } while (0)

#ifdef CONFIG_FLATMEM
#define kern_addr_valid(addr)	(1)
#endif /* CONFIG_FLATMEM */

extern void vmalloc_sync_all(void);

#endif /* !__ASSEMBLY__ */

#ifdef __tilegx__
#include <asm/pgtable_64.h>
#else
#include <asm/pgtable_32.h>
#endif

#ifndef __ASSEMBLY__

static inline int pmd_none(pmd_t pmd)
{
	/*
	 * Only check low word on 32-bit platforms, since it might be
	 * out of sync with upper half.
	 */
	return (unsigned long)pmd_val(pmd) == 0;
}

static inline int pmd_present(pmd_t pmd)
{
	return pmd_val(pmd) & _PAGE_PRESENT;
}

static inline int pmd_bad(pmd_t pmd)
{
	return ((pmd_val(pmd) & _PAGE_ALL) != _PAGE_TABLE);
}

static inline unsigned long pages_to_mb(unsigned long npg)
{
	return npg >> (20 - PAGE_SHIFT);
}

/*
 * The pmd can be thought of an array like this: pmd_t[PTRS_PER_PMD]
 *
 * This function returns the index of the entry in the pmd which would
 * control the given virtual address.
 */
static inline unsigned long pmd_index(unsigned long address)
{
	return (address >> PMD_SHIFT) & (PTRS_PER_PMD - 1);
}

#define __HAVE_ARCH_PMDP_TEST_AND_CLEAR_YOUNG
static inline int pmdp_test_and_clear_young(struct vm_area_struct *vma,
					    unsigned long address,
					    pmd_t *pmdp)
{
	return ptep_test_and_clear_young(vma, address, pmdp_ptep(pmdp));
}

#define __HAVE_ARCH_PMDP_SET_WRPROTECT
static inline void pmdp_set_wrprotect(struct mm_struct *mm,
				      unsigned long address, pmd_t *pmdp)
{
	ptep_set_wrprotect(mm, address, pmdp_ptep(pmdp));
}


#define __HAVE_ARCH_PMDP_HUGE_GET_AND_CLEAR
static inline pmd_t pmdp_huge_get_and_clear(struct mm_struct *mm,
					    unsigned long address,
					    pmd_t *pmdp)
{
	return pte_pmd(ptep_get_and_clear(mm, address, pmdp_ptep(pmdp)));
}

static inline void __set_pmd(pmd_t *pmdp, pmd_t pmdval)
{
	set_pte(pmdp_ptep(pmdp), pmd_pte(pmdval));
}

#define set_pmd_at(mm, addr, pmdp, pmdval) __set_pmd(pmdp, pmdval)

/* Create a pmd from a PTFN. */
static inline pmd_t ptfn_pmd(unsigned long ptfn, pgprot_t prot)
{
	return pte_pmd(hv_pte_set_ptfn(prot, ptfn));
}

/* Return the page-table frame number (ptfn) that a pmd_t points at. */
#define pmd_ptfn(pmd) hv_pte_get_ptfn(pmd_pte(pmd))

/*
 * A given kernel pmd_t maps to a specific virtual address (either a
 * kernel huge page or a kernel pte_t table).  Since kernel pte_t
 * tables can be aligned at sub-page granularity, this function can
 * return non-page-aligned pointers, despite its name.
 */
static inline unsigned long pmd_page_vaddr(pmd_t pmd)
{
	phys_addr_t pa =
		(phys_addr_t)pmd_ptfn(pmd) << HV_LOG2_PAGE_TABLE_ALIGN;
	return (unsigned long)__va(pa);
}

/*
 * A pmd_t points to the base of a huge page or to a pte_t array.
 * If a pte_t array, since we can have multiple per page, we don't
 * have a one-to-one mapping of pmd_t's to pages.  However, this is
 * OK for pte_lockptr(), since we just end up with potentially one
 * lock being used for several pte_t arrays.
 */
#define pmd_page(pmd) pfn_to_page(PFN_DOWN(HV_PTFN_TO_CPA(pmd_ptfn(pmd))))

static inline void pmd_clear(pmd_t *pmdp)
{
	__pte_clear(pmdp_ptep(pmdp));
}

#define pmd_mknotpresent(pmd)	pte_pmd(pte_mknotpresent(pmd_pte(pmd)))
#define pmd_young(pmd)		pte_young(pmd_pte(pmd))
#define pmd_mkyoung(pmd)	pte_pmd(pte_mkyoung(pmd_pte(pmd)))
#define pmd_mkold(pmd)		pte_pmd(pte_mkold(pmd_pte(pmd)))
#define pmd_mkwrite(pmd)	pte_pmd(pte_mkwrite(pmd_pte(pmd)))
#define pmd_write(pmd)		pte_write(pmd_pte(pmd))
#define pmd_wrprotect(pmd)	pte_pmd(pte_wrprotect(pmd_pte(pmd)))
#define pmd_mkdirty(pmd)	pte_pmd(pte_mkdirty(pmd_pte(pmd)))
#define pmd_huge_page(pmd)	pte_huge(pmd_pte(pmd))
#define pmd_mkhuge(pmd)		pte_pmd(pte_mkhuge(pmd_pte(pmd)))
#define __HAVE_ARCH_PMD_WRITE

#define pfn_pmd(pfn, pgprot)	pte_pmd(pfn_pte((pfn), (pgprot)))
#define pmd_pfn(pmd)		pte_pfn(pmd_pte(pmd))
#define mk_pmd(page, pgprot)	pfn_pmd(page_to_pfn(page), (pgprot))

static inline pmd_t pmd_modify(pmd_t pmd, pgprot_t newprot)
{
	return pfn_pmd(pmd_pfn(pmd), newprot);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
#define pmd_trans_huge pmd_huge_page
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

/*
 * The pte page can be thought of an array like this: pte_t[PTRS_PER_PTE]
 *
 * This macro returns the index of the entry in the pte page which would
 * control the given virtual address.
 */
static inline unsigned long pte_index(unsigned long address)
{
	return (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
}

static inline pte_t *pte_offset_kernel(pmd_t *pmd, unsigned long address)
{
       return (pte_t *)pmd_page_vaddr(*pmd) + pte_index(address);
}

#include <asm-generic/pgtable.h>

/* Support /proc/NN/pgtable API. */
struct seq_file;
int arch_proc_pgtable_show(struct seq_file *m, struct mm_struct *mm,
			   unsigned long vaddr, unsigned long pagesize,
			   pte_t *ptep, void **datap);

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_TILE_PGTABLE_H */
