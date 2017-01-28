/*
 *    Copyright IBM Corp. 2007, 2011
 *    Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/swapops.h>
#include <linux/sysctl.h>
#include <linux/ksm.h>
#include <linux/mman.h>

#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>

static inline pte_t ptep_flush_direct(struct mm_struct *mm,
				      unsigned long addr, pte_t *ptep)
{
	pte_t old;

	old = *ptep;
	if (unlikely(pte_val(old) & _PAGE_INVALID))
		return old;
	atomic_inc(&mm->context.flush_count);
	if (MACHINE_HAS_TLB_LC &&
	    cpumask_equal(mm_cpumask(mm), cpumask_of(smp_processor_id())))
		__ptep_ipte(addr, ptep, IPTE_LOCAL);
	else
		__ptep_ipte(addr, ptep, IPTE_GLOBAL);
	atomic_dec(&mm->context.flush_count);
	return old;
}

static inline pte_t ptep_flush_lazy(struct mm_struct *mm,
				    unsigned long addr, pte_t *ptep)
{
	pte_t old;

	old = *ptep;
	if (unlikely(pte_val(old) & _PAGE_INVALID))
		return old;
	atomic_inc(&mm->context.flush_count);
	if (cpumask_equal(&mm->context.cpu_attach_mask,
			  cpumask_of(smp_processor_id()))) {
		pte_val(*ptep) |= _PAGE_INVALID;
		mm->context.flush_mm = 1;
	} else
		__ptep_ipte(addr, ptep, IPTE_GLOBAL);
	atomic_dec(&mm->context.flush_count);
	return old;
}

static inline pgste_t pgste_get_lock(pte_t *ptep)
{
	unsigned long new = 0;
#ifdef CONFIG_PGSTE
	unsigned long old;

	asm(
		"	lg	%0,%2\n"
		"0:	lgr	%1,%0\n"
		"	nihh	%0,0xff7f\n"	/* clear PCL bit in old */
		"	oihh	%1,0x0080\n"	/* set PCL bit in new */
		"	csg	%0,%1,%2\n"
		"	jl	0b\n"
		: "=&d" (old), "=&d" (new), "=Q" (ptep[PTRS_PER_PTE])
		: "Q" (ptep[PTRS_PER_PTE]) : "cc", "memory");
#endif
	return __pgste(new);
}

static inline void pgste_set_unlock(pte_t *ptep, pgste_t pgste)
{
#ifdef CONFIG_PGSTE
	asm(
		"	nihh	%1,0xff7f\n"	/* clear PCL bit */
		"	stg	%1,%0\n"
		: "=Q" (ptep[PTRS_PER_PTE])
		: "d" (pgste_val(pgste)), "Q" (ptep[PTRS_PER_PTE])
		: "cc", "memory");
#endif
}

static inline pgste_t pgste_get(pte_t *ptep)
{
	unsigned long pgste = 0;
#ifdef CONFIG_PGSTE
	pgste = *(unsigned long *)(ptep + PTRS_PER_PTE);
#endif
	return __pgste(pgste);
}

static inline void pgste_set(pte_t *ptep, pgste_t pgste)
{
#ifdef CONFIG_PGSTE
	*(pgste_t *)(ptep + PTRS_PER_PTE) = pgste;
#endif
}

static inline pgste_t pgste_update_all(pte_t pte, pgste_t pgste,
				       struct mm_struct *mm)
{
#ifdef CONFIG_PGSTE
	unsigned long address, bits, skey;

	if (!mm_use_skey(mm) || pte_val(pte) & _PAGE_INVALID)
		return pgste;
	address = pte_val(pte) & PAGE_MASK;
	skey = (unsigned long) page_get_storage_key(address);
	bits = skey & (_PAGE_CHANGED | _PAGE_REFERENCED);
	/* Transfer page changed & referenced bit to guest bits in pgste */
	pgste_val(pgste) |= bits << 48;		/* GR bit & GC bit */
	/* Copy page access key and fetch protection bit to pgste */
	pgste_val(pgste) &= ~(PGSTE_ACC_BITS | PGSTE_FP_BIT);
	pgste_val(pgste) |= (skey & (_PAGE_ACC_BITS | _PAGE_FP_BIT)) << 56;
#endif
	return pgste;

}

