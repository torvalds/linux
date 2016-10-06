/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 Ralf Baechle
 */
#ifndef _ASM_PGTABLE_H
#define _ASM_PGTABLE_H

#include <linux/mm_types.h>
#include <linux/mmzone.h>
#ifdef CONFIG_32BIT
#include <asm/pgtable-32.h>
#endif
#ifdef CONFIG_64BIT
#include <asm/pgtable-64.h>
#endif

#include <asm/io.h>
#include <asm/pgtable-bits.h>

struct mm_struct;
struct vm_area_struct;

#define PAGE_NONE	__pgprot(_PAGE_PRESENT | _PAGE_NO_READ | \
				 _page_cachable_default)
#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_WRITE | \
				 _page_cachable_default)
#define PAGE_COPY	__pgprot(_PAGE_PRESENT | _PAGE_NO_EXEC | \
				 _page_cachable_default)
#define PAGE_READONLY	__pgprot(_PAGE_PRESENT | \
				 _page_cachable_default)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | __READABLE | __WRITEABLE | \
				 _PAGE_GLOBAL | _page_cachable_default)
#define PAGE_KERNEL_NC	__pgprot(_PAGE_PRESENT | __READABLE | __WRITEABLE | \
				 _PAGE_GLOBAL | _CACHE_CACHABLE_NONCOHERENT)
#define PAGE_USERIO	__pgprot(_PAGE_PRESENT | _PAGE_WRITE | \
				 _page_cachable_default)
#define PAGE_KERNEL_UNCACHED __pgprot(_PAGE_PRESENT | __READABLE | \
			__WRITEABLE | _PAGE_GLOBAL | _CACHE_UNCACHED)

/*
 * If _PAGE_NO_EXEC is not defined, we can't do page protection for
 * execute, and consider it to be the same as read. Also, write
 * permissions imply read permissions. This is the closest we can get
 * by reasonable means..
 */

/*
 * Dummy values to fill the table in mmap.c
 * The real values will be generated at runtime
 */
#define __P000 __pgprot(0)
#define __P001 __pgprot(0)
#define __P010 __pgprot(0)
#define __P011 __pgprot(0)
#define __P100 __pgprot(0)
#define __P101 __pgprot(0)
#define __P110 __pgprot(0)
#define __P111 __pgprot(0)

#define __S000 __pgprot(0)
#define __S001 __pgprot(0)
#define __S010 __pgprot(0)
#define __S011 __pgprot(0)
#define __S100 __pgprot(0)
#define __S101 __pgprot(0)
#define __S110 __pgprot(0)
#define __S111 __pgprot(0)

extern unsigned long _page_cachable_default;

/*
 * ZERO_PAGE is a global shared page that is always zero; used
 * for zero-mapped memory areas etc..
 */

extern unsigned long empty_zero_page;
extern unsigned long zero_page_mask;

#define ZERO_PAGE(vaddr) \
	(virt_to_page((void *)(empty_zero_page + (((unsigned long)(vaddr)) & zero_page_mask))))
#define __HAVE_COLOR_ZERO_PAGE

extern void paging_init(void);

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define pmd_phys(pmd)		virt_to_phys((void *)pmd_val(pmd))

#define __pmd_page(pmd)		(pfn_to_page(pmd_phys(pmd) >> PAGE_SHIFT))
#ifndef CONFIG_TRANSPARENT_HUGEPAGE
#define pmd_page(pmd)		__pmd_page(pmd)
#endif /* CONFIG_TRANSPARENT_HUGEPAGE  */

#define pmd_page_vaddr(pmd)	pmd_val(pmd)

#define htw_stop()							\
do {									\
	unsigned long flags;						\
									\
	if (cpu_has_htw) {						\
		local_irq_save(flags);					\
		if(!raw_current_cpu_data.htw_seq++) {			\
			write_c0_pwctl(read_c0_pwctl() &		\
				       ~(1 << MIPS_PWCTL_PWEN_SHIFT));	\
			back_to_back_c0_hazard();			\
		}							\
		local_irq_restore(flags);				\
	}								\
} while(0)

