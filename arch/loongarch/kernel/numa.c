// SPDX-License-Identifier: GPL-2.0
/*
 * Author:  Xiang Gao <gaoxiang@loongson.cn>
 *          Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
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
#include <linux/acpi.h>
#include <linux/efi.h>
#include <linux/irq.h>
#include <linux/pci.h>
#include <asm/bootinfo.h>
#include <asm/loongson.h>
#include <asm/numa.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/sections.h>
#include <asm/time.h>

int numa_off;
unsigned char node_distances[MAX_NUMNODES][MAX_NUMNODES];
EXPORT_SYMBOL(node_distances);

static struct numa_meminfo numa_meminfo;
cpumask_t cpus_on_node[MAX_NUMNODES];
cpumask_t phys_cpus_on_node[MAX_NUMNODES];
EXPORT_SYMBOL(cpus_on_node);

/*
 * apicid, cpu, node mappings
 */
s16 __cpuid_to_node[CONFIG_NR_CPUS] = {
	[0 ... CONFIG_NR_CPUS - 1] = NUMA_NO_NODE
};
EXPORT_SYMBOL(__cpuid_to_node);

nodemask_t numa_nodes_parsed __initdata;

#ifdef CONFIG_HAVE_SETUP_PER_CPU_AREA
unsigned long __per_cpu_offset[NR_CPUS] __read_mostly;
EXPORT_SYMBOL(__per_cpu_offset);

static int __init pcpu_cpu_to_node(int cpu)
{
	return early_cpu_to_node(cpu);
}

static int __init pcpu_cpu_distance(unsigned int from, unsigned int to)
{
	if (early_cpu_to_node(from) == early_cpu_to_node(to))
		return LOCAL_DISTANCE;
	else
		return REMOTE_DISTANCE;
}

void __init pcpu_populate_pte(unsigned long addr)
{
	populate_kernel_pte(addr);
}

void __init setup_per_cpu_areas(void)
{
	unsigned long delta;
	unsigned int cpu;
	int rc = -EINVAL;

	if (pcpu_chosen_fc == PCPU_FC_AUTO) {
		if (nr_node_ids >= 8)
			pcpu_chosen_fc = PCPU_FC_PAGE;
		else
			pcpu_chosen_fc = PCPU_FC_EMBED;
	}

	/*
	 * Always reserve area for module percpu variables.  That's
	 * what the legacy allocator did.
	 */
	if (pcpu_chosen_fc != PCPU_FC_PAGE) {
		rc = pcpu_embed_first_chunk(PERCPU_MODULE_RESERVE,
					    PERCPU_DYNAMIC_RESERVE, PMD_SIZE,
					    pcpu_cpu_distance, pcpu_cpu_to_node);
		if (rc < 0)
			pr_warn("%s allocator failed (%d), falling back to page size\n",
				pcpu_fc_names[pcpu_chosen_fc], rc);
	}
	if (rc < 0)
		rc = pcpu_page_first_chunk(PERCPU_MODULE_RESERVE, pcpu_cpu_to_node);
	if (rc < 0)
		panic("cannot initialize percpu area (err=%d)", rc);

	delta = (unsigned long)pcpu_base_addr - (unsigned long)__per_cpu_start;
	for_each_possible_cpu(cpu)
		__per_cpu_offset[cpu] = delta + pcpu_unit_offsets[cpu];
}
#endif

/*
 * Get nodeid by logical cpu number.
 * __cpuid_to_node maps phyical cpu id to node, so we
 * should use cpu_logical_map(cpu) to index it.
 *
 * This routine is only used in early phase during
 * booting, after setup_per_cpu_areas calling and numa_node
 * initialization, cpu_to_node will be used instead.
 */
int early_cpu_to_node(int cpu)
{
	int physid = cpu_logical_map(cpu);

	if (physid < 0)
		return NUMA_NO_NODE;

	return __cpuid_to_node[physid];
}

void __init early_numa_add_cpu(int cpuid, s16 node)
{
	int cpu = __cpu_number_map[cpuid];

	if (cpu < 0)
		return;

	cpumask_set_cpu(cpu, &cpus_on_node[node]);
	cpumask_set_cpu(cpuid, &phys_cpus_on_node[node]);
}

