// SPDX-License-Identifier: GPL-2.0
/*
 * Guest memory management for KVM/s390 nested VMs.
 *
 * Copyright IBM Corp. 2008, 2020, 2024
 *
 *    Author(s): Claudio Imbrenda <imbrenda@linux.ibm.com>
 *               Martin Schwidefsky <schwidefsky@de.ibm.com>
 *               David Hildenbrand <david@redhat.com>
 *               Janosch Frank <frankja@linux.vnet.ibm.com>
 */

#include <linux/compiler.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/pgtable.h>
#include <linux/pagemap.h>
#include <linux/mman.h>

#include <asm/lowcore.h>
#include <asm/gmap.h>
#include <asm/uv.h>

#include "kvm-s390.h"

/**
 * gmap_find_shadow - find a specific asce in the list of shadow tables
 * @parent: pointer to the parent gmap
 * @asce: ASCE for which the shadow table is created
 * @edat_level: edat level to be used for the shadow translation
 *
 * Returns the pointer to a gmap if a shadow table with the given asce is
 * already available, ERR_PTR(-EAGAIN) if another one is just being created,
 * otherwise NULL
 *
 * Context: Called with parent->shadow_lock held
 */
static struct gmap *gmap_find_shadow(struct gmap *parent, unsigned long asce, int edat_level)
{
	struct gmap *sg;

	lockdep_assert_held(&parent->shadow_lock);
	list_for_each_entry(sg, &parent->children, list) {
		if (!gmap_shadow_valid(sg, asce, edat_level))
			continue;
		if (!sg->initialized)
			return ERR_PTR(-EAGAIN);
		refcount_inc(&sg->ref_count);
		return sg;
	}
	return NULL;
}

/**
 * gmap_shadow - create/find a shadow guest address space
 * @parent: pointer to the parent gmap
 * @asce: ASCE for which the shadow table is created
 * @edat_level: edat level to be used for the shadow translation
 *
 * The pages of the top level page table referred by the asce parameter
 * will be set to read-only and marked in the PGSTEs of the kvm process.
 * The shadow table will be removed automatically on any change to the
 * PTE mapping for the source table.
 *
 * Returns a guest address space structure, ERR_PTR(-ENOMEM) if out of memory,
 * ERR_PTR(-EAGAIN) if the caller has to retry and ERR_PTR(-EFAULT) if the
 * parent gmap table could not be protected.
 */
struct gmap *gmap_shadow(struct gmap *parent, unsigned long asce, int edat_level)
{
	struct gmap *sg, *new;
	unsigned long limit;
	int rc;

	if (KVM_BUG_ON(parent->mm->context.allow_gmap_hpage_1m, (struct kvm *)parent->private) ||
	    KVM_BUG_ON(gmap_is_shadow(parent), (struct kvm *)parent->private))
		return ERR_PTR(-EFAULT);
	spin_lock(&parent->shadow_lock);
	sg = gmap_find_shadow(parent, asce, edat_level);
	spin_unlock(&parent->shadow_lock);
	if (sg)
		return sg;
	/* Create a new shadow gmap */
	limit = -1UL >> (33 - (((asce & _ASCE_TYPE_MASK) >> 2) * 11));
	if (asce & _ASCE_REAL_SPACE)
		limit = -1UL;
	new = gmap_alloc(limit);
	if (!new)
		return ERR_PTR(-ENOMEM);
	new->mm = parent->mm;
	new->parent = gmap_get(parent);
	new->private = parent->private;
	new->orig_asce = asce;
	new->edat_level = edat_level;
	new->initialized = false;
	spin_lock(&parent->shadow_lock);
	/* Recheck if another CPU created the same shadow */
	sg = gmap_find_shadow(parent, asce, edat_level);
	if (sg) {
		spin_unlock(&parent->shadow_lock);
		gmap_free(new);
		return sg;
	}
	if (asce & _ASCE_REAL_SPACE) {
		/* only allow one real-space gmap shadow */
		list_for_each_entry(sg, &parent->children, list) {
			if (sg->orig_asce & _ASCE_REAL_SPACE) {
				spin_lock(&sg->guest_table_lock);
				gmap_unshadow(sg);
				spin_unlock(&sg->guest_table_lock);
				list_del(&sg->list);
				gmap_put(sg);
				break;
			}
		}
	}
	refcount_set(&new->ref_count, 2);
	list_add(&new->list, &parent->children);
	if (asce & _ASCE_REAL_SPACE) {
		/* nothing to protect, return right away */
		new->initialized = true;
		spin_unlock(&parent->shadow_lock);
		return new;
	}
	spin_unlock(&parent->shadow_lock);
	/* protect after insertion, so it will get properly invalidated */
	mmap_read_lock(parent->mm);
	rc = __kvm_s390_mprotect_many(parent, asce & _ASCE_ORIGIN,
				      ((asce & _ASCE_TABLE_LENGTH) + 1),
				      PROT_READ, GMAP_NOTIFY_SHADOW);
	mmap_read_unlock(parent->mm);
	spin_lock(&parent->shadow_lock);
	new->initialized = true;
	if (rc) {
		list_del(&new->list);
		gmap_free(new);
		new = ERR_PTR(rc);
	}
	spin_unlock(&parent->shadow_lock);
	return new;
}