#define htw_start()							\
do {									\
	unsigned long flags;						\
									\
	if (cpu_has_htw) {						\
		local_irq_save(flags);					\
		if (!--raw_current_cpu_data.htw_seq) {			\
			write_c0_pwctl(read_c0_pwctl() |		\
				       (1 << MIPS_PWCTL_PWEN_SHIFT));	\
			back_to_back_c0_hazard();			\
		}							\
		local_irq_restore(flags);				\
	}								\
} while(0)

static inline void set_pte_at(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, pte_t pteval);

#if defined(CONFIG_PHYS_ADDR_T_64BIT) && defined(CONFIG_CPU_MIPS32)

#ifdef CONFIG_XPA
# define pte_none(pte)		(!(((pte).pte_high) & ~_PAGE_GLOBAL))
#else
# define pte_none(pte)		(!(((pte).pte_low | (pte).pte_high) & ~_PAGE_GLOBAL))
#endif

#define pte_present(pte)	((pte).pte_low & _PAGE_PRESENT)
#define pte_no_exec(pte)	((pte).pte_low & _PAGE_NO_EXEC)

static inline void set_pte(pte_t *ptep, pte_t pte)
{
	ptep->pte_high = pte.pte_high;
	smp_wmb();
	ptep->pte_low = pte.pte_low;

#ifdef CONFIG_XPA
	if (pte.pte_high & _PAGE_GLOBAL) {
#else
	if (pte.pte_low & _PAGE_GLOBAL) {
#endif
		pte_t *buddy = ptep_buddy(ptep);
		/*
		 * Make sure the buddy is global too (if it's !none,
		 * it better already be global)
		 */
		if (pte_none(*buddy)) {
			if (!IS_ENABLED(CONFIG_XPA))
				buddy->pte_low |= _PAGE_GLOBAL;
			buddy->pte_high |= _PAGE_GLOBAL;
		}
	}
}

static inline void pte_clear(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	pte_t null = __pte(0);

	htw_stop();
	/* Preserve global status for the pair */
	if (IS_ENABLED(CONFIG_XPA)) {
		if (ptep_buddy(ptep)->pte_high & _PAGE_GLOBAL)
			null.pte_high = _PAGE_GLOBAL;
	} else {
		if (ptep_buddy(ptep)->pte_low & _PAGE_GLOBAL)
			null.pte_low = null.pte_high = _PAGE_GLOBAL;
	}

	set_pte_at(mm, addr, ptep, null);
	htw_start();
}
#else

#define pte_none(pte)		(!(pte_val(pte) & ~_PAGE_GLOBAL))
#define pte_present(pte)	(pte_val(pte) & _PAGE_PRESENT)
#define pte_no_exec(pte)	(pte_val(pte) & _PAGE_NO_EXEC)

/*
 * Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
static inline void set_pte(pte_t *ptep, pte_t pteval)
{
	*ptep = pteval;
#if !defined(CONFIG_CPU_R3000) && !defined(CONFIG_CPU_TX39XX)
	if (pte_val(pteval) & _PAGE_GLOBAL) {
		pte_t *buddy = ptep_buddy(ptep);
		/*
		 * Make sure the buddy is global too (if it's !none,
		 * it better already be global)
		 */
#ifdef CONFIG_SMP
		/*
		 * For SMP, multiple CPUs can race, so we need to do
		 * this atomically.
		 */
		unsigned long page_global = _PAGE_GLOBAL;
		unsigned long tmp;

		if (kernel_uses_llsc && R10000_LLSC_WAR) {
			__asm__ __volatile__ (
			"	.set	arch=r4000			\n"
			"	.set	push				\n"
			"	.set	noreorder			\n"
			"1:"	__LL	"%[tmp], %[buddy]		\n"
			"	bnez	%[tmp], 2f			\n"
			"	 or	%[tmp], %[tmp], %[global]	\n"
				__SC	"%[tmp], %[buddy]		\n"
			"	beqzl	%[tmp], 1b			\n"
			"	nop					\n"
			"2:						\n"
			"	.set	pop				\n"
			"	.set	mips0				\n"
			: [buddy] "+m" (buddy->pte), [tmp] "=&r" (tmp)
			: [global] "r" (page_global));
		} else if (kernel_uses_llsc) {
			__asm__ __volatile__ (
			"	.set	"MIPS_ISA_ARCH_LEVEL"		\n"
			"	.set	push				\n"
			"	.set	noreorder			\n"
			"1:"	__LL	"%[tmp], %[buddy]		\n"
			"	bnez	%[tmp], 2f			\n"
			"	 or	%[tmp], %[tmp], %[global]	\n"
				__SC	"%[tmp], %[buddy]		\n"
			"	beqz	%[tmp], 1b			\n"
			"	nop					\n"
			"2:						\n"
			"	.set	pop				\n"
			"	.set	mips0				\n"
			: [buddy] "+m" (buddy->pte), [tmp] "=&r" (tmp)
			: [global] "r" (page_global));
		}
#else /* !CONFIG_SMP */
		if (pte_none(*buddy))
			pte_val(*buddy) = pte_val(*buddy) | _PAGE_GLOBAL;
#endif /* CONFIG_SMP */
	}
