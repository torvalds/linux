/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI UV APIC functions (note: not an Intel compatible APIC)
 *
 * Copyright (C) 2007-2008 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/bootmem.h>
#include <linux/module.h>
#include <linux/hardirq.h>
#include <asm/smp.h>
#include <asm/ipi.h>
#include <asm/genapic.h>
#include <asm/pgtable.h>
#include <asm/uv/uv_mmrs.h>
#include <asm/uv/uv_hub.h>
#include <asm/uv/bios.h>

DEFINE_PER_CPU(int, x2apic_extra_bits);

static enum uv_system_type uv_system_type;

static int __init uv_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	if (!strcmp(oem_id, "SGI")) {
		if (!strcmp(oem_table_id, "UVL"))
			uv_system_type = UV_LEGACY_APIC;
		else if (!strcmp(oem_table_id, "UVX"))
			uv_system_type = UV_X2APIC;
		else if (!strcmp(oem_table_id, "UVH")) {
			uv_system_type = UV_NON_UNIQUE_APIC;
			return 1;
		}
	}
	return 0;
}

enum uv_system_type get_uv_system_type(void)
{
	return uv_system_type;
}

int is_uv_system(void)
{
	return uv_system_type != UV_NONE;
}
EXPORT_SYMBOL_GPL(is_uv_system);

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

unsigned long sn_rtc_cycles_per_second;
EXPORT_SYMBOL(sn_rtc_cycles_per_second);

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
	int pnode;

	pnode = uv_apicid_to_pnode(phys_apicid);
	val = (1UL << UVH_IPI_INT_SEND_SHFT) |
	    (phys_apicid << UVH_IPI_INT_APIC_ID_SHFT) |
	    (((long)start_rip << UVH_IPI_INT_VECTOR_SHFT) >> 12) |
	    APIC_DM_INIT;
	uv_write_global_mmr64(pnode, UVH_IPI_INT, val);
	mdelay(10);

	val = (1UL << UVH_IPI_INT_SEND_SHFT) |
	    (phys_apicid << UVH_IPI_INT_APIC_ID_SHFT) |
	    (((long)start_rip << UVH_IPI_INT_VECTOR_SHFT) >> 12) |
	    APIC_DM_STARTUP;
	uv_write_global_mmr64(pnode, UVH_IPI_INT, val);
	return 0;
}

static void uv_send_IPI_one(int cpu, int vector)
{
	unsigned long val, apicid, lapicid;
	int pnode;

	apicid = per_cpu(x86_cpu_to_apicid, cpu); /* ZZZ - cache node-local ? */
	lapicid = apicid & 0x3f;		/* ZZZ macro needed */
	pnode = uv_apicid_to_pnode(apicid);
	val =
	    (1UL << UVH_IPI_INT_SEND_SHFT) | (lapicid <<
					      UVH_IPI_INT_APIC_ID_SHFT) |
	    (vector << UVH_IPI_INT_VECTOR_SHFT);
	uv_write_global_mmr64(pnode, UVH_IPI_INT, val);
}

