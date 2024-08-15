// SPDX-License-Identifier: GPL-2.0-only
/*
 * KVM PMU support for Intel CPUs
 *
 * Copyright 2011 Red Hat, Inc. and/or its affiliates.
 *
 * Authors:
 *   Avi Kivity   <avi@redhat.com>
 *   Gleb Natapov <gleb@redhat.com>
 */
#include <linux/types.h>
#include <linux/kvm_host.h>
#include <linux/perf_event.h>
#include <asm/perf_event.h>
#include "x86.h"
#include "cpuid.h"
#include "lapic.h"
#include "nested.h"
#include "pmu.h"

#define MSR_PMC_FULL_WIDTH_BIT      (MSR_IA32_PMC0 - MSR_IA32_PERFCTR0)

static struct kvm_event_hw_type_mapping intel_arch_events[] = {
	[0] = { 0x3c, 0x00, PERF_COUNT_HW_CPU_CYCLES },
	[1] = { 0xc0, 0x00, PERF_COUNT_HW_INSTRUCTIONS },
	[2] = { 0x3c, 0x01, PERF_COUNT_HW_BUS_CYCLES  },
	[3] = { 0x2e, 0x4f, PERF_COUNT_HW_CACHE_REFERENCES },
	[4] = { 0x2e, 0x41, PERF_COUNT_HW_CACHE_MISSES },
	[5] = { 0xc4, 0x00, PERF_COUNT_HW_BRANCH_INSTRUCTIONS },
	[6] = { 0xc5, 0x00, PERF_COUNT_HW_BRANCH_MISSES },
	/* The above index must match CPUID 0x0A.EBX bit vector */
	[7] = { 0x00, 0x03, PERF_COUNT_HW_REF_CPU_CYCLES },
};

/* mapping between fixed pmc index and intel_arch_events array */
static int fixed_pmc_events[] = {1, 0, 7};

static void reprogram_fixed_counters(struct kvm_pmu *pmu, u64 data)
{
	struct kvm_pmc *pmc;
	u64 old_fixed_ctr_ctrl = pmu->fixed_ctr_ctrl;
	int i;

	pmu->fixed_ctr_ctrl = data;
	for (i = 0; i < pmu->nr_arch_fixed_counters; i++) {
		u8 new_ctrl = fixed_ctrl_field(data, i);
		u8 old_ctrl = fixed_ctrl_field(old_fixed_ctr_ctrl, i);

		if (old_ctrl == new_ctrl)
			continue;

		pmc = get_fixed_pmc(pmu, MSR_CORE_PERF_FIXED_CTR0 + i);

		__set_bit(INTEL_PMC_IDX_FIXED + i, pmu->pmc_in_use);
		reprogram_counter(pmc);
	}
}

static struct kvm_pmc *intel_pmc_idx_to_pmc(struct kvm_pmu *pmu, int pmc_idx)
{
	if (pmc_idx < INTEL_PMC_IDX_FIXED) {
		return get_gp_pmc(pmu, MSR_P6_EVNTSEL0 + pmc_idx,
				  MSR_P6_EVNTSEL0);
	} else {
		u32 idx = pmc_idx - INTEL_PMC_IDX_FIXED;

		return get_fixed_pmc(pmu, idx + MSR_CORE_PERF_FIXED_CTR0);
	}
}

static void reprogram_counters(struct kvm_pmu *pmu, u64 diff)
{
	int bit;
	struct kvm_pmc *pmc;

	for_each_set_bit(bit, (unsigned long *)&diff, X86_PMC_IDX_MAX) {
		pmc = intel_pmc_idx_to_pmc(pmu, bit);
		if (pmc)
			reprogram_counter(pmc);
	}
}

static bool intel_hw_event_available(struct kvm_pmc *pmc)
{
	struct kvm_pmu *pmu = pmc_to_pmu(pmc);
	u8 event_select = pmc->eventsel & ARCH_PERFMON_EVENTSEL_EVENT;
	u8 unit_mask = (pmc->eventsel & ARCH_PERFMON_EVENTSEL_UMASK) >> 8;
	int i;

	for (i = 0; i < ARRAY_SIZE(intel_arch_events); i++) {
		if (intel_arch_events[i].eventsel != event_select ||
		    intel_arch_events[i].unit_mask != unit_mask)
			continue;

		/* disable event that reported as not present by cpuid */
		if ((i < 7) && !(pmu->available_event_types & (1 << i)))
			return false;

		break;
	}

	return true;
}

