// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Linaro Ltd.
 * Author: Shannon Zhao <shannon.zhao@linaro.org>
 */

#include <linux/cpu.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/list.h>
#include <linux/perf_event.h>
#include <linux/perf/arm_pmu.h>
#include <linux/uaccess.h>
#include <asm/kvm_emulate.h>
#include <kvm/arm_pmu.h>
#include <kvm/arm_vgic.h>

#define PERF_ATTR_CFG1_COUNTER_64BIT	BIT(0)

DEFINE_STATIC_KEY_FALSE(kvm_arm_pmu_available);

static LIST_HEAD(arm_pmus);
static DEFINE_MUTEX(arm_pmus_lock);

static void kvm_pmu_create_perf_event(struct kvm_pmc *pmc);
static void kvm_pmu_release_perf_event(struct kvm_pmc *pmc);

static struct kvm_vcpu *kvm_pmc_to_vcpu(const struct kvm_pmc *pmc)
{
	return container_of(pmc, struct kvm_vcpu, arch.pmu.pmc[pmc->idx]);
}

static struct kvm_pmc *kvm_vcpu_idx_to_pmc(struct kvm_vcpu *vcpu, int cnt_idx)
{
	return &vcpu->arch.pmu.pmc[cnt_idx];
}

static u32 __kvm_pmu_event_mask(unsigned int pmuver)
{
	switch (pmuver) {
	case ID_AA64DFR0_EL1_PMUVer_IMP:
		return GENMASK(9, 0);
	case ID_AA64DFR0_EL1_PMUVer_V3P1:
	case ID_AA64DFR0_EL1_PMUVer_V3P4:
	case ID_AA64DFR0_EL1_PMUVer_V3P5:
	case ID_AA64DFR0_EL1_PMUVer_V3P7:
		return GENMASK(15, 0);
	default:		/* Shouldn't be here, just for sanity */
		WARN_ONCE(1, "Unknown PMU version %d\n", pmuver);
		return 0;
	}
}

static u32 kvm_pmu_event_mask(struct kvm *kvm)
{
	u64 dfr0 = kvm_read_vm_id_reg(kvm, SYS_ID_AA64DFR0_EL1);
	u8 pmuver = SYS_FIELD_GET(ID_AA64DFR0_EL1, PMUVer, dfr0);

	return __kvm_pmu_event_mask(pmuver);
}

u64 kvm_pmu_evtyper_mask(struct kvm *kvm)
{
	u64 mask = ARMV8_PMU_EXCLUDE_EL1 | ARMV8_PMU_EXCLUDE_EL0 |
		   kvm_pmu_event_mask(kvm);

	if (kvm_has_feat(kvm, ID_AA64PFR0_EL1, EL2, IMP))
		mask |= ARMV8_PMU_INCLUDE_EL2;

	if (kvm_has_feat(kvm, ID_AA64PFR0_EL1, EL3, IMP))
		mask |= ARMV8_PMU_EXCLUDE_NS_EL0 |
			ARMV8_PMU_EXCLUDE_NS_EL1 |
			ARMV8_PMU_EXCLUDE_EL3;

	return mask;
}

/**
 * kvm_pmc_is_64bit - determine if counter is 64bit
 * @pmc: counter context
 */
static bool kvm_pmc_is_64bit(struct kvm_pmc *pmc)
{
	struct kvm_vcpu *vcpu = kvm_pmc_to_vcpu(pmc);

	return (pmc->idx == ARMV8_PMU_CYCLE_IDX ||
		kvm_has_feat(vcpu->kvm, ID_AA64DFR0_EL1, PMUVer, V3P5));
}

static bool kvm_pmc_has_64bit_overflow(struct kvm_pmc *pmc)
{
	u64 val = kvm_vcpu_read_pmcr(kvm_pmc_to_vcpu(pmc));

	return (pmc->idx < ARMV8_PMU_CYCLE_IDX && (val & ARMV8_PMU_PMCR_LP)) ||
	       (pmc->idx == ARMV8_PMU_CYCLE_IDX && (val & ARMV8_PMU_PMCR_LC));
}

static bool kvm_pmu_counter_can_chain(struct kvm_pmc *pmc)
{
	return (!(pmc->idx & 1) && (pmc->idx + 1) < ARMV8_PMU_CYCLE_IDX &&
		!kvm_pmc_has_64bit_overflow(pmc));
}

static u32 counter_index_to_reg(u64 idx)
{
	return (idx == ARMV8_PMU_CYCLE_IDX) ? PMCCNTR_EL0 : PMEVCNTR0_EL0 + idx;
}

static u32 counter_index_to_evtreg(u64 idx)
{
	return (idx == ARMV8_PMU_CYCLE_IDX) ? PMCCFILTR_EL0 : PMEVTYPER0_EL0 + idx;
}

static u64 kvm_pmu_get_pmc_value(struct kvm_pmc *pmc)
{
	struct kvm_vcpu *vcpu = kvm_pmc_to_vcpu(pmc);
	u64 counter, reg, enabled, running;

	reg = counter_index_to_reg(pmc->idx);
	counter = __vcpu_sys_reg(vcpu, reg);

	/*
	 * The real counter value is equal to the value of counter register plus
	 * the value perf event counts.
	 */
	if (pmc->perf_event)
		counter += perf_event_read_value(pmc->perf_event, &enabled,
						 &running);

	if (!kvm_pmc_is_64bit(pmc))
		counter = lower_32_bits(counter);

	return counter;
}

