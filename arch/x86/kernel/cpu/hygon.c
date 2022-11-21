// SPDX-License-Identifier: GPL-2.0+
/*
 * Hygon Processor Support for Linux
 *
 * Copyright (C) 2018 Chengdu Haiguang IC Design Co., Ltd.
 *
 * Author: Pu Wen <puwen@hygon.cn>
 */
#include <linux/io.h>

#include <asm/cpu.h>
#include <asm/smp.h>
#include <asm/numa.h>
#include <asm/cacheinfo.h>
#include <asm/spec-ctrl.h>
#include <asm/delay.h>

#include "cpu.h"

#define APICID_SOCKET_ID_BIT 6

/*
 * nodes_per_socket: Stores the number of nodes per socket.
 * Refer to CPUID Fn8000_001E_ECX Node Identifiers[10:8]
 */
static u32 nodes_per_socket = 1;

#ifdef CONFIG_NUMA
/*
 * To workaround broken NUMA config.  Read the comment in
 * srat_detect_node().
 */
static int nearby_node(int apicid)
{
	int i, node;

	for (i = apicid - 1; i >= 0; i--) {
		node = __apicid_to_node[i];
		if (node != NUMA_NO_NODE && node_online(node))
			return node;
	}
	for (i = apicid + 1; i < MAX_LOCAL_APIC; i++) {
		node = __apicid_to_node[i];
		if (node != NUMA_NO_NODE && node_online(node))
			return node;
	}
	return first_node(node_online_map); /* Shouldn't happen */
}
#endif

static void hygon_get_topology_early(struct cpuinfo_x86 *c)
{
	if (cpu_has(c, X86_FEATURE_TOPOEXT))
		smp_num_siblings = ((cpuid_ebx(0x8000001e) >> 8) & 0xff) + 1;
}

/*
 * Fixup core topology information for
 * (1) Hygon multi-node processors
 *     Assumption: Number of cores in each internal node is the same.
 * (2) Hygon processors supporting compute units
 */
static void hygon_get_topology(struct cpuinfo_x86 *c)
{
	int cpu = smp_processor_id();

	/* get information required for multi-node processors */
	if (boot_cpu_has(X86_FEATURE_TOPOEXT)) {
		int err;
		u32 eax, ebx, ecx, edx;

		cpuid(0x8000001e, &eax, &ebx, &ecx, &edx);

		c->cpu_die_id  = ecx & 0xff;

		c->cpu_core_id = ebx & 0xff;

		if (smp_num_siblings > 1)
			c->x86_max_cores /= smp_num_siblings;

		/*
		 * In case leaf B is available, use it to derive
		 * topology information.
		 */
		err = detect_extended_topology(c);
		if (!err)
			c->x86_coreid_bits = get_count_order(c->x86_max_cores);

		/* Socket ID is ApicId[6] for these processors. */
		c->phys_proc_id = c->apicid >> APICID_SOCKET_ID_BIT;

		cacheinfo_hygon_init_llc_id(c, cpu);
	} else if (cpu_has(c, X86_FEATURE_NODEID_MSR)) {
		u64 value;

		rdmsrl(MSR_FAM10H_NODE_ID, value);
		c->cpu_die_id = value & 7;

		per_cpu(cpu_llc_id, cpu) = c->cpu_die_id;
	} else
		return;

	if (nodes_per_socket > 1)
		set_cpu_cap(c, X86_FEATURE_AMD_DCM);
}

/*
 * On Hygon setup the lower bits of the APIC id distinguish the cores.
 * Assumes number of cores is a power of two.
 */
static void hygon_detect_cmp(struct cpuinfo_x86 *c)
{
	unsigned int bits;
	int cpu = smp_processor_id();

	bits = c->x86_coreid_bits;
	/* Low order bits define the core id (index of core in socket) */
	c->cpu_core_id = c->initial_apicid & ((1 << bits)-1);
	/* Convert the initial APIC ID into the socket ID */
	c->phys_proc_id = c->initial_apicid >> bits;
	/* use socket ID also for last level cache */
	per_cpu(cpu_llc_id, cpu) = c->cpu_die_id = c->phys_proc_id;
}