#endif
}

static inline void pte_clear(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	htw_stop();
#if !defined(CONFIG_CPU_R3000) && !defined(CONFIG_CPU_TX39XX)
	/* Preserve global status for the pair */
	if (pte_val(*ptep_buddy(ptep)) & _PAGE_GLOBAL)
		set_pte_at(mm, addr, ptep, __pte(_PAGE_GLOBAL));
	else
#endif
		set_pte_at(mm, addr, ptep, __pte(0));
	htw_start();
}
#endif

static inline void set_pte_at(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, pte_t pteval)
{
	extern void __update_cache(unsigned long address, pte_t pte);

	if (!pte_present(pteval))
		goto cache_sync_done;

	if (pte_present(*ptep) && (pte_pfn(*ptep) == pte_pfn(pteval)))
		goto cache_sync_done;

	__update_cache(addr, pteval);
cache_sync_done:
	set_pte(ptep, pteval);
}

/*
 * (pmds are folded into puds so this doesn't get actually called,
 * but the define is needed for a generic inline function.)
 */
#define set_pmd(pmdptr, pmdval) do { *(pmdptr) = (pmdval); } while(0)

#ifndef __PAGETABLE_PMD_FOLDED
/*
 * (puds are folded into pgds so this doesn't get actually called,
 * but the define is needed for a generic inline function.)
 */
#define set_pud(pudptr, pudval) do { *(pudptr) = (pudval); } while(0)
#endif

#define PGD_T_LOG2	(__builtin_ffs(sizeof(pgd_t)) - 1)
#define PMD_T_LOG2	(__builtin_ffs(sizeof(pmd_t)) - 1)
#define PTE_T_LOG2	(__builtin_ffs(sizeof(pte_t)) - 1)

/*
 * We used to declare this array with size but gcc 3.3 and older are not able
 * to find that this expression is a constant, so the size is dropped.
 */
extern pgd_t swapper_pg_dir[];

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
#if defined(CONFIG_PHYS_ADDR_T_64BIT) && defined(CONFIG_CPU_MIPS32)
static inline int pte_write(pte_t pte)	{ return pte.pte_low & _PAGE_WRITE; }
static inline int pte_dirty(pte_t pte)	{ return pte.pte_low & _PAGE_MODIFIED; }
static inline int pte_young(pte_t pte)	{ return pte.pte_low & _PAGE_ACCESSED; }

static inline pte_t pte_wrprotect(pte_t pte)
{
	pte.pte_low  &= ~_PAGE_WRITE;
	if (!IS_ENABLED(CONFIG_XPA))
		pte.pte_low &= ~_PAGE_SILENT_WRITE;
	pte.pte_high &= ~_PAGE_SILENT_WRITE;
	return pte;
}

