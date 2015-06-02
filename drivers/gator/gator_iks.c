/**
 * Copyright (C) ARM Limited 2013-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#if GATOR_IKS_SUPPORT

#include <linux/of.h>
#include <asm/bL_switcher.h>
#include <asm/smp_plat.h>
#include <trace/events/power_cpu_migrate.h>

static bool map_cpuids;
static int mpidr_cpuids[NR_CPUS];
static const struct gator_cpu *mpidr_cpus[NR_CPUS];
static int __lcpu_to_pcpu[NR_CPUS];

static const struct gator_cpu *gator_find_cpu_by_dt_name(const char *const name)
{
	int i;

	for (i = 0; gator_cpus[i].cpuid != 0; ++i) {
		const struct gator_cpu *const gator_cpu = &gator_cpus[i];

		if (gator_cpu->dt_name != NULL && strcmp(gator_cpu->dt_name, name) == 0)
			return gator_cpu;
	}

	return NULL;
}

static void calc_first_cluster_size(void)
{
	int len;
	const u32 *val;
	const char *compatible;
	struct device_node *cn = NULL;
	int mpidr_cpuids_count = 0;

	/* Zero is a valid cpuid, so initialize the array to 0xff's */
	memset(&mpidr_cpuids, 0xff, sizeof(mpidr_cpuids));
	memset(&mpidr_cpus, 0, sizeof(mpidr_cpus));

	while ((cn = of_find_node_by_type(cn, "cpu"))) {
		BUG_ON(mpidr_cpuids_count >= NR_CPUS);

		val = of_get_property(cn, "reg", &len);
		if (!val || len != 4) {
			pr_err("%s missing reg property\n", cn->full_name);
			continue;
		}
		compatible = of_get_property(cn, "compatible", NULL);
		if (compatible == NULL) {
			pr_err("%s missing compatible property\n", cn->full_name);
			continue;
		}

		mpidr_cpuids[mpidr_cpuids_count] = be32_to_cpup(val);
		mpidr_cpus[mpidr_cpuids_count] = gator_find_cpu_by_dt_name(compatible);
		++mpidr_cpuids_count;
	}

	map_cpuids = (mpidr_cpuids_count == nr_cpu_ids);
}

static int linearize_mpidr(int mpidr)
{
	int i;

	for (i = 0; i < nr_cpu_ids; ++i) {
		if (mpidr_cpuids[i] == mpidr)
			return i;
	}

	BUG();
}

int lcpu_to_pcpu(const int lcpu)
{
	int pcpu;

	if (!map_cpuids)
		return lcpu;

	BUG_ON(lcpu >= nr_cpu_ids || lcpu < 0);
	pcpu = __lcpu_to_pcpu[lcpu];
	BUG_ON(pcpu >= nr_cpu_ids || pcpu < 0);
	return pcpu;
}

int pcpu_to_lcpu(const int pcpu)
{
	int lcpu;

	if (!map_cpuids)
		return pcpu;

	BUG_ON(pcpu >= nr_cpu_ids || pcpu < 0);
	for (lcpu = 0; lcpu < nr_cpu_ids; ++lcpu) {
		if (__lcpu_to_pcpu[lcpu] == pcpu) {
			BUG_ON(lcpu >= nr_cpu_ids || lcpu < 0);
			return lcpu;
		}
	}
	BUG();
}

static void gator_update_cpu_mapping(u32 cpu_hwid)
{
	int lcpu = smp_processor_id();
	int pcpu = linearize_mpidr(cpu_hwid & MPIDR_HWID_BITMASK);

	BUG_ON(lcpu >= nr_cpu_ids || lcpu < 0);
	BUG_ON(pcpu >= nr_cpu_ids || pcpu < 0);
	__lcpu_to_pcpu[lcpu] = pcpu;
}

GATOR_DEFINE_PROBE(cpu_migrate_begin, TP_PROTO(u64 timestamp, u32 cpu_hwid))
{
	const int cpu = get_physical_cpu();

	gator_timer_offline((void *)1);
	gator_timer_offline_dispatch(cpu, true);
}

GATOR_DEFINE_PROBE(cpu_migrate_finish, TP_PROTO(u64 timestamp, u32 cpu_hwid))
{
	int cpu;

	gator_update_cpu_mapping(cpu_hwid);

	/* get_physical_cpu must be called after gator_update_cpu_mapping */
	cpu = get_physical_cpu();
	gator_timer_online_dispatch(cpu, true);
	gator_timer_online((void *)1);
}

GATOR_DEFINE_PROBE(cpu_migrate_current, TP_PROTO(u64 timestamp, u32 cpu_hwid))
{
	gator_update_cpu_mapping(cpu_hwid);
}

static void gator_send_iks_core_names(void)
{
	int cpu;
	/* Send the cpu names */
	preempt_disable();
	for (cpu = 0; cpu < nr_cpu_ids; ++cpu) {
		if (mpidr_cpus[cpu] != NULL)
			gator_send_core_name(cpu, mpidr_cpus[cpu]->cpuid);
	}
	preempt_enable();
}

static int gator_migrate_start(void)
{
	int retval = 0;

	if (!map_cpuids)
		return retval;

	if (retval == 0)
		retval = GATOR_REGISTER_TRACE(cpu_migrate_begin);
	if (retval == 0)
		retval = GATOR_REGISTER_TRACE(cpu_migrate_finish);
	if (retval == 0)
		retval = GATOR_REGISTER_TRACE(cpu_migrate_current);
	if (retval == 0) {
		/* Initialize the logical to physical cpu mapping */
		memset(&__lcpu_to_pcpu, 0xff, sizeof(__lcpu_to_pcpu));
		bL_switcher_trace_trigger();
	}
	return retval;
}

static void gator_migrate_stop(void)
{
	if (!map_cpuids)
		return;

	GATOR_UNREGISTER_TRACE(cpu_migrate_current);
	GATOR_UNREGISTER_TRACE(cpu_migrate_finish);
	GATOR_UNREGISTER_TRACE(cpu_migrate_begin);
}

#else

#define calc_first_cluster_size()
#define gator_send_iks_core_names()
#define gator_migrate_start() 0
#define gator_migrate_stop()

#endif