static void srat_detect_node(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_NUMA
	int cpu = smp_processor_id();
	int node;
	unsigned int apicid = c->apicid;

	node = numa_cpu_node(cpu);
	if (node == NUMA_NO_NODE)
		node = per_cpu(cpu_llc_id, cpu);

	/*
	 * On multi-fabric platform (e.g. Numascale NumaChip) a
	 * platform-specific handler needs to be called to fixup some
	 * IDs of the CPU.
	 */
	if (x86_cpuinit.fixup_cpu_id)
		x86_cpuinit.fixup_cpu_id(c, node);

	if (!node_online(node)) {
		/*
		 * Two possibilities here:
		 *
		 * - The CPU is missing memory and no node was created.  In
		 *   that case try picking one from a nearby CPU.
		 *
		 * - The APIC IDs differ from the HyperTransport node IDs.
		 *   Assume they are all increased by a constant offset, but
		 *   in the same order as the HT nodeids.  If that doesn't
		 *   result in a usable node fall back to the path for the
		 *   previous case.
		 *
		 * This workaround operates directly on the mapping between
		 * APIC ID and NUMA node, assuming certain relationship
		 * between APIC ID, HT node ID and NUMA topology.  As going
		 * through CPU mapping may alter the outcome, directly
		 * access __apicid_to_node[].
		 */
		int ht_nodeid = c->initial_apicid;

		if (__apicid_to_node[ht_nodeid] != NUMA_NO_NODE)
			node = __apicid_to_node[ht_nodeid];
		/* Pick a nearby node */
		if (!node_online(node))
			node = nearby_node(apicid);
	}
	numa_set_node(cpu, node);
#endif
}

