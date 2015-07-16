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

/* duplicated from amd_perfmon_event_map, K7 and above should work. */
static struct kvm_event_hw_type_mapping amd_event_mapping[] = {
	[0] = { 0x76, 0x00, PERF_COUNT_HW_CPU_CYCLES },
	[1] = { 0xc0, 0x00, PERF_COUNT_HW_INSTRUCTIONS },
	[2] = { 0x80, 0x00, PERF_COUNT_HW_CACHE_REFERENCES },
	[3] = { 0x81, 0x00, PERF_COUNT_HW_CACHE_MISSES },
	[4] = { 0xc2, 0x00, PERF_COUNT_HW_BRANCH_INSTRUCTIONS },
	[5] = { 0xc3, 0x00, PERF_COUNT_HW_BRANCH_MISSES },
	[6] = { 0xd0, 0x00, PERF_COUNT_HW_STALLED_CYCLES_FRONTEND },
	[7] = { 0xd1, 0x00, PERF_COUNT_HW_STALLED_CYCLES_BACKEND },
};

static unsigned amd_find_arch_event(struct kvm_pmu *pmu,
				    u8 event_select,
				    u8 unit_mask)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(amd_event_mapping); i++)
		if (amd_event_mapping[i].eventsel == event_select
		    && amd_event_mapping[i].unit_mask == unit_mask)
			break;

	if (i == ARRAY_SIZE(amd_event_mapping))
		return PERF_COUNT_HW_MAX;

	return amd_event_mapping[i].event_type;
}

/* return PERF_COUNT_HW_MAX as AMD doesn't have fixed events */
static unsigned amd_find_fixed_event(int idx)
{
	return PERF_COUNT_HW_MAX;
}

/* check if a PMC is enabled by comparing it against global_ctrl bits. Because
 * AMD CPU doesn't have global_ctrl MSR, all PMCs are enabled (return TRUE).
 */
static bool amd_pmc_is_enabled(struct kvm_pmc *pmc)
{
	return true;
}

static struct kvm_pmc *amd_pmc_idx_to_pmc(struct kvm_pmu *pmu, int pmc_idx)
{
	return get_gp_pmc(pmu, MSR_K7_EVNTSEL0 + pmc_idx, MSR_K7_EVNTSEL0);
}

/* returns 0 if idx's corresponding MSR exists; otherwise returns 1. */
static int amd_is_valid_msr_idx(struct kvm_vcpu *vcpu, unsigned idx)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);

	idx &= ~(3u << 30);

	return (idx >= pmu->nr_arch_gp_counters);
}

/* idx is the ECX register of RDPMC instruction */
static struct kvm_pmc *amd_msr_idx_to_pmc(struct kvm_vcpu *vcpu, unsigned idx)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	struct kvm_pmc *counters;

	idx &= ~(3u << 30);
	if (idx >= pmu->nr_arch_gp_counters)
		return NULL;
	counters = pmu->gp_counters;

	return &counters[idx];
}

static bool amd_is_valid_msr(struct kvm_vcpu *vcpu, u32 msr)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	int ret = false;

	ret = get_gp_pmc(pmu, msr, MSR_K7_PERFCTR0) ||
		get_gp_pmc(pmu, msr, MSR_K7_EVNTSEL0);

	return ret;
}

static int amd_pmu_get_msr(struct kvm_vcpu *vcpu, u32 msr, u64 *data)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	struct kvm_pmc *pmc;

	/* MSR_K7_PERFCTRn */
	pmc = get_gp_pmc(pmu, msr, MSR_K7_PERFCTR0);
	if (pmc) {
		*data = pmc_read_counter(pmc);
		return 0;
	}
	/* MSR_K7_EVNTSELn */
	pmc = get_gp_pmc(pmu, msr, MSR_K7_EVNTSEL0);
	if (pmc) {
		*data = pmc->eventsel;
		return 0;
	}

	return 1;
}

static int amd_pmu_set_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	struct kvm_pmc *pmc;
	u32 msr = msr_info->index;
	u64 data = msr_info->data;

	/* MSR_K7_PERFCTRn */
	pmc = get_gp_pmc(pmu, msr, MSR_K7_PERFCTR0);
	if (pmc) {
		if (!msr_info->host_initiated)
			data = (s64)data;
		pmc->counter += data - pmc_read_counter(pmc);
		return 0;
	}
	/* MSR_K7_EVNTSELn */
	pmc = get_gp_pmc(pmu, msr, MSR_K7_EVNTSEL0);
	if (pmc) {
		if (data == pmc->eventsel)
			return 0;
		if (!(data & pmu->reserved_bits)) {
			reprogram_gp_counter(pmc, data);
			return 0;
		}
	}

	return 1;
}

static void amd_pmu_refresh(struct kvm_vcpu *vcpu)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);

	pmu->nr_arch_gp_counters = AMD64_NUM_COUNTERS;
	pmu->counter_bitmask[KVM_PMC_GP] = ((u64)1 << 48) - 1;
	pmu->reserved_bits = 0xffffffff00200000ull;
	/* not applicable to AMD; but clean them to prevent any fall out */
	pmu->counter_bitmask[KVM_PMC_FIXED] = 0;
	pmu->nr_arch_fixed_counters = 0;
	pmu->version = 0;
	pmu->global_status = 0;
}

static void amd_pmu_init(struct kvm_vcpu *vcpu)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	int i;

	for (i = 0; i < AMD64_NUM_COUNTERS ; i++) {
		pmu->gp_counters[i].type = KVM_PMC_GP;
		pmu->gp_counters[i].vcpu = vcpu;
		pmu->gp_counters[i].idx = i;
	}
}

static void amd_pmu_reset(struct kvm_vcpu *vcpu)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	int i;

	for (i = 0; i < AMD64_NUM_COUNTERS; i++) {
		struct kvm_pmc *pmc = &pmu->gp_counters[i];

		pmc_stop_counter(pmc);
		pmc->counter = pmc->eventsel = 0;
	}
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
