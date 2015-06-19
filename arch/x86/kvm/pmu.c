/*
 * Kernel-based Virtual Machine -- Performance Monitoring Unit support
 *
 * Copyright 2011 Red Hat, Inc. and/or its affiliates.
 *
 * Authors:
 *   Avi Kivity   <avi@redhat.com>
 *   Gleb Natapov <gleb@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include <linux/types.h>
#include <linux/kvm_host.h>
#include <linux/perf_event.h>
#include <asm/perf_event.h>
#include "x86.h"
#include "cpuid.h"
#include "lapic.h"
#include "pmu.h"

static struct kvm_event_hw_type_mapping arch_events[] = {
	/* Index must match CPUID 0x0A.EBX bit vector */
	[0] = { 0x3c, 0x00, PERF_COUNT_HW_CPU_CYCLES },
	[1] = { 0xc0, 0x00, PERF_COUNT_HW_INSTRUCTIONS },
	[2] = { 0x3c, 0x01, PERF_COUNT_HW_BUS_CYCLES  },
	[3] = { 0x2e, 0x4f, PERF_COUNT_HW_CACHE_REFERENCES },
	[4] = { 0x2e, 0x41, PERF_COUNT_HW_CACHE_MISSES },
	[5] = { 0xc4, 0x00, PERF_COUNT_HW_BRANCH_INSTRUCTIONS },
	[6] = { 0xc5, 0x00, PERF_COUNT_HW_BRANCH_MISSES },
	[7] = { 0x00, 0x30, PERF_COUNT_HW_REF_CPU_CYCLES },
};

/* mapping between fixed pmc index and arch_events array */
static int fixed_pmc_events[] = {1, 0, 7};

static bool pmc_is_gp(struct kvm_pmc *pmc)
{
	return pmc->type == KVM_PMC_GP;
}

static inline u64 pmc_bitmask(struct kvm_pmc *pmc)
{
	struct kvm_pmu *pmu = pmc_to_pmu(pmc);

	return pmu->counter_bitmask[pmc->type];
}

static inline bool pmc_is_enabled(struct kvm_pmc *pmc)
{
	struct kvm_pmu *pmu = pmc_to_pmu(pmc);
	return test_bit(pmc->idx, (unsigned long *)&pmu->global_ctrl);
}

static inline struct kvm_pmc *get_gp_pmc(struct kvm_pmu *pmu, u32 msr,
					 u32 base)
{
	if (msr >= base && msr < base + pmu->nr_arch_gp_counters)
		return &pmu->gp_counters[msr - base];
	return NULL;
}

static inline struct kvm_pmc *get_fixed_pmc(struct kvm_pmu *pmu, u32 msr)
{
	int base = MSR_CORE_PERF_FIXED_CTR0;
	if (msr >= base && msr < base + pmu->nr_arch_fixed_counters)
		return &pmu->fixed_counters[msr - base];
	return NULL;
}

static inline struct kvm_pmc *get_fixed_pmc_idx(struct kvm_pmu *pmu, int idx)
{
	return get_fixed_pmc(pmu, MSR_CORE_PERF_FIXED_CTR0 + idx);
}

static struct kvm_pmc *global_idx_to_pmc(struct kvm_pmu *pmu, int idx)
{
	if (idx < INTEL_PMC_IDX_FIXED)
		return get_gp_pmc(pmu, MSR_P6_EVNTSEL0 + idx, MSR_P6_EVNTSEL0);
	else
		return get_fixed_pmc_idx(pmu, idx - INTEL_PMC_IDX_FIXED);
}

static void kvm_pmi_trigger_fn(struct irq_work *irq_work)
{
	struct kvm_pmu *pmu = container_of(irq_work, struct kvm_pmu, irq_work);
	struct kvm_vcpu *vcpu = pmu_to_vcpu(pmu);

	kvm_pmu_deliver_pmi(vcpu);
}

static void kvm_perf_overflow(struct perf_event *perf_event,
			      struct perf_sample_data *data,
			      struct pt_regs *regs)
{
	struct kvm_pmc *pmc = perf_event->overflow_handler_context;
	struct kvm_pmu *pmu = pmc_to_pmu(pmc);

	if (!test_and_set_bit(pmc->idx,
			      (unsigned long *)&pmu->reprogram_pmi)) {
		__set_bit(pmc->idx, (unsigned long *)&pmu->global_status);
		kvm_make_request(KVM_REQ_PMU, pmc->vcpu);
	}
}

