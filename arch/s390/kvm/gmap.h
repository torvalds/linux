/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  KVM guest address space mapping code
 *
 *    Copyright IBM Corp. 2007, 2016, 2025
 *    Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 *               Claudio Imbrenda <imbrenda@linux.ibm.com>
 */

#ifndef ARCH_KVM_S390_GMAP_H
#define ARCH_KVM_S390_GMAP_H

#include "dat.h"

/**
 * enum gmap_flags - Flags of a gmap.
 *
 * @GMAP_FLAG_SHADOW: The gmap is a vsie shadow gmap.
 * @GMAP_FLAG_OWNS_PAGETABLES: The gmap owns all dat levels; normally 1, is 0
 *                             only for ucontrol per-cpu gmaps, since they
 *                             share the page tables with the main gmap.
 * @GMAP_FLAG_IS_UCONTROL: The gmap is ucontrol (main gmap or per-cpu gmap).
 * @GMAP_FLAG_ALLOW_HPAGE_1M: 1M hugepages are allowed for this gmap,
 *                            independently of the page size used by userspace.
 * @GMAP_FLAG_ALLOW_HPAGE_2G: 2G hugepages are allowed for this gmap,
 *                            independently of the page size used by userspace.
 * @GMAP_FLAG_PFAULT_ENABLED: Pfault is enabled for the gmap.
 * @GMAP_FLAG_USES_SKEYS: If the guest uses storage keys.
 * @GMAP_FLAG_USES_CMM: Whether the guest uses CMMA.
 * @GMAP_FLAG_EXPORT_ON_UNMAP: Whether to export guest pages when unmapping.
 */
enum gmap_flags {
	GMAP_FLAG_SHADOW = 0,
	GMAP_FLAG_OWNS_PAGETABLES,
	GMAP_FLAG_IS_UCONTROL,
	GMAP_FLAG_ALLOW_HPAGE_1M,
	GMAP_FLAG_ALLOW_HPAGE_2G,
	GMAP_FLAG_PFAULT_ENABLED,
	GMAP_FLAG_USES_SKEYS,
	GMAP_FLAG_USES_CMM,
	GMAP_FLAG_EXPORT_ON_UNMAP,
};

/**
 * struct gmap_struct - Guest address space.
 *
 * @flags: GMAP_FLAG_* flags.
 * @edat_level: The edat level of this shadow gmap.
 * @kvm: The vm.
 * @asce: The ASCE used by this gmap.
 * @list: List head used in children gmaps for the children gmap list.
 * @children_lock: Protects children and scb_users.
 * @children: List of child gmaps of this gmap.
 * @scb_users: List of vsie_scb that use this shadow gmap.
 * @parent: Parent gmap of a child gmap.
 * @guest_asce: Original ASCE of this shadow gmap.
 * @host_to_rmap_lock: Protects host_to_rmap.
 * @host_to_rmap: Radix tree mapping host addresses to guest addresses.
 */
struct gmap {
	unsigned long flags;
	unsigned char edat_level;
	struct kvm *kvm;
	union asce asce;
	struct list_head list;
	spinlock_t children_lock;	/* Protects: children, scb_users */
	struct list_head children;
	struct list_head scb_users;
	struct gmap *parent;
	union asce guest_asce;
	spinlock_t host_to_rmap_lock;	/* Protects host_to_rmap */
	struct radix_tree_root host_to_rmap;
	refcount_t refcount;
};

struct gmap_cache {
	struct list_head list;
	struct gmap *gmap;
};

#define gmap_for_each_rmap_safe(pos, n, head) \
	for (pos = (head); n = pos ? pos->next : NULL, pos; pos = n)

int s390_replace_asce(struct gmap *gmap);
bool _gmap_unmap_prefix(struct gmap *gmap, gfn_t gfn, gfn_t end, bool hint);
bool gmap_age_gfn(struct gmap *gmap, gfn_t start, gfn_t end);
bool gmap_unmap_gfn_range(struct gmap *gmap, struct kvm_memory_slot *slot, gfn_t start, gfn_t end);
int gmap_try_fixup_minor(struct gmap *gmap, struct guest_fault *fault);
struct gmap *gmap_new(struct kvm *kvm, gfn_t limit);
struct gmap *gmap_new_child(struct gmap *parent, gfn_t limit);
void gmap_remove_child(struct gmap *child);
void gmap_dispose(struct gmap *gmap);
int gmap_link(struct kvm_s390_mmu_cache *mc, struct gmap *gmap, struct guest_fault *fault);
void gmap_sync_dirty_log(struct gmap *gmap, gfn_t start, gfn_t end);
int gmap_set_limit(struct gmap *gmap, gfn_t limit);
int gmap_ucas_translate(struct kvm_s390_mmu_cache *mc, struct gmap *gmap, gpa_t *gaddr);
int gmap_ucas_map(struct gmap *gmap, gfn_t p_gfn, gfn_t c_gfn, unsigned long count);
void gmap_ucas_unmap(struct gmap *gmap, gfn_t c_gfn, unsigned long count);
int gmap_enable_skeys(struct gmap *gmap);
int gmap_pv_destroy_range(struct gmap *gmap, gfn_t start, gfn_t end, bool interruptible);
int gmap_insert_rmap(struct gmap *sg, gfn_t p_gfn, gfn_t r_gfn, int level);
int gmap_protect_rmap(struct kvm_s390_mmu_cache *mc, struct gmap *sg, gfn_t p_gfn, gfn_t r_gfn,
		      kvm_pfn_t pfn, int level, bool wr);
