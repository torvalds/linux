/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_NOHASH_PGTABLE_H
#define _ASM_POWERPC_NOHASH_PGTABLE_H

#if defined(CONFIG_PPC64)
#include <asm/nohash/64/pgtable.h>
#else
#include <asm/nohash/32/pgtable.h>
#endif

/* Permission masks used for kernel mappings */
#define PAGE_KERNEL	__pgprot(_PAGE_BASE | _PAGE_KERNEL_RW)
#define PAGE_KERNEL_NC	__pgprot(_PAGE_BASE_NC | _PAGE_KERNEL_RW | _PAGE_NO_CACHE)
#define PAGE_KERNEL_NCG	__pgprot(_PAGE_BASE_NC | _PAGE_KERNEL_RW | \
				 _PAGE_NO_CACHE | _PAGE_GUARDED)
#define PAGE_KERNEL_X	__pgprot(_PAGE_BASE | _PAGE_KERNEL_RWX)
#define PAGE_KERNEL_RO	__pgprot(_PAGE_BASE | _PAGE_KERNEL_RO)
#define PAGE_KERNEL_ROX	__pgprot(_PAGE_BASE | _PAGE_KERNEL_ROX)

/*
 * Protection used for kernel text. We want the debuggers to be able to
 * set breakpoints anywhere, so don't write protect the kernel text
 * on platforms where such control is possible.
 */
#if defined(CONFIG_KGDB) || defined(CONFIG_XMON) || defined(CONFIG_BDI_SWITCH) ||\
	defined(CONFIG_KPROBES) || defined(CONFIG_DYNAMIC_FTRACE)
#define PAGE_KERNEL_TEXT	PAGE_KERNEL_X
#else
#define PAGE_KERNEL_TEXT	PAGE_KERNEL_ROX
#endif

/* Make modules code happy. We don't set RO yet */
#define PAGE_KERNEL_EXEC	PAGE_KERNEL_X

/* Advertise special mapping type for AGP */
#define PAGE_AGP		(PAGE_KERNEL_NC)
#define HAVE_PAGE_AGP

#ifndef __ASSEMBLY__

/* Generic accessors to PTE bits */
#ifndef pte_write
static inline int pte_write(pte_t pte)
{
	return pte_val(pte) & _PAGE_RW;
}
#endif
static inline int pte_read(pte_t pte)		{ return 1; }
static inline int pte_dirty(pte_t pte)		{ return pte_val(pte) & _PAGE_DIRTY; }
static inline int pte_special(pte_t pte)	{ return pte_val(pte) & _PAGE_SPECIAL; }
static inline int pte_none(pte_t pte)		{ return (pte_val(pte) & ~_PTE_NONE_MASK) == 0; }
static inline bool pte_hashpte(pte_t pte)	{ return false; }
static inline bool pte_ci(pte_t pte)		{ return pte_val(pte) & _PAGE_NO_CACHE; }
static inline bool pte_exec(pte_t pte)		{ return pte_val(pte) & _PAGE_EXEC; }

#ifdef CONFIG_NUMA_BALANCING
/*
 * These work without NUMA balancing but the kernel does not care. See the
 * comment in include/asm-generic/pgtable.h . On powerpc, this will only
 * work for user pages and always return true for kernel pages.
 */
static inline int pte_protnone(pte_t pte)
{
	return pte_present(pte) && !pte_user(pte);
}

static inline int pmd_protnone(pmd_t pmd)
{
	return pte_protnone(pmd_pte(pmd));
}
#endif /* CONFIG_NUMA_BALANCING */

static inline int pte_present(pte_t pte)
{
	return pte_val(pte) & _PAGE_PRESENT;
}

static inline bool pte_hw_valid(pte_t pte)
{
	return pte_val(pte) & _PAGE_PRESENT;
}

/*
 * Don't just check for any non zero bits in __PAGE_USER, since for book3e
 * and PTE_64BIT, PAGE_KERNEL_X contains _PAGE_BAP_SR which is also in
 * _PAGE_USER.  Need to explicitly match _PAGE_BAP_UR bit in that case too.
 */
#ifndef pte_user
static inline bool pte_user(pte_t pte)
{
	return (pte_val(pte) & _PAGE_USER) == _PAGE_USER;
}
#endif

/*
 * We only find page table entry in the last level
 * Hence no need for other accessors
 */
#define pte_access_permitted pte_access_permitted
static inline bool pte_access_permitted(pte_t pte, bool write)
{
	/*
	 * A read-only access is controlled by _PAGE_USER bit.
	 * We have _PAGE_READ set for WRITE and EXECUTE
	 */
	if (!pte_present(pte) || !pte_user(pte) || !pte_read(pte))
		return false;

	if (write && !pte_write(pte))
		return false;

	return true;
}

/* Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 *
 * Even if PTEs can be unsigned long long, a PFN is always an unsigned
 * long for now.
 */
static inline pte_t pfn_pte(unsigned long pfn, pgprot_t pgprot) {
	return __pte(((pte_basic_t)(pfn) << PTE_RPN_SHIFT) |
		     pgprot_val(pgprot)); }
static inline unsigned long pte_pfn(pte_t pte)	{
	return pte_val(pte) >> PTE_RPN_SHIFT; }

/* Generic modifiers for PTE bits */
static inline pte_t pte_exprotect(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_EXEC);
}

