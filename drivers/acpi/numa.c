/*
 *  acpi_numa.c - ACPI NUMA support
 *
 *  Copyright (C) 2002 Takayoshi Kochi <t-kochi@bq.jp.nec.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/acpi.h>
#include <linux/numa.h>
#include <acpi/acpi_bus.h>

#define PREFIX "ACPI: "

#define ACPI_NUMA	0x80000000
#define _COMPONENT	ACPI_NUMA
ACPI_MODULE_NAME("numa");

static nodemask_t nodes_found_map = NODE_MASK_NONE;

/* maps to convert between proximity domain and logical node ID */
static int pxm_to_node_map[MAX_PXM_DOMAINS]
			= { [0 ... MAX_PXM_DOMAINS - 1] = NUMA_NO_NODE };
static int node_to_pxm_map[MAX_NUMNODES]
			= { [0 ... MAX_NUMNODES - 1] = PXM_INVAL };

unsigned char acpi_srat_revision __initdata;

int pxm_to_node(int pxm)
{
	if (pxm < 0)
		return NUMA_NO_NODE;
	return pxm_to_node_map[pxm];
}

int node_to_pxm(int node)
{
	if (node < 0)
		return PXM_INVAL;
	return node_to_pxm_map[node];
}

void __acpi_map_pxm_to_node(int pxm, int node)
{
	if (pxm_to_node_map[pxm] == NUMA_NO_NODE || node < pxm_to_node_map[pxm])
		pxm_to_node_map[pxm] = node;
	if (node_to_pxm_map[node] == PXM_INVAL || pxm < node_to_pxm_map[node])
		node_to_pxm_map[node] = pxm;
}

int acpi_map_pxm_to_node(int pxm)
{
	int node = pxm_to_node_map[pxm];

	if (node < 0) {
		if (nodes_weight(nodes_found_map) >= MAX_NUMNODES)
			return NUMA_NO_NODE;
		node = first_unset_node(nodes_found_map);
		__acpi_map_pxm_to_node(pxm, node);
		node_set(node, nodes_found_map);
	}

	return node;
}

static void __init
acpi_table_print_srat_entry(struct acpi_subtable_header *header)
{

	ACPI_FUNCTION_NAME("acpi_table_print_srat_entry");

	if (!header)
		return;

	switch (header->type) {

	case ACPI_SRAT_TYPE_CPU_AFFINITY:
#ifdef ACPI_DEBUG_OUTPUT
		{
			struct acpi_srat_cpu_affinity *p =
			    (struct acpi_srat_cpu_affinity *)header;
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					  "SRAT Processor (id[0x%02x] eid[0x%02x]) in proximity domain %d %s\n",
					  p->apic_id, p->local_sapic_eid,
					  p->proximity_domain_lo,
					  (p->flags & ACPI_SRAT_CPU_ENABLED)?
					  "enabled" : "disabled"));
		}
#endif				/* ACPI_DEBUG_OUTPUT */
		break;

	case ACPI_SRAT_TYPE_MEMORY_AFFINITY:
#ifdef ACPI_DEBUG_OUTPUT
		{
			struct acpi_srat_mem_affinity *p =
			    (struct acpi_srat_mem_affinity *)header;
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					  "SRAT Memory (0x%lx length 0x%lx) in proximity domain %d %s%s%s\n",
					  (unsigned long)p->base_address,
					  (unsigned long)p->length,
					  p->proximity_domain,
					  (p->flags & ACPI_SRAT_MEM_ENABLED)?
					  "enabled" : "disabled",
					  (p->flags & ACPI_SRAT_MEM_HOT_PLUGGABLE)?
					  " hot-pluggable" : "",
					  (p->flags & ACPI_SRAT_MEM_NON_VOLATILE)?
					  " non-volatile" : ""));
		}
#endif				/* ACPI_DEBUG_OUTPUT */
		break;

	case ACPI_SRAT_TYPE_X2APIC_CPU_AFFINITY:
#ifdef ACPI_DEBUG_OUTPUT
		{
			struct acpi_srat_x2apic_cpu_affinity *p =
			    (struct acpi_srat_x2apic_cpu_affinity *)header;
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					  "SRAT Processor (x2apicid[0x%08x]) in"
					  " proximity domain %d %s\n",
					  p->apic_id,
					  p->proximity_domain,
					  (p->flags & ACPI_SRAT_CPU_ENABLED) ?
					  "enabled" : "disabled"));
		}
#endif				/* ACPI_DEBUG_OUTPUT */
		break;
	default:
		printk(KERN_WARNING PREFIX
		       "Found unsupported SRAT entry (type = 0x%x)\n",
		       header->type);
		break;
	}
}

/*
 * A lot of BIOS fill in 10 (= no distance) everywhere. This messes
 * up the NUMA heuristics which wants the local node to have a smaller
 * distance than the others.
 * Do some quick checks here and only use the SLIT if it passes.
 */
static int __init slit_valid(struct acpi_table_slit *slit)
{
	int i, j;
	int d = slit->locality_count;
	for (i = 0; i < d; i++) {
		for (j = 0; j < d; j++)  {
			u8 val = slit->entry[d*i + j];
			if (i == j) {
				if (val != LOCAL_DISTANCE)
					return 0;
			} else if (val <= LOCAL_DISTANCE)
				return 0;
		}
	}
	return 1;
}

