// SPDX-License-Identifier: GPL-2.0
/*
 * Arch specific cpu topology information
 *
 * Copyright (C) 2016, ARM Ltd.
 * Written by: Juri Lelli, ARM Ltd.
 */

#include <linux/acpi.h>
#include <linux/cacheinfo.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/sched/topology.h>
#include <linux/cpuset.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>

#define CREATE_TRACE_POINTS
#include <trace/events/thermal_pressure.h>

static DEFINE_PER_CPU(struct scale_freq_data __rcu *, sft_data);
static struct cpumask scale_freq_counters_mask;
static bool scale_freq_invariant;
static DEFINE_PER_CPU(u32, freq_factor) = 1;

static bool supports_scale_freq_counters(const struct cpumask *cpus)
{
	return cpumask_subset(cpus, &scale_freq_counters_mask);
}

bool topology_scale_freq_invariant(void)
{
	return cpufreq_supports_freq_invariance() ||
	       supports_scale_freq_counters(cpu_online_mask);
}

static void update_scale_freq_invariant(bool status)
{
	if (scale_freq_invariant == status)
		return;

	/*
	 * Task scheduler behavior depends on frequency invariance support,
	 * either cpufreq or counter driven. If the support status changes as
	 * a result of counter initialisation and use, retrigger the build of
	 * scheduling domains to ensure the information is propagated properly.
	 */
	if (topology_scale_freq_invariant() == status) {
		scale_freq_invariant = status;
		rebuild_sched_domains_energy();
	}
}

void topology_set_scale_freq_source(struct scale_freq_data *data,
				    const struct cpumask *cpus)
{
	struct scale_freq_data *sfd;
	int cpu;

	/*
	 * Avoid calling rebuild_sched_domains() unnecessarily if FIE is
	 * supported by cpufreq.
	 */
	if (cpumask_empty(&scale_freq_counters_mask))
		scale_freq_invariant = topology_scale_freq_invariant();

	rcu_read_lock();

	for_each_cpu(cpu, cpus) {
		sfd = rcu_dereference(*per_cpu_ptr(&sft_data, cpu));

		/* Use ARCH provided counters whenever possible */
		if (!sfd || sfd->source != SCALE_FREQ_SOURCE_ARCH) {
			rcu_assign_pointer(per_cpu(sft_data, cpu), data);
			cpumask_set_cpu(cpu, &scale_freq_counters_mask);
		}
	}

	rcu_read_unlock();

	update_scale_freq_invariant(true);
}
EXPORT_SYMBOL_GPL(topology_set_scale_freq_source);

void topology_clear_scale_freq_source(enum scale_freq_source source,
				      const struct cpumask *cpus)
{
	struct scale_freq_data *sfd;
	int cpu;

	rcu_read_lock();

	for_each_cpu(cpu, cpus) {
		sfd = rcu_dereference(*per_cpu_ptr(&sft_data, cpu));

		if (sfd && sfd->source == source) {
			rcu_assign_pointer(per_cpu(sft_data, cpu), NULL);
			cpumask_clear_cpu(cpu, &scale_freq_counters_mask);
		}
	}

	rcu_read_unlock();

	/*
	 * Make sure all references to previous sft_data are dropped to avoid
	 * use-after-free races.
	 */
	synchronize_rcu();

	update_scale_freq_invariant(false);
}
EXPORT_SYMBOL_GPL(topology_clear_scale_freq_source);

void topology_scale_freq_tick(void)
{
	struct scale_freq_data *sfd = rcu_dereference_sched(*this_cpu_ptr(&sft_data));

	if (sfd)
		sfd->set_freq_scale();
}

DEFINE_PER_CPU(unsigned long, arch_freq_scale) = SCHED_CAPACITY_SCALE;
EXPORT_PER_CPU_SYMBOL_GPL(arch_freq_scale);

void topology_set_freq_scale(const struct cpumask *cpus, unsigned long cur_freq,
			     unsigned long max_freq)
{
	unsigned long scale;
	int i;

	if (WARN_ON_ONCE(!cur_freq || !max_freq))
		return;

	/*
	 * If the use of counters for FIE is enabled, just return as we don't
	 * want to update the scale factor with information from CPUFREQ.
	 * Instead the scale factor will be updated from arch_scale_freq_tick.
	 */
	if (supports_scale_freq_counters(cpus))
		return;

	scale = (cur_freq << SCHED_CAPACITY_SHIFT) / max_freq;

	for_each_cpu(i, cpus)
		per_cpu(arch_freq_scale, i) = scale;
}

