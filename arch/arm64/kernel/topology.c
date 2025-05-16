/*
 * arch/arm64/kernel/topology.c
 *
 * Copyright (C) 2011,2013,2014 Linaro Limited.
 *
 * Based on the arm32 version written by Vincent Guittot in turn based on
 * arch/sh/kernel/topology.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/acpi.h>
#include <linux/arch_topology.h>
#include <linux/cacheinfo.h>
#include <linux/cpufreq.h>
#include <linux/cpu_smt.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/sched/isolation.h>
#include <linux/xarray.h>

#include <asm/cpu.h>
#include <asm/cputype.h>
#include <asm/topology.h>

#ifdef CONFIG_ACPI
static bool __init acpi_cpu_is_threaded(int cpu)
{
	int is_threaded = acpi_pptt_cpu_is_thread(cpu);

	/*
	 * if the PPTT doesn't have thread information, assume a homogeneous
	 * machine and return the current CPU's thread state.
	 */
	if (is_threaded < 0)
		is_threaded = read_cpuid_mpidr() & MPIDR_MT_BITMASK;

	return !!is_threaded;
}

struct cpu_smt_info {
	unsigned int thread_num;
	int core_id;
};

/*
 * Propagate the topology information of the processor_topology_node tree to the
 * cpu_topology array.
 */
int __init parse_acpi_topology(void)
{
	unsigned int max_smt_thread_num = 1;
	struct cpu_smt_info *entry;
	struct xarray hetero_cpu;
	unsigned long hetero_id;
	int cpu, topology_id;

	if (acpi_disabled)
		return 0;

	xa_init(&hetero_cpu);

	for_each_possible_cpu(cpu) {
		topology_id = find_acpi_cpu_topology(cpu, 0);
		if (topology_id < 0)
			return topology_id;

		if (acpi_cpu_is_threaded(cpu)) {
			cpu_topology[cpu].thread_id = topology_id;
			topology_id = find_acpi_cpu_topology(cpu, 1);
			cpu_topology[cpu].core_id   = topology_id;

			/*
			 * In the PPTT, CPUs below a node with the 'identical
			 * implementation' flag have the same number of threads.
			 * Count the number of threads for only one CPU (i.e.
			 * one core_id) among those with the same hetero_id.
			 * See the comment of find_acpi_cpu_topology_hetero_id()
			 * for more details.
			 *
			 * One entry is created for each node having:
			 * - the 'identical implementation' flag
			 * - its parent not having the flag
			 */
			hetero_id = find_acpi_cpu_topology_hetero_id(cpu);
			entry = xa_load(&hetero_cpu, hetero_id);
			if (!entry) {
				entry = kzalloc(sizeof(*entry), GFP_KERNEL);
				WARN_ON_ONCE(!entry);

				if (entry) {
					entry->core_id = topology_id;
					entry->thread_num = 1;
					xa_store(&hetero_cpu, hetero_id,
						 entry, GFP_KERNEL);
				}
			} else if (entry->core_id == topology_id) {
				entry->thread_num++;
			}
		} else {
			cpu_topology[cpu].thread_id  = -1;
			cpu_topology[cpu].core_id    = topology_id;
		}
		topology_id = find_acpi_cpu_topology_cluster(cpu);
		cpu_topology[cpu].cluster_id = topology_id;
		topology_id = find_acpi_cpu_topology_package(cpu);
		cpu_topology[cpu].package_id = topology_id;
	}

	/*
	 * This is a short loop since the number of XArray elements is the
	 * number of heterogeneous CPU clusters. On a homogeneous system
	 * there's only one entry in the XArray.
	 */
	xa_for_each(&hetero_cpu, hetero_id, entry) {
		max_smt_thread_num = max(max_smt_thread_num, entry->thread_num);
		xa_erase(&hetero_cpu, hetero_id);
		kfree(entry);
	}

	cpu_smt_set_num_threads(max_smt_thread_num, max_smt_thread_num);
	xa_destroy(&hetero_cpu);
	return 0;
}
#endif

#ifdef CONFIG_ARM64_AMU_EXTN
#define read_corecnt()	read_sysreg_s(SYS_AMEVCNTR0_CORE_EL0)
#define read_constcnt()	read_sysreg_s(SYS_AMEVCNTR0_CONST_EL0)
#else
#define read_corecnt()	(0UL)
#define read_constcnt()	(0UL)
#endif