static inline pte_t pte_mkclean(pte_t pte)
{
	pte.pte_low  &= ~_PAGE_MODIFIED;
	if (!IS_ENABLED(CONFIG_XPA))
		pte.pte_low &= ~_PAGE_SILENT_WRITE;
	pte.pte_high &= ~_PAGE_SILENT_WRITE;
	return pte;
}

static inline pte_t pte_mkold(pte_t pte)
{
	pte.pte_low  &= ~_PAGE_ACCESSED;
	if (!IS_ENABLED(CONFIG_XPA))
		pte.pte_low &= ~_PAGE_SILENT_READ;
	pte.pte_high &= ~_PAGE_SILENT_READ;
	return pte;
}

static inline pte_t pte_mkwrite(pte_t pte)
{
	pte.pte_low |= _PAGE_WRITE;
	if (pte.pte_low & _PAGE_MODIFIED) {
		if (!IS_ENABLED(CONFIG_XPA))
			pte.pte_low |= _PAGE_SILENT_WRITE;
		pte.pte_high |= _PAGE_SILENT_WRITE;
	}
	return pte;
}

static inline pte_t pte_mkdirty(pte_t pte)
{
	pte.pte_low |= _PAGE_MODIFIED;
	if (pte.pte_low & _PAGE_WRITE) {
		if (!IS_ENABLED(CONFIG_XPA))
			pte.pte_low |= _PAGE_SILENT_WRITE;
		pte.pte_high |= _PAGE_SILENT_WRITE;
	}
	return pte;
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	pte.pte_low |= _PAGE_ACCESSED;
	if (!(pte.pte_low & _PAGE_NO_READ)) {
		if (!IS_ENABLED(CONFIG_XPA))
			pte.pte_low |= _PAGE_SILENT_READ;
		pte.pte_high |= _PAGE_SILENT_READ;
	}
	return pte;
}
#else
static inline int pte_write(pte_t pte)	{ return pte_val(pte) & _PAGE_WRITE; }
static inline int pte_dirty(pte_t pte)	{ return pte_val(pte) & _PAGE_MODIFIED; }
static inline int pte_young(pte_t pte)	{ return pte_val(pte) & _PAGE_ACCESSED; }

static inline pte_t pte_wrprotect(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_WRITE | _PAGE_SILENT_WRITE);
	return pte;
}

static inline pte_t pte_mkclean(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_MODIFIED | _PAGE_SILENT_WRITE);
	return pte;
}

static inline pte_t pte_mkold(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_ACCESSED | _PAGE_SILENT_READ);
	return pte;
}

static inline pte_t pte_mkwrite(pte_t pte)
{
	pte_val(pte) |= _PAGE_WRITE;
	if (pte_val(pte) & _PAGE_MODIFIED)
		pte_val(pte) |= _PAGE_SILENT_WRITE;
	return pte;
}

static inline pte_t pte_mkdirty(pte_t pte)
{
	pte_val(pte) |= _PAGE_MODIFIED;
	if (pte_val(pte) & _PAGE_WRITE)
		pte_val(pte) |= _PAGE_SILENT_WRITE;
	return pte;
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	pte_val(pte) |= _PAGE_ACCESSED;
	if (!(pte_val(pte) & _PAGE_NO_READ))
		pte_val(pte) |= _PAGE_SILENT_READ;
	return pte;
}

#ifdef CONFIG_MIPS_HUGE_TLB_SUPPORT
static inline int pte_huge(pte_t pte)	{ return pte_val(pte) & _PAGE_HUGE; }

static inline pte_t pte_mkhuge(pte_t pte)
{
	pte_val(pte) |= _PAGE_HUGE;
	return pte;
}
#endif /* CONFIG_MIPS_HUGE_TLB_SUPPORT */
#endif
static inline int pte_special(pte_t pte)	{ return 0; }
static inline pte_t pte_mkspecial(pte_t pte)	{ return pte; }

/*
 * Macro to make mark a page protection value as "uncacheable".	 Note
 * that "protection" is really a misnomer here as the protection value
 * contains the memory attribute bits, dirty bits, and various other
 * bits as well.
 */
#define pgprot_noncached pgprot_noncached

