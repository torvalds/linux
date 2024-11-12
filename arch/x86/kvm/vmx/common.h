/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __KVM_X86_VMX_COMMON_H
#define __KVM_X86_VMX_COMMON_H

#include <linux/kvm_host.h>

#include "mmu.h"

static inline bool vt_is_tdx_private_gpa(struct kvm *kvm, gpa_t gpa)
{
	/* For TDX the direct mask is the shared mask. */
	return !kvm_is_addr_direct(kvm, gpa);
}

static inline int __vmx_handle_ept_violation(struct kvm_vcpu *vcpu, gpa_t gpa,
					     unsigned long exit_qualification)
{
	u64 error_code;

	/* Is it a read fault? */
	error_code = (exit_qualification & EPT_VIOLATION_ACC_READ)
		     ? PFERR_USER_MASK : 0;
	/* Is it a write fault? */
	error_code |= (exit_qualification & EPT_VIOLATION_ACC_WRITE)
		      ? PFERR_WRITE_MASK : 0;
	/* Is it a fetch fault? */
	error_code |= (exit_qualification & EPT_VIOLATION_ACC_INSTR)
		      ? PFERR_FETCH_MASK : 0;
	/* ept page table entry is present? */
	error_code |= (exit_qualification & EPT_VIOLATION_RWX_MASK)
		      ? PFERR_PRESENT_MASK : 0;

	if (error_code & EPT_VIOLATION_GVA_IS_VALID)
		error_code |= (exit_qualification & EPT_VIOLATION_GVA_TRANSLATED) ?
			      PFERR_GUEST_FINAL_MASK : PFERR_GUEST_PAGE_MASK;

	if (vt_is_tdx_private_gpa(vcpu->kvm, gpa))
		error_code |= PFERR_PRIVATE_ACCESS;

	return kvm_mmu_page_fault(vcpu, gpa, error_code, NULL, 0);
}

#endif /* __KVM_X86_VMX_COMMON_H */
