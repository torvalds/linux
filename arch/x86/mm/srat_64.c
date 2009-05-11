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
#include <linux/bootmem.h>
#include <linux/mm.h>
#include <asm/proto.h>
#include <asm/numa.h>
#include <asm/e820.h>
#include <asm/apic.h>
#include <asm/uv/uv.h>

int acpi_numa __initdata;

static struct acpi_table_slit *acpi_slit;

static nodemask_t nodes_parsed __initdata;
static nodemask_t cpu_nodes_parsed __initdata;
static struct bootnode nodes[MAX_NUMNODES] __initdata;
static struct bootnode nodes_add[MAX_NUMNODES];
static int found_add_area __initdata;
int hotadd_percent __initdata = 0;

static int num_node_memblks __initdata;
static struct bootnode node_memblk_range[NR_NODE_MEMBLKS] __initdata;
static int memblk_nodeid[NR_NODE_MEMBLKS] __initdata;

/* Too small nodes confuse the VM badly. Usually they result
   from BIOS bugs. */
#define NODE_MIN_SIZE (4*1024*1024)

static __init int setup_node(int pxm)
{
	return acpi_map_pxm_to_node(pxm);
}

static __init int conflicting_memblks(unsigned long start, unsigned long end)
{
	int i;
	for (i = 0; i < num_node_memblks; i++) {
		struct bootnode *nd = &node_memblk_range[i];
		if (nd->start == nd->end)
			continue;
		if (nd->end > start && nd->start < end)
			return memblk_nodeid[i];
		if (nd->end == end && nd->start == start)
			return memblk_nodeid[i];
	}
	return -1;
}

static __init void cutoff_node(int i, unsigned long start, unsigned long end)
{
	struct bootnode *nd = &nodes[i];

	if (found_add_area)
		return;

	if (nd->start < start) {
		nd->start = start;
		if (nd->end < nd->start)
			nd->start = nd->end;
	}
	if (nd->end > end) {
		nd->end = end;
		if (nd->start > nd->end)
			nd->start = nd->end;
	}
}

static __init void bad_srat(void)
{
	int i;
	printk(KERN_ERR "SRAT: SRAT not used.\n");
	acpi_numa = -1;
	found_add_area = 0;
	for (i = 0; i < MAX_LOCAL_APIC; i++)
		apicid_to_node[i] = NUMA_NO_NODE;
	for (i = 0; i < MAX_NUMNODES; i++)
		nodes_add[i].start = nodes[i].end = 0;
	remove_all_active_ranges();
}

static __init inline int srat_disabled(void)
{
	return numa_off || acpi_numa < 0;
}

/* Callback for SLIT parsing */
void __init acpi_numa_slit_init(struct acpi_table_slit *slit)
{
	unsigned length;
	unsigned long phys;

	length = slit->header.length;
	phys = find_e820_area(0, max_pfn_mapped<<PAGE_SHIFT, length,
		 PAGE_SIZE);

	if (phys == -1L)
		panic(" Can not save slit!\n");

	acpi_slit = __va(phys);
	memcpy(acpi_slit, slit, length);
	reserve_early(phys, phys + length, "ACPI SLIT");
}

/* Callback for Proximity Domain -> x2APIC mapping */
void __init
acpi_numa_x2apic_affinity_init(struct acpi_srat_x2apic_cpu_affinity *pa)
{
	int pxm, node;
	int apic_id;

	if (srat_disabled())
		return;
	if (pa->header.length < sizeof(struct acpi_srat_x2apic_cpu_affinity)) {
		bad_srat();
		return;
	}
	if ((pa->flags & ACPI_SRAT_CPU_ENABLED) == 0)
		return;
	pxm = pa->proximity_domain;
	node = setup_node(pxm);
	if (node < 0) {
		printk(KERN_ERR "SRAT: Too many proximity domains %x\n", pxm);
		bad_srat();
		return;
	}

	apic_id = pa->apic_id;
	apicid_to_node[apic_id] = node;
	node_set(node, cpu_nodes_parsed);
	acpi_numa = 1;
	printk(KERN_INFO "SRAT: PXM %u -> APIC %u -> Node %u\n",
	       pxm, apic_id, node);
}

