/*
 * AMD NUMA support.
 * Discover the memory map and associated nodes.
 *
 * This version reads it directly from the AMD northbridge.
 *
 * Copyright 2002,2003 Andi Kleen, SuSE Labs.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/nodemask.h>
#include <linux/memblock.h>

#include <asm/io.h>
#include <linux/pci_ids.h>
#include <linux/acpi.h>
#include <asm/types.h>
#include <asm/mmzone.h>
#include <asm/proto.h>
#include <asm/e820.h>
#include <asm/pci-direct.h>
#include <asm/numa.h>
#include <asm/mpspec.h>
#include <asm/apic.h>
#include <asm/amd_nb.h>

static struct bootnode __initdata nodes[8];
static unsigned char __initdata nodeids[8];
static nodemask_t __initdata nodes_parsed = NODE_MASK_NONE;

static __init int find_northbridge(void)
{
	int num;

	for (num = 0; num < 32; num++) {
		u32 header;

		header = read_pci_config(0, num, 0, 0x00);
		if (header != (PCI_VENDOR_ID_AMD | (0x1100<<16)) &&
			header != (PCI_VENDOR_ID_AMD | (0x1200<<16)) &&
			header != (PCI_VENDOR_ID_AMD | (0x1300<<16)))
			continue;

		header = read_pci_config(0, num, 1, 0x00);
		if (header != (PCI_VENDOR_ID_AMD | (0x1101<<16)) &&
			header != (PCI_VENDOR_ID_AMD | (0x1201<<16)) &&
			header != (PCI_VENDOR_ID_AMD | (0x1301<<16)))
			continue;
		return num;
	}

	return -1;
}

static __init void early_get_boot_cpu_id(void)
{
	/*
	 * need to get the APIC ID of the BSP so can use that to
	 * create apicid_to_node in amd_scan_nodes()
	 */
#ifdef CONFIG_X86_MPPARSE
	/*
	 * get boot-time SMP configuration:
	 */
	if (smp_found_config)
		early_get_smp_config();
#endif
}

int __init amd_numa_init(unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long start = PFN_PHYS(start_pfn);
	unsigned long end = PFN_PHYS(end_pfn);
	unsigned numnodes;
	unsigned long prevbase;
	int i, nb, found = 0;
	u32 nodeid, reg;

	if (!early_pci_allowed())
		return -1;

	nb = find_northbridge();
	if (nb < 0)
		return nb;

	pr_info("Scanning NUMA topology in Northbridge %d\n", nb);

	reg = read_pci_config(0, nb, 0, 0x60);
	numnodes = ((reg >> 4) & 0xF) + 1;
	if (numnodes <= 1)
		return -1;

	pr_info("Number of physical nodes %d\n", numnodes);

	prevbase = 0;
	for (i = 0; i < 8; i++) {
		unsigned long base, limit;

		base = read_pci_config(0, nb, 1, 0x40 + i*8);
		limit = read_pci_config(0, nb, 1, 0x44 + i*8);

		nodeids[i] = nodeid = limit & 7;
		if ((base & 3) == 0) {
			if (i < numnodes)
				pr_info("Skipping disabled node %d\n", i);
			continue;
		}
		if (nodeid >= numnodes) {
			pr_info("Ignoring excess node %d (%lx:%lx)\n", nodeid,
				base, limit);
			continue;
		}

		if (!limit) {
			pr_info("Skipping node entry %d (base %lx)\n",
				i, base);
			continue;
		}
		if ((base >> 8) & 3 || (limit >> 8) & 3) {
			pr_err("Node %d using interleaving mode %lx/%lx\n",
			       nodeid, (base >> 8) & 3, (limit >> 8) & 3);
			return -1;
		}
		if (node_isset(nodeid, nodes_parsed)) {
			pr_info("Node %d already present, skipping\n",
				nodeid);
			continue;
		}

		limit >>= 16;
		limit <<= 24;
		limit |= (1<<24)-1;
		limit++;

		if (limit > end)
			limit = end;
		if (limit <= base)
			continue;

		base >>= 16;
		base <<= 24;

		if (base < start)
			base = start;
		if (limit > end)
			limit = end;
		if (limit == base) {
			pr_err("Empty node %d\n", nodeid);
			continue;
		}
		if (limit < base) {
			pr_err("Node %d bogus settings %lx-%lx.\n",
			       nodeid, base, limit);
			continue;
		}

		/* Could sort here, but pun for now. Should not happen anyroads. */
		if (prevbase > base) {
			pr_err("Node map not sorted %lx,%lx\n",
			       prevbase, base);
			return -1;
		}

		pr_info("Node %d MemBase %016lx Limit %016lx\n",
			nodeid, base, limit);

		found++;

		nodes[nodeid].start = base;
		nodes[nodeid].end = limit;

		prevbase = base;

		node_set(nodeid, nodes_parsed);
	}

	if (!found)
		return -1;
	return 0;
}