/**
 * kvm_pmu_get_counter_value - get PMU counter value
 * @vcpu: The vcpu pointer
 * @select_idx: The counter index
 */
u64 kvm_pmu_get_counter_value(struct kvm_vcpu *vcpu, u64 select_idx)
{
	if (!kvm_vcpu_has_pmu(vcpu))
		return 0;

	return kvm_pmu_get_pmc_value(kvm_vcpu_idx_to_pmc(vcpu, select_idx));
}

static void kvm_pmu_set_pmc_value(struct kvm_pmc *pmc, u64 val, bool force)
{
	struct kvm_vcpu *vcpu = kvm_pmc_to_vcpu(pmc);
	u64 reg;

	kvm_pmu_release_perf_event(pmc);

	reg = counter_index_to_reg(pmc->idx);

	if (vcpu_mode_is_32bit(vcpu) && pmc->idx != ARMV8_PMU_CYCLE_IDX &&
	    !force) {
		/*
		 * Even with PMUv3p5, AArch32 cannot write to the top
		 * 32bit of the counters. The only possible course of
		 * action is to use PMCR.P, which will reset them to
		 * 0 (the only use of the 'force' parameter).
		 */
		val  = __vcpu_sys_reg(vcpu, reg) & GENMASK(63, 32);
		val |= lower_32_bits(val);
	}

	__vcpu_sys_reg(vcpu, reg) = val;

	/* Recreate the perf event to reflect the updated sample_period */
	kvm_pmu_create_perf_event(pmc);
}

/**
 * kvm_pmu_set_counter_value - set PMU counter value
 * @vcpu: The vcpu pointer
 * @select_idx: The counter index
 * @val: The counter value
 */
void kvm_pmu_set_counter_value(struct kvm_vcpu *vcpu, u64 select_idx, u64 val)
{
	if (!kvm_vcpu_has_pmu(vcpu))
		return;

	kvm_pmu_set_pmc_value(kvm_vcpu_idx_to_pmc(vcpu, select_idx), val, false);
}

/**
 * kvm_pmu_release_perf_event - remove the perf event
 * @pmc: The PMU counter pointer
 */
static void kvm_pmu_release_perf_event(struct kvm_pmc *pmc)
{
	if (pmc->perf_event) {
		perf_event_disable(pmc->perf_event);
		perf_event_release_kernel(pmc->perf_event);
		pmc->perf_event = NULL;
	}
}

/**
 * kvm_pmu_stop_counter - stop PMU counter
 * @pmc: The PMU counter pointer
 *
 * If this counter has been configured to monitor some event, release it here.
 */
static void kvm_pmu_stop_counter(struct kvm_pmc *pmc)
{
	struct kvm_vcpu *vcpu = kvm_pmc_to_vcpu(pmc);
	u64 reg, val;

	if (!pmc->perf_event)
		return;

	val = kvm_pmu_get_pmc_value(pmc);

	reg = counter_index_to_reg(pmc->idx);

	__vcpu_sys_reg(vcpu, reg) = val;

	kvm_pmu_release_perf_event(pmc);
}

/**
 * kvm_pmu_vcpu_init - assign pmu counter idx for cpu
 * @vcpu: The vcpu pointer
 *
 */
void kvm_pmu_vcpu_init(struct kvm_vcpu *vcpu)
{
	int i;
	struct kvm_pmu *pmu = &vcpu->arch.pmu;

	for (i = 0; i < ARMV8_PMU_MAX_COUNTERS; i++)
		pmu->pmc[i].idx = i;
}

/**
 * kvm_pmu_vcpu_reset - reset pmu state for cpu
 * @vcpu: The vcpu pointer
 *
 */
void kvm_pmu_vcpu_reset(struct kvm_vcpu *vcpu)
{
	unsigned long mask = kvm_pmu_valid_counter_mask(vcpu);
	int i;

	for_each_set_bit(i, &mask, 32)
		kvm_pmu_stop_counter(kvm_vcpu_idx_to_pmc(vcpu, i));
}

/**
 * kvm_pmu_vcpu_destroy - free perf event of PMU for cpu
 * @vcpu: The vcpu pointer
 *
 */
void kvm_pmu_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	int i;

	for (i = 0; i < ARMV8_PMU_MAX_COUNTERS; i++)
		kvm_pmu_release_perf_event(kvm_vcpu_idx_to_pmc(vcpu, i));
	irq_work_sync(&vcpu->arch.pmu.overflow_work);
}

u64 kvm_pmu_valid_counter_mask(struct kvm_vcpu *vcpu)
{
	u64 val = FIELD_GET(ARMV8_PMU_PMCR_N, kvm_vcpu_read_pmcr(vcpu));

	if (val == 0)
		return BIT(ARMV8_PMU_CYCLE_IDX);
	else
		return GENMASK(val - 1, 0) | BIT(ARMV8_PMU_CYCLE_IDX);
}