static void kvm_perf_overflow_intr(struct perf_event *perf_event,
				   struct perf_sample_data *data,
				   struct pt_regs *regs)
{
	struct kvm_pmc *pmc = perf_event->overflow_handler_context;
	struct kvm_pmu *pmu = pmc_to_pmu(pmc);

	if (!test_and_set_bit(pmc->idx,
			      (unsigned long *)&pmu->reprogram_pmi)) {
		__set_bit(pmc->idx, (unsigned long *)&pmu->global_status);
		kvm_make_request(KVM_REQ_PMU, pmc->vcpu);

		/*
		 * Inject PMI. If vcpu was in a guest mode during NMI PMI
		 * can be ejected on a guest mode re-entry. Otherwise we can't
		 * be sure that vcpu wasn't executing hlt instruction at the
		 * time of vmexit and is not going to re-enter guest mode until
		 * woken up. So we should wake it, but this is impossible from
		 * NMI context. Do it from irq work instead.
		 */
		if (!kvm_is_in_guest())
			irq_work_queue(&pmc_to_pmu(pmc)->irq_work);
		else
			kvm_make_request(KVM_REQ_PMI, pmc->vcpu);
	}
}

static u64 pmc_read_counter(struct kvm_pmc *pmc)
{
	u64 counter, enabled, running;

	counter = pmc->counter;

	if (pmc->perf_event)
		counter += perf_event_read_value(pmc->perf_event,
						 &enabled, &running);

	/* FIXME: Scaling needed? */

	return counter & pmc_bitmask(pmc);
}

static void pmc_stop_counter(struct kvm_pmc *pmc)
{
	if (pmc->perf_event) {
		pmc->counter = pmc_read_counter(pmc);
		perf_event_release_kernel(pmc->perf_event);
		pmc->perf_event = NULL;
	}
}

static void pmc_reprogram_counter(struct kvm_pmc *pmc, u32 type,
				  unsigned config, bool exclude_user,
				  bool exclude_kernel, bool intr,
				  bool in_tx, bool in_tx_cp)
{
	struct perf_event *event;
	struct perf_event_attr attr = {
		.type = type,
		.size = sizeof(attr),
		.pinned = true,
		.exclude_idle = true,
		.exclude_host = 1,
		.exclude_user = exclude_user,
		.exclude_kernel = exclude_kernel,
		.config = config,
	};

	if (in_tx)
		attr.config |= HSW_IN_TX;
	if (in_tx_cp)
		attr.config |= HSW_IN_TX_CHECKPOINTED;

	attr.sample_period = (-pmc->counter) & pmc_bitmask(pmc);

	event = perf_event_create_kernel_counter(&attr, -1, current,
						 intr ? kvm_perf_overflow_intr :
						 kvm_perf_overflow, pmc);
	if (IS_ERR(event)) {
		printk_once("kvm_pmu: event creation failed %ld\n",
			    PTR_ERR(event));
		return;
	}

	pmc->perf_event = event;
	clear_bit(pmc->idx, (unsigned long*)&pmc_to_pmu(pmc)->reprogram_pmi);
}

static unsigned find_arch_event(struct kvm_pmu *pmu, u8 event_select,
		u8 unit_mask)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(arch_events); i++)
		if (arch_events[i].eventsel == event_select
				&& arch_events[i].unit_mask == unit_mask
				&& (pmu->available_event_types & (1 << i)))
			break;

	if (i == ARRAY_SIZE(arch_events))
		return PERF_COUNT_HW_MAX;

	return arch_events[i].event_type;
}

