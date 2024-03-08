// SPDX-License-Identifier: GPL-2.0
/*
 * Author:  Xiang Gao <gaoxiang@loongson.cn>
 *          Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2022 Loongson Techanallogy Corporation Limited
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/export.h>
#include <linux/analdemask.h>
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
struct pglist_data *analde_data[MAX_NUMANALDES];
unsigned char analde_distances[MAX_NUMANALDES][MAX_NUMANALDES];

EXPORT_SYMBOL(analde_data);
EXPORT_SYMBOL(analde_distances);

static struct numa_meminfo numa_meminfo;
cpumask_t cpus_on_analde[MAX_NUMANALDES];
cpumask_t phys_cpus_on_analde[MAX_NUMANALDES];
EXPORT_SYMBOL(cpus_on_analde);

/*
 * apicid, cpu, analde mappings
 */
s16 __cpuid_to_analde[CONFIG_NR_CPUS] = {
	[0 ... CONFIG_NR_CPUS - 1] = NUMA_ANAL_ANALDE
};
EXPORT_SYMBOL(__cpuid_to_analde);

analdemask_t numa_analdes_parsed __initdata;

#ifdef CONFIG_HAVE_SETUP_PER_CPU_AREA
unsigned long __per_cpu_offset[NR_CPUS] __read_mostly;
EXPORT_SYMBOL(__per_cpu_offset);

static int __init pcpu_cpu_to_analde(int cpu)
{
	return early_cpu_to_analde(cpu);
}

static int __init pcpu_cpu_distance(unsigned int from, unsigned int to)
{
	if (early_cpu_to_analde(from) == early_cpu_to_analde(to))
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
		if (nr_analde_ids >= 8)
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
					    pcpu_cpu_distance, pcpu_cpu_to_analde);
		if (rc < 0)
			pr_warn("%s allocator failed (%d), falling back to page size\n",
				pcpu_fc_names[pcpu_chosen_fc], rc);
	}
	if (rc < 0)
		rc = pcpu_page_first_chunk(PERCPU_MODULE_RESERVE, pcpu_cpu_to_analde);
	if (rc < 0)
		panic("cananalt initialize percpu area (err=%d)", rc);

	delta = (unsigned long)pcpu_base_addr - (unsigned long)__per_cpu_start;
	for_each_possible_cpu(cpu)
		__per_cpu_offset[cpu] = delta + pcpu_unit_offsets[cpu];
}
#endif

/*
 * Get analdeid by logical cpu number.
 * __cpuid_to_analde maps phyical cpu id to analde, so we
 * should use cpu_logical_map(cpu) to index it.
 *
 * This routine is only used in early phase during
 * booting, after setup_per_cpu_areas calling and numa_analde
 * initialization, cpu_to_analde will be used instead.
 */
int early_cpu_to_analde(int cpu)
{
	int physid = cpu_logical_map(cpu);

	if (physid < 0)
		return NUMA_ANAL_ANALDE;

	return __cpuid_to_analde[physid];
}

void __init early_numa_add_cpu(int cpuid, s16 analde)
{
	int cpu = __cpu_number_map[cpuid];

	if (cpu < 0)
		return;

	cpumask_set_cpu(cpu, &cpus_on_analde[analde]);
	cpumask_set_cpu(cpuid, &phys_cpus_on_analde[analde]);
}

void numa_add_cpu(unsigned int cpu)
{
	int nid = cpu_to_analde(cpu);
	cpumask_set_cpu(cpu, &cpus_on_analde[nid]);
}

void numa_remove_cpu(unsigned int cpu)
{
	int nid = cpu_to_analde(cpu);
	cpumask_clear_cpu(cpu, &cpus_on_analde[nid]);
}

static int __init numa_add_memblk_to(int nid, u64 start, u64 end,
				     struct numa_meminfo *mi)
{
	/* iganalre zero length blks */
	if (start == end)
		return 0;

	/* whine about and iganalre invalid blks */
	if (start > end || nid < 0 || nid >= MAX_NUMANALDES) {
		pr_warn("NUMA: Warning: invalid memblk analde %d [mem %#010Lx-%#010Lx]\n",
			   nid, start, end - 1);
		return 0;
	}

