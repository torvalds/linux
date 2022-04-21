// SPDX-License-Identifier: GPL-2.0-only
/*
 * KVM L1 hypervisor optimizations on Hyper-V.
 */

#include <linux/kvm_host.h>
#include <asm/mshyperv.h>

#include "hyperv.h"
#include "kvm_onhyperv.h"

static int kvm_fill_hv_flush_list_func(struct hv_guest_mapping_flush_list *flush,
		void *data)
{
	struct kvm_tlb_range *range = data;

	return hyperv_fill_flush_guest_mapping_list(flush, range->start_gfn,
			range->pages);
}

static inline int hv_remote_flush_root_tdp(hpa_t root_tdp,
					   struct kvm_tlb_range *range)
{
	if (range)
		return hyperv_flush_guest_mapping_range(root_tdp,
				kvm_fill_hv_flush_list_func, (void *)range);
	else
		return hyperv_flush_guest_mapping(root_tdp);
}

int hv_remote_flush_tlb_with_range(struct kvm *kvm,
		struct kvm_tlb_range *range)
{
	struct kvm_arch *kvm_arch = &kvm->arch;
	struct kvm_vcpu *vcpu;
	int ret = 0, nr_unique_valid_roots;
	unsigned long i;
	hpa_t root;

	spin_lock(&kvm_arch->hv_root_tdp_lock);

	if (!VALID_PAGE(kvm_arch->hv_root_tdp)) {
		nr_unique_valid_roots = 0;

		/*
		 * Flush all valid roots, and see if all vCPUs have converged
		 * on a common root, in which case future flushes can skip the
		 * loop and flush the common root.
		 */
		kvm_for_each_vcpu(i, vcpu, kvm) {
			root = vcpu->arch.hv_root_tdp;
			if (!VALID_PAGE(root) || root == kvm_arch->hv_root_tdp)
				continue;

			/*
			 * Set the tracked root to the first valid root.  Keep
			 * this root for the entirety of the loop even if more
			 * roots are encountered as a low effort optimization
			 * to avoid flushing the same (first) root again.
			 */
			if (++nr_unique_valid_roots == 1)
				kvm_arch->hv_root_tdp = root;

			if (!ret)
				ret = hv_remote_flush_root_tdp(root, range);

			/*
			 * Stop processing roots if a failure occurred and
			 * multiple valid roots have already been detected.
			 */
			if (ret && nr_unique_valid_roots > 1)
				break;
		}

		/*
		 * The optimized flush of a single root can't be used if there
		 * are multiple valid roots (obviously).
		 */
		if (nr_unique_valid_roots > 1)
			kvm_arch->hv_root_tdp = INVALID_PAGE;
	} else {
		ret = hv_remote_flush_root_tdp(kvm_arch->hv_root_tdp, range);
	}

	spin_unlock(&kvm_arch->hv_root_tdp_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(hv_remote_flush_tlb_with_range);

int hv_remote_flush_tlb(struct kvm *kvm)
{
	return hv_remote_flush_tlb_with_range(kvm, NULL);
}
EXPORT_SYMBOL_GPL(hv_remote_flush_tlb);

void hv_track_root_tdp(struct kvm_vcpu *vcpu, hpa_t root_tdp)
{
	struct kvm_arch *kvm_arch = &vcpu->kvm->arch;

	if (kvm_x86_ops.tlb_remote_flush == hv_remote_flush_tlb) {
		spin_lock(&kvm_arch->hv_root_tdp_lock);
		vcpu->arch.hv_root_tdp = root_tdp;
		if (root_tdp != kvm_arch->hv_root_tdp)
			kvm_arch->hv_root_tdp = INVALID_PAGE;
		spin_unlock(&kvm_arch->hv_root_tdp_lock);
	}
}
EXPORT_SYMBOL_GPL(hv_track_root_tdp);
