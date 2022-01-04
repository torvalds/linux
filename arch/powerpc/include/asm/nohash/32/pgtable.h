/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_NOHASH_32_PGTABLE_H
#define _ASM_POWERPC_NOHASH_32_PGTABLE_H

#include <asm-generic/pgtable-nopmd.h>

#ifndef __ASSEMBLY__
#include <linux/sched.h>
#include <linux/threads.h>
#include <asm/mmu.h>			/* For sub-arch specific PPC_PIN_SIZE */

#ifdef CONFIG_44x
extern int icache_44x_need_flush;
#endif

#endif /* __ASSEMBLY__ */

#define PTE_INDEX_SIZE	PTE_SHIFT
#define PMD_INDEX_SIZE	0
#define PUD_INDEX_SIZE	0
#define PGD_INDEX_SIZE	(32 - PGDIR_SHIFT)

#define PMD_CACHE_INDEX	PMD_INDEX_SIZE
#define PUD_CACHE_INDEX	PUD_INDEX_SIZE

#ifndef __ASSEMBLY__
#define PTE_TABLE_SIZE	(sizeof(pte_t) << PTE_INDEX_SIZE)
#define PMD_TABLE_SIZE	0
#define PUD_TABLE_SIZE	0
#define PGD_TABLE_SIZE	(sizeof(pgd_t) << PGD_INDEX_SIZE)

#define PMD_MASKED_BITS (PTE_TABLE_SIZE - 1)
#endif	/* __ASSEMBLY__ */

#define PTRS_PER_PTE	(1 << PTE_INDEX_SIZE)
#define PTRS_PER_PGD	(1 << PGD_INDEX_SIZE)

/*
 * The normal case is that PTEs are 32-bits and we have a 1-page
 * 1024-entry pgdir pointing to 1-page 1024-entry PTE pages.  -- paulus
 *
 * For any >32-bit physical address platform, we can use the following
 * two level page table layout where the pgdir is 8KB and the MS 13 bits
 * are an index to the second level table.  The combined pgdir/pmd first
 * level has 2048 entries and the second level has 512 64-bit PTE entries.
 * -Matt
 */
/* PGDIR_SHIFT determines what a top-level page table entry can map */
#define PGDIR_SHIFT	(PAGE_SHIFT + PTE_INDEX_SIZE)
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/* Bits to mask out from a PGD to get to the PUD page */
#define PGD_MASKED_BITS		0

#define USER_PTRS_PER_PGD	(TASK_SIZE / PGDIR_SIZE)

#define pte_ERROR(e) \
	pr_err("%s:%d: bad pte %llx.\n", __FILE__, __LINE__, \
		(unsigned long long)pte_val(e))