/**
 * kvm_pmu_enable_counter_mask - enable selected PMU counters
 * @vcpu: The vcpu pointer
 * @val: the value guest writes to PMCNTENSET register
 *
 * Call perf_event_enable to start counting the perf event
 */
void kvm_pmu_enable_counter_mask(struct kvm_vcpu *vcpu, u64 val)
{
	int i;
	if (!kvm_vcpu_has_pmu(vcpu))
		return;

	if (!(kvm_vcpu_read_pmcr(vcpu) & ARMV8_PMU_PMCR_E) || !val)
		return;

	for (i = 0; i < ARMV8_PMU_MAX_COUNTERS; i++) {
		struct kvm_pmc *pmc;

		if (!(val & BIT(i)))
			continue;

		pmc = kvm_vcpu_idx_to_pmc(vcpu, i);

		if (!pmc->perf_event) {
			kvm_pmu_create_perf_event(pmc);
		} else {
			perf_event_enable(pmc->perf_event);
			if (pmc->perf_event->state != PERF_EVENT_STATE_ACTIVE)
				kvm_debug("fail to enable perf event\n");
		}
	}
}

/**
 * kvm_pmu_disable_counter_mask - disable selected PMU counters
 * @vcpu: The vcpu pointer
 * @val: the value guest writes to PMCNTENCLR register
 *
 * Call perf_event_disable to stop counting the perf event
 */
void kvm_pmu_disable_counter_mask(struct kvm_vcpu *vcpu, u64 val)
{
	int i;

	if (!kvm_vcpu_has_pmu(vcpu) || !val)
		return;

	for (i = 0; i < ARMV8_PMU_MAX_COUNTERS; i++) {
		struct kvm_pmc *pmc;

		if (!(val & BIT(i)))
			continue;

		pmc = kvm_vcpu_idx_to_pmc(vcpu, i);

		if (pmc->perf_event)
			perf_event_disable(pmc->perf_event);
	}
}

static u64 kvm_pmu_overflow_status(struct kvm_vcpu *vcpu)
{
	u64 reg = 0;

	if ((kvm_vcpu_read_pmcr(vcpu) & ARMV8_PMU_PMCR_E)) {
		reg = __vcpu_sys_reg(vcpu, PMOVSSET_EL0);
		reg &= __vcpu_sys_reg(vcpu, PMCNTENSET_EL0);
		reg &= __vcpu_sys_reg(vcpu, PMINTENSET_EL1);
	}

	return reg;
}

static void kvm_pmu_update_state(struct kvm_vcpu *vcpu)
{
	struct kvm_pmu *pmu = &vcpu->arch.pmu;
	bool overflow;

	if (!kvm_vcpu_has_pmu(vcpu))
		return;

	overflow = !!kvm_pmu_overflow_status(vcpu);
	if (pmu->irq_level == overflow)
		return;

	pmu->irq_level = overflow;

	if (likely(irqchip_in_kernel(vcpu->kvm))) {
		int ret = kvm_vgic_inject_irq(vcpu->kvm, vcpu,
					      pmu->irq_num, overflow, pmu);
		WARN_ON(ret);
	}
}

bool kvm_pmu_should_notify_user(struct kvm_vcpu *vcpu)
{
	struct kvm_pmu *pmu = &vcpu->arch.pmu;
	struct kvm_sync_regs *sregs = &vcpu->run->s.regs;
	bool run_level = sregs->device_irq_level & KVM_ARM_DEV_PMU;

	if (likely(irqchip_in_kernel(vcpu->kvm)))
		return false;

	return pmu->irq_level != run_level;
}

/*
 * Reflect the PMU overflow interrupt output level into the kvm_run structure
 */
void kvm_pmu_update_run(struct kvm_vcpu *vcpu)
{
	struct kvm_sync_regs *regs = &vcpu->run->s.regs;

	/* Populate the timer bitmap for user space */
	regs->device_irq_level &= ~KVM_ARM_DEV_PMU;
	if (vcpu->arch.pmu.irq_level)
		regs->device_irq_level |= KVM_ARM_DEV_PMU;
}

/**
 * kvm_pmu_flush_hwstate - flush pmu state to cpu
 * @vcpu: The vcpu pointer
 *
 * Check if the PMU has overflowed while we were running in the host, and inject
 * an interrupt if that was the case.
 */
void kvm_pmu_flush_hwstate(struct kvm_vcpu *vcpu)
{
	kvm_pmu_update_state(vcpu);
}

/**
 * kvm_pmu_sync_hwstate - sync pmu state from cpu
 * @vcpu: The vcpu pointer
 *
 * Check if the PMU has overflowed while we were running in the guest, and
 * inject an interrupt if that was the case.
 */
void kvm_pmu_sync_hwstate(struct kvm_vcpu *vcpu)
{
	kvm_pmu_update_state(vcpu);
}

/*
 * When perf interrupt is an NMI, we cannot safely notify the vcpu corresponding
 * to the event.
 * This is why we need a callback to do it once outside of the NMI context.
 */
static void kvm_pmu_perf_overflow_notify_vcpu(struct irq_work *work)
{
	struct kvm_vcpu *vcpu;

	vcpu = container_of(work, struct kvm_vcpu, arch.pmu.overflow_work);
	kvm_vcpu_kick(vcpu);
}