/* Callback for Proximity Domain -> LAPIC mapping */
void __init
acpi_numa_processor_affinity_init(struct acpi_srat_cpu_affinity *pa)
{
	int pxm, node;
	int apic_id;

	if (srat_disabled())
		return;
	if (pa->header.length != sizeof(struct acpi_srat_cpu_affinity)) {
		bad_srat();
		return;
	}
	if ((pa->flags & ACPI_SRAT_CPU_ENABLED) == 0)
		return;
	pxm = pa->proximity_domain_lo;
	node = setup_node(pxm);
	if (node < 0) {
		printk(KERN_ERR "SRAT: Too many proximity domains %x\n", pxm);
		bad_srat();
		return;
	}

	if (get_uv_system_type() >= UV_X2APIC)
		apic_id = (pa->apic_id << 8) | pa->local_sapic_eid;
	else
		apic_id = pa->apic_id;
	apicid_to_node[apic_id] = node;
	node_set(node, cpu_nodes_parsed);
	acpi_numa = 1;
	printk(KERN_INFO "SRAT: PXM %u -> APIC %u -> Node %u\n",
	       pxm, apic_id, node);
}

static int update_end_of_memory(unsigned long end) {return -1;}
static int hotadd_enough_memory(struct bootnode *nd) {return 1;}
#ifdef CONFIG_MEMORY_HOTPLUG_SPARSE
static inline int save_add_info(void) {return 1;}
#else
static inline int save_add_info(void) {return 0;}
#endif
/*
 * Update nodes_add and decide if to include add are in the zone.
 * Both SPARSE and RESERVE need nodes_add information.
 * This code supports one contiguous hot add area per node.
 */
static int __init
reserve_hotadd(int node, unsigned long start, unsigned long end)
{
	unsigned long s_pfn = start >> PAGE_SHIFT;
	unsigned long e_pfn = end >> PAGE_SHIFT;
	int ret = 0, changed = 0;
	struct bootnode *nd = &nodes_add[node];

	/* I had some trouble with strange memory hotadd regions breaking
	   the boot. Be very strict here and reject anything unexpected.
	   If you want working memory hotadd write correct SRATs.

	   The node size check is a basic sanity check to guard against
	   mistakes */
	if ((signed long)(end - start) < NODE_MIN_SIZE) {
		printk(KERN_ERR "SRAT: Hotplug area too small\n");
		return -1;
	}

	/* This check might be a bit too strict, but I'm keeping it for now. */
	if (absent_pages_in_range(s_pfn, e_pfn) != e_pfn - s_pfn) {
		printk(KERN_ERR
			"SRAT: Hotplug area %lu -> %lu has existing memory\n",
			s_pfn, e_pfn);
		return -1;
	}

	if (!hotadd_enough_memory(&nodes_add[node]))  {
		printk(KERN_ERR "SRAT: Hotplug area too large\n");
		return -1;
	}

	/* Looks good */

	if (nd->start == nd->end) {
		nd->start = start;
		nd->end = end;
		changed = 1;
	} else {
		if (nd->start == end) {
			nd->start = start;
			changed = 1;
		}
		if (nd->end == start) {
			nd->end = end;
			changed = 1;
		}
		if (!changed)
			printk(KERN_ERR "SRAT: Hotplug zone not continuous. Partly ignored\n");
	}

	ret = update_end_of_memory(nd->end);

	if (changed)
	 	printk(KERN_INFO "SRAT: hot plug zone found %Lx - %Lx\n", nd->start, nd->end);
	return ret;
}

/* Callback for parsing of the Proximity Domain <-> Memory Area mappings */
void __init
acpi_numa_memory_affinity_init(struct acpi_srat_mem_affinity *ma)
{
	struct bootnode *nd, oldnode;
	unsigned long start, end;
	int node, pxm;
	int i;

	if (srat_disabled())
		return;
	if (ma->header.length != sizeof(struct acpi_srat_mem_affinity)) {
		bad_srat();
		return;
	}
	if ((ma->flags & ACPI_SRAT_MEM_ENABLED) == 0)
		return;

	if ((ma->flags & ACPI_SRAT_MEM_HOT_PLUGGABLE) && !save_add_info())
		return;
	start = ma->base_address;
	end = start + ma->length;
	pxm = ma->proximity_domain;
	node = setup_node(pxm);
	if (node < 0) {
		printk(KERN_ERR "SRAT: Too many proximity domains.\n");
		bad_srat();
		return;
	}
	i = conflicting_memblks(start, end);
	if (i == node) {
		printk(KERN_WARNING
		"SRAT: Warning: PXM %d (%lx-%lx) overlaps with itself (%Lx-%Lx)\n",
			pxm, start, end, nodes[i].start, nodes[i].end);
	} else if (i >= 0) {
		printk(KERN_ERR
		       "SRAT: PXM %d (%lx-%lx) overlaps with PXM %d (%Lx-%Lx)\n",
		       pxm, start, end, node_to_pxm(i),
			nodes[i].start, nodes[i].end);
		bad_srat();
		return;
	}
	nd = &nodes[node];
	oldnode = *nd;
	if (!node_test_and_set(node, nodes_parsed)) {
		nd->start = start;
		nd->end = end;
	} else {
		if (start < nd->start)
			nd->start = start;
		if (nd->end < end)
			nd->end = end;
	}

	printk(KERN_INFO "SRAT: Node %u PXM %u %lx-%lx\n", node, pxm,
	       start, end);
	e820_register_active_regions(node, start >> PAGE_SHIFT,
				     end >> PAGE_SHIFT);
	push_node_boundaries(node, nd->start >> PAGE_SHIFT,
						nd->end >> PAGE_SHIFT);

	if ((ma->flags & ACPI_SRAT_MEM_HOT_PLUGGABLE) &&
	    (reserve_hotadd(node, start, end) < 0)) {
		/* Ignore hotadd region. Undo damage */
		printk(KERN_NOTICE "SRAT: Hotplug region ignored\n");
		*nd = oldnode;
		if ((nd->start | nd->end) == 0)
			node_clear(node, nodes_parsed);
	}

	node_memblk_range[num_node_memblks].start = start;
	node_memblk_range[num_node_memblks].end = end;
	memblk_nodeid[num_node_memblks] = node;
	num_node_memblks++;
}