static void reprogram_gp_counter(struct kvm_pmc *pmc, u64 eventsel)
{
	unsigned config, type = PERF_TYPE_RAW;
	u8 event_select, unit_mask;

	if (eventsel & ARCH_PERFMON_EVENTSEL_PIN_CONTROL)
		printk_once("kvm pmu: pin control bit is ignored\n");

	pmc->eventsel = eventsel;

	pmc_stop_counter(pmc);

	if (!(eventsel & ARCH_PERFMON_EVENTSEL_ENABLE) || !pmc_is_enabled(pmc))
		return;

	event_select = eventsel & ARCH_PERFMON_EVENTSEL_EVENT;
	unit_mask = (eventsel & ARCH_PERFMON_EVENTSEL_UMASK) >> 8;

	if (!(eventsel & (ARCH_PERFMON_EVENTSEL_EDGE |
			  ARCH_PERFMON_EVENTSEL_INV |
			  ARCH_PERFMON_EVENTSEL_CMASK |
			  HSW_IN_TX |
			  HSW_IN_TX_CHECKPOINTED))) {
		config = find_arch_event(pmc_to_pmu(pmc), event_select,
				unit_mask);
		if (config != PERF_COUNT_HW_MAX)
			type = PERF_TYPE_HARDWARE;
	}

	if (type == PERF_TYPE_RAW)
		config = eventsel & X86_RAW_EVENT_MASK;

	pmc_reprogram_counter(pmc, type, config,
			      !(eventsel & ARCH_PERFMON_EVENTSEL_USR),
			      !(eventsel & ARCH_PERFMON_EVENTSEL_OS),
			      eventsel & ARCH_PERFMON_EVENTSEL_INT,
			      (eventsel & HSW_IN_TX),
			      (eventsel & HSW_IN_TX_CHECKPOINTED));
}

static void reprogram_fixed_counter(struct kvm_pmc *pmc, u8 ctrl, int idx)
{
	unsigned en_field = ctrl & 0x3;
	bool pmi = ctrl & 0x8;

	pmc_stop_counter(pmc);

	if (!en_field || !pmc_is_enabled(pmc))
		return;

	pmc_reprogram_counter(pmc, PERF_TYPE_HARDWARE,
			      arch_events[fixed_pmc_events[idx]].event_type,
			      !(en_field & 0x2), /* exclude user */
			      !(en_field & 0x1), /* exclude kernel */
			      pmi, false, false);
}

static inline u8 fixed_ctrl_field(u64 ctrl, int idx)
{
	return (ctrl >> (idx * 4)) & 0xf;
}

static void reprogram_fixed_counters(struct kvm_pmu *pmu, u64 data)
{
	int i;

	for (i = 0; i < pmu->nr_arch_fixed_counters; i++) {
		u8 old_ctrl = fixed_ctrl_field(pmu->fixed_ctr_ctrl, i);
		u8 new_ctrl = fixed_ctrl_field(data, i);
		struct kvm_pmc *pmc = get_fixed_pmc_idx(pmu, i);

		if (old_ctrl == new_ctrl)
			continue;

		reprogram_fixed_counter(pmc, new_ctrl, i);
	}

	pmu->fixed_ctr_ctrl = data;
}

static void reprogram_counter(struct kvm_pmu *pmu, int pmc_idx)
{
	struct kvm_pmc *pmc = global_idx_to_pmc(pmu, pmc_idx);

	if (!pmc)
		return;

	if (pmc_is_gp(pmc))
		reprogram_gp_counter(pmc, pmc->eventsel);
	else {
		int idx = pmc_idx - INTEL_PMC_IDX_FIXED;
		u8 ctrl = fixed_ctrl_field(pmu->fixed_ctr_ctrl, idx);

		reprogram_fixed_counter(pmc, ctrl, idx);
	}
}

static void global_ctrl_changed(struct kvm_pmu *pmu, u64 data)
{
	int bit;
	u64 diff = pmu->global_ctrl ^ data;

	pmu->global_ctrl = data;

	for_each_set_bit(bit, (unsigned long *)&diff, X86_PMC_IDX_MAX)
		reprogram_counter(pmu, bit);
}

void kvm_pmu_handle_event(struct kvm_vcpu *vcpu)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	u64 bitmask;
	int bit;

	bitmask = pmu->reprogram_pmi;

	for_each_set_bit(bit, (unsigned long *)&bitmask, X86_PMC_IDX_MAX) {
		struct kvm_pmc *pmc = global_idx_to_pmc(pmu, bit);

		if (unlikely(!pmc || !pmc->perf_event)) {
			clear_bit(bit, (unsigned long *)&pmu->reprogram_pmi);
			continue;
		}

		reprogram_counter(pmu, bit);
	}
}

/* check if idx is a valid index to access PMU */
int kvm_pmu_is_valid_msr_idx(struct kvm_vcpu *vcpu, unsigned idx)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	bool fixed = idx & (1u << 30);
	idx &= ~(3u << 30);
	return (!fixed && idx >= pmu->nr_arch_gp_counters) ||
		(fixed && idx >= pmu->nr_arch_fixed_counters);
}