static inline void pgste_set_key(pte_t *ptep, pgste_t pgste, pte_t entry,
				 struct mm_struct *mm)
{
#ifdef CONFIG_PGSTE
	unsigned long address;
	unsigned long nkey;

	if (!mm_use_skey(mm) || pte_val(entry) & _PAGE_INVALID)
		return;
	VM_BUG_ON(!(pte_val(*ptep) & _PAGE_INVALID));
	address = pte_val(entry) & PAGE_MASK;
	/*
	 * Set page access key and fetch protection bit from pgste.
	 * The guest C/R information is still in the PGSTE, set real
	 * key C/R to 0.
	 */
	nkey = (pgste_val(pgste) & (PGSTE_ACC_BITS | PGSTE_FP_BIT)) >> 56;
	nkey |= (pgste_val(pgste) & (PGSTE_GR_BIT | PGSTE_GC_BIT)) >> 48;
	page_set_storage_key(address, nkey, 0);
#endif
}

static inline pgste_t pgste_set_pte(pte_t *ptep, pgste_t pgste, pte_t entry)
{
#ifdef CONFIG_PGSTE
	if ((pte_val(entry) & _PAGE_PRESENT) &&
	    (pte_val(entry) & _PAGE_WRITE) &&
	    !(pte_val(entry) & _PAGE_INVALID)) {
		if (!MACHINE_HAS_ESOP) {
			/*
			 * Without enhanced suppression-on-protection force
			 * the dirty bit on for all writable ptes.
			 */
			pte_val(entry) |= _PAGE_DIRTY;
			pte_val(entry) &= ~_PAGE_PROTECT;
		}
		if (!(pte_val(entry) & _PAGE_PROTECT))
			/* This pte allows write access, set user-dirty */
			pgste_val(pgste) |= PGSTE_UC_BIT;
	}
#endif
	*ptep = entry;
	return pgste;
}

static inline pgste_t pgste_pte_notify(struct mm_struct *mm,
				       unsigned long addr,
				       pte_t *ptep, pgste_t pgste)
{
#ifdef CONFIG_PGSTE
	unsigned long bits;

	bits = pgste_val(pgste) & (PGSTE_IN_BIT | PGSTE_VSIE_BIT);
	if (bits) {
		pgste_val(pgste) ^= bits;
		ptep_notify(mm, addr, ptep, bits);
	}
#endif
	return pgste;
}

static inline pgste_t ptep_xchg_start(struct mm_struct *mm,
				      unsigned long addr, pte_t *ptep)
{
	pgste_t pgste = __pgste(0);

	if (mm_has_pgste(mm)) {
		pgste = pgste_get_lock(ptep);
		pgste = pgste_pte_notify(mm, addr, ptep, pgste);
	}
	return pgste;
}

static inline pte_t ptep_xchg_commit(struct mm_struct *mm,
				    unsigned long addr, pte_t *ptep,
				    pgste_t pgste, pte_t old, pte_t new)
{
	if (mm_has_pgste(mm)) {
		if (pte_val(old) & _PAGE_INVALID)
			pgste_set_key(ptep, pgste, new, mm);
		if (pte_val(new) & _PAGE_INVALID) {
			pgste = pgste_update_all(old, pgste, mm);
			if ((pgste_val(pgste) & _PGSTE_GPS_USAGE_MASK) ==
			    _PGSTE_GPS_USAGE_UNUSED)
				pte_val(old) |= _PAGE_UNUSED;
		}
		pgste = pgste_set_pte(ptep, pgste, new);
		pgste_set_unlock(ptep, pgste);
	} else {
		*ptep = new;
	}
	return old;
}

pte_t ptep_xchg_direct(struct mm_struct *mm, unsigned long addr,
		       pte_t *ptep, pte_t new)
{
	pgste_t pgste;
	pte_t old;

	preempt_disable();
	pgste = ptep_xchg_start(mm, addr, ptep);
	old = ptep_flush_direct(mm, addr, ptep);
	old = ptep_xchg_commit(mm, addr, ptep, pgste, old, new);
	preempt_enable();
	return old;
}
EXPORT_SYMBOL(ptep_xchg_direct);