#undef pr_fmt
#define pr_fmt(fmt) "AMU: " fmt

/*
 * Ensure that amu_scale_freq_tick() will return SCHED_CAPACITY_SCALE until
 * the CPU capacity and its associated frequency have been correctly
 * initialized.
 */
static DEFINE_PER_CPU_READ_MOSTLY(unsigned long, arch_max_freq_scale) =  1UL << (2 * SCHED_CAPACITY_SHIFT);
static cpumask_var_t amu_fie_cpus;

struct amu_cntr_sample {
	u64		arch_const_cycles_prev;
	u64		arch_core_cycles_prev;
	unsigned long	last_scale_update;
};

static DEFINE_PER_CPU_SHARED_ALIGNED(struct amu_cntr_sample, cpu_amu_samples);

void update_freq_counters_refs(void)
{
	struct amu_cntr_sample *amu_sample = this_cpu_ptr(&cpu_amu_samples);

	amu_sample->arch_core_cycles_prev = read_corecnt();
	amu_sample->arch_const_cycles_prev = read_constcnt();
}

static inline bool freq_counters_valid(int cpu)
{
	struct amu_cntr_sample *amu_sample = per_cpu_ptr(&cpu_amu_samples, cpu);

	if ((cpu >= nr_cpu_ids) || !cpumask_test_cpu(cpu, cpu_present_mask))
		return false;

	if (!cpu_has_amu_feat(cpu)) {
		pr_debug("CPU%d: counters are not supported.\n", cpu);
		return false;
	}

	if (unlikely(!amu_sample->arch_const_cycles_prev ||
		     !amu_sample->arch_core_cycles_prev)) {
		pr_debug("CPU%d: cycle counters are not enabled.\n", cpu);
		return false;
	}

	return true;
}

void freq_inv_set_max_ratio(int cpu, u64 max_rate)
{
	u64 ratio, ref_rate = arch_timer_get_rate();

	if (unlikely(!max_rate || !ref_rate)) {
		WARN_ONCE(1, "CPU%d: invalid maximum or reference frequency.\n",
			 cpu);
		return;
	}

	/*
	 * Pre-compute the fixed ratio between the frequency of the constant
	 * reference counter and the maximum frequency of the CPU.
	 *
	 *			    ref_rate
	 * arch_max_freq_scale =   ---------- * SCHED_CAPACITY_SCALE²
	 *			    max_rate
	 *
	 * We use a factor of 2 * SCHED_CAPACITY_SHIFT -> SCHED_CAPACITY_SCALE²
	 * in order to ensure a good resolution for arch_max_freq_scale for
	 * very low reference frequencies (down to the KHz range which should
	 * be unlikely).
	 */
	ratio = ref_rate << (2 * SCHED_CAPACITY_SHIFT);
	ratio = div64_u64(ratio, max_rate);
	if (!ratio) {
		WARN_ONCE(1, "Reference frequency too low.\n");
		return;
	}

	WRITE_ONCE(per_cpu(arch_max_freq_scale, cpu), (unsigned long)ratio);
}

static void amu_scale_freq_tick(void)
{
	struct amu_cntr_sample *amu_sample = this_cpu_ptr(&cpu_amu_samples);
	u64 prev_core_cnt, prev_const_cnt;
	u64 core_cnt, const_cnt, scale;

	prev_const_cnt = amu_sample->arch_const_cycles_prev;
	prev_core_cnt = amu_sample->arch_core_cycles_prev;

	update_freq_counters_refs();

	const_cnt = amu_sample->arch_const_cycles_prev;
	core_cnt = amu_sample->arch_core_cycles_prev;

	/*
	 * This should not happen unless the AMUs have been reset and the
	 * counter values have not been restored - unlikely
	 */
	if (unlikely(core_cnt <= prev_core_cnt ||
		     const_cnt <= prev_const_cnt))
		return;

	/*
	 *	    /\core    arch_max_freq_scale
	 * scale =  ------- * --------------------
	 *	    /\const   SCHED_CAPACITY_SCALE
	 *
	 * See validate_cpu_freq_invariance_counters() for details on
	 * arch_max_freq_scale and the use of SCHED_CAPACITY_SHIFT.
	 */
	scale = core_cnt - prev_core_cnt;
	scale *= this_cpu_read(arch_max_freq_scale);
	scale = div64_u64(scale >> SCHED_CAPACITY_SHIFT,
			  const_cnt - prev_const_cnt);

	scale = min_t(unsigned long, scale, SCHED_CAPACITY_SCALE);
	this_cpu_write(arch_freq_scale, (unsigned long)scale);

	amu_sample->last_scale_update = jiffies;
}