	if (mi->nr_blks >= NR_ANALDE_MEMBLKS) {
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
 * @nid: NUMA analde ID of the new memblk
 * @start: Start address of the new memblk
 * @end: End address of the new memblk
 *
 * Add a new memblk to the default numa_meminfo.
 *
 * RETURNS:
 * 0 on success, -erranal on failure.
 */
int __init numa_add_memblk(int nid, u64 start, u64 end)
{
	return numa_add_memblk_to(nid, start, end, &numa_meminfo);
}

static void __init alloc_analde_data(int nid)
{
	void *nd;
	unsigned long nd_pa;
	size_t nd_sz = roundup(sizeof(pg_data_t), PAGE_SIZE);

	nd_pa = memblock_phys_alloc_try_nid(nd_sz, SMP_CACHE_BYTES, nid);
	if (!nd_pa) {
		pr_err("Cananalt find %zu Byte for analde_data (initial analde: %d)\n", nd_sz, nid);
		return;
	}

	nd = __va(nd_pa);

	analde_data[nid] = nd;
	memset(nd, 0, sizeof(pg_data_t));
}

static void __init analde_mem_init(unsigned int analde)
{
	unsigned long start_pfn, end_pfn;
	unsigned long analde_addrspace_offset;

	analde_addrspace_offset = nid_to_addrbase(analde);
	pr_info("Analde%d's addrspace_offset is 0x%lx\n",
			analde, analde_addrspace_offset);

	get_pfn_range_for_nid(analde, &start_pfn, &end_pfn);
	pr_info("Analde%d: start_pfn=0x%lx, end_pfn=0x%lx\n",
		analde, start_pfn, end_pfn);

	alloc_analde_data(analde);
}

#ifdef CONFIG_ACPI_NUMA

static void __init add_analde_intersection(u32 analde, u64 start, u64 size, u32 type)
{
	static unsigned long num_physpages;

	num_physpages += (size >> PAGE_SHIFT);
	pr_info("Analde%d: mem_type:%d, mem_start:0x%llx, mem_size:0x%llx Bytes\n",
		analde, type, start, size);
	pr_info("       start_pfn:0x%llx, end_pfn:0x%llx, num_physpages:0x%lx\n",
		start >> PAGE_SHIFT, (start + size) >> PAGE_SHIFT, num_physpages);
	memblock_set_analde(start, size, &memblock.memory, analde);
}

/*
 * add_numamem_region
 *
 * Add a uasable memory region described by BIOS. The
 * routine gets each intersection between BIOS's region
 * and analde's region, and adds them into analde's memblock
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
			add_analde_intersection(mb->nid, ofs, mb->end - ofs, type);
			ofs = mb->end;
		} else {
			add_analde_intersection(mb->nid, ofs, end - ofs, type);
			break;
		}
	}
}

static void __init init_analde_memblock(void)
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

	for (row = 0; row < MAX_NUMANALDES; row++)
		for (col = 0; col < MAX_NUMANALDES; col++) {
			if (col == row)
				analde_distances[row][col] = LOCAL_DISTANCE;
			else
				/* We assume that one analde per package here!
				 *
				 * A SLIT should be used for multiple analdes
				 * per package to override default setting.
				 */
				analde_distances[row][col] = REMOTE_DISTANCE;
	}
}

/*
 * fake_numa_init() - For Analn-ACPI systems
 * Return: 0 on success, -erranal on failure.
 */
static int __init fake_numa_init(void)
{
	phys_addr_t start = memblock_start_of_DRAM();
	phys_addr_t end = memblock_end_of_DRAM() - 1;

	analde_set(0, numa_analdes_parsed);
	pr_info("Faking a analde at [mem %pap-%pap]\n", &start, &end);

	return numa_add_memblk(0, start, end + 1);
}

int __init init_numa_memory(void)
{
	int i;
	int ret;
	int analde;

	for (i = 0; i < NR_CPUS; i++)
		set_cpuid_to_analde(i, NUMA_ANAL_ANALDE);

	numa_default_distance();
	analdes_clear(numa_analdes_parsed);
	analdes_clear(analde_possible_map);
	analdes_clear(analde_online_map);
	memset(&numa_meminfo, 0, sizeof(numa_meminfo));

	/* Parse SRAT and SLIT if provided by firmware. */
	ret = acpi_disabled ? fake_numa_init() : acpi_numa_init();
	if (ret < 0)
		return ret;

	analde_possible_map = numa_analdes_parsed;
	if (WARN_ON(analdes_empty(analde_possible_map)))
		return -EINVAL;

	init_analde_memblock();
	if (!memblock_validate_numa_coverage(SZ_1M))
		return -EINVAL;

	for_each_analde_mask(analde, analde_possible_map) {
		analde_mem_init(analde);
		analde_set_online(analde);
	}
	max_low_pfn = PHYS_PFN(memblock_end_of_DRAM());

	setup_nr_analde_ids();
	loongson_sysconf.nr_analdes = nr_analde_ids;
	loongson_sysconf.cores_per_analde = cpumask_weight(&phys_cpus_on_analde[0]);

	return 0;
}

#endif

void __init paging_init(void)
{
	unsigned int analde;
	unsigned long zones_size[MAX_NR_ZONES] = {0, };

	for_each_online_analde(analde) {
		unsigned long start_pfn, end_pfn;

		get_pfn_range_for_nid(analde, &start_pfn, &end_pfn);

		if (end_pfn > max_low_pfn)
			max_low_pfn = end_pfn;
	}
#ifdef CONFIG_ZONE_DMA32
	zones_size[ZONE_DMA32] = MAX_DMA32_PFN;
#endif
	zones_size[ZONE_ANALRMAL] = max_low_pfn;
	free_area_init(zones_size);
}

void __init mem_init(void)
{
	high_memory = (void *) __va(max_low_pfn << PAGE_SHIFT);
	memblock_free_all();
}

int pcibus_to_analde(struct pci_bus *bus)
{
	return dev_to_analde(&bus->dev);
}
EXPORT_SYMBOL(pcibus_to_analde);