DEFINE_PER_CPU(unsigned long, cpu_scale) = SCHED_CAPACITY_SCALE;
EXPORT_PER_CPU_SYMBOL_GPL(cpu_scale);

void topology_set_cpu_scale(unsigned int cpu, unsigned long capacity)
{
	per_cpu(cpu_scale, cpu) = capacity;
}

DEFINE_PER_CPU(unsigned long, thermal_pressure);

/**
 * topology_update_thermal_pressure() - Update thermal pressure for CPUs
 * @cpus        : The related CPUs for which capacity has been reduced
 * @capped_freq : The maximum allowed frequency that CPUs can run at
 *
 * Update the value of thermal pressure for all @cpus in the mask. The
 * cpumask should include all (online+offline) affected CPUs, to avoid
 * operating on stale data when hot-plug is used for some CPUs. The
 * @capped_freq reflects the currently allowed max CPUs frequency due to
 * thermal capping. It might be also a boost frequency value, which is bigger
 * than the internal 'freq_factor' max frequency. In such case the pressure
 * value should simply be removed, since this is an indication that there is
 * no thermal throttling. The @capped_freq must be provided in kHz.
 */
void topology_update_thermal_pressure(const struct cpumask *cpus,
				      unsigned long capped_freq)
{
	unsigned long max_capacity, capacity, th_pressure;
	u32 max_freq;
	int cpu;

	cpu = cpumask_first(cpus);
	max_capacity = arch_scale_cpu_capacity(cpu);
	max_freq = per_cpu(freq_factor, cpu);

	/* Convert to MHz scale which is used in 'freq_factor' */
	capped_freq /= 1000;

	/*
	 * Handle properly the boost frequencies, which should simply clean
	 * the thermal pressure value.
	 */
	if (max_freq <= capped_freq)
		capacity = max_capacity;
	else
		capacity = mult_frac(max_capacity, capped_freq, max_freq);

	th_pressure = max_capacity - capacity;

	trace_thermal_pressure_update(cpu, th_pressure);

	for_each_cpu(cpu, cpus)
		WRITE_ONCE(per_cpu(thermal_pressure, cpu), th_pressure);
}
EXPORT_SYMBOL_GPL(topology_update_thermal_pressure);

static ssize_t cpu_capacity_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct cpu *cpu = container_of(dev, struct cpu, dev);

	return sysfs_emit(buf, "%lu\n", topology_get_cpu_scale(cpu->dev.id));
}

static void update_topology_flags_workfn(struct work_struct *work);
static DECLARE_WORK(update_topology_flags_work, update_topology_flags_workfn);

static DEVICE_ATTR_RO(cpu_capacity);

static int register_cpu_capacity_sysctl(void)
{
	int i;
	struct device *cpu;

	for_each_possible_cpu(i) {
		cpu = get_cpu_device(i);
		if (!cpu) {
			pr_err("%s: too early to get CPU%d device!\n",
			       __func__, i);
			continue;
		}
		device_create_file(cpu, &dev_attr_cpu_capacity);
	}

	return 0;
}
subsys_initcall(register_cpu_capacity_sysctl);

static int update_topology;

int topology_update_cpu_topology(void)
{
	return update_topology;
}

/*
 * Updating the sched_domains can't be done directly from cpufreq callbacks
 * due to locking, so queue the work for later.
 */
static void update_topology_flags_workfn(struct work_struct *work)
{
	update_topology = 1;
	rebuild_sched_domains();
	pr_debug("sched_domain hierarchy rebuilt, flags updated\n");
	update_topology = 0;
}

static u32 *raw_capacity;

static int free_raw_capacity(void)
{
	kfree(raw_capacity);
	raw_capacity = NULL;

	return 0;
}

void topology_normalize_cpu_scale(void)
{
	u64 capacity;
	u64 capacity_scale;
	int cpu;

	if (!raw_capacity)
		return;

	capacity_scale = 1;
	for_each_possible_cpu(cpu) {
		capacity = raw_capacity[cpu] * per_cpu(freq_factor, cpu);
		capacity_scale = max(capacity, capacity_scale);
	}

	pr_debug("cpu_capacity: capacity_scale=%llu\n", capacity_scale);
	for_each_possible_cpu(cpu) {
		capacity = raw_capacity[cpu] * per_cpu(freq_factor, cpu);
		capacity = div64_u64(capacity << SCHED_CAPACITY_SHIFT,
			capacity_scale);
		topology_set_cpu_scale(cpu, capacity);
		pr_debug("cpu_capacity: CPU%d cpu_capacity=%lu\n",
			cpu, topology_get_cpu_scale(cpu));
	}
}