pte_t ptep_xchg_lazy(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t new)
{
	pgste_t pgste;
	pte_t old;

	preempt_disable();
	pgste = ptep_xchg_start(mm, addr, ptep);
	old = ptep_flush_lazy(mm, addr, ptep);
	old = ptep_xchg_commit(mm, addr, ptep, pgste, old, new);
	preempt_enable();
	return old;
}
EXPORT_SYMBOL(ptep_xchg_lazy);

pte_t ptep_modify_prot_start(struct mm_struct *mm, unsigned long addr,
			     pte_t *ptep)
{
	pgste_t pgste;
	pte_t old;

	preempt_disable();
	pgste = ptep_xchg_start(mm, addr, ptep);
	old = ptep_flush_lazy(mm, addr, ptep);
	if (mm_has_pgste(mm)) {
		pgste = pgste_update_all(old, pgste, mm);
		pgste_set(ptep, pgste);
	}
	return old;
}
EXPORT_SYMBOL(ptep_modify_prot_start);

void ptep_modify_prot_commit(struct mm_struct *mm, unsigned long addr,
			     pte_t *ptep, pte_t pte)
{
	pgste_t pgste;

	if (mm_has_pgste(mm)) {
		pgste = pgste_get(ptep);
		pgste_set_key(ptep, pgste, pte, mm);
		pgste = pgste_set_pte(ptep, pgste, pte);
		pgste_set_unlock(ptep, pgste);
	} else {
		*ptep = pte;
	}
	preempt_enable();
}
EXPORT_SYMBOL(ptep_modify_prot_commit);

static inline pmd_t pmdp_flush_direct(struct mm_struct *mm,
				      unsigned long addr, pmd_t *pmdp)
{
	pmd_t old;

	old = *pmdp;
	if (pmd_val(old) & _SEGMENT_ENTRY_INVALID)
		return old;
	if (!MACHINE_HAS_IDTE) {
		__pmdp_csp(pmdp);
		return old;
	}
	atomic_inc(&mm->context.flush_count);
	if (MACHINE_HAS_TLB_LC &&
	    cpumask_equal(mm_cpumask(mm), cpumask_of(smp_processor_id())))
		__pmdp_idte(addr, pmdp, IDTE_LOCAL);
	else
		__pmdp_idte(addr, pmdp, IDTE_GLOBAL);
	atomic_dec(&mm->context.flush_count);
	return old;
}

static inline pmd_t pmdp_flush_lazy(struct mm_struct *mm,
				    unsigned long addr, pmd_t *pmdp)
{
	pmd_t old;

	old = *pmdp;
	if (pmd_val(old) & _SEGMENT_ENTRY_INVALID)
		return old;
	atomic_inc(&mm->context.flush_count);
	if (cpumask_equal(&mm->context.cpu_attach_mask,
			  cpumask_of(smp_processor_id()))) {
		pmd_val(*pmdp) |= _SEGMENT_ENTRY_INVALID;
		mm->context.flush_mm = 1;
	} else if (MACHINE_HAS_IDTE)
		__pmdp_idte(addr, pmdp, IDTE_GLOBAL);
	else
		__pmdp_csp(pmdp);
	atomic_dec(&mm->context.flush_count);
	return old;
}

pmd_t pmdp_xchg_direct(struct mm_struct *mm, unsigned long addr,
		       pmd_t *pmdp, pmd_t new)
{
	pmd_t old;

	preempt_disable();
	old = pmdp_flush_direct(mm, addr, pmdp);
	*pmdp = new;
	preempt_enable();
	return old;
}
EXPORT_SYMBOL(pmdp_xchg_direct);

pmd_t pmdp_xchg_lazy(struct mm_struct *mm, unsigned long addr,
		     pmd_t *pmdp, pmd_t new)
{
	pmd_t old;

	preempt_disable();
	old = pmdp_flush_lazy(mm, addr, pmdp);
	*pmdp = new;
	preempt_enable();
	return old;
}
EXPORT_SYMBOL(pmdp_xchg_lazy);

