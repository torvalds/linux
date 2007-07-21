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
#include <acpi/acpi_bus.h>
#include <acpi/acmacros.h>

#define ACPI_NUMA	0x80000000
#define _COMPONENT	ACPI_NUMA
ACPI_MODULE_NAME("numa");

static nodemask_t nodes_found_map = NODE_MASK_NONE;

/* maps to convert between proximity domain and logical node ID */
static int __cpuinitdata pxm_to_node_map[MAX_PXM_DOMAINS]
				= { [0 ... MAX_PXM_DOMAINS - 1] = NID_INVAL };
static int __cpuinitdata node_to_pxm_map[MAX_NUMNODES]
				= { [0 ... MAX_NUMNODES - 1] = PXM_INVAL };

int pxm_to_node(int pxm)
{
	if (pxm < 0)
		return NID_INVAL;
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
	pxm_to_node_map[pxm] = node;
	node_to_pxm_map[node] = pxm;
}

int acpi_map_pxm_to_node(int pxm)
{
	int node = pxm_to_node_map[pxm];

	if (node < 0){
		if (nodes_weight(nodes_found_map) >= MAX_NUMNODES)
			return NID_INVAL;
		node = first_unset_node(nodes_found_map);
		__acpi_map_pxm_to_node(pxm, node);
		node_set(node, nodes_found_map);
	}

	return node;
}

void __cpuinit acpi_unmap_pxm_to_node(int node)
{
	int pxm = node_to_pxm_map[node];
	pxm_to_node_map[pxm] = NID_INVAL;
	node_to_pxm_map[node] = PXM_INVAL;
	node_clear(node, nodes_found_map);
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
					  "SRAT Memory (0x%lx length 0x%lx type 0x%x) in proximity domain %d %s%s\n",
					  (unsigned long)p->base_address,
					  (unsigned long)p->length,
					  p->memory_type, p->proximity_domain,
					  (p->flags & ACPI_SRAT_MEM_ENABLED)?
					  "enabled" : "disabled",
					  (p->flags & ACPI_SRAT_MEM_HOT_PLUGGABLE)?
					  " hot-pluggable" : ""));
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

static int __init acpi_parse_slit(struct acpi_table_header *table)
{
	struct acpi_table_slit *slit;
	u32 localities;

	if (!table)
		return -EINVAL;

	slit = (struct acpi_table_slit *)table;

	/* downcast just for %llu vs %lu for i386/ia64  */
	localities = (u32) slit->locality_count;

	acpi_numa_slit_init(slit);

	return 0;
}

static int __init
acpi_parse_processor_affinity(struct acpi_subtable_header * header,
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
	acpi_numa_memory_affinity_init(memory_affinity);

	return 0;
}

static int __init acpi_parse_srat(struct acpi_table_header *table)
{
	struct acpi_table_srat *srat;

	if (!table)
		return -EINVAL;

	srat = (struct acpi_table_srat *)table;

	return 0;
}

static int __init
acpi_table_parse_srat(enum acpi_srat_type id,
		      acpi_table_entry_handler handler, unsigned int max_entries)
{
	return acpi_table_parse_entries(ACPI_SIG_SRAT,
					    sizeof(struct acpi_table_srat), id,
					    handler, max_entries);
}

int __init acpi_numa_init(void)
{
	/* SRAT: Static Resource Affinity Table */
	if (!acpi_table_parse(ACPI_SIG_SRAT, acpi_parse_srat)) {
		acpi_table_parse_srat(ACPI_SRAT_TYPE_CPU_AFFINITY,
				      acpi_parse_processor_affinity, NR_CPUS);
		acpi_table_parse_srat(ACPI_SRAT_TYPE_MEMORY_AFFINITY,
				      acpi_parse_memory_affinity,
				      NR_NODE_MEMBLKS);
	}

	/* SLIT: System Locality Information Table */
	acpi_table_parse(ACPI_SIG_SLIT, acpi_parse_slit);

	acpi_numa_arch_fixup();
	return 0;
}

int acpi_get_pxm(acpi_handle h)
{
	unsigned long pxm;
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
EXPORT_SYMBOL(acpi_get_pxm);

int acpi_get_node(acpi_handle *handle)
{
	int pxm, node = -1;

	pxm = acpi_get_pxm(handle);
	if (pxm >= 0)
		node = acpi_map_pxm_to_node(pxm);

	return node;
}
EXPORT_SYMBOL(acpi_get_node);