/* check if a PMC is enabled by comparing it with globl_ctrl bits. */
static bool intel_pmc_is_enabled(struct kvm_pmc *pmc)
{
	struct kvm_pmu *pmu = pmc_to_pmu(pmc);

	if (!intel_pmu_has_perf_global_ctrl(pmu))
		return true;

	return test_bit(pmc->idx, (unsigned long *)&pmu->global_ctrl);
}

static bool intel_is_valid_rdpmc_ecx(struct kvm_vcpu *vcpu, unsigned int idx)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	bool fixed = idx & (1u << 30);

	idx &= ~(3u << 30);

	return fixed ? idx < pmu->nr_arch_fixed_counters
		     : idx < pmu->nr_arch_gp_counters;
}

static struct kvm_pmc *intel_rdpmc_ecx_to_pmc(struct kvm_vcpu *vcpu,
					    unsigned int idx, u64 *mask)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	bool fixed = idx & (1u << 30);
	struct kvm_pmc *counters;
	unsigned int num_counters;

	idx &= ~(3u << 30);
	if (fixed) {
		counters = pmu->fixed_counters;
		num_counters = pmu->nr_arch_fixed_counters;
	} else {
		counters = pmu->gp_counters;
		num_counters = pmu->nr_arch_gp_counters;
	}
	if (idx >= num_counters)
		return NULL;
	*mask &= pmu->counter_bitmask[fixed ? KVM_PMC_FIXED : KVM_PMC_GP];
	return &counters[array_index_nospec(idx, num_counters)];
}

static inline u64 vcpu_get_perf_capabilities(struct kvm_vcpu *vcpu)
{
	if (!guest_cpuid_has(vcpu, X86_FEATURE_PDCM))
		return 0;

	return vcpu->arch.perf_capabilities;
}

static inline bool fw_writes_is_enabled(struct kvm_vcpu *vcpu)
{
	return (vcpu_get_perf_capabilities(vcpu) & PMU_CAP_FW_WRITES) != 0;
}

static inline struct kvm_pmc *get_fw_gp_pmc(struct kvm_pmu *pmu, u32 msr)
{
	if (!fw_writes_is_enabled(pmu_to_vcpu(pmu)))
		return NULL;

	return get_gp_pmc(pmu, msr, MSR_IA32_PMC0);
}

static bool intel_pmu_is_valid_lbr_msr(struct kvm_vcpu *vcpu, u32 index)
{
	struct x86_pmu_lbr *records = vcpu_to_lbr_records(vcpu);
	bool ret = false;

	if (!intel_pmu_lbr_is_enabled(vcpu))
		return ret;

	ret = (index == MSR_LBR_SELECT) || (index == MSR_LBR_TOS) ||
		(index >= records->from && index < records->from + records->nr) ||
		(index >= records->to && index < records->to + records->nr);

	if (!ret && records->info)
		ret = (index >= records->info && index < records->info + records->nr);

	return ret;
}

static bool intel_is_valid_msr(struct kvm_vcpu *vcpu, u32 msr)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	u64 perf_capabilities;
	int ret;

	switch (msr) {
	case MSR_CORE_PERF_FIXED_CTR_CTRL:
	case MSR_CORE_PERF_GLOBAL_STATUS:
	case MSR_CORE_PERF_GLOBAL_CTRL:
	case MSR_CORE_PERF_GLOBAL_OVF_CTRL:
		return intel_pmu_has_perf_global_ctrl(pmu);
		break;
	case MSR_IA32_PEBS_ENABLE:
		ret = vcpu_get_perf_capabilities(vcpu) & PERF_CAP_PEBS_FORMAT;
		break;
	case MSR_IA32_DS_AREA:
		ret = guest_cpuid_has(vcpu, X86_FEATURE_DS);
		break;
	case MSR_PEBS_DATA_CFG:
		perf_capabilities = vcpu_get_perf_capabilities(vcpu);
		ret = (perf_capabilities & PERF_CAP_PEBS_BASELINE) &&
			((perf_capabilities & PERF_CAP_PEBS_FORMAT) > 3);
		break;
	default:
		ret = get_gp_pmc(pmu, msr, MSR_IA32_PERFCTR0) ||
			get_gp_pmc(pmu, msr, MSR_P6_EVNTSEL0) ||
			get_fixed_pmc(pmu, msr) || get_fw_gp_pmc(pmu, msr) ||
			intel_pmu_is_valid_lbr_msr(vcpu, msr);
		break;
	}

	return ret;
}

