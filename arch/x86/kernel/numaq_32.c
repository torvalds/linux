/*
 * Written by: Patricia Gaughen, IBM Corporation
 *
 * Copyright (C) 2002, IBM Corp.
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

#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/mmzone.h>
#include <linux/module.h>
#include <linux/nodemask.h>
#include <asm/numaq.h>
#include <asm/topology.h>
#include <asm/processor.h>
#include <asm/mpspec.h>
#include <asm/e820.h>
#include <asm/setup.h>

#define	MB_TO_PAGES(addr) ((addr) << (20 - PAGE_SHIFT))

/*
 * Function: smp_dump_qct()
 *
 * Description: gets memory layout from the quad config table.  This
 * function also updates node_online_map with the nodes (quads) present.
 */
static void __init smp_dump_qct(void)
{
	int node;
	struct eachquadmem *eq;
	struct sys_cfg_data *scd =
		(struct sys_cfg_data *)__va(SYS_CFG_DATA_PRIV_ADDR);

	nodes_clear(node_online_map);
	for_each_node(node) {
		if (scd->quads_present31_0 & (1 << node)) {
			node_set_online(node);
			eq = &scd->eq[node];
			/* Convert to pages */
			node_start_pfn[node] = MB_TO_PAGES(
				eq->hi_shrd_mem_start - eq->priv_mem_size);
			node_end_pfn[node] = MB_TO_PAGES(
				eq->hi_shrd_mem_start + eq->hi_shrd_mem_size);

			e820_register_active_regions(node, node_start_pfn[node],
							node_end_pfn[node]);
			memory_present(node,
				node_start_pfn[node], node_end_pfn[node]);
			node_remap_size[node] = node_memmap_size_bytes(node,
							node_start_pfn[node],
							node_end_pfn[node]);
		}
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

static int __init numaq_pre_time_init(void)
{
	numaq_tsc_disable();
	return 0;
}

int found_numaq;
/*
 * Have to match translation table entries to main table entries by counter
 * hence the mpc_record variable .... can't see a less disgusting way of
 * doing this ....
 */
struct mpc_config_translation {
	unsigned char mpc_type;
	unsigned char trans_len;
	unsigned char trans_type;
	unsigned char trans_quad;
	unsigned char trans_global;
	unsigned char trans_local;
	unsigned short trans_reserved;
};

/* x86_quirks member */
static int mpc_record;
static struct mpc_config_translation *translation_table[MAX_MPC_ENTRY]
    __cpuinitdata;

static inline int generate_logical_apicid(int quad, int phys_apicid)
{
	return (quad << 4) + (phys_apicid ? phys_apicid << 1 : 1);
}

/* x86_quirks member */
static int mpc_apic_id(struct mpc_config_processor *m)
{
	int quad = translation_table[mpc_record]->trans_quad;
	int logical_apicid = generate_logical_apicid(quad, m->mpc_apicid);

	printk(KERN_DEBUG "Processor #%d %u:%u APIC version %d (quad %d, apic %d)\n",
	       m->mpc_apicid,
	       (m->mpc_cpufeature & CPU_FAMILY_MASK) >> 8,
	       (m->mpc_cpufeature & CPU_MODEL_MASK) >> 4,
	       m->mpc_apicver, quad, logical_apicid);
	return logical_apicid;
}

int mp_bus_id_to_node[MAX_MP_BUSSES];

int mp_bus_id_to_local[MAX_MP_BUSSES];

/* x86_quirks member */
static void mpc_oem_bus_info(struct mpc_config_bus *m, char *name)
{
	int quad = translation_table[mpc_record]->trans_quad;
	int local = translation_table[mpc_record]->trans_local;

	mp_bus_id_to_node[m->mpc_busid] = quad;
	mp_bus_id_to_local[m->mpc_busid] = local;
	printk(KERN_INFO "Bus #%d is %s (node %d)\n",
	       m->mpc_busid, name, quad);
}

int quad_local_to_mp_bus_id [NR_CPUS/4][4];

/* x86_quirks member */
static void mpc_oem_pci_bus(struct mpc_config_bus *m)
{
	int quad = translation_table[mpc_record]->trans_quad;
	int local = translation_table[mpc_record]->trans_local;

	quad_local_to_mp_bus_id[quad][local] = m->mpc_busid;
}

static void __init MP_translation_info(struct mpc_config_translation *m)
{
	printk(KERN_INFO
	       "Translation: record %d, type %d, quad %d, global %d, local %d\n",
	       mpc_record, m->trans_type, m->trans_quad, m->trans_global,
	       m->trans_local);

	if (mpc_record >= MAX_MPC_ENTRY)
		printk(KERN_ERR "MAX_MPC_ENTRY exceeded!\n");
	else
		translation_table[mpc_record] = m;	/* stash this for later */
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

static void __init smp_read_mpc_oem(struct mp_config_oemtable *oemtable,
				    unsigned short oemsize)
{
	int count = sizeof(*oemtable);	/* the header size */
	unsigned char *oemptr = ((unsigned char *)oemtable) + count;

	mpc_record = 0;
	printk(KERN_INFO "Found an OEM MPC table at %8p - parsing it ... \n",
	       oemtable);
	if (memcmp(oemtable->oem_signature, MPC_OEM_SIGNATURE, 4)) {
		printk(KERN_WARNING
		       "SMP mpc oemtable: bad signature [%c%c%c%c]!\n",
		       oemtable->oem_signature[0], oemtable->oem_signature[1],
		       oemtable->oem_signature[2], oemtable->oem_signature[3]);
		return;
	}
	if (mpf_checksum((unsigned char *)oemtable, oemtable->oem_length)) {
		printk(KERN_WARNING "SMP oem mptable: checksum error!\n");
		return;
	}
	while (count < oemtable->oem_length) {
		switch (*oemptr) {
		case MP_TRANSLATION:
			{
				struct mpc_config_translation *m =
				    (struct mpc_config_translation *)oemptr;
				MP_translation_info(m);
				oemptr += sizeof(*m);
				count += sizeof(*m);
				++mpc_record;
				break;
			}
		default:
			{
				printk(KERN_WARNING
				       "Unrecognised OEM table entry type! - %d\n",
				       (int)*oemptr);
				return;
			}
		}
	}
}

static struct x86_quirks numaq_x86_quirks __initdata = {
	.arch_pre_time_init	= numaq_pre_time_init,
	.arch_time_init		= NULL,
	.arch_pre_intr_init	= NULL,
	.arch_memory_setup	= NULL,
	.arch_intr_init		= NULL,
	.arch_trap_init		= NULL,
	.mach_get_smp_config	= NULL,
	.mach_find_smp_config	= NULL,
	.mpc_record		= &mpc_record,
	.mpc_apic_id		= mpc_apic_id,
	.mpc_oem_bus_info	= mpc_oem_bus_info,
	.mpc_oem_pci_bus	= mpc_oem_pci_bus,
	.smp_read_mpc_oem	= smp_read_mpc_oem,
};

void numaq_mps_oem_check(struct mp_config_table *mpc, char *oem,
				 char *productid)
{
	if (strncmp(oem, "IBM NUMA", 8))
		printk("Warning!  Not a NUMA-Q system!\n");
	else
		found_numaq = 1;
}

static __init void early_check_numaq(void)
{
	/*
	 * Find possible boot-time SMP configuration:
	 */
	early_find_smp_config();
	/*
	 * get boot-time SMP configuration:
	 */
	if (smp_found_config)
		early_get_smp_config();

	if (found_numaq)
		x86_quirks = &numaq_x86_quirks;
}

int __init get_memcfg_numaq(void)
{
	early_check_numaq();
	if (!found_numaq)
		return 0;
	smp_dump_qct();
	return 1;
}
