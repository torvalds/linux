/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_NOHASH_PGTABLE_H
#define _ASM_POWERPC_NOHASH_PGTABLE_H

#ifndef __ASSEMBLY__
static inline pte_basic_t pte_update(struct mm_struct *mm, unsigned long addr, pte_t *p,
				     unsigned long clr, unsigned long set, int huge);
#endif

#if defined(CONFIG_PPC64)
#include <asm/nohash/64/pgtable.h>
#else
#include <asm/nohash/32/pgtable.h>
#endif

/*
 * _PAGE_CHG_MASK masks of bits that are to be preserved across
 * pgprot changes.
 */
#define _PAGE_CHG_MASK	(PTE_RPN_MASK | _PAGE_DIRTY | _PAGE_ACCESSED | _PAGE_SPECIAL)

/* Permission masks used for kernel mappings */
#define PAGE_KERNEL	__pgprot(_PAGE_BASE | _PAGE_KERNEL_RW)
#define PAGE_KERNEL_NC	__pgprot(_PAGE_BASE_NC | _PAGE_KERNEL_RW | _PAGE_NO_CACHE)
#define PAGE_KERNEL_NCG	__pgprot(_PAGE_BASE_NC | _PAGE_KERNEL_RW | _PAGE_NO_CACHE | _PAGE_GUARDED)
#define PAGE_KERNEL_X	__pgprot(_PAGE_BASE | _PAGE_KERNEL_RWX)
#define PAGE_KERNEL_RO	__pgprot(_PAGE_BASE | _PAGE_KERNEL_RO)
#define PAGE_KERNEL_ROX	__pgprot(_PAGE_BASE | _PAGE_KERNEL_ROX)

#ifndef __ASSEMBLY__

extern int icache_44x_need_flush;

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
 */
#ifndef pte_update
static inline pte_basic_t pte_update(struct mm_struct *mm, unsigned long addr, pte_t *p,
				     unsigned long clr, unsigned long set, int huge)
{
	pte_basic_t old = pte_val(*p);
	pte_basic_t new = (old & ~(pte_basic_t)clr) | set;

	if (new == old)
		return old;

	*p = __pte(new);

	if (IS_ENABLED(CONFIG_44x) && !is_kernel_addr(addr) && (old & _PAGE_EXEC))
		icache_44x_need_flush = 1;

	/* huge pages use the old page table lock */
	if (!huge)
		assert_pte_locked(mm, addr);

	return old;
}
#endif

static inline int ptep_test_and_clear_young(struct vm_area_struct *vma,
					    unsigned long addr, pte_t *ptep)
{
	unsigned long old;

	old = pte_update(vma->vm_mm, addr, ptep, _PAGE_ACCESSED, 0, 0);

	return (old & _PAGE_ACCESSED) != 0;
}
#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG

#ifndef ptep_set_wrprotect
static inline void ptep_set_wrprotect(struct mm_struct *mm, unsigned long addr,
				      pte_t *ptep)
{
	pte_update(mm, addr, ptep, _PAGE_WRITE, 0, 0);
}
#endif
#define __HAVE_ARCH_PTEP_SET_WRPROTECT

static inline pte_t ptep_get_and_clear(struct mm_struct *mm, unsigned long addr,
				       pte_t *ptep)
{
	return __pte(pte_update(mm, addr, ptep, ~0UL, 0, 0));
}
#define __HAVE_ARCH_PTEP_GET_AND_CLEAR

static inline void pte_clear(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	pte_update(mm, addr, ptep, ~0UL, 0, 0);
}

/* Set the dirty and/or accessed bits atomically in a linux PTE */
#ifndef __ptep_set_access_flags
static inline void __ptep_set_access_flags(struct vm_area_struct *vma,
					   pte_t *ptep, pte_t entry,
					   unsigned long address,
					   int psize)
{
	unsigned long set = pte_val(entry) &
			    (_PAGE_DIRTY | _PAGE_ACCESSED | _PAGE_RW | _PAGE_EXEC);
	int huge = psize > mmu_virtual_psize ? 1 : 0;

	pte_update(vma->vm_mm, address, ptep, 0, set, huge);

	flush_tlb_page(vma, address);
}
#endif