int kvm_pmu_rdpmc(struct kvm_vcpu *vcpu, unsigned idx, u64 *data)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	bool fast_mode = idx & (1u << 31);
	bool fixed = idx & (1u << 30);
	struct kvm_pmc *counters;
	u64 ctr_val;

	idx &= ~(3u << 30);
	if (!fixed && idx >= pmu->nr_arch_gp_counters)
		return 1;
	if (fixed && idx >= pmu->nr_arch_fixed_counters)
		return 1;
	counters = fixed ? pmu->fixed_counters : pmu->gp_counters;

	ctr_val = pmc_read_counter(&counters[idx]);
	if (fast_mode)
		ctr_val = (u32)ctr_val;

	*data = ctr_val;
	return 0;
}

void kvm_pmu_deliver_pmi(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.apic)
		kvm_apic_local_deliver(vcpu->arch.apic, APIC_LVTPC);
}

bool kvm_pmu_is_valid_msr(struct kvm_vcpu *vcpu, u32 msr)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	int ret;

	switch (msr) {
	case MSR_CORE_PERF_FIXED_CTR_CTRL:
	case MSR_CORE_PERF_GLOBAL_STATUS:
	case MSR_CORE_PERF_GLOBAL_CTRL:
	case MSR_CORE_PERF_GLOBAL_OVF_CTRL:
		ret = pmu->version > 1;
		break;
	default:
		ret = get_gp_pmc(pmu, msr, MSR_IA32_PERFCTR0)
			|| get_gp_pmc(pmu, msr, MSR_P6_EVNTSEL0)
			|| get_fixed_pmc(pmu, msr);
		break;
	}
	return ret;
}

int kvm_pmu_get_msr(struct kvm_vcpu *vcpu, u32 index, u64 *data)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	struct kvm_pmc *pmc;

	switch (index) {
	case MSR_CORE_PERF_FIXED_CTR_CTRL:
		*data = pmu->fixed_ctr_ctrl;
		return 0;
	case MSR_CORE_PERF_GLOBAL_STATUS:
		*data = pmu->global_status;
		return 0;
	case MSR_CORE_PERF_GLOBAL_CTRL:
		*data = pmu->global_ctrl;
		return 0;
	case MSR_CORE_PERF_GLOBAL_OVF_CTRL:
		*data = pmu->global_ovf_ctrl;
		return 0;
	default:
		if ((pmc = get_gp_pmc(pmu, index, MSR_IA32_PERFCTR0)) ||
				(pmc = get_fixed_pmc(pmu, index))) {
			*data = pmc_read_counter(pmc);
			return 0;
		} else if ((pmc = get_gp_pmc(pmu, index, MSR_P6_EVNTSEL0))) {
			*data = pmc->eventsel;
			return 0;
		}
	}
	return 1;
}

int kvm_pmu_set_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	struct kvm_pmc *pmc;
	u32 index = msr_info->index;
	u64 data = msr_info->data;

	switch (index) {
	case MSR_CORE_PERF_FIXED_CTR_CTRL:
		if (pmu->fixed_ctr_ctrl == data)
			return 0;
		if (!(data & 0xfffffffffffff444ull)) {
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
		if (!(data & pmu->global_ctrl_mask)) {
			global_ctrl_changed(pmu, data);
			return 0;
		}
		break;
	case MSR_CORE_PERF_GLOBAL_OVF_CTRL:
		if (!(data & (pmu->global_ctrl_mask & ~(3ull<<62)))) {
			if (!msr_info->host_initiated)
				pmu->global_status &= ~data;
			pmu->global_ovf_ctrl = data;
			return 0;
		}
		break;
	default:
		if ((pmc = get_gp_pmc(pmu, index, MSR_IA32_PERFCTR0)) ||
				(pmc = get_fixed_pmc(pmu, index))) {
			if (!msr_info->host_initiated)
				data = (s64)(s32)data;
			pmc->counter += data - pmc_read_counter(pmc);
			return 0;
		} else if ((pmc = get_gp_pmc(pmu, index, MSR_P6_EVNTSEL0))) {
			if (data == pmc->eventsel)
				return 0;
			if (!(data & pmu->reserved_bits)) {
				reprogram_gp_counter(pmc, data);
				return 0;
			}
		}
	}
	return 1;
}

