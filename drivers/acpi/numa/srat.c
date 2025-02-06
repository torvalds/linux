// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  acpi_numa.c - ACPI NUMA support
 *
 *  Copyright (C) 2002 Takayoshi Kochi <t-kochi@bq.jp.nec.com>
 */

#define pr_fmt(fmt) "ACPI: " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/acpi.h>
#include <linux/memblock.h>
#include <linux/numa.h>
#include <linux/nodemask.h>
#include <linux/topology.h>
#include <linux/numa_memblks.h>

static nodemask_t nodes_found_map = NODE_MASK_NONE;

/* maps to convert between proximity domain and logical node ID */
static int pxm_to_node_map[MAX_PXM_DOMAINS]
			= { [0 ... MAX_PXM_DOMAINS - 1] = NUMA_NO_NODE };
static int node_to_pxm_map[MAX_NUMNODES]
			= { [0 ... MAX_NUMNODES - 1] = PXM_INVAL };

unsigned char acpi_srat_revision __initdata;
static int acpi_numa __initdata;

static int last_real_pxm;

void __init disable_srat(void)
{
	acpi_numa = -1;
}

int pxm_to_node(int pxm)
{
	if (pxm < 0 || pxm >= MAX_PXM_DOMAINS || numa_off)
		return NUMA_NO_NODE;
	return pxm_to_node_map[pxm];
}
EXPORT_SYMBOL(pxm_to_node);

int node_to_pxm(int node)
{
	if (node < 0)
		return PXM_INVAL;
	return node_to_pxm_map[node];
}

static void __acpi_map_pxm_to_node(int pxm, int node)
{
	if (pxm_to_node_map[pxm] == NUMA_NO_NODE || node < pxm_to_node_map[pxm])
		pxm_to_node_map[pxm] = node;
	if (node_to_pxm_map[node] == PXM_INVAL || pxm < node_to_pxm_map[node])
		node_to_pxm_map[node] = pxm;
}

int acpi_map_pxm_to_node(int pxm)
{
	int node;

	if (pxm < 0 || pxm >= MAX_PXM_DOMAINS || numa_off)
		return NUMA_NO_NODE;

	node = pxm_to_node_map[pxm];

	if (node == NUMA_NO_NODE) {
		node = first_unset_node(nodes_found_map);
		if (node >= MAX_NUMNODES)
			return NUMA_NO_NODE;
		__acpi_map_pxm_to_node(pxm, node);
		node_set(node, nodes_found_map);
	}

	return node;
}
EXPORT_SYMBOL(acpi_map_pxm_to_node);

#ifdef CONFIG_NUMA_EMU
/*
 * Take max_nid - 1 fake-numa nodes into account in both
 * pxm_to_node_map()/node_to_pxm_map[] tables.
 */
int __init fix_pxm_node_maps(int max_nid)
{
	static int pxm_to_node_map_copy[MAX_PXM_DOMAINS] __initdata
			= { [0 ... MAX_PXM_DOMAINS - 1] = NUMA_NO_NODE };
	static int node_to_pxm_map_copy[MAX_NUMNODES] __initdata
			= { [0 ... MAX_NUMNODES - 1] = PXM_INVAL };
	int i, j, index = -1, count = 0;
	nodemask_t nodes_to_enable;

	if (numa_off)
		return -1;

	/* no or incomplete node/PXM mapping set, nothing to do */
	if (srat_disabled())
		return 0;

	/* find fake nodes PXM mapping */
	for (i = 0; i < MAX_NUMNODES; i++) {
		if (node_to_pxm_map[i] != PXM_INVAL) {
			for (j = 0; j <= max_nid; j++) {
				if ((emu_nid_to_phys[j] == i) &&
				    WARN(node_to_pxm_map_copy[j] != PXM_INVAL,
					 "Node %d is already binded to PXM %d\n",
					 j, node_to_pxm_map_copy[j]))
					return -1;
				if (emu_nid_to_phys[j] == i) {
					node_to_pxm_map_copy[j] =
						node_to_pxm_map[i];
					if (j > index)
						index = j;
					count++;
				}
			}
		}
	}
	if (index == -1) {
		pr_debug("No node/PXM mapping has been set\n");
		/* nothing more to be done */
		return 0;
	}
	if (WARN(index != max_nid, "%d max nid  when expected %d\n",
		      index, max_nid))
		return -1;

	nodes_clear(nodes_to_enable);

	/* map phys nodes not used for fake nodes */
	for (i = 0; i < MAX_NUMNODES; i++) {
		if (node_to_pxm_map[i] != PXM_INVAL) {
			for (j = 0; j <= max_nid; j++)
				if (emu_nid_to_phys[j] == i)
					break;
			/* fake nodes PXM mapping has been done */
			if (j <= max_nid)
				continue;
			/* find first hole */
			for (j = 0;
			     j < MAX_NUMNODES &&
				 node_to_pxm_map_copy[j] != PXM_INVAL;
			     j++)
			;
			if (WARN(j == MAX_NUMNODES,
			    "Number of nodes exceeds MAX_NUMNODES\n"))
				return -1;
			node_to_pxm_map_copy[j] = node_to_pxm_map[i];
			node_set(j, nodes_to_enable);
			count++;
		}
	}

	/* creating reverse mapping in pxm_to_node_map[] */
	for (i = 0; i < MAX_NUMNODES; i++)
		if (node_to_pxm_map_copy[i] != PXM_INVAL &&
		    pxm_to_node_map_copy[node_to_pxm_map_copy[i]] == NUMA_NO_NODE)
			pxm_to_node_map_copy[node_to_pxm_map_copy[i]] = i;

	/* overwrite with new mapping */
	for (i = 0; i < MAX_NUMNODES; i++) {
		node_to_pxm_map[i] = node_to_pxm_map_copy[i];
		pxm_to_node_map[i] = pxm_to_node_map_copy[i];
	}

	/* enable other nodes found in PXM for hotplug */
	nodes_or(numa_nodes_parsed, nodes_to_enable, numa_nodes_parsed);

	pr_debug("found %d total number of nodes\n", count);
	return 0;
}
#endif