static inline pgprot_t pgprot_noncached(pgprot_t _prot)
{
	unsigned long prot = pgprot_val(_prot);

	prot = (prot & ~_CACHE_MASK) | _CACHE_UNCACHED;

	return __pgprot(prot);
}

#define pgprot_writecombine pgprot_writecombine

static inline pgprot_t pgprot_writecombine(pgprot_t _prot)
{
	unsigned long prot = pgprot_val(_prot);

	/* cpu_data[0].writecombine is already shifted by _CACHE_SHIFT */
	prot = (prot & ~_CACHE_MASK) | cpu_data[0].writecombine;

	return __pgprot(prot);
}

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define mk_pte(page, pgprot)	pfn_pte(page_to_pfn(page), (pgprot))

#if defined(CONFIG_XPA)
static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	pte.pte_low  &= (_PAGE_MODIFIED | _PAGE_ACCESSED | _PFNX_MASK);
	pte.pte_high &= (_PFN_MASK | _CACHE_MASK);
	pte.pte_low  |= pgprot_val(newprot) & ~_PFNX_MASK;
	pte.pte_high |= pgprot_val(newprot) & ~(_PFN_MASK | _CACHE_MASK);
	return pte;
}
#elif defined(CONFIG_PHYS_ADDR_T_64BIT) && defined(CONFIG_CPU_MIPS32)
static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	pte.pte_low  &= _PAGE_CHG_MASK;
	pte.pte_high &= (_PFN_MASK | _CACHE_MASK);
	pte.pte_low  |= pgprot_val(newprot);
	pte.pte_high |= pgprot_val(newprot) & ~(_PFN_MASK | _CACHE_MASK);
	return pte;
}
#else
static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & _PAGE_CHG_MASK) |
		     (pgprot_val(newprot) & ~_PAGE_CHG_MASK));
}
#endif


extern void __update_tlb(struct vm_area_struct *vma, unsigned long address,
	pte_t pte);

static inline void update_mmu_cache(struct vm_area_struct *vma,
	unsigned long address, pte_t *ptep)
{
	pte_t pte = *ptep;
	__update_tlb(vma, address, pte);
}

static inline void update_mmu_cache_pmd(struct vm_area_struct *vma,
	unsigned long address, pmd_t *pmdp)
{
	pte_t pte = *(pte_t *)pmdp;

	__update_tlb(vma, address, pte);
}

#define kern_addr_valid(addr)	(1)

#ifdef CONFIG_PHYS_ADDR_T_64BIT
extern int remap_pfn_range(struct vm_area_struct *vma, unsigned long from, unsigned long pfn, unsigned long size, pgprot_t prot);

static inline int io_remap_pfn_range(struct vm_area_struct *vma,
		unsigned long vaddr,
		unsigned long pfn,
		unsigned long size,
		pgprot_t prot)
{
	phys_addr_t phys_addr_high = fixup_bigphys_addr(pfn << PAGE_SHIFT, size);
	return remap_pfn_range(vma, vaddr, phys_addr_high >> PAGE_SHIFT, size, prot);
}
#define io_remap_pfn_range io_remap_pfn_range
#endif

#ifdef CONFIG_TRANSPARENT_HUGEPAGE

#define has_transparent_hugepage has_transparent_hugepage
extern int has_transparent_hugepage(void);

static inline int pmd_trans_huge(pmd_t pmd)
{
	return !!(pmd_val(pmd) & _PAGE_HUGE);
}

static inline pmd_t pmd_mkhuge(pmd_t pmd)
{
	pmd_val(pmd) |= _PAGE_HUGE;

	return pmd;
}

extern void set_pmd_at(struct mm_struct *mm, unsigned long addr,
		       pmd_t *pmdp, pmd_t pmd);

#define __HAVE_ARCH_PMD_WRITE
static inline int pmd_write(pmd_t pmd)
{
	return !!(pmd_val(pmd) & _PAGE_WRITE);
}

static inline pmd_t pmd_wrprotect(pmd_t pmd)
{
	pmd_val(pmd) &= ~(_PAGE_WRITE | _PAGE_SILENT_WRITE);
	return pmd;
}

