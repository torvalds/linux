// SPDX-License-Identifier: GPL-2.0
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
#include <linux/nodemask.h>
#include <linux/memblock.h>

#include <asm/io.h>
#include <linux/pci_ids.h>
#include <linux/acpi.h>
#include <asm/types.h>
#include <asm/mmzone.h>
#include <asm/proto.h>
#include <asm/e820/api.h>
#include <asm/pci-direct.h>
#include <asm/numa.h>
#include <asm/mpspec.h>
#include <asm/apic.h>
#include <asm/amd_nb.h>

static unsigned char __initdata nodeids[8];

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

	return -ENOENT;
}

int __init amd_numa_init(void)
{
	u64 start = PFN_PHYS(0);
	u64 end = PFN_PHYS(max_pfn);
	unsigned numnodes;
	u64 prevbase;
	int i, j, nb;
	u32 nodeid, reg;
	unsigned int bits, cores, apicid_base;

	if (!early_pci_allowed())
		return -EINVAL;

	nb = find_northbridge();
	if (nb < 0)
		return nb;

	pr_info("Scanning NUMA topology in Northbridge %d\n", nb);

	reg = read_pci_config(0, nb, 0, 0x60);
	numnodes = ((reg >> 4) & 0xF) + 1;
	if (numnodes <= 1)
		return -ENOENT;

	pr_info("Number of physical nodes %d\n", numnodes);

	prevbase = 0;
	for (i = 0; i < 8; i++) {
		u64 base, limit;

		base = read_pci_config(0, nb, 1, 0x40 + i*8);
		limit = read_pci_config(0, nb, 1, 0x44 + i*8);

		nodeids[i] = nodeid = limit & 7;
		if ((base & 3) == 0) {
			if (i < numnodes)
				pr_info("Skipping disabled node %d\n", i);
			continue;
		}
		if (nodeid >= numnodes) {
			pr_info("Ignoring excess node %d (%Lx:%Lx)\n", nodeid,
				base, limit);
			continue;
		}

		if (!limit) {
			pr_info("Skipping node entry %d (base %Lx)\n",
				i, base);
			continue;
		}
		if ((base >> 8) & 3 || (limit >> 8) & 3) {
			pr_err("Node %d using interleaving mode %Lx/%Lx\n",
			       nodeid, (base >> 8) & 3, (limit >> 8) & 3);
			return -EINVAL;
		}
		if (node_isset(nodeid, numa_nodes_parsed)) {
			pr_info("Node %d already present, skipping\n",
				nodeid);
			continue;
		}

		limit >>= 16;
		limit++;
		limit <<= 24;

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
			pr_err("Node %d bogus settings %Lx-%Lx.\n",
			       nodeid, base, limit);
			continue;
		}

		/* Could sort here, but pun for now. Should not happen anyroads. */
		if (prevbase > base) {
			pr_err("Node map not sorted %Lx,%Lx\n",
			       prevbase, base);
			return -EINVAL;
		}

		pr_info("Node %d MemBase %016Lx Limit %016Lx\n",
			nodeid, base, limit);

		prevbase = base;
		numa_add_memblk(nodeid, base, limit);
		node_set(nodeid, numa_nodes_parsed);
	}

	if (!nodes_weight(numa_nodes_parsed))
		return -ENOENT;

	/*
	 * We seem to have valid NUMA configuration.  Map apicids to nodes
	 * using the coreid bits from early_identify_cpu.
	 */
	bits = boot_cpu_data.x86_coreid_bits;
	cores = 1 << bits;
	apicid_base = 0;

	/*
	 * get boot-time SMP configuration:
	 */
	early_get_smp_config();

	if (boot_cpu_physical_apicid > 0) {
		pr_info("BSP APIC ID: %02x\n", boot_cpu_physical_apicid);
		apicid_base = boot_cpu_physical_apicid;
	}

	for_each_node_mask(i, numa_nodes_parsed)
		for (j = apicid_base; j < cores + apicid_base; j++)
			set_apicid_to_node((i << bits) + j, i);

	return 0;
}
