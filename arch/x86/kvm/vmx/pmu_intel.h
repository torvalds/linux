/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_VMX_PMU_INTEL_H
#define  __KVM_X86_VMX_PMU_INTEL_H

#include <linux/kvm_host.h>

bool intel_pmu_lbr_is_enabled(struct kvm_vcpu *vcpu);
int intel_pmu_create_guest_lbr_event(struct kvm_vcpu *vcpu);

struct lbr_desc {
	/* Basic info about guest LBR records. */
	struct x86_pmu_lbr records;

	/*
	 * Emulate LBR feature via passthrough LBR registers when the
	 * per-vcpu guest LBR event is scheduled on the current pcpu.
	 *
	 * The records may be inaccurate if the host reclaims the LBR.
	 */
	struct perf_event *event;

	/* True if LBRs are marked as not intercepted in the MSR bitmap */
	bool msr_passthrough;
};

extern struct x86_pmu_lbr vmx_lbr_caps;

#endif /* __KVM_X86_VMX_PMU_INTEL_H */
