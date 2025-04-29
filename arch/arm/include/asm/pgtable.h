/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/include/asm/pgtable.h
 *
 *  Copyright (C) 1995-2002 Russell King
 */
#ifndef _ASMARM_PGTABLE_H
#define _ASMARM_PGTABLE_H

#include <linux/const.h>
#include <asm/proc-fns.h>

#ifndef __ASSEMBLY__
/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern struct page *empty_zero_page;
#define ZERO_PAGE(vaddr)	(empty_zero_page)
#endif

#include <asm-generic/pgtable-nopud.h>

#ifndef CONFIG_MMU
#include <asm/pgtable-nommu.h>

#else

#include <asm/page.h>
#include <asm/pgtable-hwdef.h>


#include <asm/tlbflush.h>

#ifdef CONFIG_ARM_LPAE
#include <asm/pgtable-3level.h>
#else
#include <asm/pgtable-2level.h>
#endif

/*
 * Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 */
#define VMALLOC_OFFSET		(8*1024*1024)
#define VMALLOC_START		(((unsigned long)high_memory + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1))
#define VMALLOC_END		0xff800000UL

#define LIBRARY_TEXT_START	0x0c000000

#ifndef __ASSEMBLY__
extern void __pte_error(const char *file, int line, pte_t);
extern void __pmd_error(const char *file, int line, pmd_t);
extern void __pgd_error(const char *file, int line, pgd_t);

#define pte_ERROR(pte)		__pte_error(__FILE__, __LINE__, pte)
#define pmd_ERROR(pmd)		__pmd_error(__FILE__, __LINE__, pmd)
#define pgd_ERROR(pgd)		__pgd_error(__FILE__, __LINE__, pgd)

/*
 * This is the lowest virtual address we can permit any user space
 * mapping to be mapped at.  This is particularly important for
 * non-high vector CPUs.
 */
#define FIRST_USER_ADDRESS	(PAGE_SIZE * 2)

/*
 * Use TASK_SIZE as the ceiling argument for free_pgtables() and
 * free_pgd_range() to avoid freeing the modules pmd when LPAE is enabled (pmd
 * page shared between user and kernel).
 */
#ifdef CONFIG_ARM_LPAE
#define USER_PGTABLES_CEILING	TASK_SIZE
#endif

/*
 * The pgprot_* and protection_map entries will be fixed up in runtime
 * to include the cachable and bufferable bits based on memory policy,
 * as well as any architecture dependent bits like global/ASID and SMP
 * shared mapping bits.
 */
#define _L_PTE_DEFAULT	L_PTE_PRESENT | L_PTE_YOUNG

extern pgprot_t		pgprot_user;
extern pgprot_t		pgprot_kernel;

#define _MOD_PROT(p, b)	__pgprot(pgprot_val(p) | (b))

#define PAGE_NONE		_MOD_PROT(pgprot_user, L_PTE_XN | L_PTE_RDONLY | L_PTE_NONE)
#define PAGE_SHARED		_MOD_PROT(pgprot_user, L_PTE_USER | L_PTE_XN)
#define PAGE_SHARED_EXEC	_MOD_PROT(pgprot_user, L_PTE_USER)
#define PAGE_COPY		_MOD_PROT(pgprot_user, L_PTE_USER | L_PTE_RDONLY | L_PTE_XN)
#define PAGE_COPY_EXEC		_MOD_PROT(pgprot_user, L_PTE_USER | L_PTE_RDONLY)
#define PAGE_READONLY		_MOD_PROT(pgprot_user, L_PTE_USER | L_PTE_RDONLY | L_PTE_XN)
#define PAGE_READONLY_EXEC	_MOD_PROT(pgprot_user, L_PTE_USER | L_PTE_RDONLY)
#define PAGE_KERNEL		_MOD_PROT(pgprot_kernel, L_PTE_XN)
#define PAGE_KERNEL_EXEC	pgprot_kernel

#define __PAGE_NONE		__pgprot(_L_PTE_DEFAULT | L_PTE_RDONLY | L_PTE_XN | L_PTE_NONE)
#define __PAGE_SHARED		__pgprot(_L_PTE_DEFAULT | L_PTE_USER | L_PTE_XN)
#define __PAGE_SHARED_EXEC	__pgprot(_L_PTE_DEFAULT | L_PTE_USER)
#define __PAGE_COPY		__pgprot(_L_PTE_DEFAULT | L_PTE_USER | L_PTE_RDONLY | L_PTE_XN)
#define __PAGE_COPY_EXEC	__pgprot(_L_PTE_DEFAULT | L_PTE_USER | L_PTE_RDONLY)
#define __PAGE_READONLY		__pgprot(_L_PTE_DEFAULT | L_PTE_USER | L_PTE_RDONLY | L_PTE_XN)
#define __PAGE_READONLY_EXEC	__pgprot(_L_PTE_DEFAULT | L_PTE_USER | L_PTE_RDONLY)

