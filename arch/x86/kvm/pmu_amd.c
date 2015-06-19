/*
 * KVM PMU support for AMD
 *
 * Copyright 2015, Red Hat, Inc. and/or its affiliates.
 *
 * Author:
 *   Wei Huang <wei@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Implementation is based on pmu_intel.c file
 */
#include <linux/types.h>
#include <linux/kvm_host.h>
#include <linux/perf_event.h>
#include "x86.h"
#include "cpuid.h"
#include "lapic.h"
#include "pmu.h"

static unsigned amd_find_arch_event(struct kvm_pmu *pmu,
				    u8 event_select,
				    u8 unit_mask)
{
	return PERF_COUNT_HW_MAX;
}

/* return PERF_COUNT_HW_MAX as AMD doesn't have fixed events */
static unsigned amd_find_fixed_event(int idx)
{
	return PERF_COUNT_HW_MAX;
}

static bool amd_pmc_is_enabled(struct kvm_pmc *pmc)
{
	return false;
}

static struct kvm_pmc *amd_pmc_idx_to_pmc(struct kvm_pmu *pmu, int pmc_idx)
{
	return NULL;
}

/* returns 0 if idx's corresponding MSR exists; otherwise returns 1. */
static int amd_is_valid_msr_idx(struct kvm_vcpu *vcpu, unsigned idx)
{
	return 1;
}

/* idx is the ECX register of RDPMC instruction */
static struct kvm_pmc *amd_msr_idx_to_pmc(struct kvm_vcpu *vcpu, unsigned idx)
{
	return NULL;
}

static bool amd_is_valid_msr(struct kvm_vcpu *vcpu, u32 msr)
{
	return false;
}

static int amd_pmu_get_msr(struct kvm_vcpu *vcpu, u32 msr, u64 *data)
{
	return 1;
}

static int amd_pmu_set_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	return 1;
}

static void amd_pmu_refresh(struct kvm_vcpu *vcpu)
{
}

static void amd_pmu_init(struct kvm_vcpu *vcpu)
{
}

static void amd_pmu_reset(struct kvm_vcpu *vcpu)
{
}

struct kvm_pmu_ops amd_pmu_ops = {
	.find_arch_event = amd_find_arch_event,
	.find_fixed_event = amd_find_fixed_event,
	.pmc_is_enabled = amd_pmc_is_enabled,
	.pmc_idx_to_pmc = amd_pmc_idx_to_pmc,
	.msr_idx_to_pmc = amd_msr_idx_to_pmc,
	.is_valid_msr_idx = amd_is_valid_msr_idx,
	.is_valid_msr = amd_is_valid_msr,
	.get_msr = amd_pmu_get_msr,
	.set_msr = amd_pmu_set_msr,
	.refresh = amd_pmu_refresh,
	.init = amd_pmu_init,
	.reset = amd_pmu_reset,
};