/* Sanity check to catch more bad SRATs (they are amazingly common).
   Make sure the PXMs cover all memory. */
static int __init nodes_cover_memory(const struct bootnode *nodes)
{
	int i;
	unsigned long pxmram, e820ram;

	pxmram = 0;
	for_each_node_mask(i, nodes_parsed) {
		unsigned long s = nodes[i].start >> PAGE_SHIFT;
		unsigned long e = nodes[i].end >> PAGE_SHIFT;
		pxmram += e - s;
		pxmram -= absent_pages_in_range(s, e);
		if ((long)pxmram < 0)
			pxmram = 0;
	}

	e820ram = max_pfn - absent_pages_in_range(0, max_pfn);
	/* We seem to lose 3 pages somewhere. Allow a bit of slack. */
	if ((long)(e820ram - pxmram) >= 1*1024*1024) {
		printk(KERN_ERR
	"SRAT: PXMs only cover %luMB of your %luMB e820 RAM. Not used.\n",
			(pxmram << PAGE_SHIFT) >> 20,
			(e820ram << PAGE_SHIFT) >> 20);
		return 0;
	}
	return 1;
}

static void __init unparse_node(int node)
{
	int i;
	node_clear(node, nodes_parsed);
	node_clear(node, cpu_nodes_parsed);
	for (i = 0; i < MAX_LOCAL_APIC; i++) {
		if (apicid_to_node[i] == node)
			apicid_to_node[i] = NUMA_NO_NODE;
	}
}

void __init acpi_numa_arch_fixup(void) {}

/* Use the information discovered above to actually set up the nodes. */
int __init acpi_scan_nodes(unsigned long start, unsigned long end)
{
	int i;

	if (acpi_numa <= 0)
		return -1;

	/* First clean up the node list */
	for (i = 0; i < MAX_NUMNODES; i++) {
		cutoff_node(i, start, end);
		/*
		 * don't confuse VM with a node that doesn't have the
		 * minimum memory.
		 */
		if (nodes[i].end &&
			(nodes[i].end - nodes[i].start) < NODE_MIN_SIZE) {
			unparse_node(i);
			node_set_offline(i);
		}
	}

	if (!nodes_cover_memory(nodes)) {
		bad_srat();
		return -1;
	}

	memnode_shift = compute_hash_shift(node_memblk_range, num_node_memblks,
					   memblk_nodeid);
	if (memnode_shift < 0) {
		printk(KERN_ERR
		     "SRAT: No NUMA node hash function found. Contact maintainer\n");
		bad_srat();
		return -1;
	}

	/* Account for nodes with cpus and no memory */
	nodes_or(node_possible_map, nodes_parsed, cpu_nodes_parsed);

	/* Finally register nodes */
	for_each_node_mask(i, node_possible_map)
		setup_node_bootmem(i, nodes[i].start, nodes[i].end);
	/* Try again in case setup_node_bootmem missed one due
	   to missing bootmem */
	for_each_node_mask(i, node_possible_map)
		if (!node_online(i))
			setup_node_bootmem(i, nodes[i].start, nodes[i].end);

	for (i = 0; i < nr_cpu_ids; i++) {
		int node = early_cpu_to_node(i);

		if (node == NUMA_NO_NODE)
			continue;
		if (!node_isset(node, node_possible_map))
			numa_clear_node(i);
	}
	numa_init_array();
	return 0;
}

