/*
 * Written by: Patricia Gaughen, IBM Corporation
 *
 * Copyright (C) 2002, IBM Corp.
 * Copyright (C) 2009, Red Hat, Inc., Ingo Molnar
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <gone@us.ibm.com>
 */
#include <linux/nodemask.h>
#include <linux/topology.h>
#include <linux/bootmem.h>
#include <linux/memblock.h>
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/kernel.h>
#include <linux/mmzone.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/numa.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/mm.h>

#include <asm/processor.h>
#include <asm/fixmap.h>
#include <asm/mpspec.h>
#include <asm/numaq.h>
#include <asm/setup.h>
#include <asm/apic.h>
#include <asm/e820.h>
#include <asm/ipi.h>

int found_numaq;

/*
 * Have to match translation table entries to main table entries by counter
 * hence the mpc_record variable .... can't see a less disgusting way of
 * doing this ....
 */
struct mpc_trans {
	unsigned char			mpc_type;
	unsigned char			trans_len;
	unsigned char			trans_type;
	unsigned char			trans_quad;
	unsigned char			trans_global;
	unsigned char			trans_local;
	unsigned short			trans_reserved;
};

static int				mpc_record;

static struct mpc_trans			*translation_table[MAX_MPC_ENTRY];

int					mp_bus_id_to_node[MAX_MP_BUSSES];
int					mp_bus_id_to_local[MAX_MP_BUSSES];
int					quad_local_to_mp_bus_id[NR_CPUS/4][4];


static inline void numaq_register_node(int node, struct sys_cfg_data *scd)
{
	struct eachquadmem *eq = scd->eq + node;
	u64 start = (u64)(eq->hi_shrd_mem_start - eq->priv_mem_size) << 20;
	u64 end = (u64)(eq->hi_shrd_mem_start + eq->hi_shrd_mem_size) << 20;
	int ret;

	node_set(node, numa_nodes_parsed);
	ret = numa_add_memblk(node, start, end);
	BUG_ON(ret < 0);
}

/*
 * Function: smp_dump_qct()
 *
 * Description: gets memory layout from the quad config table.  This
 * function also updates numa_nodes_parsed with the nodes (quads) present.
 */
static void __init smp_dump_qct(void)
{
	struct sys_cfg_data *scd;
	int node;

	scd = (void *)__va(SYS_CFG_DATA_PRIV_ADDR);

	for_each_node(node) {
		if (scd->quads_present31_0 & (1 << node))
			numaq_register_node(node, scd);
	}
}

void __cpuinit numaq_tsc_disable(void)
{
	if (!found_numaq)
		return;

	if (num_online_nodes() > 1) {
		printk(KERN_DEBUG "NUMAQ: disabling TSC\n");
		setup_clear_cpu_cap(X86_FEATURE_TSC);
	}
}

static void __init numaq_tsc_init(void)
{
	numaq_tsc_disable();
}

static inline int generate_logical_apicid(int quad, int phys_apicid)
{
	return (quad << 4) + (phys_apicid ? phys_apicid << 1 : 1);
}

/* x86_quirks member */
static int mpc_apic_id(struct mpc_cpu *m)
{
	int quad = translation_table[mpc_record]->trans_quad;
	int logical_apicid = generate_logical_apicid(quad, m->apicid);

	printk(KERN_DEBUG
		"Processor #%d %u:%u APIC version %d (quad %d, apic %d)\n",
		 m->apicid, (m->cpufeature & CPU_FAMILY_MASK) >> 8,
		(m->cpufeature & CPU_MODEL_MASK) >> 4,
		 m->apicver, quad, logical_apicid);

	return logical_apicid;
}

/* x86_quirks member */
static void mpc_oem_bus_info(struct mpc_bus *m, char *name)
{
	int quad = translation_table[mpc_record]->trans_quad;
	int local = translation_table[mpc_record]->trans_local;

	mp_bus_id_to_node[m->busid] = quad;
	mp_bus_id_to_local[m->busid] = local;

	printk(KERN_INFO "Bus #%d is %s (node %d)\n", m->busid, name, quad);
}

/* x86_quirks member */
static void mpc_oem_pci_bus(struct mpc_bus *m)
{
	int quad = translation_table[mpc_record]->trans_quad;
	int local = translation_table[mpc_record]->trans_local;

	quad_local_to_mp_bus_id[quad][local] = m->busid;
}