void numa_add_cpu(unsigned int cpu)
{
	int nid = cpu_to_node(cpu);
	cpumask_set_cpu(cpu, &cpus_on_node[nid]);
}

void numa_remove_cpu(unsigned int cpu)
{
	int nid = cpu_to_node(cpu);
	cpumask_clear_cpu(cpu, &cpus_on_node[nid]);
}

static int __init numa_add_memblk_to(int nid, u64 start, u64 end,
				     struct numa_meminfo *mi)
{
	/* ignore zero length blks */
	if (start == end)
		return 0;

	/* whine about and ignore invalid blks */
	if (start > end || nid < 0 || nid >= MAX_NUMNODES) {
		pr_warn("NUMA: Warning: invalid memblk node %d [mem %#010Lx-%#010Lx]\n",
			   nid, start, end - 1);
		return 0;
	}

	if (mi->nr_blks >= NR_NODE_MEMBLKS) {
		pr_err("NUMA: too many memblk ranges\n");
		return -EINVAL;
	}

	mi->blk[mi->nr_blks].start = PFN_ALIGN(start);
	mi->blk[mi->nr_blks].end = PFN_ALIGN(end - PAGE_SIZE + 1);
	mi->blk[mi->nr_blks].nid = nid;
	mi->nr_blks++;
	return 0;
}

/**
 * numa_add_memblk - Add one numa_memblk to numa_meminfo
 * @nid: NUMA node ID of the new memblk
 * @start: Start address of the new memblk
 * @end: End address of the new memblk
 *
 * Add a new memblk to the default numa_meminfo.
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */
int __init numa_add_memblk(int nid, u64 start, u64 end)
{
	return numa_add_memblk_to(nid, start, end, &numa_meminfo);
}

static void __init node_mem_init(unsigned int node)
{
	unsigned long start_pfn, end_pfn;
	unsigned long node_addrspace_offset;

	node_addrspace_offset = nid_to_addrbase(node);
	pr_info("Node%d's addrspace_offset is 0x%lx\n",
			node, node_addrspace_offset);

	get_pfn_range_for_nid(node, &start_pfn, &end_pfn);
	pr_info("Node%d: start_pfn=0x%lx, end_pfn=0x%lx\n",
		node, start_pfn, end_pfn);

	alloc_node_data(node);
}

#ifdef CONFIG_ACPI_NUMA

static void __init add_node_intersection(u32 node, u64 start, u64 size, u32 type)
{
	static unsigned long num_physpages;

	num_physpages += (size >> PAGE_SHIFT);
	pr_info("Node%d: mem_type:%d, mem_start:0x%llx, mem_size:0x%llx Bytes\n",
		node, type, start, size);
	pr_info("       start_pfn:0x%llx, end_pfn:0x%llx, num_physpages:0x%lx\n",
		start >> PAGE_SHIFT, (start + size) >> PAGE_SHIFT, num_physpages);
	memblock_set_node(start, size, &memblock.memory, node);
}

/*
 * add_numamem_region
 *
 * Add a uasable memory region described by BIOS. The
 * routine gets each intersection between BIOS's region
 * and node's region, and adds them into node's memblock
 * pool.
 *
 */
static void __init add_numamem_region(u64 start, u64 end, u32 type)
{
	u32 i;
	u64 ofs = start;

	if (start >= end) {
		pr_debug("Invalid region: %016llx-%016llx\n", start, end);
		return;
	}

	for (i = 0; i < numa_meminfo.nr_blks; i++) {
		struct numa_memblk *mb = &numa_meminfo.blk[i];

		if (ofs > mb->end)
			continue;

		if (end > mb->end) {
			add_node_intersection(mb->nid, ofs, mb->end - ofs, type);
			ofs = mb->end;
		} else {
			add_node_intersection(mb->nid, ofs, end - ofs, type);
			break;
		}
	}
}

