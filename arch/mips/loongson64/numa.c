// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2010 Loongson Inc. & Lemote Inc. &
 *                    Institute of Computing Technology
 * Author:  Xiang Gao, gaoxiang@ict.ac.cn
 *          Huacai Chen, chenhc@lemote.com
 *          Xiaofu Meng, Shuangshuang Zhang
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/export.h>
#include <linux/nodemask.h>
#include <linux/swap.h>
#include <linux/memblock.h>
#include <linux/pfn.h>
#include <linux/highmem.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/sections.h>
#include <linux/irq.h>
#include <asm/bootinfo.h>
#include <asm/mc146818-time.h>
#include <asm/time.h>
#include <asm/wbflush.h>
#include <boot_param.h>
#include <loongson.h>

unsigned char __node_distances[MAX_NUMNODES][MAX_NUMNODES];
EXPORT_SYMBOL(__node_distances);

cpumask_t __node_cpumask[MAX_NUMNODES];
EXPORT_SYMBOL(__node_cpumask);

static void cpu_node_probe(void)
{
	int i;

	nodes_clear(node_possible_map);
	nodes_clear(node_online_map);
	for (i = 0; i < loongson_sysconf.nr_nodes; i++) {
		node_set_state(num_online_nodes(), N_POSSIBLE);
		node_set_online(num_online_nodes());
	}

	pr_info("NUMA: Discovered %d cpus on %d nodes\n",
		loongson_sysconf.nr_cpus, num_online_nodes());
}

static int __init compute_node_distance(int row, int col)
{
	int package_row = row * loongson_sysconf.cores_per_node /
				loongson_sysconf.cores_per_package;
	int package_col = col * loongson_sysconf.cores_per_node /
				loongson_sysconf.cores_per_package;

	if (col == row)
		return LOCAL_DISTANCE;
	else if (package_row == package_col)
		return 40;
	else
		return 100;
}

static void __init init_topology_matrix(void)
{
	int row, col;

	for (row = 0; row < MAX_NUMNODES; row++)
		for (col = 0; col < MAX_NUMNODES; col++)
			__node_distances[row][col] = -1;

	for_each_online_node(row) {
		for_each_online_node(col) {
			__node_distances[row][col] =
				compute_node_distance(row, col);
		}
	}
}

static void __init node_mem_init(unsigned int node)
{
	unsigned long node_addrspace_offset;
	unsigned long start_pfn, end_pfn;

	node_addrspace_offset = nid_to_addrbase(node);
	pr_info("Node%d's addrspace_offset is 0x%lx\n",
			node, node_addrspace_offset);

	get_pfn_range_for_nid(node, &start_pfn, &end_pfn);
	pr_info("Node%d: start_pfn=0x%lx, end_pfn=0x%lx\n",
		node, start_pfn, end_pfn);

	alloc_node_data(node);

	NODE_DATA(node)->node_start_pfn = start_pfn;
	NODE_DATA(node)->node_spanned_pages = end_pfn - start_pfn;

	if (node == 0) {
		/* kernel start address */
		unsigned long kernel_start_pfn = PFN_DOWN(__pa_symbol(&_text));

		/* kernel end address */
		unsigned long kernel_end_pfn = PFN_UP(__pa_symbol(&_end));

		/* used by finalize_initrd() */
		max_low_pfn = end_pfn;

		/* Reserve the kernel text/data/bss */
		memblock_reserve(kernel_start_pfn << PAGE_SHIFT,
				 ((kernel_end_pfn - kernel_start_pfn) << PAGE_SHIFT));

		/* Reserve 0xfe000000~0xffffffff for RS780E integrated GPU */
		if (node_end_pfn(0) >= (0xffffffff >> PAGE_SHIFT))
			memblock_reserve((node_addrspace_offset | 0xfe000000),
					 32 << 20);

		/* Reserve pfn range 0~node[0]->node_start_pfn */
		memblock_reserve(0, PAGE_SIZE * start_pfn);
		/* set nid for reserved memory on node 0 */
		memblock_set_node(0, 1ULL << 44, &memblock.reserved, 0);
	}
}

static __init void prom_meminit(void)
{
	unsigned int node, cpu, active_cpu = 0;

	cpu_node_probe();
	init_topology_matrix();

	for (node = 0; node < loongson_sysconf.nr_nodes; node++) {
		if (node_online(node)) {
			szmem(node);
			node_mem_init(node);
			cpumask_clear(&__node_cpumask[node]);
		}
	}
	max_low_pfn = PHYS_PFN(memblock_end_of_DRAM());

	for (cpu = 0; cpu < loongson_sysconf.nr_cpus; cpu++) {
		node = cpu / loongson_sysconf.cores_per_node;
		if (node >= num_online_nodes())
			node = 0;

		if (loongson_sysconf.reserved_cpus_mask & (1<<cpu))
			continue;

		cpumask_set_cpu(active_cpu, &__node_cpumask[node]);
		pr_info("NUMA: set cpumask cpu %d on node %d\n", active_cpu, node);

		active_cpu++;
	}
}

void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES] = {0, };

	pagetable_init();
	zones_size[ZONE_DMA32] = MAX_DMA32_PFN;
	zones_size[ZONE_NORMAL] = max_low_pfn;
	free_area_init(zones_size);
}

/* All PCI device belongs to logical Node-0 */
int pcibus_to_node(struct pci_bus *bus)
{
	return 0;
}
EXPORT_SYMBOL(pcibus_to_node);

void __init prom_init_numa_memory(void)
{
	pr_info("CP0_Config3: CP0 16.3 (0x%x)\n", read_c0_config3());
	pr_info("CP0_PageGrain: CP0 5.1 (0x%x)\n", read_c0_pagegrain());
	prom_meminit();
}