static inline pmd_t pmd_mkwrite(pmd_t pmd)
{
	pmd_val(pmd) |= _PAGE_WRITE;
	if (pmd_val(pmd) & _PAGE_MODIFIED)
		pmd_val(pmd) |= _PAGE_SILENT_WRITE;

	return pmd;
}

static inline int pmd_dirty(pmd_t pmd)
{
	return !!(pmd_val(pmd) & _PAGE_MODIFIED);
}

static inline pmd_t pmd_mkclean(pmd_t pmd)
{
	pmd_val(pmd) &= ~(_PAGE_MODIFIED | _PAGE_SILENT_WRITE);
	return pmd;
}

static inline pmd_t pmd_mkdirty(pmd_t pmd)
{
	pmd_val(pmd) |= _PAGE_MODIFIED;
	if (pmd_val(pmd) & _PAGE_WRITE)
		pmd_val(pmd) |= _PAGE_SILENT_WRITE;

	return pmd;
}

static inline int pmd_young(pmd_t pmd)
{
	return !!(pmd_val(pmd) & _PAGE_ACCESSED);
}

static inline pmd_t pmd_mkold(pmd_t pmd)
{
	pmd_val(pmd) &= ~(_PAGE_ACCESSED|_PAGE_SILENT_READ);

	return pmd;
}

static inline pmd_t pmd_mkyoung(pmd_t pmd)
{
	pmd_val(pmd) |= _PAGE_ACCESSED;

	if (!(pmd_val(pmd) & _PAGE_NO_READ))
		pmd_val(pmd) |= _PAGE_SILENT_READ;

	return pmd;
}

/* Extern to avoid header file madness */
extern pmd_t mk_pmd(struct page *page, pgprot_t prot);

static inline unsigned long pmd_pfn(pmd_t pmd)
{
	return pmd_val(pmd) >> _PFN_SHIFT;
}

static inline struct page *pmd_page(pmd_t pmd)
{
	if (pmd_trans_huge(pmd))
		return pfn_to_page(pmd_pfn(pmd));

	return pfn_to_page(pmd_phys(pmd) >> PAGE_SHIFT);
}

static inline pmd_t pmd_modify(pmd_t pmd, pgprot_t newprot)
{
	pmd_val(pmd) = (pmd_val(pmd) & (_PAGE_CHG_MASK | _PAGE_HUGE)) |
		       (pgprot_val(newprot) & ~_PAGE_CHG_MASK);
	return pmd;
}

static inline pmd_t pmd_mknotpresent(pmd_t pmd)
{
	pmd_val(pmd) &= ~(_PAGE_PRESENT | _PAGE_VALID | _PAGE_DIRTY);

	return pmd;
}

/*
 * The generic version pmdp_huge_get_and_clear uses a version of pmd_clear() with a
 * different prototype.
 */
#define __HAVE_ARCH_PMDP_HUGE_GET_AND_CLEAR
static inline pmd_t pmdp_huge_get_and_clear(struct mm_struct *mm,
					    unsigned long address, pmd_t *pmdp)
{
	pmd_t old = *pmdp;

	pmd_clear(pmdp);

	return old;
}

#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

#include <asm-generic/pgtable.h>

/*
 * uncached accelerated TLB map for video memory access
 */
#ifdef CONFIG_CPU_SUPPORTS_UNCACHED_ACCELERATED
#define __HAVE_PHYS_MEM_ACCESS_PROT

struct file;
pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
		unsigned long size, pgprot_t vma_prot);
int phys_mem_access_prot_allowed(struct file *file, unsigned long pfn,
		unsigned long size, pgprot_t *vma_prot);
#endif

/*
 * We provide our own get_unmapped area to cope with the virtual aliasing
 * constraints placed on us by the cache architecture.
 */
#define HAVE_ARCH_UNMAPPED_AREA
#define HAVE_ARCH_UNMAPPED_AREA_TOPDOWN

/*
 * No page table caches to initialise
 */
#define pgtable_cache_init()	do { } while (0)

#endif /* _ASM_PGTABLE_H */