void gmap_set_cmma_all_dirty(struct gmap *gmap);
void _gmap_handle_vsie_unshadow_event(struct gmap *parent, gfn_t gfn);
struct gmap *gmap_create_shadow(struct kvm_s390_mmu_cache *mc, struct gmap *gmap,
				union asce asce, int edat_level);
void gmap_split_huge_pages(struct gmap *gmap);

static inline bool uses_skeys(struct gmap *gmap)
{
	return test_bit(GMAP_FLAG_USES_SKEYS, &gmap->flags);
}

static inline bool uses_cmm(struct gmap *gmap)
{
	return test_bit(GMAP_FLAG_USES_CMM, &gmap->flags);
}

static inline bool pfault_enabled(struct gmap *gmap)
{
	return test_bit(GMAP_FLAG_PFAULT_ENABLED, &gmap->flags);
}

static inline bool is_ucontrol(struct gmap *gmap)
{
	return test_bit(GMAP_FLAG_IS_UCONTROL, &gmap->flags);
}

static inline bool is_shadow(struct gmap *gmap)
{
	return test_bit(GMAP_FLAG_SHADOW, &gmap->flags);
}

static inline bool owns_page_tables(struct gmap *gmap)
{
	return test_bit(GMAP_FLAG_OWNS_PAGETABLES, &gmap->flags);
}

static inline struct gmap *gmap_put(struct gmap *gmap)
{
	if (refcount_dec_and_test(&gmap->refcount))
		gmap_dispose(gmap);
	return NULL;
}

static inline void gmap_get(struct gmap *gmap)
{
	WARN_ON_ONCE(unlikely(!refcount_inc_not_zero(&gmap->refcount)));
}

static inline void gmap_handle_vsie_unshadow_event(struct gmap *parent, gfn_t gfn)
{
	scoped_guard(spinlock, &parent->children_lock)
		_gmap_handle_vsie_unshadow_event(parent, gfn);
}

static inline bool gmap_mkold_prefix(struct gmap *gmap, gfn_t gfn, gfn_t end)
{
	return _gmap_unmap_prefix(gmap, gfn, end, true);
}

static inline bool gmap_unmap_prefix(struct gmap *gmap, gfn_t gfn, gfn_t end)
{
	return _gmap_unmap_prefix(gmap, gfn, end, false);
}

static inline union pgste _gmap_ptep_xchg(struct gmap *gmap, union pte *ptep, union pte newpte,
					  union pgste pgste, gfn_t gfn, bool needs_lock)
{
	lockdep_assert_held(&gmap->kvm->mmu_lock);
	if (!needs_lock)
		lockdep_assert_held(&gmap->children_lock);
	else
		lockdep_assert_not_held(&gmap->children_lock);

	if (pgste.prefix_notif && (newpte.h.p || newpte.h.i)) {
		pgste.prefix_notif = 0;
		gmap_unmap_prefix(gmap, gfn, gfn + 1);
	}
	if (pgste.vsie_notif && (ptep->h.p != newpte.h.p || newpte.h.i)) {
		pgste.vsie_notif = 0;
		if (needs_lock)
			gmap_handle_vsie_unshadow_event(gmap, gfn);
		else
			_gmap_handle_vsie_unshadow_event(gmap, gfn);
	}
	return __dat_ptep_xchg(ptep, pgste, newpte, gfn, gmap->asce, uses_skeys(gmap));
}

static inline union pgste gmap_ptep_xchg(struct gmap *gmap, union pte *ptep, union pte newpte,
					 union pgste pgste, gfn_t gfn)
{
	return _gmap_ptep_xchg(gmap, ptep, newpte, pgste, gfn, true);
}

static inline void _gmap_crstep_xchg(struct gmap *gmap, union crste *crstep, union crste ne,
				     gfn_t gfn, bool needs_lock)
{
	unsigned long align = 8 + (is_pmd(*crstep) ? 0 : 11);

	lockdep_assert_held(&gmap->kvm->mmu_lock);
	if (!needs_lock)
		lockdep_assert_held(&gmap->children_lock);

	gfn = ALIGN_DOWN(gfn, align);
	if (crste_prefix(*crstep) && (ne.h.p || ne.h.i || !crste_prefix(ne))) {
		ne.s.fc1.prefix_notif = 0;
		gmap_unmap_prefix(gmap, gfn, gfn + align);
	}
	if (crste_leaf(*crstep) && crstep->s.fc1.vsie_notif &&
	    (ne.h.p || ne.h.i || !ne.s.fc1.vsie_notif)) {
		ne.s.fc1.vsie_notif = 0;
		if (needs_lock)
			gmap_handle_vsie_unshadow_event(gmap, gfn);
		else
			_gmap_handle_vsie_unshadow_event(gmap, gfn);
	}
	dat_crstep_xchg(crstep, ne, gfn, gmap->asce);
}

static inline void gmap_crstep_xchg(struct gmap *gmap, union crste *crstep, union crste ne,
				    gfn_t gfn)
{
	return _gmap_crstep_xchg(gmap, crstep, ne, gfn, true);
}

/**
 * gmap_is_shadow_valid() - check if a shadow guest address space matches the
 *                          given properties and is still valid.
 * @sg: Pointer to the shadow guest address space structure.
 * @asce: ASCE for which the shadow table is requested.
 * @edat_level: Edat level to be used for the shadow translation.
 *
 * Return: true if the gmap shadow is still valid and matches the given
 * properties and the caller can continue using it; false otherwise, the
 * caller has to request a new shadow gmap in this case.
 */
static inline bool gmap_is_shadow_valid(struct gmap *sg, union asce asce, int edat_level)
{
	return sg->guest_asce.val == asce.val && sg->edat_level == edat_level;
}

#endif /* ARCH_KVM_S390_GMAP_H */