/* refresh PMU settings. This function generally is called when underlying
 * settings are changed (such as changes of PMU CPUID by guest VMs), which
 * should rarely happen.
 */
void kvm_pmu_refresh(struct kvm_vcpu *vcpu)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	struct kvm_cpuid_entry2 *entry;
	union cpuid10_eax eax;
	union cpuid10_edx edx;

	pmu->nr_arch_gp_counters = 0;
	pmu->nr_arch_fixed_counters = 0;
	pmu->counter_bitmask[KVM_PMC_GP] = 0;
	pmu->counter_bitmask[KVM_PMC_FIXED] = 0;
	pmu->version = 0;
	pmu->reserved_bits = 0xffffffff00200000ull;

	entry = kvm_find_cpuid_entry(vcpu, 0xa, 0);
	if (!entry)
		return;
	eax.full = entry->eax;
	edx.full = entry->edx;

	pmu->version = eax.split.version_id;
	if (!pmu->version)
		return;

	pmu->nr_arch_gp_counters = min_t(int, eax.split.num_counters,
					INTEL_PMC_MAX_GENERIC);
	pmu->counter_bitmask[KVM_PMC_GP] = ((u64)1 << eax.split.bit_width) - 1;
	pmu->available_event_types = ~entry->ebx &
					((1ull << eax.split.mask_length) - 1);

	if (pmu->version == 1) {
		pmu->nr_arch_fixed_counters = 0;
	} else {
		pmu->nr_arch_fixed_counters =
			min_t(int, edx.split.num_counters_fixed,
				INTEL_PMC_MAX_FIXED);
		pmu->counter_bitmask[KVM_PMC_FIXED] =
			((u64)1 << edx.split.bit_width_fixed) - 1;
	}

	pmu->global_ctrl = ((1 << pmu->nr_arch_gp_counters) - 1) |
		(((1ull << pmu->nr_arch_fixed_counters) - 1) << INTEL_PMC_IDX_FIXED);
	pmu->global_ctrl_mask = ~pmu->global_ctrl;

	entry = kvm_find_cpuid_entry(vcpu, 7, 0);
	if (entry &&
	    (boot_cpu_has(X86_FEATURE_HLE) || boot_cpu_has(X86_FEATURE_RTM)) &&
	    (entry->ebx & (X86_FEATURE_HLE|X86_FEATURE_RTM)))
		pmu->reserved_bits ^= HSW_IN_TX|HSW_IN_TX_CHECKPOINTED;
}

void kvm_pmu_reset(struct kvm_vcpu *vcpu)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	int i;

	irq_work_sync(&pmu->irq_work);
	for (i = 0; i < INTEL_PMC_MAX_GENERIC; i++) {
		struct kvm_pmc *pmc = &pmu->gp_counters[i];
		pmc_stop_counter(pmc);
		pmc->counter = pmc->eventsel = 0;
	}

	for (i = 0; i < INTEL_PMC_MAX_FIXED; i++)
		pmc_stop_counter(&pmu->fixed_counters[i]);

	pmu->fixed_ctr_ctrl = pmu->global_ctrl = pmu->global_status =
		pmu->global_ovf_ctrl = 0;
}

void kvm_pmu_init(struct kvm_vcpu *vcpu)
{
	int i;
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);

	memset(pmu, 0, sizeof(*pmu));
	for (i = 0; i < INTEL_PMC_MAX_GENERIC; i++) {
		pmu->gp_counters[i].type = KVM_PMC_GP;
		pmu->gp_counters[i].vcpu = vcpu;
		pmu->gp_counters[i].idx = i;
	}
	for (i = 0; i < INTEL_PMC_MAX_FIXED; i++) {
		pmu->fixed_counters[i].type = KVM_PMC_FIXED;
		pmu->fixed_counters[i].vcpu = vcpu;
		pmu->fixed_counters[i].idx = i + INTEL_PMC_IDX_FIXED;
	}
	init_irq_work(&pmu->irq_work, kvm_pmi_trigger_fn);
	kvm_pmu_refresh(vcpu);
}

void kvm_pmu_destroy(struct kvm_vcpu *vcpu)
{
	kvm_pmu_reset(vcpu);
}