/*
 * Called from mpparse code.
 * mode = 0: prescan
 * mode = 1: one mpc entry scanned
 */
static void numaq_mpc_record(unsigned int mode)
{
	if (!mode)
		mpc_record = 0;
	else
		mpc_record++;
}

static void __init MP_translation_info(struct mpc_trans *m)
{
	printk(KERN_INFO
	    "Translation: record %d, type %d, quad %d, global %d, local %d\n",
	       mpc_record, m->trans_type, m->trans_quad, m->trans_global,
	       m->trans_local);

	if (mpc_record >= MAX_MPC_ENTRY)
		printk(KERN_ERR "MAX_MPC_ENTRY exceeded!\n");
	else
		translation_table[mpc_record] = m; /* stash this for later */

	if (m->trans_quad < MAX_NUMNODES && !node_online(m->trans_quad))
		node_set_online(m->trans_quad);
}

static int __init mpf_checksum(unsigned char *mp, int len)
{
	int sum = 0;

	while (len--)
		sum += *mp++;

	return sum & 0xFF;
}

/*
 * Read/parse the MPC oem tables
 */
static void __init smp_read_mpc_oem(struct mpc_table *mpc)
{
	struct mpc_oemtable *oemtable = (void *)(long)mpc->oemptr;
	int count = sizeof(*oemtable);	/* the header size */
	unsigned char *oemptr = ((unsigned char *)oemtable) + count;

	mpc_record = 0;
	printk(KERN_INFO
		"Found an OEM MPC table at %8p - parsing it...\n", oemtable);

	if (memcmp(oemtable->signature, MPC_OEM_SIGNATURE, 4)) {
		printk(KERN_WARNING
		       "SMP mpc oemtable: bad signature [%c%c%c%c]!\n",
		       oemtable->signature[0], oemtable->signature[1],
		       oemtable->signature[2], oemtable->signature[3]);
		return;
	}

	if (mpf_checksum((unsigned char *)oemtable, oemtable->length)) {
		printk(KERN_WARNING "SMP oem mptable: checksum error!\n");
		return;
	}

	while (count < oemtable->length) {
		switch (*oemptr) {
		case MP_TRANSLATION:
			{
				struct mpc_trans *m = (void *)oemptr;

				MP_translation_info(m);
				oemptr += sizeof(*m);
				count += sizeof(*m);
				++mpc_record;
				break;
			}
		default:
			printk(KERN_WARNING
			       "Unrecognised OEM table entry type! - %d\n",
			       (int)*oemptr);
			return;
		}
	}
}

static __init void early_check_numaq(void)
{
	/*
	 * get boot-time SMP configuration:
	 */
	if (smp_found_config)
		early_get_smp_config();

	if (found_numaq) {
		x86_init.mpparse.mpc_record = numaq_mpc_record;
		x86_init.mpparse.setup_ioapic_ids = x86_init_noop;
		x86_init.mpparse.mpc_apic_id = mpc_apic_id;
		x86_init.mpparse.smp_read_mpc_oem = smp_read_mpc_oem;
		x86_init.mpparse.mpc_oem_pci_bus = mpc_oem_pci_bus;
		x86_init.mpparse.mpc_oem_bus_info = mpc_oem_bus_info;
		x86_init.timers.tsc_pre_init = numaq_tsc_init;
		x86_init.pci.init = pci_numaq_init;
	}
}

int __init numaq_numa_init(void)
{
	early_check_numaq();
	if (!found_numaq)
		return -ENOENT;
	smp_dump_qct();

	return 0;
}

#define NUMAQ_APIC_DFR_VALUE	(APIC_DFR_CLUSTER)

static inline unsigned int numaq_get_apic_id(unsigned long x)
{
	return (x >> 24) & 0x0F;
}

static inline void numaq_send_IPI_mask(const struct cpumask *mask, int vector)
{
	default_send_IPI_mask_sequence_logical(mask, vector);
}

static inline void numaq_send_IPI_allbutself(int vector)
{
	default_send_IPI_mask_allbutself_logical(cpu_online_mask, vector);
}

static inline void numaq_send_IPI_all(int vector)
{
	numaq_send_IPI_mask(cpu_online_mask, vector);
}

