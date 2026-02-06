// SPDX-License-Identifier: GPL-2.0
/*
 * Guest memory management for KVM/s390
 *
 * Copyright IBM Corp. 2008, 2020, 2024
 *
 *    Author(s): Claudio Imbrenda <imbrenda@linux.ibm.com>
 *               Martin Schwidefsky <schwidefsky@de.ibm.com>
 *               David Hildenbrand <david@redhat.com>
 *               Janosch Frank <frankja@linux.ibm.com>
 */

#include <linux/compiler.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/pgtable.h>
#include <linux/pagemap.h>
#include <asm/lowcore.h>
#include <asm/uv.h>
#include <asm/gmap_helpers.h>

#include "dat.h"
#include "gmap.h"
#include "kvm-s390.h"
#include "faultin.h"

static inline bool kvm_s390_is_in_sie(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.sie_block->prog0c & PROG_IN_SIE;
}

static int gmap_limit_to_type(gfn_t limit)
{
	if (!limit)
		return TABLE_TYPE_REGION1;
	if (limit <= _REGION3_SIZE >> PAGE_SHIFT)
		return TABLE_TYPE_SEGMENT;
	if (limit <= _REGION2_SIZE >> PAGE_SHIFT)
		return TABLE_TYPE_REGION3;
	if (limit <= _REGION1_SIZE >> PAGE_SHIFT)
		return TABLE_TYPE_REGION2;
	return TABLE_TYPE_REGION1;
}

/**
 * gmap_new() - Allocate and initialize a guest address space.
 * @kvm: The kvm owning the guest.
 * @limit: Maximum address of the gmap address space.
 *
 * Return: A guest address space structure.
 */
struct gmap *gmap_new(struct kvm *kvm, gfn_t limit)
{
	struct crst_table *table;
	struct gmap *gmap;
	int type;

	type = gmap_limit_to_type(limit);

	gmap = kzalloc(sizeof(*gmap), GFP_KERNEL_ACCOUNT);
	if (!gmap)
		return NULL;
	INIT_LIST_HEAD(&gmap->children);
	INIT_LIST_HEAD(&gmap->list);
	INIT_LIST_HEAD(&gmap->scb_users);
	INIT_RADIX_TREE(&gmap->host_to_rmap, GFP_KVM_S390_MMU_CACHE);
	spin_lock_init(&gmap->children_lock);
	spin_lock_init(&gmap->host_to_rmap_lock);
	refcount_set(&gmap->refcount, 1);

	table = dat_alloc_crst_sleepable(_CRSTE_EMPTY(type).val);
	if (!table) {
		kfree(gmap);
		return NULL;
	}

	gmap->asce.val = __pa(table);
	gmap->asce.dt = type;
	gmap->asce.tl = _ASCE_TABLE_LENGTH;
	gmap->asce.x = 1;
	gmap->asce.p = 1;
	gmap->asce.s = 1;
	gmap->kvm = kvm;
	set_bit(GMAP_FLAG_OWNS_PAGETABLES, &gmap->flags);

	return gmap;
}

static void gmap_add_child(struct gmap *parent, struct gmap *child)
{
	KVM_BUG_ON(is_ucontrol(parent) && parent->parent, parent->kvm);
	KVM_BUG_ON(is_ucontrol(parent) && !owns_page_tables(parent), parent->kvm);
	KVM_BUG_ON(!refcount_read(&child->refcount), parent->kvm);
	lockdep_assert_held(&parent->children_lock);

	child->parent = parent;

	if (is_ucontrol(parent))
		set_bit(GMAP_FLAG_IS_UCONTROL, &child->flags);
	else
		clear_bit(GMAP_FLAG_IS_UCONTROL, &child->flags);

	if (test_bit(GMAP_FLAG_ALLOW_HPAGE_1M, &parent->flags))
		set_bit(GMAP_FLAG_ALLOW_HPAGE_1M, &child->flags);
	else
		clear_bit(GMAP_FLAG_ALLOW_HPAGE_1M, &child->flags);

	if (kvm_is_ucontrol(parent->kvm))
		clear_bit(GMAP_FLAG_OWNS_PAGETABLES, &child->flags);
	list_add(&child->list, &parent->children);
}

struct gmap *gmap_new_child(struct gmap *parent, gfn_t limit)
{
	struct gmap *res;

	lockdep_assert_not_held(&parent->children_lock);
	res = gmap_new(parent->kvm, limit);
	if (res) {
		scoped_guard(spinlock, &parent->children_lock)
			gmap_add_child(parent, res);
	}
	return res;
}

int gmap_set_limit(struct gmap *gmap, gfn_t limit)
{
	struct kvm_s390_mmu_cache *mc;
	int rc, type;

	type = gmap_limit_to_type(limit);

	mc = kvm_s390_new_mmu_cache();
	if (!mc)
		return -ENOMEM;

	do {
		rc = kvm_s390_mmu_cache_topup(mc);
		if (rc)
			return rc;
		scoped_guard(write_lock, &gmap->kvm->mmu_lock)
			rc = dat_set_asce_limit(mc, &gmap->asce, type);
	} while (rc == -ENOMEM);

	kvm_s390_free_mmu_cache(mc);
	return 0;
}

static void gmap_rmap_radix_tree_free(struct radix_tree_root *root)
{
	struct vsie_rmap *rmap, *rnext, *head;
	struct radix_tree_iter iter;
	unsigned long indices[16];
	unsigned long index;
	void __rcu **slot;
	int i, nr;

	/* A radix tree is freed by deleting all of its entries */
	index = 0;
	do {
		nr = 0;
		radix_tree_for_each_slot(slot, root, &iter, index) {
			indices[nr] = iter.index;
			if (++nr == 16)
				break;
		}
		for (i = 0; i < nr; i++) {
			index = indices[i];
			head = radix_tree_delete(root, index);
			gmap_for_each_rmap_safe(rmap, rnext, head)
				kfree(rmap);
		}
	} while (nr > 0);
}

