/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_X86_TOPOLOGY_H
#define ARCH_X86_TOPOLOGY_H

struct topo_scan {
	struct cpuinfo_x86	*c;
	unsigned int		dom_shifts[TOPO_MAX_DOMAIN];
	unsigned int		dom_ncpus[TOPO_MAX_DOMAIN];

	/* Legacy CPUID[1]:EBX[23:16] number of logical processors */
	unsigned int		ebx1_nproc_shift;

	/* AMD specific node ID which cannot be mapped into APIC space. */
	u16			amd_nodes_per_pkg;
	u16			amd_node_id;
};

void cpu_init_topology(struct cpuinfo_x86 *c);
void cpu_parse_topology(struct cpuinfo_x86 *c);
void topology_set_dom(struct topo_scan *tscan, enum x86_topology_domains dom,
		      unsigned int shift, unsigned int ncpus);
bool cpu_parse_topology_ext(struct topo_scan *tscan);
void cpu_parse_topology_amd(struct topo_scan *tscan);
void cpu_topology_fixup_amd(struct topo_scan *tscan);

static inline u32 topo_shift_apicid(u32 apicid, enum x86_topology_domains dom)
{
	if (dom == TOPO_SMT_DOMAIN)
		return apicid;
	return apicid >> x86_topo_system.dom_shifts[dom - 1];
}

static inline u32 topo_relative_domain_id(u32 apicid, enum x86_topology_domains dom)
{
	if (dom != TOPO_SMT_DOMAIN)
		apicid >>= x86_topo_system.dom_shifts[dom - 1];
	return apicid & (x86_topo_system.dom_size[dom] - 1);
}

static inline u32 topo_domain_mask(enum x86_topology_domains dom)
{
	return (1U << x86_topo_system.dom_shifts[dom]) - 1;
}

/*
 * Update a domain level after the fact without propagating. Used to fixup
 * broken CPUID enumerations.
 */
static inline void topology_update_dom(struct topo_scan *tscan, enum x86_topology_domains dom,
				       unsigned int shift, unsigned int ncpus)
{
	tscan->dom_shifts[dom] = shift;
	tscan->dom_ncpus[dom] = ncpus;
}

#ifdef CONFIG_X86_LOCAL_APIC
unsigned int topology_unit_count(u32 apicid, enum x86_topology_domains which_units,
				 enum x86_topology_domains at_level);
#else
static inline unsigned int topology_unit_count(u32 apicid, enum x86_topology_domains which_units,
					       enum x86_topology_domains at_level)
{
	return 1;
}
#endif

#endif /* ARCH_X86_TOPOLOGY_H */
