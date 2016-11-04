/*
 * arch/arm/kernel/topology.c
 *
 * Copyright (C) 2011 Linaro Limited.
 * Written by: Vincent Guittot
 *
 * based on arch/sh/kernel/topology.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/node.h>
#include <linux/nodemask.h>
#include <linux/of.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <asm/cputype.h>
#include <asm/topology.h>

/*
 * cpu capacity scale management
 */

/*
 * cpu capacity table
 * This per cpu data structure describes the relative capacity of each core.
 * On a heteregenous system, cores don't have the same computation capacity
 * and we reflect that difference in the cpu_capacity field so the scheduler
 * can take this difference into account during load balance. A per cpu
 * structure is preferred because each CPU updates its own cpu_capacity field
 * during the load balance except for idle cores. One idle core is selected
 * to run the rebalance_domains for all idle cores and the cpu_capacity can be
 * updated during this sequence.
 */
static DEFINE_PER_CPU(unsigned long, cpu_scale);

unsigned long scale_cpu_capacity(struct sched_domain *sd, int cpu)
{
#ifdef CONFIG_CPU_FREQ
	unsigned long max_freq_scale = cpufreq_scale_max_freq_capacity(cpu);

	return per_cpu(cpu_scale, cpu) * max_freq_scale >> SCHED_CAPACITY_SHIFT;
#else
	return per_cpu(cpu_scale, cpu);
#endif
}

static void set_capacity_scale(unsigned int cpu, unsigned long capacity)
{
	per_cpu(cpu_scale, cpu) = capacity;
}

#ifdef CONFIG_OF
struct cpu_efficiency {
	const char *compatible;
	unsigned long efficiency;
};

/*
 * Table of relative efficiency of each processors
 * The efficiency value must fit in 20bit and the final
 * cpu_scale value must be in the range
 *   0 < cpu_scale < 3*SCHED_CAPACITY_SCALE/2
 * in order to return at most 1 when DIV_ROUND_CLOSEST
 * is used to compute the capacity of a CPU.
 * Processors that are not defined in the table,
 * use the default SCHED_CAPACITY_SCALE value for cpu_scale.
 */
static const struct cpu_efficiency table_efficiency[] = {
	{"arm,cortex-a15", 3891},
	{"arm,cortex-a7",  2048},
	{NULL, },
};

static unsigned long *__cpu_capacity;
#define cpu_capacity(cpu)	__cpu_capacity[cpu]

static unsigned long middle_capacity = 1;

/*
 * Iterate all CPUs' descriptor in DT and compute the efficiency
 * (as per table_efficiency). Also calculate a middle efficiency
 * as close as possible to  (max{eff_i} - min{eff_i}) / 2
 * This is later used to scale the cpu_capacity field such that an
 * 'average' CPU is of middle capacity. Also see the comments near
 * table_efficiency[] and update_cpu_capacity().
 */
static void __init parse_dt_topology(void)
{
	const struct cpu_efficiency *cpu_eff;
	struct device_node *cn = NULL;
	unsigned long min_capacity = ULONG_MAX;
	unsigned long max_capacity = 0;
	unsigned long capacity = 0;
	int cpu = 0;

	__cpu_capacity = kcalloc(nr_cpu_ids, sizeof(*__cpu_capacity),
				 GFP_NOWAIT);

	for_each_possible_cpu(cpu) {
		const u32 *rate;
		int len;

		/* too early to use cpu->of_node */
		cn = of_get_cpu_node(cpu, NULL);
		if (!cn) {
			pr_err("missing device node for CPU %d\n", cpu);
			continue;
		}

		for (cpu_eff = table_efficiency; cpu_eff->compatible; cpu_eff++)
			if (of_device_is_compatible(cn, cpu_eff->compatible))
				break;

		if (cpu_eff->compatible == NULL)
			continue;

		rate = of_get_property(cn, "clock-frequency", &len);
		if (!rate || len != 4) {
			pr_err("%s missing clock-frequency property\n",
				cn->full_name);
			continue;
		}

		capacity = ((be32_to_cpup(rate)) >> 20) * cpu_eff->efficiency;

		/* Save min capacity of the system */
		if (capacity < min_capacity)
			min_capacity = capacity;

		/* Save max capacity of the system */
		if (capacity > max_capacity)
			max_capacity = capacity;

		cpu_capacity(cpu) = capacity;
	}

	/* If min and max capacities are equals, we bypass the update of the
	 * cpu_scale because all CPUs have the same capacity. Otherwise, we
	 * compute a middle_capacity factor that will ensure that the capacity
	 * of an 'average' CPU of the system will be as close as possible to
	 * SCHED_CAPACITY_SCALE, which is the default value, but with the
	 * constraint explained near table_efficiency[].
	 */
	if (4*max_capacity < (3*(max_capacity + min_capacity)))
		middle_capacity = (min_capacity + max_capacity)
				>> (SCHED_CAPACITY_SHIFT+1);
	else
		middle_capacity = ((max_capacity / 3)
				>> (SCHED_CAPACITY_SHIFT-1)) + 1;

}