void gmap_remove_child(struct gmap *child)
{
	if (KVM_BUG_ON(!child->parent, child->kvm))
		return;
	lockdep_assert_held(&child->parent->children_lock);

	list_del(&child->list);
	child->parent = NULL;
}

/**
 * gmap_dispose() - Remove and free a guest address space and its children.
 * @gmap: Pointer to the guest address space structure.
 */
void gmap_dispose(struct gmap *gmap)
{
	/* The gmap must have been removed from the parent beforehands */
	KVM_BUG_ON(gmap->parent, gmap->kvm);
	/* All children of this gmap must have been removed beforehands */
	KVM_BUG_ON(!list_empty(&gmap->children), gmap->kvm);
	/* No VSIE shadow block is allowed to use this gmap */
	KVM_BUG_ON(!list_empty(&gmap->scb_users), gmap->kvm);
	/* The ASCE must be valid */
	KVM_BUG_ON(!gmap->asce.val, gmap->kvm);
	/* The refcount must be 0 */
	KVM_BUG_ON(refcount_read(&gmap->refcount), gmap->kvm);

	/* Flush tlb of all gmaps */
	asce_flush_tlb(gmap->asce);

	/* Free all DAT tables. */
	dat_free_level(dereference_asce(gmap->asce), owns_page_tables(gmap));

	/* Free additional data for a shadow gmap */
	if (is_shadow(gmap))
		gmap_rmap_radix_tree_free(&gmap->host_to_rmap);

	kfree(gmap);
}

/**
 * s390_replace_asce() - Try to replace the current ASCE of a gmap with a copy.
 * @gmap: The gmap whose ASCE needs to be replaced.
 *
 * If the ASCE is a SEGMENT type then this function will return -EINVAL,
 * otherwise the pointers in the host_to_guest radix tree will keep pointing
 * to the wrong pages, causing use-after-free and memory corruption.
 * If the allocation of the new top level page table fails, the ASCE is not
 * replaced.
 * In any case, the old ASCE is always removed from the gmap CRST list.
 * Therefore the caller has to make sure to save a pointer to it
 * beforehand, unless a leak is actually intended.
 *
 * Return: 0 in case of success, -EINVAL if the ASCE is segment type ASCE,
 *         -ENOMEM if runinng out of memory.
 */
int s390_replace_asce(struct gmap *gmap)
{
	struct crst_table *table;
	union asce asce;

	/* Replacing segment type ASCEs would cause serious issues */
	if (gmap->asce.dt == ASCE_TYPE_SEGMENT)
		return -EINVAL;

	table = dat_alloc_crst_sleepable(0);
	if (!table)
		return -ENOMEM;
	memcpy(table, dereference_asce(gmap->asce), sizeof(*table));

	/* Set new table origin while preserving existing ASCE control bits */
	asce = gmap->asce;
	asce.rsto = virt_to_pfn(table);
	WRITE_ONCE(gmap->asce, asce);

	return 0;
}

bool _gmap_unmap_prefix(struct gmap *gmap, gfn_t gfn, gfn_t end, bool hint)
{
	struct kvm *kvm = gmap->kvm;
	struct kvm_vcpu *vcpu;
	gfn_t prefix_gfn;
	unsigned long i;

	if (is_shadow(gmap))
		return false;
	kvm_for_each_vcpu(i, vcpu, kvm) {
		/* Match against both prefix pages */
		prefix_gfn = gpa_to_gfn(kvm_s390_get_prefix(vcpu));
		if (prefix_gfn < end && gfn <= prefix_gfn + 1) {
			if (hint && kvm_s390_is_in_sie(vcpu))
				return false;
			VCPU_EVENT(vcpu, 2, "gmap notifier for %llx-%llx",
				   gfn_to_gpa(gfn), gfn_to_gpa(end));
			kvm_s390_sync_request(KVM_REQ_REFRESH_GUEST_PREFIX, vcpu);
		}
	}
	return true;
}

struct clear_young_pte_priv {
	struct gmap *gmap;
	bool young;
};

static long gmap_clear_young_pte(union pte *ptep, gfn_t gfn, gfn_t end, struct dat_walk *walk)
{
	struct clear_young_pte_priv *p = walk->priv;
	union pgste pgste;
	union pte pte, new;

	pte = READ_ONCE(*ptep);

	if (!pte.s.pr || (!pte.s.y && pte.h.i))
		return 0;

	pgste = pgste_get_lock(ptep);
	if (!pgste.prefix_notif || gmap_mkold_prefix(p->gmap, gfn, end)) {
		new = pte;
		new.h.i = 1;
		new.s.y = 0;
		if ((new.s.d || !new.h.p) && !new.s.s)
			folio_set_dirty(pfn_folio(pte.h.pfra));
		new.s.d = 0;
		new.h.p = 1;

		pgste.prefix_notif = 0;
		pgste = __dat_ptep_xchg(ptep, pgste, new, gfn, walk->asce, uses_skeys(p->gmap));
	}
	p->young = 1;
	pgste_set_unlock(ptep, pgste);
	return 0;
}