/*
 * Perform an increment on any of the counters described in @mask,
 * generating the overflow if required, and propagate it as a chained
 * event if possible.
 */
static void kvm_pmu_counter_increment(struct kvm_vcpu *vcpu,
				      unsigned long mask, u32 event)
{
	int i;

	if (!(kvm_vcpu_read_pmcr(vcpu) & ARMV8_PMU_PMCR_E))
		return;

	/* Weed out disabled counters */
	mask &= __vcpu_sys_reg(vcpu, PMCNTENSET_EL0);

	for_each_set_bit(i, &mask, ARMV8_PMU_CYCLE_IDX) {
		struct kvm_pmc *pmc = kvm_vcpu_idx_to_pmc(vcpu, i);
		u64 type, reg;

		/* Filter on event type */
		type = __vcpu_sys_reg(vcpu, counter_index_to_evtreg(i));
		type &= kvm_pmu_event_mask(vcpu->kvm);
		if (type != event)
			continue;

		/* Increment this counter */
		reg = __vcpu_sys_reg(vcpu, counter_index_to_reg(i)) + 1;
		if (!kvm_pmc_is_64bit(pmc))
			reg = lower_32_bits(reg);
		__vcpu_sys_reg(vcpu, counter_index_to_reg(i)) = reg;

		/* No overflow? move on */
		if (kvm_pmc_has_64bit_overflow(pmc) ? reg : lower_32_bits(reg))
			continue;

		/* Mark overflow */
		__vcpu_sys_reg(vcpu, PMOVSSET_EL0) |= BIT(i);

		if (kvm_pmu_counter_can_chain(pmc))
			kvm_pmu_counter_increment(vcpu, BIT(i + 1),
						  ARMV8_PMUV3_PERFCTR_CHAIN);
	}
}

/* Compute the sample period for a given counter value */
static u64 compute_period(struct kvm_pmc *pmc, u64 counter)
{
	u64 val;

	if (kvm_pmc_is_64bit(pmc) && kvm_pmc_has_64bit_overflow(pmc))
		val = (-counter) & GENMASK(63, 0);
	else
		val = (-counter) & GENMASK(31, 0);

	return val;
}

/*
 * When the perf event overflows, set the overflow status and inform the vcpu.
 */
static void kvm_pmu_perf_overflow(struct perf_event *perf_event,
				  struct perf_sample_data *data,
				  struct pt_regs *regs)
{
	struct kvm_pmc *pmc = perf_event->overflow_handler_context;
	struct arm_pmu *cpu_pmu = to_arm_pmu(perf_event->pmu);
	struct kvm_vcpu *vcpu = kvm_pmc_to_vcpu(pmc);
	int idx = pmc->idx;
	u64 period;

	cpu_pmu->pmu.stop(perf_event, PERF_EF_UPDATE);

	/*
	 * Reset the sample period to the architectural limit,
	 * i.e. the point where the counter overflows.
	 */
	period = compute_period(pmc, local64_read(&perf_event->count));

	local64_set(&perf_event->hw.period_left, 0);
	perf_event->attr.sample_period = period;
	perf_event->hw.sample_period = period;

	__vcpu_sys_reg(vcpu, PMOVSSET_EL0) |= BIT(idx);

	if (kvm_pmu_counter_can_chain(pmc))
		kvm_pmu_counter_increment(vcpu, BIT(idx + 1),
					  ARMV8_PMUV3_PERFCTR_CHAIN);

	if (kvm_pmu_overflow_status(vcpu)) {
		kvm_make_request(KVM_REQ_IRQ_PENDING, vcpu);

		if (!in_nmi())
			kvm_vcpu_kick(vcpu);
		else
			irq_work_queue(&vcpu->arch.pmu.overflow_work);
	}

	cpu_pmu->pmu.start(perf_event, PERF_EF_RELOAD);
}

/**
 * kvm_pmu_software_increment - do software increment
 * @vcpu: The vcpu pointer
 * @val: the value guest writes to PMSWINC register
 */
void kvm_pmu_software_increment(struct kvm_vcpu *vcpu, u64 val)
{
	kvm_pmu_counter_increment(vcpu, val, ARMV8_PMUV3_PERFCTR_SW_INCR);
}

/**
 * kvm_pmu_handle_pmcr - handle PMCR register
 * @vcpu: The vcpu pointer
 * @val: the value guest writes to PMCR register
 */