static const struct sched_group_energy * const cpu_core_energy(int cpu);

/*
 * Look for a customed capacity of a CPU in the cpu_capacity table during the
 * boot. The update of all CPUs is in O(n^2) for heteregeneous system but the
 * function returns directly for SMP system.
 */
static void update_cpu_capacity(unsigned int cpu)
{
	unsigned long capacity = SCHED_CAPACITY_SCALE;

	if (cpu_core_energy(cpu)) {
		int max_cap_idx = cpu_core_energy(cpu)->nr_cap_states - 1;
		capacity = cpu_core_energy(cpu)->cap_states[max_cap_idx].cap;
	}

	set_capacity_scale(cpu, capacity);

	pr_info("CPU%u: update cpu_capacity %lu\n",
		cpu, arch_scale_cpu_capacity(NULL, cpu));
}

#else
static inline void parse_dt_topology(void) {}
static inline void update_cpu_capacity(unsigned int cpuid) {}
#endif

 /*
 * cpu topology table
 */
struct cputopo_arm cpu_topology[NR_CPUS];
EXPORT_SYMBOL_GPL(cpu_topology);

const struct cpumask *cpu_coregroup_mask(int cpu)
{
	return &cpu_topology[cpu].core_sibling;
}

/*
 * The current assumption is that we can power gate each core independently.
 * This will be superseded by DT binding once available.
 */
const struct cpumask *cpu_corepower_mask(int cpu)
{
	return &cpu_topology[cpu].thread_sibling;
}

static void update_siblings_masks(unsigned int cpuid)
{
	struct cputopo_arm *cpu_topo, *cpuid_topo = &cpu_topology[cpuid];
	int cpu;

	/* update core and thread sibling masks */
	for_each_possible_cpu(cpu) {
		cpu_topo = &cpu_topology[cpu];

		if (cpuid_topo->socket_id != cpu_topo->socket_id)
			continue;

		cpumask_set_cpu(cpuid, &cpu_topo->core_sibling);
		if (cpu != cpuid)
			cpumask_set_cpu(cpu, &cpuid_topo->core_sibling);

		if (cpuid_topo->core_id != cpu_topo->core_id)
			continue;

		cpumask_set_cpu(cpuid, &cpu_topo->thread_sibling);
		if (cpu != cpuid)
			cpumask_set_cpu(cpu, &cpuid_topo->thread_sibling);
	}
	smp_wmb();
}

/*
 * store_cpu_topology is called at boot when only one cpu is running
 * and with the mutex cpu_hotplug.lock locked, when several cpus have booted,
 * which prevents simultaneous write access to cpu_topology array
 */