static long gmap_clear_young_crste(union crste *crstep, gfn_t gfn, gfn_t end, struct dat_walk *walk)
{
	struct clear_young_pte_priv *priv = walk->priv;
	union crste crste, new;

	crste = READ_ONCE(*crstep);

	if (!crste.h.fc)
		return 0;
	if (!crste.s.fc1.y && crste.h.i)
		return 0;
	if (!crste_prefix(crste) || gmap_mkold_prefix(priv->gmap, gfn, end)) {
		new = crste;
		new.h.i = 1;
		new.s.fc1.y = 0;
		new.s.fc1.prefix_notif = 0;
		if (new.s.fc1.d || !new.h.p)
			folio_set_dirty(phys_to_folio(crste_origin_large(crste)));
		new.s.fc1.d = 0;
		new.h.p = 1;
		dat_crstep_xchg(crstep, new, gfn, walk->asce);
	}
	priv->young = 1;
	return 0;
}

/**
 * gmap_age_gfn() - Clear young.
 * @gmap: The guest gmap.
 * @start: The first gfn to test.
 * @end: The gfn after the last one to test.
 *
 * Context: Called with the kvm mmu write lock held.
 * Return: 1 if any page in the given range was young, otherwise 0.
 */
bool gmap_age_gfn(struct gmap *gmap, gfn_t start, gfn_t end)
{
	const struct dat_walk_ops ops = {
		.pte_entry = gmap_clear_young_pte,
		.pmd_entry = gmap_clear_young_crste,
		.pud_entry = gmap_clear_young_crste,
	};
	struct clear_young_pte_priv priv = {
		.gmap = gmap,
		.young = false,
	};

	_dat_walk_gfn_range(start, end, gmap->asce, &ops, 0, &priv);

	return priv.young;
}

struct gmap_unmap_priv {
	struct gmap *gmap;
	struct kvm_memory_slot *slot;
};

static long _gmap_unmap_pte(union pte *ptep, gfn_t gfn, gfn_t next, struct dat_walk *w)
{
	struct gmap_unmap_priv *priv = w->priv;
	struct folio *folio = NULL;
	unsigned long vmaddr;
	union pgste pgste;

	pgste = pgste_get_lock(ptep);
	if (ptep->s.pr && pgste.usage == PGSTE_GPS_USAGE_UNUSED) {
		vmaddr = __gfn_to_hva_memslot(priv->slot, gfn);
		gmap_helper_try_set_pte_unused(priv->gmap->kvm->mm, vmaddr);
	}
	if (ptep->s.pr && test_bit(GMAP_FLAG_EXPORT_ON_UNMAP, &priv->gmap->flags))
		folio = pfn_folio(ptep->h.pfra);
	pgste = gmap_ptep_xchg(priv->gmap, ptep, _PTE_EMPTY, pgste, gfn);
	pgste_set_unlock(ptep, pgste);
	if (folio)
		uv_convert_from_secure_folio(folio);

	return 0;
}

static long _gmap_unmap_crste(union crste *crstep, gfn_t gfn, gfn_t next, struct dat_walk *walk)
{
	struct gmap_unmap_priv *priv = walk->priv;
	struct folio *folio = NULL;

	if (crstep->h.fc) {
		if (crstep->s.fc1.pr && test_bit(GMAP_FLAG_EXPORT_ON_UNMAP, &priv->gmap->flags))
			folio = phys_to_folio(crste_origin_large(*crstep));
		gmap_crstep_xchg(priv->gmap, crstep, _CRSTE_EMPTY(crstep->h.tt), gfn);
		if (folio)
			uv_convert_from_secure_folio(folio);
	}

	return 0;
}

/**
 * gmap_unmap_gfn_range() - Unmap a range of guest addresses.
 * @gmap: The gmap to act on.
 * @slot: The memslot in which the range is located.
 * @start: The first gfn to unmap.
 * @end: The gfn after the last one to unmap.
 *
 * Context: Called with the kvm mmu write lock held.
 * Return: false
 */
bool gmap_unmap_gfn_range(struct gmap *gmap, struct kvm_memory_slot *slot, gfn_t start, gfn_t end)
{
	const struct dat_walk_ops ops = {
		.pte_entry = _gmap_unmap_pte,
		.pmd_entry = _gmap_unmap_crste,
		.pud_entry = _gmap_unmap_crste,
	};
	struct gmap_unmap_priv priv = {
		.gmap = gmap,
		.slot = slot,
	};

	lockdep_assert_held_write(&gmap->kvm->mmu_lock);

	_dat_walk_gfn_range(start, end, gmap->asce, &ops, 0, &priv);
	return false;
}

static union pgste __pte_test_and_clear_softdirty(union pte *ptep, union pgste pgste, gfn_t gfn,
						  struct gmap *gmap)
{
	union pte pte = READ_ONCE(*ptep);

	if (!pte.s.pr || (pte.h.p && !pte.s.sd))
		return pgste;

	/*
	 * If this page contains one or more prefixes of vCPUS that are currently
	 * running, do not reset the protection, leave it marked as dirty.
	 */
	if (!pgste.prefix_notif || gmap_mkold_prefix(gmap, gfn, gfn + 1)) {
		pte.h.p = 1;
		pte.s.sd = 0;
		pgste = gmap_ptep_xchg(gmap, ptep, pte, pgste, gfn);
	}

	mark_page_dirty(gmap->kvm, gfn);

	return pgste;
}

static long _pte_test_and_clear_softdirty(union pte *ptep, gfn_t gfn, gfn_t end,
					  struct dat_walk *walk)
{
	struct gmap *gmap = walk->priv;
	union pgste pgste;

	pgste = pgste_get_lock(ptep);
	pgste = __pte_test_and_clear_softdirty(ptep, pgste, gfn, gmap);
	pgste_set_unlock(ptep, pgste);
	return 0;
}

static long _crste_test_and_clear_softdirty(union crste *table, gfn_t gfn, gfn_t end,
					    struct dat_walk *walk)
{
	struct gmap *gmap = walk->priv;
	union crste crste, new;

	if (fatal_signal_pending(current))
		return 1;
	crste = READ_ONCE(*table);
	if (!crste.h.fc)
		return 0;
	if (crste.h.p && !crste.s.fc1.sd)
		return 0;

