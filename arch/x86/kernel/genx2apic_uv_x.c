/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI UV APIC functions (note: not an Intel compatible APIC)
 *
 * Copyright (C) 2007 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/bootmem.h>
#include <linux/module.h>
#include <asm/smp.h>
#include <asm/ipi.h>
#include <asm/genapic.h>
#include <asm/uv/uv_mmrs.h>
#include <asm/uv/uv_hub.h>

DEFINE_PER_CPU(struct uv_hub_info_s, __uv_hub_info);
EXPORT_PER_CPU_SYMBOL_GPL(__uv_hub_info);

struct uv_blade_info *uv_blade_info;
EXPORT_SYMBOL_GPL(uv_blade_info);

short *uv_node_to_blade;
EXPORT_SYMBOL_GPL(uv_node_to_blade);

short *uv_cpu_to_blade;
EXPORT_SYMBOL_GPL(uv_cpu_to_blade);

short uv_possible_blades;
EXPORT_SYMBOL_GPL(uv_possible_blades);

/* Start with all IRQs pointing to boot CPU.  IRQ balancing will shift them. */

static cpumask_t uv_target_cpus(void)
{
	return cpumask_of_cpu(0);
}

static cpumask_t uv_vector_allocation_domain(int cpu)
{
	cpumask_t domain = CPU_MASK_NONE;
	cpu_set(cpu, domain);
	return domain;
}

int uv_wakeup_secondary(int phys_apicid, unsigned int start_rip)
{
	unsigned long val;
	int nasid;

	nasid = uv_apicid_to_nasid(phys_apicid);
	val = (1UL << UVH_IPI_INT_SEND_SHFT) |
	    (phys_apicid << UVH_IPI_INT_APIC_ID_SHFT) |
	    (((long)start_rip << UVH_IPI_INT_VECTOR_SHFT) >> 12) |
	    (6 << UVH_IPI_INT_DELIVERY_MODE_SHFT);
	uv_write_global_mmr64(nasid, UVH_IPI_INT, val);
	return 0;
}

static void uv_send_IPI_one(int cpu, int vector)
{
	unsigned long val, apicid;
	int nasid;

	apicid = per_cpu(x86_cpu_to_apicid, cpu); /* ZZZ - cache node-local ? */
	nasid = uv_apicid_to_nasid(apicid);
	val =
	    (1UL << UVH_IPI_INT_SEND_SHFT) | (apicid <<
					      UVH_IPI_INT_APIC_ID_SHFT) |
	    (vector << UVH_IPI_INT_VECTOR_SHFT);
	uv_write_global_mmr64(nasid, UVH_IPI_INT, val);
	printk(KERN_DEBUG
	     "UV: IPI to cpu %d, apicid 0x%lx, vec %d, nasid%d, val 0x%lx\n",
	     cpu, apicid, vector, nasid, val);
}

static void uv_send_IPI_mask(cpumask_t mask, int vector)
{
	unsigned int cpu;

	for (cpu = 0; cpu < NR_CPUS; ++cpu)
		if (cpu_isset(cpu, mask))
			uv_send_IPI_one(cpu, vector);
}

static void uv_send_IPI_allbutself(int vector)
{
	cpumask_t mask = cpu_online_map;

	cpu_clear(smp_processor_id(), mask);

	if (!cpus_empty(mask))
		uv_send_IPI_mask(mask, vector);
}

static void uv_send_IPI_all(int vector)
{
	uv_send_IPI_mask(cpu_online_map, vector);
}

static int uv_apic_id_registered(void)
{
	return 1;
}

static unsigned int uv_cpu_mask_to_apicid(cpumask_t cpumask)
{
	int cpu;

	/*
	 * We're using fixed IRQ delivery, can only return one phys APIC ID.
	 * May as well be the first.
	 */
	cpu = first_cpu(cpumask);
	if ((unsigned)cpu < NR_CPUS)
		return per_cpu(x86_cpu_to_apicid, cpu);
	else
		return BAD_APICID;
}

static unsigned int phys_pkg_id(int index_msb)
{
	return GET_APIC_ID(read_apic_id()) >> index_msb;
}

#ifdef ZZZ		/* Needs x2apic patch */
static void uv_send_IPI_self(int vector)
{
	apic_write(APIC_SELF_IPI, vector);
}
#endif