static struct kvm_pmc *intel_msr_idx_to_pmc(struct kvm_vcpu *vcpu, u32 msr)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	struct kvm_pmc *pmc;

	pmc = get_fixed_pmc(pmu, msr);
	pmc = pmc ? pmc : get_gp_pmc(pmu, msr, MSR_P6_EVNTSEL0);
	pmc = pmc ? pmc : get_gp_pmc(pmu, msr, MSR_IA32_PERFCTR0);

	return pmc;
}

static inline void intel_pmu_release_guest_lbr_event(struct kvm_vcpu *vcpu)
{
	struct lbr_desc *lbr_desc = vcpu_to_lbr_desc(vcpu);

	if (lbr_desc->event) {
		perf_event_release_kernel(lbr_desc->event);
		lbr_desc->event = NULL;
		vcpu_to_pmu(vcpu)->event_count--;
	}
}

int intel_pmu_create_guest_lbr_event(struct kvm_vcpu *vcpu)
{
	struct lbr_desc *lbr_desc = vcpu_to_lbr_desc(vcpu);
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	struct perf_event *event;

	/*
	 * The perf_event_attr is constructed in the minimum efficient way:
	 * - set 'pinned = true' to make it task pinned so that if another
	 *   cpu pinned event reclaims LBR, the event->oncpu will be set to -1;
	 * - set '.exclude_host = true' to record guest branches behavior;
	 *
	 * - set '.config = INTEL_FIXED_VLBR_EVENT' to indicates host perf
	 *   schedule the event without a real HW counter but a fake one;
	 *   check is_guest_lbr_event() and __intel_get_event_constraints();
	 *
	 * - set 'sample_type = PERF_SAMPLE_BRANCH_STACK' and
	 *   'branch_sample_type = PERF_SAMPLE_BRANCH_CALL_STACK |
	 *   PERF_SAMPLE_BRANCH_USER' to configure it as a LBR callstack
	 *   event, which helps KVM to save/restore guest LBR records
	 *   during host context switches and reduces quite a lot overhead,
	 *   check branch_user_callstack() and intel_pmu_lbr_sched_task();
	 */
	struct perf_event_attr attr = {
		.type = PERF_TYPE_RAW,
		.size = sizeof(attr),
		.config = INTEL_FIXED_VLBR_EVENT,
		.sample_type = PERF_SAMPLE_BRANCH_STACK,
		.pinned = true,
		.exclude_host = true,
		.branch_sample_type = PERF_SAMPLE_BRANCH_CALL_STACK |
					PERF_SAMPLE_BRANCH_USER,
	};

	if (unlikely(lbr_desc->event)) {
		__set_bit(INTEL_PMC_IDX_FIXED_VLBR, pmu->pmc_in_use);
		return 0;
	}

	event = perf_event_create_kernel_counter(&attr, -1,
						current, NULL, NULL);
	if (IS_ERR(event)) {
		pr_debug_ratelimited("%s: failed %ld\n",
					__func__, PTR_ERR(event));
		return PTR_ERR(event);
	}
	lbr_desc->event = event;
	pmu->event_count++;
	__set_bit(INTEL_PMC_IDX_FIXED_VLBR, pmu->pmc_in_use);
	return 0;
}

/*
 * It's safe to access LBR msrs from guest when they have not
 * been passthrough since the host would help restore or reset
 * the LBR msrs records when the guest LBR event is scheduled in.
 */
static bool intel_pmu_handle_lbr_msrs_access(struct kvm_vcpu *vcpu,
				     struct msr_data *msr_info, bool read)
{
	struct lbr_desc *lbr_desc = vcpu_to_lbr_desc(vcpu);
	u32 index = msr_info->index;

	if (!intel_pmu_is_valid_lbr_msr(vcpu, index))
		return false;

