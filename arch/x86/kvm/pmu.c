// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel-based Virtual Machine -- Performance Monitoring Unit support
 *
 * Copyright 2015 Red Hat, Inc. and/or its affiliates.
 *
 * Authors:
 *   Avi Kivity   <avi@redhat.com>
 *   Gleb Natapov <gleb@redhat.com>
 *   Wei Huang    <wei@redhat.com>
 */

#include <linux/types.h>
#include <linux/kvm_host.h>
#include <linux/perf_event.h>
#include <asm/perf_event.h>
#include "x86.h"
#include "cpuid.h"
#include "lapic.h"
#include "pmu.h"

/* This is enough to filter the vast majority of currently defined events. */
#define KVM_PMU_EVENT_FILTER_MAX_EVENTS 300

/* NOTE:
 * - Each perf counter is defined as "struct kvm_pmc";
 * - There are two types of perf counters: general purpose (gp) and fixed.
 *   gp counters are stored in gp_counters[] and fixed counters are stored
 *   in fixed_counters[] respectively. Both of them are part of "struct
 *   kvm_pmu";
 * - pmu.c understands the difference between gp counters and fixed counters.
 *   However AMD doesn't support fixed-counters;
 * - There are three types of index to access perf counters (PMC):
 *     1. MSR (named msr): For example Intel has MSR_IA32_PERFCTRn and AMD
 *        has MSR_K7_PERFCTRn.
 *     2. MSR Index (named idx): This normally is used by RDPMC instruction.
 *        For instance AMD RDPMC instruction uses 0000_0003h in ECX to access
 *        C001_0007h (MSR_K7_PERCTR3). Intel has a similar mechanism, except
 *        that it also supports fixed counters. idx can be used to as index to
 *        gp and fixed counters.
 *     3. Global PMC Index (named pmc): pmc is an index specific to PMU
 *        code. Each pmc, stored in kvm_pmc.idx field, is unique across
 *        all perf counters (both gp and fixed). The mapping relationship
 *        between pmc and perf counters is as the following:
 *        * Intel: [0 .. INTEL_PMC_MAX_GENERIC-1] <=> gp counters
 *                 [INTEL_PMC_IDX_FIXED .. INTEL_PMC_IDX_FIXED + 2] <=> fixed
 *        * AMD:   [0 .. AMD64_NUM_COUNTERS-1] <=> gp counters
 */

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

	if (!test_and_set_bit(pmc->idx, pmu->reprogram_pmi)) {
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

	if (!test_and_set_bit(pmc->idx, pmu->reprogram_pmi)) {
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

static void pmc_reprogram_counter(struct kvm_pmc *pmc, u32 type,
				  u64 config, bool exclude_user,
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

	attr.sample_period = get_sample_period(pmc, pmc->counter);

	if (in_tx)
		attr.config |= HSW_IN_TX;
	if (in_tx_cp) {
		/*
		 * HSW_IN_TX_CHECKPOINTED is not supported with nonzero
		 * period. Just clear the sample period so at least
		 * allocating the counter doesn't fail.
		 */
		attr.sample_period = 0;
		attr.config |= HSW_IN_TX_CHECKPOINTED;
	}

	event = perf_event_create_kernel_counter(&attr, -1, current,
						 intr ? kvm_perf_overflow_intr :
						 kvm_perf_overflow, pmc);
	if (IS_ERR(event)) {
		pr_debug_ratelimited("kvm_pmu: event creation failed %ld for pmc->idx = %d\n",
			    PTR_ERR(event), pmc->idx);
		return;
	}

	pmc->perf_event = event;
	pmc_to_pmu(pmc)->event_count++;
	clear_bit(pmc->idx, pmc_to_pmu(pmc)->reprogram_pmi);
}

static void pmc_pause_counter(struct kvm_pmc *pmc)
{
	u64 counter = pmc->counter;

	if (!pmc->perf_event)
		return;

	/* update counter, reset event value to avoid redundant accumulation */
	counter += perf_event_pause(pmc->perf_event, true);
	pmc->counter = counter & pmc_bitmask(pmc);
}

static bool pmc_resume_counter(struct kvm_pmc *pmc)
{
	if (!pmc->perf_event)
		return false;

	/* recalibrate sample period and check if it's accepted by perf core */
	if (perf_event_period(pmc->perf_event,
			      get_sample_period(pmc, pmc->counter)))
		return false;

	/* reuse perf_event to serve as pmc_reprogram_counter() does*/
	perf_event_enable(pmc->perf_event);

	clear_bit(pmc->idx, (unsigned long *)&pmc_to_pmu(pmc)->reprogram_pmi);
	return true;
}

void reprogram_gp_counter(struct kvm_pmc *pmc, u64 eventsel)
{
	u64 config;
	u32 type = PERF_TYPE_RAW;
	struct kvm *kvm = pmc->vcpu->kvm;
	struct kvm_pmu_event_filter *filter;
	int i;
	bool allow_event = true;

	if (eventsel & ARCH_PERFMON_EVENTSEL_PIN_CONTROL)
		printk_once("kvm pmu: pin control bit is ignored\n");

	pmc->eventsel = eventsel;

	pmc_pause_counter(pmc);

	if (!(eventsel & ARCH_PERFMON_EVENTSEL_ENABLE) || !pmc_is_enabled(pmc))
		return;

	filter = srcu_dereference(kvm->arch.pmu_event_filter, &kvm->srcu);
	if (filter) {
		for (i = 0; i < filter->nevents; i++)
			if (filter->events[i] ==
			    (eventsel & AMD64_RAW_EVENT_MASK_NB))
				break;
		if (filter->action == KVM_PMU_EVENT_ALLOW &&
		    i == filter->nevents)
			allow_event = false;
		if (filter->action == KVM_PMU_EVENT_DENY &&
		    i < filter->nevents)
			allow_event = false;
	}
	if (!allow_event)
		return;

	if (!(eventsel & (ARCH_PERFMON_EVENTSEL_EDGE |
			  ARCH_PERFMON_EVENTSEL_INV |
			  ARCH_PERFMON_EVENTSEL_CMASK |
			  HSW_IN_TX |
			  HSW_IN_TX_CHECKPOINTED))) {
		config = kvm_x86_ops.pmu_ops->pmc_perf_hw_id(pmc);
		if (config != PERF_COUNT_HW_MAX)
			type = PERF_TYPE_HARDWARE;
	}

	if (type == PERF_TYPE_RAW)
		config = eventsel & AMD64_RAW_EVENT_MASK;

	if (pmc->current_config == eventsel && pmc_resume_counter(pmc))
		return;

	pmc_release_perf_event(pmc);

	pmc->current_config = eventsel;
	pmc_reprogram_counter(pmc, type, config,
			      !(eventsel & ARCH_PERFMON_EVENTSEL_USR),
			      !(eventsel & ARCH_PERFMON_EVENTSEL_OS),
			      eventsel & ARCH_PERFMON_EVENTSEL_INT,
			      (eventsel & HSW_IN_TX),
			      (eventsel & HSW_IN_TX_CHECKPOINTED));
}
EXPORT_SYMBOL_GPL(reprogram_gp_counter);

void reprogram_fixed_counter(struct kvm_pmc *pmc, u8 ctrl, int idx)
{
	unsigned en_field = ctrl & 0x3;
	bool pmi = ctrl & 0x8;
	struct kvm_pmu_event_filter *filter;
	struct kvm *kvm = pmc->vcpu->kvm;

	pmc_pause_counter(pmc);

	if (!en_field || !pmc_is_enabled(pmc))
		return;

	filter = srcu_dereference(kvm->arch.pmu_event_filter, &kvm->srcu);
	if (filter) {
		if (filter->action == KVM_PMU_EVENT_DENY &&
		    test_bit(idx, (ulong *)&filter->fixed_counter_bitmap))
			return;
		if (filter->action == KVM_PMU_EVENT_ALLOW &&
		    !test_bit(idx, (ulong *)&filter->fixed_counter_bitmap))
			return;
	}

	if (pmc->current_config == (u64)ctrl && pmc_resume_counter(pmc))
		return;

	pmc_release_perf_event(pmc);

	pmc->current_config = (u64)ctrl;
	pmc_reprogram_counter(pmc, PERF_TYPE_HARDWARE,
			      kvm_x86_ops.pmu_ops->find_fixed_event(idx),
			      !(en_field & 0x2), /* exclude user */
			      !(en_field & 0x1), /* exclude kernel */
			      pmi, false, false);
}
EXPORT_SYMBOL_GPL(reprogram_fixed_counter);

void reprogram_counter(struct kvm_pmu *pmu, int pmc_idx)
{
	struct kvm_pmc *pmc = kvm_x86_ops.pmu_ops->pmc_idx_to_pmc(pmu, pmc_idx);

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
EXPORT_SYMBOL_GPL(reprogram_counter);

void kvm_pmu_handle_event(struct kvm_vcpu *vcpu)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	int bit;

	for_each_set_bit(bit, pmu->reprogram_pmi, X86_PMC_IDX_MAX) {
		struct kvm_pmc *pmc = kvm_x86_ops.pmu_ops->pmc_idx_to_pmc(pmu, bit);

		if (unlikely(!pmc || !pmc->perf_event)) {
			clear_bit(bit, pmu->reprogram_pmi);
			continue;
		}

		reprogram_counter(pmu, bit);
	}

	/*
	 * Unused perf_events are only released if the corresponding MSRs
	 * weren't accessed during the last vCPU time slice. kvm_arch_sched_in
	 * triggers KVM_REQ_PMU if cleanup is needed.
	 */
	if (unlikely(pmu->need_cleanup))
		kvm_pmu_cleanup(vcpu);
}

/* check if idx is a valid index to access PMU */
int kvm_pmu_is_valid_rdpmc_ecx(struct kvm_vcpu *vcpu, unsigned int idx)
{
	return kvm_x86_ops.pmu_ops->is_valid_rdpmc_ecx(vcpu, idx);
}

bool is_vmware_backdoor_pmc(u32 pmc_idx)
{
	switch (pmc_idx) {
	case VMWARE_BACKDOOR_PMC_HOST_TSC:
	case VMWARE_BACKDOOR_PMC_REAL_TIME:
	case VMWARE_BACKDOOR_PMC_APPARENT_TIME:
		return true;
	}
	return false;
}

static int kvm_pmu_rdpmc_vmware(struct kvm_vcpu *vcpu, unsigned idx, u64 *data)
{
	u64 ctr_val;

	switch (idx) {
	case VMWARE_BACKDOOR_PMC_HOST_TSC:
		ctr_val = rdtsc();
		break;
	case VMWARE_BACKDOOR_PMC_REAL_TIME:
		ctr_val = ktime_get_boottime_ns();
		break;
	case VMWARE_BACKDOOR_PMC_APPARENT_TIME:
		ctr_val = ktime_get_boottime_ns() +
			vcpu->kvm->arch.kvmclock_offset;
		break;
	default:
		return 1;
	}

	*data = ctr_val;
	return 0;
}

int kvm_pmu_rdpmc(struct kvm_vcpu *vcpu, unsigned idx, u64 *data)
{
	bool fast_mode = idx & (1u << 31);
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	struct kvm_pmc *pmc;
	u64 mask = fast_mode ? ~0u : ~0ull;

	if (!pmu->version)
		return 1;

	if (is_vmware_backdoor_pmc(idx))
		return kvm_pmu_rdpmc_vmware(vcpu, idx, data);

	pmc = kvm_x86_ops.pmu_ops->rdpmc_ecx_to_pmc(vcpu, idx, &mask);
	if (!pmc)
		return 1;

	if (!(kvm_read_cr4(vcpu) & X86_CR4_PCE) &&
	    (kvm_x86_ops.get_cpl(vcpu) != 0) &&
	    (kvm_read_cr0(vcpu) & X86_CR0_PE))
		return 1;

	*data = pmc_read_counter(pmc) & mask;
	return 0;
}

void kvm_pmu_deliver_pmi(struct kvm_vcpu *vcpu)
{
	if (lapic_in_kernel(vcpu))
		kvm_apic_local_deliver(vcpu->arch.apic, APIC_LVTPC);
}

bool kvm_pmu_is_valid_msr(struct kvm_vcpu *vcpu, u32 msr)
{
	return kvm_x86_ops.pmu_ops->msr_idx_to_pmc(vcpu, msr) ||
		kvm_x86_ops.pmu_ops->is_valid_msr(vcpu, msr);
}

static void kvm_pmu_mark_pmc_in_use(struct kvm_vcpu *vcpu, u32 msr)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	struct kvm_pmc *pmc = kvm_x86_ops.pmu_ops->msr_idx_to_pmc(vcpu, msr);

	if (pmc)
		__set_bit(pmc->idx, pmu->pmc_in_use);
}

int kvm_pmu_get_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	return kvm_x86_ops.pmu_ops->get_msr(vcpu, msr_info);
}