bool __init topology_parse_cpu_capacity(struct device_node *cpu_node, int cpu)
{
	struct clk *cpu_clk;
	static bool cap_parsing_failed;
	int ret;
	u32 cpu_capacity;

	if (cap_parsing_failed)
		return false;

	ret = of_property_read_u32(cpu_node, "capacity-dmips-mhz",
				   &cpu_capacity);
	if (!ret) {
		if (!raw_capacity) {
			raw_capacity = kcalloc(num_possible_cpus(),
					       sizeof(*raw_capacity),
					       GFP_KERNEL);
			if (!raw_capacity) {
				cap_parsing_failed = true;
				return false;
			}
		}
		raw_capacity[cpu] = cpu_capacity;
		pr_debug("cpu_capacity: %pOF cpu_capacity=%u (raw)\n",
			cpu_node, raw_capacity[cpu]);

		/*
		 * Update freq_factor for calculating early boot cpu capacities.
		 * For non-clk CPU DVFS mechanism, there's no way to get the
		 * frequency value now, assuming they are running at the same
		 * frequency (by keeping the initial freq_factor value).
		 */
		cpu_clk = of_clk_get(cpu_node, 0);
		if (!PTR_ERR_OR_ZERO(cpu_clk)) {
			per_cpu(freq_factor, cpu) =
				clk_get_rate(cpu_clk) / 1000;
			clk_put(cpu_clk);
		}
	} else {
		if (raw_capacity) {
			pr_err("cpu_capacity: missing %pOF raw capacity\n",
				cpu_node);
			pr_err("cpu_capacity: partial information: fallback to 1024 for all CPUs\n");
		}
		cap_parsing_failed = true;
		free_raw_capacity();
	}

	return !ret;
}

#ifdef CONFIG_ACPI_CPPC_LIB
#include <acpi/cppc_acpi.h>

void topology_init_cpu_capacity_cppc(void)
{
	struct cppc_perf_caps perf_caps;
	int cpu;

	if (likely(acpi_disabled || !acpi_cpc_valid()))
		return;

	raw_capacity = kcalloc(num_possible_cpus(), sizeof(*raw_capacity),
			       GFP_KERNEL);
	if (!raw_capacity)
		return;

	for_each_possible_cpu(cpu) {
		if (!cppc_get_perf_caps(cpu, &perf_caps) &&
		    (perf_caps.highest_perf >= perf_caps.nominal_perf) &&
		    (perf_caps.highest_perf >= perf_caps.lowest_perf)) {
			raw_capacity[cpu] = perf_caps.highest_perf;
			pr_debug("cpu_capacity: CPU%d cpu_capacity=%u (raw).\n",
				 cpu, raw_capacity[cpu]);
			continue;
		}

		pr_err("cpu_capacity: CPU%d missing/invalid highest performance.\n", cpu);
		pr_err("cpu_capacity: partial information: fallback to 1024 for all CPUs\n");
		goto exit;
	}

	topology_normalize_cpu_scale();
	schedule_work(&update_topology_flags_work);
	pr_debug("cpu_capacity: cpu_capacity initialization done\n");

exit:
	free_raw_capacity();
}
#endif

#ifdef CONFIG_CPU_FREQ
static cpumask_var_t cpus_to_visit;
static void parsing_done_workfn(struct work_struct *work);
static DECLARE_WORK(parsing_done_work, parsing_done_workfn);

static int
init_cpu_capacity_callback(struct notifier_block *nb,
			   unsigned long val,
			   void *data)
{
	struct cpufreq_policy *policy = data;
	int cpu;

	if (!raw_capacity)
		return 0;

	if (val != CPUFREQ_CREATE_POLICY)
		return 0;

	pr_debug("cpu_capacity: init cpu capacity for CPUs [%*pbl] (to_visit=%*pbl)\n",
		 cpumask_pr_args(policy->related_cpus),
		 cpumask_pr_args(cpus_to_visit));

	cpumask_andnot(cpus_to_visit, cpus_to_visit, policy->related_cpus);

	for_each_cpu(cpu, policy->related_cpus)
		per_cpu(freq_factor, cpu) = policy->cpuinfo.max_freq / 1000;

	if (cpumask_empty(cpus_to_visit)) {
		topology_normalize_cpu_scale();
		schedule_work(&update_topology_flags_work);
		free_raw_capacity();
		pr_debug("cpu_capacity: parsing done\n");
		schedule_work(&parsing_done_work);
	}

	return 0;
}