	/*
	 * If this large page contains one or more prefixes of vCPUs that are
	 * currently running, do not reset the protection, leave it marked as
	 * dirty.
	 */
	if (!crste.s.fc1.prefix_notif || gmap_mkold_prefix(gmap, gfn, end)) {
		new = crste;
		new.h.p = 1;
		new.s.fc1.sd = 0;
		gmap_crstep_xchg(gmap, table, new, gfn);
	}

	for ( ; gfn < end; gfn++)
		mark_page_dirty(gmap->kvm, gfn);

	return 0;
}

void gmap_sync_dirty_log(struct gmap *gmap, gfn_t start, gfn_t end)
{
	const struct dat_walk_ops walk_ops = {
		.pte_entry = _pte_test_and_clear_softdirty,
		.pmd_entry = _crste_test_and_clear_softdirty,
		.pud_entry = _crste_test_and_clear_softdirty,
	};

	lockdep_assert_held(&gmap->kvm->mmu_lock);

	_dat_walk_gfn_range(start, end, gmap->asce, &walk_ops, 0, gmap);
}

static int gmap_handle_minor_crste_fault(union asce asce, struct guest_fault *f)
{
	union crste newcrste, oldcrste = READ_ONCE(*f->crstep);

	/* Somehow the crste is not large anymore, let the slow path deal with it. */
	if (!oldcrste.h.fc)
		return 1;

	f->pfn = PHYS_PFN(large_crste_to_phys(oldcrste, f->gfn));
	f->writable = oldcrste.s.fc1.w;

	/* Appropriate permissions already (race with another handler), nothing to do. */
	if (!oldcrste.h.i && !(f->write_attempt && oldcrste.h.p))
		return 0;

	if (!f->write_attempt || oldcrste.s.fc1.w) {
		f->write_attempt |= oldcrste.s.fc1.w && oldcrste.s.fc1.d;
		newcrste = oldcrste;
		newcrste.h.i = 0;
		newcrste.s.fc1.y = 1;
		if (f->write_attempt) {
			newcrste.h.p = 0;
			newcrste.s.fc1.d = 1;
			newcrste.s.fc1.sd = 1;
		}
		if (!oldcrste.s.fc1.d && newcrste.s.fc1.d)
			SetPageDirty(phys_to_page(crste_origin_large(newcrste)));
		/* In case of races, let the slow path deal with it. */
		return !dat_crstep_xchg_atomic(f->crstep, oldcrste, newcrste, f->gfn, asce);
	}
	/* Trying to write on a read-only page, let the slow path deal with it. */
	return 1;
}

static int _gmap_handle_minor_pte_fault(struct gmap *gmap, union pgste *pgste,
					struct guest_fault *f)
{
	union pte newpte, oldpte = READ_ONCE(*f->ptep);

	f->pfn = oldpte.h.pfra;
	f->writable = oldpte.s.w;

	/* Appropriate permissions already (race with another handler), nothing to do. */
	if (!oldpte.h.i && !(f->write_attempt && oldpte.h.p))
		return 0;
	/* Trying to write on a read-only page, let the slow path deal with it. */
	if (!oldpte.s.pr || (f->write_attempt && !oldpte.s.w))
		return 1;

	newpte = oldpte;
	newpte.h.i = 0;
	newpte.s.y = 1;
	if (f->write_attempt) {
		newpte.h.p = 0;
		newpte.s.d = 1;
		newpte.s.sd = 1;
	}
	if (!oldpte.s.d && newpte.s.d)
		SetPageDirty(pfn_to_page(newpte.h.pfra));
	*pgste = gmap_ptep_xchg(gmap, f->ptep, newpte, *pgste, f->gfn);

	return 0;
}

/**
 * gmap_try_fixup_minor() -- Try to fixup a minor gmap fault.
 * @gmap: The gmap whose fault needs to be resolved.
 * @fault: Describes the fault that is being resolved.
 *
 * A minor fault is a fault that can be resolved quickly within gmap.
 * The page is already mapped, the fault is only due to dirty/young tracking.
 *
 * Return: 0 in case of success, < 0 in case of error, > 0 if the fault could
 *         not be resolved and needs to go through the slow path.
 */
int gmap_try_fixup_minor(struct gmap *gmap, struct guest_fault *fault)
{
	union pgste pgste;
	int rc;

	lockdep_assert_held(&gmap->kvm->mmu_lock);

	rc = dat_entry_walk(NULL, fault->gfn, gmap->asce, DAT_WALK_LEAF, TABLE_TYPE_PAGE_TABLE,
			    &fault->crstep, &fault->ptep);
	/* If a PTE or a leaf CRSTE could not be reached, slow path. */
	if (rc)
		return 1;

	if (fault->ptep) {
		pgste = pgste_get_lock(fault->ptep);
		rc = _gmap_handle_minor_pte_fault(gmap, &pgste, fault);
		if (!rc && fault->callback)
			fault->callback(fault);
		pgste_set_unlock(fault->ptep, pgste);
	} else {
		rc = gmap_handle_minor_crste_fault(gmap->asce, fault);
		if (!rc && fault->callback)
			fault->callback(fault);
	}
	return rc;
}

static inline bool gmap_2g_allowed(struct gmap *gmap, gfn_t gfn)
{
	return false;
}

static inline bool gmap_1m_allowed(struct gmap *gmap, gfn_t gfn)
{
	return test_bit(GMAP_FLAG_ALLOW_HPAGE_1M, &gmap->flags);
}