	if (!lbr_desc->event && intel_pmu_create_guest_lbr_event(vcpu) < 0)
		goto dummy;

	/*
	 * Disable irq to ensure the LBR feature doesn't get reclaimed by the
	 * host at the time the value is read from the msr, and this avoids the
	 * host LBR value to be leaked to the guest. If LBR has been reclaimed,
	 * return 0 on guest reads.
	 */
	local_irq_disable();
	if (lbr_desc->event->state == PERF_EVENT_STATE_ACTIVE) {
		if (read)
			rdmsrl(index, msr_info->data);
		else
			wrmsrl(index, msr_info->data);
		__set_bit(INTEL_PMC_IDX_FIXED_VLBR, vcpu_to_pmu(vcpu)->pmc_in_use);
		local_irq_enable();
		return true;
	}
	clear_bit(INTEL_PMC_IDX_FIXED_VLBR, vcpu_to_pmu(vcpu)->pmc_in_use);
	local_irq_enable();

dummy:
	if (read)
		msr_info->data = 0;
	return true;
}

static int intel_pmu_get_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	struct kvm_pmc *pmc;
	u32 msr = msr_info->index;

	switch (msr) {
	case MSR_CORE_PERF_FIXED_CTR_CTRL:
		msr_info->data = pmu->fixed_ctr_ctrl;
		return 0;
	case MSR_CORE_PERF_GLOBAL_STATUS:
		msr_info->data = pmu->global_status;
		return 0;
	case MSR_CORE_PERF_GLOBAL_CTRL:
		msr_info->data = pmu->global_ctrl;
		return 0;
	case MSR_CORE_PERF_GLOBAL_OVF_CTRL:
		msr_info->data = 0;
		return 0;
	case MSR_IA32_PEBS_ENABLE:
		msr_info->data = pmu->pebs_enable;
		return 0;
	case MSR_IA32_DS_AREA:
		msr_info->data = pmu->ds_area;
		return 0;
	case MSR_PEBS_DATA_CFG:
		msr_info->data = pmu->pebs_data_cfg;
		return 0;
	default:
		if ((pmc = get_gp_pmc(pmu, msr, MSR_IA32_PERFCTR0)) ||
		    (pmc = get_gp_pmc(pmu, msr, MSR_IA32_PMC0))) {
			u64 val = pmc_read_counter(pmc);
			msr_info->data =
				val & pmu->counter_bitmask[KVM_PMC_GP];
			return 0;
		} else if ((pmc = get_fixed_pmc(pmu, msr))) {
			u64 val = pmc_read_counter(pmc);
			msr_info->data =
				val & pmu->counter_bitmask[KVM_PMC_FIXED];
			return 0;
		} else if ((pmc = get_gp_pmc(pmu, msr, MSR_P6_EVNTSEL0))) {
			msr_info->data = pmc->eventsel;
			return 0;
		} else if (intel_pmu_handle_lbr_msrs_access(vcpu, msr_info, true))
			return 0;
	}

	return 1;
}

