// SPDX-License-Identifier: GPL-2.0-only
/*
 * KVM PMU support for AMD
 *
 * Copyright 2015, Red Hat, Inc. and/or its affiliates.
 *
 * Author:
 *   Wei Huang <wei@redhat.com>
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
#include "svm.h"

enum pmu_type {
	PMU_TYPE_COUNTER = 0,
	PMU_TYPE_EVNTSEL,
};

enum index {
	INDEX_ZERO = 0,
	INDEX_ONE,
	INDEX_TWO,
	INDEX_THREE,
	INDEX_FOUR,
	INDEX_FIVE,
	INDEX_ERROR,
};

static unsigned int get_msr_base(struct kvm_pmu *pmu, enum pmu_type type)
{
	struct kvm_vcpu *vcpu = pmu_to_vcpu(pmu);

	if (guest_cpuid_has(vcpu, X86_FEATURE_PERFCTR_CORE)) {
		if (type == PMU_TYPE_COUNTER)
			return MSR_F15H_PERF_CTR;
		else
			return MSR_F15H_PERF_CTL;
	} else {
		if (type == PMU_TYPE_COUNTER)
			return MSR_K7_PERFCTR0;
		else
			return MSR_K7_EVNTSEL0;
	}
}

static enum index msr_to_index(u32 msr)
{
	switch (msr) {
	case MSR_F15H_PERF_CTL0:
	case MSR_F15H_PERF_CTR0:
	case MSR_K7_EVNTSEL0:
	case MSR_K7_PERFCTR0:
		return INDEX_ZERO;
	case MSR_F15H_PERF_CTL1:
	case MSR_F15H_PERF_CTR1:
	case MSR_K7_EVNTSEL1:
	case MSR_K7_PERFCTR1:
		return INDEX_ONE;
	case MSR_F15H_PERF_CTL2:
	case MSR_F15H_PERF_CTR2:
	case MSR_K7_EVNTSEL2:
	case MSR_K7_PERFCTR2:
		return INDEX_TWO;
	case MSR_F15H_PERF_CTL3:
	case MSR_F15H_PERF_CTR3:
	case MSR_K7_EVNTSEL3:
	case MSR_K7_PERFCTR3:
		return INDEX_THREE;
	case MSR_F15H_PERF_CTL4:
	case MSR_F15H_PERF_CTR4:
		return INDEX_FOUR;
	case MSR_F15H_PERF_CTL5:
	case MSR_F15H_PERF_CTR5:
		return INDEX_FIVE;
	default:
		return INDEX_ERROR;
	}
}

static inline struct kvm_pmc *get_gp_pmc_amd(struct kvm_pmu *pmu, u32 msr,
					     enum pmu_type type)
{
	struct kvm_vcpu *vcpu = pmu_to_vcpu(pmu);

	if (!vcpu->kvm->arch.enable_pmu)
		return NULL;

	switch (msr) {
	case MSR_F15H_PERF_CTL0:
	case MSR_F15H_PERF_CTL1:
	case MSR_F15H_PERF_CTL2:
	case MSR_F15H_PERF_CTL3:
	case MSR_F15H_PERF_CTL4:
	case MSR_F15H_PERF_CTL5:
		if (!guest_cpuid_has(vcpu, X86_FEATURE_PERFCTR_CORE))
			return NULL;
		fallthrough;
	case MSR_K7_EVNTSEL0 ... MSR_K7_EVNTSEL3:
		if (type != PMU_TYPE_EVNTSEL)
			return NULL;
		break;
	case MSR_F15H_PERF_CTR0:
	case MSR_F15H_PERF_CTR1:
	case MSR_F15H_PERF_CTR2:
	case MSR_F15H_PERF_CTR3:
	case MSR_F15H_PERF_CTR4:
	case MSR_F15H_PERF_CTR5:
		if (!guest_cpuid_has(vcpu, X86_FEATURE_PERFCTR_CORE))
			return NULL;
		fallthrough;
	case MSR_K7_PERFCTR0 ... MSR_K7_PERFCTR3:
		if (type != PMU_TYPE_COUNTER)
			return NULL;
		break;
	default:
		return NULL;
	}

	return &pmu->gp_counters[msr_to_index(msr)];
}

static bool amd_hw_event_available(struct kvm_pmc *pmc)
{
	return true;
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
	unsigned int base = get_msr_base(pmu, PMU_TYPE_COUNTER);
	struct kvm_vcpu *vcpu = pmu_to_vcpu(pmu);

	if (guest_cpuid_has(vcpu, X86_FEATURE_PERFCTR_CORE)) {
		/*
		 * The idx is contiguous. The MSRs are not. The counter MSRs
		 * are interleaved with the event select MSRs.
		 */
		pmc_idx *= 2;
	}

	return get_gp_pmc_amd(pmu, base + pmc_idx, PMU_TYPE_COUNTER);
}

