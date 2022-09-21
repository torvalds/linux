/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_NOHASH_64_PGTABLE_H
#define _ASM_POWERPC_NOHASH_64_PGTABLE_H
/*
 * This file contains the functions and defines necessary to modify and use
 * the ppc64 non-hashed page table.
 */

#include <linux/sizes.h>

#include <asm/nohash/64/pgtable-4k.h>
#include <asm/barrier.h>
#include <asm/asm-const.h>

/*
 * Size of EA range mapped by our pagetables.
 */
#define PGTABLE_EADDR_SIZE (PTE_INDEX_SIZE + PMD_INDEX_SIZE + \
			    PUD_INDEX_SIZE + PGD_INDEX_SIZE + PAGE_SHIFT)
#define PGTABLE_RANGE (ASM_CONST(1) << PGTABLE_EADDR_SIZE)

#define PMD_CACHE_INDEX	PMD_INDEX_SIZE
#define PUD_CACHE_INDEX PUD_INDEX_SIZE

/*
 * Define the address range of the kernel non-linear virtual area
 */
#define KERN_VIRT_START ASM_CONST(0xc000100000000000)
#define KERN_VIRT_SIZE	ASM_CONST(0x0000100000000000)

/*
 * The vmalloc space starts at the beginning of that region, and
 * occupies a quarter of it on Book3E
 * (we keep a quarter for the virtual memmap)
 */
#define VMALLOC_START	KERN_VIRT_START
#define VMALLOC_SIZE	(KERN_VIRT_SIZE >> 2)
#define VMALLOC_END	(VMALLOC_START + VMALLOC_SIZE)

/*
 * The third quarter of the kernel virtual space is used for IO mappings,
 * it's itself carved into the PIO region (ISA and PHB IO space) and
 * the ioremap space
 *
 *  ISA_IO_BASE = KERN_IO_START, 64K reserved area
 *  PHB_IO_BASE = ISA_IO_BASE + 64K to ISA_IO_BASE + 2G, PHB IO spaces
 * IOREMAP_BASE = ISA_IO_BASE + 2G to KERN_IO_START + KERN_IO_SIZE
 */
#define KERN_IO_START	(KERN_VIRT_START + (KERN_VIRT_SIZE >> 1))
#define KERN_IO_SIZE	(KERN_VIRT_SIZE >> 2)
#define FULL_IO_SIZE	0x80000000ul
#define  ISA_IO_BASE	(KERN_IO_START)
#define  ISA_IO_END	(KERN_IO_START + 0x10000ul)
#define  PHB_IO_BASE	(ISA_IO_END)
#define  PHB_IO_END	(KERN_IO_START + FULL_IO_SIZE)
#define IOREMAP_BASE	(PHB_IO_END)
#define IOREMAP_START	(ioremap_bot)
#define IOREMAP_END	(KERN_IO_START + KERN_IO_SIZE - FIXADDR_SIZE)
#define FIXADDR_SIZE	SZ_32M

/*
 * Defines the address of the vmemap area, in its own region on
 * after the vmalloc space on Book3E
 */
#define VMEMMAP_BASE		VMALLOC_END
#define VMEMMAP_END		KERN_IO_START
#define vmemmap			((struct page *)VMEMMAP_BASE)


/*
 * Include the PTE bits definitions
 */
#include <asm/nohash/pte-book3e.h>

#define PTE_RPN_MASK	(~((1UL << PTE_RPN_SHIFT) - 1))

/*
 * _PAGE_CHG_MASK masks of bits that are to be preserved across
 * pgprot changes.
 */
#define _PAGE_CHG_MASK	(PTE_RPN_MASK | _PAGE_DIRTY | _PAGE_ACCESSED | _PAGE_SPECIAL)

#define H_PAGE_4K_PFN 0

#ifndef __ASSEMBLY__
/* pte_clear moved to later in this file */

static inline pte_t pte_mkwrite(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_RW);
}

static inline pte_t pte_mkdirty(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_DIRTY);
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_ACCESSED);
}

static inline pte_t pte_wrprotect(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_RW);
}

#define PMD_BAD_BITS		(PTE_TABLE_SIZE-1)
#define PUD_BAD_BITS		(PMD_TABLE_SIZE-1)

static inline void pmd_set(pmd_t *pmdp, unsigned long val)
{
	*pmdp = __pmd(val);
}

static inline void pmd_clear(pmd_t *pmdp)
{
	*pmdp = __pmd(0);
}

static inline pte_t pmd_pte(pmd_t pmd)
{
	return __pte(pmd_val(pmd));
}

#define pmd_none(pmd)		(!pmd_val(pmd))
#define	pmd_bad(pmd)		(!is_kernel_addr(pmd_val(pmd)) \
				 || (pmd_val(pmd) & PMD_BAD_BITS))
#define	pmd_present(pmd)	(!pmd_none(pmd))
#define pmd_page_vaddr(pmd)	(pmd_val(pmd) & ~PMD_MASKED_BITS)
extern struct page *pmd_page(pmd_t pmd);
#define pmd_pfn(pmd)		(page_to_pfn(pmd_page(pmd)))

static inline void pud_set(pud_t *pudp, unsigned long val)
{
	*pudp = __pud(val);
}

static inline void pud_clear(pud_t *pudp)
{
	*pudp = __pud(0);
}

#define pud_none(pud)		(!pud_val(pud))
#define	pud_bad(pud)		(!is_kernel_addr(pud_val(pud)) \
				 || (pud_val(pud) & PUD_BAD_BITS))
#define pud_present(pud)	(pud_val(pud) != 0)