int gmap_link(struct kvm_s390_mmu_cache *mc, struct gmap *gmap, struct guest_fault *f)
{
	unsigned int order;
	int rc, level;

	lockdep_assert_held(&gmap->kvm->mmu_lock);

	level = TABLE_TYPE_PAGE_TABLE;
	if (f->page) {
		order = folio_order(page_folio(f->page));
		if (order >= get_order(_REGION3_SIZE) && gmap_2g_allowed(gmap, f->gfn))
			level = TABLE_TYPE_REGION3;
		else if (order >= get_order(_SEGMENT_SIZE) && gmap_1m_allowed(gmap, f->gfn))
			level = TABLE_TYPE_SEGMENT;
	}
	rc = dat_link(mc, gmap->asce, level, uses_skeys(gmap), f);
	KVM_BUG_ON(rc == -EINVAL, gmap->kvm);
	return rc;
}

static int gmap_ucas_map_one(struct kvm_s390_mmu_cache *mc, struct gmap *gmap,
			     gfn_t p_gfn, gfn_t c_gfn, bool force_alloc)
{
	struct page_table *pt;
	union crste newcrste;
	union crste *crstep;
	union pte *ptep;
	int rc;

	if (force_alloc)
		rc = dat_entry_walk(mc, p_gfn, gmap->parent->asce, DAT_WALK_ALLOC,
				    TABLE_TYPE_PAGE_TABLE, &crstep, &ptep);
	else
		rc = dat_entry_walk(mc, p_gfn, gmap->parent->asce, DAT_WALK_ALLOC_CONTINUE,
				    TABLE_TYPE_SEGMENT, &crstep, &ptep);
	if (rc)
		return rc;
	if (!ptep) {
		newcrste = _crste_fc0(p_gfn, TABLE_TYPE_SEGMENT);
		newcrste.h.i = 1;
		newcrste.h.fc0.tl = 1;
	} else {
		pt = pte_table_start(ptep);
		dat_set_ptval(pt, PTVAL_VMADDR, p_gfn >> (_SEGMENT_SHIFT - PAGE_SHIFT));
		newcrste = _crste_fc0(virt_to_pfn(pt), TABLE_TYPE_SEGMENT);
	}
	rc = dat_entry_walk(mc, c_gfn, gmap->asce, DAT_WALK_ALLOC, TABLE_TYPE_SEGMENT,
			    &crstep, &ptep);
	if (rc)
		return rc;
	dat_crstep_xchg(crstep, newcrste, c_gfn, gmap->asce);
	return 0;
}

static int gmap_ucas_translate_simple(struct gmap *gmap, gpa_t *gaddr, union crste **crstepp)
{
	union pte *ptep;
	int rc;

	rc = dat_entry_walk(NULL, gpa_to_gfn(*gaddr), gmap->asce, DAT_WALK_CONTINUE,
			    TABLE_TYPE_SEGMENT, crstepp, &ptep);
	if (rc || (!ptep && !crste_is_ucas(**crstepp)))
		return -EREMOTE;
	if (!ptep)
		return 1;
	*gaddr &= ~_SEGMENT_MASK;
	*gaddr |= dat_get_ptval(pte_table_start(ptep), PTVAL_VMADDR) << _SEGMENT_SHIFT;
	return 0;
}

/**
 * gmap_ucas_translate() - Translate a vcpu address into a host gmap address
 * @mc: The memory cache to be used for allocations.
 * @gmap: The per-cpu gmap.
 * @gaddr: Pointer to the address to be translated, will get overwritten with
 *         the translated address in case of success.
 * Translates the per-vCPU guest address into a fake guest address, which can
 * then be used with the fake memslots that are identity mapping userspace.
 * This allows ucontrol VMs to use the normal fault resolution path, like
 * normal VMs.
 *
 * Return: %0 in case of success, otherwise %-EREMOTE.
 */
int gmap_ucas_translate(struct kvm_s390_mmu_cache *mc, struct gmap *gmap, gpa_t *gaddr)
{
	gpa_t translated_address;
	union crste *crstep;
	gfn_t gfn;
	int rc;

	gfn = gpa_to_gfn(*gaddr);

	scoped_guard(read_lock, &gmap->kvm->mmu_lock) {
		rc = gmap_ucas_translate_simple(gmap, gaddr, &crstep);
		if (rc <= 0)
			return rc;
	}
	do {
		scoped_guard(write_lock, &gmap->kvm->mmu_lock) {
			rc = gmap_ucas_translate_simple(gmap, gaddr, &crstep);
			if (rc <= 0)
				return rc;
			translated_address = (*gaddr & ~_SEGMENT_MASK) |
					     (crstep->val & _SEGMENT_MASK);
			rc = gmap_ucas_map_one(mc, gmap, gpa_to_gfn(translated_address), gfn, true);
		}
		if (!rc) {
			*gaddr = translated_address;
			return 0;
		}
		if (rc != -ENOMEM)
			return -EREMOTE;
		rc = kvm_s390_mmu_cache_topup(mc);
		if (rc)
			return rc;
	} while (1);
	return 0;
}

int gmap_ucas_map(struct gmap *gmap, gfn_t p_gfn, gfn_t c_gfn, unsigned long count)
{
	struct kvm_s390_mmu_cache *mc;
	int rc;

	mc = kvm_s390_new_mmu_cache();
	if (!mc)
		return -ENOMEM;

	while (count) {
		scoped_guard(write_lock, &gmap->kvm->mmu_lock)
			rc = gmap_ucas_map_one(mc, gmap, p_gfn, c_gfn, false);
		if (rc == -ENOMEM) {
			rc = kvm_s390_mmu_cache_topup(mc);
			if (rc)
				return rc;
			continue;
		}
		if (rc)
			return rc;

		count--;
		c_gfn += _PAGE_ENTRIES;
		p_gfn += _PAGE_ENTRIES;
	}
	return rc;
}

