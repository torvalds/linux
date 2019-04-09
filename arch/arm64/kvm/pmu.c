// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Arm Limited
 * Author: Andrew Murray <Andrew.Murray@arm.com>
 */
#include <linux/kvm_host.h>
#include <linux/perf_event.h>
#include <asm/kvm_hyp.h>

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

/*
 * Add events to track that we may want to switch at guest entry/exit
 * time.
 */
void kvm_set_pmu_events(u32 set, struct perf_event_attr *attr)
{
	struct kvm_host_data *ctx = this_cpu_ptr(&kvm_host_data);

	if (!kvm_pmu_switch_needed(attr))
		return;

	if (!attr->exclude_host)
		ctx->pmu_events.events_host |= set;
	if (!attr->exclude_guest)
		ctx->pmu_events.events_guest |= set;
}

/*
 * Stop tracking events
 */
void kvm_clr_pmu_events(u32 clr)
{
	struct kvm_host_data *ctx = this_cpu_ptr(&kvm_host_data);

	ctx->pmu_events.events_host &= ~clr;
	ctx->pmu_events.events_guest &= ~clr;
}

/**
 * Disable host events, enable guest events
 */
bool __hyp_text __pmu_switch_to_guest(struct kvm_cpu_context *host_ctxt)
{
	struct kvm_host_data *host;
	struct kvm_pmu_events *pmu;

	host = container_of(host_ctxt, struct kvm_host_data, host_ctxt);
	pmu = &host->pmu_events;

	if (pmu->events_host)
		write_sysreg(pmu->events_host, pmcntenclr_el0);

	if (pmu->events_guest)
		write_sysreg(pmu->events_guest, pmcntenset_el0);

	return (pmu->events_host || pmu->events_guest);
}

/**
 * Disable guest events, enable host events
 */
void __hyp_text __pmu_switch_to_host(struct kvm_cpu_context *host_ctxt)
{
	struct kvm_host_data *host;
	struct kvm_pmu_events *pmu;

	host = container_of(host_ctxt, struct kvm_host_data, host_ctxt);
	pmu = &host->pmu_events;

	if (pmu->events_guest)
		write_sysreg(pmu->events_guest, pmcntenclr_el0);

	if (pmu->events_host)
		write_sysreg(pmu->events_host, pmcntenset_el0);
}

/*
 * Modify ARMv8 PMU events to include EL0 counting
 */
static void kvm_vcpu_pmu_enable_el0(unsigned long events)
{
	u64 typer;
	u32 counter;

	for_each_set_bit(counter, &events, 32) {
		write_sysreg(counter, pmselr_el0);
		isb();
		typer = read_sysreg(pmxevtyper_el0) & ~ARMV8_PMU_EXCLUDE_EL0;
		write_sysreg(typer, pmxevtyper_el0);
		isb();
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
		write_sysreg(counter, pmselr_el0);
		isb();
		typer = read_sysreg(pmxevtyper_el0) | ARMV8_PMU_EXCLUDE_EL0;
		write_sysreg(typer, pmxevtyper_el0);
		isb();
	}
}

/*
 * On VHE ensure that only guest events have EL0 counting enabled
 */
void kvm_vcpu_pmu_restore_guest(struct kvm_vcpu *vcpu)
{
	struct kvm_cpu_context *host_ctxt;
	struct kvm_host_data *host;
	u32 events_guest, events_host;

	if (!has_vhe())
		return;

	host_ctxt = vcpu->arch.host_cpu_context;
	host = container_of(host_ctxt, struct kvm_host_data, host_ctxt);
	events_guest = host->pmu_events.events_guest;
	events_host = host->pmu_events.events_host;

	kvm_vcpu_pmu_enable_el0(events_guest);
	kvm_vcpu_pmu_disable_el0(events_host);
}

/*
 * On VHE ensure that only host events have EL0 counting enabled
 */
void kvm_vcpu_pmu_restore_host(struct kvm_vcpu *vcpu)
{
	struct kvm_cpu_context *host_ctxt;
	struct kvm_host_data *host;
	u32 events_guest, events_host;

	if (!has_vhe())
		return;

	host_ctxt = vcpu->arch.host_cpu_context;
	host = container_of(host_ctxt, struct kvm_host_data, host_ctxt);
	events_guest = host->pmu_events.events_guest;
	events_host = host->pmu_events.events_host;

	kvm_vcpu_pmu_enable_el0(events_host);
	kvm_vcpu_pmu_disable_el0(events_guest);
}
