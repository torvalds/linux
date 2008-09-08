#include <linux/init.h>
#include <linux/mm.h>

#include <asm/numa_64.h>
#include <asm/mmconfig.h>
#include <asm/cacheflush.h>

#include <mach_apic.h>

#include "cpu.h"

#ifdef CONFIG_NUMA
static int __cpuinit nearby_node(int apicid)
{
	int i, node;

	for (i = apicid - 1; i >= 0; i--) {
		node = apicid_to_node[i];
		if (node != NUMA_NO_NODE && node_online(node))
			return node;
	}
	for (i = apicid + 1; i < MAX_LOCAL_APIC; i++) {
		node = apicid_to_node[i];
		if (node != NUMA_NO_NODE && node_online(node))
			return node;
	}
	return first_node(node_online_map); /* Shouldn't happen */
}
#endif

/*
 * On a AMD dual core setup the lower bits of the APIC id distingush the cores.
 * Assumes number of cores is a power of two.
 */
static void __cpuinit amd_detect_cmp(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_SMP
	unsigned bits;

	bits = c->x86_coreid_bits;

	/* Low order bits define the core id (index of core in socket) */
	c->cpu_core_id = c->initial_apicid & ((1 << bits)-1);
	/* Convert the initial APIC ID into the socket ID */
	c->phys_proc_id = c->initial_apicid >> bits;
#endif
}

static void __cpuinit srat_detect_node(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_NUMA
	int cpu = smp_processor_id();
	int node;
	unsigned apicid = hard_smp_processor_id();

	node = c->phys_proc_id;
	if (apicid_to_node[apicid] != NUMA_NO_NODE)
		node = apicid_to_node[apicid];
	if (!node_online(node)) {
		/* Two possibilities here:
		   - The CPU is missing memory and no node was created.
		   In that case try picking one from a nearby CPU
		   - The APIC IDs differ from the HyperTransport node IDs
		   which the K8 northbridge parsing fills in.
		   Assume they are all increased by a constant offset,
		   but in the same order as the HT nodeids.
		   If that doesn't result in a usable node fall back to the
		   path for the previous case.  */

		int ht_nodeid = c->initial_apicid;

		if (ht_nodeid >= 0 &&
		    apicid_to_node[ht_nodeid] != NUMA_NO_NODE)
			node = apicid_to_node[ht_nodeid];
		/* Pick a nearby node */
		if (!node_online(node))
			node = nearby_node(apicid);
	}
	numa_set_node(cpu, node);

	printk(KERN_INFO "CPU %d/%x -> Node %d\n", cpu, apicid, node);
#endif
}

static void __cpuinit early_init_amd_mc(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_SMP
	unsigned bits, ecx;

	/* Multi core CPU? */
	if (c->extended_cpuid_level < 0x80000008)
		return;

	ecx = cpuid_ecx(0x80000008);

	c->x86_max_cores = (ecx & 0xff) + 1;

	/* CPU telling us the core id bits shift? */
	bits = (ecx >> 12) & 0xF;

	/* Otherwise recompute */
	if (bits == 0) {
		while ((1 << bits) < c->x86_max_cores)
			bits++;
	}

	c->x86_coreid_bits = bits;

#endif
}

static void __cpuinit early_init_amd(struct cpuinfo_x86 *c)
{
	early_init_amd_mc(c);

	/* c->x86_power is 8000_0007 edx. Bit 8 is constant TSC */
	if (c->x86_power & (1<<8))
		set_cpu_cap(c, X86_FEATURE_CONSTANT_TSC);

	set_cpu_cap(c, X86_FEATURE_SYSCALL32);
}

static void __cpuinit init_amd(struct cpuinfo_x86 *c)
{
	unsigned level;

#ifdef CONFIG_SMP
	unsigned long value;

	/*
	 * Disable TLB flush filter by setting HWCR.FFDIS on K8
	 * bit 6 of msr C001_0015
	 *
	 * Errata 63 for SH-B3 steppings
	 * Errata 122 for all steppings (F+ have it disabled by default)
	 */
	if (c->x86 == 0xf) {
		rdmsrl(MSR_K8_HWCR, value);
		value |= 1 << 6;
		wrmsrl(MSR_K8_HWCR, value);
	}
#endif

	early_init_amd(c);

	/* Bit 31 in normal CPUID used for nonstandard 3DNow ID;
	   3DNow is IDd by bit 31 in extended CPUID (1*32+31) anyway */
	clear_cpu_cap(c, 0*32+31);

	/* On C+ stepping K8 rep microcode works well for copy/memset */
	if (c->x86 == 0xf) {
		level = cpuid_eax(1);
		if((level >= 0x0f48 && level < 0x0f50) || level >= 0x0f58)
			set_cpu_cap(c, X86_FEATURE_REP_GOOD);
	}
	if (c->x86 == 0x10 || c->x86 == 0x11)
		set_cpu_cap(c, X86_FEATURE_REP_GOOD);

	/* Enable workaround for FXSAVE leak */
	if (c->x86 >= 6)
		set_cpu_cap(c, X86_FEATURE_FXSAVE_LEAK);

	if (!c->x86_model_id[0]) {
		switch (c->x86) {
		case 0xf:
			/* Should distinguish Models here, but this is only
			   a fallback anyways. */
			strcpy(c->x86_model_id, "Hammer");
			break;
		}
	}
	display_cacheinfo(c);

	/* Multi core CPU? */
	if (c->extended_cpuid_level >= 0x80000008) {
		amd_detect_cmp(c);
		srat_detect_node(c);
	}

	if (c->extended_cpuid_level >= 0x80000006) {
		if ((c->x86 >= 0x0f) && (cpuid_edx(0x80000006) & 0xf000))
			num_cache_leaves = 4;
		else
			num_cache_leaves = 3;
	}

	if (c->x86 >= 0xf && c->x86 <= 0x11)
		set_cpu_cap(c, X86_FEATURE_K8);

	if (cpu_has_xmm2) {
		/* MFENCE stops RDTSC speculation */
		set_cpu_cap(c, X86_FEATURE_MFENCE_RDTSC);
	}

	if (c->x86 == 0x10) {
		/* do this for boot cpu */
		if (c == &boot_cpu_data)
			check_enable_amd_mmconf_dmi();

		fam10h_check_enable_mmcfg();
	}

	if (c == &boot_cpu_data && c->x86 >= 0xf && c->x86 <= 0x11) {
		unsigned long long tseg;

		/*
		 * Split up direct mapping around the TSEG SMM area.
		 * Don't do it for gbpages because there seems very little
		 * benefit in doing so.
		 */
		if (!rdmsrl_safe(MSR_K8_TSEG_ADDR, &tseg)) {
		    printk(KERN_DEBUG "tseg: %010llx\n", tseg);
		    if ((tseg>>PMD_SHIFT) <
				(max_low_pfn_mapped>>(PMD_SHIFT-PAGE_SHIFT)) ||
			((tseg>>PMD_SHIFT) <
				(max_pfn_mapped>>(PMD_SHIFT-PAGE_SHIFT)) &&
			 (tseg>>PMD_SHIFT) >= (1ULL<<(32 - PMD_SHIFT))))
			set_memory_4k((unsigned long)__va(tseg), 1);
		}
	}
}

static struct cpu_dev amd_cpu_dev __cpuinitdata = {
	.c_vendor	= "AMD",
	.c_ident	= { "AuthenticAMD" },
	.c_early_init   = early_init_amd,
	.c_init		= init_amd,
	.c_x86_vendor	= X86_VENDOR_AMD,
};

cpu_dev_register(amd_cpu_dev);