static void early_init_hygon_mc(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_SMP
	unsigned int bits, ecx;

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

static void bsp_init_hygon(struct cpuinfo_x86 *c)
{
	if (cpu_has(c, X86_FEATURE_CONSTANT_TSC)) {
		u64 val;

		rdmsrl(MSR_K7_HWCR, val);
		if (!(val & BIT(24)))
			pr_warn(FW_BUG "TSC doesn't count with P0 frequency!\n");
	}

	if (cpu_has(c, X86_FEATURE_MWAITX))
		use_mwaitx_delay();

	if (boot_cpu_has(X86_FEATURE_TOPOEXT)) {
		u32 ecx;

		ecx = cpuid_ecx(0x8000001e);
		__max_die_per_package = nodes_per_socket = ((ecx >> 8) & 7) + 1;
	} else if (boot_cpu_has(X86_FEATURE_NODEID_MSR)) {
		u64 value;

		rdmsrl(MSR_FAM10H_NODE_ID, value);
		__max_die_per_package = nodes_per_socket = ((value >> 3) & 7) + 1;
	}

	if (!boot_cpu_has(X86_FEATURE_AMD_SSBD) &&
	    !boot_cpu_has(X86_FEATURE_VIRT_SSBD)) {
		/*
		 * Try to cache the base value so further operations can
		 * avoid RMW. If that faults, do not enable SSBD.
		 */
		if (!rdmsrl_safe(MSR_AMD64_LS_CFG, &x86_amd_ls_cfg_base)) {
			setup_force_cpu_cap(X86_FEATURE_LS_CFG_SSBD);
			setup_force_cpu_cap(X86_FEATURE_SSBD);
			x86_amd_ls_cfg_ssbd_mask = 1ULL << 10;
		}
	}
}

static void early_init_hygon(struct cpuinfo_x86 *c)
{
	u32 dummy;

	early_init_hygon_mc(c);

	set_cpu_cap(c, X86_FEATURE_K8);

	rdmsr_safe(MSR_AMD64_PATCH_LEVEL, &c->microcode, &dummy);

	/*
	 * c->x86_power is 8000_0007 edx. Bit 8 is TSC runs at constant rate
	 * with P/T states and does not stop in deep C-states
	 */
	if (c->x86_power & (1 << 8)) {
		set_cpu_cap(c, X86_FEATURE_CONSTANT_TSC);
		set_cpu_cap(c, X86_FEATURE_NONSTOP_TSC);
	}

	/* Bit 12 of 8000_0007 edx is accumulated power mechanism. */
	if (c->x86_power & BIT(12))
		set_cpu_cap(c, X86_FEATURE_ACC_POWER);

	/* Bit 14 indicates the Runtime Average Power Limit interface. */
	if (c->x86_power & BIT(14))
		set_cpu_cap(c, X86_FEATURE_RAPL);

#ifdef CONFIG_X86_64
	set_cpu_cap(c, X86_FEATURE_SYSCALL32);
#endif

#if defined(CONFIG_X86_LOCAL_APIC) && defined(CONFIG_PCI)
	/*
	 * ApicID can always be treated as an 8-bit value for Hygon APIC So, we
	 * can safely set X86_FEATURE_EXTD_APICID unconditionally.
	 */
	if (boot_cpu_has(X86_FEATURE_APIC))
		set_cpu_cap(c, X86_FEATURE_EXTD_APICID);
#endif

	/*
	 * This is only needed to tell the kernel whether to use VMCALL
	 * and VMMCALL.  VMMCALL is never executed except under virt, so
	 * we can set it unconditionally.
	 */
	set_cpu_cap(c, X86_FEATURE_VMMCALL);

	hygon_get_topology_early(c);
}

static void init_hygon(struct cpuinfo_x86 *c)
{
	early_init_hygon(c);

	/*
	 * Bit 31 in normal CPUID used for nonstandard 3DNow ID;
	 * 3DNow is IDd by bit 31 in extended CPUID (1*32+31) anyway
	 */
	clear_cpu_cap(c, 0*32+31);

	set_cpu_cap(c, X86_FEATURE_REP_GOOD);

	/* get apicid instead of initial apic id from cpuid */
	c->apicid = hard_smp_processor_id();

	set_cpu_cap(c, X86_FEATURE_ZEN);
	set_cpu_cap(c, X86_FEATURE_CPB);

	cpu_detect_cache_sizes(c);

	hygon_detect_cmp(c);
	hygon_get_topology(c);
	srat_detect_node(c);

	init_hygon_cacheinfo(c);

	if (cpu_has(c, X86_FEATURE_XMM2)) {
		/*
		 * Use LFENCE for execution serialization.  On families which
		 * don't have that MSR, LFENCE is already serializing.
		 * msr_set_bit() uses the safe accessors, too, even if the MSR
		 * is not present.
		 */
		msr_set_bit(MSR_F10H_DECFG,
			    MSR_F10H_DECFG_LFENCE_SERIALIZE_BIT);

		/* A serializing LFENCE stops RDTSC speculation */
		set_cpu_cap(c, X86_FEATURE_LFENCE_RDTSC);
	}

	/*
	 * Hygon processors have APIC timer running in deep C states.
	 */
	set_cpu_cap(c, X86_FEATURE_ARAT);

	/* Hygon CPUs don't reset SS attributes on SYSRET, Xen does. */
	if (!cpu_has(c, X86_FEATURE_XENPV))
		set_cpu_bug(c, X86_BUG_SYSRET_SS_ATTRS);

	check_null_seg_clears_base(c);
}

static void cpu_detect_tlb_hygon(struct cpuinfo_x86 *c)
{
	u32 ebx, eax, ecx, edx;
	u16 mask = 0xfff;

	if (c->extended_cpuid_level < 0x80000006)
		return;

	cpuid(0x80000006, &eax, &ebx, &ecx, &edx);

	tlb_lld_4k[ENTRIES] = (ebx >> 16) & mask;
	tlb_lli_4k[ENTRIES] = ebx & mask;

	/* Handle DTLB 2M and 4M sizes, fall back to L1 if L2 is disabled */
	if (!((eax >> 16) & mask))
		tlb_lld_2m[ENTRIES] = (cpuid_eax(0x80000005) >> 16) & 0xff;
	else
		tlb_lld_2m[ENTRIES] = (eax >> 16) & mask;

	/* a 4M entry uses two 2M entries */
	tlb_lld_4m[ENTRIES] = tlb_lld_2m[ENTRIES] >> 1;

	/* Handle ITLB 2M and 4M sizes, fall back to L1 if L2 is disabled */
	if (!(eax & mask)) {
		cpuid(0x80000005, &eax, &ebx, &ecx, &edx);
		tlb_lli_2m[ENTRIES] = eax & 0xff;
	} else
		tlb_lli_2m[ENTRIES] = eax & mask;

	tlb_lli_4m[ENTRIES] = tlb_lli_2m[ENTRIES] >> 1;
}

static const struct cpu_dev hygon_cpu_dev = {
	.c_vendor	= "Hygon",
	.c_ident	= { "HygonGenuine" },
	.c_early_init   = early_init_hygon,
	.c_detect_tlb	= cpu_detect_tlb_hygon,
	.c_bsp_init	= bsp_init_hygon,
	.c_init		= init_hygon,
	.c_x86_vendor	= X86_VENDOR_HYGON,
};

cpu_dev_register(hygon_cpu_dev);