static inline pud_t pudp_flush_direct(struct mm_struct *mm,
				      unsigned long addr, pud_t *pudp)
{
	pud_t old;

	old = *pudp;
	if (pud_val(old) & _REGION_ENTRY_INVALID)
		return old;
	if (!MACHINE_HAS_IDTE) {
		/*
		 * Invalid bit position is the same for pmd and pud, so we can
		 * re-use _pmd_csp() here
		 */
		__pmdp_csp((pmd_t *) pudp);
		return old;
	}
	atomic_inc(&mm->context.flush_count);
	if (MACHINE_HAS_TLB_LC &&
	    cpumask_equal(mm_cpumask(mm), cpumask_of(smp_processor_id())))
		__pudp_idte(addr, pudp, IDTE_LOCAL);
	else
		__pudp_idte(addr, pudp, IDTE_GLOBAL);
	atomic_dec(&mm->context.flush_count);
	return old;
}

pud_t pudp_xchg_direct(struct mm_struct *mm, unsigned long addr,
		       pud_t *pudp, pud_t new)
{
	pud_t old;

	preempt_disable();
	old = pudp_flush_direct(mm, addr, pudp);
	*pudp = new;
	preempt_enable();
	return old;
}
EXPORT_SYMBOL(pudp_xchg_direct);

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
void pgtable_trans_huge_deposit(struct mm_struct *mm, pmd_t *pmdp,
				pgtable_t pgtable)
{
	struct list_head *lh = (struct list_head *) pgtable;

	assert_spin_locked(pmd_lockptr(mm, pmdp));

	/* FIFO */
	if (!pmd_huge_pte(mm, pmdp))
		INIT_LIST_HEAD(lh);
	else
		list_add(lh, (struct list_head *) pmd_huge_pte(mm, pmdp));
	pmd_huge_pte(mm, pmdp) = pgtable;
}