#define __pgprot_modify(prot,mask,bits)		\
	__pgprot((pgprot_val(prot) & ~(mask)) | (bits))

#define pgprot_noncached(prot) \
	__pgprot_modify(prot, L_PTE_MT_MASK, L_PTE_MT_UNCACHED)

#define pgprot_writecombine(prot) \
	__pgprot_modify(prot, L_PTE_MT_MASK, L_PTE_MT_BUFFERABLE)

#define pgprot_stronglyordered(prot) \
	__pgprot_modify(prot, L_PTE_MT_MASK, L_PTE_MT_UNCACHED)

#define pgprot_device(prot) \
	__pgprot_modify(prot, L_PTE_MT_MASK, L_PTE_MT_DEV_SHARED | L_PTE_SHARED | L_PTE_DIRTY | L_PTE_XN)

#ifdef CONFIG_ARM_DMA_MEM_BUFFERABLE
#define pgprot_dmacoherent(prot) \
	__pgprot_modify(prot, L_PTE_MT_MASK, L_PTE_MT_BUFFERABLE | L_PTE_XN)
#define __HAVE_PHYS_MEM_ACCESS_PROT
struct file;
extern pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
				     unsigned long size, pgprot_t vma_prot);
#else
#define pgprot_dmacoherent(prot) \
	__pgprot_modify(prot, L_PTE_MT_MASK, L_PTE_MT_UNCACHED | L_PTE_XN)
#endif

#endif /* __ASSEMBLY__ */

/*
 * The table below defines the page protection levels that we insert into our
 * Linux page table version.  These get translated into the best that the
 * architecture can perform.  Note that on most ARM hardware:
 *  1) We cannot do execute protection
 *  2) If we could do execute protection, then read is implied
 *  3) write implies read permissions
 */

#ifndef __ASSEMBLY__

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

#define pgdp_get(pgpd)		READ_ONCE(*pgdp)

#define pud_page(pud)		pmd_page(__pmd(pud_val(pud)))
#define pud_write(pud)		pmd_write(__pmd(pud_val(pud)))

#define pmd_none(pmd)		(!pmd_val(pmd))

static inline pte_t *pmd_page_vaddr(pmd_t pmd)
{
	return __va(pmd_val(pmd) & PHYS_MASK & (s32)PAGE_MASK);
}

#define pmd_page(pmd)		pfn_to_page(__phys_to_pfn(pmd_val(pmd) & PHYS_MASK))

#define pte_pfn(pte)		((pte_val(pte) & PHYS_MASK) >> PAGE_SHIFT)
#define pfn_pte(pfn,prot)	__pte(__pfn_to_phys(pfn) | pgprot_val(prot))

#define pte_page(pte)		pfn_to_page(pte_pfn(pte))
#define mk_pte(page,prot)	pfn_pte(page_to_pfn(page), prot)

#define pte_clear(mm,addr,ptep)	set_pte_ext(ptep, __pte(0), 0)

#define pte_isset(pte, val)	((u32)(val) == (val) ? pte_val(pte) & (val) \
						: !!(pte_val(pte) & (val)))
#define pte_isclear(pte, val)	(!(pte_val(pte) & (val)))

#define pte_none(pte)		(!pte_val(pte))
#define pte_present(pte)	(pte_isset((pte), L_PTE_PRESENT))
#define pte_valid(pte)		(pte_isset((pte), L_PTE_VALID))
#define pte_accessible(mm, pte)	(mm_tlb_flush_pending(mm) ? pte_present(pte) : pte_valid(pte))
#define pte_write(pte)		(pte_isclear((pte), L_PTE_RDONLY))
#define pte_dirty(pte)		(pte_isset((pte), L_PTE_DIRTY))
#define pte_young(pte)		(pte_isset((pte), L_PTE_YOUNG))
#define pte_exec(pte)		(pte_isclear((pte), L_PTE_XN))

#define pte_valid_user(pte)	\
	(pte_valid(pte) && pte_isset((pte), L_PTE_USER) && pte_young(pte))

