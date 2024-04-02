// SPDX-License-Identifier: GPL-2.0
#include <linux/cpu.h>

#include <asm/apic.h>
#include <asm/memtype.h>
#include <asm/processor.h>

#include "cpu.h"

static bool parse_8000_0008(struct topo_scan *tscan)
{
	struct {
		// ecx
		u32	cpu_nthreads		:  8, // Number of physical threads - 1
						:  4, // Reserved
			apicid_coreid_len	:  4, // Number of thread core ID bits (shift) in APIC ID
			perf_tsc_len		:  2, // Performance time-stamp counter size
						: 14; // Reserved
	} ecx;
	unsigned int sft;

	if (tscan->c->extended_cpuid_level < 0x80000008)
		return false;

	cpuid_leaf_reg(0x80000008, CPUID_ECX, &ecx);

	/* If the thread bits are 0, then get the shift value from ecx.cpu_nthreads */
	sft = ecx.apicid_coreid_len;
	if (!sft)
		sft = get_count_order(ecx.cpu_nthreads + 1);

	topology_set_dom(tscan, TOPO_SMT_DOMAIN, sft, ecx.cpu_nthreads + 1);
	return true;
}

static void store_node(struct topo_scan *tscan, unsigned int nr_nodes, u16 node_id)
{
	/*
	 * Starting with Fam 17h the DIE domain could probably be used to
	 * retrieve the node info on AMD/HYGON. Analysis of CPUID dumps
	 * suggests it's the topmost bit(s) of the CPU cores area, but
	 * that's guess work and neither enumerated nor documented.
	 *
	 * Up to Fam 16h this does not work at all and the legacy node ID
	 * has to be used.
	 */
	tscan->amd_nodes_per_pkg = nr_nodes;
	tscan->amd_node_id = node_id;
}

static bool parse_8000_001e(struct topo_scan *tscan, bool has_0xb)
{
	struct {
		// eax
		u32	ext_apic_id		: 32; // Extended APIC ID
		// ebx
		u32	core_id			:  8, // Unique per-socket logical core unit ID
			core_nthreads		:  8, // #Threads per core (zero-based)
						: 16; // Reserved
		// ecx
		u32	node_id			:  8, // Node (die) ID of invoking logical CPU
			nnodes_per_socket	:  3, // #nodes in invoking logical CPU's package/socket
						: 21; // Reserved
		// edx
		u32				: 32; // Reserved
	} leaf;

	if (!boot_cpu_has(X86_FEATURE_TOPOEXT))
		return false;

	cpuid_leaf(0x8000001e, &leaf);

	tscan->c->topo.initial_apicid = leaf.ext_apic_id;

	/*
	 * If leaf 0xb is available, then SMT shift is set already. If not
	 * take it from ecx.threads_per_core and use topo_update_dom() -
	 * topology_set_dom() would propagate and overwrite the already
	 * propagated CORE level.
	 */
	if (!has_0xb) {
		unsigned int nthreads = leaf.core_nthreads + 1;

		topology_update_dom(tscan, TOPO_SMT_DOMAIN, get_count_order(nthreads), nthreads);
	}

	store_node(tscan, leaf.nnodes_per_socket + 1, leaf.node_id);

	if (tscan->c->x86_vendor == X86_VENDOR_AMD) {
		if (tscan->c->x86 == 0x15)
			tscan->c->topo.cu_id = leaf.core_id;

		cacheinfo_amd_init_llc_id(tscan->c, leaf.node_id);
	} else {
		/*
		 * Package ID is ApicId[6..] on certain Hygon CPUs. See
		 * commit e0ceeae708ce for explanation. The topology info
		 * is screwed up: The package shift is always 6 and the
		 * node ID is bit [4:5].
		 */
		if (!boot_cpu_has(X86_FEATURE_HYPERVISOR) && tscan->c->x86_model <= 0x3) {
			topology_set_dom(tscan, TOPO_CORE_DOMAIN, 6,
					 tscan->dom_ncpus[TOPO_CORE_DOMAIN]);
		}
		cacheinfo_hygon_init_llc_id(tscan->c);
	}
	return true;
}

static bool parse_fam10h_node_id(struct topo_scan *tscan)
{
	struct {
		union {
			u64	node_id		:  3,
				nodes_per_pkg	:  3,
				unused		: 58;
			u64	msr;
		};
	} nid;

	if (!boot_cpu_has(X86_FEATURE_NODEID_MSR))
		return false;

	rdmsrl(MSR_FAM10H_NODE_ID, nid.msr);
	store_node(tscan, nid.nodes_per_pkg + 1, nid.node_id);
	tscan->c->topo.llc_id = nid.node_id;
	return true;
}

static void legacy_set_llc(struct topo_scan *tscan)
{
	unsigned int apicid = tscan->c->topo.initial_apicid;

	/* parse_8000_0008() set everything up except llc_id */
	tscan->c->topo.llc_id = apicid >> tscan->dom_shifts[TOPO_CORE_DOMAIN];
}

static void parse_topology_amd(struct topo_scan *tscan)
{
	bool has_0xb = false;

	/*
	 * If the extended topology leaf 0x8000_001e is available
	 * try to get SMT and CORE shift from leaf 0xb first, then
	 * try to get the CORE shift from leaf 0x8000_0008.
	 */
	if (cpu_feature_enabled(X86_FEATURE_TOPOEXT))
		has_0xb = cpu_parse_topology_ext(tscan);

	if (!has_0xb && !parse_8000_0008(tscan))
		return;

	/* Prefer leaf 0x8000001e if available */
	if (parse_8000_001e(tscan, has_0xb))
		return;

	/* Try the NODEID MSR */
	if (parse_fam10h_node_id(tscan))
		return;

	legacy_set_llc(tscan);
}

void cpu_parse_topology_amd(struct topo_scan *tscan)
{
	tscan->amd_nodes_per_pkg = 1;
	parse_topology_amd(tscan);

	if (tscan->amd_nodes_per_pkg > 1)
		set_cpu_cap(tscan->c, X86_FEATURE_AMD_DCM);
}

void cpu_topology_fixup_amd(struct topo_scan *tscan)
{
	struct cpuinfo_x86 *c = tscan->c;

	/*
	 * Adjust the core_id relative to the node when there is more than
	 * one node.
	 */
	if (tscan->c->x86 < 0x17 && tscan->amd_nodes_per_pkg > 1)
		c->topo.core_id %= tscan->dom_ncpus[TOPO_CORE_DOMAIN] / tscan->amd_nodes_per_pkg;
}