#ifndef pte_mkclean
static inline pte_t pte_mkclean(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_DIRTY);
}
#endif

static inline pte_t pte_mkold(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_ACCESSED);
}

static inline pte_t pte_mkpte(pte_t pte)
{
	return pte;
}

static inline pte_t pte_mkspecial(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_SPECIAL);
}

#ifndef pte_mkhuge
static inline pte_t pte_mkhuge(pte_t pte)
{
	return __pte(pte_val(pte));
}
#endif

#ifndef pte_mkprivileged
static inline pte_t pte_mkprivileged(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_USER);
}
#endif

#ifndef pte_mkuser
static inline pte_t pte_mkuser(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_USER);
}
#endif

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot));
}

/* Insert a PTE, top-level function is out of line. It uses an inline
 * low level function in the respective pgtable-* files
 */
extern void set_pte_at(struct mm_struct *mm, unsigned long addr, pte_t *ptep,
		       pte_t pte);

/* This low level function performs the actual PTE insertion
 * Setting the PTE depends on the MMU type and other factors. It's
 * an horrible mess that I'm not going to try to clean up now but
 * I'm keeping it in one place rather than spread around
 */
static inline void __set_pte_at(struct mm_struct *mm, unsigned long addr,
				pte_t *ptep, pte_t pte, int percpu)
{
	/* Second case is 32-bit with 64-bit PTE.  In this case, we
	 * can just store as long as we do the two halves in the right order
	 * with a barrier in between.
	 * In the percpu case, we also fallback to the simple update
	 */
	if (IS_ENABLED(CONFIG_PPC32) && IS_ENABLED(CONFIG_PTE_64BIT) && !percpu) {
		__asm__ __volatile__("\
			stw%U0%X0 %2,%0\n\
			eieio\n\
			stw%U0%X0 %L2,%1"
		: "=m" (*ptep), "=m" (*((unsigned char *)ptep+4))
		: "r" (pte) : "memory");
		return;
	}
	/* Anything else just stores the PTE normally. That covers all 64-bit
	 * cases, and 32-bit non-hash with 32-bit PTEs.
	 */
#if defined(CONFIG_PPC_8xx) && defined(CONFIG_PPC_16K_PAGES)
	ptep->pte = ptep->pte1 = ptep->pte2 = ptep->pte3 = pte_val(pte);
#else
	*ptep = pte;
#endif

	/*
	 * With hardware tablewalk, a sync is needed to ensure that
	 * subsequent accesses see the PTE we just wrote.  Unlike userspace
	 * mappings, we can't tolerate spurious faults, so make sure
	 * the new PTE will be seen the first time.
	 */
	if (IS_ENABLED(CONFIG_PPC_BOOK3E_64) && is_kernel_addr(addr))
		mb();
}


#define __HAVE_ARCH_PTEP_SET_ACCESS_FLAGS
extern int ptep_set_access_flags(struct vm_area_struct *vma, unsigned long address,
				 pte_t *ptep, pte_t entry, int dirty);

/*
 * Macro to mark a page protection value as "uncacheable".
 */

#define _PAGE_CACHE_CTL	(_PAGE_COHERENT | _PAGE_GUARDED | _PAGE_NO_CACHE | \
			 _PAGE_WRITETHRU)

#define pgprot_noncached(prot)	  (__pgprot((pgprot_val(prot) & ~_PAGE_CACHE_CTL) | \
				            _PAGE_NO_CACHE | _PAGE_GUARDED))

#define pgprot_noncached_wc(prot) (__pgprot((pgprot_val(prot) & ~_PAGE_CACHE_CTL) | \
				            _PAGE_NO_CACHE))

#define pgprot_cached(prot)       (__pgprot((pgprot_val(prot) & ~_PAGE_CACHE_CTL) | \
				            _PAGE_COHERENT))

#if _PAGE_WRITETHRU != 0
#define pgprot_cached_wthru(prot) (__pgprot((pgprot_val(prot) & ~_PAGE_CACHE_CTL) | \
				            _PAGE_COHERENT | _PAGE_WRITETHRU))
#else
#define pgprot_cached_wthru(prot)	pgprot_noncached(prot)
#endif

#define pgprot_cached_noncoherent(prot) \
		(__pgprot(pgprot_val(prot) & ~_PAGE_CACHE_CTL))

#define pgprot_writecombine pgprot_noncached_wc

struct file;
extern pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
				     unsigned long size, pgprot_t vma_prot);
#define __HAVE_PHYS_MEM_ACCESS_PROT

#ifdef CONFIG_HUGETLB_PAGE
static inline int hugepd_ok(hugepd_t hpd)
{
#ifdef CONFIG_PPC_8xx
	return ((hpd_val(hpd) & 0x4) != 0);
#else
	/* We clear the top bit to indicate hugepd */
	return (hpd_val(hpd) && (hpd_val(hpd) & PD_HUGE) == 0);
#endif
}

static inline int pmd_huge(pmd_t pmd)
{
	return 0;
}

static inline int pud_huge(pud_t pud)
{
	return 0;
}

static inline int pgd_huge(pgd_t pgd)
{
	return 0;
}
#define pgd_huge		pgd_huge

#define is_hugepd(hpd)		(hugepd_ok(hpd))
#endif

#endif /* __ASSEMBLY__ */
#endif
