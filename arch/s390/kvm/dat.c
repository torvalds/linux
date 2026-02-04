// SPDX-License-Identifier: GPL-2.0
/*
 *  KVM guest address space mapping code
 *
 *    Copyright IBM Corp. 2007, 2020, 2024
 *    Author(s): Claudio Imbrenda <imbrenda@linux.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 *		 David Hildenbrand <david@redhat.com>
 *		 Janosch Frank <frankja@linux.ibm.com>
 */

#include <linux/kernel.h>
#include <linux/pagewalk.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/swapops.h>
#include <linux/ksm.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/pgtable.h>
#include <linux/kvm_types.h>
#include <linux/kvm_host.h>
#include <linux/pgalloc.h>

#include <asm/page-states.h>
#include <asm/tlb.h>
#include "dat.h"

int kvm_s390_mmu_cache_topup(struct kvm_s390_mmu_cache *mc)
{
	void *o;

	for ( ; mc->n_crsts < KVM_S390_MMU_CACHE_N_CRSTS; mc->n_crsts++) {
		o = (void *)__get_free_pages(GFP_KERNEL_ACCOUNT | __GFP_COMP, CRST_ALLOC_ORDER);
		if (!o)
			return -ENOMEM;
		mc->crsts[mc->n_crsts] = o;
	}
	for ( ; mc->n_pts < KVM_S390_MMU_CACHE_N_PTS; mc->n_pts++) {
		o = (void *)__get_free_page(GFP_KERNEL_ACCOUNT);
		if (!o)
			return -ENOMEM;
		mc->pts[mc->n_pts] = o;
	}
	for ( ; mc->n_rmaps < KVM_S390_MMU_CACHE_N_RMAPS; mc->n_rmaps++) {
		o = kzalloc(sizeof(*mc->rmaps[0]), GFP_KERNEL_ACCOUNT);
		if (!o)
			return -ENOMEM;
		mc->rmaps[mc->n_rmaps] = o;
	}
	return 0;
}

static inline struct page_table *dat_alloc_pt_noinit(struct kvm_s390_mmu_cache *mc)
{
	struct page_table *res;

	res = kvm_s390_mmu_cache_alloc_pt(mc);
	if (res)
		__arch_set_page_dat(res, 1);
	return res;
}

static inline struct crst_table *dat_alloc_crst_noinit(struct kvm_s390_mmu_cache *mc)
{
	struct crst_table *res;

	res = kvm_s390_mmu_cache_alloc_crst(mc);
	if (res)
		__arch_set_page_dat(res, 1UL << CRST_ALLOC_ORDER);
	return res;
}

struct crst_table *dat_alloc_crst_sleepable(unsigned long init)
{
	struct page *page;
	void *virt;

	page = alloc_pages(GFP_KERNEL_ACCOUNT | __GFP_COMP, CRST_ALLOC_ORDER);
	if (!page)
		return NULL;
	virt = page_to_virt(page);
	__arch_set_page_dat(virt, 1UL << CRST_ALLOC_ORDER);
	crst_table_init(virt, init);
	return virt;
}

void dat_free_level(struct crst_table *table, bool owns_ptes)
{
	unsigned int i;

	for (i = 0; i < _CRST_ENTRIES; i++) {
		if (table->crstes[i].h.fc || table->crstes[i].h.i)
			continue;
		if (!is_pmd(table->crstes[i]))
			dat_free_level(dereference_crste(table->crstes[i]), owns_ptes);
		else if (owns_ptes)
			dat_free_pt(dereference_pmd(table->crstes[i].pmd));
	}
	dat_free_crst(table);
}

/**
 * dat_crstep_xchg() - Exchange a gmap CRSTE with another.
 * @crstep: Pointer to the CRST entry
 * @new: Replacement entry.
 * @gfn: The affected guest address.
 * @asce: The ASCE of the address space.
 *
 * Context: This function is assumed to be called with kvm->mmu_lock held.
 */