static struct notifier_block init_cpu_capacity_notifier = {
	.notifier_call = init_cpu_capacity_callback,
};

static int __init register_cpufreq_notifier(void)
{
	int ret;

	/*
	 * On ACPI-based systems skip registering cpufreq notifier as cpufreq
	 * information is not needed for cpu capacity initialization.
	 */
	if (!acpi_disabled || !raw_capacity)
		return -EINVAL;

	if (!alloc_cpumask_var(&cpus_to_visit, GFP_KERNEL))
		return -ENOMEM;

	cpumask_copy(cpus_to_visit, cpu_possible_mask);

	ret = cpufreq_register_notifier(&init_cpu_capacity_notifier,
					CPUFREQ_POLICY_NOTIFIER);

	if (ret)
		free_cpumask_var(cpus_to_visit);

	return ret;
}
core_initcall(register_cpufreq_notifier);

static void parsing_done_workfn(struct work_struct *work)
{
	cpufreq_unregister_notifier(&init_cpu_capacity_notifier,
					 CPUFREQ_POLICY_NOTIFIER);
	free_cpumask_var(cpus_to_visit);
}

#else
core_initcall(free_raw_capacity);
#endif

#if defined(CONFIG_ARM64) || defined(CONFIG_RISCV)
/*
 * This function returns the logic cpu number of the node.
 * There are basically three kinds of return values:
 * (1) logic cpu number which is > 0.
 * (2) -ENODEV when the device tree(DT) node is valid and found in the DT but
 * there is no possible logical CPU in the kernel to match. This happens
 * when CONFIG_NR_CPUS is configure to be smaller than the number of
 * CPU nodes in DT. We need to just ignore this case.
 * (3) -1 if the node does not exist in the device tree
 */
static int __init get_cpu_for_node(struct device_node *node)
{
	struct device_node *cpu_node;
	int cpu;

	cpu_node = of_parse_phandle(node, "cpu", 0);
	if (!cpu_node)
		return -1;

	cpu = of_cpu_node_to_id(cpu_node);
	if (cpu >= 0)
		topology_parse_cpu_capacity(cpu_node, cpu);
	else
		pr_info("CPU node for %pOF exist but the possible cpu range is :%*pbl\n",
			cpu_node, cpumask_pr_args(cpu_possible_mask));

	of_node_put(cpu_node);
	return cpu;
}

static int __init parse_core(struct device_node *core, int package_id,
			     int cluster_id, int core_id)
{
	char name[20];
	bool leaf = true;
	int i = 0;
	int cpu;
	struct device_node *t;

	do {
		snprintf(name, sizeof(name), "thread%d", i);
		t = of_get_child_by_name(core, name);
		if (t) {
			leaf = false;
			cpu = get_cpu_for_node(t);
			if (cpu >= 0) {
				cpu_topology[cpu].package_id = package_id;
				cpu_topology[cpu].cluster_id = cluster_id;
				cpu_topology[cpu].core_id = core_id;
				cpu_topology[cpu].thread_id = i;
			} else if (cpu != -ENODEV) {
				pr_err("%pOF: Can't get CPU for thread\n", t);
				of_node_put(t);
				return -EINVAL;
			}
			of_node_put(t);
		}
		i++;
	} while (t);

	cpu = get_cpu_for_node(core);
	if (cpu >= 0) {
		if (!leaf) {
			pr_err("%pOF: Core has both threads and CPU\n",
			       core);
			return -EINVAL;
		}

		cpu_topology[cpu].package_id = package_id;
		cpu_topology[cpu].cluster_id = cluster_id;
		cpu_topology[cpu].core_id = core_id;
	} else if (leaf && cpu != -ENODEV) {
		pr_err("%pOF: Can't get CPU for leaf core\n", core);
		return -EINVAL;
	}

	return 0;
}