static void __init
acpi_table_print_srat_entry(struct acpi_subtable_header *header)
{
	switch (header->type) {
	case ACPI_SRAT_TYPE_CPU_AFFINITY:
		{
			struct acpi_srat_cpu_affinity *p =
			    (struct acpi_srat_cpu_affinity *)header;
			pr_debug("SRAT Processor (id[0x%02x] eid[0x%02x]) in proximity domain %d %s\n",
				 p->apic_id, p->local_sapic_eid,
				 p->proximity_domain_lo,
				 (p->flags & ACPI_SRAT_CPU_ENABLED) ?
				 "enabled" : "disabled");
		}
		break;

	case ACPI_SRAT_TYPE_MEMORY_AFFINITY:
		{
			struct acpi_srat_mem_affinity *p =
			    (struct acpi_srat_mem_affinity *)header;
			pr_debug("SRAT Memory (0x%llx length 0x%llx) in proximity domain %d %s%s%s\n",
				 (unsigned long long)p->base_address,
				 (unsigned long long)p->length,
				 p->proximity_domain,
				 (p->flags & ACPI_SRAT_MEM_ENABLED) ?
				 "enabled" : "disabled",
				 (p->flags & ACPI_SRAT_MEM_HOT_PLUGGABLE) ?
				 " hot-pluggable" : "",
				 (p->flags & ACPI_SRAT_MEM_NON_VOLATILE) ?
				 " non-volatile" : "");
		}
		break;

	case ACPI_SRAT_TYPE_X2APIC_CPU_AFFINITY:
		{
			struct acpi_srat_x2apic_cpu_affinity *p =
			    (struct acpi_srat_x2apic_cpu_affinity *)header;
			pr_debug("SRAT Processor (x2apicid[0x%08x]) in proximity domain %d %s\n",
				 p->apic_id,
				 p->proximity_domain,
				 (p->flags & ACPI_SRAT_CPU_ENABLED) ?
				 "enabled" : "disabled");
		}
		break;

	case ACPI_SRAT_TYPE_GICC_AFFINITY:
		{
			struct acpi_srat_gicc_affinity *p =
			    (struct acpi_srat_gicc_affinity *)header;
			pr_debug("SRAT Processor (acpi id[0x%04x]) in proximity domain %d %s\n",
				 p->acpi_processor_uid,
				 p->proximity_domain,
				 (p->flags & ACPI_SRAT_GICC_ENABLED) ?
				 "enabled" : "disabled");
		}
		break;

	case ACPI_SRAT_TYPE_GENERIC_AFFINITY:
	{
		struct acpi_srat_generic_affinity *p =
			(struct acpi_srat_generic_affinity *)header;

		if (p->device_handle_type == 0) {
			/*
			 * For pci devices this may be the only place they
			 * are assigned a proximity domain
			 */
			pr_debug("SRAT Generic Initiator(Seg:%u BDF:%u) in proximity domain %d %s\n",
				 *(u16 *)(&p->device_handle[0]),
				 *(u16 *)(&p->device_handle[2]),
				 p->proximity_domain,
				 (p->flags & ACPI_SRAT_GENERIC_AFFINITY_ENABLED) ?
				"enabled" : "disabled");
		} else {
			/*
			 * In this case we can rely on the device having a
			 * proximity domain reference
			 */
			pr_debug("SRAT Generic Initiator(HID=%.8s UID=%.4s) in proximity domain %d %s\n",
				(char *)(&p->device_handle[0]),
				(char *)(&p->device_handle[8]),
				p->proximity_domain,
				(p->flags & ACPI_SRAT_GENERIC_AFFINITY_ENABLED) ?
				"enabled" : "disabled");
		}
	}
	break;

	case ACPI_SRAT_TYPE_RINTC_AFFINITY:
		{
			struct acpi_srat_rintc_affinity *p =
			    (struct acpi_srat_rintc_affinity *)header;
			pr_debug("SRAT Processor (acpi id[0x%04x]) in proximity domain %d %s\n",
				 p->acpi_processor_uid,
				 p->proximity_domain,
				 (p->flags & ACPI_SRAT_RINTC_ENABLED) ?
				 "enabled" : "disabled");
		}
		break;

	default:
		pr_warn("Found unsupported SRAT entry (type = 0x%x)\n",
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
		for (j = 0; j < d; j++) {
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

void __init bad_srat(void)
{
	pr_err("SRAT: SRAT not used.\n");
	disable_srat();
}

int __init srat_disabled(void)
{
	return acpi_numa < 0;
}

__weak int __init numa_fill_memblks(u64 start, u64 end)
{
	return NUMA_NO_MEMBLK;
}

/*
 * Callback for SLIT parsing.  pxm_to_node() returns NUMA_NO_NODE for
 * I/O localities since SRAT does not list them.  I/O localities are
 * not supported at this point.
 */
static int __init acpi_parse_slit(struct acpi_table_header *table)
{
	struct acpi_table_slit *slit = (struct acpi_table_slit *)table;
	int i, j;

	if (!slit_valid(slit)) {
		pr_info("SLIT table looks invalid. Not used.\n");
		return -EINVAL;
	}

	for (i = 0; i < slit->locality_count; i++) {
		const int from_node = pxm_to_node(i);

		if (from_node == NUMA_NO_NODE)
			continue;

		for (j = 0; j < slit->locality_count; j++) {
			const int to_node = pxm_to_node(j);

			if (to_node == NUMA_NO_NODE)
				continue;

			numa_set_distance(from_node, to_node,
				slit->entry[slit->locality_count * i + j]);
		}
	}

	return 0;
}

static int parsed_numa_memblks __initdata;

static int __init
acpi_parse_memory_affinity(union acpi_subtable_headers *header,
			   const unsigned long table_end)
{
	struct acpi_srat_mem_affinity *ma;
	u64 start, end;
	u32 hotpluggable;
	int node, pxm;

	ma = (struct acpi_srat_mem_affinity *)header;

	acpi_table_print_srat_entry(&header->common);

	if (srat_disabled())
		return 0;
	if (ma->header.length < sizeof(struct acpi_srat_mem_affinity)) {
		pr_err("SRAT: Unexpected header length: %d\n",
		       ma->header.length);
		goto out_err_bad_srat;
	}
	if ((ma->flags & ACPI_SRAT_MEM_ENABLED) == 0)
		return 0;
	hotpluggable = IS_ENABLED(CONFIG_MEMORY_HOTPLUG) &&
		(ma->flags & ACPI_SRAT_MEM_HOT_PLUGGABLE);

	start = ma->base_address;
	end = start + ma->length;
	pxm = ma->proximity_domain;
	if (acpi_srat_revision <= 1)
		pxm &= 0xff;

	node = acpi_map_pxm_to_node(pxm);
	if (node == NUMA_NO_NODE) {
		pr_err("SRAT: Too many proximity domains.\n");
		goto out_err_bad_srat;
	}

	if (numa_add_memblk(node, start, end) < 0) {
		pr_err("SRAT: Failed to add memblk to node %u [mem %#010Lx-%#010Lx]\n",
		       node, (unsigned long long) start,
		       (unsigned long long) end - 1);
		goto out_err_bad_srat;
	}

	node_set(node, numa_nodes_parsed);

	pr_info("SRAT: Node %u PXM %u [mem %#010Lx-%#010Lx]%s%s\n",
		node, pxm,
		(unsigned long long) start, (unsigned long long) end - 1,
		hotpluggable ? " hotplug" : "",
		ma->flags & ACPI_SRAT_MEM_NON_VOLATILE ? " non-volatile" : "");

	/* Mark hotplug range in memblock. */
	if (hotpluggable && memblock_mark_hotplug(start, ma->length))
		pr_warn("SRAT: Failed to mark hotplug range [mem %#010Lx-%#010Lx] in memblock\n",
			(unsigned long long)start, (unsigned long long)end - 1);

	max_possible_pfn = max(max_possible_pfn, PFN_UP(end - 1));

	parsed_numa_memblks++;

	return 0;

out_err_bad_srat:
	/* Just disable SRAT, but do not fail and ignore errors. */
	bad_srat();

	return 0;
}

static int __init acpi_parse_cfmws(union acpi_subtable_headers *header,
				   void *arg, const unsigned long table_end)
{
	struct acpi_cedt_cfmws *cfmws;
	int *fake_pxm = arg;
	u64 start, end;
	int node;

	cfmws = (struct acpi_cedt_cfmws *)header;
	start = cfmws->base_hpa;
	end = cfmws->base_hpa + cfmws->window_size;

	/*
	 * The SRAT may have already described NUMA details for all,
	 * or a portion of, this CFMWS HPA range. Extend the memblks
	 * found for any portion of the window to cover the entire
	 * window.
	 */
	if (!numa_fill_memblks(start, end))
		return 0;

	/* No SRAT description. Create a new node. */
	node = acpi_map_pxm_to_node(*fake_pxm);

	if (node == NUMA_NO_NODE) {
		pr_err("ACPI NUMA: Too many proximity domains while processing CFMWS.\n");
		return -EINVAL;
	}

	if (numa_add_memblk(node, start, end) < 0) {
		/* CXL driver must handle the NUMA_NO_NODE case */
		pr_warn("ACPI NUMA: Failed to add memblk for CFMWS node %d [mem %#llx-%#llx]\n",
			node, start, end);
	}
	node_set(node, numa_nodes_parsed);

	/* Set the next available fake_pxm value */
	(*fake_pxm)++;
	return 0;
}

void __init __weak
acpi_numa_x2apic_affinity_init(struct acpi_srat_x2apic_cpu_affinity *pa)
{
	pr_warn("Found unsupported x2apic [0x%08x] SRAT entry\n", pa->apic_id);
}

static int __init
acpi_parse_x2apic_affinity(union acpi_subtable_headers *header,
			   const unsigned long end)
{
	struct acpi_srat_x2apic_cpu_affinity *processor_affinity;

	processor_affinity = (struct acpi_srat_x2apic_cpu_affinity *)header;

	acpi_table_print_srat_entry(&header->common);

	/* let architecture-dependent part to do it */
	acpi_numa_x2apic_affinity_init(processor_affinity);

	return 0;
}

static int __init
acpi_parse_processor_affinity(union acpi_subtable_headers *header,
			      const unsigned long end)
{
	struct acpi_srat_cpu_affinity *processor_affinity;

	processor_affinity = (struct acpi_srat_cpu_affinity *)header;

	acpi_table_print_srat_entry(&header->common);

	/* let architecture-dependent part to do it */
	acpi_numa_processor_affinity_init(processor_affinity);

	return 0;
}

static int __init
acpi_parse_gicc_affinity(union acpi_subtable_headers *header,
			 const unsigned long end)
{
	struct acpi_srat_gicc_affinity *processor_affinity;

	processor_affinity = (struct acpi_srat_gicc_affinity *)header;

	acpi_table_print_srat_entry(&header->common);

	/* let architecture-dependent part to do it */
	acpi_numa_gicc_affinity_init(processor_affinity);

	return 0;
}

#if defined(CONFIG_X86) || defined(CONFIG_ARM64)
static int __init
acpi_parse_gi_affinity(union acpi_subtable_headers *header,
		       const unsigned long end)
{
	struct acpi_srat_generic_affinity *gi_affinity;
	int node;

	gi_affinity = (struct acpi_srat_generic_affinity *)header;
	if (!gi_affinity)
		return -EINVAL;
	acpi_table_print_srat_entry(&header->common);

	if (!(gi_affinity->flags & ACPI_SRAT_GENERIC_AFFINITY_ENABLED))
		return -EINVAL;

	node = acpi_map_pxm_to_node(gi_affinity->proximity_domain);
	if (node == NUMA_NO_NODE) {
		pr_err("SRAT: Too many proximity domains.\n");
		return -EINVAL;
	}
	node_set(node, numa_nodes_parsed);
	node_set_state(node, N_GENERIC_INITIATOR);

	return 0;
}
#else
static int __init
acpi_parse_gi_affinity(union acpi_subtable_headers *header,
		       const unsigned long end)
{
	return 0;
}
#endif /* defined(CONFIG_X86) || defined (CONFIG_ARM64) */

static int __init
acpi_parse_rintc_affinity(union acpi_subtable_headers *header,
			  const unsigned long end)
{
	struct acpi_srat_rintc_affinity *rintc_affinity;

	rintc_affinity = (struct acpi_srat_rintc_affinity *)header;
	acpi_table_print_srat_entry(&header->common);

	/* let architecture-dependent part to do it */
	acpi_numa_rintc_affinity_init(rintc_affinity);

	return 0;
}

static int __init acpi_parse_srat(struct acpi_table_header *table)
{
	struct acpi_table_srat *srat = (struct acpi_table_srat *)table;

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
	int i, fake_pxm, cnt = 0;

	if (acpi_disabled)
		return -EINVAL;

	/*
	 * Should not limit number with cpu num that is from NR_CPUS or nr_cpus=
	 * SRAT cpu entries could have different order with that in MADT.
	 * So go over all cpu entries in SRAT to get apicid to node mapping.
	 */

	/* SRAT: System Resource Affinity Table */
	if (!acpi_table_parse(ACPI_SIG_SRAT, acpi_parse_srat)) {
		struct acpi_subtable_proc srat_proc[5];

		memset(srat_proc, 0, sizeof(srat_proc));
		srat_proc[0].id = ACPI_SRAT_TYPE_CPU_AFFINITY;
		srat_proc[0].handler = acpi_parse_processor_affinity;
		srat_proc[1].id = ACPI_SRAT_TYPE_X2APIC_CPU_AFFINITY;
		srat_proc[1].handler = acpi_parse_x2apic_affinity;
		srat_proc[2].id = ACPI_SRAT_TYPE_GICC_AFFINITY;
		srat_proc[2].handler = acpi_parse_gicc_affinity;
		srat_proc[3].id = ACPI_SRAT_TYPE_GENERIC_AFFINITY;
		srat_proc[3].handler = acpi_parse_gi_affinity;
		srat_proc[4].id = ACPI_SRAT_TYPE_RINTC_AFFINITY;
		srat_proc[4].handler = acpi_parse_rintc_affinity;

		acpi_table_parse_entries_array(ACPI_SIG_SRAT,
					sizeof(struct acpi_table_srat),
					srat_proc, ARRAY_SIZE(srat_proc), 0);

		cnt = acpi_table_parse_srat(ACPI_SRAT_TYPE_MEMORY_AFFINITY,
					    acpi_parse_memory_affinity, 0);
	}

	/* SLIT: System Locality Information Table */
	acpi_table_parse(ACPI_SIG_SLIT, acpi_parse_slit);

	/*
	 * CXL Fixed Memory Window Structures (CFMWS) must be parsed
	 * after the SRAT. Create NUMA Nodes for CXL memory ranges that
	 * are defined in the CFMWS and not already defined in the SRAT.
	 * Initialize a fake_pxm as the first available PXM to emulate.
	 */

	/* fake_pxm is the next unused PXM value after SRAT parsing */
	for (i = 0, fake_pxm = -1; i < MAX_NUMNODES; i++) {
		if (node_to_pxm_map[i] > fake_pxm)
			fake_pxm = node_to_pxm_map[i];
	}
	last_real_pxm = fake_pxm;
	fake_pxm++;
	acpi_table_parse_cedt(ACPI_CEDT_TYPE_CFMWS, acpi_parse_cfmws,
			      &fake_pxm);

	if (cnt < 0)
		return cnt;
	else if (!parsed_numa_memblks)
		return -ENOENT;
	return 0;
}

bool acpi_node_backed_by_real_pxm(int nid)
{
	int pxm = node_to_pxm(nid);

	return pxm <= last_real_pxm;
}
EXPORT_SYMBOL_GPL(acpi_node_backed_by_real_pxm);

static int acpi_get_pxm(acpi_handle h)
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

int acpi_get_node(acpi_handle handle)
{
	int pxm;

	pxm = acpi_get_pxm(handle);

	return pxm_to_node(pxm);
}
EXPORT_SYMBOL(acpi_get_node);
