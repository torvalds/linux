/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI UV APIC functions (note: not an Intel compatible APIC)
 *
 * Copyright (C) 2007-2008 Silicon Graphics, Inc. All rights reserved.
 */
#include <linux/cpumask.h>
#include <linux/hardirq.h>
#include <linux/proc_fs.h>
#include <linux/threads.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/io.h>

#include <asm/uv/uv_mmrs.h>
#include <asm/uv/uv_hub.h>
#include <asm/current.h>
#include <asm/pgtable.h>
#include <asm/uv/bios.h>
#include <asm/uv/uv.h>
#include <asm/apic.h>
#include <asm/ipi.h>
#include <asm/smp.h>

DEFINE_PER_CPU(int, x2apic_extra_bits);

static enum uv_system_type uv_system_type;

static int early_get_nodeid(void)
{
	union uvh_node_id_u node_id;
	unsigned long *mmr;

	mmr = early_ioremap(UV_LOCAL_MMR_BASE | UVH_NODE_ID, sizeof(*mmr));
	node_id.v = *mmr;
	early_iounmap(mmr, sizeof(*mmr));
	return node_id.s.node_id;
}

static int __init uv_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	if (!strcmp(oem_id, "SGI")) {
		if (!strcmp(oem_table_id, "UVL"))
			uv_system_type = UV_LEGACY_APIC;
		else if (!strcmp(oem_table_id, "UVX"))
			uv_system_type = UV_X2APIC;
		else if (!strcmp(oem_table_id, "UVH")) {
			__get_cpu_var(x2apic_extra_bits) =
				early_get_nodeid() << (UV_APIC_PNODE_SHIFT - 1);
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

static const struct cpumask *uv_target_cpus(void)
{
	return cpumask_of(0);
}

static void uv_vector_allocation_domain(int cpu, struct cpumask *retmask)
{
	cpumask_clear(retmask);
	cpumask_set_cpu(cpu, retmask);
}

static int __cpuinit uv_wakeup_secondary(int phys_apicid, unsigned long start_rip)
{
#ifdef CONFIG_SMP
	unsigned long val;
	int pnode;

	pnode = uv_apicid_to_pnode(phys_apicid);
	val = (1UL << UVH_IPI_INT_SEND_SHFT) |
	    (phys_apicid << UVH_IPI_INT_APIC_ID_SHFT) |
	    ((start_rip << UVH_IPI_INT_VECTOR_SHFT) >> 12) |
	    APIC_DM_INIT;
	uv_write_global_mmr64(pnode, UVH_IPI_INT, val);
	mdelay(10);

	val = (1UL << UVH_IPI_INT_SEND_SHFT) |
	    (phys_apicid << UVH_IPI_INT_APIC_ID_SHFT) |
	    ((start_rip << UVH_IPI_INT_VECTOR_SHFT) >> 12) |
	    APIC_DM_STARTUP;
	uv_write_global_mmr64(pnode, UVH_IPI_INT, val);

	atomic_set(&init_deasserted, 1);
#endif
	return 0;
}

static void uv_send_IPI_one(int cpu, int vector)
{
	unsigned long apicid;
	int pnode;

	apicid = per_cpu(x86_cpu_to_apicid, cpu);
	pnode = uv_apicid_to_pnode(apicid);
	uv_hub_send_ipi(pnode, apicid, vector);
}

static void uv_send_IPI_mask(const struct cpumask *mask, int vector)
{
	unsigned int cpu;

	for_each_cpu(cpu, mask)
		uv_send_IPI_one(cpu, vector);
}

static void uv_send_IPI_mask_allbutself(const struct cpumask *mask, int vector)
{
	unsigned int this_cpu = smp_processor_id();
	unsigned int cpu;

	for_each_cpu(cpu, mask) {
		if (cpu != this_cpu)
			uv_send_IPI_one(cpu, vector);
	}
}

static void uv_send_IPI_allbutself(int vector)
{
	unsigned int this_cpu = smp_processor_id();
	unsigned int cpu;

	for_each_online_cpu(cpu) {
		if (cpu != this_cpu)
			uv_send_IPI_one(cpu, vector);
	}
}

static void uv_send_IPI_all(int vector)
{
	uv_send_IPI_mask(cpu_online_mask, vector);
}

static int uv_apic_id_registered(void)
{
	return 1;
}

static void uv_init_apic_ldr(void)
{
}

static unsigned int uv_cpu_mask_to_apicid(const struct cpumask *cpumask)
{
	/*
	 * We're using fixed IRQ delivery, can only return one phys APIC ID.
	 * May as well be the first.
	 */
	int cpu = cpumask_first(cpumask);

	if ((unsigned)cpu < nr_cpu_ids)
		return per_cpu(x86_cpu_to_apicid, cpu);
	else
		return BAD_APICID;
}

static unsigned int
uv_cpu_mask_to_apicid_and(const struct cpumask *cpumask,
			  const struct cpumask *andmask)
{
	int cpu;

	/*
	 * We're using fixed IRQ delivery, can only return one phys APIC ID.
	 * May as well be the first.
	 */
	for_each_cpu_and(cpu, cpumask, andmask) {
		if (cpumask_test_cpu(cpu, cpu_online_mask))
			break;
	}
	if (cpu < nr_cpu_ids)
		return per_cpu(x86_cpu_to_apicid, cpu);

	return BAD_APICID;
}

static unsigned int x2apic_get_apic_id(unsigned long x)
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

	return x2apic_get_apic_id(apic_read(APIC_ID));
}

static int uv_phys_pkg_id(int initial_apicid, int index_msb)
{
	return uv_read_apic_id() >> index_msb;
}

static void uv_send_IPI_self(int vector)
{
	apic_write(APIC_SELF_IPI, vector);
}

struct apic __refdata apic_x2apic_uv_x = {

	.name				= "UV large system",
	.probe				= NULL,
	.acpi_madt_oem_check		= uv_acpi_madt_oem_check,
	.apic_id_registered		= uv_apic_id_registered,

	.irq_delivery_mode		= dest_Fixed,
	.irq_dest_mode			= 0, /* physical */

	.target_cpus			= uv_target_cpus,
	.disable_esr			= 0,
	.dest_logical			= APIC_DEST_LOGICAL,
	.check_apicid_used		= NULL,
	.check_apicid_present		= NULL,

	.vector_allocation_domain	= uv_vector_allocation_domain,
	.init_apic_ldr			= uv_init_apic_ldr,

	.ioapic_phys_id_map		= NULL,
	.setup_apic_routing		= NULL,
	.multi_timer_check		= NULL,
	.apicid_to_node			= NULL,
	.cpu_to_logical_apicid		= NULL,
	.cpu_present_to_apicid		= default_cpu_present_to_apicid,
	.apicid_to_cpu_present		= NULL,
	.setup_portio_remap		= NULL,
	.check_phys_apicid_present	= default_check_phys_apicid_present,
	.enable_apic_mode		= NULL,
	.phys_pkg_id			= uv_phys_pkg_id,
	.mps_oem_check			= NULL,

	.get_apic_id			= x2apic_get_apic_id,
	.set_apic_id			= set_apic_id,
	.apic_id_mask			= 0xFFFFFFFFu,

	.cpu_mask_to_apicid		= uv_cpu_mask_to_apicid,
	.cpu_mask_to_apicid_and		= uv_cpu_mask_to_apicid_and,

	.send_IPI_mask			= uv_send_IPI_mask,
	.send_IPI_mask_allbutself	= uv_send_IPI_mask_allbutself,
	.send_IPI_allbutself		= uv_send_IPI_allbutself,
	.send_IPI_all			= uv_send_IPI_all,
	.send_IPI_self			= uv_send_IPI_self,

	.wakeup_secondary_cpu		= uv_wakeup_secondary,
	.trampoline_phys_low		= DEFAULT_TRAMPOLINE_PHYS_LOW,
	.trampoline_phys_high		= DEFAULT_TRAMPOLINE_PHYS_HIGH,
	.wait_for_init_deassert		= NULL,
	.smp_callin_clear_local_apic	= NULL,
	.inquire_remote_apic		= NULL,

	.read				= native_apic_msr_read,
	.write				= native_apic_msr_write,
	.icr_read			= native_x2apic_icr_read,
	.icr_write			= native_x2apic_icr_write,
	.wait_icr_idle			= native_x2apic_wait_icr_idle,
	.safe_wait_icr_idle		= native_safe_x2apic_wait_icr_idle,
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
		if (alias.s.enable && alias.s.base == 0) {
			*size = (1UL << alias.s.m_alias);
			redirect.v = uv_read_local_mmr(redir_addrs[i].redirect);
			*base = (unsigned long)redirect.s.dest_base << DEST_SHIFT;
			return;
		}
	}
	*base = *size = 0;
}