static void __init init_node_memblock(void)
{
	u32 mem_type;
	u64 mem_end, mem_start, mem_size;
	efi_memory_desc_t *md;

	/* Parse memory information and activate */
	for_each_efi_memory_desc(md) {
		mem_type = md->type;
		mem_start = md->phys_addr;
		mem_size = md->num_pages << EFI_PAGE_SHIFT;
		mem_end = mem_start + mem_size;

		switch (mem_type) {
		case EFI_LOADER_CODE:
		case EFI_LOADER_DATA:
		case EFI_BOOT_SERVICES_CODE:
		case EFI_BOOT_SERVICES_DATA:
		case EFI_PERSISTENT_MEMORY:
		case EFI_CONVENTIONAL_MEMORY:
			add_numamem_region(mem_start, mem_end, mem_type);
			break;
		case EFI_PAL_CODE:
		case EFI_UNUSABLE_MEMORY:
		case EFI_ACPI_RECLAIM_MEMORY:
			add_numamem_region(mem_start, mem_end, mem_type);
			fallthrough;
		case EFI_RESERVED_TYPE:
		case EFI_RUNTIME_SERVICES_CODE:
		case EFI_RUNTIME_SERVICES_DATA:
		case EFI_MEMORY_MAPPED_IO:
		case EFI_MEMORY_MAPPED_IO_PORT_SPACE:
			pr_info("Resvd: mem_type:%d, mem_start:0x%llx, mem_size:0x%llx Bytes\n",
					mem_type, mem_start, mem_size);
			break;
		}
	}
}

static void __init numa_default_distance(void)
{
	int row, col;

	for (row = 0; row < MAX_NUMNODES; row++)
		for (col = 0; col < MAX_NUMNODES; col++) {
			if (col == row)
				node_distances[row][col] = LOCAL_DISTANCE;
			else
				/* We assume that one node per package here!
				 *
				 * A SLIT should be used for multiple nodes
				 * per package to override default setting.
				 */
				node_distances[row][col] = REMOTE_DISTANCE;
	}
}

/*
 * fake_numa_init() - For Non-ACPI systems
 * Return: 0 on success, -errno on failure.
 */
static int __init fake_numa_init(void)
{
	phys_addr_t start = memblock_start_of_DRAM();
	phys_addr_t end = memblock_end_of_DRAM() - 1;

	node_set(0, numa_nodes_parsed);
	pr_info("Faking a node at [mem %pap-%pap]\n", &start, &end);

	return numa_add_memblk(0, start, end + 1);
}

int __init init_numa_memory(void)
{
	int i;
	int ret;
	int node;

	for (i = 0; i < NR_CPUS; i++)
		set_cpuid_to_node(i, NUMA_NO_NODE);

	numa_default_distance();
	nodes_clear(numa_nodes_parsed);
	nodes_clear(node_possible_map);
	nodes_clear(node_online_map);
	memset(&numa_meminfo, 0, sizeof(numa_meminfo));

	/* Parse SRAT and SLIT if provided by firmware. */
	ret = acpi_disabled ? fake_numa_init() : acpi_numa_init();
	if (ret < 0)
		return ret;

	node_possible_map = numa_nodes_parsed;
	if (WARN_ON(nodes_empty(node_possible_map)))
		return -EINVAL;

	init_node_memblock();
	if (!memblock_validate_numa_coverage(SZ_1M))
		return -EINVAL;

	for_each_node_mask(node, node_possible_map) {
		node_mem_init(node);
		node_set_online(node);
	}
	max_low_pfn = PHYS_PFN(memblock_end_of_DRAM());

	setup_nr_node_ids();
	loongson_sysconf.nr_nodes = nr_node_ids;
	loongson_sysconf.cores_per_node = cpumask_weight(&phys_cpus_on_node[0]);

	return 0;
}

#endif

void __init paging_init(void)
{
	unsigned int node;
	unsigned long zones_size[MAX_NR_ZONES] = {0, };

	for_each_online_node(node) {
		unsigned long start_pfn, end_pfn;

		get_pfn_range_for_nid(node, &start_pfn, &end_pfn);

		if (end_pfn > max_low_pfn)
			max_low_pfn = end_pfn;
	}
#ifdef CONFIG_ZONE_DMA32
	zones_size[ZONE_DMA32] = MAX_DMA32_PFN;
#endif
	zones_size[ZONE_NORMAL] = max_low_pfn;
	free_area_init(zones_size);
}

void __init mem_init(void)
{
	memblock_free_all();
}

int pcibus_to_node(struct pci_bus *bus)
{
	return dev_to_node(&bus->dev);
}
EXPORT_SYMBOL(pcibus_to_node);