static int __init acpi_parse_slit(struct acpi_table_header *table)
{
	struct acpi_table_slit *slit;

	if (!table)
		return -EINVAL;

	slit = (struct acpi_table_slit *)table;

	if (!slit_valid(slit)) {
		printk(KERN_INFO "ACPI: SLIT table looks invalid. Not used.\n");
		return -EINVAL;
	}
	acpi_numa_slit_init(slit);

	return 0;
}

void __init __attribute__ ((weak))
acpi_numa_x2apic_affinity_init(struct acpi_srat_x2apic_cpu_affinity *pa)
{
	printk(KERN_WARNING PREFIX
	       "Found unsupported x2apic [0x%08x] SRAT entry\n", pa->apic_id);
	return;
}


static int __init
acpi_parse_x2apic_affinity(struct acpi_subtable_header *header,
			   const unsigned long end)
{
	struct acpi_srat_x2apic_cpu_affinity *processor_affinity;

	processor_affinity = (struct acpi_srat_x2apic_cpu_affinity *)header;
	if (!processor_affinity)
		return -EINVAL;

	acpi_table_print_srat_entry(header);

	/* let architecture-dependent part to do it */
	acpi_numa_x2apic_affinity_init(processor_affinity);

	return 0;
}

static int __init
acpi_parse_processor_affinity(struct acpi_subtable_header *header,
			      const unsigned long end)
{
	struct acpi_srat_cpu_affinity *processor_affinity;

	processor_affinity = (struct acpi_srat_cpu_affinity *)header;
	if (!processor_affinity)
		return -EINVAL;

	acpi_table_print_srat_entry(header);

	/* let architecture-dependent part to do it */
	acpi_numa_processor_affinity_init(processor_affinity);

	return 0;
}

static int __initdata parsed_numa_memblks;

static int __init
acpi_parse_memory_affinity(struct acpi_subtable_header * header,
			   const unsigned long end)
{
	struct acpi_srat_mem_affinity *memory_affinity;

	memory_affinity = (struct acpi_srat_mem_affinity *)header;
	if (!memory_affinity)
		return -EINVAL;

	acpi_table_print_srat_entry(header);

	/* let architecture-dependent part to do it */
	if (!acpi_numa_memory_affinity_init(memory_affinity))
		parsed_numa_memblks++;
	return 0;
}

static int __init acpi_parse_srat(struct acpi_table_header *table)
{
	struct acpi_table_srat *srat;
	if (!table)
		return -EINVAL;

	srat = (struct acpi_table_srat *)table;
	acpi_srat_revision = srat->header.revision;

	/* Real work done in acpi_table_parse_srat below. */

	return 0;
}

static int __init
acpi_table_parse_srat(enum acpi_srat_type id,
		      acpi_tbl_entry_handler handler, unsigned int max_entries)
{
	return acpi_table_parse_entries(ACPI_SIG_SRAT,
					    sizeof(struct acpi_table_srat), id,
					    handler, max_entries);
}

int __init acpi_numa_init(void)
{
	int cnt = 0;

	/*
	 * Should not limit number with cpu num that is from NR_CPUS or nr_cpus=
	 * SRAT cpu entries could have different order with that in MADT.
	 * So go over all cpu entries in SRAT to get apicid to node mapping.
	 */

	/* SRAT: Static Resource Affinity Table */
	if (!acpi_table_parse(ACPI_SIG_SRAT, acpi_parse_srat)) {
		acpi_table_parse_srat(ACPI_SRAT_TYPE_X2APIC_CPU_AFFINITY,
				     acpi_parse_x2apic_affinity, 0);
		acpi_table_parse_srat(ACPI_SRAT_TYPE_CPU_AFFINITY,
				     acpi_parse_processor_affinity, 0);
		cnt = acpi_table_parse_srat(ACPI_SRAT_TYPE_MEMORY_AFFINITY,
					    acpi_parse_memory_affinity,
					    NR_NODE_MEMBLKS);
	}

	/* SLIT: System Locality Information Table */
	acpi_table_parse(ACPI_SIG_SLIT, acpi_parse_slit);

	acpi_numa_arch_fixup();

	if (cnt < 0)
		return cnt;
	else if (!parsed_numa_memblks)
		return -ENOENT;
	return 0;
}

int acpi_get_pxm(acpi_handle h)
{
	unsigned long long pxm;
	acpi_status status;
	acpi_handle handle;
	acpi_handle phandle = h;

	do {
		handle = phandle;
		status = acpi_evaluate_integer(handle, "_PXM", NULL, &pxm);
		if (ACPI_SUCCESS(status))
			return pxm;
		status = acpi_get_parent(handle, &phandle);
	} while (ACPI_SUCCESS(status));
	return -1;
}

int acpi_get_node(acpi_handle *handle)
{
	int pxm, node = -1;

	pxm = acpi_get_pxm(handle);
	if (pxm >= 0 && pxm < MAX_PXM_DOMAINS)
		node = acpi_map_pxm_to_node(pxm);

	return node;
}
EXPORT_SYMBOL(acpi_get_node);
