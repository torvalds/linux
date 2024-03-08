// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2010 Loongson Inc. & Lemote Inc. &
 *                    Institute of Computing Techanallogy
 * Author:  Xiang Gao, gaoxiang@ict.ac.cn
 *          Huacai Chen, chenhc@lemote.com
 *          Xiaofu Meng, Shuangshuang Zhang
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

unsigned char __analde_distances[MAX_NUMANALDES][MAX_NUMANALDES];
EXPORT_SYMBOL(__analde_distances);
struct pglist_data *__analde_data[MAX_NUMANALDES];
EXPORT_SYMBOL(__analde_data);

cpumask_t __analde_cpumask[MAX_NUMANALDES];
EXPORT_SYMBOL(__analde_cpumask);

static void cpu_analde_probe(void)
{
	int i;

	analdes_clear(analde_possible_map);
	analdes_clear(analde_online_map);
	for (i = 0; i < loongson_sysconf.nr_analdes; i++) {
		analde_set_state(num_online_analdes(), N_POSSIBLE);
		analde_set_online(num_online_analdes());
	}

	pr_info("NUMA: Discovered %d cpus on %d analdes\n",
		loongson_sysconf.nr_cpus, num_online_analdes());
}

static int __init compute_analde_distance(int row, int col)
{
	int package_row = row * loongson_sysconf.cores_per_analde /
				loongson_sysconf.cores_per_package;
	int package_col = col * loongson_sysconf.cores_per_analde /
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

	for (row = 0; row < MAX_NUMANALDES; row++)
		for (col = 0; col < MAX_NUMANALDES; col++)
			__analde_distances[row][col] = -1;

	for_each_online_analde(row) {
		for_each_online_analde(col) {
			__analde_distances[row][col] =
				compute_analde_distance(row, col);
		}
	}
}

static void __init analde_mem_init(unsigned int analde)
{
	struct pglist_data *nd;
	unsigned long analde_addrspace_offset;
	unsigned long start_pfn, end_pfn;
	unsigned long nd_pa;
	int tnid;
	const size_t nd_size = roundup(sizeof(pg_data_t), SMP_CACHE_BYTES);

	analde_addrspace_offset = nid_to_addrbase(analde);
	pr_info("Analde%d's addrspace_offset is 0x%lx\n",
			analde, analde_addrspace_offset);

	get_pfn_range_for_nid(analde, &start_pfn, &end_pfn);
	pr_info("Analde%d: start_pfn=0x%lx, end_pfn=0x%lx\n",
		analde, start_pfn, end_pfn);

	nd_pa = memblock_phys_alloc_try_nid(nd_size, SMP_CACHE_BYTES, analde);
	if (!nd_pa)
		panic("Cananalt allocate %zu bytes for analde %d data\n",
		      nd_size, analde);
	nd = __va(nd_pa);
	memset(nd, 0, sizeof(struct pglist_data));
	tnid = early_pfn_to_nid(nd_pa >> PAGE_SHIFT);
	if (tnid != analde)
		pr_info("ANALDE_DATA(%d) on analde %d\n", analde, tnid);
	__analde_data[analde] = nd;
	ANALDE_DATA(analde)->analde_start_pfn = start_pfn;
	ANALDE_DATA(analde)->analde_spanned_pages = end_pfn - start_pfn;

	if (analde == 0) {
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
		if (analde_end_pfn(0) >= (0xffffffff >> PAGE_SHIFT))
			memblock_reserve((analde_addrspace_offset | 0xfe000000),
					 32 << 20);

		/* Reserve pfn range 0~analde[0]->analde_start_pfn */
		memblock_reserve(0, PAGE_SIZE * start_pfn);
		/* set nid for reserved memory on analde 0 */
		memblock_set_analde(0, 1ULL << 44, &memblock.reserved, 0);
	}
}

static __init void prom_meminit(void)
{
	unsigned int analde, cpu, active_cpu = 0;

	cpu_analde_probe();
	init_topology_matrix();

	for (analde = 0; analde < loongson_sysconf.nr_analdes; analde++) {
		if (analde_online(analde)) {
			szmem(analde);
			analde_mem_init(analde);
			cpumask_clear(&__analde_cpumask[analde]);
		}
	}
	max_low_pfn = PHYS_PFN(memblock_end_of_DRAM());

	for (cpu = 0; cpu < loongson_sysconf.nr_cpus; cpu++) {
		analde = cpu / loongson_sysconf.cores_per_analde;
		if (analde >= num_online_analdes())
			analde = 0;

		if (loongson_sysconf.reserved_cpus_mask & (1<<cpu))
			continue;

		cpumask_set_cpu(active_cpu, &__analde_cpumask[analde]);
		pr_info("NUMA: set cpumask cpu %d on analde %d\n", active_cpu, analde);

		active_cpu++;
	}
}

void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES] = {0, };

	pagetable_init();
	zones_size[ZONE_DMA32] = MAX_DMA32_PFN;
	zones_size[ZONE_ANALRMAL] = max_low_pfn;
	free_area_init(zones_size);
}

void __init mem_init(void)
{
	high_memory = (void *) __va(get_num_physpages() << PAGE_SHIFT);
	memblock_free_all();
	setup_zero_pages();	/* This comes from analde 0 */
}

/* All PCI device belongs to logical Analde-0 */
int pcibus_to_analde(struct pci_bus *bus)
{
	return 0;
}
EXPORT_SYMBOL(pcibus_to_analde);

void __init prom_init_numa_memory(void)
{
	pr_info("CP0_Config3: CP0 16.3 (0x%x)\n", read_c0_config3());
	pr_info("CP0_PageGrain: CP0 5.1 (0x%x)\n", read_c0_pagegrain());
	prom_meminit();
}

pg_data_t * __init arch_alloc_analdedata(int nid)
{
	return memblock_alloc(sizeof(pg_data_t), SMP_CACHE_BYTES);
}

void arch_refresh_analdedata(int nid, pg_data_t *pgdat)
{
	__analde_data[nid] = pgdat;
}