pgtable_t pgtable_trans_huge_withdraw(struct mm_struct *mm, pmd_t *pmdp)
{
	struct list_head *lh;
	pgtable_t pgtable;
	pte_t *ptep;

	assert_spin_locked(pmd_lockptr(mm, pmdp));

	/* FIFO */
	pgtable = pmd_huge_pte(mm, pmdp);
	lh = (struct list_head *) pgtable;
	if (list_empty(lh))
		pmd_huge_pte(mm, pmdp) = NULL;
	else {
		pmd_huge_pte(mm, pmdp) = (pgtable_t) lh->next;
		list_del(lh);
	}
	ptep = (pte_t *) pgtable;
	pte_val(*ptep) = _PAGE_INVALID;
	ptep++;
	pte_val(*ptep) = _PAGE_INVALID;
	return pgtable;
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

#ifdef CONFIG_PGSTE
void ptep_set_pte_at(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t entry)
{
	pgste_t pgste;

	/* the mm_has_pgste() check is done in set_pte_at() */
	preempt_disable();
	pgste = pgste_get_lock(ptep);
	pgste_val(pgste) &= ~_PGSTE_GPS_ZERO;
	pgste_set_key(ptep, pgste, entry, mm);
	pgste = pgste_set_pte(ptep, pgste, entry);
	pgste_set_unlock(ptep, pgste);
	preempt_enable();
}

void ptep_set_notify(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	pgste_t pgste;

	preempt_disable();
	pgste = pgste_get_lock(ptep);
	pgste_val(pgste) |= PGSTE_IN_BIT;
	pgste_set_unlock(ptep, pgste);
	preempt_enable();
}

/**
 * ptep_force_prot - change access rights of a locked pte
 * @mm: pointer to the process mm_struct
 * @addr: virtual address in the guest address space
 * @ptep: pointer to the page table entry
 * @prot: indicates guest access rights: PROT_NONE, PROT_READ or PROT_WRITE
 * @bit: pgste bit to set (e.g. for notification)
 *
 * Returns 0 if the access rights were changed and -EAGAIN if the current
 * and requested access rights are incompatible.
 */
int ptep_force_prot(struct mm_struct *mm, unsigned long addr,
		    pte_t *ptep, int prot, unsigned long bit)
{
	pte_t entry;
	pgste_t pgste;
	int pte_i, pte_p;

	pgste = pgste_get_lock(ptep);
	entry = *ptep;
	/* Check pte entry after all locks have been acquired */
	pte_i = pte_val(entry) & _PAGE_INVALID;
	pte_p = pte_val(entry) & _PAGE_PROTECT;
	if ((pte_i && (prot != PROT_NONE)) ||
	    (pte_p && (prot & PROT_WRITE))) {
		pgste_set_unlock(ptep, pgste);
		return -EAGAIN;
	}
	/* Change access rights and set pgste bit */
	if (prot == PROT_NONE && !pte_i) {
		ptep_flush_direct(mm, addr, ptep);
		pgste = pgste_update_all(entry, pgste, mm);
		pte_val(entry) |= _PAGE_INVALID;
	}
	if (prot == PROT_READ && !pte_p) {
		ptep_flush_direct(mm, addr, ptep);
		pte_val(entry) &= ~_PAGE_INVALID;
		pte_val(entry) |= _PAGE_PROTECT;
	}
	pgste_val(pgste) |= bit;
	pgste = pgste_set_pte(ptep, pgste, entry);
	pgste_set_unlock(ptep, pgste);
	return 0;
}

int ptep_shadow_pte(struct mm_struct *mm, unsigned long saddr,
		    pte_t *sptep, pte_t *tptep, pte_t pte)
{
	pgste_t spgste, tpgste;
	pte_t spte, tpte;
	int rc = -EAGAIN;

	if (!(pte_val(*tptep) & _PAGE_INVALID))
		return 0;	/* already shadowed */
	spgste = pgste_get_lock(sptep);
	spte = *sptep;
	if (!(pte_val(spte) & _PAGE_INVALID) &&
	    !((pte_val(spte) & _PAGE_PROTECT) &&
	      !(pte_val(pte) & _PAGE_PROTECT))) {
		pgste_val(spgste) |= PGSTE_VSIE_BIT;
		tpgste = pgste_get_lock(tptep);
		pte_val(tpte) = (pte_val(spte) & PAGE_MASK) |
				(pte_val(pte) & _PAGE_PROTECT);
		/* don't touch the storage key - it belongs to parent pgste */
		tpgste = pgste_set_pte(tptep, tpgste, tpte);
		pgste_set_unlock(tptep, tpgste);
		rc = 1;
	}
	pgste_set_unlock(sptep, spgste);
	return rc;
}

void ptep_unshadow_pte(struct mm_struct *mm, unsigned long saddr, pte_t *ptep)
{
	pgste_t pgste;

	pgste = pgste_get_lock(ptep);
	/* notifier is called by the caller */
	ptep_flush_direct(mm, saddr, ptep);
	/* don't touch the storage key - it belongs to parent pgste */
	pgste = pgste_set_pte(ptep, pgste, __pte(_PAGE_INVALID));
	pgste_set_unlock(ptep, pgste);
}

static void ptep_zap_swap_entry(struct mm_struct *mm, swp_entry_t entry)
{
	if (!non_swap_entry(entry))
		dec_mm_counter(mm, MM_SWAPENTS);
	else if (is_migration_entry(entry)) {
		struct page *page = migration_entry_to_page(entry);

		dec_mm_counter(mm, mm_counter(page));
	}
	free_swap_and_cache(entry);
}

void ptep_zap_unused(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, int reset)
{
	unsigned long pgstev;
	pgste_t pgste;
	pte_t pte;

	/* Zap unused and logically-zero pages */
	preempt_disable();
	pgste = pgste_get_lock(ptep);
	pgstev = pgste_val(pgste);
	pte = *ptep;
	if (!reset && pte_swap(pte) &&
	    ((pgstev & _PGSTE_GPS_USAGE_MASK) == _PGSTE_GPS_USAGE_UNUSED ||
	     (pgstev & _PGSTE_GPS_ZERO))) {
		ptep_zap_swap_entry(mm, pte_to_swp_entry(pte));
		pte_clear(mm, addr, ptep);
	}
	if (reset)
		pgste_val(pgste) &= ~_PGSTE_GPS_USAGE_MASK;
	pgste_set_unlock(ptep, pgste);
	preempt_enable();
}

void ptep_zap_key(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	unsigned long ptev;
	pgste_t pgste;

	/* Clear storage key */
	preempt_disable();
	pgste = pgste_get_lock(ptep);
	pgste_val(pgste) &= ~(PGSTE_ACC_BITS | PGSTE_FP_BIT |
			      PGSTE_GR_BIT | PGSTE_GC_BIT);
	ptev = pte_val(*ptep);
	if (!(ptev & _PAGE_INVALID) && (ptev & _PAGE_WRITE))
		page_set_storage_key(ptev & PAGE_MASK, PAGE_DEFAULT_KEY, 1);
	pgste_set_unlock(ptep, pgste);
	preempt_enable();
}

/*
 * Test and reset if a guest page is dirty
 */
bool test_and_clear_guest_dirty(struct mm_struct *mm, unsigned long addr)
{
	spinlock_t *ptl;
	pgste_t pgste;
	pte_t *ptep;
	pte_t pte;
	bool dirty;

	ptep = get_locked_pte(mm, addr, &ptl);
	if (unlikely(!ptep))
		return false;

	pgste = pgste_get_lock(ptep);
	dirty = !!(pgste_val(pgste) & PGSTE_UC_BIT);
	pgste_val(pgste) &= ~PGSTE_UC_BIT;
	pte = *ptep;
	if (dirty && (pte_val(pte) & _PAGE_PRESENT)) {
		pgste = pgste_pte_notify(mm, addr, ptep, pgste);
		__ptep_ipte(addr, ptep, IPTE_GLOBAL);
		if (MACHINE_HAS_ESOP || !(pte_val(pte) & _PAGE_WRITE))
			pte_val(pte) |= _PAGE_PROTECT;
		else
			pte_val(pte) |= _PAGE_INVALID;
		*ptep = pte;
	}
	pgste_set_unlock(ptep, pgste);

	spin_unlock(ptl);
	return dirty;
}
EXPORT_SYMBOL_GPL(test_and_clear_guest_dirty);

int set_guest_storage_key(struct mm_struct *mm, unsigned long addr,
			  unsigned char key, bool nq)
{
	unsigned long keyul;
	spinlock_t *ptl;
	pgste_t old, new;
	pte_t *ptep;

	ptep = get_locked_pte(mm, addr, &ptl);
	if (unlikely(!ptep))
		return -EFAULT;

	new = old = pgste_get_lock(ptep);
	pgste_val(new) &= ~(PGSTE_GR_BIT | PGSTE_GC_BIT |
			    PGSTE_ACC_BITS | PGSTE_FP_BIT);
	keyul = (unsigned long) key;
	pgste_val(new) |= (keyul & (_PAGE_CHANGED | _PAGE_REFERENCED)) << 48;
	pgste_val(new) |= (keyul & (_PAGE_ACC_BITS | _PAGE_FP_BIT)) << 56;
	if (!(pte_val(*ptep) & _PAGE_INVALID)) {
		unsigned long address, bits, skey;

		address = pte_val(*ptep) & PAGE_MASK;
		skey = (unsigned long) page_get_storage_key(address);
		bits = skey & (_PAGE_CHANGED | _PAGE_REFERENCED);
		skey = key & (_PAGE_ACC_BITS | _PAGE_FP_BIT);
		/* Set storage key ACC and FP */
		page_set_storage_key(address, skey, !nq);
		/* Merge host changed & referenced into pgste  */
		pgste_val(new) |= bits << 52;
	}
	/* changing the guest storage key is considered a change of the page */
	if ((pgste_val(new) ^ pgste_val(old)) &
	    (PGSTE_ACC_BITS | PGSTE_FP_BIT | PGSTE_GR_BIT | PGSTE_GC_BIT))
		pgste_val(new) |= PGSTE_UC_BIT;

	pgste_set_unlock(ptep, new);
	pte_unmap_unlock(ptep, ptl);
	return 0;
}
EXPORT_SYMBOL(set_guest_storage_key);

/**
 * Conditionally set a guest storage key (handling csske).
 * oldkey will be updated when either mr or mc is set and a pointer is given.
 *
 * Returns 0 if a guests storage key update wasn't necessary, 1 if the guest
 * storage key was updated and -EFAULT on access errors.
 */
int cond_set_guest_storage_key(struct mm_struct *mm, unsigned long addr,
			       unsigned char key, unsigned char *oldkey,
			       bool nq, bool mr, bool mc)
{
	unsigned char tmp, mask = _PAGE_ACC_BITS | _PAGE_FP_BIT;
	int rc;

	/* we can drop the pgste lock between getting and setting the key */
	if (mr | mc) {
		rc = get_guest_storage_key(current->mm, addr, &tmp);
		if (rc)
			return rc;
		if (oldkey)
			*oldkey = tmp;
		if (!mr)
			mask |= _PAGE_REFERENCED;
		if (!mc)
			mask |= _PAGE_CHANGED;
		if (!((tmp ^ key) & mask))
			return 0;
	}
	rc = set_guest_storage_key(current->mm, addr, key, nq);
	return rc < 0 ? rc : 1;
}
EXPORT_SYMBOL(cond_set_guest_storage_key);

/**
 * Reset a guest reference bit (rrbe), returning the reference and changed bit.
 *
 * Returns < 0 in case of error, otherwise the cc to be reported to the guest.
 */
int reset_guest_reference_bit(struct mm_struct *mm, unsigned long addr)
{
	spinlock_t *ptl;
	pgste_t old, new;
	pte_t *ptep;
	int cc = 0;

	ptep = get_locked_pte(mm, addr, &ptl);
	if (unlikely(!ptep))
		return -EFAULT;

	new = old = pgste_get_lock(ptep);
	/* Reset guest reference bit only */
	pgste_val(new) &= ~PGSTE_GR_BIT;

	if (!(pte_val(*ptep) & _PAGE_INVALID)) {
		cc = page_reset_referenced(pte_val(*ptep) & PAGE_MASK);
		/* Merge real referenced bit into host-set */
		pgste_val(new) |= ((unsigned long) cc << 53) & PGSTE_HR_BIT;
	}
	/* Reflect guest's logical view, not physical */
	cc |= (pgste_val(old) & (PGSTE_GR_BIT | PGSTE_GC_BIT)) >> 49;
	/* Changing the guest storage key is considered a change of the page */
	if ((pgste_val(new) ^ pgste_val(old)) & PGSTE_GR_BIT)
		pgste_val(new) |= PGSTE_UC_BIT;

	pgste_set_unlock(ptep, new);
	pte_unmap_unlock(ptep, ptl);
	return 0;
}
EXPORT_SYMBOL(reset_guest_reference_bit);

int get_guest_storage_key(struct mm_struct *mm, unsigned long addr,
			  unsigned char *key)
{
	spinlock_t *ptl;
	pgste_t pgste;
	pte_t *ptep;

	ptep = get_locked_pte(mm, addr, &ptl);
	if (unlikely(!ptep))
		return -EFAULT;

	pgste = pgste_get_lock(ptep);
	*key = (pgste_val(pgste) & (PGSTE_ACC_BITS | PGSTE_FP_BIT)) >> 56;
	if (!(pte_val(*ptep) & _PAGE_INVALID))
		*key = page_get_storage_key(pte_val(*ptep) & PAGE_MASK);
	/* Reflect guest's logical view, not physical */
	*key |= (pgste_val(pgste) & (PGSTE_GR_BIT | PGSTE_GC_BIT)) >> 48;
	pgste_set_unlock(ptep, pgste);
	pte_unmap_unlock(ptep, ptl);
	return 0;
}
EXPORT_SYMBOL(get_guest_storage_key);
#endif