void kvm_pmu_handle_pmcr(struct kvm_vcpu *vcpu, u64 val)
{
	int i;

	if (!kvm_vcpu_has_pmu(vcpu))
		return;

	/* Fixup PMCR_EL0 to reconcile the PMU version and the LP bit */
	if (!kvm_has_feat(vcpu->kvm, ID_AA64DFR0_EL1, PMUVer, V3P5))
		val &= ~ARMV8_PMU_PMCR_LP;

	/* The reset bits don't indicate any state, and shouldn't be saved. */
	__vcpu_sys_reg(vcpu, PMCR_EL0) = val & ~(ARMV8_PMU_PMCR_C | ARMV8_PMU_PMCR_P);

	if (val & ARMV8_PMU_PMCR_E) {
		kvm_pmu_enable_counter_mask(vcpu,
		       __vcpu_sys_reg(vcpu, PMCNTENSET_EL0));
	} else {
		kvm_pmu_disable_counter_mask(vcpu,
		       __vcpu_sys_reg(vcpu, PMCNTENSET_EL0));
	}

	if (val & ARMV8_PMU_PMCR_C)
		kvm_pmu_set_counter_value(vcpu, ARMV8_PMU_CYCLE_IDX, 0);

	if (val & ARMV8_PMU_PMCR_P) {
		unsigned long mask = kvm_pmu_valid_counter_mask(vcpu);
		mask &= ~BIT(ARMV8_PMU_CYCLE_IDX);
		for_each_set_bit(i, &mask, 32)
			kvm_pmu_set_pmc_value(kvm_vcpu_idx_to_pmc(vcpu, i), 0, true);
	}
	kvm_vcpu_pmu_restore_guest(vcpu);
}

static bool kvm_pmu_counter_is_enabled(struct kvm_pmc *pmc)
{
	struct kvm_vcpu *vcpu = kvm_pmc_to_vcpu(pmc);
	return (kvm_vcpu_read_pmcr(vcpu) & ARMV8_PMU_PMCR_E) &&
	       (__vcpu_sys_reg(vcpu, PMCNTENSET_EL0) & BIT(pmc->idx));
}

/**
 * kvm_pmu_create_perf_event - create a perf event for a counter
 * @pmc: Counter context
 */
static void kvm_pmu_create_perf_event(struct kvm_pmc *pmc)
{
	struct kvm_vcpu *vcpu = kvm_pmc_to_vcpu(pmc);
	struct arm_pmu *arm_pmu = vcpu->kvm->arch.arm_pmu;
	struct perf_event *event;
	struct perf_event_attr attr;
	u64 eventsel, reg, data;
	bool p, u, nsk, nsu;

	reg = counter_index_to_evtreg(pmc->idx);
	data = __vcpu_sys_reg(vcpu, reg);

	kvm_pmu_stop_counter(pmc);
	if (pmc->idx == ARMV8_PMU_CYCLE_IDX)
		eventsel = ARMV8_PMUV3_PERFCTR_CPU_CYCLES;
	else
		eventsel = data & kvm_pmu_event_mask(vcpu->kvm);

	/*
	 * Neither SW increment nor chained events need to be backed
	 * by a perf event.
	 */
	if (eventsel == ARMV8_PMUV3_PERFCTR_SW_INCR ||
	    eventsel == ARMV8_PMUV3_PERFCTR_CHAIN)
		return;

	/*
	 * If we have a filter in place and that the event isn't allowed, do
	 * not install a perf event either.
	 */
	if (vcpu->kvm->arch.pmu_filter &&
	    !test_bit(eventsel, vcpu->kvm->arch.pmu_filter))
		return;

	p = data & ARMV8_PMU_EXCLUDE_EL1;
	u = data & ARMV8_PMU_EXCLUDE_EL0;
	nsk = data & ARMV8_PMU_EXCLUDE_NS_EL1;
	nsu = data & ARMV8_PMU_EXCLUDE_NS_EL0;

	memset(&attr, 0, sizeof(struct perf_event_attr));
	attr.type = arm_pmu->pmu.type;
	attr.size = sizeof(attr);
	attr.pinned = 1;
	attr.disabled = !kvm_pmu_counter_is_enabled(pmc);
	attr.exclude_user = (u != nsu);
	attr.exclude_kernel = (p != nsk);
	attr.exclude_hv = 1; /* Don't count EL2 events */
	attr.exclude_host = 1; /* Don't count host events */
	attr.config = eventsel;

	/*
	 * If counting with a 64bit counter, advertise it to the perf
	 * code, carefully dealing with the initial sample period
	 * which also depends on the overflow.
	 */
	if (kvm_pmc_is_64bit(pmc))
		attr.config1 |= PERF_ATTR_CFG1_COUNTER_64BIT;

	attr.sample_period = compute_period(pmc, kvm_pmu_get_pmc_value(pmc));

	event = perf_event_create_kernel_counter(&attr, -1, current,
						 kvm_pmu_perf_overflow, pmc);

	if (IS_ERR(event)) {
		pr_err_once("kvm: pmu event creation failed %ld\n",
			    PTR_ERR(event));
		return;
	}

	pmc->perf_event = event;
}

/**
 * kvm_pmu_set_counter_event_type - set selected counter to monitor some event
 * @vcpu: The vcpu pointer
 * @data: The data guest writes to PMXEVTYPER_EL0
 * @select_idx: The number of selected counter
 *
 * When OS accesses PMXEVTYPER_EL0, that means it wants to set a PMC to count an
 * event with given hardware event number. Here we call perf_event API to
 * emulate this action and create a kernel perf event for it.
 */
void kvm_pmu_set_counter_event_type(struct kvm_vcpu *vcpu, u64 data,
				    u64 select_idx)
{
	struct kvm_pmc *pmc = kvm_vcpu_idx_to_pmc(vcpu, select_idx);
	u64 reg;

	if (!kvm_vcpu_has_pmu(vcpu))
		return;

	reg = counter_index_to_evtreg(pmc->idx);
	__vcpu_sys_reg(vcpu, reg) = data & kvm_pmu_evtyper_mask(vcpu->kvm);

	kvm_pmu_create_perf_event(pmc);
}