#ifdef CONFIG_NUMA_EMU
static s16 fake_apicid_to_node[MAX_LOCAL_APIC] __initdata = {
	[0 ... MAX_LOCAL_APIC-1] = NUMA_NO_NODE
};

void __init amd_get_nodes(struct bootnode *physnodes)
{
	int i;

	for_each_node_mask(i, nodes_parsed) {
		physnodes[i].start = nodes[i].start;
		physnodes[i].end = nodes[i].end;
	}
}

static int __init find_node_by_addr(unsigned long addr)
{
	int ret = NUMA_NO_NODE;
	int i;

	for (i = 0; i < 8; i++)
		if (addr >= nodes[i].start && addr < nodes[i].end) {
			ret = i;
			break;
		}
	return ret;
}

/*
 * For NUMA emulation, fake proximity domain (_PXM) to node id mappings must be
 * setup to represent the physical topology but reflect the emulated
 * environment.  For each emulated node, the real node which it appears on is
 * found and a fake pxm to nid mapping is created which mirrors the actual
 * locality.  node_distance() then represents the correct distances between
 * emulated nodes by using the fake acpi mappings to pxms.
 */
void __init amd_fake_nodes(const struct bootnode *nodes, int nr_nodes)
{
	unsigned int bits;
	unsigned int cores;
	unsigned int apicid_base = 0;
	int i;

	bits = boot_cpu_data.x86_coreid_bits;
	cores = 1 << bits;
	early_get_boot_cpu_id();
	if (boot_cpu_physical_apicid > 0)
		apicid_base = boot_cpu_physical_apicid;

	for (i = 0; i < nr_nodes; i++) {
		int index;
		int nid;
		int j;

		nid = find_node_by_addr(nodes[i].start);
		if (nid == NUMA_NO_NODE)
			continue;

		index = nodeids[nid] << bits;
		if (fake_apicid_to_node[index + apicid_base] == NUMA_NO_NODE)
			for (j = apicid_base; j < cores + apicid_base; j++)
				fake_apicid_to_node[index + j] = i;
#ifdef CONFIG_ACPI_NUMA
		__acpi_map_pxm_to_node(nid, i);
#endif
	}
	memcpy(apicid_to_node, fake_apicid_to_node, sizeof(apicid_to_node));
}
#endif /* CONFIG_NUMA_EMU */

int __init amd_scan_nodes(void)
{
	unsigned int bits;
	unsigned int cores;
	unsigned int apicid_base;
	int i;

	BUG_ON(nodes_empty(nodes_parsed));
	node_possible_map = nodes_parsed;
	memnode_shift = compute_hash_shift(nodes, 8, NULL);
	if (memnode_shift < 0) {
		pr_err("No NUMA node hash function found. Contact maintainer\n");
		return -1;
	}
	pr_info("Using node hash shift of %d\n", memnode_shift);

	/* use the coreid bits from early_identify_cpu */
	bits = boot_cpu_data.x86_coreid_bits;
	cores = (1<<bits);
	apicid_base = 0;
	/* get the APIC ID of the BSP early for systems with apicid lifting */
	early_get_boot_cpu_id();
	if (boot_cpu_physical_apicid > 0) {
		pr_info("BSP APIC ID: %02x\n", boot_cpu_physical_apicid);
		apicid_base = boot_cpu_physical_apicid;
	}

	for_each_node_mask(i, node_possible_map) {
		int j;

		memblock_x86_register_active_regions(i,
				nodes[i].start >> PAGE_SHIFT,
				nodes[i].end >> PAGE_SHIFT);
		for (j = apicid_base; j < cores + apicid_base; j++)
			apicid_to_node[(i << bits) + j] = i;
		setup_node_bootmem(i, nodes[i].start, nodes[i].end);
	}

	numa_init_array();
	return 0;
}