static int intel_pmu_set_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	struct kvm_pmc *pmc;
	u32 msr = msr_info->index;
	u64 data = msr_info->data;
	u64 reserved_bits, diff;

	switch (msr) {
	case MSR_CORE_PERF_FIXED_CTR_CTRL:
		if (pmu->fixed_ctr_ctrl == data)
			return 0;
		if (!(data & pmu->fixed_ctr_ctrl_mask)) {
			reprogram_fixed_counters(pmu, data);
			return 0;
		}
		break;
	case MSR_CORE_PERF_GLOBAL_STATUS:
		if (msr_info->host_initiated) {
			pmu->global_status = data;
			return 0;
		}
		break; /* RO MSR */
	case MSR_CORE_PERF_GLOBAL_CTRL:
		if (pmu->global_ctrl == data)
			return 0;
		if (kvm_valid_perf_global_ctrl(pmu, data)) {
			diff = pmu->global_ctrl ^ data;
			pmu->global_ctrl = data;
			reprogram_counters(pmu, diff);
			return 0;
		}
		break;
	case MSR_CORE_PERF_GLOBAL_OVF_CTRL:
		if (!(data & pmu->global_ovf_ctrl_mask)) {
			if (!msr_info->host_initiated)
				pmu->global_status &= ~data;
			return 0;
		}
		break;
	case MSR_IA32_PEBS_ENABLE:
		if (pmu->pebs_enable == data)
			return 0;
		if (!(data & pmu->pebs_enable_mask)) {
			diff = pmu->pebs_enable ^ data;
			pmu->pebs_enable = data;
			reprogram_counters(pmu, diff);
			return 0;
		}
		break;
	case MSR_IA32_DS_AREA:
		if (msr_info->host_initiated && data && !guest_cpuid_has(vcpu, X86_FEATURE_DS))
			return 1;
		if (is_noncanonical_address(data, vcpu))
			return 1;
		pmu->ds_area = data;
		return 0;
	case MSR_PEBS_DATA_CFG:
		if (pmu->pebs_data_cfg == data)
			return 0;
		if (!(data & pmu->pebs_data_cfg_mask)) {
			pmu->pebs_data_cfg = data;
			return 0;
		}
		break;
	default:
		if ((pmc = get_gp_pmc(pmu, msr, MSR_IA32_PERFCTR0)) ||
		    (pmc = get_gp_pmc(pmu, msr, MSR_IA32_PMC0))) {
			if ((msr & MSR_PMC_FULL_WIDTH_BIT) &&
			    (data & ~pmu->counter_bitmask[KVM_PMC_GP]))
				return 1;
			if (!msr_info->host_initiated &&
			    !(msr & MSR_PMC_FULL_WIDTH_BIT))
				data = (s64)(s32)data;
			pmc_write_counter(pmc, data);
			pmc_update_sample_period(pmc);
			return 0;
		} else if ((pmc = get_fixed_pmc(pmu, msr))) {
			pmc_write_counter(pmc, data);
			pmc_update_sample_period(pmc);
			return 0;
		} else if ((pmc = get_gp_pmc(pmu, msr, MSR_P6_EVNTSEL0))) {
			if (data == pmc->eventsel)
				return 0;
			reserved_bits = pmu->reserved_bits;
			if ((pmc->idx == 2) &&
			    (pmu->raw_event_mask & HSW_IN_TX_CHECKPOINTED))
				reserved_bits ^= HSW_IN_TX_CHECKPOINTED;
			if (!(data & reserved_bits)) {
				pmc->eventsel = data;
				reprogram_counter(pmc);
				return 0;
			}
		} else if (intel_pmu_handle_lbr_msrs_access(vcpu, msr_info, false))
			return 0;
	}

	return 1;
}

static void setup_fixed_pmc_eventsel(struct kvm_pmu *pmu)
{
	size_t size = ARRAY_SIZE(fixed_pmc_events);
	struct kvm_pmc *pmc;
	u32 event;
	int i;

	for (i = 0; i < pmu->nr_arch_fixed_counters; i++) {
		pmc = &pmu->fixed_counters[i];
		event = fixed_pmc_events[array_index_nospec(i, size)];
		pmc->eventsel = (intel_arch_events[event].unit_mask << 8) |
			intel_arch_events[event].eventsel;
	}
}