struct genapic apic_x2apic_uv_x = {
	.name = "UV large system",
	.int_delivery_mode = dest_Fixed,
	.int_dest_mode = (APIC_DEST_PHYSICAL != 0),
	.target_cpus = uv_target_cpus,
	.vector_allocation_domain = uv_vector_allocation_domain,/* Fixme ZZZ */
	.apic_id_registered = uv_apic_id_registered,
	.send_IPI_all = uv_send_IPI_all,
	.send_IPI_allbutself = uv_send_IPI_allbutself,
	.send_IPI_mask = uv_send_IPI_mask,
	/* ZZZ.send_IPI_self = uv_send_IPI_self, */
	.cpu_mask_to_apicid = uv_cpu_mask_to_apicid,
	.phys_pkg_id = phys_pkg_id,	/* Fixme ZZZ */
};

static __cpuinit void set_x2apic_extra_bits(int nasid)
{
	__get_cpu_var(x2apic_extra_bits) = ((nasid >> 1) << 6);
}

/*
 * Called on boot cpu.
 */
static __init void uv_system_init(void)
{
	union uvh_si_addr_map_config_u m_n_config;
	int bytes, nid, cpu, lcpu, nasid, last_nasid, blade;
	unsigned long mmr_base;

	m_n_config.v = uv_read_local_mmr(UVH_SI_ADDR_MAP_CONFIG);
	mmr_base =
	    uv_read_local_mmr(UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR) &
	    ~UV_MMR_ENABLE;
	printk(KERN_DEBUG "UV: global MMR base 0x%lx\n", mmr_base);

	last_nasid = -1;
	for_each_possible_cpu(cpu) {
		nid = cpu_to_node(cpu);
		nasid = uv_apicid_to_nasid(per_cpu(x86_cpu_to_apicid, cpu));
		if (nasid != last_nasid)
			uv_possible_blades++;
		last_nasid = nasid;
	}
	printk(KERN_DEBUG "UV: Found %d blades\n", uv_num_possible_blades());

	bytes = sizeof(struct uv_blade_info) * uv_num_possible_blades();
	uv_blade_info = alloc_bootmem_pages(bytes);

	bytes = sizeof(uv_node_to_blade[0]) * num_possible_nodes();
	uv_node_to_blade = alloc_bootmem_pages(bytes);
	memset(uv_node_to_blade, 255, bytes);

	bytes = sizeof(uv_cpu_to_blade[0]) * num_possible_cpus();
	uv_cpu_to_blade = alloc_bootmem_pages(bytes);
	memset(uv_cpu_to_blade, 255, bytes);

	last_nasid = -1;
	blade = -1;
	lcpu = -1;
	for_each_possible_cpu(cpu) {
		nid = cpu_to_node(cpu);
		nasid = uv_apicid_to_nasid(per_cpu(x86_cpu_to_apicid, cpu));
		if (nasid != last_nasid) {
			blade++;
			lcpu = -1;
			uv_blade_info[blade].nr_posible_cpus = 0;
			uv_blade_info[blade].nr_online_cpus = 0;
		}
		last_nasid = nasid;
		lcpu++;

		uv_cpu_hub_info(cpu)->m_val = m_n_config.s.m_skt;
		uv_cpu_hub_info(cpu)->n_val = m_n_config.s.n_skt;
		uv_cpu_hub_info(cpu)->numa_blade_id = blade;
		uv_cpu_hub_info(cpu)->blade_processor_id = lcpu;
		uv_cpu_hub_info(cpu)->local_nasid = nasid;
		uv_cpu_hub_info(cpu)->gnode_upper =
		    nasid & ~((1 << uv_hub_info->n_val) - 1);
		uv_cpu_hub_info(cpu)->global_mmr_base = mmr_base;
		uv_cpu_hub_info(cpu)->coherency_domain_number = 0;/* ZZZ */
		uv_blade_info[blade].nasid = nasid;
		uv_blade_info[blade].nr_posible_cpus++;
		uv_node_to_blade[nid] = blade;
		uv_cpu_to_blade[cpu] = blade;

		printk(KERN_DEBUG "UV cpu %d, apicid 0x%x, nasid %d, nid %d\n",
		       cpu, per_cpu(x86_cpu_to_apicid, cpu), nasid, nid);
		printk(KERN_DEBUG "UV   lcpu %d, blade %d\n", lcpu, blade);
	}
}

/*
 * Called on each cpu to initialize the per_cpu UV data area.
 */
void __cpuinit uv_cpu_init(void)
{
	if (!uv_node_to_blade)
		uv_system_init();

	uv_blade_info[uv_numa_blade_id()].nr_online_cpus++;

	if (get_uv_system_type() == UV_NON_UNIQUE_APIC)
		set_x2apic_extra_bits(uv_hub_info->local_nasid);
}