enum map_type {map_wb, map_uc};

static __init void map_high(char *id, unsigned long base, int shift,
			    int max_pnode, enum map_type map_type)
{
	unsigned long bytes, paddr;

	paddr = base << shift;
	bytes = (1UL << shift) * (max_pnode + 1);
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
		map_high("GRU", gru.s.base, shift, max_pnode, map_wb);
}

static __init void map_mmr_high(int max_pnode)
{
	union uvh_rh_gam_mmr_overlay_config_mmr_u mmr;
	int shift = UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR_BASE_SHFT;

	mmr.v = uv_read_local_mmr(UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR);
	if (mmr.s.enable)
		map_high("MMR", mmr.s.base, shift, max_pnode, map_uc);
}

static __init void map_mmioh_high(int max_pnode)
{
	union uvh_rh_gam_mmioh_overlay_config_mmr_u mmioh;
	int shift = UVH_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR_BASE_SHFT;

	mmioh.v = uv_read_local_mmr(UVH_RH_GAM_MMIOH_OVERLAY_CONFIG_MMR);
	if (mmioh.s.enable)
		map_high("MMIOH", mmioh.s.base, shift, max_pnode, map_uc);
}

static __init void uv_rtc_init(void)
{
	long status;
	u64 ticks_per_sec;

	status = uv_bios_freq_base(BIOS_FREQ_BASE_REALTIME_CLOCK,
					&ticks_per_sec);
	if (status != BIOS_STATUS_SUCCESS || ticks_per_sec < 100000) {
		printk(KERN_WARNING
			"unable to determine platform RTC clock frequency, "
			"guessing.\n");
		/* BIOS gives wrong value for clock freq. so guess */
		sn_rtc_cycles_per_second = 1000000000000UL / 30000UL;
	} else
		sn_rtc_cycles_per_second = ticks_per_sec;
}

