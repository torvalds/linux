#include <linux/init.h>
#include <linux/smp.h>
#include <linux/module.h>
#include <linux/sched.h>

/* Number of siblings per CPU package */
int smp_num_siblings = 1;
EXPORT_SYMBOL(smp_num_siblings);

/* Last level cache ID of each logical CPU */
DEFINE_PER_CPU(u16, cpu_llc_id) = BAD_APICID;

/* bitmap of online cpus */
cpumask_t cpu_online_map __read_mostly;
EXPORT_SYMBOL(cpu_online_map);

cpumask_t cpu_callin_map;
cpumask_t cpu_callout_map;
cpumask_t cpu_possible_map;
EXPORT_SYMBOL(cpu_possible_map);

/* representing HT siblings of each logical CPU */
DEFINE_PER_CPU(cpumask_t, cpu_sibling_map);
EXPORT_PER_CPU_SYMBOL(cpu_sibling_map);

/* representing HT and core siblings of each logical CPU */
DEFINE_PER_CPU(cpumask_t, cpu_core_map);
EXPORT_PER_CPU_SYMBOL(cpu_core_map);

/* Per CPU bogomips and other parameters */
DEFINE_PER_CPU_SHARED_ALIGNED(struct cpuinfo_x86, cpu_info);
EXPORT_PER_CPU_SYMBOL(cpu_info);

/* representing cpus for which sibling maps can be computed */
static cpumask_t cpu_sibling_setup_map;

void __cpuinit set_cpu_sibling_map(int cpu)
{
	int i;
	struct cpuinfo_x86 *c = &cpu_data(cpu);

	cpu_set(cpu, cpu_sibling_setup_map);

	if (smp_num_siblings > 1) {
		for_each_cpu_mask(i, cpu_sibling_setup_map) {
			if (c->phys_proc_id == cpu_data(i).phys_proc_id &&
			    c->cpu_core_id == cpu_data(i).cpu_core_id) {
				cpu_set(i, per_cpu(cpu_sibling_map, cpu));
				cpu_set(cpu, per_cpu(cpu_sibling_map, i));
				cpu_set(i, per_cpu(cpu_core_map, cpu));
				cpu_set(cpu, per_cpu(cpu_core_map, i));
				cpu_set(i, c->llc_shared_map);
				cpu_set(cpu, cpu_data(i).llc_shared_map);
			}
		}
	} else {
		cpu_set(cpu, per_cpu(cpu_sibling_map, cpu));
	}

	cpu_set(cpu, c->llc_shared_map);

	if (current_cpu_data.x86_max_cores == 1) {
		per_cpu(cpu_core_map, cpu) = per_cpu(cpu_sibling_map, cpu);
		c->booted_cores = 1;
		return;
	}

	for_each_cpu_mask(i, cpu_sibling_setup_map) {
		if (per_cpu(cpu_llc_id, cpu) != BAD_APICID &&
		    per_cpu(cpu_llc_id, cpu) == per_cpu(cpu_llc_id, i)) {
			cpu_set(i, c->llc_shared_map);
			cpu_set(cpu, cpu_data(i).llc_shared_map);
		}
		if (c->phys_proc_id == cpu_data(i).phys_proc_id) {
			cpu_set(i, per_cpu(cpu_core_map, cpu));
			cpu_set(cpu, per_cpu(cpu_core_map, i));
			/*
			 *  Does this new cpu bringup a new core?
			 */
			if (cpus_weight(per_cpu(cpu_sibling_map, cpu)) == 1) {
				/*
				 * for each core in package, increment
				 * the booted_cores for this new cpu
				 */
				if (first_cpu(per_cpu(cpu_sibling_map, i)) == i)
					c->booted_cores++;
				/*
				 * increment the core count for all
				 * the other cpus in this package
				 */
				if (i != cpu)
					cpu_data(i).booted_cores++;
			} else if (i != cpu && !c->booted_cores)
				c->booted_cores = cpu_data(i).booted_cores;
		}
	}
}

/* maps the cpu to the sched domain representing multi-core */
cpumask_t cpu_coregroup_map(int cpu)
{
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	/*
	 * For perf, we return last level cache shared map.
	 * And for power savings, we return cpu_core_map
	 */
	if (sched_mc_power_savings || sched_smt_power_savings)
		return per_cpu(cpu_core_map, cpu);
	else
		return c->llc_shared_map;
}


#ifdef CONFIG_HOTPLUG_CPU
void remove_siblinginfo(int cpu)
{
	int sibling;
	struct cpuinfo_x86 *c = &cpu_data(cpu);

	for_each_cpu_mask(sibling, per_cpu(cpu_core_map, cpu)) {
		cpu_clear(cpu, per_cpu(cpu_core_map, sibling));
		/*/
		 * last thread sibling in this cpu core going down
		 */
		if (cpus_weight(per_cpu(cpu_sibling_map, cpu)) == 1)
			cpu_data(sibling).booted_cores--;
	}

	for_each_cpu_mask(sibling, per_cpu(cpu_sibling_map, cpu))
		cpu_clear(cpu, per_cpu(cpu_sibling_map, sibling));
	cpus_clear(per_cpu(cpu_sibling_map, cpu));
	cpus_clear(per_cpu(cpu_core_map, cpu));
	c->phys_proc_id = 0;
	c->cpu_core_id = 0;
	cpu_clear(cpu, cpu_sibling_setup_map);
}

int additional_cpus __initdata = -1;

static __init int setup_additional_cpus(char *s)
{
	return s && get_option(&s, &additional_cpus) ? 0 : -EINVAL;
}
early_param("additional_cpus", setup_additional_cpus);

/*
 * cpu_possible_map should be static, it cannot change as cpu's
 * are onlined, or offlined. The reason is per-cpu data-structures
 * are allocated by some modules at init time, and dont expect to
 * do this dynamically on cpu arrival/departure.
 * cpu_present_map on the other hand can change dynamically.
 * In case when cpu_hotplug is not compiled, then we resort to current
 * behaviour, which is cpu_possible == cpu_present.
 * - Ashok Raj
 *
 * Three ways to find out the number of additional hotplug CPUs:
 * - If the BIOS specified disabled CPUs in ACPI/mptables use that.
 * - The user can overwrite it with additional_cpus=NUM
 * - Otherwise don't reserve additional CPUs.
 * We do this because additional CPUs waste a lot of memory.
 * -AK
 */
__init void prefill_possible_map(void)
{
	int i;
	int possible;

	if (additional_cpus == -1) {
		if (disabled_cpus > 0)
			additional_cpus = disabled_cpus;
		else
			additional_cpus = 0;
	}
	possible = num_processors + additional_cpus;
	if (possible > NR_CPUS)
		possible = NR_CPUS;

	printk(KERN_INFO "SMP: Allowing %d CPUs, %d hotplug CPUs\n",
		possible, max_t(int, possible - num_processors, 0));

	for (i = 0; i < possible; i++)
		cpu_set(i, cpu_possible_map);
}
#endif

