// SPDX-License-Identifier: GPL-2.0
#include <linux/cpu.h>

#include <xen/xen.h>

#include <asm/intel-family.h>
#include <asm/apic.h>
#include <asm/processor.h>
#include <asm/smp.h>

#include "cpu.h"

struct x86_topology_system x86_topo_system __ro_after_init;
EXPORT_SYMBOL_GPL(x86_topo_system);

unsigned int __amd_nodes_per_pkg __ro_after_init;
EXPORT_SYMBOL_GPL(__amd_nodes_per_pkg);

void topology_set_dom(struct topo_scan *tscan, enum x86_topology_domains dom,
		      unsigned int shift, unsigned int ncpus)
{
	topology_update_dom(tscan, dom, shift, ncpus);

	/* Propagate to the upper levels */
	for (dom++; dom < TOPO_MAX_DOMAIN; dom++) {
		tscan->dom_shifts[dom] = tscan->dom_shifts[dom - 1];
		tscan->dom_ncpus[dom] = tscan->dom_ncpus[dom - 1];
	}
}

enum x86_topology_cpu_type get_topology_cpu_type(struct cpuinfo_x86 *c)
{
	if (c->x86_vendor == X86_VENDOR_INTEL) {
		switch (c->topo.intel_type) {
		case INTEL_CPU_TYPE_ATOM: return TOPO_CPU_TYPE_EFFICIENCY;
		case INTEL_CPU_TYPE_CORE: return TOPO_CPU_TYPE_PERFORMANCE;
		}
	}
	if (c->x86_vendor == X86_VENDOR_AMD) {
		switch (c->topo.amd_type) {
		case 0:	return TOPO_CPU_TYPE_PERFORMANCE;
		case 1:	return TOPO_CPU_TYPE_EFFICIENCY;
		}
	}

	return TOPO_CPU_TYPE_UNKNOWN;
}

const char *get_topology_cpu_type_name(struct cpuinfo_x86 *c)
{
	switch (get_topology_cpu_type(c)) {
	case TOPO_CPU_TYPE_PERFORMANCE:
		return "performance";
	case TOPO_CPU_TYPE_EFFICIENCY:
		return "efficiency";
	default:
		return "unknown";
	}
}

static unsigned int __maybe_unused parse_num_cores_legacy(struct cpuinfo_x86 *c)
{
	struct {
		u32	cache_type	:  5,
			unused		: 21,
			ncores		:  6;
	} eax;

	if (c->cpuid_level < 4)
		return 1;

	cpuid_subleaf_reg(4, 0, CPUID_EAX, &eax);
	if (!eax.cache_type)
		return 1;

	return eax.ncores + 1;
}

static void parse_legacy(struct topo_scan *tscan)
{
	unsigned int cores, core_shift, smt_shift = 0;
	struct cpuinfo_x86 *c = tscan->c;

	cores = parse_num_cores_legacy(c);
	core_shift = get_count_order(cores);

	if (cpu_has(c, X86_FEATURE_HT)) {
		if (!WARN_ON_ONCE(tscan->ebx1_nproc_shift < core_shift))
			smt_shift = tscan->ebx1_nproc_shift - core_shift;
		/*
		 * The parser expects leaf 0xb/0x1f format, which means
		 * the number of logical processors at core level is
		 * counting threads.
		 */
		core_shift += smt_shift;
		cores <<= smt_shift;
	}

	topology_set_dom(tscan, TOPO_SMT_DOMAIN, smt_shift, 1U << smt_shift);
	topology_set_dom(tscan, TOPO_CORE_DOMAIN, core_shift, cores);
}

static bool fake_topology(struct topo_scan *tscan)
{
	/*
	 * Preset the CORE level shift for CPUID less systems and XEN_PV,
	 * which has useless CPUID information.
	 */
	topology_set_dom(tscan, TOPO_SMT_DOMAIN, 0, 1);
	topology_set_dom(tscan, TOPO_CORE_DOMAIN, 0, 1);

	return tscan->c->cpuid_level < 1;
}