/*
 * percpu heartbeat timer
 */
static void uv_heartbeat(unsigned long ignored)
{
	struct timer_list *timer = &uv_hub_info->scir.timer;
	unsigned char bits = uv_hub_info->scir.state;

	/* flip heartbeat bit */
	bits ^= SCIR_CPU_HEARTBEAT;

	/* is this cpu idle? */
	if (idle_cpu(raw_smp_processor_id()))
		bits &= ~SCIR_CPU_ACTIVITY;
	else
		bits |= SCIR_CPU_ACTIVITY;

	/* update system controller interface reg */
	uv_set_scir_bits(bits);

	/* enable next timer period */
	mod_timer_pinned(timer, jiffies + SCIR_CPU_HB_INTERVAL);
}

static void __cpuinit uv_heartbeat_enable(int cpu)
{
	if (!uv_cpu_hub_info(cpu)->scir.enabled) {
		struct timer_list *timer = &uv_cpu_hub_info(cpu)->scir.timer;

		uv_set_cpu_scir_bits(cpu, SCIR_CPU_HEARTBEAT|SCIR_CPU_ACTIVITY);
		setup_timer(timer, uv_heartbeat, cpu);
		timer->expires = jiffies + SCIR_CPU_HB_INTERVAL;
		add_timer_on(timer, cpu);
		uv_cpu_hub_info(cpu)->scir.enabled = 1;
	}

	/* check boot cpu */
	if (!uv_cpu_hub_info(0)->scir.enabled)
		uv_heartbeat_enable(0);
}