static struct scale_freq_data amu_sfd = {
	.source = SCALE_FREQ_SOURCE_ARCH,
	.set_freq_scale = amu_scale_freq_tick,
};

static __always_inline bool amu_fie_cpu_supported(unsigned int cpu)
{
	return cpumask_available(amu_fie_cpus) &&
		cpumask_test_cpu(cpu, amu_fie_cpus);
}

void arch_cpu_idle_enter(void)
{
	unsigned int cpu = smp_processor_id();

	if (!amu_fie_cpu_supported(cpu))
		return;

	/* Kick in AMU update but only if one has not happened already */
	if (housekeeping_cpu(cpu, HK_TYPE_TICK) &&
	    time_is_before_jiffies(per_cpu(cpu_amu_samples.last_scale_update, cpu)))
		amu_scale_freq_tick();
}

#define AMU_SAMPLE_EXP_MS	20

int arch_freq_get_on_cpu(int cpu)
{
	struct amu_cntr_sample *amu_sample;
	unsigned int start_cpu = cpu;
	unsigned long last_update;
	unsigned int freq = 0;
	u64 scale;

	if (!amu_fie_cpu_supported(cpu) || !arch_scale_freq_ref(cpu))
		return -EOPNOTSUPP;

	while (1) {

		amu_sample = per_cpu_ptr(&cpu_amu_samples, cpu);

		last_update = amu_sample->last_scale_update;

		/*
		 * For those CPUs that are in full dynticks mode, or those that have
		 * not seen tick for a while, try an alternative source for the counters
		 * (and thus freq scale), if available, for given policy: this boils
		 * down to identifying an active cpu within the same freq domain, if any.
		 */
		if (!housekeeping_cpu(cpu, HK_TYPE_TICK) ||
		    time_is_before_jiffies(last_update + msecs_to_jiffies(AMU_SAMPLE_EXP_MS))) {
			struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);
			int ref_cpu;

			if (!policy)
				return -EINVAL;

			if (!cpumask_intersects(policy->related_cpus,
						housekeeping_cpumask(HK_TYPE_TICK))) {
				cpufreq_cpu_put(policy);
				return -EOPNOTSUPP;
			}

			for_each_cpu_wrap(ref_cpu, policy->cpus, cpu + 1) {
				if (ref_cpu == start_cpu) {
					/* Prevent verifying same CPU twice */
					ref_cpu = nr_cpu_ids;
					break;
				}
				if (!idle_cpu(ref_cpu))
					break;
			}

			cpufreq_cpu_put(policy);

			if (ref_cpu >= nr_cpu_ids)
				/* No alternative to pull info from */
				return -EAGAIN;

			cpu = ref_cpu;
		} else {
			break;
		}
	}
	/*
	 * Reversed computation to the one used to determine
	 * the arch_freq_scale value
	 * (see amu_scale_freq_tick for details)
	 */
	scale = arch_scale_freq_capacity(cpu);
	freq = scale * arch_scale_freq_ref(cpu);
	freq >>= SCHED_CAPACITY_SHIFT;
	return freq;
}

static void amu_fie_setup(const struct cpumask *cpus)
{
	int cpu;

	/* We are already set since the last insmod of cpufreq driver */
	if (cpumask_available(amu_fie_cpus) &&
	    unlikely(cpumask_subset(cpus, amu_fie_cpus)))
		return;

	for_each_cpu(cpu, cpus)
		if (!freq_counters_valid(cpu))
			return;

	if (!cpumask_available(amu_fie_cpus) &&
	    !zalloc_cpumask_var(&amu_fie_cpus, GFP_KERNEL)) {
		WARN_ONCE(1, "Failed to allocate FIE cpumask for CPUs[%*pbl]\n",
			  cpumask_pr_args(cpus));
		return;
	}

	cpumask_or(amu_fie_cpus, amu_fie_cpus, cpus);

	topology_set_scale_freq_source(&amu_sfd, amu_fie_cpus);

	pr_debug("CPUs[%*pbl]: counters will be used for FIE.",
		 cpumask_pr_args(cpus));
}