void kvm_host_pmu_init(struct arm_pmu *pmu)
{
	struct arm_pmu_entry *entry;

	/*
	 * Check the sanitised PMU version for the system, as KVM does not
	 * support implementations where PMUv3 exists on a subset of CPUs.
	 */
	if (!pmuv3_implemented(kvm_arm_pmu_get_pmuver_limit()))
		return;

	mutex_lock(&arm_pmus_lock);

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		goto out_unlock;

	entry->arm_pmu = pmu;
	list_add_tail(&entry->entry, &arm_pmus);

	if (list_is_singular(&arm_pmus))
		static_branch_enable(&kvm_arm_pmu_available);

out_unlock:
	mutex_unlock(&arm_pmus_lock);
}

static struct arm_pmu *kvm_pmu_probe_armpmu(void)
{
	struct arm_pmu *tmp, *pmu = NULL;
	struct arm_pmu_entry *entry;
	int cpu;

	mutex_lock(&arm_pmus_lock);

	/*
	 * It is safe to use a stale cpu to iterate the list of PMUs so long as
	 * the same value is used for the entirety of the loop. Given this, and
	 * the fact that no percpu data is used for the lookup there is no need
	 * to disable preemption.
	 *
	 * It is still necessary to get a valid cpu, though, to probe for the
	 * default PMU instance as userspace is not required to specify a PMU
	 * type. In order to uphold the preexisting behavior KVM selects the
	 * PMU instance for the core during vcpu init. A dependent use
	 * case would be a user with disdain of all things big.LITTLE that
	 * affines the VMM to a particular cluster of cores.
	 *
	 * In any case, userspace should just do the sane thing and use the UAPI
	 * to select a PMU type directly. But, be wary of the baggage being
	 * carried here.
	 */
	cpu = raw_smp_processor_id();
	list_for_each_entry(entry, &arm_pmus, entry) {
		tmp = entry->arm_pmu;

		if (cpumask_test_cpu(cpu, &tmp->supported_cpus)) {
			pmu = tmp;
			break;
		}
	}

	mutex_unlock(&arm_pmus_lock);

	return pmu;
}

u64 kvm_pmu_get_pmceid(struct kvm_vcpu *vcpu, bool pmceid1)
{
	unsigned long *bmap = vcpu->kvm->arch.pmu_filter;
	u64 val, mask = 0;
	int base, i, nr_events;

	if (!kvm_vcpu_has_pmu(vcpu))
		return 0;

	if (!pmceid1) {
		val = read_sysreg(pmceid0_el0);
		/* always support CHAIN */
		val |= BIT(ARMV8_PMUV3_PERFCTR_CHAIN);
		base = 0;
	} else {
		val = read_sysreg(pmceid1_el0);
		/*
		 * Don't advertise STALL_SLOT*, as PMMIR_EL0 is handled
		 * as RAZ
		 */
		val &= ~(BIT_ULL(ARMV8_PMUV3_PERFCTR_STALL_SLOT - 32) |
			 BIT_ULL(ARMV8_PMUV3_PERFCTR_STALL_SLOT_FRONTEND - 32) |
			 BIT_ULL(ARMV8_PMUV3_PERFCTR_STALL_SLOT_BACKEND - 32));
		base = 32;
	}

	if (!bmap)
		return val;

	nr_events = kvm_pmu_event_mask(vcpu->kvm) + 1;

	for (i = 0; i < 32; i += 8) {
		u64 byte;

		byte = bitmap_get_value8(bmap, base + i);
		mask |= byte << i;
		if (nr_events >= (0x4000 + base + 32)) {
			byte = bitmap_get_value8(bmap, 0x4000 + base + i);
			mask |= byte << (32 + i);
		}
	}

	return val & mask;
}

void kvm_vcpu_reload_pmu(struct kvm_vcpu *vcpu)
{
	u64 mask = kvm_pmu_valid_counter_mask(vcpu);

	kvm_pmu_handle_pmcr(vcpu, kvm_vcpu_read_pmcr(vcpu));

	__vcpu_sys_reg(vcpu, PMOVSSET_EL0) &= mask;
	__vcpu_sys_reg(vcpu, PMINTENSET_EL1) &= mask;
	__vcpu_sys_reg(vcpu, PMCNTENSET_EL0) &= mask;
}

int kvm_arm_pmu_v3_enable(struct kvm_vcpu *vcpu)
{
	if (!kvm_vcpu_has_pmu(vcpu))
		return 0;

	if (!vcpu->arch.pmu.created)
		return -EINVAL;

	/*
	 * A valid interrupt configuration for the PMU is either to have a
	 * properly configured interrupt number and using an in-kernel
	 * irqchip, or to not have an in-kernel GIC and not set an IRQ.
	 */
	if (irqchip_in_kernel(vcpu->kvm)) {
		int irq = vcpu->arch.pmu.irq_num;
		/*
		 * If we are using an in-kernel vgic, at this point we know
		 * the vgic will be initialized, so we can check the PMU irq
		 * number against the dimensions of the vgic and make sure
		 * it's valid.
		 */
		if (!irq_is_ppi(irq) && !vgic_valid_spi(vcpu->kvm, irq))
			return -EINVAL;
	} else if (kvm_arm_pmu_irq_initialized(vcpu)) {
		   return -EINVAL;
	}

	/* One-off reload of the PMU on first run */
	kvm_make_request(KVM_REQ_RELOAD_PMU, vcpu);

	return 0;
}