static void intel_pmu_refresh(struct kvm_vcpu *vcpu)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	struct lbr_desc *lbr_desc = vcpu_to_lbr_desc(vcpu);
	struct kvm_cpuid_entry2 *entry;
	union cpuid10_eax eax;
	union cpuid10_edx edx;
	u64 perf_capabilities;
	u64 counter_mask;
	int i;

	pmu->nr_arch_gp_counters = 0;
	pmu->nr_arch_fixed_counters = 0;
	pmu->counter_bitmask[KVM_PMC_GP] = 0;
	pmu->counter_bitmask[KVM_PMC_FIXED] = 0;
	pmu->version = 0;
	pmu->reserved_bits = 0xffffffff00200000ull;
	pmu->raw_event_mask = X86_RAW_EVENT_MASK;
	pmu->global_ctrl_mask = ~0ull;
	pmu->global_ovf_ctrl_mask = ~0ull;
	pmu->fixed_ctr_ctrl_mask = ~0ull;
	pmu->pebs_enable_mask = ~0ull;
	pmu->pebs_data_cfg_mask = ~0ull;

	entry = kvm_find_cpuid_entry(vcpu, 0xa);
	if (!entry || !vcpu->kvm->arch.enable_pmu)
		return;
	eax.full = entry->eax;
	edx.full = entry->edx;

	pmu->version = eax.split.version_id;
	if (!pmu->version)
		return;

	pmu->nr_arch_gp_counters = min_t(int, eax.split.num_counters,
					 kvm_pmu_cap.num_counters_gp);
	eax.split.bit_width = min_t(int, eax.split.bit_width,
				    kvm_pmu_cap.bit_width_gp);
	pmu->counter_bitmask[KVM_PMC_GP] = ((u64)1 << eax.split.bit_width) - 1;
	eax.split.mask_length = min_t(int, eax.split.mask_length,
				      kvm_pmu_cap.events_mask_len);
	pmu->available_event_types = ~entry->ebx &
					((1ull << eax.split.mask_length) - 1);

	if (pmu->version == 1) {
		pmu->nr_arch_fixed_counters = 0;
	} else {
		pmu->nr_arch_fixed_counters =
			min3(ARRAY_SIZE(fixed_pmc_events),
			     (size_t) edx.split.num_counters_fixed,
			     (size_t)kvm_pmu_cap.num_counters_fixed);
		edx.split.bit_width_fixed = min_t(int, edx.split.bit_width_fixed,
						  kvm_pmu_cap.bit_width_fixed);
		pmu->counter_bitmask[KVM_PMC_FIXED] =
			((u64)1 << edx.split.bit_width_fixed) - 1;
		setup_fixed_pmc_eventsel(pmu);
	}

	for (i = 0; i < pmu->nr_arch_fixed_counters; i++)
		pmu->fixed_ctr_ctrl_mask &= ~(0xbull << (i * 4));
	counter_mask = ~(((1ull << pmu->nr_arch_gp_counters) - 1) |
		(((1ull << pmu->nr_arch_fixed_counters) - 1) << INTEL_PMC_IDX_FIXED));
	pmu->global_ctrl_mask = counter_mask;
	pmu->global_ovf_ctrl_mask = pmu->global_ctrl_mask
			& ~(MSR_CORE_PERF_GLOBAL_OVF_CTRL_OVF_BUF |
			    MSR_CORE_PERF_GLOBAL_OVF_CTRL_COND_CHGD);
	if (vmx_pt_mode_is_host_guest())
		pmu->global_ovf_ctrl_mask &=
				~MSR_CORE_PERF_GLOBAL_OVF_CTRL_TRACE_TOPA_PMI;

	entry = kvm_find_cpuid_entry_index(vcpu, 7, 0);
	if (entry &&
	    (boot_cpu_has(X86_FEATURE_HLE) || boot_cpu_has(X86_FEATURE_RTM)) &&
	    (entry->ebx & (X86_FEATURE_HLE|X86_FEATURE_RTM))) {
		pmu->reserved_bits ^= HSW_IN_TX;
		pmu->raw_event_mask |= (HSW_IN_TX|HSW_IN_TX_CHECKPOINTED);
	}

	bitmap_set(pmu->all_valid_pmc_idx,
		0, pmu->nr_arch_gp_counters);
	bitmap_set(pmu->all_valid_pmc_idx,
		INTEL_PMC_MAX_GENERIC, pmu->nr_arch_fixed_counters);

	perf_capabilities = vcpu_get_perf_capabilities(vcpu);
	if (cpuid_model_is_consistent(vcpu) &&
	    (perf_capabilities & PMU_CAP_LBR_FMT))
		x86_perf_get_lbr(&lbr_desc->records);
	else
		lbr_desc->records.nr = 0;

	if (lbr_desc->records.nr)
		bitmap_set(pmu->all_valid_pmc_idx, INTEL_PMC_IDX_FIXED_VLBR, 1);

	if (perf_capabilities & PERF_CAP_PEBS_FORMAT) {
		if (perf_capabilities & PERF_CAP_PEBS_BASELINE) {
			pmu->pebs_enable_mask = counter_mask;
			pmu->reserved_bits &= ~ICL_EVENTSEL_ADAPTIVE;
			for (i = 0; i < pmu->nr_arch_fixed_counters; i++) {
				pmu->fixed_ctr_ctrl_mask &=
					~(1ULL << (INTEL_PMC_IDX_FIXED + i * 4));
			}
			pmu->pebs_data_cfg_mask = ~0xff00000full;
		} else {
			pmu->pebs_enable_mask =
				~((1ull << pmu->nr_arch_gp_counters) - 1);
		}
	}
}

