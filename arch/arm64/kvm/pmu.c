// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Arm Limited
 * Author: Andrew Murray <Andrew.Murray@arm.com>
 */
#include <linux/kvm_host.h>
#include <linux/perf_event.h>
#include <linux/perf/arm_pmu.h>
#include <linux/perf/arm_pmuv3.h>

static DEFINE_PER_CPU(struct kvm_pmu_events, kvm_pmu_events);

/*
 * Given the perf event attributes and system type, determine
 * if we are going to need to switch counters at guest entry/exit.
 */
static bool kvm_pmu_switch_needed(struct perf_event_attr *attr)
{
	/**
	 * With VHE the guest kernel runs at EL1 and the host at EL2,
	 * where user (EL0) is excluded then we have no reason to switch
	 * counters.
	 */
	if (has_vhe() && attr->exclude_user)
		return false;

	/* Only switch if attributes are different */
	return (attr->exclude_host != attr->exclude_guest);
}

struct kvm_pmu_events *kvm_get_pmu_events(void)
{
	return this_cpu_ptr(&kvm_pmu_events);
}

/*
 * Add events to track that we may want to switch at guest entry/exit
 * time.
 */
void kvm_set_pmu_events(u64 set, struct perf_event_attr *attr)
{
	struct kvm_pmu_events *pmu = kvm_get_pmu_events();

	if (!kvm_arm_support_pmu_v3() || !kvm_pmu_switch_needed(attr))
		return;

	if (!attr->exclude_host)
		pmu->events_host |= set;
	if (!attr->exclude_guest)
		pmu->events_guest |= set;
}

/*
 * Stop tracking events
 */
void kvm_clr_pmu_events(u64 clr)
{
	struct kvm_pmu_events *pmu = kvm_get_pmu_events();

	if (!kvm_arm_support_pmu_v3())
		return;

	pmu->events_host &= ~clr;
	pmu->events_guest &= ~clr;
}

/*
 * Read a value direct from PMEVTYPER<idx> where idx is 0-30
 * or PMxCFILTR_EL0 where idx is 31-32.
 */
static u64 kvm_vcpu_pmu_read_evtype_direct(int idx)
{
	if (idx == ARMV8_PMU_CYCLE_IDX)
		return read_pmccfiltr();
	else if (idx == ARMV8_PMU_INSTR_IDX)
		return read_pmicfiltr();

	return read_pmevtypern(idx);
}

/*
 * Write a value direct to PMEVTYPER<idx> where idx is 0-30
 * or PMxCFILTR_EL0 where idx is 31-32.
 */
static void kvm_vcpu_pmu_write_evtype_direct(int idx, u32 val)
{
	if (idx == ARMV8_PMU_CYCLE_IDX)
		write_pmccfiltr(val);
	else if (idx == ARMV8_PMU_INSTR_IDX)
		write_pmicfiltr(val);
	else
		write_pmevtypern(idx, val);
}

/*
 * Modify ARMv8 PMU events to include EL0 counting
 */
static void kvm_vcpu_pmu_enable_el0(unsigned long events)
{
	u64 typer;
	u32 counter;

	for_each_set_bit(counter, &events, ARMPMU_MAX_HWEVENTS) {
		typer = kvm_vcpu_pmu_read_evtype_direct(counter);
		typer &= ~ARMV8_PMU_EXCLUDE_EL0;
		kvm_vcpu_pmu_write_evtype_direct(counter, typer);
	}
}

/*
 * Modify ARMv8 PMU events to exclude EL0 counting
 */
static void kvm_vcpu_pmu_disable_el0(unsigned long events)
{
	u64 typer;
	u32 counter;

	for_each_set_bit(counter, &events, ARMPMU_MAX_HWEVENTS) {
		typer = kvm_vcpu_pmu_read_evtype_direct(counter);
		typer |= ARMV8_PMU_EXCLUDE_EL0;
		kvm_vcpu_pmu_write_evtype_direct(counter, typer);
	}
}

/*
 * On VHE ensure that only guest events have EL0 counting enabled.
 * This is called from both vcpu_{load,put} and the sysreg handling.
 * Since the latter is preemptible, special care must be taken to
 * disable preemption.
 */
void kvm_vcpu_pmu_restore_guest(struct kvm_vcpu *vcpu)
{
	struct kvm_pmu_events *pmu;
	u64 events_guest, events_host;

	if (!kvm_arm_support_pmu_v3() || !has_vhe())
		return;

	preempt_disable();
	pmu = kvm_get_pmu_events();
	events_guest = pmu->events_guest;
	events_host = pmu->events_host;

	kvm_vcpu_pmu_enable_el0(events_guest);
	kvm_vcpu_pmu_disable_el0(events_host);
	preempt_enable();
}

/*
 * On VHE ensure that only host events have EL0 counting enabled
 */
void kvm_vcpu_pmu_restore_host(struct kvm_vcpu *vcpu)
{
	struct kvm_pmu_events *pmu;
	u64 events_guest, events_host;

	if (!kvm_arm_support_pmu_v3() || !has_vhe())
		return;

	pmu = kvm_get_pmu_events();
	events_guest = pmu->events_guest;
	events_host = pmu->events_host;

	kvm_vcpu_pmu_enable_el0(events_host);
	kvm_vcpu_pmu_disable_el0(events_guest);
}

/*
 * With VHE, keep track of the PMUSERENR_EL0 value for the host EL0 on the pCPU
 * where PMUSERENR_EL0 for the guest is loaded, since PMUSERENR_EL0 is switched
 * to the value for the guest on vcpu_load().  The value for the host EL0
 * will be restored on vcpu_put(), before returning to userspace.
 * This isn't necessary for nVHE, as the register is context switched for
 * every guest enter/exit.
 *
 * Return true if KVM takes care of the register. Otherwise return false.
 */
bool kvm_set_pmuserenr(u64 val)
{
	struct kvm_cpu_context *hctxt;
	struct kvm_vcpu *vcpu;

	if (!kvm_arm_support_pmu_v3() || !has_vhe())
		return false;

	vcpu = kvm_get_running_vcpu();
	if (!vcpu || !vcpu_get_flag(vcpu, PMUSERENR_ON_CPU))
		return false;

	hctxt = host_data_ptr(host_ctxt);
	ctxt_sys_reg(hctxt, PMUSERENR_EL0) = val;
	return true;
}

/*
 * If we interrupted the guest to update the host PMU context, make
 * sure we re-apply the guest EL0 state.
 */
void kvm_vcpu_pmu_resync_el0(void)
{
	struct kvm_vcpu *vcpu;

	if (!has_vhe() || !in_interrupt())
		return;

	vcpu = kvm_get_running_vcpu();
	if (!vcpu)
		return;

	kvm_make_request(KVM_REQ_RESYNC_PMU_EL0, vcpu);
}
