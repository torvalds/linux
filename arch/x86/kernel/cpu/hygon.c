// SPDX-License-Identifier: GPL-2.0+
/*
 * Hygon Processor Support for Linux
 *
 * Copyright (C) 2018 Chengdu Haiguang IC Design Co., Ltd.
 *
 * Author: Pu Wen <puwen@hygon.cn>
 */
#include <linux/io.h>

#include <asm/apic.h>
#include <asm/cpu.h>
#include <asm/smp.h>
#include <asm/numa.h>
#include <asm/cacheinfo.h>
#include <asm/spec-ctrl.h>
#include <asm/delay.h>
#include <asm/msr.h>
#include <asm/resctrl.h>

#include "cpu.h"

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

static void srat_detect_node(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_NUMA
	int cpu = smp_processor_id();
	int node;
	unsigned int apicid = c->topo.apicid;

	node = numa_cpu_node(cpu);
	if (node == NUMA_NO_NODE)
		node = c->topo.llc_id;

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
		int ht_nodeid = c->topo.initial_apicid;

		if (__apicid_to_node[ht_nodeid] != NUMA_NO_NODE)
			node = __apicid_to_node[ht_nodeid];
		/* Pick a nearby node */
		if (!node_online(node))
			node = nearby_node(apicid);
	}
	numa_set_node(cpu, node);
#endif
}

static void bsp_init_hygon(struct cpuinfo_x86 *c)
{
	if (cpu_has(c, X86_FEATURE_CONSTANT_TSC)) {
		u64 val;

		rdmsrq(MSR_K7_HWCR, val);
		if (!(val & BIT(24)))
			pr_warn(FW_BUG "TSC doesn't count with P0 frequency!\n");
	}

	if (cpu_has(c, X86_FEATURE_MWAITX))
		use_mwaitx_delay();

	if (!boot_cpu_has(X86_FEATURE_AMD_SSBD) &&
	    !boot_cpu_has(X86_FEATURE_VIRT_SSBD)) {
		/*
		 * Try to cache the base value so further operations can
		 * avoid RMW. If that faults, do not enable SSBD.
		 */
		if (!rdmsrq_safe(MSR_AMD64_LS_CFG, &x86_amd_ls_cfg_base)) {
			setup_force_cpu_cap(X86_FEATURE_LS_CFG_SSBD);
			setup_force_cpu_cap(X86_FEATURE_SSBD);
			x86_amd_ls_cfg_ssbd_mask = 1ULL << 10;
		}
	}

	resctrl_cpu_detect(c);
}

static void early_init_hygon(struct cpuinfo_x86 *c)
{
	u32 dummy;

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
}

static void init_hygon(struct cpuinfo_x86 *c)
{
	u64 vm_cr;

	early_init_hygon(c);

	/*
	 * Bit 31 in normal CPUID used for nonstandard 3DNow ID;
	 * 3DNow is IDd by bit 31 in extended CPUID (1*32+31) anyway
	 */
	clear_cpu_cap(c, 0*32+31);

	set_cpu_cap(c, X86_FEATURE_REP_GOOD);

	/*
	 * XXX someone from Hygon needs to confirm this DTRT
	 *
	init_spectral_chicken(c);
	 */

	set_cpu_cap(c, X86_FEATURE_ZEN);
	set_cpu_cap(c, X86_FEATURE_CPB);

	cpu_detect_cache_sizes(c);

	srat_detect_node(c);

	init_hygon_cacheinfo(c);

	if (cpu_has(c, X86_FEATURE_SVM)) {
		rdmsrq(MSR_VM_CR, vm_cr);
		if (vm_cr & SVM_VM_CR_SVM_DIS_MASK) {
			pr_notice_once("SVM disabled (by BIOS) in MSR_VM_CR\n");
			clear_cpu_cap(c, X86_FEATURE_SVM);
		}
	}

	if (cpu_has(c, X86_FEATURE_XMM2)) {
		/*
		 * Use LFENCE for execution serialization.  On families which
		 * don't have that MSR, LFENCE is already serializing.
		 * msr_set_bit() uses the safe accessors, too, even if the MSR
		 * is not present.
		 */
		msr_set_bit(MSR_AMD64_DE_CFG,
			    MSR_AMD64_DE_CFG_LFENCE_SERIALIZE_BIT);

		/* A serializing LFENCE stops RDTSC speculation */
		set_cpu_cap(c, X86_FEATURE_LFENCE_RDTSC);
	}

	/*
	 * Hygon processors have APIC timer running in deep C states.
	 */
	set_cpu_cap(c, X86_FEATURE_ARAT);

	/* Hygon CPUs don't reset SS attributes on SYSRET, Xen does. */
	if (!cpu_feature_enabled(X86_FEATURE_XENPV))
		set_cpu_bug(c, X86_BUG_SYSRET_SS_ATTRS);

	check_null_seg_clears_base(c);

	/* Hygon CPUs don't need fencing after x2APIC/TSC_DEADLINE MSR writes. */
	clear_cpu_cap(c, X86_FEATURE_APIC_MSRS_FENCE);
}

static void cpu_detect_tlb_hygon(struct cpuinfo_x86 *c)
{
	u32 ebx, eax, ecx, edx;
	u16 mask = 0xfff;

	if (c->extended_cpuid_level < 0x80000006)
		return;

	cpuid(0x80000006, &eax, &ebx, &ecx, &edx);

	tlb_lld_4k = (ebx >> 16) & mask;
	tlb_lli_4k = ebx & mask;

	/* Handle DTLB 2M and 4M sizes, fall back to L1 if L2 is disabled */
	if (!((eax >> 16) & mask))
		tlb_lld_2m = (cpuid_eax(0x80000005) >> 16) & 0xff;
	else
		tlb_lld_2m = (eax >> 16) & mask;

	/* a 4M entry uses two 2M entries */
	tlb_lld_4m = tlb_lld_2m >> 1;

	/* Handle ITLB 2M and 4M sizes, fall back to L1 if L2 is disabled */
	if (!(eax & mask)) {
		cpuid(0x80000005, &eax, &ebx, &ecx, &edx);
		tlb_lli_2m = eax & 0xff;
	} else
		tlb_lli_2m = eax & mask;

	tlb_lli_4m = tlb_lli_2m >> 1;
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