static void uv_send_IPI_mask(cpumask_t mask, int vector)
{
	unsigned int cpu;

	for_each_possible_cpu(cpu)
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

static void uv_init_apic_ldr(void)
{
}

static unsigned int uv_cpu_mask_to_apicid(cpumask_t cpumask)
{
	int cpu;

	/*
	 * We're using fixed IRQ delivery, can only return one phys APIC ID.
	 * May as well be the first.
	 */
	cpu = first_cpu(cpumask);
	if ((unsigned)cpu < nr_cpu_ids)
		return per_cpu(x86_cpu_to_apicid, cpu);
	else
		return BAD_APICID;
}

static unsigned int get_apic_id(unsigned long x)
{
	unsigned int id;

	WARN_ON(preemptible() && num_online_cpus() > 1);
	id = x | __get_cpu_var(x2apic_extra_bits);

	return id;
}

static unsigned long set_apic_id(unsigned int id)
{
	unsigned long x;

	/* maskout x2apic_extra_bits ? */
	x = id;
	return x;
}

static unsigned int uv_read_apic_id(void)
{

	return get_apic_id(apic_read(APIC_ID));
}

static unsigned int phys_pkg_id(int index_msb)
{
	return uv_read_apic_id() >> index_msb;
}

#ifdef ZZZ		/* Needs x2apic patch */
static void uv_send_IPI_self(int vector)
{
	apic_write(APIC_SELF_IPI, vector);
}
#endif

struct genapic apic_x2apic_uv_x = {
	.name = "UV large system",
	.acpi_madt_oem_check = uv_acpi_madt_oem_check,
	.int_delivery_mode = dest_Fixed,
	.int_dest_mode = (APIC_DEST_PHYSICAL != 0),
	.target_cpus = uv_target_cpus,
	.vector_allocation_domain = uv_vector_allocation_domain,/* Fixme ZZZ */
	.apic_id_registered = uv_apic_id_registered,
	.init_apic_ldr = uv_init_apic_ldr,
	.send_IPI_all = uv_send_IPI_all,
	.send_IPI_allbutself = uv_send_IPI_allbutself,
	.send_IPI_mask = uv_send_IPI_mask,
	/* ZZZ.send_IPI_self = uv_send_IPI_self, */
	.cpu_mask_to_apicid = uv_cpu_mask_to_apicid,
	.phys_pkg_id = phys_pkg_id,	/* Fixme ZZZ */
	.get_apic_id = get_apic_id,
	.set_apic_id = set_apic_id,
	.apic_id_mask = (0xFFFFFFFFu),
};

static __cpuinit void set_x2apic_extra_bits(int pnode)
{
	__get_cpu_var(x2apic_extra_bits) = (pnode << 6);
}

/*
 * Called on boot cpu.
 */
static __init int boot_pnode_to_blade(int pnode)
{
	int blade;

	for (blade = 0; blade < uv_num_possible_blades(); blade++)
		if (pnode == uv_blade_info[blade].pnode)
			return blade;
	BUG();
}

struct redir_addr {
	unsigned long redirect;
	unsigned long alias;
};

#define DEST_SHIFT UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_0_MMR_DEST_BASE_SHFT

static __initdata struct redir_addr redir_addrs[] = {
	{UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_0_MMR, UVH_SI_ALIAS0_OVERLAY_CONFIG},
	{UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_1_MMR, UVH_SI_ALIAS1_OVERLAY_CONFIG},
	{UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_2_MMR, UVH_SI_ALIAS2_OVERLAY_CONFIG},
};

static __init void get_lowmem_redirect(unsigned long *base, unsigned long *size)
{
	union uvh_si_alias0_overlay_config_u alias;
	union uvh_rh_gam_alias210_redirect_config_2_mmr_u redirect;
	int i;

	for (i = 0; i < ARRAY_SIZE(redir_addrs); i++) {
		alias.v = uv_read_local_mmr(redir_addrs[i].alias);
		if (alias.s.base == 0) {
			*size = (1UL << alias.s.m_alias);
			redirect.v = uv_read_local_mmr(redir_addrs[i].redirect);
			*base = (unsigned long)redirect.s.dest_base << DEST_SHIFT;
			return;
		}
	}
	BUG();
}

static __init void map_low_mmrs(void)
{
	init_extra_mapping_uc(UV_GLOBAL_MMR32_BASE, UV_GLOBAL_MMR32_SIZE);
	init_extra_mapping_uc(UV_LOCAL_MMR_BASE, UV_LOCAL_MMR_SIZE);
}

enum map_type {map_wb, map_uc};

static __init void map_high(char *id, unsigned long base, int shift, enum map_type map_type)
{
	unsigned long bytes, paddr;

	paddr = base << shift;
	bytes = (1UL << shift);
	printk(KERN_INFO "UV: Map %s_HI 0x%lx - 0x%lx\n", id, paddr,
	       					paddr + bytes);
	if (map_type == map_uc)
		init_extra_mapping_uc(paddr, bytes);
	else
		init_extra_mapping_wb(paddr, bytes);

}
static __init void map_gru_high(int max_pnode)
{
	union uvh_rh_gam_gru_overlay_config_mmr_u gru;
	int shift = UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_BASE_SHFT;

	gru.v = uv_read_local_mmr(UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR);
	if (gru.s.enable)
		map_high("GRU", gru.s.base, shift, map_wb);
}

static __init void map_config_high(int max_pnode)
{
	union uvh_rh_gam_cfg_overlay_config_mmr_u cfg;
	int shift = UVH_RH_GAM_CFG_OVERLAY_CONFIG_MMR_BASE_SHFT;

	cfg.v = uv_read_local_mmr(UVH_RH_GAM_CFG_OVERLAY_CONFIG_MMR);
	if (cfg.s.enable)
		map_high("CONFIG", cfg.s.base, shift, map_uc);
}

static __init void map_mmr_high(int max_pnode)
{
	union uvh_rh_gam_mmr_overlay_config_mmr_u mmr;
	int shift = UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR_BASE_SHFT;

	mmr.v = uv_read_local_mmr(UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR);
	if (mmr.s.enable)
		map_high("MMR", mmr.s.base, shift, map_uc);
}

static __init void map_mmioh_high(int max_pnode)
{
	union uvh_rh_gam_mmioh_overlay_config_mmr_u mmioh;
	int shift = UVH_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR_BASE_SHFT;

	mmioh.v = uv_read_local_mmr(UVH_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR);
	if (mmioh.s.enable)
		map_high("MMIOH", mmioh.s.base, shift, map_uc);
}

static __init void uv_rtc_init(void)
{
	long status, ticks_per_sec, drift;

	status =
	    x86_bios_freq_base(BIOS_FREQ_BASE_REALTIME_CLOCK, &ticks_per_sec,
					&drift);
	if (status != 0 || ticks_per_sec < 100000) {
		printk(KERN_WARNING
			"unable to determine platform RTC clock frequency, "
			"guessing.\n");
		/* BIOS gives wrong value for clock freq. so guess */
		sn_rtc_cycles_per_second = 1000000000000UL / 30000UL;
	} else
		sn_rtc_cycles_per_second = ticks_per_sec;
}

static bool uv_system_inited;

void __init uv_system_init(void)
{
	union uvh_si_addr_map_config_u m_n_config;
	union uvh_node_id_u node_id;
	unsigned long gnode_upper, lowmem_redir_base, lowmem_redir_size;
	int bytes, nid, cpu, lcpu, pnode, blade, i, j, m_val, n_val;
	int max_pnode = 0;
	unsigned long mmr_base, present;

	map_low_mmrs();

	m_n_config.v = uv_read_local_mmr(UVH_SI_ADDR_MAP_CONFIG);
	m_val = m_n_config.s.m_skt;
	n_val = m_n_config.s.n_skt;
	mmr_base =
	    uv_read_local_mmr(UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR) &
	    ~UV_MMR_ENABLE;
	printk(KERN_DEBUG "UV: global MMR base 0x%lx\n", mmr_base);

	for(i = 0; i < UVH_NODE_PRESENT_TABLE_DEPTH; i++)
		uv_possible_blades +=
		  hweight64(uv_read_local_mmr( UVH_NODE_PRESENT_TABLE + i * 8));
	printk(KERN_DEBUG "UV: Found %d blades\n", uv_num_possible_blades());

	bytes = sizeof(struct uv_blade_info) * uv_num_possible_blades();
	uv_blade_info = alloc_bootmem_pages(bytes);

	get_lowmem_redirect(&lowmem_redir_base, &lowmem_redir_size);

	bytes = sizeof(uv_node_to_blade[0]) * num_possible_nodes();
	uv_node_to_blade = alloc_bootmem_pages(bytes);
	memset(uv_node_to_blade, 255, bytes);

	bytes = sizeof(uv_cpu_to_blade[0]) * num_possible_cpus();
	uv_cpu_to_blade = alloc_bootmem_pages(bytes);
	memset(uv_cpu_to_blade, 255, bytes);

	blade = 0;
	for (i = 0; i < UVH_NODE_PRESENT_TABLE_DEPTH; i++) {
		present = uv_read_local_mmr(UVH_NODE_PRESENT_TABLE + i * 8);
		for (j = 0; j < 64; j++) {
			if (!test_bit(j, &present))
				continue;
			uv_blade_info[blade].pnode = (i * 64 + j);
			uv_blade_info[blade].nr_possible_cpus = 0;
			uv_blade_info[blade].nr_online_cpus = 0;
			blade++;
		}
	}

	node_id.v = uv_read_local_mmr(UVH_NODE_ID);
	gnode_upper = (((unsigned long)node_id.s.node_id) &
		       ~((1 << n_val) - 1)) << m_val;

	uv_rtc_init();

	for_each_present_cpu(cpu) {
		nid = cpu_to_node(cpu);
		pnode = uv_apicid_to_pnode(per_cpu(x86_cpu_to_apicid, cpu));
		blade = boot_pnode_to_blade(pnode);
		lcpu = uv_blade_info[blade].nr_possible_cpus;
		uv_blade_info[blade].nr_possible_cpus++;

		uv_cpu_hub_info(cpu)->lowmem_remap_base = lowmem_redir_base;
		uv_cpu_hub_info(cpu)->lowmem_remap_top =
					lowmem_redir_base + lowmem_redir_size;
		uv_cpu_hub_info(cpu)->m_val = m_val;
		uv_cpu_hub_info(cpu)->n_val = m_val;
		uv_cpu_hub_info(cpu)->numa_blade_id = blade;
		uv_cpu_hub_info(cpu)->blade_processor_id = lcpu;
		uv_cpu_hub_info(cpu)->pnode = pnode;
		uv_cpu_hub_info(cpu)->pnode_mask = (1 << n_val) - 1;
		uv_cpu_hub_info(cpu)->gpa_mask = (1 << (m_val + n_val)) - 1;
		uv_cpu_hub_info(cpu)->gnode_upper = gnode_upper;
		uv_cpu_hub_info(cpu)->global_mmr_base = mmr_base;
		uv_cpu_hub_info(cpu)->coherency_domain_number = 0;/* ZZZ */
		uv_node_to_blade[nid] = blade;
		uv_cpu_to_blade[cpu] = blade;
		max_pnode = max(pnode, max_pnode);

		printk(KERN_DEBUG "UV: cpu %d, apicid 0x%x, pnode %d, nid %d, "
			"lcpu %d, blade %d\n",
			cpu, per_cpu(x86_cpu_to_apicid, cpu), pnode, nid,
			lcpu, blade);
	}

	map_gru_high(max_pnode);
	map_mmr_high(max_pnode);
	map_config_high(max_pnode);
	map_mmioh_high(max_pnode);
	uv_system_inited = true;
}

/*
 * Called on each cpu to initialize the per_cpu UV data area.
 * 	ZZZ hotplug not supported yet
 */
void __cpuinit uv_cpu_init(void)
{
	BUG_ON(!uv_system_inited);

	uv_blade_info[uv_numa_blade_id()].nr_online_cpus++;

	if (get_uv_system_type() == UV_NON_UNIQUE_APIC)
		set_x2apic_extra_bits(uv_hub_info->pnode);
}


