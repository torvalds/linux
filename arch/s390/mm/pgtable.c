// SPDX-License-Identifier: GPL-2.0
/*
 *    Copyright IBM Corp. 2007, 2011
 *    Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/cpufeature.h>
#include <linux/export.h>
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
#include <linux/leafops.h>
#include <linux/sysctl.h>
#include <linux/ksm.h>
#include <linux/mman.h>

#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/page-states.h>
#include <asm/machine.h>

pgprot_t pgprot_writecombine(pgprot_t prot)
{
	/*
	 * mio_wb_bit_mask may be set on a different CPU, but it is only set
	 * once at init and only read afterwards.
	 */
	return __pgprot(pgprot_val(prot) | mio_wb_bit_mask);
}
EXPORT_SYMBOL_GPL(pgprot_writecombine);

static inline void ptep_ipte_local(struct mm_struct *mm, unsigned long addr,
				   pte_t *ptep, int nodat)
{
	unsigned long opt, asce;

	if (machine_has_tlb_guest()) {
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

	if (machine_has_tlb_guest()) {
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
	if (cpu_has_tlb_lc() &&
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

pte_t ptep_xchg_direct(struct mm_struct *mm, unsigned long addr,
		       pte_t *ptep, pte_t new)
{
	pte_t old;

	preempt_disable();
	old = ptep_flush_direct(mm, addr, ptep, 1);
	set_pte(ptep, new);
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
		__ptep_rdp(addr, ptep, 1);
	else
		__ptep_rdp(addr, ptep, 0);
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
	pte_t old;

	preempt_disable();
	old = ptep_flush_lazy(mm, addr, ptep, 1);
	set_pte(ptep, new);
	preempt_enable();
	return old;
}
EXPORT_SYMBOL(ptep_xchg_lazy);

pte_t ptep_modify_prot_start(struct vm_area_struct *vma, unsigned long addr,
			     pte_t *ptep)
{
	return ptep_flush_lazy(vma->vm_mm, addr, ptep, 1);
}

void ptep_modify_prot_commit(struct vm_area_struct *vma, unsigned long addr,
			     pte_t *ptep, pte_t old_pte, pte_t pte)
{
	set_pte(ptep, pte);
}

static inline void pmdp_idte_local(struct mm_struct *mm,
				   unsigned long addr, pmd_t *pmdp)
{
	if (machine_has_tlb_guest())
		__pmdp_idte(addr, pmdp, IDTE_NODAT | IDTE_GUEST_ASCE, mm->context.asce, IDTE_LOCAL);
	else
		__pmdp_idte(addr, pmdp, 0, 0, IDTE_LOCAL);
}

static inline void pmdp_idte_global(struct mm_struct *mm,
				    unsigned long addr, pmd_t *pmdp)
{
	if (machine_has_tlb_guest()) {
		__pmdp_idte(addr, pmdp, IDTE_NODAT | IDTE_GUEST_ASCE,
			    mm->context.asce, IDTE_GLOBAL);
	} else {
		__pmdp_idte(addr, pmdp, 0, 0, IDTE_GLOBAL);
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
	if (cpu_has_tlb_lc() &&
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
	} else {
		pmdp_idte_global(mm, addr, pmdp);
	}
	atomic_dec(&mm->context.flush_count);
	return old;
}

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
	if (machine_has_tlb_guest())
		__pudp_idte(addr, pudp, IDTE_NODAT | IDTE_GUEST_ASCE,
			    mm->context.asce, IDTE_LOCAL);
	else
		__pudp_idte(addr, pudp, 0, 0, IDTE_LOCAL);
}

static inline void pudp_idte_global(struct mm_struct *mm,
				    unsigned long addr, pud_t *pudp)
{
	if (machine_has_tlb_guest())
		__pudp_idte(addr, pudp, IDTE_NODAT | IDTE_GUEST_ASCE,
			    mm->context.asce, IDTE_GLOBAL);
	else
		__pudp_idte(addr, pudp, 0, 0, IDTE_GLOBAL);
}

static inline pud_t pudp_flush_direct(struct mm_struct *mm,
				      unsigned long addr, pud_t *pudp)
{
	pud_t old;

	old = *pudp;
	if (pud_val(old) & _REGION_ENTRY_INVALID)
		return old;
	atomic_inc(&mm->context.flush_count);
	if (cpu_has_tlb_lc() &&
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
