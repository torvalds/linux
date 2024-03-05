// SPDX-License-Identifier: GPL-2.0
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

#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/page-states.h>

pgprot_t pgprot_writecombine(pgprot_t prot)
{
	/*
	 * mio_wb_bit_mask may be set on a different CPU, but it is only set
	 * once at init and only read afterwards.
	 */
	return __pgprot(pgprot_val(prot) | mio_wb_bit_mask);
}
EXPORT_SYMBOL_GPL(pgprot_writecombine);

pgprot_t pgprot_writethrough(pgprot_t prot)
{
	/*
	 * mio_wb_bit_mask may be set on a different CPU, but it is only set
	 * once at init and only read afterwards.
	 */
	return __pgprot(pgprot_val(prot) & ~mio_wb_bit_mask);
}
EXPORT_SYMBOL_GPL(pgprot_writethrough);

static inline void ptep_ipte_local(struct mm_struct *mm, unsigned long addr,
				   pte_t *ptep, int nodat)
{
	unsigned long opt, asce;

	if (MACHINE_HAS_TLB_GUEST) {
		opt = 0;
		asce = READ_ONCE(mm->context.gmap_asce);
		if (asce == 0UL || nodat)
			opt |= IPTE_NODAT;
		if (asce != -1UL) {
			asce = asce ? : mm->context.asce;
			opt |= IPTE_GUEST_ASCE;
		}
		__ptep_ipte(addr, ptep, opt, asce, IPTE_LOCAL);
	} else {
		__ptep_ipte(addr, ptep, 0, 0, IPTE_LOCAL);
	}
}

static inline void ptep_ipte_global(struct mm_struct *mm, unsigned long addr,
				    pte_t *ptep, int nodat)
{
	unsigned long opt, asce;

	if (MACHINE_HAS_TLB_GUEST) {
		opt = 0;
		asce = READ_ONCE(mm->context.gmap_asce);
		if (asce == 0UL || nodat)
			opt |= IPTE_NODAT;
		if (asce != -1UL) {
			asce = asce ? : mm->context.asce;
			opt |= IPTE_GUEST_ASCE;
		}
		__ptep_ipte(addr, ptep, opt, asce, IPTE_GLOBAL);
	} else {
		__ptep_ipte(addr, ptep, 0, 0, IPTE_GLOBAL);
	}
}

static inline pte_t ptep_flush_direct(struct mm_struct *mm,
				      unsigned long addr, pte_t *ptep,
				      int nodat)
{
	pte_t old;

	old = *ptep;
	if (unlikely(pte_val(old) & _PAGE_INVALID))
		return old;
	atomic_inc(&mm->context.flush_count);
	if (MACHINE_HAS_TLB_LC &&
	    cpumask_equal(mm_cpumask(mm), cpumask_of(smp_processor_id())))
		ptep_ipte_local(mm, addr, ptep, nodat);
	else
		ptep_ipte_global(mm, addr, ptep, nodat);
	atomic_dec(&mm->context.flush_count);
	return old;
}

static inline pte_t ptep_flush_lazy(struct mm_struct *mm,
				    unsigned long addr, pte_t *ptep,
				    int nodat)
{
	pte_t old;

	old = *ptep;
	if (unlikely(pte_val(old) & _PAGE_INVALID))
		return old;
	atomic_inc(&mm->context.flush_count);
	if (cpumask_equal(&mm->context.cpu_attach_mask,
			  cpumask_of(smp_processor_id()))) {
		set_pte(ptep, set_pte_bit(*ptep, __pgprot(_PAGE_INVALID)));
		mm->context.flush_mm = 1;
	} else
		ptep_ipte_global(mm, addr, ptep, nodat);
	atomic_dec(&mm->context.flush_count);
	return old;
}