/* Generic accessors to PTE bits */
#ifndef pte_mkwrite_novma
static inline pte_t pte_mkwrite_novma(pte_t pte)
{
	/*
	 * write implies read, hence set both
	 */
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
	return __pte(pte_val(pte) & ~_PAGE_WRITE);
}
#endif

#ifndef pte_mkexec
static inline pte_t pte_mkexec(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_EXEC);
}
#endif

#ifndef pte_write
static inline int pte_write(pte_t pte)
{
	return pte_val(pte) & _PAGE_WRITE;
}
#endif
static inline int pte_dirty(pte_t pte)		{ return pte_val(pte) & _PAGE_DIRTY; }
static inline int pte_special(pte_t pte)	{ return pte_val(pte) & _PAGE_SPECIAL; }
static inline int pte_none(pte_t pte)		{ return (pte_val(pte) & ~_PTE_NONE_MASK) == 0; }
static inline bool pte_hashpte(pte_t pte)	{ return false; }
static inline bool pte_ci(pte_t pte)		{ return pte_val(pte) & _PAGE_NO_CACHE; }
static inline bool pte_exec(pte_t pte)		{ return pte_val(pte) & _PAGE_EXEC; }

static inline int pte_present(pte_t pte)
{
	return pte_val(pte) & _PAGE_PRESENT;
}

static inline bool pte_hw_valid(pte_t pte)
{
	return pte_val(pte) & _PAGE_PRESENT;
}

static inline int pte_young(pte_t pte)
{
	return pte_val(pte) & _PAGE_ACCESSED;
}

/*
 * Don't just check for any non zero bits in __PAGE_READ, since for book3e
 * and PTE_64BIT, PAGE_KERNEL_X contains _PAGE_BAP_SR which is also in
 * _PAGE_READ.  Need to explicitly match _PAGE_BAP_UR bit in that case too.
 */
#ifndef pte_read
static inline bool pte_read(pte_t pte)
{
	return (pte_val(pte) & _PAGE_READ) == _PAGE_READ;
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
	 * A read-only access is controlled by _PAGE_READ bit.
	 * We have _PAGE_READ set for WRITE
	 */
	if (!pte_present(pte) || !pte_read(pte))
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

/* Generic modifiers for PTE bits */
static inline pte_t pte_exprotect(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_EXEC);
}

static inline pte_t pte_mkclean(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_DIRTY);
}

static inline pte_t pte_mkold(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_ACCESSED);
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

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot));
}

static inline int pte_swp_exclusive(pte_t pte)
{
	return pte_val(pte) & _PAGE_SWP_EXCLUSIVE;
}

static inline pte_t pte_swp_mkexclusive(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_SWP_EXCLUSIVE);
}

static inline pte_t pte_swp_clear_exclusive(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_SWP_EXCLUSIVE);
}

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
			stw%X0 %2,%0\n\
			mbar\n\
			stw%X1 %L2,%1"
		: "=m" (*ptep), "=m" (*((unsigned char *)ptep+4))
		: "r" (pte) : "memory");
		return;
	}
	/* Anything else just stores the PTE normally. That covers all 64-bit
	 * cases, and 32-bit non-hash with 32-bit PTEs.
	 */
#if defined(CONFIG_PPC_8xx) && defined(CONFIG_PPC_16K_PAGES)
	ptep->pte3 = ptep->pte2 = ptep->pte1 = ptep->pte = pte_val(pte);
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

#ifdef CONFIG_HUGETLB_PAGE
static inline int hugepd_ok(hugepd_t hpd)
{
#ifdef CONFIG_PPC_8xx
	return ((hpd_val(hpd) & _PMD_PAGE_MASK) == _PMD_PAGE_8M);
#else
	/* We clear the top bit to indicate hugepd */
	return (hpd_val(hpd) && (hpd_val(hpd) & PD_HUGE) == 0);
#endif
}

#define is_hugepd(hpd)		(hugepd_ok(hpd))
#endif

int map_kernel_page(unsigned long va, phys_addr_t pa, pgprot_t prot);
void unmap_kernel_page(unsigned long va);

#endif /* __ASSEMBLY__ */
#endif