static int __init parse_cluster(struct device_node *cluster, int package_id,
				int cluster_id, int depth)
{
	char name[20];
	bool leaf = true;
	bool has_cores = false;
	struct device_node *c;
	int core_id = 0;
	int i, ret;

	/*
	 * First check for child clusters; we currently ignore any
	 * information about the nesting of clusters and present the
	 * scheduler with a flat list of them.
	 */
	i = 0;
	do {
		snprintf(name, sizeof(name), "cluster%d", i);
		c = of_get_child_by_name(cluster, name);
		if (c) {
			leaf = false;
			ret = parse_cluster(c, package_id, i, depth + 1);
			if (depth > 0)
				pr_warn("Topology for clusters of clusters not yet supported\n");
			of_node_put(c);
			if (ret != 0)
				return ret;
		}
		i++;
	} while (c);

	/* Now check for cores */
	i = 0;
	do {
		snprintf(name, sizeof(name), "core%d", i);
		c = of_get_child_by_name(cluster, name);
		if (c) {
			has_cores = true;

			if (depth == 0) {
				pr_err("%pOF: cpu-map children should be clusters\n",
				       c);
				of_node_put(c);
				return -EINVAL;
			}

			if (leaf) {
				ret = parse_core(c, package_id, cluster_id,
						 core_id++);
			} else {
				pr_err("%pOF: Non-leaf cluster with core %s\n",
				       cluster, name);
				ret = -EINVAL;
			}

			of_node_put(c);
			if (ret != 0)
				return ret;
		}
		i++;
	} while (c);

	if (leaf && !has_cores)
		pr_warn("%pOF: empty cluster\n", cluster);

	return 0;
}

static int __init parse_socket(struct device_node *socket)
{
	char name[20];
	struct device_node *c;
	bool has_socket = false;
	int package_id = 0, ret;

	do {
		snprintf(name, sizeof(name), "socket%d", package_id);
		c = of_get_child_by_name(socket, name);
		if (c) {
			has_socket = true;
			ret = parse_cluster(c, package_id, -1, 0);
			of_node_put(c);
			if (ret != 0)
				return ret;
		}
		package_id++;
	} while (c);

	if (!has_socket)
		ret = parse_cluster(socket, 0, -1, 0);

	return ret;
}

static int __init parse_dt_topology(void)
{
	struct device_node *cn, *map;
	int ret = 0;
	int cpu;

	cn = of_find_node_by_path("/cpus");
	if (!cn) {
		pr_err("No CPU information found in DT\n");
		return 0;
	}

	/*
	 * When topology is provided cpu-map is essentially a root
	 * cluster with restricted subnodes.
	 */
	map = of_get_child_by_name(cn, "cpu-map");
	if (!map)
		goto out;

	ret = parse_socket(map);
	if (ret != 0)
		goto out_map;

	topology_normalize_cpu_scale();

	/*
	 * Check that all cores are in the topology; the SMP code will
	 * only mark cores described in the DT as possible.
	 */
	for_each_possible_cpu(cpu)
		if (cpu_topology[cpu].package_id < 0) {
			ret = -EINVAL;
			break;
		}

out_map:
	of_node_put(map);
out:
	of_node_put(cn);
	return ret;
}
#endif

/*
 * cpu topology table
 */
struct cpu_topology cpu_topology[NR_CPUS];
EXPORT_SYMBOL_GPL(cpu_topology);

const struct cpumask *cpu_coregroup_mask(int cpu)
{
	const cpumask_t *core_mask = cpumask_of_node(cpu_to_node(cpu));

	/* Find the smaller of NUMA, core or LLC siblings */
	if (cpumask_subset(&cpu_topology[cpu].core_sibling, core_mask)) {
		/* not numa in package, lets use the package siblings */
		core_mask = &cpu_topology[cpu].core_sibling;
	}

	if (last_level_cache_is_valid(cpu)) {
		if (cpumask_subset(&cpu_topology[cpu].llc_sibling, core_mask))
			core_mask = &cpu_topology[cpu].llc_sibling;
	}

	/*
	 * For systems with no shared cpu-side LLC but with clusters defined,
	 * extend core_mask to cluster_siblings. The sched domain builder will
	 * then remove MC as redundant with CLS if SCHED_CLUSTER is enabled.
	 */
	if (IS_ENABLED(CONFIG_SCHED_CLUSTER) &&
	    cpumask_subset(core_mask, &cpu_topology[cpu].cluster_sibling))
		core_mask = &cpu_topology[cpu].cluster_sibling;

	return core_mask;
}