static inline pgste_t pgste_get_lock(pte_t *ptep)
{
	unsigned long value = 0;
#ifdef CONFIG_PGSTE
	unsigned long *ptr = (unsigned long *)(ptep + PTRS_PER_PTE);

	do {
		value = __atomic64_or_barrier(PGSTE_PCL_BIT, ptr);
	} while (value & PGSTE_PCL_BIT);
	value |= PGSTE_PCL_BIT;
#endif
	return __pgste(value);
}

static inline void pgste_set_unlock(pte_t *ptep, pgste_t pgste)
{
#ifdef CONFIG_PGSTE
	barrier();
	WRITE_ONCE(*(unsigned long *)(ptep + PTRS_PER_PTE), pgste_val(pgste) & ~PGSTE_PCL_BIT);
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

	if (!mm_uses_skeys(mm) || pte_val(pte) & _PAGE_INVALID)
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

	if (!mm_uses_skeys(mm) || pte_val(entry) & _PAGE_INVALID)
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
			entry = set_pte_bit(entry, __pgprot(_PAGE_DIRTY));
			entry = clear_pte_bit(entry, __pgprot(_PAGE_PROTECT));
		}
		if (!(pte_val(entry) & _PAGE_PROTECT))
			/* This pte allows write access, set user-dirty */
			pgste_val(pgste) |= PGSTE_UC_BIT;
	}
#endif
	set_pte(ptep, entry);
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
				old = set_pte_bit(old, __pgprot(_PAGE_UNUSED));
		}
		pgste = pgste_set_pte(ptep, pgste, new);
		pgste_set_unlock(ptep, pgste);
	} else {
		set_pte(ptep, new);
	}
	return old;
}

pte_t ptep_xchg_direct(struct mm_struct *mm, unsigned long addr,
		       pte_t *ptep, pte_t new)
{
	pgste_t pgste;
	pte_t old;
	int nodat;

	preempt_disable();
	pgste = ptep_xchg_start(mm, addr, ptep);
	nodat = !!(pgste_val(pgste) & _PGSTE_GPS_NODAT);
	old = ptep_flush_direct(mm, addr, ptep, nodat);
	old = ptep_xchg_commit(mm, addr, ptep, pgste, old, new);
	preempt_enable();
	return old;
}
EXPORT_SYMBOL(ptep_xchg_direct);

/*
 * Caller must check that new PTE only differs in _PAGE_PROTECT HW bit, so that
 * RDP can be used instead of IPTE. See also comments at pte_allow_rdp().
 */
void ptep_reset_dat_prot(struct mm_struct *mm, unsigned long addr, pte_t *ptep,
			 pte_t new)
{
	preempt_disable();
	atomic_inc(&mm->context.flush_count);
	if (cpumask_equal(mm_cpumask(mm), cpumask_of(smp_processor_id())))
		__ptep_rdp(addr, ptep, 0, 0, 1);
	else
		__ptep_rdp(addr, ptep, 0, 0, 0);
	/*
	 * PTE is not invalidated by RDP, only _PAGE_PROTECT is cleared. That
	 * means it is still valid and active, and must not be changed according
	 * to the architecture. But writing a new value that only differs in SW
	 * bits is allowed.
	 */
	set_pte(ptep, new);
	atomic_dec(&mm->context.flush_count);
	preempt_enable();
}
EXPORT_SYMBOL(ptep_reset_dat_prot);

pte_t ptep_xchg_lazy(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t new)
{
	pgste_t pgste;
	pte_t old;
	int nodat;

	preempt_disable();
	pgste = ptep_xchg_start(mm, addr, ptep);
	nodat = !!(pgste_val(pgste) & _PGSTE_GPS_NODAT);
	old = ptep_flush_lazy(mm, addr, ptep, nodat);
	old = ptep_xchg_commit(mm, addr, ptep, pgste, old, new);
	preempt_enable();
	return old;
}
EXPORT_SYMBOL(ptep_xchg_lazy);

