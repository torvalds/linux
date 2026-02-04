/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  KVM guest fault handling.
 *
 *    Copyright IBM Corp. 2025
 *    Author(s): Claudio Imbrenda <imbrenda@linux.ibm.com>
 */

#ifndef __KVM_S390_FAULTIN_H
#define __KVM_S390_FAULTIN_H

#include <linux/kvm_host.h>

#include "dat.h"

int kvm_s390_faultin_gfn(struct kvm_vcpu *vcpu, struct kvm *kvm, struct guest_fault *f);
int kvm_s390_get_guest_page(struct kvm *kvm, struct guest_fault *f, gfn_t gfn, bool w);

static inline int kvm_s390_faultin_gfn_simple(struct kvm_vcpu *vcpu, struct kvm *kvm,
					      gfn_t gfn, bool wr)
{
	struct guest_fault f = { .gfn = gfn, .write_attempt = wr, };

	return kvm_s390_faultin_gfn(vcpu, kvm, &f);
}

static inline int kvm_s390_get_guest_page_and_read_gpa(struct kvm *kvm, struct guest_fault *f,
						       gpa_t gaddr, unsigned long *val)
{
	int rc;

	rc = kvm_s390_get_guest_page(kvm, f, gpa_to_gfn(gaddr), false);
	if (rc)
		return rc;

	*val = *(unsigned long *)phys_to_virt(pfn_to_phys(f->pfn) | offset_in_page(gaddr));

	return 0;
}

static inline void kvm_s390_release_multiple(struct kvm *kvm, struct guest_fault *guest_faults,
					     int n, bool ignore)
{
	int i;

	for (i = 0; i < n; i++) {
		kvm_release_faultin_page(kvm, guest_faults[i].page, ignore,
					 guest_faults[i].write_attempt);
		guest_faults[i].page = NULL;
	}
}

static inline bool kvm_s390_multiple_faults_need_retry(struct kvm *kvm, unsigned long seq,
						       struct guest_fault *guest_faults, int n,
						       bool unsafe)
{
	int i;

	for (i = 0; i < n; i++) {
		if (!guest_faults[i].valid)
			continue;
		if (unsafe && mmu_invalidate_retry_gfn_unsafe(kvm, seq, guest_faults[i].gfn))
			return true;
		if (!unsafe && mmu_invalidate_retry_gfn(kvm, seq, guest_faults[i].gfn))
			return true;
	}
	return false;
}

static inline int kvm_s390_get_guest_pages(struct kvm *kvm, struct guest_fault *guest_faults,
					   gfn_t start, int n_pages, bool write_attempt)
{
	int i, rc;

	for (i = 0; i < n_pages; i++) {
		rc = kvm_s390_get_guest_page(kvm, guest_faults + i, start + i, write_attempt);
		if (rc)
			break;
	}
	return rc;
}

#define kvm_s390_release_faultin_array(kvm, array, ignore) \
	kvm_s390_release_multiple(kvm, array, ARRAY_SIZE(array), ignore)

#define kvm_s390_array_needs_retry_unsafe(kvm, seq, array) \
	kvm_s390_multiple_faults_need_retry(kvm, seq, array, ARRAY_SIZE(array), true)

#define kvm_s390_array_needs_retry_safe(kvm, seq, array) \
	kvm_s390_multiple_faults_need_retry(kvm, seq, array, ARRAY_SIZE(array), false)

#endif /* __KVM_S390_FAULTIN_H */