static int kvm_arm_pmu_v3_init(struct kvm_vcpu *vcpu)
{
	if (irqchip_in_kernel(vcpu->kvm)) {
		int ret;

		/*
		 * If using the PMU with an in-kernel virtual GIC
		 * implementation, we require the GIC to be already
		 * initialized when initializing the PMU.
		 */
		if (!vgic_initialized(vcpu->kvm))
			return -ENODEV;

		if (!kvm_arm_pmu_irq_initialized(vcpu))
			return -ENXIO;

		ret = kvm_vgic_set_owner(vcpu, vcpu->arch.pmu.irq_num,
					 &vcpu->arch.pmu);
		if (ret)
			return ret;
	}

	init_irq_work(&vcpu->arch.pmu.overflow_work,
		      kvm_pmu_perf_overflow_notify_vcpu);

	vcpu->arch.pmu.created = true;
	return 0;
}

/*
 * For one VM the interrupt type must be same for each vcpu.
 * As a PPI, the interrupt number is the same for all vcpus,
 * while as an SPI it must be a separate number per vcpu.
 */
static bool pmu_irq_is_valid(struct kvm *kvm, int irq)
{
	unsigned long i;
	struct kvm_vcpu *vcpu;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (!kvm_arm_pmu_irq_initialized(vcpu))
			continue;

		if (irq_is_ppi(irq)) {
			if (vcpu->arch.pmu.irq_num != irq)
				return false;
		} else {
			if (vcpu->arch.pmu.irq_num == irq)
				return false;
		}
	}

	return true;
}

/**
 * kvm_arm_pmu_get_max_counters - Return the max number of PMU counters.
 * @kvm: The kvm pointer
 */
u8 kvm_arm_pmu_get_max_counters(struct kvm *kvm)
{
	struct arm_pmu *arm_pmu = kvm->arch.arm_pmu;

	/*
	 * The arm_pmu->num_events considers the cycle counter as well.
	 * Ignore that and return only the general-purpose counters.
	 */
	return arm_pmu->num_events - 1;
}

static void kvm_arm_set_pmu(struct kvm *kvm, struct arm_pmu *arm_pmu)
{
	lockdep_assert_held(&kvm->arch.config_lock);

	kvm->arch.arm_pmu = arm_pmu;
	kvm->arch.pmcr_n = kvm_arm_pmu_get_max_counters(kvm);
}

/**
 * kvm_arm_set_default_pmu - No PMU set, get the default one.
 * @kvm: The kvm pointer
 *
 * The observant among you will notice that the supported_cpus
 * mask does not get updated for the default PMU even though it
 * is quite possible the selected instance supports only a
 * subset of cores in the system. This is intentional, and
 * upholds the preexisting behavior on heterogeneous systems
 * where vCPUs can be scheduled on any core but the guest
 * counters could stop working.
 */
int kvm_arm_set_default_pmu(struct kvm *kvm)
{
	struct arm_pmu *arm_pmu = kvm_pmu_probe_armpmu();

	if (!arm_pmu)
		return -ENODEV;

	kvm_arm_set_pmu(kvm, arm_pmu);
	return 0;
}

static int kvm_arm_pmu_v3_set_pmu(struct kvm_vcpu *vcpu, int pmu_id)
{
	struct kvm *kvm = vcpu->kvm;
	struct arm_pmu_entry *entry;
	struct arm_pmu *arm_pmu;
	int ret = -ENXIO;

	lockdep_assert_held(&kvm->arch.config_lock);
	mutex_lock(&arm_pmus_lock);

	list_for_each_entry(entry, &arm_pmus, entry) {
		arm_pmu = entry->arm_pmu;
		if (arm_pmu->pmu.type == pmu_id) {
			if (kvm_vm_has_ran_once(kvm) ||
			    (kvm->arch.pmu_filter && kvm->arch.arm_pmu != arm_pmu)) {
				ret = -EBUSY;
				break;
			}

			kvm_arm_set_pmu(kvm, arm_pmu);
			cpumask_copy(kvm->arch.supported_cpus, &arm_pmu->supported_cpus);
			ret = 0;
			break;
		}
	}

	mutex_unlock(&arm_pmus_lock);
	return ret;
}