int kvm_pmu_set_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	kvm_pmu_mark_pmc_in_use(vcpu, msr_info->index);
	return kvm_x86_ops.pmu_ops->set_msr(vcpu, msr_info);
}

/* refresh PMU settings. This function generally is called when underlying
 * settings are changed (such as changes of PMU CPUID by guest VMs), which
 * should rarely happen.
 */
void kvm_pmu_refresh(struct kvm_vcpu *vcpu)
{
	kvm_x86_ops.pmu_ops->refresh(vcpu);
}

void kvm_pmu_reset(struct kvm_vcpu *vcpu)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);

	irq_work_sync(&pmu->irq_work);
	kvm_x86_ops.pmu_ops->reset(vcpu);
}

void kvm_pmu_init(struct kvm_vcpu *vcpu)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);

	memset(pmu, 0, sizeof(*pmu));
	kvm_x86_ops.pmu_ops->init(vcpu);
	init_irq_work(&pmu->irq_work, kvm_pmi_trigger_fn);
	pmu->event_count = 0;
	pmu->need_cleanup = false;
	kvm_pmu_refresh(vcpu);
}

static inline bool pmc_speculative_in_use(struct kvm_pmc *pmc)
{
	struct kvm_pmu *pmu = pmc_to_pmu(pmc);

	if (pmc_is_fixed(pmc))
		return fixed_ctrl_field(pmu->fixed_ctr_ctrl,
			pmc->idx - INTEL_PMC_IDX_FIXED) & 0x3;

	return pmc->eventsel & ARCH_PERFMON_EVENTSEL_ENABLE;
}