#define NUMAQ_TRAMPOLINE_PHYS_LOW	(0x8)
#define NUMAQ_TRAMPOLINE_PHYS_HIGH	(0xa)

/*
 * Because we use NMIs rather than the INIT-STARTUP sequence to
 * bootstrap the CPUs, the APIC may be in a weird state. Kick it:
 */
static inline void numaq_smp_callin_clear_local_apic(void)
{
	clear_local_APIC();
}

static inline const struct cpumask *numaq_target_cpus(void)
{
	return cpu_all_mask;
}

static unsigned long numaq_check_apicid_used(physid_mask_t *map, int apicid)
{
	return physid_isset(apicid, *map);
}

static inline unsigned long numaq_check_apicid_present(int bit)
{
	return physid_isset(bit, phys_cpu_present_map);
}

static inline int numaq_apic_id_registered(void)
{
	return 1;
}

static inline void numaq_init_apic_ldr(void)
{
	/* Already done in NUMA-Q firmware */
}

static inline void numaq_setup_apic_routing(void)
{
	printk(KERN_INFO
		"Enabling APIC mode:  NUMA-Q.  Using %d I/O APICs\n",
		nr_ioapics);
}

/*
 * Skip adding the timer int on secondary nodes, which causes
 * a small but painful rift in the time-space continuum.
 */
static inline int numaq_multi_timer_check(int apic, int irq)
{
	return apic != 0 && irq == 0;
}

static inline void numaq_ioapic_phys_id_map(physid_mask_t *phys_map, physid_mask_t *retmap)
{
	/* We don't have a good way to do this yet - hack */
	return physids_promote(0xFUL, retmap);
}

/*
 * Supporting over 60 cpus on NUMA-Q requires a locality-dependent
 * cpu to APIC ID relation to properly interact with the intelligent
 * mode of the cluster controller.
 */
static inline int numaq_cpu_present_to_apicid(int mps_cpu)
{
	if (mps_cpu < 60)
		return ((mps_cpu >> 2) << 4) | (1 << (mps_cpu & 0x3));
	else
		return BAD_APICID;
}

static inline int numaq_apicid_to_node(int logical_apicid)
{
	return logical_apicid >> 4;
}

static int numaq_numa_cpu_node(int cpu)
{
	int logical_apicid = early_per_cpu(x86_cpu_to_logical_apicid, cpu);

	if (logical_apicid != BAD_APICID)
		return numaq_apicid_to_node(logical_apicid);
	return NUMA_NO_NODE;
}

static void numaq_apicid_to_cpu_present(int logical_apicid, physid_mask_t *retmap)
{
	int node = numaq_apicid_to_node(logical_apicid);
	int cpu = __ffs(logical_apicid & 0xf);

	physid_set_mask_of_physid(cpu + 4*node, retmap);
}

/* Where the IO area was mapped on multiquad, always 0 otherwise */
void *xquad_portio;

static inline int numaq_check_phys_apicid_present(int phys_apicid)
{
	return 1;
}

/*
 * We use physical apicids here, not logical, so just return the default
 * physical broadcast to stop people from breaking us
 */
static unsigned int numaq_cpu_mask_to_apicid(const struct cpumask *cpumask)
{
	return 0x0F;
}

static inline unsigned int
numaq_cpu_mask_to_apicid_and(const struct cpumask *cpumask,
			     const struct cpumask *andmask)
{
	return 0x0F;
}

/* No NUMA-Q box has a HT CPU, but it can't hurt to use the default code. */
static inline int numaq_phys_pkg_id(int cpuid_apic, int index_msb)
{
	return cpuid_apic >> index_msb;
}

static int
numaq_mps_oem_check(struct mpc_table *mpc, char *oem, char *productid)
{
	if (strncmp(oem, "IBM NUMA", 8))
		printk(KERN_ERR "Warning! Not a NUMA-Q system!\n");
	else
		found_numaq = 1;

	return found_numaq;
}

static int probe_numaq(void)
{
	/* already know from get_memcfg_numaq() */
	return found_numaq;
}