static inline pmd_t *pud_pgtable(pud_t pud)
{
	return (pmd_t *)(pud_val(pud) & ~PUD_MASKED_BITS);
}

extern struct page *pud_page(pud_t pud);

static inline pte_t pud_pte(pud_t pud)
{
	return __pte(pud_val(pud));
}

static inline pud_t pte_pud(pte_t pte)
{
	return __pud(pte_val(pte));
}
#define pud_write(pud)		pte_write(pud_pte(pud))
#define p4d_write(pgd)		pte_write(p4d_pte(p4d))

static inline void p4d_set(p4d_t *p4dp, unsigned long val)
{
	*p4dp = __p4d(val);
}

/* Atomic PTE updates */
static inline unsigned long pte_update(struct mm_struct *mm,
				       unsigned long addr,
				       pte_t *ptep, unsigned long clr,
				       unsigned long set,
				       int huge)
{
	unsigned long old = pte_val(*ptep);
	*ptep = __pte((old & ~clr) | set);

	/* huge pages use the old page table lock */
	if (!huge)
		assert_pte_locked(mm, addr);

	return old;
}

static inline int pte_young(pte_t pte)
{
	return pte_val(pte) & _PAGE_ACCESSED;
}

static inline int __ptep_test_and_clear_young(struct mm_struct *mm,
					      unsigned long addr, pte_t *ptep)
{
	unsigned long old;

	if (pte_young(*ptep))
		return 0;
	old = pte_update(mm, addr, ptep, _PAGE_ACCESSED, 0, 0);
	return (old & _PAGE_ACCESSED) != 0;
}
#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
#define ptep_test_and_clear_young(__vma, __addr, __ptep)		   \
({									   \
	int __r;							   \
	__r = __ptep_test_and_clear_young((__vma)->vm_mm, __addr, __ptep); \
	__r;								   \
})

#define __HAVE_ARCH_PTEP_SET_WRPROTECT
static inline void ptep_set_wrprotect(struct mm_struct *mm, unsigned long addr,
				      pte_t *ptep)
{

	if ((pte_val(*ptep) & _PAGE_RW) == 0)
		return;

	pte_update(mm, addr, ptep, _PAGE_RW, 0, 0);
}

#define __HAVE_ARCH_HUGE_PTEP_SET_WRPROTECT
static inline void huge_ptep_set_wrprotect(struct mm_struct *mm,
					   unsigned long addr, pte_t *ptep)
{
	if ((pte_val(*ptep) & _PAGE_RW) == 0)
		return;

	pte_update(mm, addr, ptep, _PAGE_RW, 0, 1);
}

#define __HAVE_ARCH_PTEP_CLEAR_YOUNG_FLUSH
#define ptep_clear_flush_young(__vma, __address, __ptep)		\
({									\
	int __young = __ptep_test_and_clear_young((__vma)->vm_mm, __address, \
						  __ptep);		\
	__young;							\
})

#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
static inline pte_t ptep_get_and_clear(struct mm_struct *mm,
				       unsigned long addr, pte_t *ptep)
{
	unsigned long old = pte_update(mm, addr, ptep, ~0UL, 0, 0);
	return __pte(old);
}

static inline void pte_clear(struct mm_struct *mm, unsigned long addr,
			     pte_t * ptep)
{
	pte_update(mm, addr, ptep, ~0UL, 0, 0);
}


/* Set the dirty and/or accessed bits atomically in a linux PTE */
static inline void __ptep_set_access_flags(struct vm_area_struct *vma,
					   pte_t *ptep, pte_t entry,
					   unsigned long address,
					   int psize)
{
	unsigned long bits = pte_val(entry) &
		(_PAGE_DIRTY | _PAGE_ACCESSED | _PAGE_RW | _PAGE_EXEC);

	unsigned long old = pte_val(*ptep);
	*ptep = __pte(old | bits);

	flush_tlb_page(vma, address);
}

#define pte_ERROR(e) \
	pr_err("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, pte_val(e))
#define pmd_ERROR(e) \
	pr_err("%s:%d: bad pmd %08lx.\n", __FILE__, __LINE__, pmd_val(e))
#define pgd_ERROR(e) \
	pr_err("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

/* Encode and de-code a swap entry */
#define MAX_SWAPFILES_CHECK() do { \
	BUILD_BUG_ON(MAX_SWAPFILES_SHIFT > SWP_TYPE_BITS); \
	} while (0)

#define SWP_TYPE_BITS 5
#define __swp_type(x)		(((x).val >> _PAGE_BIT_SWAP_TYPE) \
				& ((1UL << SWP_TYPE_BITS) - 1))
#define __swp_offset(x)		((x).val >> PTE_RPN_SHIFT)
#define __swp_entry(type, offset)	((swp_entry_t) { \
					((type) << _PAGE_BIT_SWAP_TYPE) \
					| ((offset) << PTE_RPN_SHIFT) })

#define __pte_to_swp_entry(pte)		((swp_entry_t) { pte_val((pte)) })
#define __swp_entry_to_pte(x)		__pte((x).val)

int map_kernel_page(unsigned long ea, unsigned long pa, pgprot_t prot);
void unmap_kernel_page(unsigned long va);
extern int __meminit vmemmap_create_mapping(unsigned long start,
					    unsigned long page_size,
					    unsigned long phys);
extern void vmemmap_remove_mapping(unsigned long start,
				   unsigned long page_size);
void __patch_exception(int exc, unsigned long addr);
#define patch_exception(exc, name) do { \
	extern unsigned int name; \
	__patch_exception((exc), (unsigned long)&name); \
} while (0)

#endif /* __ASSEMBLY__ */

#endif /* _ASM_POWERPC_NOHASH_64_PGTABLE_H */