pte_t ptep_modify_prot_start(struct vm_area_struct *vma, unsigned long addr,
			     pte_t *ptep)
{
	pgste_t pgste;
	pte_t old;
	int nodat;
	struct mm_struct *mm = vma->vm_mm;

	preempt_disable();
	pgste = ptep_xchg_start(mm, addr, ptep);
	nodat = !!(pgste_val(pgste) & _PGSTE_GPS_NODAT);
	old = ptep_flush_lazy(mm, addr, ptep, nodat);
	if (mm_has_pgste(mm)) {
		pgste = pgste_update_all(old, pgste, mm);
		pgste_set(ptep, pgste);
	}
	return old;
}

void ptep_modify_prot_commit(struct vm_area_struct *vma, unsigned long addr,
			     pte_t *ptep, pte_t old_pte, pte_t pte)
{
	pgste_t pgste;
	struct mm_struct *mm = vma->vm_mm;

	if (!MACHINE_HAS_NX)
		pte = clear_pte_bit(pte, __pgprot(_PAGE_NOEXEC));
	if (mm_has_pgste(mm)) {
		pgste = pgste_get(ptep);
		pgste_set_key(ptep, pgste, pte, mm);
		pgste = pgste_set_pte(ptep, pgste, pte);
		pgste_set_unlock(ptep, pgste);
	} else {
		set_pte(ptep, pte);
	}
	preempt_enable();
}

static inline void pmdp_idte_local(struct mm_struct *mm,
				   unsigned long addr, pmd_t *pmdp)
{
	if (MACHINE_HAS_TLB_GUEST)
		__pmdp_idte(addr, pmdp, IDTE_NODAT | IDTE_GUEST_ASCE,
			    mm->context.asce, IDTE_LOCAL);
	else
		__pmdp_idte(addr, pmdp, 0, 0, IDTE_LOCAL);
	if (mm_has_pgste(mm) && mm->context.allow_gmap_hpage_1m)
		gmap_pmdp_idte_local(mm, addr);
}

static inline void pmdp_idte_global(struct mm_struct *mm,
				    unsigned long addr, pmd_t *pmdp)
{
	if (MACHINE_HAS_TLB_GUEST) {
		__pmdp_idte(addr, pmdp, IDTE_NODAT | IDTE_GUEST_ASCE,
			    mm->context.asce, IDTE_GLOBAL);
		if (mm_has_pgste(mm) && mm->context.allow_gmap_hpage_1m)
			gmap_pmdp_idte_global(mm, addr);
	} else if (MACHINE_HAS_IDTE) {
		__pmdp_idte(addr, pmdp, 0, 0, IDTE_GLOBAL);
		if (mm_has_pgste(mm) && mm->context.allow_gmap_hpage_1m)
			gmap_pmdp_idte_global(mm, addr);
	} else {
		__pmdp_csp(pmdp);
		if (mm_has_pgste(mm) && mm->context.allow_gmap_hpage_1m)
			gmap_pmdp_csp(mm, addr);
	}
}

static inline pmd_t pmdp_flush_direct(struct mm_struct *mm,
				      unsigned long addr, pmd_t *pmdp)
{
	pmd_t old;

	old = *pmdp;
	if (pmd_val(old) & _SEGMENT_ENTRY_INVALID)
		return old;
	atomic_inc(&mm->context.flush_count);
	if (MACHINE_HAS_TLB_LC &&
	    cpumask_equal(mm_cpumask(mm), cpumask_of(smp_processor_id())))
		pmdp_idte_local(mm, addr, pmdp);
	else
		pmdp_idte_global(mm, addr, pmdp);
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
		set_pmd(pmdp, set_pmd_bit(*pmdp, __pgprot(_SEGMENT_ENTRY_INVALID)));
		mm->context.flush_mm = 1;
		if (mm_has_pgste(mm))
			gmap_pmdp_invalidate(mm, addr);
	} else {
		pmdp_idte_global(mm, addr, pmdp);
	}
	atomic_dec(&mm->context.flush_count);
	return old;
}