static void intel_pmu_init(struct kvm_vcpu *vcpu)
{
	int i;
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	struct lbr_desc *lbr_desc = vcpu_to_lbr_desc(vcpu);

	for (i = 0; i < KVM_INTEL_PMC_MAX_GENERIC; i++) {
		pmu->gp_counters[i].type = KVM_PMC_GP;
		pmu->gp_counters[i].vcpu = vcpu;
		pmu->gp_counters[i].idx = i;
		pmu->gp_counters[i].current_config = 0;
	}

	for (i = 0; i < KVM_PMC_MAX_FIXED; i++) {
		pmu->fixed_counters[i].type = KVM_PMC_FIXED;
		pmu->fixed_counters[i].vcpu = vcpu;
		pmu->fixed_counters[i].idx = i + INTEL_PMC_IDX_FIXED;
		pmu->fixed_counters[i].current_config = 0;
	}

	vcpu->arch.perf_capabilities = kvm_caps.supported_perf_cap;
	lbr_desc->records.nr = 0;
	lbr_desc->event = NULL;
	lbr_desc->msr_passthrough = false;
}

static void intel_pmu_reset(struct kvm_vcpu *vcpu)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	struct kvm_pmc *pmc = NULL;
	int i;

	for (i = 0; i < KVM_INTEL_PMC_MAX_GENERIC; i++) {
		pmc = &pmu->gp_counters[i];

		pmc_stop_counter(pmc);
		pmc->counter = pmc->eventsel = 0;
	}

	for (i = 0; i < KVM_PMC_MAX_FIXED; i++) {
		pmc = &pmu->fixed_counters[i];

		pmc_stop_counter(pmc);
		pmc->counter = 0;
	}

	pmu->fixed_ctr_ctrl = pmu->global_ctrl = pmu->global_status = 0;

	intel_pmu_release_guest_lbr_event(vcpu);
}

/*
 * Emulate LBR_On_PMI behavior for 1 < pmu.version < 4.
 *
 * If Freeze_LBR_On_PMI = 1, the LBR is frozen on PMI and
 * the KVM emulates to clear the LBR bit (bit 0) in IA32_DEBUGCTL.
 *
 * Guest needs to re-enable LBR to resume branches recording.
 */
static void intel_pmu_legacy_freezing_lbrs_on_pmi(struct kvm_vcpu *vcpu)
{
	u64 data = vmcs_read64(GUEST_IA32_DEBUGCTL);

	if (data & DEBUGCTLMSR_FREEZE_LBRS_ON_PMI) {
		data &= ~DEBUGCTLMSR_LBR;
		vmcs_write64(GUEST_IA32_DEBUGCTL, data);
	}
}

static void intel_pmu_deliver_pmi(struct kvm_vcpu *vcpu)
{
	u8 version = vcpu_to_pmu(vcpu)->version;

	if (!intel_pmu_lbr_is_enabled(vcpu))
		return;

	if (version > 1 && version < 4)
		intel_pmu_legacy_freezing_lbrs_on_pmi(vcpu);
}

static void vmx_update_intercept_for_lbr_msrs(struct kvm_vcpu *vcpu, bool set)
{
	struct x86_pmu_lbr *lbr = vcpu_to_lbr_records(vcpu);
	int i;

	for (i = 0; i < lbr->nr; i++) {
		vmx_set_intercept_for_msr(vcpu, lbr->from + i, MSR_TYPE_RW, set);
		vmx_set_intercept_for_msr(vcpu, lbr->to + i, MSR_TYPE_RW, set);
		if (lbr->info)
			vmx_set_intercept_for_msr(vcpu, lbr->info + i, MSR_TYPE_RW, set);
	}

	vmx_set_intercept_for_msr(vcpu, MSR_LBR_SELECT, MSR_TYPE_RW, set);
	vmx_set_intercept_for_msr(vcpu, MSR_LBR_TOS, MSR_TYPE_RW, set);
}