static bool amd_is_valid_rdpmc_ecx(struct kvm_vcpu *vcpu, unsigned int idx)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);

	idx &= ~(3u << 30);

	return idx < pmu->nr_arch_gp_counters;
}

/* idx is the ECX register of RDPMC instruction */
static struct kvm_pmc *amd_rdpmc_ecx_to_pmc(struct kvm_vcpu *vcpu,
	unsigned int idx, u64 *mask)
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
	/* All MSRs refer to exactly one PMC, so msr_idx_to_pmc is enough.  */
	return false;
}

static struct kvm_pmc *amd_msr_idx_to_pmc(struct kvm_vcpu *vcpu, u32 msr)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	struct kvm_pmc *pmc;

	pmc = get_gp_pmc_amd(pmu, msr, PMU_TYPE_COUNTER);
	pmc = pmc ? pmc : get_gp_pmc_amd(pmu, msr, PMU_TYPE_EVNTSEL);

	return pmc;
}

static int amd_pmu_get_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	struct kvm_pmc *pmc;
	u32 msr = msr_info->index;

	/* MSR_PERFCTRn */
	pmc = get_gp_pmc_amd(pmu, msr, PMU_TYPE_COUNTER);
	if (pmc) {
		msr_info->data = pmc_read_counter(pmc);
		return 0;
	}
	/* MSR_EVNTSELn */
	pmc = get_gp_pmc_amd(pmu, msr, PMU_TYPE_EVNTSEL);
	if (pmc) {
		msr_info->data = pmc->eventsel;
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

	/* MSR_PERFCTRn */
	pmc = get_gp_pmc_amd(pmu, msr, PMU_TYPE_COUNTER);
	if (pmc) {
		pmc->counter += data - pmc_read_counter(pmc);
		pmc_update_sample_period(pmc);
		return 0;
	}
	/* MSR_EVNTSELn */
	pmc = get_gp_pmc_amd(pmu, msr, PMU_TYPE_EVNTSEL);
	if (pmc) {
		data &= ~pmu->reserved_bits;
		if (data != pmc->eventsel) {
			pmc->eventsel = data;
			reprogram_counter(pmc);
		}
		return 0;
	}

	return 1;
}

static void amd_pmu_refresh(struct kvm_vcpu *vcpu)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);

	if (guest_cpuid_has(vcpu, X86_FEATURE_PERFCTR_CORE))
		pmu->nr_arch_gp_counters = AMD64_NUM_COUNTERS_CORE;
	else
		pmu->nr_arch_gp_counters = AMD64_NUM_COUNTERS;

	pmu->counter_bitmask[KVM_PMC_GP] = ((u64)1 << 48) - 1;
	pmu->reserved_bits = 0xfffffff000280000ull;
	pmu->raw_event_mask = AMD64_RAW_EVENT_MASK;
	pmu->version = 1;
	/* not applicable to AMD; but clean them to prevent any fall out */
	pmu->counter_bitmask[KVM_PMC_FIXED] = 0;
	pmu->nr_arch_fixed_counters = 0;
	pmu->global_status = 0;
	bitmap_set(pmu->all_valid_pmc_idx, 0, pmu->nr_arch_gp_counters);
}

static void amd_pmu_init(struct kvm_vcpu *vcpu)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	int i;

	BUILD_BUG_ON(AMD64_NUM_COUNTERS_CORE > INTEL_PMC_MAX_GENERIC);

	for (i = 0; i < AMD64_NUM_COUNTERS_CORE ; i++) {
		pmu->gp_counters[i].type = KVM_PMC_GP;
		pmu->gp_counters[i].vcpu = vcpu;
		pmu->gp_counters[i].idx = i;
		pmu->gp_counters[i].current_config = 0;
	}
}

static void amd_pmu_reset(struct kvm_vcpu *vcpu)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	int i;

	for (i = 0; i < AMD64_NUM_COUNTERS_CORE; i++) {
		struct kvm_pmc *pmc = &pmu->gp_counters[i];

		pmc_stop_counter(pmc);
		pmc->counter = pmc->eventsel = 0;
	}
}

struct kvm_pmu_ops amd_pmu_ops __initdata = {
	.hw_event_available = amd_hw_event_available,
	.pmc_is_enabled = amd_pmc_is_enabled,
	.pmc_idx_to_pmc = amd_pmc_idx_to_pmc,
	.rdpmc_ecx_to_pmc = amd_rdpmc_ecx_to_pmc,
	.msr_idx_to_pmc = amd_msr_idx_to_pmc,
	.is_valid_rdpmc_ecx = amd_is_valid_rdpmc_ecx,
	.is_valid_msr = amd_is_valid_msr,
	.get_msr = amd_pmu_get_msr,
	.set_msr = amd_pmu_set_msr,
	.refresh = amd_pmu_refresh,
	.init = amd_pmu_init,
	.reset = amd_pmu_reset,
};
