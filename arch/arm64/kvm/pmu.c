// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Arm Limited
 * Author: Andrew Murray <Andrew.Murray@arm.com>
 */
#include <linux/kvm_host.h>
#include <linux/perf_event.h>

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
void kvm_set_pmu_events(u32 set, struct perf_event_attr *attr)
{
	struct kvm_pmu_events *pmu = kvm_get_pmu_events();

	if (!kvm_arm_support_pmu_v3() || !pmu || !kvm_pmu_switch_needed(attr))
		return;

	if (!attr->exclude_host)
		pmu->events_host |= set;
	if (!attr->exclude_guest)
		pmu->events_guest |= set;
}

/*
 * Stop tracking events
 */
void kvm_clr_pmu_events(u32 clr)
{
	struct kvm_pmu_events *pmu = kvm_get_pmu_events();

	if (!kvm_arm_support_pmu_v3() || !pmu)
		return;

	pmu->events_host &= ~clr;
	pmu->events_guest &= ~clr;
}

#define PMEVTYPER_READ_CASE(idx)				\
	case idx:						\
		return read_sysreg(pmevtyper##idx##_el0)

#define PMEVTYPER_WRITE_CASE(idx)				\
	case idx:						\
		write_sysreg(val, pmevtyper##idx##_el0);	\
		break

#define PMEVTYPER_CASES(readwrite)				\
	PMEVTYPER_##readwrite##_CASE(0);			\
	PMEVTYPER_##readwrite##_CASE(1);			\
	PMEVTYPER_##readwrite##_CASE(2);			\
	PMEVTYPER_##readwrite##_CASE(3);			\
	PMEVTYPER_##readwrite##_CASE(4);			\
	PMEVTYPER_##readwrite##_CASE(5);			\
	PMEVTYPER_##readwrite##_CASE(6);			\
	PMEVTYPER_##readwrite##_CASE(7);			\
	PMEVTYPER_##readwrite##_CASE(8);			\
	PMEVTYPER_##readwrite##_CASE(9);			\
	PMEVTYPER_##readwrite##_CASE(10);			\
	PMEVTYPER_##readwrite##_CASE(11);			\
	PMEVTYPER_##readwrite##_CASE(12);			\
	PMEVTYPER_##readwrite##_CASE(13);			\
	PMEVTYPER_##readwrite##_CASE(14);			\
	PMEVTYPER_##readwrite##_CASE(15);			\
	PMEVTYPER_##readwrite##_CASE(16);			\
	PMEVTYPER_##readwrite##_CASE(17);			\
	PMEVTYPER_##readwrite##_CASE(18);			\
	PMEVTYPER_##readwrite##_CASE(19);			\
	PMEVTYPER_##readwrite##_CASE(20);			\
	PMEVTYPER_##readwrite##_CASE(21);			\
	PMEVTYPER_##readwrite##_CASE(22);			\
	PMEVTYPER_##readwrite##_CASE(23);			\
	PMEVTYPER_##readwrite##_CASE(24);			\
	PMEVTYPER_##readwrite##_CASE(25);			\
	PMEVTYPER_##readwrite##_CASE(26);			\
	PMEVTYPER_##readwrite##_CASE(27);			\
	PMEVTYPER_##readwrite##_CASE(28);			\
	PMEVTYPER_##readwrite##_CASE(29);			\
	PMEVTYPER_##readwrite##_CASE(30)

/*
 * Read a value direct from PMEVTYPER<idx> where idx is 0-30
 * or PMCCFILTR_EL0 where idx is ARMV8_PMU_CYCLE_IDX (31).
 */
static u64 kvm_vcpu_pmu_read_evtype_direct(int idx)
{
	switch (idx) {
	PMEVTYPER_CASES(READ);
	case ARMV8_PMU_CYCLE_IDX:
		return read_sysreg(pmccfiltr_el0);
	default:
		WARN_ON(1);
	}

	return 0;
}

/*
 * Write a value direct to PMEVTYPER<idx> where idx is 0-30
 * or PMCCFILTR_EL0 where idx is ARMV8_PMU_CYCLE_IDX (31).
 */
static void kvm_vcpu_pmu_write_evtype_direct(int idx, u32 val)
{
	switch (idx) {
	PMEVTYPER_CASES(WRITE);
	case ARMV8_PMU_CYCLE_IDX:
		write_sysreg(val, pmccfiltr_el0);
		break;
	default:
		WARN_ON(1);
	}
}

/*
 * Modify ARMv8 PMU events to include EL0 counting
 */
static void kvm_vcpu_pmu_enable_el0(unsigned long events)
{
	u64 typer;
	u32 counter;

	for_each_set_bit(counter, &events, 32) {
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

	for_each_set_bit(counter, &events, 32) {
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
	u32 events_guest, events_host;

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
	u32 events_guest, events_host;

	if (!kvm_arm_support_pmu_v3() || !has_vhe())
		return;

	pmu = kvm_get_pmu_events();
	events_guest = pmu->events_guest;
	events_host = pmu->events_host;

	kvm_vcpu_pmu_enable_el0(events_host);
	kvm_vcpu_pmu_disable_el0(events_guest);
}