static inline void vmx_disable_lbr_msrs_passthrough(struct kvm_vcpu *vcpu)
{
	struct lbr_desc *lbr_desc = vcpu_to_lbr_desc(vcpu);

	if (!lbr_desc->msr_passthrough)
		return;

	vmx_update_intercept_for_lbr_msrs(vcpu, true);
	lbr_desc->msr_passthrough = false;
}

static inline void vmx_enable_lbr_msrs_passthrough(struct kvm_vcpu *vcpu)
{
	struct lbr_desc *lbr_desc = vcpu_to_lbr_desc(vcpu);

	if (lbr_desc->msr_passthrough)
		return;

	vmx_update_intercept_for_lbr_msrs(vcpu, false);
	lbr_desc->msr_passthrough = true;
}

/*
 * Higher priority host perf events (e.g. cpu pinned) could reclaim the
 * pmu resources (e.g. LBR) that were assigned to the guest. This is
 * usually done via ipi calls (more details in perf_install_in_context).
 *
 * Before entering the non-root mode (with irq disabled here), double
 * confirm that the pmu features enabled to the guest are not reclaimed
 * by higher priority host events. Otherwise, disallow vcpu's access to
 * the reclaimed features.
 */
void vmx_passthrough_lbr_msrs(struct kvm_vcpu *vcpu)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	struct lbr_desc *lbr_desc = vcpu_to_lbr_desc(vcpu);

	if (!lbr_desc->event) {
		vmx_disable_lbr_msrs_passthrough(vcpu);
		if (vmcs_read64(GUEST_IA32_DEBUGCTL) & DEBUGCTLMSR_LBR)
			goto warn;
		if (test_bit(INTEL_PMC_IDX_FIXED_VLBR, pmu->pmc_in_use))
			goto warn;
		return;
	}

	if (lbr_desc->event->state < PERF_EVENT_STATE_ACTIVE) {
		vmx_disable_lbr_msrs_passthrough(vcpu);
		__clear_bit(INTEL_PMC_IDX_FIXED_VLBR, pmu->pmc_in_use);
		goto warn;
	} else
		vmx_enable_lbr_msrs_passthrough(vcpu);

	return;

warn:
	pr_warn_ratelimited("kvm: vcpu-%d: fail to passthrough LBR.\n",
		vcpu->vcpu_id);
}

static void intel_pmu_cleanup(struct kvm_vcpu *vcpu)
{
	if (!(vmcs_read64(GUEST_IA32_DEBUGCTL) & DEBUGCTLMSR_LBR))
		intel_pmu_release_guest_lbr_event(vcpu);
}

void intel_pmu_cross_mapped_check(struct kvm_pmu *pmu)
{
	struct kvm_pmc *pmc = NULL;
	int bit, hw_idx;

	for_each_set_bit(bit, (unsigned long *)&pmu->global_ctrl,
			 X86_PMC_IDX_MAX) {
		pmc = intel_pmc_idx_to_pmc(pmu, bit);

		if (!pmc || !pmc_speculative_in_use(pmc) ||
		    !intel_pmc_is_enabled(pmc) || !pmc->perf_event)
			continue;

		/*
		 * A negative index indicates the event isn't mapped to a
		 * physical counter in the host, e.g. due to contention.
		 */
		hw_idx = pmc->perf_event->hw.idx;
		if (hw_idx != pmc->idx && hw_idx > -1)
			pmu->host_cross_mapped_mask |= BIT_ULL(hw_idx);
	}
}

struct kvm_pmu_ops intel_pmu_ops __initdata = {
	.hw_event_available = intel_hw_event_available,
	.pmc_is_enabled = intel_pmc_is_enabled,
	.pmc_idx_to_pmc = intel_pmc_idx_to_pmc,
	.rdpmc_ecx_to_pmc = intel_rdpmc_ecx_to_pmc,
	.msr_idx_to_pmc = intel_msr_idx_to_pmc,
	.is_valid_rdpmc_ecx = intel_is_valid_rdpmc_ecx,
	.is_valid_msr = intel_is_valid_msr,
	.get_msr = intel_pmu_get_msr,
	.set_msr = intel_pmu_set_msr,
	.refresh = intel_pmu_refresh,
	.init = intel_pmu_init,
	.reset = intel_pmu_reset,
	.deliver_pmi = intel_pmu_deliver_pmi,
	.cleanup = intel_pmu_cleanup,
};