int kvm_arm_pmu_v3_set_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr)
{
	struct kvm *kvm = vcpu->kvm;

	lockdep_assert_held(&kvm->arch.config_lock);

	if (!kvm_vcpu_has_pmu(vcpu))
		return -ENODEV;

	if (vcpu->arch.pmu.created)
		return -EBUSY;

	switch (attr->attr) {
	case KVM_ARM_VCPU_PMU_V3_IRQ: {
		int __user *uaddr = (int __user *)(long)attr->addr;
		int irq;

		if (!irqchip_in_kernel(kvm))
			return -EINVAL;

		if (get_user(irq, uaddr))
			return -EFAULT;

		/* The PMU overflow interrupt can be a PPI or a valid SPI. */
		if (!(irq_is_ppi(irq) || irq_is_spi(irq)))
			return -EINVAL;

		if (!pmu_irq_is_valid(kvm, irq))
			return -EINVAL;

		if (kvm_arm_pmu_irq_initialized(vcpu))
			return -EBUSY;

		kvm_debug("Set kvm ARM PMU irq: %d\n", irq);
		vcpu->arch.pmu.irq_num = irq;
		return 0;
	}
	case KVM_ARM_VCPU_PMU_V3_FILTER: {
		u8 pmuver = kvm_arm_pmu_get_pmuver_limit();
		struct kvm_pmu_event_filter __user *uaddr;
		struct kvm_pmu_event_filter filter;
		int nr_events;

		/*
		 * Allow userspace to specify an event filter for the entire
		 * event range supported by PMUVer of the hardware, rather
		 * than the guest's PMUVer for KVM backward compatibility.
		 */
		nr_events = __kvm_pmu_event_mask(pmuver) + 1;

		uaddr = (struct kvm_pmu_event_filter __user *)(long)attr->addr;

		if (copy_from_user(&filter, uaddr, sizeof(filter)))
			return -EFAULT;

		if (((u32)filter.base_event + filter.nevents) > nr_events ||
		    (filter.action != KVM_PMU_EVENT_ALLOW &&
		     filter.action != KVM_PMU_EVENT_DENY))
			return -EINVAL;

		if (kvm_vm_has_ran_once(kvm))
			return -EBUSY;

		if (!kvm->arch.pmu_filter) {
			kvm->arch.pmu_filter = bitmap_alloc(nr_events, GFP_KERNEL_ACCOUNT);
			if (!kvm->arch.pmu_filter)
				return -ENOMEM;

			/*
			 * The default depends on the first applied filter.
			 * If it allows events, the default is to deny.
			 * Conversely, if the first filter denies a set of
			 * events, the default is to allow.
			 */
			if (filter.action == KVM_PMU_EVENT_ALLOW)
				bitmap_zero(kvm->arch.pmu_filter, nr_events);
			else
				bitmap_fill(kvm->arch.pmu_filter, nr_events);
		}

		if (filter.action == KVM_PMU_EVENT_ALLOW)
			bitmap_set(kvm->arch.pmu_filter, filter.base_event, filter.nevents);
		else
			bitmap_clear(kvm->arch.pmu_filter, filter.base_event, filter.nevents);

		return 0;
	}
	case KVM_ARM_VCPU_PMU_V3_SET_PMU: {
		int __user *uaddr = (int __user *)(long)attr->addr;
		int pmu_id;

		if (get_user(pmu_id, uaddr))
			return -EFAULT;

		return kvm_arm_pmu_v3_set_pmu(vcpu, pmu_id);
	}
	case KVM_ARM_VCPU_PMU_V3_INIT:
		return kvm_arm_pmu_v3_init(vcpu);
	}

	return -ENXIO;
}

int kvm_arm_pmu_v3_get_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr)
{
	switch (attr->attr) {
	case KVM_ARM_VCPU_PMU_V3_IRQ: {
		int __user *uaddr = (int __user *)(long)attr->addr;
		int irq;

		if (!irqchip_in_kernel(vcpu->kvm))
			return -EINVAL;

		if (!kvm_vcpu_has_pmu(vcpu))
			return -ENODEV;

		if (!kvm_arm_pmu_irq_initialized(vcpu))
			return -ENXIO;

		irq = vcpu->arch.pmu.irq_num;
		return put_user(irq, uaddr);
	}
	}

	return -ENXIO;
}

int kvm_arm_pmu_v3_has_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr)
{
	switch (attr->attr) {
	case KVM_ARM_VCPU_PMU_V3_IRQ:
	case KVM_ARM_VCPU_PMU_V3_INIT:
	case KVM_ARM_VCPU_PMU_V3_FILTER:
	case KVM_ARM_VCPU_PMU_V3_SET_PMU:
		if (kvm_vcpu_has_pmu(vcpu))
			return 0;
	}

	return -ENXIO;
}

u8 kvm_arm_pmu_get_pmuver_limit(void)
{
	u64 tmp;

	tmp = read_sanitised_ftr_reg(SYS_ID_AA64DFR0_EL1);
	tmp = cpuid_feature_cap_perfmon_field(tmp,
					      ID_AA64DFR0_EL1_PMUVer_SHIFT,
					      ID_AA64DFR0_EL1_PMUVer_V3P5);
	return FIELD_GET(ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_PMUVer), tmp);
}

/**
 * kvm_vcpu_read_pmcr - Read PMCR_EL0 register for the vCPU
 * @vcpu: The vcpu pointer
 */
u64 kvm_vcpu_read_pmcr(struct kvm_vcpu *vcpu)
{
	u64 pmcr = __vcpu_sys_reg(vcpu, PMCR_EL0);

	return u64_replace_bits(pmcr, vcpu->kvm->arch.pmcr_n, ARMV8_PMU_PMCR_N);
}