static void gmap_ucas_unmap_one(struct gmap *gmap, gfn_t c_gfn)
{
	union crste *crstep;
	union pte *ptep;
	int rc;

	rc = dat_entry_walk(NULL, c_gfn, gmap->asce, 0, TABLE_TYPE_SEGMENT, &crstep, &ptep);
	if (!rc)
		dat_crstep_xchg(crstep, _PMD_EMPTY, c_gfn, gmap->asce);
}

void gmap_ucas_unmap(struct gmap *gmap, gfn_t c_gfn, unsigned long count)
{
	guard(read_lock)(&gmap->kvm->mmu_lock);

	for ( ; count; count--, c_gfn += _PAGE_ENTRIES)
		gmap_ucas_unmap_one(gmap, c_gfn);
}

static long _gmap_split_crste(union crste *crstep, gfn_t gfn, gfn_t next, struct dat_walk *walk)
{
	struct gmap *gmap = walk->priv;
	union crste crste, newcrste;

	crste = READ_ONCE(*crstep);
	newcrste = _CRSTE_EMPTY(crste.h.tt);

	while (crste_leaf(crste)) {
		if (crste_prefix(crste))
			gmap_unmap_prefix(gmap, gfn, next);
		if (crste.s.fc1.vsie_notif)
			gmap_handle_vsie_unshadow_event(gmap, gfn);
		if (dat_crstep_xchg_atomic(crstep, crste, newcrste, gfn, walk->asce))
			break;
		crste = READ_ONCE(*crstep);
	}

	if (need_resched())
		return next;

	return 0;
}

void gmap_split_huge_pages(struct gmap *gmap)
{
	const struct dat_walk_ops ops = {
		.pmd_entry = _gmap_split_crste,
		.pud_entry = _gmap_split_crste,
	};
	gfn_t start = 0;

	do {
		scoped_guard(read_lock, &gmap->kvm->mmu_lock)
			start = _dat_walk_gfn_range(start, asce_end(gmap->asce), gmap->asce,
						    &ops, DAT_WALK_IGN_HOLES, gmap);
		cond_resched();
	} while (start);
}

static int _gmap_enable_skeys(struct gmap *gmap)
{
	gfn_t start = 0;
	int rc;

	if (uses_skeys(gmap))
		return 0;

	set_bit(GMAP_FLAG_USES_SKEYS, &gmap->flags);
	rc = gmap_helper_disable_cow_sharing();
	if (rc) {
		clear_bit(GMAP_FLAG_USES_SKEYS, &gmap->flags);
		return rc;
	}

	do {
		scoped_guard(write_lock, &gmap->kvm->mmu_lock)
			start = dat_reset_skeys(gmap->asce, start);
		cond_resched();
	} while (start);
	return 0;
}

int gmap_enable_skeys(struct gmap *gmap)
{
	int rc;

	mmap_write_lock(gmap->kvm->mm);
	rc = _gmap_enable_skeys(gmap);
	mmap_write_unlock(gmap->kvm->mm);
	return rc;
}

static long _destroy_pages_pte(union pte *ptep, gfn_t gfn, gfn_t next, struct dat_walk *walk)
{
	if (!ptep->s.pr)
		return 0;
	__kvm_s390_pv_destroy_page(phys_to_page(pte_origin(*ptep)));
	if (need_resched())
		return next;
	return 0;
}

static long _destroy_pages_crste(union crste *crstep, gfn_t gfn, gfn_t next, struct dat_walk *walk)
{
	phys_addr_t origin, cur, end;

	if (!crstep->h.fc || !crstep->s.fc1.pr)
		return 0;

	origin = crste_origin_large(*crstep);
	cur = ((max(gfn, walk->start) - gfn) << PAGE_SHIFT) + origin;
	end = ((min(next, walk->end) - gfn) << PAGE_SHIFT) + origin;
	for ( ; cur < end; cur += PAGE_SIZE)
		__kvm_s390_pv_destroy_page(phys_to_page(cur));
	if (need_resched())
		return next;
	return 0;
}

int gmap_pv_destroy_range(struct gmap *gmap, gfn_t start, gfn_t end, bool interruptible)
{
	const struct dat_walk_ops ops = {
		.pte_entry = _destroy_pages_pte,
		.pmd_entry = _destroy_pages_crste,
		.pud_entry = _destroy_pages_crste,
	};

	do {
		scoped_guard(read_lock, &gmap->kvm->mmu_lock)
			start = _dat_walk_gfn_range(start, end, gmap->asce, &ops,
						    DAT_WALK_IGN_HOLES, NULL);
		if (interruptible && fatal_signal_pending(current))
			return -EINTR;
		cond_resched();
	} while (start && start < end);
	return 0;
}

int gmap_insert_rmap(struct gmap *sg, gfn_t p_gfn, gfn_t r_gfn, int level)
{
	struct vsie_rmap *rmap __free(kvfree) = NULL;
	struct vsie_rmap *temp;
	void __rcu **slot;
	int rc = 0;

	KVM_BUG_ON(!is_shadow(sg), sg->kvm);
	lockdep_assert_held(&sg->host_to_rmap_lock);

	rmap = kzalloc(sizeof(*rmap), GFP_ATOMIC);
	if (!rmap)
		return -ENOMEM;

	rmap->r_gfn = r_gfn;
	rmap->level = level;
	slot = radix_tree_lookup_slot(&sg->host_to_rmap, p_gfn);
	if (slot) {
		rmap->next = radix_tree_deref_slot_protected(slot, &sg->host_to_rmap_lock);
		for (temp = rmap->next; temp; temp = temp->next) {
			if (temp->val == rmap->val)
				return 0;
		}
		radix_tree_replace_slot(&sg->host_to_rmap, slot, rmap);
	} else {
		rmap->next = NULL;
		rc = radix_tree_insert(&sg->host_to_rmap, p_gfn, rmap);
		if (rc)
			return rc;
	}
	rmap = NULL;

	return 0;
}