void store_cpu_topology(unsigned int cpuid)
{
	struct cputopo_arm *cpuid_topo = &cpu_topology[cpuid];
	unsigned int mpidr;

	/* If the cpu topology has been already set, just return */
	if (cpuid_topo->core_id != -1)
		return;

	mpidr = read_cpuid_mpidr();

	/* create cpu topology mapping */
	if ((mpidr & MPIDR_SMP_BITMASK) == MPIDR_SMP_VALUE) {
		/*
		 * This is a multiprocessor system
		 * multiprocessor format & multiprocessor mode field are set
		 */

		if (mpidr & MPIDR_MT_BITMASK) {
			/* core performance interdependency */
			cpuid_topo->thread_id = MPIDR_AFFINITY_LEVEL(mpidr, 0);
			cpuid_topo->core_id = MPIDR_AFFINITY_LEVEL(mpidr, 1);
			cpuid_topo->socket_id = MPIDR_AFFINITY_LEVEL(mpidr, 2);
		} else {
			/* largely independent cores */
			cpuid_topo->thread_id = -1;
			cpuid_topo->core_id = MPIDR_AFFINITY_LEVEL(mpidr, 0);
			cpuid_topo->socket_id = MPIDR_AFFINITY_LEVEL(mpidr, 1);
		}
	} else {
		/*
		 * This is an uniprocessor system
		 * we are in multiprocessor format but uniprocessor system
		 * or in the old uniprocessor format
		 */
		cpuid_topo->thread_id = -1;
		cpuid_topo->core_id = 0;
		cpuid_topo->socket_id = -1;
	}

	update_siblings_masks(cpuid);

	update_cpu_capacity(cpuid);

	pr_info("CPU%u: thread %d, cpu %d, socket %d, mpidr %x\n",
		cpuid, cpu_topology[cpuid].thread_id,
		cpu_topology[cpuid].core_id,
		cpu_topology[cpuid].socket_id, mpidr);
}

/*
 * ARM TC2 specific energy cost model data. There are no unit requirements for
 * the data. Data can be normalized to any reference point, but the
 * normalization must be consistent. That is, one bogo-joule/watt must be the
 * same quantity for all data, but we don't care what it is.
 */
static struct idle_state idle_states_cluster_a7[] = {
	 { .power = 25 }, /* arch_cpu_idle() (active idle) = WFI */
	 { .power = 25 }, /* WFI */
	 { .power = 10 }, /* cluster-sleep-l */
	};

static struct idle_state idle_states_cluster_a15[] = {
	 { .power = 70 }, /* arch_cpu_idle() (active idle) = WFI */
	 { .power = 70 }, /* WFI */
	 { .power = 25 }, /* cluster-sleep-b */
	};

static struct capacity_state cap_states_cluster_a7[] = {
	/* Cluster only power */
	 { .cap =  150, .power = 2967, }, /*  350 MHz */
	 { .cap =  172, .power = 2792, }, /*  400 MHz */
	 { .cap =  215, .power = 2810, }, /*  500 MHz */
	 { .cap =  258, .power = 2815, }, /*  600 MHz */
	 { .cap =  301, .power = 2919, }, /*  700 MHz */
	 { .cap =  344, .power = 2847, }, /*  800 MHz */
	 { .cap =  387, .power = 3917, }, /*  900 MHz */
	 { .cap =  430, .power = 4905, }, /* 1000 MHz */
	};

static struct capacity_state cap_states_cluster_a15[] = {
	/* Cluster only power */
	 { .cap =  426, .power =  7920, }, /*  500 MHz */
	 { .cap =  512, .power =  8165, }, /*  600 MHz */
	 { .cap =  597, .power =  8172, }, /*  700 MHz */
	 { .cap =  682, .power =  8195, }, /*  800 MHz */
	 { .cap =  768, .power =  8265, }, /*  900 MHz */
	 { .cap =  853, .power =  8446, }, /* 1000 MHz */
	 { .cap =  938, .power = 11426, }, /* 1100 MHz */
	 { .cap = 1024, .power = 15200, }, /* 1200 MHz */
	};

static struct sched_group_energy energy_cluster_a7 = {
	  .nr_idle_states = ARRAY_SIZE(idle_states_cluster_a7),
	  .idle_states    = idle_states_cluster_a7,
	  .nr_cap_states  = ARRAY_SIZE(cap_states_cluster_a7),
	  .cap_states     = cap_states_cluster_a7,
};

static struct sched_group_energy energy_cluster_a15 = {
	  .nr_idle_states = ARRAY_SIZE(idle_states_cluster_a15),
	  .idle_states    = idle_states_cluster_a15,
	  .nr_cap_states  = ARRAY_SIZE(cap_states_cluster_a15),
	  .cap_states     = cap_states_cluster_a15,
};

static struct idle_state idle_states_core_a7[] = {
	 { .power = 0 }, /* arch_cpu_idle (active idle) = WFI */
	 { .power = 0 }, /* WFI */
	 { .power = 0 }, /* cluster-sleep-l */
	};