/* Release perf_events for vPMCs that have been unused for a full time slice.  */
void kvm_pmu_cleanup(struct kvm_vcpu *vcpu)
{
	struct kvm_pmu *pmu = vcpu_to_pmu(vcpu);
	struct kvm_pmc *pmc = NULL;
	DECLARE_BITMAP(bitmask, X86_PMC_IDX_MAX);
	int i;

	pmu->need_cleanup = false;

	bitmap_andnot(bitmask, pmu->all_valid_pmc_idx,
		      pmu->pmc_in_use, X86_PMC_IDX_MAX);

	for_each_set_bit(i, bitmask, X86_PMC_IDX_MAX) {
		pmc = kvm_x86_ops.pmu_ops->pmc_idx_to_pmc(pmu, i);

		if (pmc && pmc->perf_event && !pmc_speculative_in_use(pmc))
			pmc_stop_counter(pmc);
	}

	bitmap_zero(pmu->pmc_in_use, X86_PMC_IDX_MAX);
}

void kvm_pmu_destroy(struct kvm_vcpu *vcpu)
{
	kvm_pmu_reset(vcpu);
}

int kvm_vm_ioctl_set_pmu_event_filter(struct kvm *kvm, void __user *argp)
{
	struct kvm_pmu_event_filter tmp, *filter;
	size_t size;
	int r;

	if (copy_from_user(&tmp, argp, sizeof(tmp)))
		return -EFAULT;

	if (tmp.action != KVM_PMU_EVENT_ALLOW &&
	    tmp.action != KVM_PMU_EVENT_DENY)
		return -EINVAL;

	if (tmp.flags != 0)
		return -EINVAL;

	if (tmp.nevents > KVM_PMU_EVENT_FILTER_MAX_EVENTS)
		return -E2BIG;

	size = struct_size(filter, events, tmp.nevents);
	filter = kmalloc(size, GFP_KERNEL_ACCOUNT);
	if (!filter)
		return -ENOMEM;

	r = -EFAULT;
	if (copy_from_user(filter, argp, size))
		goto cleanup;

	/* Ensure nevents can't be changed between the user copies. */
	*filter = tmp;

	mutex_lock(&kvm->lock);
	filter = rcu_replace_pointer(kvm->arch.pmu_event_filter, filter,
				     mutex_is_locked(&kvm->lock));
	mutex_unlock(&kvm->lock);

	synchronize_srcu_expedited(&kvm->srcu);
	r = 0;
cleanup:
	kfree(filter);
	return r;
}