int gmap_protect_rmap(struct kvm_s390_mmu_cache *mc, struct gmap *sg, gfn_t p_gfn, gfn_t r_gfn,
		      kvm_pfn_t pfn, int level, bool wr)
{
	union crste *crstep;
	union pgste pgste;
	union pte *ptep;
	union pte pte;
	int flags, rc;

	KVM_BUG_ON(!is_shadow(sg), sg->kvm);
	lockdep_assert_held(&sg->parent->children_lock);

	flags = DAT_WALK_SPLIT_ALLOC | (uses_skeys(sg->parent) ? DAT_WALK_USES_SKEYS : 0);
	rc = dat_entry_walk(mc, p_gfn, sg->parent->asce, flags,
			    TABLE_TYPE_PAGE_TABLE, &crstep, &ptep);
	if (rc)
		return rc;
	if (level <= TABLE_TYPE_REGION1) {
		scoped_guard(spinlock, &sg->host_to_rmap_lock)
			rc = gmap_insert_rmap(sg, p_gfn, r_gfn, level);
	}
	if (rc)
		return rc;

	if (!pgste_get_trylock(ptep, &pgste))
		return -EAGAIN;
	pte = ptep->s.pr ? *ptep : _pte(pfn, wr, false, false);
	pte.h.p = 1;
	pgste = _gmap_ptep_xchg(sg->parent, ptep, pte, pgste, p_gfn, false);
	pgste.vsie_notif = 1;
	pgste_set_unlock(ptep, pgste);

	return 0;
}

static long __set_cmma_dirty_pte(union pte *ptep, gfn_t gfn, gfn_t next, struct dat_walk *walk)
{
	__atomic64_or(PGSTE_CMMA_D_BIT, &pgste_of(ptep)->val);
	if (need_resched())
		return next;
	return 0;
}

void gmap_set_cmma_all_dirty(struct gmap *gmap)
{
	const struct dat_walk_ops ops = { .pte_entry = __set_cmma_dirty_pte, };
	gfn_t gfn = 0;

	do {
		scoped_guard(read_lock, &gmap->kvm->mmu_lock)
			gfn = _dat_walk_gfn_range(gfn, asce_end(gmap->asce), gmap->asce, &ops,
						  DAT_WALK_IGN_HOLES, NULL);
		cond_resched();
	} while (gfn);
}

static void gmap_unshadow_level(struct gmap *sg, gfn_t r_gfn, int level)
{
	unsigned long align = PAGE_SIZE;
	gpa_t gaddr = gfn_to_gpa(r_gfn);
	union crste *crstep;
	union crste crste;
	union pte *ptep;

	if (level > TABLE_TYPE_PAGE_TABLE)
		align = 1UL << (11 * level + _SEGMENT_SHIFT);
	kvm_s390_vsie_gmap_notifier(sg, ALIGN_DOWN(gaddr, align), ALIGN(gaddr + 1, align));
	if (dat_entry_walk(NULL, r_gfn, sg->asce, 0, level, &crstep, &ptep))
		return;
	if (ptep) {
		if (READ_ONCE(*ptep).val != _PTE_EMPTY.val)
			dat_ptep_xchg(ptep, _PTE_EMPTY, r_gfn, sg->asce, uses_skeys(sg));
		return;
	}
	crste = READ_ONCE(*crstep);
	dat_crstep_clear(crstep, r_gfn, sg->asce);
	if (crste_leaf(crste) || crste.h.i)
		return;
	if (is_pmd(crste))
		dat_free_pt(dereference_pmd(crste.pmd));
	else
		dat_free_level(dereference_crste(crste), true);
}

static void gmap_unshadow(struct gmap *sg)
{
	struct gmap_cache *gmap_cache, *next;

	KVM_BUG_ON(!is_shadow(sg), sg->kvm);
	KVM_BUG_ON(!sg->parent, sg->kvm);

	lockdep_assert_held(&sg->parent->children_lock);

	gmap_remove_child(sg);
	kvm_s390_vsie_gmap_notifier(sg, 0, -1UL);

	list_for_each_entry_safe(gmap_cache, next, &sg->scb_users, list) {
		gmap_cache->gmap = NULL;
		list_del(&gmap_cache->list);
	}

	gmap_put(sg);
}

void _gmap_handle_vsie_unshadow_event(struct gmap *parent, gfn_t gfn)
{
	struct vsie_rmap *rmap, *rnext, *head;
	struct gmap *sg, *next;
	gfn_t start, end;

	list_for_each_entry_safe(sg, next, &parent->children, list) {
		start = sg->guest_asce.rsto;
		end = start + sg->guest_asce.tl + 1;
		if (!sg->guest_asce.r && gfn >= start && gfn < end) {
			gmap_unshadow(sg);
			continue;
		}
		scoped_guard(spinlock, &sg->host_to_rmap_lock)
			head = radix_tree_delete(&sg->host_to_rmap, gfn);
		gmap_for_each_rmap_safe(rmap, rnext, head)
			gmap_unshadow_level(sg, rmap->r_gfn, rmap->level);
	}
}

/**
 * gmap_find_shadow() - Find a specific ASCE in the list of shadow tables.
 * @parent: Pointer to the parent gmap.
 * @asce: ASCE for which the shadow table is created.
 * @edat_level: Edat level to be used for the shadow translation.
 *
 * Context: Called with parent->children_lock held.
 *
 * Return: The pointer to a gmap if a shadow table with the given asce is
 * already available, ERR_PTR(-EAGAIN) if another one is just being created,
 * otherwise NULL.
 */
