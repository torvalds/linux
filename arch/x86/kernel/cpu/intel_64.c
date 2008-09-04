#include <linux/init.h>
#include <linux/smp.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/topology.h>
#include <asm/numa_64.h>

#include "cpu.h"

static void __cpuinit early_init_intel(struct cpuinfo_x86 *c)
{
	if ((c->x86 == 0xf && c->x86_model >= 0x03) ||
	    (c->x86 == 0x6 && c->x86_model >= 0x0e))
		set_cpu_cap(c, X86_FEATURE_CONSTANT_TSC);

	set_cpu_cap(c, X86_FEATURE_SYSENTER32);
}

/*
 * find out the number of processor cores on the die
 */
static int __cpuinit intel_num_cpu_cores(struct cpuinfo_x86 *c)
{
	unsigned int eax, t;

	if (c->cpuid_level < 4)
		return 1;

	cpuid_count(4, 0, &eax, &t, &t, &t);

	if (eax & 0x1f)
		return ((eax >> 26) + 1);
	else
		return 1;
}

static void __cpuinit srat_detect_node(void)
{
#ifdef CONFIG_NUMA
	unsigned node;
	int cpu = smp_processor_id();
	int apicid = hard_smp_processor_id();

	/* Don't do the funky fallback heuristics the AMD version employs
	   for now. */
	node = apicid_to_node[apicid];
	if (node == NUMA_NO_NODE || !node_online(node))
		node = first_node(node_online_map);
	numa_set_node(cpu, node);

	printk(KERN_INFO "CPU %d/%x -> Node %d\n", cpu, apicid, node);
#endif
}

static void __cpuinit init_intel(struct cpuinfo_x86 *c)
{
	init_intel_cacheinfo(c);
	if (c->cpuid_level > 9) {
		unsigned eax = cpuid_eax(10);
		/* Check for version and the number of counters */
		if ((eax & 0xff) && (((eax>>8) & 0xff) > 1))
			set_cpu_cap(c, X86_FEATURE_ARCH_PERFMON);
	}

	if (cpu_has_ds) {
		unsigned int l1, l2;
		rdmsr(MSR_IA32_MISC_ENABLE, l1, l2);
		if (!(l1 & (1<<11)))
			set_cpu_cap(c, X86_FEATURE_BTS);
		if (!(l1 & (1<<12)))
			set_cpu_cap(c, X86_FEATURE_PEBS);
	}


	if (cpu_has_bts)
		ds_init_intel(c);

	if (c->x86 == 15)
		c->x86_cache_alignment = c->x86_clflush_size * 2;
	if (c->x86 == 6)
		set_cpu_cap(c, X86_FEATURE_REP_GOOD);
	set_cpu_cap(c, X86_FEATURE_LFENCE_RDTSC);
	c->x86_max_cores = intel_num_cpu_cores(c);

	srat_detect_node();
}

static struct cpu_dev intel_cpu_dev __cpuinitdata = {
	.c_vendor	= "Intel",
	.c_ident	= { "GenuineIntel" },
	.c_early_init   = early_init_intel,
	.c_init		= init_intel,
	.c_x86_vendor	= X86_VENDOR_INTEL,
};

cpu_dev_register(intel_cpu_dev);