const struct cpumask *cpu_clustergroup_mask(int cpu)
{
	/*
	 * Forbid cpu_clustergroup_mask() to span more or the same CPUs as
	 * cpu_coregroup_mask().
	 */
	if (cpumask_subset(cpu_coregroup_mask(cpu),
			   &cpu_topology[cpu].cluster_sibling))
		return get_cpu_mask(cpu);

	return &cpu_topology[cpu].cluster_sibling;
}

void update_siblings_masks(unsigned int cpuid)
{
	struct cpu_topology *cpu_topo, *cpuid_topo = &cpu_topology[cpuid];
	int cpu, ret;

	ret = detect_cache_attributes(cpuid);
	if (ret && ret != -ENOENT)
		pr_info("Early cacheinfo failed, ret = %d\n", ret);

	/* update core and thread sibling masks */
	for_each_online_cpu(cpu) {
		cpu_topo = &cpu_topology[cpu];

		if (last_level_cache_is_shared(cpu, cpuid)) {
			cpumask_set_cpu(cpu, &cpuid_topo->llc_sibling);
			cpumask_set_cpu(cpuid, &cpu_topo->llc_sibling);
		}

		if (cpuid_topo->package_id != cpu_topo->package_id)
			continue;

		cpumask_set_cpu(cpuid, &cpu_topo->core_sibling);
		cpumask_set_cpu(cpu, &cpuid_topo->core_sibling);

		if (cpuid_topo->cluster_id != cpu_topo->cluster_id)
			continue;

		if (cpuid_topo->cluster_id >= 0) {
			cpumask_set_cpu(cpu, &cpuid_topo->cluster_sibling);
			cpumask_set_cpu(cpuid, &cpu_topo->cluster_sibling);
		}

		if (cpuid_topo->core_id != cpu_topo->core_id)
			continue;

		cpumask_set_cpu(cpuid, &cpu_topo->thread_sibling);
		cpumask_set_cpu(cpu, &cpuid_topo->thread_sibling);
	}
}

static void clear_cpu_topology(int cpu)
{
	struct cpu_topology *cpu_topo = &cpu_topology[cpu];

	cpumask_clear(&cpu_topo->llc_sibling);
	cpumask_set_cpu(cpu, &cpu_topo->llc_sibling);

	cpumask_clear(&cpu_topo->cluster_sibling);
	cpumask_set_cpu(cpu, &cpu_topo->cluster_sibling);

	cpumask_clear(&cpu_topo->core_sibling);
	cpumask_set_cpu(cpu, &cpu_topo->core_sibling);
	cpumask_clear(&cpu_topo->thread_sibling);
	cpumask_set_cpu(cpu, &cpu_topo->thread_sibling);
}

void __init reset_cpu_topology(void)
{
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		struct cpu_topology *cpu_topo = &cpu_topology[cpu];

		cpu_topo->thread_id = -1;
		cpu_topo->core_id = -1;
		cpu_topo->cluster_id = -1;
		cpu_topo->package_id = -1;

		clear_cpu_topology(cpu);
	}
}

void remove_cpu_topology(unsigned int cpu)
{
	int sibling;

	for_each_cpu(sibling, topology_core_cpumask(cpu))
		cpumask_clear_cpu(cpu, topology_core_cpumask(sibling));
	for_each_cpu(sibling, topology_sibling_cpumask(cpu))
		cpumask_clear_cpu(cpu, topology_sibling_cpumask(sibling));
	for_each_cpu(sibling, topology_cluster_cpumask(cpu))
		cpumask_clear_cpu(cpu, topology_cluster_cpumask(sibling));
	for_each_cpu(sibling, topology_llc_cpumask(cpu))
		cpumask_clear_cpu(cpu, topology_llc_cpumask(sibling));

	clear_cpu_topology(cpu);
}

__weak int __init parse_acpi_topology(void)
{
	return 0;
}

#if defined(CONFIG_ARM64) || defined(CONFIG_RISCV)
void __init init_cpu_topology(void)
{
	int ret;

	reset_cpu_topology();
	ret = parse_acpi_topology();
	if (!ret)
		ret = of_have_populated_dt() && parse_dt_topology();

	if (ret) {
		/*
		 * Discard anything that was parsed if we hit an error so we
		 * don't use partial information.
		 */
		reset_cpu_topology();
		return;
	}
}
#endif
