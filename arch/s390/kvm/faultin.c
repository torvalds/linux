// SPDX-License-Identifier: GPL-2.0
/*
 *  KVM guest fault handling.
 *
 *    Copyright IBM Corp. 2025
 *    Author(s): Claudio Imbrenda <imbrenda@linux.ibm.com>
 */
#include <linux/kvm_types.h>
#include <linux/kvm_host.h>

#include "gmap.h"
#include "trace.h"
#include "faultin.h"

bool kvm_arch_setup_async_pf(struct kvm_vcpu *vcpu);

/*
 * kvm_s390_faultin_gfn() - handle a dat fault.
 * @vcpu: The vCPU whose gmap is to be fixed up, or NULL if operating on the VM.
 * @kvm: The VM whose gmap is to be fixed up, or NULL if operating on a vCPU.
 * @f: The guest fault that needs to be resolved.
 *
 * Return:
 * * 0 on success
 * * < 0 in case of error
 * * > 0 in case of guest exceptions
 *
 * Context:
 * * The mm lock must not be held before calling
 * * kvm->srcu must be held
 * * may sleep
 */
int kvm_s390_faultin_gfn(struct kvm_vcpu *vcpu, struct kvm *kvm, struct guest_fault *f)
{
	struct kvm_s390_mmu_cache *local_mc __free(kvm_s390_mmu_cache) = NULL;
	struct kvm_s390_mmu_cache *mc = NULL;
	struct kvm_memory_slot *slot;
	unsigned long inv_seq;
	int foll, rc = 0;

	foll = f->write_attempt ? FOLL_WRITE : 0;
	foll |= f->attempt_pfault ? FOLL_NOWAIT : 0;

	if (vcpu) {
		kvm = vcpu->kvm;
		mc = vcpu->arch.mc;
	}

	lockdep_assert_held(&kvm->srcu);

	scoped_guard(read_lock, &kvm->mmu_lock) {
		if (gmap_try_fixup_minor(kvm->arch.gmap, f) == 0)
			return 0;
	}

	while (1) {
		f->valid = false;
		inv_seq = kvm->mmu_invalidate_seq;
		/* Pairs with the smp_wmb() in kvm_mmu_invalidate_end(). */
		smp_rmb();

		if (vcpu)
			slot = kvm_vcpu_gfn_to_memslot(vcpu, f->gfn);
		else
			slot = gfn_to_memslot(kvm, f->gfn);
		f->pfn = __kvm_faultin_pfn(slot, f->gfn, foll, &f->writable, &f->page);

		/* Needs I/O, try to setup async pfault (only possible with FOLL_NOWAIT). */
		if (f->pfn == KVM_PFN_ERR_NEEDS_IO) {
			if (unlikely(!f->attempt_pfault))
				return -EAGAIN;
			if (unlikely(!vcpu))
				return -EINVAL;
			trace_kvm_s390_major_guest_pfault(vcpu);
			if (kvm_arch_setup_async_pf(vcpu))
				return 0;
			vcpu->stat.pfault_sync++;
			/* Could not setup async pfault, try again synchronously. */
			foll &= ~FOLL_NOWAIT;
			f->pfn = __kvm_faultin_pfn(slot, f->gfn, foll, &f->writable, &f->page);
		}

		/* Access outside memory, addressing exception. */
		if (is_noslot_pfn(f->pfn))
			return PGM_ADDRESSING;
		/* Signal pending: try again. */
		if (f->pfn == KVM_PFN_ERR_SIGPENDING)
			return -EAGAIN;
		/* Check if it's read-only memory; don't try to actually handle that case. */
		if (f->pfn == KVM_PFN_ERR_RO_FAULT)
			return -EOPNOTSUPP;
		/* Any other error. */
		if (is_error_pfn(f->pfn))
			return -EFAULT;

		if (!mc) {
			local_mc = kvm_s390_new_mmu_cache();
			if (!local_mc)
				return -ENOMEM;
			mc = local_mc;
		}

		/* Loop, will automatically release the faulted page. */
		if (mmu_invalidate_retry_gfn_unsafe(kvm, inv_seq, f->gfn)) {
			kvm_release_faultin_page(kvm, f->page, true, false);
			continue;
		}

		scoped_guard(read_lock, &kvm->mmu_lock) {
			if (!mmu_invalidate_retry_gfn(kvm, inv_seq, f->gfn)) {
				f->valid = true;
				rc = gmap_link(mc, kvm->arch.gmap, f);
				kvm_release_faultin_page(kvm, f->page, !!rc, f->write_attempt);
				f->page = NULL;
			}
		}
		kvm_release_faultin_page(kvm, f->page, true, false);

		if (rc == -ENOMEM) {
			rc = kvm_s390_mmu_cache_topup(mc);
			if (rc)
				return rc;
		} else if (rc != -EAGAIN) {
			return rc;
		}
	}
}

int kvm_s390_get_guest_page(struct kvm *kvm, struct guest_fault *f, gfn_t gfn, bool w)
{
	struct kvm_memory_slot *slot = gfn_to_memslot(kvm, gfn);
	int foll = w ? FOLL_WRITE : 0;

	f->write_attempt = w;
	f->gfn = gfn;
	f->pfn = __kvm_faultin_pfn(slot, gfn, foll, &f->writable, &f->page);
	if (is_noslot_pfn(f->pfn))
		return PGM_ADDRESSING;
	if (is_sigpending_pfn(f->pfn))
		return -EINTR;
	if (f->pfn == KVM_PFN_ERR_NEEDS_IO)
		return -EAGAIN;
	if (is_error_pfn(f->pfn))
		return -EFAULT;

	f->valid = true;
	return 0;
}
