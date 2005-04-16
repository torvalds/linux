/*
 * ACPI 3.0 based NUMA setup
 * Copyright 2004 Andi Kleen, SuSE Labs.
 *
 * Reads the ACPI SRAT table to figure out what memory belongs to which CPUs.
 *
 * Called from acpi_numa_init while reading the SRAT and SLIT tables.
 * Assumes all memory regions belonging to a single proximity domain
 * are in one chunk. Holes between them will be included in the node.
 */

#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/mmzone.h>
#include <linux/bitmap.h>
#include <linux/module.h>
#include <linux/topology.h>
#include <asm/proto.h>
#include <asm/numa.h>

static struct acpi_table_slit *acpi_slit;

static nodemask_t nodes_parsed __initdata;
static nodemask_t nodes_found __initdata;
static struct node nodes[MAX_NUMNODES] __initdata;
static __u8  pxm2node[256] = { [0 ... 255] = 0xff };

static __init int setup_node(int pxm)
{
	unsigned node = pxm2node[pxm];
	if (node == 0xff) {
		if (nodes_weight(nodes_found) >= MAX_NUMNODES)
			return -1;
		node = first_unset_node(nodes_found); 
		node_set(node, nodes_found);
		pxm2node[pxm] = node;
	}
	return pxm2node[pxm];
}

static __init int conflicting_nodes(unsigned long start, unsigned long end)
{
	int i;
	for_each_online_node(i) {
		struct node *nd = &nodes[i];
		if (nd->start == nd->end)
			continue;
		if (nd->end > start && nd->start < end)
			return 1;
		if (nd->end == end && nd->start == start)
			return 1;
	}
	return -1;
}

static __init void cutoff_node(int i, unsigned long start, unsigned long end)
{
	struct node *nd = &nodes[i];
	if (nd->start < start) {
		nd->start = start;
		if (nd->end < nd->start)
			nd->start = nd->end;
	}
	if (nd->end > end) {
		if (!(end & 0xfff))
			end--;
		nd->end = end;
		if (nd->start > nd->end)
			nd->start = nd->end;
	}
}

static __init void bad_srat(void)
{
	printk(KERN_ERR "SRAT: SRAT not used.\n");
	acpi_numa = -1;
}

static __init inline int srat_disabled(void)
{
	return numa_off || acpi_numa < 0;
}

/* Callback for SLIT parsing */
void __init acpi_numa_slit_init(struct acpi_table_slit *slit)
{
	acpi_slit = slit;
}

/* Callback for Proximity Domain -> LAPIC mapping */
void __init
acpi_numa_processor_affinity_init(struct acpi_table_processor_affinity *pa)
{
	int pxm, node;
	if (srat_disabled() || pa->flags.enabled == 0)
		return;
	pxm = pa->proximity_domain;
	node = setup_node(pxm);
	if (node < 0) {
		printk(KERN_ERR "SRAT: Too many proximity domains %x\n", pxm);
		bad_srat();
		return;
	}
	if (pa->apic_id >= NR_CPUS) {
		printk(KERN_ERR "SRAT: lapic %u too large.\n",
		       pa->apic_id);
		bad_srat();
		return;
	}
	cpu_to_node[pa->apic_id] = node;
	acpi_numa = 1;
	printk(KERN_INFO "SRAT: PXM %u -> APIC %u -> Node %u\n",
	       pxm, pa->apic_id, node);
}

/* Callback for parsing of the Proximity Domain <-> Memory Area mappings */
void __init
acpi_numa_memory_affinity_init(struct acpi_table_memory_affinity *ma)
{
	struct node *nd;
	unsigned long start, end;
	int node, pxm;
	int i;

	if (srat_disabled() || ma->flags.enabled == 0)
		return;
	/* hotplug bit is ignored for now */
	pxm = ma->proximity_domain;
	node = setup_node(pxm);
	if (node < 0) {
		printk(KERN_ERR "SRAT: Too many proximity domains.\n");
		bad_srat();
		return;
	}
	start = ma->base_addr_lo | ((u64)ma->base_addr_hi << 32);
	end = start + (ma->length_lo | ((u64)ma->length_hi << 32));
	i = conflicting_nodes(start, end);
	if (i >= 0) {
		printk(KERN_ERR
		       "SRAT: pxm %d overlap %lx-%lx with node %d(%Lx-%Lx)\n",
		       pxm, start, end, i, nodes[i].start, nodes[i].end);
		bad_srat();
		return;
	}
	nd = &nodes[node];
	if (!node_test_and_set(node, nodes_parsed)) {
		nd->start = start;
		nd->end = end;
	} else {
		if (start < nd->start)
			nd->start = start;
		if (nd->end < end)
			nd->end = end;
	}
	if (!(nd->end & 0xfff))
		nd->end--;
	printk(KERN_INFO "SRAT: Node %u PXM %u %Lx-%Lx\n", node, pxm,
	       nd->start, nd->end);
}

void __init acpi_numa_arch_fixup(void) {}

/* Use the information discovered above to actually set up the nodes. */
int __init acpi_scan_nodes(unsigned long start, unsigned long end)
{
	int i;
	if (acpi_numa <= 0)
		return -1;
	memnode_shift = compute_hash_shift(nodes, nodes_weight(nodes_parsed));
	if (memnode_shift < 0) {
		printk(KERN_ERR
		     "SRAT: No NUMA node hash function found. Contact maintainer\n");
		bad_srat();
		return -1;
	}
	for (i = 0; i < MAX_NUMNODES; i++) {
		if (!node_isset(i, nodes_parsed))
			continue;
		cutoff_node(i, start, end);
		if (nodes[i].start == nodes[i].end) { 
			node_clear(i, nodes_parsed);
			continue;
		}
		setup_node_bootmem(i, nodes[i].start, nodes[i].end);
	}
	for (i = 0; i < NR_CPUS; i++) { 
		if (cpu_to_node[i] == NUMA_NO_NODE)
			continue;
		if (!node_isset(cpu_to_node[i], nodes_parsed))
			cpu_to_node[i] = NUMA_NO_NODE; 
	}
	numa_init_array();
	return 0;
}

int node_to_pxm(int n)
{
       int i;
       if (pxm2node[n] == n)
               return n;
       for (i = 0; i < 256; i++)
               if (pxm2node[i] == n)
                       return i;
       return 0;
}

int __node_distance(int a, int b)
{
	int index;

	if (!acpi_slit)
		return a == b ? 10 : 20;
	index = acpi_slit->localities * node_to_pxm(a);
	return acpi_slit->entry[index + node_to_pxm(b)];
}

EXPORT_SYMBOL(__node_distance);