#define pgd_ERROR(e) \
	pr_err("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

#ifndef __ASSEMBLY__

int map_kernel_page(unsigned long va, phys_addr_t pa, pgprot_t prot);

#endif /* !__ASSEMBLY__ */


/*
 * This is the bottom of the PKMAP area with HIGHMEM or an arbitrary
 * value (for now) on others, from where we can start layout kernel
 * virtual space that goes below PKMAP and FIXMAP
 */
#include <asm/fixmap.h>

/*
 * ioremap_bot starts at that address. Early ioremaps move down from there,
 * until mem_init() at which point this becomes the top of the vmalloc
 * and ioremap space
 */
#ifdef CONFIG_HIGHMEM
#define IOREMAP_TOP	PKMAP_BASE
#else
#define IOREMAP_TOP	FIXADDR_START
#endif

/* PPC32 shares vmalloc area with ioremap */
#define IOREMAP_START	VMALLOC_START
#define IOREMAP_END	VMALLOC_END

/*
 * Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 16MB value just means that there will be a 64MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 *
 * We no longer map larger than phys RAM with the BATs so we don't have
 * to worry about the VMALLOC_OFFSET causing problems.  We do have to worry
 * about clashes between our early calls to ioremap() that start growing down
 * from IOREMAP_TOP being run into the VM area allocations (growing upwards
 * from VMALLOC_START).  For this reason we have ioremap_bot to check when
 * we actually run into our mappings setup in the early boot with the VM
 * system.  This really does become a problem for machines with good amounts
 * of RAM.  -- Cort
 */
#define VMALLOC_OFFSET (0x1000000) /* 16M */
#ifdef PPC_PIN_SIZE
#define VMALLOC_START (((ALIGN((long)high_memory, PPC_PIN_SIZE) + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1)))
#else
#define VMALLOC_START ((((long)high_memory + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1)))
#endif

#ifdef CONFIG_KASAN_VMALLOC
#define VMALLOC_END	ALIGN_DOWN(ioremap_bot, PAGE_SIZE << KASAN_SHADOW_SCALE_SHIFT)
#else
#define VMALLOC_END	ioremap_bot
#endif

/*
 * Bits in a linux-style PTE.  These match the bits in the
 * (hardware-defined) PowerPC PTE as closely as possible.
 */

#if defined(CONFIG_40x)
#include <asm/nohash/32/pte-40x.h>
#elif defined(CONFIG_44x)
#include <asm/nohash/32/pte-44x.h>
#elif defined(CONFIG_FSL_BOOKE) && defined(CONFIG_PTE_64BIT)
#include <asm/nohash/pte-book3e.h>
#elif defined(CONFIG_FSL_BOOKE)
#include <asm/nohash/32/pte-fsl-booke.h>
#elif defined(CONFIG_PPC_8xx)
#include <asm/nohash/32/pte-8xx.h>
#endif

/*
 * Location of the PFN in the PTE. Most 32-bit platforms use the same
 * as _PAGE_SHIFT here (ie, naturally aligned).
 * Platform who don't just pre-define the value so we don't override it here.
 */
#ifndef PTE_RPN_SHIFT
#define PTE_RPN_SHIFT	(PAGE_SHIFT)
#endif

/*
 * The mask covered by the RPN must be a ULL on 32-bit platforms with
 * 64-bit PTEs.
 */
#if defined(CONFIG_PPC32) && defined(CONFIG_PTE_64BIT)
#define PTE_RPN_MASK	(~((1ULL << PTE_RPN_SHIFT) - 1))
#define MAX_POSSIBLE_PHYSMEM_BITS 36
#else
#define PTE_RPN_MASK	(~((1UL << PTE_RPN_SHIFT) - 1))
#define MAX_POSSIBLE_PHYSMEM_BITS 32
#endif

/*
 * _PAGE_CHG_MASK masks of bits that are to be preserved across
 * pgprot changes.
 */
#define _PAGE_CHG_MASK	(PTE_RPN_MASK | _PAGE_DIRTY | _PAGE_ACCESSED | _PAGE_SPECIAL)

#ifndef __ASSEMBLY__

#define pte_clear(mm, addr, ptep) \
	do { pte_update(mm, addr, ptep, ~0, 0, 0); } while (0)

#ifndef pte_mkwrite
static inline pte_t pte_mkwrite(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_RW);
}
#endif

static inline pte_t pte_mkdirty(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_DIRTY);
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_ACCESSED);
}

#ifndef pte_wrprotect
static inline pte_t pte_wrprotect(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_RW);
}
#endif

static inline pte_t pte_mkexec(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_EXEC);
}

#define pmd_none(pmd)		(!pmd_val(pmd))
#define	pmd_bad(pmd)		(pmd_val(pmd) & _PMD_BAD)
#define	pmd_present(pmd)	(pmd_val(pmd) & _PMD_PRESENT_MASK)
static inline void pmd_clear(pmd_t *pmdp)
{
	*pmdp = __pmd(0);
}

/*
 * PTE updates. This function is called whenever an existing
 * valid PTE is updated. This does -not- include set_pte_at()
 * which nowadays only sets a new PTE.
 *
 * Depending on the type of MMU, we may need to use atomic updates
 * and the PTE may be either 32 or 64 bit wide. In the later case,
 * when using atomic updates, only the low part of the PTE is
 * accessed atomically.
 *
 * In addition, on 44x, we also maintain a global flag indicating
 * that an executable user mapping was modified, which is needed
 * to properly flush the virtually tagged instruction cache of
 * those implementations.
 *
 * On the 8xx, the page tables are a bit special. For 16k pages, we have
 * 4 identical entries. For 512k pages, we have 128 entries as if it was
 * 4k pages, but they are flagged as 512k pages for the hardware.
 * For other page sizes, we have a single entry in the table.
 */
#ifdef CONFIG_PPC_8xx
static pmd_t *pmd_off(struct mm_struct *mm, unsigned long addr);
static int hugepd_ok(hugepd_t hpd);

static int number_of_cells_per_pte(pmd_t *pmd, pte_basic_t val, int huge)
{
	if (!huge)
		return PAGE_SIZE / SZ_4K;
	else if (hugepd_ok(*((hugepd_t *)pmd)))
		return 1;
	else if (IS_ENABLED(CONFIG_PPC_4K_PAGES) && !(val & _PAGE_HUGE))
		return SZ_16K / SZ_4K;
	else
		return SZ_512K / SZ_4K;
}