#ifdef CONFIG_NUMA_EMU
static int fake_node_to_pxm_map[MAX_NUMNODES] __initdata = {
	[0 ... MAX_NUMNODES-1] = PXM_INVAL
};
static s16 fake_apicid_to_node[MAX_LOCAL_APIC] __initdata = {
	[0 ... MAX_LOCAL_APIC-1] = NUMA_NO_NODE
};
static int __init find_node_by_addr(unsigned long addr)
{
	int ret = NUMA_NO_NODE;
	int i;

	for_each_node_mask(i, nodes_parsed) {
		/*
		 * Find the real node that this emulated node appears on.  For
		 * the sake of simplicity, we only use a real node's starting
		 * address to determine which emulated node it appears on.
		 */
		if (addr >= nodes[i].start && addr < nodes[i].end) {
			ret = i;
			break;
		}
	}
	return ret;
}

/*
 * In NUMA emulation, we need to setup proximity domain (_PXM) to node ID
 * mappings that respect the real ACPI topology but reflect our emulated
 * environment.  For each emulated node, we find which real node it appears on
 * and create PXM to NID mappings for those fake nodes which mirror that
 * locality.  SLIT will now represent the correct distances between emulated
 * nodes as a result of the real topology.
 */
void __init acpi_fake_nodes(const struct bootnode *fake_nodes, int num_nodes)
{
	int i, j;

	printk(KERN_INFO "Faking PXM affinity for fake nodes on real "
			 "topology.\n");
	for (i = 0; i < num_nodes; i++) {
		int nid, pxm;

		nid = find_node_by_addr(fake_nodes[i].start);
		if (nid == NUMA_NO_NODE)
			continue;
		pxm = node_to_pxm(nid);
		if (pxm == PXM_INVAL)
			continue;
		fake_node_to_pxm_map[i] = pxm;
		/*
		 * For each apicid_to_node mapping that exists for this real
		 * node, it must now point to the fake node ID.
		 */
		for (j = 0; j < MAX_LOCAL_APIC; j++)
			if (apicid_to_node[j] == nid)
				fake_apicid_to_node[j] = i;
	}
	for (i = 0; i < num_nodes; i++)
		__acpi_map_pxm_to_node(fake_node_to_pxm_map[i], i);
	memcpy(apicid_to_node, fake_apicid_to_node, sizeof(apicid_to_node));

	nodes_clear(nodes_parsed);
	for (i = 0; i < num_nodes; i++)
		if (fake_nodes[i].start != fake_nodes[i].end)
			node_set(i, nodes_parsed);
	WARN_ON(!nodes_cover_memory(fake_nodes));
}

static int null_slit_node_compare(int a, int b)
{
	return node_to_pxm(a) == node_to_pxm(b);
}
#else
static int null_slit_node_compare(int a, int b)
{
	return a == b;
}
#endif /* CONFIG_NUMA_EMU */

void __init srat_reserve_add_area(int nodeid)
{
	if (found_add_area && nodes_add[nodeid].end) {
		u64 total_mb;

		printk(KERN_INFO "SRAT: Reserving hot-add memory space "
				"for node %d at %Lx-%Lx\n",
			nodeid, nodes_add[nodeid].start, nodes_add[nodeid].end);
		total_mb = (nodes_add[nodeid].end - nodes_add[nodeid].start)
					>> PAGE_SHIFT;
		total_mb *= sizeof(struct page);
		total_mb >>= 20;
		printk(KERN_INFO "SRAT: This will cost you %Lu MB of "
				"pre-allocated memory.\n", (unsigned long long)total_mb);
		reserve_bootmem_node(NODE_DATA(nodeid), nodes_add[nodeid].start,
			       nodes_add[nodeid].end - nodes_add[nodeid].start,
			       BOOTMEM_DEFAULT);
	}
}

int __node_distance(int a, int b)
{
	int index;

	if (!acpi_slit)
		return null_slit_node_compare(a, b) ? LOCAL_DISTANCE :
						      REMOTE_DISTANCE;
	index = acpi_slit->locality_count * node_to_pxm(a);
	return acpi_slit->entry[index + node_to_pxm(b)];
}

EXPORT_SYMBOL(__node_distance);

#if defined(CONFIG_MEMORY_HOTPLUG_SPARSE) || defined(CONFIG_ACPI_HOTPLUG_MEMORY)
int memory_add_physaddr_to_nid(u64 start)
{
	int i, ret = 0;

	for_each_node(i)
		if (nodes_add[i].start <= start && nodes_add[i].end > start)
			ret = i;

	return ret;
}
EXPORT_SYMBOL_GPL(memory_add_physaddr_to_nid);
#endif