static struct gmap *gmap_find_shadow(struct gmap *parent, union asce asce, int edat_level)
{
	struct gmap *sg;

	lockdep_assert_held(&parent->children_lock);
	list_for_each_entry(sg, &parent->children, list) {
		if (!gmap_is_shadow_valid(sg, asce, edat_level))
			continue;
		return sg;
	}
	return NULL;
}

#define CRST_TABLE_PAGES (_CRST_TABLE_SIZE / PAGE_SIZE)
struct gmap_protect_asce_top_level {
	unsigned long seq;
	struct guest_fault f[CRST_TABLE_PAGES];
};

static inline int __gmap_protect_asce_top_level(struct kvm_s390_mmu_cache *mc, struct gmap *sg,
						struct gmap_protect_asce_top_level *context)
{
	int rc, i;

	guard(write_lock)(&sg->kvm->mmu_lock);

	if (kvm_s390_array_needs_retry_safe(sg->kvm, context->seq, context->f))
		return -EAGAIN;

	scoped_guard(spinlock, &sg->parent->children_lock) {
		for (i = 0; i < CRST_TABLE_PAGES; i++) {
			if (!context->f[i].valid)
				continue;
			rc = gmap_protect_rmap(mc, sg, context->f[i].gfn, 0, context->f[i].pfn,
					       TABLE_TYPE_REGION1 + 1, context->f[i].writable);
			if (rc)
				return rc;
		}
		gmap_add_child(sg->parent, sg);
	}

	kvm_s390_release_faultin_array(sg->kvm, context->f, false);
	return 0;
}

static inline int _gmap_protect_asce_top_level(struct kvm_s390_mmu_cache *mc, struct gmap *sg,
					       struct gmap_protect_asce_top_level *context)
{
	int rc;

	if (kvm_s390_array_needs_retry_unsafe(sg->kvm, context->seq, context->f))
		return -EAGAIN;
	do {
		rc = kvm_s390_mmu_cache_topup(mc);
		if (rc)
			return rc;
		rc = radix_tree_preload(GFP_KERNEL);
		if (rc)
			return rc;
		rc = __gmap_protect_asce_top_level(mc, sg, context);
		radix_tree_preload_end();
	} while (rc == -ENOMEM);

	return rc;
}

static int gmap_protect_asce_top_level(struct kvm_s390_mmu_cache *mc, struct gmap *sg)
{
	struct gmap_protect_asce_top_level context = {};
	union asce asce = sg->guest_asce;
	int rc;

	KVM_BUG_ON(!is_shadow(sg), sg->kvm);

	context.seq = sg->kvm->mmu_invalidate_seq;
	/* Pairs with the smp_wmb() in kvm_mmu_invalidate_end(). */
	smp_rmb();

	rc = kvm_s390_get_guest_pages(sg->kvm, context.f, asce.rsto, asce.dt + 1, false);
	if (rc > 0)
		rc = -EFAULT;
	if (!rc)
		rc = _gmap_protect_asce_top_level(mc, sg, &context);
	if (rc)
		kvm_s390_release_faultin_array(sg->kvm, context.f, true);
	return rc;
}

/**
 * gmap_create_shadow() - Create/find a shadow guest address space.
 * @mc: The cache to use to allocate dat tables.
 * @parent: Pointer to the parent gmap.
 * @asce: ASCE for which the shadow table is created.
 * @edat_level: Edat level to be used for the shadow translation.
 *
 * The pages of the top level page table referred by the asce parameter
 * will be set to read-only and marked in the PGSTEs of the kvm process.
 * The shadow table will be removed automatically on any change to the
 * PTE mapping for the source table.
 *
 * The returned shadow gmap will be returned with one extra reference.
 *
 * Return: A guest address space structure, ERR_PTR(-ENOMEM) if out of memory,
 * ERR_PTR(-EAGAIN) if the caller has to retry and ERR_PTR(-EFAULT) if the
 * parent gmap table could not be protected.
 */
struct gmap *gmap_create_shadow(struct kvm_s390_mmu_cache *mc, struct gmap *parent,
				union asce asce, int edat_level)
{
	struct gmap *sg, *new;
	int rc;

	scoped_guard(spinlock, &parent->children_lock) {
		sg = gmap_find_shadow(parent, asce, edat_level);
		if (sg) {
			gmap_get(sg);
			return sg;
		}
	}
	/* Create a new shadow gmap. */
	new = gmap_new(parent->kvm, asce.r ? 1UL << (64 - PAGE_SHIFT) : asce_end(asce));
	if (!new)
		return ERR_PTR(-ENOMEM);
	new->guest_asce = asce;
	new->edat_level = edat_level;
	set_bit(GMAP_FLAG_SHADOW, &new->flags);

	scoped_guard(spinlock, &parent->children_lock) {
		/* Recheck if another CPU created the same shadow. */
		sg = gmap_find_shadow(parent, asce, edat_level);
		if (sg) {
			gmap_put(new);
			gmap_get(sg);
			return sg;
		}
		if (asce.r) {
			/* Only allow one real-space gmap shadow. */
			list_for_each_entry(sg, &parent->children, list) {
				if (sg->guest_asce.r) {
					scoped_guard(write_lock, &parent->kvm->mmu_lock)
						gmap_unshadow(sg);
					break;
				}
			}
			gmap_add_child(parent, new);
			/* Nothing to protect, return right away. */
			gmap_get(new);
			return new;
		}
	}

	gmap_get(new);
	new->parent = parent;
	/* Protect while inserting, protects against invalidation races. */
	rc = gmap_protect_asce_top_level(mc, new);
	if (rc) {
		new->parent = NULL;
		gmap_put(new);
		gmap_put(new);
		return ERR_PTR(rc);
	}
	return new;
}