static void parse_topology(struct topo_scan *tscan, bool early)
{
	const struct cpuinfo_topology topo_defaults = {
		.cu_id			= 0xff,
		.llc_id			= BAD_APICID,
		.l2c_id			= BAD_APICID,
		.cpu_type		= TOPO_CPU_TYPE_UNKNOWN,
	};
	struct cpuinfo_x86 *c = tscan->c;
	struct {
		u32	unused0		: 16,
			nproc		:  8,
			apicid		:  8;
	} ebx;

	c->topo = topo_defaults;

	if (fake_topology(tscan))
		return;

	/* Preset Initial APIC ID from CPUID leaf 1 */
	cpuid_leaf_reg(1, CPUID_EBX, &ebx);
	c->topo.initial_apicid = ebx.apicid;

	/*
	 * The initial invocation from early_identify_cpu() happens before
	 * the APIC is mapped or X2APIC enabled. For establishing the
	 * topology, that's not required. Use the initial APIC ID.
	 */
	if (early)
		c->topo.apicid = c->topo.initial_apicid;
	else
		c->topo.apicid = read_apic_id();

	/* The above is sufficient for UP */
	if (!IS_ENABLED(CONFIG_SMP))
		return;

	tscan->ebx1_nproc_shift = get_count_order(ebx.nproc);

	switch (c->x86_vendor) {
	case X86_VENDOR_AMD:
		if (IS_ENABLED(CONFIG_CPU_SUP_AMD))
			cpu_parse_topology_amd(tscan);
		break;
	case X86_VENDOR_CENTAUR:
	case X86_VENDOR_ZHAOXIN:
		parse_legacy(tscan);
		break;
	case X86_VENDOR_INTEL:
		if (!IS_ENABLED(CONFIG_CPU_SUP_INTEL) || !cpu_parse_topology_ext(tscan))
			parse_legacy(tscan);
		if (c->cpuid_level >= 0x1a)
			c->topo.cpu_type = cpuid_eax(0x1a);
		break;
	case X86_VENDOR_HYGON:
		if (IS_ENABLED(CONFIG_CPU_SUP_HYGON))
			cpu_parse_topology_amd(tscan);
		break;
	}
}

static void topo_set_ids(struct topo_scan *tscan, bool early)
{
	struct cpuinfo_x86 *c = tscan->c;
	u32 apicid = c->topo.apicid;

	c->topo.pkg_id = topo_shift_apicid(apicid, TOPO_PKG_DOMAIN);
	c->topo.die_id = topo_shift_apicid(apicid, TOPO_DIE_DOMAIN);

	if (!early) {
		c->topo.logical_pkg_id = topology_get_logical_id(apicid, TOPO_PKG_DOMAIN);
		c->topo.logical_die_id = topology_get_logical_id(apicid, TOPO_DIE_DOMAIN);
	}

	/* Package relative core ID */
	c->topo.core_id = (apicid & topo_domain_mask(TOPO_PKG_DOMAIN)) >>
		x86_topo_system.dom_shifts[TOPO_SMT_DOMAIN];

	c->topo.amd_node_id = tscan->amd_node_id;

	if (c->x86_vendor == X86_VENDOR_AMD)
		cpu_topology_fixup_amd(tscan);
}

void cpu_parse_topology(struct cpuinfo_x86 *c)
{
	unsigned int dom, cpu = smp_processor_id();
	struct topo_scan tscan = { .c = c, };

	parse_topology(&tscan, false);

	if (IS_ENABLED(CONFIG_X86_LOCAL_APIC)) {
		if (c->topo.initial_apicid != c->topo.apicid) {
			pr_err(FW_BUG "CPU%4u: APIC ID mismatch. CPUID: 0x%04x APIC: 0x%04x\n",
			       cpu, c->topo.initial_apicid, c->topo.apicid);
		}

		if (c->topo.apicid != cpuid_to_apicid[cpu]) {
			pr_err(FW_BUG "CPU%4u: APIC ID mismatch. Firmware: 0x%04x APIC: 0x%04x\n",
			       cpu, cpuid_to_apicid[cpu], c->topo.apicid);
		}
	}

	for (dom = TOPO_SMT_DOMAIN; dom < TOPO_MAX_DOMAIN; dom++) {
		if (tscan.dom_shifts[dom] == x86_topo_system.dom_shifts[dom])
			continue;
		pr_err(FW_BUG "CPU%d: Topology domain %u shift %u != %u\n", cpu, dom,
		       tscan.dom_shifts[dom], x86_topo_system.dom_shifts[dom]);
	}

	topo_set_ids(&tscan, false);
}

void __init cpu_init_topology(struct cpuinfo_x86 *c)
{
	struct topo_scan tscan = { .c = c, };
	unsigned int dom, sft;

	parse_topology(&tscan, true);

	/* Copy the shift values and calculate the unit sizes. */
	memcpy(x86_topo_system.dom_shifts, tscan.dom_shifts, sizeof(x86_topo_system.dom_shifts));

	dom = TOPO_SMT_DOMAIN;
	x86_topo_system.dom_size[dom] = 1U << x86_topo_system.dom_shifts[dom];

	for (dom++; dom < TOPO_MAX_DOMAIN; dom++) {
		sft = x86_topo_system.dom_shifts[dom] - x86_topo_system.dom_shifts[dom - 1];
		x86_topo_system.dom_size[dom] = 1U << sft;
	}

	topo_set_ids(&tscan, true);

	/*
	 * AMD systems have Nodes per package which cannot be mapped to
	 * APIC ID.
	 */
	__amd_nodes_per_pkg = tscan.amd_nodes_per_pkg;
}