#ifdef CONFIG_PGSTE
static int pmd_lookup(struct mm_struct *mm, unsigned long addr, pmd_t **pmdp)
{
	struct vm_area_struct *vma;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;

	/* We need a valid VMA, otherwise this is clearly a fault. */
	vma = vma_lookup(mm, addr);
	if (!vma)
		return -EFAULT;

	pgd = pgd_offset(mm, addr);
	if (!pgd_present(*pgd))
		return -ENOENT;

	p4d = p4d_offset(pgd, addr);
	if (!p4d_present(*p4d))
		return -ENOENT;

	pud = pud_offset(p4d, addr);
	if (!pud_present(*pud))
		return -ENOENT;

	/* Large PUDs are not supported yet. */
	if (pud_leaf(*pud))
		return -EFAULT;

	*pmdp = pmd_offset(pud, addr);
	return 0;
}
#endif

pmd_t pmdp_xchg_direct(struct mm_struct *mm, unsigned long addr,
		       pmd_t *pmdp, pmd_t new)
{
	pmd_t old;

	preempt_disable();
	old = pmdp_flush_direct(mm, addr, pmdp);
	set_pmd(pmdp, new);
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
	set_pmd(pmdp, new);
	preempt_enable();
	return old;
}
EXPORT_SYMBOL(pmdp_xchg_lazy);

static inline void pudp_idte_local(struct mm_struct *mm,
				   unsigned long addr, pud_t *pudp)
{
	if (MACHINE_HAS_TLB_GUEST)
		__pudp_idte(addr, pudp, IDTE_NODAT | IDTE_GUEST_ASCE,
			    mm->context.asce, IDTE_LOCAL);
	else
		__pudp_idte(addr, pudp, 0, 0, IDTE_LOCAL);
}

static inline void pudp_idte_global(struct mm_struct *mm,
				    unsigned long addr, pud_t *pudp)
{
	if (MACHINE_HAS_TLB_GUEST)
		__pudp_idte(addr, pudp, IDTE_NODAT | IDTE_GUEST_ASCE,
			    mm->context.asce, IDTE_GLOBAL);
	else if (MACHINE_HAS_IDTE)
		__pudp_idte(addr, pudp, 0, 0, IDTE_GLOBAL);
	else
		/*
		 * Invalid bit position is the same for pmd and pud, so we can
		 * re-use _pmd_csp() here
		 */
		__pmdp_csp((pmd_t *) pudp);
}

static inline pud_t pudp_flush_direct(struct mm_struct *mm,
				      unsigned long addr, pud_t *pudp)
{
	pud_t old;

	old = *pudp;
	if (pud_val(old) & _REGION_ENTRY_INVALID)
		return old;
	atomic_inc(&mm->context.flush_count);
	if (MACHINE_HAS_TLB_LC &&
	    cpumask_equal(mm_cpumask(mm), cpumask_of(smp_processor_id())))
		pudp_idte_local(mm, addr, pudp);
	else
		pudp_idte_global(mm, addr, pudp);
	atomic_dec(&mm->context.flush_count);
	return old;
}

