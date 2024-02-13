/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_X86_TOPOLOGY_H
#define ARCH_X86_TOPOLOGY_H

struct topo_scan {
	struct cpuinfo_x86	*c;
	unsigned int		dom_shifts[TOPO_MAX_DOMAIN];
	unsigned int		dom_ncpus[TOPO_MAX_DOMAIN];

	/* Legacy CPUID[1]:EBX[23:16] number of logical processors */
	unsigned int		ebx1_nproc_shift;
};

bool topo_is_converted(struct cpuinfo_x86 *c);
void cpu_init_topology(struct cpuinfo_x86 *c);
void cpu_parse_topology(struct cpuinfo_x86 *c);
void topology_set_dom(struct topo_scan *tscan, enum x86_topology_domains dom,
		      unsigned int shift, unsigned int ncpus);

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

#endif /* ARCH_X86_TOPOLOGY_H */