static struct idle_state idle_states_core_a15[] = {
	 { .power = 0 }, /* arch_cpu_idle (active idle) = WFI */
	 { .power = 0 }, /* WFI */
	 { .power = 0 }, /* cluster-sleep-b */
	};

static struct capacity_state cap_states_core_a7[] = {
	/* Power per cpu */
	 { .cap =  150, .power =  187, }, /*  350 MHz */
	 { .cap =  172, .power =  275, }, /*  400 MHz */
	 { .cap =  215, .power =  334, }, /*  500 MHz */
	 { .cap =  258, .power =  407, }, /*  600 MHz */
	 { .cap =  301, .power =  447, }, /*  700 MHz */
	 { .cap =  344, .power =  549, }, /*  800 MHz */
	 { .cap =  387, .power =  761, }, /*  900 MHz */
	 { .cap =  430, .power = 1024, }, /* 1000 MHz */
	};

static struct capacity_state cap_states_core_a15[] = {
	/* Power per cpu */
	 { .cap =  426, .power = 2021, }, /*  500 MHz */
	 { .cap =  512, .power = 2312, }, /*  600 MHz */
	 { .cap =  597, .power = 2756, }, /*  700 MHz */
	 { .cap =  682, .power = 3125, }, /*  800 MHz */
	 { .cap =  768, .power = 3524, }, /*  900 MHz */
	 { .cap =  853, .power = 3846, }, /* 1000 MHz */
	 { .cap =  938, .power = 5177, }, /* 1100 MHz */
	 { .cap = 1024, .power = 6997, }, /* 1200 MHz */
	};

static struct sched_group_energy energy_core_a7 = {
	  .nr_idle_states = ARRAY_SIZE(idle_states_core_a7),
	  .idle_states    = idle_states_core_a7,
	  .nr_cap_states  = ARRAY_SIZE(cap_states_core_a7),
	  .cap_states     = cap_states_core_a7,
};

static struct sched_group_energy energy_core_a15 = {
	  .nr_idle_states = ARRAY_SIZE(idle_states_core_a15),
	  .idle_states    = idle_states_core_a15,
	  .nr_cap_states  = ARRAY_SIZE(cap_states_core_a15),
	  .cap_states     = cap_states_core_a15,
};

/* sd energy functions */
static inline
const struct sched_group_energy * const cpu_cluster_energy(int cpu)
{
	return cpu_topology[cpu].socket_id ? &energy_cluster_a7 :
			&energy_cluster_a15;
}

static inline
const struct sched_group_energy * const cpu_core_energy(int cpu)
{
	return cpu_topology[cpu].socket_id ? &energy_core_a7 :
			&energy_core_a15;
}

static inline int cpu_corepower_flags(void)
{
	return SD_SHARE_PKG_RESOURCES  | SD_SHARE_POWERDOMAIN | \
	       SD_SHARE_CAP_STATES;
}

static struct sched_domain_topology_level arm_topology[] = {
#ifdef CONFIG_SCHED_MC
	{ cpu_coregroup_mask, cpu_corepower_flags, cpu_core_energy, SD_INIT_NAME(MC) },
#endif
	{ cpu_cpu_mask, NULL, cpu_cluster_energy, SD_INIT_NAME(DIE) },
	{ NULL, },
};

/*
 * init_cpu_topology is called at boot when only one cpu is running
 * which prevent simultaneous write access to cpu_topology array
 */
void __init init_cpu_topology(void)
{
	unsigned int cpu;

	/* init core mask and capacity */
	for_each_possible_cpu(cpu) {
		struct cputopo_arm *cpu_topo = &(cpu_topology[cpu]);

		cpu_topo->thread_id = -1;
		cpu_topo->core_id =  -1;
		cpu_topo->socket_id = -1;
		cpumask_clear(&cpu_topo->core_sibling);
		cpumask_clear(&cpu_topo->thread_sibling);

		set_capacity_scale(cpu, SCHED_CAPACITY_SCALE);
	}
	smp_wmb();

	parse_dt_topology();

	/* Set scheduler topology descriptor */
	set_sched_topology(arm_topology);
}