static void numaq_vector_allocation_domain(int cpu, struct cpumask *retmask)
{
	/* Careful. Some cpus do not strictly honor the set of cpus
	 * specified in the interrupt destination when using lowest
	 * priority interrupt delivery mode.
	 *
	 * In particular there was a hyperthreading cpu observed to
	 * deliver interrupts to the wrong hyperthread when only one
	 * hyperthread was specified in the interrupt desitination.
	 */
	cpumask_clear(retmask);
	cpumask_bits(retmask)[0] = APIC_ALL_CPUS;
}

static void numaq_setup_portio_remap(void)
{
	int num_quads = num_online_nodes();

	if (num_quads <= 1)
		return;

	printk(KERN_INFO
		"Remapping cross-quad port I/O for %d quads\n", num_quads);

	xquad_portio = ioremap(XQUAD_PORTIO_BASE, num_quads*XQUAD_PORTIO_QUAD);

	printk(KERN_INFO
		"xquad_portio vaddr 0x%08lx, len %08lx\n",
		(u_long) xquad_portio, (u_long) num_quads*XQUAD_PORTIO_QUAD);
}

/* Use __refdata to keep false positive warning calm.  */
static struct apic __refdata apic_numaq = {

	.name				= "NUMAQ",
	.probe				= probe_numaq,
	.acpi_madt_oem_check		= NULL,
	.apic_id_valid			= default_apic_id_valid,
	.apic_id_registered		= numaq_apic_id_registered,

	.irq_delivery_mode		= dest_LowestPrio,
	/* physical delivery on LOCAL quad: */
	.irq_dest_mode			= 0,

	.target_cpus			= numaq_target_cpus,
	.disable_esr			= 1,
	.dest_logical			= APIC_DEST_LOGICAL,
	.check_apicid_used		= numaq_check_apicid_used,
	.check_apicid_present		= numaq_check_apicid_present,

	.vector_allocation_domain	= numaq_vector_allocation_domain,
	.init_apic_ldr			= numaq_init_apic_ldr,

	.ioapic_phys_id_map		= numaq_ioapic_phys_id_map,
	.setup_apic_routing		= numaq_setup_apic_routing,
	.multi_timer_check		= numaq_multi_timer_check,
	.cpu_present_to_apicid		= numaq_cpu_present_to_apicid,
	.apicid_to_cpu_present		= numaq_apicid_to_cpu_present,
	.setup_portio_remap		= numaq_setup_portio_remap,
	.check_phys_apicid_present	= numaq_check_phys_apicid_present,
	.enable_apic_mode		= NULL,
	.phys_pkg_id			= numaq_phys_pkg_id,
	.mps_oem_check			= numaq_mps_oem_check,

	.get_apic_id			= numaq_get_apic_id,
	.set_apic_id			= NULL,
	.apic_id_mask			= 0x0F << 24,

	.cpu_mask_to_apicid		= numaq_cpu_mask_to_apicid,
	.cpu_mask_to_apicid_and		= numaq_cpu_mask_to_apicid_and,

	.send_IPI_mask			= numaq_send_IPI_mask,
	.send_IPI_mask_allbutself	= NULL,
	.send_IPI_allbutself		= numaq_send_IPI_allbutself,
	.send_IPI_all			= numaq_send_IPI_all,
	.send_IPI_self			= default_send_IPI_self,

	.wakeup_secondary_cpu		= wakeup_secondary_cpu_via_nmi,
	.trampoline_phys_low		= NUMAQ_TRAMPOLINE_PHYS_LOW,
	.trampoline_phys_high		= NUMAQ_TRAMPOLINE_PHYS_HIGH,

	/* We don't do anything here because we use NMI's to boot instead */
	.wait_for_init_deassert		= NULL,

	.smp_callin_clear_local_apic	= numaq_smp_callin_clear_local_apic,
	.inquire_remote_apic		= NULL,

	.read				= native_apic_mem_read,
	.write				= native_apic_mem_write,
	.eoi_write			= native_apic_mem_write,
	.icr_read			= native_apic_icr_read,
	.icr_write			= native_apic_icr_write,
	.wait_icr_idle			= native_apic_wait_icr_idle,
	.safe_wait_icr_idle		= native_safe_apic_wait_icr_idle,

	.x86_32_early_logical_apicid	= noop_x86_32_early_logical_apicid,
	.x86_32_numa_cpu_node		= numaq_numa_cpu_node,
};

apic_driver(apic_numaq);
