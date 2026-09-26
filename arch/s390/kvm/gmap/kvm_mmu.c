// SPDX-License-Identifier: GPL-2.0

#include <linux/kvm_types.h>
#include <linux/kvm_host.h>

#include "s390.h"
#include "gmap.h"
#include "dat.h"
#include "kvm_mmu.h"

/*
 * Get (and clear) the dirty memory log for a memory slot.
 */
int s390_kvm_mmu_get_dirty_log(struct kvm *kvm, struct kvm_dirty_log *log)
{
	int r;
	unsigned long n;
	struct kvm_memory_slot *memslot;
	int is_dirty;

	if (kvm_is_ucontrol(kvm))
		return -EINVAL;

	mutex_lock(&kvm->slots_lock);

	r = -EINVAL;
	if (log->slot >= KVM_USER_MEM_SLOTS)
		goto out;

	r = kvm_get_dirty_log(kvm, log, &is_dirty, &memslot);
	if (r)
		goto out;

	/* Clear the dirty log */
	if (is_dirty) {
		n = kvm_dirty_bitmap_bytes(memslot);
		memset(memslot->dirty_bitmap, 0, n);
	}
	r = 0;
out:
	mutex_unlock(&kvm->slots_lock);
	return r;
}

int s390_kvm_mmu_prepare_memory_region(struct kvm *kvm,
				       const struct kvm_memory_slot *old,
				       struct kvm_memory_slot *new,
				       enum kvm_mr_change change)
{
	struct kvm_s390_mmu_cache *mc __free(kvm_s390_mmu_cache) = NULL;
	int rc = 0;

	if (kvm_is_ucontrol(kvm) && new && new->id < KVM_USER_MEM_SLOTS)
		return -EINVAL;

	/* When we are protected, we should not change the memory slots */
	if (kvm_s390_pv_get_handle(kvm))
		return -EINVAL;

	if (change != KVM_MR_DELETE && change != KVM_MR_FLAGS_ONLY) {
		/*
		 * A few sanity checks. The memory in userland is ok to be
		 * fragmented into various different vmas. It is okay to mmap()
		 * and munmap() stuff in this slot after doing this call at any
		 * time.
		 */
		if (change != KVM_MR_MOVE && change != KVM_MR_CREATE) {
			WARN(1, "Unknown KVM MR CHANGE: %d\n", change);
			return -EINVAL;
		}
		if (new->userspace_addr & ~PAGE_MASK)
			return -EINVAL;
		if ((new->base_gfn + new->npages) * PAGE_SIZE > kvm->arch.mem_limit)
			return -EINVAL;
		if (!asce_contains_gfn(kvm->arch.gmap->asce, new->base_gfn + new->npages - 1))
			return -EINVAL;
	}

	if (kvm->arch.migration_mode) {
		/*
		 * Turn off migration mode when:
		 * - userspace creates a new memslot with dirty logging off,
		 * - userspace modifies an existing memslot (MOVE or FLAGS_ONLY)
		 *   and dirty logging is turned off.
		 * Migration mode expects dirty page logging being enabled to
		 * store its dirty bitmap.
		 */
		if (change != KVM_MR_DELETE &&
		    !(new->flags & KVM_MEM_LOG_DIRTY_PAGES))
			WARN(kvm_s390_vm_stop_migration(kvm),
			     "Failed to stop migration mode");
	}

	if (change == KVM_MR_FLAGS_ONLY)
		return 0;
	if (change != KVM_MR_DELETE) {
		/* Enough capacity to add a new memslot */
		mc = kvm_s390_new_mmu_cache();
		if (!mc)
			return -ENOMEM;
	}
	scoped_guard(write_lock, &kvm->mmu_lock) {
		kvm_s390_update_cmma_dirty(kvm, old);
		if (change == KVM_MR_DELETE || change == KVM_MR_MOVE)
			rc = dat_delete_slot(kvm->arch.gmap->asce, old->base_gfn, old->npages);
		if (!rc && (change == KVM_MR_MOVE || change == KVM_MR_CREATE))
			rc = dat_create_slot(mc, kvm->arch.gmap->asce, new->base_gfn, new->npages);
	}
	/*
	 * Can only be triggered if dat_{create,delete}_slot() found an
	 * internal inconsistency or if the mmu cache ran out of memory;
	 * both should be impossible.
	 */
	KVM_BUG_ON(rc, kvm);
	return rc;
}