#ifdef CONFIG_HOTPLUG_CPU
static void __cpuinit uv_heartbeat_disable(int cpu)
{
	if (uv_cpu_hub_info(cpu)->scir.enabled) {
		uv_cpu_hub_info(cpu)->scir.enabled = 0;
		del_timer(&uv_cpu_hub_info(cpu)->scir.timer);
	}
	uv_set_cpu_scir_bits(cpu, 0xff);
}

/*
 * cpu hotplug notifier
 */
static __cpuinit int uv_scir_cpu_notify(struct notifier_block *self,
				       unsigned long action, void *hcpu)
{
	long cpu = (long)hcpu;

	switch (action) {
	case CPU_ONLINE:
		uv_heartbeat_enable(cpu);
		break;
	case CPU_DOWN_PREPARE:
		uv_heartbeat_disable(cpu);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static __init void uv_scir_register_cpu_notifier(void)
{
	hotcpu_notifier(uv_scir_cpu_notify, 0);
}

#else /* !CONFIG_HOTPLUG_CPU */

static __init void uv_scir_register_cpu_notifier(void)
{
}

static __init int uv_init_heartbeat(void)
{
	int cpu;

	if (is_uv_system())
		for_each_online_cpu(cpu)
			uv_heartbeat_enable(cpu);
	return 0;
}

late_initcall(uv_init_heartbeat);

#endif /* !CONFIG_HOTPLUG_CPU */

/*
 * Called on each cpu to initialize the per_cpu UV data area.
 * FIXME: hotplug not supported yet
 */
void __cpuinit uv_cpu_init(void)
{
	/* CPU 0 initilization will be done via uv_system_init. */
	if (!uv_blade_info)
		return;

	uv_blade_info[uv_numa_blade_id()].nr_online_cpus++;

	if (get_uv_system_type() == UV_NON_UNIQUE_APIC)
		set_x2apic_extra_bits(uv_hub_info->pnode);
}


void __init uv_system_init(void)
{
	union uvh_si_addr_map_config_u m_n_config;
	union uvh_node_id_u node_id;
	unsigned long gnode_upper, lowmem_redir_base, lowmem_redir_size;
	int bytes, nid, cpu, lcpu, pnode, blade, i, j, m_val, n_val;
	int gnode_extra, max_pnode = 0;
	unsigned long mmr_base, present, paddr;
	unsigned short pnode_mask;

	m_n_config.v = uv_read_local_mmr(UVH_SI_ADDR_MAP_CONFIG);
	m_val = m_n_config.s.m_skt;
	n_val = m_n_config.s.n_skt;
	mmr_base =
	    uv_read_local_mmr(UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR) &
	    ~UV_MMR_ENABLE;
	pnode_mask = (1 << n_val) - 1;
	node_id.v = uv_read_local_mmr(UVH_NODE_ID);
	gnode_extra = (node_id.s.node_id & ~((1 << n_val) - 1)) >> 1;
	gnode_upper = ((unsigned long)gnode_extra  << m_val);
	printk(KERN_DEBUG "UV: N %d, M %d, gnode_upper 0x%lx, gnode_extra 0x%x\n",
			n_val, m_val, gnode_upper, gnode_extra);

	printk(KERN_DEBUG "UV: global MMR base 0x%lx\n", mmr_base);

	for(i = 0; i < UVH_NODE_PRESENT_TABLE_DEPTH; i++)
		uv_possible_blades +=
		  hweight64(uv_read_local_mmr( UVH_NODE_PRESENT_TABLE + i * 8));
	printk(KERN_DEBUG "UV: Found %d blades\n", uv_num_possible_blades());

	bytes = sizeof(struct uv_blade_info) * uv_num_possible_blades();
	uv_blade_info = kmalloc(bytes, GFP_KERNEL);
	BUG_ON(!uv_blade_info);
	for (blade = 0; blade < uv_num_possible_blades(); blade++)
		uv_blade_info[blade].memory_nid = -1;

	get_lowmem_redirect(&lowmem_redir_base, &lowmem_redir_size);

	bytes = sizeof(uv_node_to_blade[0]) * num_possible_nodes();
	uv_node_to_blade = kmalloc(bytes, GFP_KERNEL);
	BUG_ON(!uv_node_to_blade);
	memset(uv_node_to_blade, 255, bytes);

	bytes = sizeof(uv_cpu_to_blade[0]) * num_possible_cpus();
	uv_cpu_to_blade = kmalloc(bytes, GFP_KERNEL);
	BUG_ON(!uv_cpu_to_blade);
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

	uv_bios_init();
	uv_bios_get_sn_info(0, &uv_type, &sn_partition_id,
			    &sn_coherency_id, &sn_region_size);
	uv_rtc_init();

	for_each_present_cpu(cpu) {
		nid = cpu_to_node(cpu);
		pnode = uv_apicid_to_pnode(per_cpu(x86_cpu_to_apicid, cpu));
		blade = boot_pnode_to_blade(pnode);
		lcpu = uv_blade_info[blade].nr_possible_cpus;
		uv_blade_info[blade].nr_possible_cpus++;

		/* Any node on the blade, else will contain -1. */
		uv_blade_info[blade].memory_nid = nid;

		uv_cpu_hub_info(cpu)->lowmem_remap_base = lowmem_redir_base;
		uv_cpu_hub_info(cpu)->lowmem_remap_top = lowmem_redir_size;
		uv_cpu_hub_info(cpu)->m_val = m_val;
		uv_cpu_hub_info(cpu)->n_val = n_val;
		uv_cpu_hub_info(cpu)->numa_blade_id = blade;
		uv_cpu_hub_info(cpu)->blade_processor_id = lcpu;
		uv_cpu_hub_info(cpu)->pnode = pnode;
		uv_cpu_hub_info(cpu)->pnode_mask = pnode_mask;
		uv_cpu_hub_info(cpu)->gpa_mask = (1UL << (m_val + n_val)) - 1;
		uv_cpu_hub_info(cpu)->gnode_upper = gnode_upper;
		uv_cpu_hub_info(cpu)->gnode_extra = gnode_extra;
		uv_cpu_hub_info(cpu)->global_mmr_base = mmr_base;
		uv_cpu_hub_info(cpu)->coherency_domain_number = sn_coherency_id;
		uv_cpu_hub_info(cpu)->scir.offset = SCIR_LOCAL_MMR_BASE + lcpu;
		uv_node_to_blade[nid] = blade;
		uv_cpu_to_blade[cpu] = blade;
		max_pnode = max(pnode, max_pnode);

		printk(KERN_DEBUG "UV: cpu %d, apicid 0x%x, pnode %d, nid %d, "
			"lcpu %d, blade %d\n",
			cpu, per_cpu(x86_cpu_to_apicid, cpu), pnode, nid,
			lcpu, blade);
	}

	/* Add blade/pnode info for nodes without cpus */
	for_each_online_node(nid) {
		if (uv_node_to_blade[nid] >= 0)
			continue;
		paddr = node_start_pfn(nid) << PAGE_SHIFT;
		paddr = uv_soc_phys_ram_to_gpa(paddr);
		pnode = (paddr >> m_val) & pnode_mask;
		blade = boot_pnode_to_blade(pnode);
		uv_node_to_blade[nid] = blade;
		max_pnode = max(pnode, max_pnode);
	}

	map_gru_high(max_pnode);
	map_mmr_high(max_pnode);
	map_mmioh_high(max_pnode);

	uv_cpu_init();
	uv_scir_register_cpu_notifier();
	proc_mkdir("sgi_uv", NULL);
}