void dat_crstep_xchg(union crste *crstep, union crste new, gfn_t gfn, union asce asce)
{
	if (crstep->h.i) {
		WRITE_ONCE(*crstep, new);
		return;
	} else if (cpu_has_edat2()) {
		crdte_crste(crstep, *crstep, new, gfn, asce);
		return;
	}

	if (machine_has_tlb_guest())
		idte_crste(crstep, gfn, IDTE_GUEST_ASCE, asce, IDTE_GLOBAL);
	else
		idte_crste(crstep, gfn, 0, NULL_ASCE, IDTE_GLOBAL);
	WRITE_ONCE(*crstep, new);
}

/**
 * dat_crstep_xchg_atomic() - Atomically exchange a gmap CRSTE with another.
 * @crstep: Pointer to the CRST entry.
 * @old: Expected old value.
 * @new: Replacement entry.
 * @gfn: The affected guest address.
 * @asce: The asce of the address space.
 *
 * This function is needed to atomically exchange a CRSTE that potentially
 * maps a prefix area, without having to invalidate it inbetween.
 *
 * Context: This function is assumed to be called with kvm->mmu_lock held.
 *
 * Return: %true if the exchange was successful.
 */
bool dat_crstep_xchg_atomic(union crste *crstep, union crste old, union crste new, gfn_t gfn,
			    union asce asce)
{
	if (old.h.i)
		return arch_try_cmpxchg((long *)crstep, &old.val, new.val);
	if (cpu_has_edat2())
		return crdte_crste(crstep, old, new, gfn, asce);
	return cspg_crste(crstep, old, new);
}

static void dat_set_storage_key_from_pgste(union pte pte, union pgste pgste)
{
	union skey nkey = { .acc = pgste.acc, .fp = pgste.fp };

	page_set_storage_key(pte_origin(pte), nkey.skey, 0);
}

static void dat_move_storage_key(union pte old, union pte new)
{
	page_set_storage_key(pte_origin(new), page_get_storage_key(pte_origin(old)), 1);
}

static union pgste dat_save_storage_key_into_pgste(union pte pte, union pgste pgste)
{
	union skey skey;

	skey.skey = page_get_storage_key(pte_origin(pte));

	pgste.acc = skey.acc;
	pgste.fp = skey.fp;
	pgste.gr |= skey.r;
	pgste.gc |= skey.c;

	return pgste;
}

union pgste __dat_ptep_xchg(union pte *ptep, union pgste pgste, union pte new, gfn_t gfn,
			    union asce asce, bool uses_skeys)
{
	union pte old = READ_ONCE(*ptep);

	/* Updating only the software bits while holding the pgste lock. */
	if (!((ptep->val ^ new.val) & ~_PAGE_SW_BITS)) {
		WRITE_ONCE(ptep->swbyte, new.swbyte);
		return pgste;
	}

	if (!old.h.i) {
		unsigned long opts = IPTE_GUEST_ASCE | (pgste.nodat ? IPTE_NODAT : 0);

		if (machine_has_tlb_guest())
			__ptep_ipte(gfn_to_gpa(gfn), (void *)ptep, opts, asce.val, IPTE_GLOBAL);
		else
			__ptep_ipte(gfn_to_gpa(gfn), (void *)ptep, 0, 0, IPTE_GLOBAL);
	}

	if (uses_skeys) {
		if (old.h.i && !new.h.i)
			/* Invalid to valid: restore storage keys from PGSTE. */
			dat_set_storage_key_from_pgste(new, pgste);
		else if (!old.h.i && new.h.i)
			/* Valid to invalid: save storage keys to PGSTE. */
			pgste = dat_save_storage_key_into_pgste(old, pgste);
		else if (!old.h.i && !new.h.i)
			/* Valid to valid: move storage keys. */
			if (old.h.pfra != new.h.pfra)
				dat_move_storage_key(old, new);
		/* Invalid to invalid: nothing to do. */
	}

	WRITE_ONCE(*ptep, new);
	return pgste;
}
