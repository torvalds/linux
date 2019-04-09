// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Arm Limited
 * Author: Andrew Murray <Andrew.Murray@arm.com>
 */
#include <linux/kvm_host.h>
#include <linux/perf_event.h>

/*
 * Given the exclude_{host,guest} attributes, determine if we are going
 * to need to switch counters at guest entry/exit.
 */
static bool kvm_pmu_switch_needed(struct perf_event_attr *attr)
{
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