static int init_amu_fie_callback(struct notifier_block *nb, unsigned long val,
				 void *data)
{
	struct cpufreq_policy *policy = data;

	if (val == CPUFREQ_CREATE_POLICY)
		amu_fie_setup(policy->related_cpus);

	/*
	 * We don't need to handle CPUFREQ_REMOVE_POLICY event as the AMU
	 * counters don't have any dependency on cpufreq driver once we have
	 * initialized AMU support and enabled invariance. The AMU counters will
	 * keep on working just fine in the absence of the cpufreq driver, and
	 * for the CPUs for which there are no counters available, the last set
	 * value of arch_freq_scale will remain valid as that is the frequency
	 * those CPUs are running at.
	 */

	return 0;
}

static struct notifier_block init_amu_fie_notifier = {
	.notifier_call = init_amu_fie_callback,
};

static int __init init_amu_fie(void)
{
	return cpufreq_register_notifier(&init_amu_fie_notifier,
					CPUFREQ_POLICY_NOTIFIER);
}
core_initcall(init_amu_fie);

#ifdef CONFIG_ACPI_CPPC_LIB
#include <acpi/cppc_acpi.h>

static void cpu_read_corecnt(void *val)
{
	/*
	 * A value of 0 can be returned if the current CPU does not support AMUs
	 * or if the counter is disabled for this CPU. A return value of 0 at
	 * counter read is properly handled as an error case by the users of the
	 * counter.
	 */
	*(u64 *)val = read_corecnt();
}

static void cpu_read_constcnt(void *val)
{
	/*
	 * Return 0 if the current CPU is affected by erratum 2457168. A value
	 * of 0 is also returned if the current CPU does not support AMUs or if
	 * the counter is disabled. A return value of 0 at counter read is
	 * properly handled as an error case by the users of the counter.
	 */
	*(u64 *)val = this_cpu_has_cap(ARM64_WORKAROUND_2457168) ?
		      0UL : read_constcnt();
}

static inline
int counters_read_on_cpu(int cpu, smp_call_func_t func, u64 *val)
{
	/*
	 * Abort call on counterless CPU or when interrupts are
	 * disabled - can lead to deadlock in smp sync call.
	 */
	if (!cpu_has_amu_feat(cpu))
		return -EOPNOTSUPP;

	if (WARN_ON_ONCE(irqs_disabled()))
		return -EPERM;

	smp_call_function_single(cpu, func, val, 1);

	return 0;
}

/*
 * Refer to drivers/acpi/cppc_acpi.c for the description of the functions
 * below.
 */
bool cpc_ffh_supported(void)
{
	int cpu = get_cpu_with_amu_feat();

	/*
	 * FFH is considered supported if there is at least one present CPU that
	 * supports AMUs. Using FFH to read core and reference counters for CPUs
	 * that do not support AMUs, have counters disabled or that are affected
	 * by errata, will result in a return value of 0.
	 *
	 * This is done to allow any enabled and valid counters to be read
	 * through FFH, knowing that potentially returning 0 as counter value is
	 * properly handled by the users of these counters.
	 */
	if ((cpu >= nr_cpu_ids) || !cpumask_test_cpu(cpu, cpu_present_mask))
		return false;

	return true;
}

int cpc_read_ffh(int cpu, struct cpc_reg *reg, u64 *val)
{
	int ret = -EOPNOTSUPP;

	switch ((u64)reg->address) {
	case 0x0:
		ret = counters_read_on_cpu(cpu, cpu_read_corecnt, val);
		break;
	case 0x1:
		ret = counters_read_on_cpu(cpu, cpu_read_constcnt, val);
		break;
	}

	if (!ret) {
		*val &= GENMASK_ULL(reg->bit_offset + reg->bit_width - 1,
				    reg->bit_offset);
		*val >>= reg->bit_offset;
	}

	return ret;
}

int cpc_write_ffh(int cpunum, struct cpc_reg *reg, u64 val)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_ACPI_CPPC_LIB */