pud_t pudp_xchg_direct(struct mm_struct *mm, unsigned long addr,
		       pud_t *pudp, pud_t new)
{
	pud_t old;

	preempt_disable();
	old = pudp_flush_direct(mm, addr, pudp);
	set_pud(pudp, new);
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
	set_pte(ptep, __pte(_PAGE_INVALID));
	ptep++;
	set_pte(ptep, __pte(_PAGE_INVALID));
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
	int pte_i, pte_p, nodat;

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
	nodat = !!(pgste_val(pgste) & _PGSTE_GPS_NODAT);
	if (prot == PROT_NONE && !pte_i) {
		ptep_flush_direct(mm, addr, ptep, nodat);
		pgste = pgste_update_all(entry, pgste, mm);
		entry = set_pte_bit(entry, __pgprot(_PAGE_INVALID));
	}
	if (prot == PROT_READ && !pte_p) {
		ptep_flush_direct(mm, addr, ptep, nodat);
		entry = clear_pte_bit(entry, __pgprot(_PAGE_INVALID));
		entry = set_pte_bit(entry, __pgprot(_PAGE_PROTECT));
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
		tpte = __pte((pte_val(spte) & PAGE_MASK) |
			     (pte_val(pte) & _PAGE_PROTECT));
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
	int nodat;

	pgste = pgste_get_lock(ptep);
	/* notifier is called by the caller */
	nodat = !!(pgste_val(pgste) & _PGSTE_GPS_NODAT);
	ptep_flush_direct(mm, saddr, ptep, nodat);
	/* don't touch the storage key - it belongs to parent pgste */
	pgste = pgste_set_pte(ptep, pgste, __pte(_PAGE_INVALID));
	pgste_set_unlock(ptep, pgste);
}

static void ptep_zap_swap_entry(struct mm_struct *mm, swp_entry_t entry)
{
	if (!non_swap_entry(entry))
		dec_mm_counter(mm, MM_SWAPENTS);
	else if (is_migration_entry(entry)) {
		struct folio *folio = pfn_swap_entry_folio(entry);

		dec_mm_counter(mm, mm_counter(folio));
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
		pgste_val(pgste) &= ~(_PGSTE_GPS_USAGE_MASK | _PGSTE_GPS_NODAT);
	pgste_set_unlock(ptep, pgste);
	preempt_enable();
}

void ptep_zap_key(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	unsigned long ptev;
	pgste_t pgste;

	/* Clear storage key ACC and F, but set R/C */
	preempt_disable();
	pgste = pgste_get_lock(ptep);
	pgste_val(pgste) &= ~(PGSTE_ACC_BITS | PGSTE_FP_BIT);
	pgste_val(pgste) |= PGSTE_GR_BIT | PGSTE_GC_BIT;
	ptev = pte_val(*ptep);
	if (!(ptev & _PAGE_INVALID) && (ptev & _PAGE_WRITE))
		page_set_storage_key(ptev & PAGE_MASK, PAGE_DEFAULT_KEY, 0);
	pgste_set_unlock(ptep, pgste);
	preempt_enable();
}

/*
 * Test and reset if a guest page is dirty
 */
bool ptep_test_and_clear_uc(struct mm_struct *mm, unsigned long addr,
		       pte_t *ptep)
{
	pgste_t pgste;
	pte_t pte;
	bool dirty;
	int nodat;

	pgste = pgste_get_lock(ptep);
	dirty = !!(pgste_val(pgste) & PGSTE_UC_BIT);
	pgste_val(pgste) &= ~PGSTE_UC_BIT;
	pte = *ptep;
	if (dirty && (pte_val(pte) & _PAGE_PRESENT)) {
		pgste = pgste_pte_notify(mm, addr, ptep, pgste);
		nodat = !!(pgste_val(pgste) & _PGSTE_GPS_NODAT);
		ptep_ipte_global(mm, addr, ptep, nodat);
		if (MACHINE_HAS_ESOP || !(pte_val(pte) & _PAGE_WRITE))
			pte = set_pte_bit(pte, __pgprot(_PAGE_PROTECT));
		else
			pte = set_pte_bit(pte, __pgprot(_PAGE_INVALID));
		set_pte(ptep, pte);
	}
	pgste_set_unlock(ptep, pgste);
	return dirty;
}
EXPORT_SYMBOL_GPL(ptep_test_and_clear_uc);

int set_guest_storage_key(struct mm_struct *mm, unsigned long addr,
			  unsigned char key, bool nq)
{
	unsigned long keyul, paddr;
	spinlock_t *ptl;
	pgste_t old, new;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If we don't have a PTE table and if there is no huge page mapped,
	 * we can ignore attempts to set the key to 0, because it already is 0.
	 */
	switch (pmd_lookup(mm, addr, &pmdp)) {
	case -ENOENT:
		return key ? -EFAULT : 0;
	case 0:
		break;
	default:
		return -EFAULT;
	}
again:
	ptl = pmd_lock(mm, pmdp);
	if (!pmd_present(*pmdp)) {
		spin_unlock(ptl);
		return key ? -EFAULT : 0;
	}

	if (pmd_leaf(*pmdp)) {
		paddr = pmd_val(*pmdp) & HPAGE_MASK;
		paddr |= addr & ~HPAGE_MASK;
		/*
		 * Huge pmds need quiescing operations, they are
		 * always mapped.
		 */
		page_set_storage_key(paddr, key, 1);
		spin_unlock(ptl);
		return 0;
	}
	spin_unlock(ptl);

	ptep = pte_offset_map_lock(mm, pmdp, addr, &ptl);
	if (!ptep)
		goto again;
	new = old = pgste_get_lock(ptep);
	pgste_val(new) &= ~(PGSTE_GR_BIT | PGSTE_GC_BIT |
			    PGSTE_ACC_BITS | PGSTE_FP_BIT);
	keyul = (unsigned long) key;
	pgste_val(new) |= (keyul & (_PAGE_CHANGED | _PAGE_REFERENCED)) << 48;
	pgste_val(new) |= (keyul & (_PAGE_ACC_BITS | _PAGE_FP_BIT)) << 56;
	if (!(pte_val(*ptep) & _PAGE_INVALID)) {
		unsigned long bits, skey;

		paddr = pte_val(*ptep) & PAGE_MASK;
		skey = (unsigned long) page_get_storage_key(paddr);
		bits = skey & (_PAGE_CHANGED | _PAGE_REFERENCED);
		skey = key & (_PAGE_ACC_BITS | _PAGE_FP_BIT);
		/* Set storage key ACC and FP */
		page_set_storage_key(paddr, skey, !nq);
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

/*
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

/*
 * Reset a guest reference bit (rrbe), returning the reference and changed bit.
 *
 * Returns < 0 in case of error, otherwise the cc to be reported to the guest.
 */
int reset_guest_reference_bit(struct mm_struct *mm, unsigned long addr)
{
	spinlock_t *ptl;
	unsigned long paddr;
	pgste_t old, new;
	pmd_t *pmdp;
	pte_t *ptep;
	int cc = 0;

	/*
	 * If we don't have a PTE table and if there is no huge page mapped,
	 * the storage key is 0 and there is nothing for us to do.
	 */
	switch (pmd_lookup(mm, addr, &pmdp)) {
	case -ENOENT:
		return 0;
	case 0:
		break;
	default:
		return -EFAULT;
	}
again:
	ptl = pmd_lock(mm, pmdp);
	if (!pmd_present(*pmdp)) {
		spin_unlock(ptl);
		return 0;
	}

	if (pmd_leaf(*pmdp)) {
		paddr = pmd_val(*pmdp) & HPAGE_MASK;
		paddr |= addr & ~HPAGE_MASK;
		cc = page_reset_referenced(paddr);
		spin_unlock(ptl);
		return cc;
	}
	spin_unlock(ptl);

	ptep = pte_offset_map_lock(mm, pmdp, addr, &ptl);
	if (!ptep)
		goto again;
	new = old = pgste_get_lock(ptep);
	/* Reset guest reference bit only */
	pgste_val(new) &= ~PGSTE_GR_BIT;

	if (!(pte_val(*ptep) & _PAGE_INVALID)) {
		paddr = pte_val(*ptep) & PAGE_MASK;
		cc = page_reset_referenced(paddr);
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
	return cc;
}
EXPORT_SYMBOL(reset_guest_reference_bit);

int get_guest_storage_key(struct mm_struct *mm, unsigned long addr,
			  unsigned char *key)
{
	unsigned long paddr;
	spinlock_t *ptl;
	pgste_t pgste;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If we don't have a PTE table and if there is no huge page mapped,
	 * the storage key is 0.
	 */
	*key = 0;

	switch (pmd_lookup(mm, addr, &pmdp)) {
	case -ENOENT:
		return 0;
	case 0:
		break;
	default:
		return -EFAULT;
	}
again:
	ptl = pmd_lock(mm, pmdp);
	if (!pmd_present(*pmdp)) {
		spin_unlock(ptl);
		return 0;
	}

	if (pmd_leaf(*pmdp)) {
		paddr = pmd_val(*pmdp) & HPAGE_MASK;
		paddr |= addr & ~HPAGE_MASK;
		*key = page_get_storage_key(paddr);
		spin_unlock(ptl);
		return 0;
	}
	spin_unlock(ptl);

	ptep = pte_offset_map_lock(mm, pmdp, addr, &ptl);
	if (!ptep)
		goto again;
	pgste = pgste_get_lock(ptep);
	*key = (pgste_val(pgste) & (PGSTE_ACC_BITS | PGSTE_FP_BIT)) >> 56;
	paddr = pte_val(*ptep) & PAGE_MASK;
	if (!(pte_val(*ptep) & _PAGE_INVALID))
		*key = page_get_storage_key(paddr);
	/* Reflect guest's logical view, not physical */
	*key |= (pgste_val(pgste) & (PGSTE_GR_BIT | PGSTE_GC_BIT)) >> 48;
	pgste_set_unlock(ptep, pgste);
	pte_unmap_unlock(ptep, ptl);
	return 0;
}
EXPORT_SYMBOL(get_guest_storage_key);

/**
 * pgste_perform_essa - perform ESSA actions on the PGSTE.
 * @mm: the memory context. It must have PGSTEs, no check is performed here!
 * @hva: the host virtual address of the page whose PGSTE is to be processed
 * @orc: the specific action to perform, see the ESSA_SET_* macros.
 * @oldpte: the PTE will be saved there if the pointer is not NULL.
 * @oldpgste: the old PGSTE will be saved there if the pointer is not NULL.
 *
 * Return: 1 if the page is to be added to the CBRL, otherwise 0,
 *	   or < 0 in case of error. -EINVAL is returned for invalid values
 *	   of orc, -EFAULT for invalid addresses.
 */
int pgste_perform_essa(struct mm_struct *mm, unsigned long hva, int orc,
			unsigned long *oldpte, unsigned long *oldpgste)
{
	struct vm_area_struct *vma;
	unsigned long pgstev;
	spinlock_t *ptl;
	pgste_t pgste;
	pte_t *ptep;
	int res = 0;

	WARN_ON_ONCE(orc > ESSA_MAX);
	if (unlikely(orc > ESSA_MAX))
		return -EINVAL;

	vma = vma_lookup(mm, hva);
	if (!vma || is_vm_hugetlb_page(vma))
		return -EFAULT;
	ptep = get_locked_pte(mm, hva, &ptl);
	if (unlikely(!ptep))
		return -EFAULT;
	pgste = pgste_get_lock(ptep);
	pgstev = pgste_val(pgste);
	if (oldpte)
		*oldpte = pte_val(*ptep);
	if (oldpgste)
		*oldpgste = pgstev;

	switch (orc) {
	case ESSA_GET_STATE:
		break;
	case ESSA_SET_STABLE:
		pgstev &= ~(_PGSTE_GPS_USAGE_MASK | _PGSTE_GPS_NODAT);
		pgstev |= _PGSTE_GPS_USAGE_STABLE;
		break;
	case ESSA_SET_UNUSED:
		pgstev &= ~_PGSTE_GPS_USAGE_MASK;
		pgstev |= _PGSTE_GPS_USAGE_UNUSED;
		if (pte_val(*ptep) & _PAGE_INVALID)
			res = 1;
		break;
	case ESSA_SET_VOLATILE:
		pgstev &= ~_PGSTE_GPS_USAGE_MASK;
		pgstev |= _PGSTE_GPS_USAGE_VOLATILE;
		if (pte_val(*ptep) & _PAGE_INVALID)
			res = 1;
		break;
	case ESSA_SET_POT_VOLATILE:
		pgstev &= ~_PGSTE_GPS_USAGE_MASK;
		if (!(pte_val(*ptep) & _PAGE_INVALID)) {
			pgstev |= _PGSTE_GPS_USAGE_POT_VOLATILE;
			break;
		}
		if (pgstev & _PGSTE_GPS_ZERO) {
			pgstev |= _PGSTE_GPS_USAGE_VOLATILE;
			break;
		}
		if (!(pgstev & PGSTE_GC_BIT)) {
			pgstev |= _PGSTE_GPS_USAGE_VOLATILE;
			res = 1;
			break;
		}
		break;
	case ESSA_SET_STABLE_RESIDENT:
		pgstev &= ~_PGSTE_GPS_USAGE_MASK;
		pgstev |= _PGSTE_GPS_USAGE_STABLE;
		/*
		 * Since the resident state can go away any time after this
		 * call, we will not make this page resident. We can revisit
		 * this decision if a guest will ever start using this.
		 */
		break;
	case ESSA_SET_STABLE_IF_RESIDENT:
		if (!(pte_val(*ptep) & _PAGE_INVALID)) {
			pgstev &= ~_PGSTE_GPS_USAGE_MASK;
			pgstev |= _PGSTE_GPS_USAGE_STABLE;
		}
		break;
	case ESSA_SET_STABLE_NODAT:
		pgstev &= ~_PGSTE_GPS_USAGE_MASK;
		pgstev |= _PGSTE_GPS_USAGE_STABLE | _PGSTE_GPS_NODAT;
		break;
	default:
		/* we should never get here! */
		break;
	}
	/* If we are discarding a page, set it to logical zero */
	if (res)
		pgstev |= _PGSTE_GPS_ZERO;

	pgste_val(pgste) = pgstev;
	pgste_set_unlock(ptep, pgste);
	pte_unmap_unlock(ptep, ptl);
	return res;
}
EXPORT_SYMBOL(pgste_perform_essa);

/**
 * set_pgste_bits - set specific PGSTE bits.
 * @mm: the memory context. It must have PGSTEs, no check is performed here!
 * @hva: the host virtual address of the page whose PGSTE is to be processed
 * @bits: a bitmask representing the bits that will be touched
 * @value: the values of the bits to be written. Only the bits in the mask
 *	   will be written.
 *
 * Return: 0 on success, < 0 in case of error.
 */
int set_pgste_bits(struct mm_struct *mm, unsigned long hva,
			unsigned long bits, unsigned long value)
{
	struct vm_area_struct *vma;
	spinlock_t *ptl;
	pgste_t new;
	pte_t *ptep;

	vma = vma_lookup(mm, hva);
	if (!vma || is_vm_hugetlb_page(vma))
		return -EFAULT;
	ptep = get_locked_pte(mm, hva, &ptl);
	if (unlikely(!ptep))
		return -EFAULT;
	new = pgste_get_lock(ptep);

	pgste_val(new) &= ~bits;
	pgste_val(new) |= value & bits;

	pgste_set_unlock(ptep, new);
	pte_unmap_unlock(ptep, ptl);
	return 0;
}
EXPORT_SYMBOL(set_pgste_bits);

/**
 * get_pgste - get the current PGSTE for the given address.
 * @mm: the memory context. It must have PGSTEs, no check is performed here!
 * @hva: the host virtual address of the page whose PGSTE is to be processed
 * @pgstep: will be written with the current PGSTE for the given address.
 *
 * Return: 0 on success, < 0 in case of error.
 */
int get_pgste(struct mm_struct *mm, unsigned long hva, unsigned long *pgstep)
{
	struct vm_area_struct *vma;
	spinlock_t *ptl;
	pte_t *ptep;

	vma = vma_lookup(mm, hva);
	if (!vma || is_vm_hugetlb_page(vma))
		return -EFAULT;
	ptep = get_locked_pte(mm, hva, &ptl);
	if (unlikely(!ptep))
		return -EFAULT;
	*pgstep = pgste_val(pgste_get(ptep));
	pte_unmap_unlock(ptep, ptl);
	return 0;
}
EXPORT_SYMBOL(get_pgste);
#endif