static inline bool pte_access_permitted(pte_t pte, bool write)
{
	pteval_t mask = L_PTE_PRESENT | L_PTE_USER;
	pteval_t needed = mask;

	if (write)
		mask |= L_PTE_RDONLY;

	return (pte_val(pte) & mask) == needed;
}
#define pte_access_permitted pte_access_permitted

#if __LINUX_ARM_ARCH__ < 6
static inline void __sync_icache_dcache(pte_t pteval)
{
}
#else
extern void __sync_icache_dcache(pte_t pteval);
#endif

#define PFN_PTE_SHIFT		PAGE_SHIFT

void set_ptes(struct mm_struct *mm, unsigned long addr,
		      pte_t *ptep, pte_t pteval, unsigned int nr);
#define set_ptes set_ptes

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
	return set_pte_bit(pte, __pgprot(L_PTE_RDONLY));
}

static inline pte_t pte_mkwrite_novma(pte_t pte)
{
	return clear_pte_bit(pte, __pgprot(L_PTE_RDONLY));
}

static inline pte_t pte_mkclean(pte_t pte)
{
	return clear_pte_bit(pte, __pgprot(L_PTE_DIRTY));
}

static inline pte_t pte_mkdirty(pte_t pte)
{
	return set_pte_bit(pte, __pgprot(L_PTE_DIRTY));
}

static inline pte_t pte_mkold(pte_t pte)
{
	return clear_pte_bit(pte, __pgprot(L_PTE_YOUNG));
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	return set_pte_bit(pte, __pgprot(L_PTE_YOUNG));
}

static inline pte_t pte_mkexec(pte_t pte)
{
	return clear_pte_bit(pte, __pgprot(L_PTE_XN));
}

static inline pte_t pte_mknexec(pte_t pte)
{
	return set_pte_bit(pte, __pgprot(L_PTE_XN));
}

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	const pteval_t mask = L_PTE_XN | L_PTE_RDONLY | L_PTE_USER |
		L_PTE_NONE | L_PTE_VALID;
	pte_val(pte) = (pte_val(pte) & ~mask) | (pgprot_val(newprot) & mask);
	return pte;
}

/*
 * Encode/decode swap entries and swap PTEs. Swap PTEs are all PTEs that
 * are !pte_none() && !pte_present().
 *
 * Format of swap PTEs:
 *
 *   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
 *   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *   <------------------- offset ------------------> E < type -> 0 0
 *
 *   E is the exclusive marker that is not stored in swap entries.
 *
 * This gives us up to 31 swap files and 64GB per swap file.  Note that
 * the offset field is always non-zero.
 */
#define __SWP_TYPE_SHIFT	2
#define __SWP_TYPE_BITS		5
#define __SWP_TYPE_MASK		((1 << __SWP_TYPE_BITS) - 1)
#define __SWP_OFFSET_SHIFT	(__SWP_TYPE_BITS + __SWP_TYPE_SHIFT + 1)

#define __swp_type(x)		(((x).val >> __SWP_TYPE_SHIFT) & __SWP_TYPE_MASK)
#define __swp_offset(x)		((x).val >> __SWP_OFFSET_SHIFT)
#define __swp_entry(type, offset) ((swp_entry_t) { (((type) & __SWP_TYPE_MASK) << __SWP_TYPE_SHIFT) | \
						   ((offset) << __SWP_OFFSET_SHIFT) })

#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(swp)	__pte((swp).val)

static inline int pte_swp_exclusive(pte_t pte)
{
	return pte_isset(pte, L_PTE_SWP_EXCLUSIVE);
}

static inline pte_t pte_swp_mkexclusive(pte_t pte)
{
	return set_pte_bit(pte, __pgprot(L_PTE_SWP_EXCLUSIVE));
}

static inline pte_t pte_swp_clear_exclusive(pte_t pte)
{
	return clear_pte_bit(pte, __pgprot(L_PTE_SWP_EXCLUSIVE));
}

/*
 * It is an error for the kernel to have more swap files than we can
 * encode in the PTEs.  This ensures that we know when MAX_SWAPFILES
 * is increased beyond what we presently support.
 */
#define MAX_SWAPFILES_CHECK() BUILD_BUG_ON(MAX_SWAPFILES_SHIFT > __SWP_TYPE_BITS)

/*
 * We provide our own arch_get_unmapped_area to cope with VIPT caches.
 */
#define HAVE_ARCH_UNMAPPED_AREA
#define HAVE_ARCH_UNMAPPED_AREA_TOPDOWN

#endif /* !__ASSEMBLY__ */

#endif /* CONFIG_MMU */

#endif /* _ASMARM_PGTABLE_H */