static inline pte_basic_t pte_update(struct mm_struct *mm, unsigned long addr, pte_t *p,
				     unsigned long clr, unsigned long set, int huge)
{
	pte_basic_t *entry = &p->pte;
	pte_basic_t old = pte_val(*p);
	pte_basic_t new = (old & ~(pte_basic_t)clr) | set;
	int num, i;
	pmd_t *pmd = pmd_off(mm, addr);

	num = number_of_cells_per_pte(pmd, new, huge);

	for (i = 0; i < num; i++, entry++, new += SZ_4K)
		*entry = new;

	return old;
}

#ifdef CONFIG_PPC_16K_PAGES
#define __HAVE_ARCH_PTEP_GET
static inline pte_t ptep_get(pte_t *ptep)
{
	pte_basic_t val = READ_ONCE(ptep->pte);
	pte_t pte = {val, val, val, val};

	return pte;
}
#endif /* CONFIG_PPC_16K_PAGES */

#else
static inline pte_basic_t pte_update(struct mm_struct *mm, unsigned long addr, pte_t *p,
				     unsigned long clr, unsigned long set, int huge)
{
	pte_basic_t old = pte_val(*p);
	pte_basic_t new = (old & ~(pte_basic_t)clr) | set;

	*p = __pte(new);

#ifdef CONFIG_44x
	if ((old & _PAGE_USER) && (old & _PAGE_EXEC))
		icache_44x_need_flush = 1;
#endif
	return old;
}
#endif

#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
static inline int __ptep_test_and_clear_young(struct mm_struct *mm,
					      unsigned long addr, pte_t *ptep)
{
	unsigned long old;
	old = pte_update(mm, addr, ptep, _PAGE_ACCESSED, 0, 0);
	return (old & _PAGE_ACCESSED) != 0;
}
#define ptep_test_and_clear_young(__vma, __addr, __ptep) \
	__ptep_test_and_clear_young((__vma)->vm_mm, __addr, __ptep)

#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
static inline pte_t ptep_get_and_clear(struct mm_struct *mm, unsigned long addr,
				       pte_t *ptep)
{
	return __pte(pte_update(mm, addr, ptep, ~0, 0, 0));
}

#define __HAVE_ARCH_PTEP_SET_WRPROTECT
static inline void ptep_set_wrprotect(struct mm_struct *mm, unsigned long addr,
				      pte_t *ptep)
{
	unsigned long clr = ~pte_val(pte_wrprotect(__pte(~0)));
	unsigned long set = pte_val(pte_wrprotect(__pte(0)));

	pte_update(mm, addr, ptep, clr, set, 0);
}

static inline void __ptep_set_access_flags(struct vm_area_struct *vma,
					   pte_t *ptep, pte_t entry,
					   unsigned long address,
					   int psize)
{
	pte_t pte_set = pte_mkyoung(pte_mkdirty(pte_mkwrite(pte_mkexec(__pte(0)))));
	pte_t pte_clr = pte_mkyoung(pte_mkdirty(pte_mkwrite(pte_mkexec(__pte(~0)))));
	unsigned long set = pte_val(entry) & pte_val(pte_set);
	unsigned long clr = ~pte_val(entry) & ~pte_val(pte_clr);
	int huge = psize > mmu_virtual_psize ? 1 : 0;

	pte_update(vma->vm_mm, address, ptep, clr, set, huge);

	flush_tlb_page(vma, address);
}

static inline int pte_young(pte_t pte)
{
	return pte_val(pte) & _PAGE_ACCESSED;
}

#define __HAVE_ARCH_PTE_SAME
#define pte_same(A,B)	((pte_val(A) ^ pte_val(B)) == 0)

/*
 * Note that on Book E processors, the pmd contains the kernel virtual
 * (lowmem) address of the pte page.  The physical address is less useful
 * because everything runs with translation enabled (even the TLB miss
 * handler).  On everything else the pmd contains the physical address
 * of the pte page.  -- paulus
 */
#ifndef CONFIG_BOOKE
#define pmd_page(pmd)		\
	pfn_to_page(pmd_val(pmd) >> PAGE_SHIFT)
#else
#define pmd_page_vaddr(pmd)	\
	((unsigned long)(pmd_val(pmd) & ~(PTE_TABLE_SIZE - 1)))
#define pmd_page(pmd)		\
	pfn_to_page((__pa(pmd_val(pmd)) >> PAGE_SHIFT))
#endif

/*
 * Encode and decode a swap entry.
 * Note that the bits we use in a PTE for representing a swap entry
 * must not include the _PAGE_PRESENT bit.
 *   -- paulus
 */
#define __swp_type(entry)		((entry).val & 0x1f)
#define __swp_offset(entry)		((entry).val >> 5)
#define __swp_entry(type, offset)	((swp_entry_t) { (type) | ((offset) << 5) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) >> 3 })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val << 3 })

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_POWERPC_NOHASH_32_PGTABLE_H */
