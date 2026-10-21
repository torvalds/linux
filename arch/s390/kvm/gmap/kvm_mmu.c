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
		if (new->userspace_addr & ~PAGE_MASK)
			return -EINVAL;
		if ((new->base_gfn + new->npages) * PAGE_SIZE > kvm->arch.mem_limit)
			return -EINVAL;
		if (!asce_contains_gfn(kvm->arch.gmap->asce, new->base_gfn + new->npages - 1))
			return -EINVAL;
	}

	if (!kvm_s390_is_migration_mode(kvm))
		return 0;

	/*
	 * Turn off migration mode when:
	 * - userspace creates a new memslot with dirty logging off,
	 * - userspace modifies an existing memslot (MOVE or FLAGS_ONLY) and
	 *   dirty logging is turned off.
	 * Migration mode expects dirty page logging being enabled to store
	 * its dirty bitmap.
	 */
	if (change != KVM_MR_DELETE &&
	    !(new->flags & KVM_MEM_LOG_DIRTY_PAGES))
		WARN(kvm_s390_vm_stop_migration(kvm),
		     "Failed to stop migration mode");

	return 0;
}

void s390_kvm_mmu_commit_memory_region(struct kvm *kvm,
				       struct kvm_memory_slot *old,
				       const struct kvm_memory_slot *new,
				       enum kvm_mr_change change)
{
	struct kvm_s390_mmu_cache *mc __free(kvm_s390_mmu_cache) = NULL;
	int rc = 0;

	guard(mutex)(&kvm->slots_arch_lock);

	if (change == KVM_MR_FLAGS_ONLY)
		return;

	mc = kvm_s390_new_mmu_cache();
	if (!mc) {
		rc = -ENOMEM;
		goto out;
	}

	scoped_guard(write_lock, &kvm->mmu_lock) {
		kvm_s390_update_cmma_dirty(kvm, old);
		switch (change) {
		case KVM_MR_DELETE:
			rc = dat_delete_slot(mc, kvm->arch.gmap->asce, old->base_gfn, old->npages);
			break;
		case KVM_MR_MOVE:
			rc = dat_delete_slot(mc, kvm->arch.gmap->asce, old->base_gfn, old->npages);
			if (rc)
				break;
			fallthrough;
		case KVM_MR_CREATE:
			rc = dat_create_slot(mc, kvm->arch.gmap->asce, new->base_gfn, new->npages);
			break;
		case KVM_MR_FLAGS_ONLY:
			break;
		default:
			WARN(1, "Unknown KVM MR CHANGE: %d\n", change);
		}
	}
out:
	if (rc)
		pr_warn("failed to commit memory region\n");
}
