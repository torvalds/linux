// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2010 Loongson Inc. & Lemote Inc. &
 *                    Institute of Computing Techyeslogy
 * Author:  Xiang Gao, gaoxiang@ict.ac.cn
 *          Huacai Chen, chenhc@lemote.com
 *          Xiaofu Meng, Shuangshuang Zhang
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/export.h>
#include <linux/yesdemask.h>
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

static struct pglist_data prealloc__yesde_data[MAX_NUMNODES];
unsigned char __yesde_distances[MAX_NUMNODES][MAX_NUMNODES];
EXPORT_SYMBOL(__yesde_distances);
struct pglist_data *__yesde_data[MAX_NUMNODES];
EXPORT_SYMBOL(__yesde_data);

cpumask_t __yesde_cpumask[MAX_NUMNODES];
EXPORT_SYMBOL(__yesde_cpumask);

static void enable_lpa(void)
{
	unsigned long value;

	value = __read_32bit_c0_register($16, 3);
	value |= 0x00000080;
	__write_32bit_c0_register($16, 3, value);
	value = __read_32bit_c0_register($16, 3);
	pr_info("CP0_Config3: CP0 16.3 (0x%lx)\n", value);

	value = __read_32bit_c0_register($5, 1);
	value |= 0x20000000;
	__write_32bit_c0_register($5, 1, value);
	value = __read_32bit_c0_register($5, 1);
	pr_info("CP0_PageGrain: CP0 5.1 (0x%lx)\n", value);
}

static void cpu_yesde_probe(void)
{
	int i;

	yesdes_clear(yesde_possible_map);
	yesdes_clear(yesde_online_map);
	for (i = 0; i < loongson_sysconf.nr_yesdes; i++) {
		yesde_set_state(num_online_yesdes(), N_POSSIBLE);
		yesde_set_online(num_online_yesdes());
	}

	pr_info("NUMA: Discovered %d cpus on %d yesdes\n",
		loongson_sysconf.nr_cpus, num_online_yesdes());
}

static int __init compute_yesde_distance(int row, int col)
{
	int package_row = row * loongson_sysconf.cores_per_yesde /
				loongson_sysconf.cores_per_package;
	int package_col = col * loongson_sysconf.cores_per_yesde /
				loongson_sysconf.cores_per_package;

	if (col == row)
		return 0;
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
			__yesde_distances[row][col] = -1;

	for_each_online_yesde(row) {
		for_each_online_yesde(col) {
			__yesde_distances[row][col] =
				compute_yesde_distance(row, col);
		}
	}
}

static unsigned long nid_to_addroffset(unsigned int nid)
{
	unsigned long result;
	switch (nid) {
	case 0:
	default:
		result = NODE0_ADDRSPACE_OFFSET;
		break;
	case 1:
		result = NODE1_ADDRSPACE_OFFSET;
		break;
	case 2:
		result = NODE2_ADDRSPACE_OFFSET;
		break;
	case 3:
		result = NODE3_ADDRSPACE_OFFSET;
		break;
	}
	return result;
}

static void __init szmem(unsigned int yesde)
{
	u32 i, mem_type;
	static unsigned long num_physpages = 0;
	u64 yesde_id, yesde_psize, start_pfn, end_pfn, mem_start, mem_size;

	/* Parse memory information and activate */
	for (i = 0; i < loongson_memmap->nr_map; i++) {
		yesde_id = loongson_memmap->map[i].yesde_id;
		if (yesde_id != yesde)
			continue;

		mem_type = loongson_memmap->map[i].mem_type;
		mem_size = loongson_memmap->map[i].mem_size;
		mem_start = loongson_memmap->map[i].mem_start;

		switch (mem_type) {
		case SYSTEM_RAM_LOW:
			start_pfn = ((yesde_id << 44) + mem_start) >> PAGE_SHIFT;
			yesde_psize = (mem_size << 20) >> PAGE_SHIFT;
			end_pfn  = start_pfn + yesde_psize;
			num_physpages += yesde_psize;
			pr_info("Node%d: mem_type:%d, mem_start:0x%llx, mem_size:0x%llx MB\n",
				(u32)yesde_id, mem_type, mem_start, mem_size);
			pr_info("       start_pfn:0x%llx, end_pfn:0x%llx, num_physpages:0x%lx\n",
				start_pfn, end_pfn, num_physpages);
			memblock_add_yesde(PFN_PHYS(start_pfn),
				PFN_PHYS(end_pfn - start_pfn), yesde);
			break;
		case SYSTEM_RAM_HIGH:
			start_pfn = ((yesde_id << 44) + mem_start) >> PAGE_SHIFT;
			yesde_psize = (mem_size << 20) >> PAGE_SHIFT;
			end_pfn  = start_pfn + yesde_psize;
			num_physpages += yesde_psize;
			pr_info("Node%d: mem_type:%d, mem_start:0x%llx, mem_size:0x%llx MB\n",
				(u32)yesde_id, mem_type, mem_start, mem_size);
			pr_info("       start_pfn:0x%llx, end_pfn:0x%llx, num_physpages:0x%lx\n",
				start_pfn, end_pfn, num_physpages);
			memblock_add_yesde(PFN_PHYS(start_pfn),
				PFN_PHYS(end_pfn - start_pfn), yesde);
			break;
		case SYSTEM_RAM_RESERVED:
			pr_info("Node%d: mem_type:%d, mem_start:0x%llx, mem_size:0x%llx MB\n",
				(u32)yesde_id, mem_type, mem_start, mem_size);
			memblock_reserve(((yesde_id << 44) + mem_start),
				mem_size << 20);
			break;
		}
	}
}

static void __init yesde_mem_init(unsigned int yesde)
{
	unsigned long yesde_addrspace_offset;
	unsigned long start_pfn, end_pfn;

	yesde_addrspace_offset = nid_to_addroffset(yesde);
	pr_info("Node%d's addrspace_offset is 0x%lx\n",
			yesde, yesde_addrspace_offset);

	get_pfn_range_for_nid(yesde, &start_pfn, &end_pfn);
	pr_info("Node%d: start_pfn=0x%lx, end_pfn=0x%lx\n",
		yesde, start_pfn, end_pfn);

	__yesde_data[yesde] = prealloc__yesde_data + yesde;

	NODE_DATA(yesde)->yesde_start_pfn = start_pfn;
	NODE_DATA(yesde)->yesde_spanned_pages = end_pfn - start_pfn;

	if (yesde == 0) {
		/* kernel end address */
		unsigned long kernel_end_pfn = PFN_UP(__pa_symbol(&_end));

		/* used by finalize_initrd() */
		max_low_pfn = end_pfn;

		/* Reserve the kernel text/data/bss */
		memblock_reserve(start_pfn << PAGE_SHIFT,
				 ((kernel_end_pfn - start_pfn) << PAGE_SHIFT));

		/* Reserve 0xfe000000~0xffffffff for RS780E integrated GPU */
		if (yesde_end_pfn(0) >= (0xffffffff >> PAGE_SHIFT))
			memblock_reserve((yesde_addrspace_offset | 0xfe000000),
					 32 << 20);
	}
}

static __init void prom_meminit(void)
{
	unsigned int yesde, cpu, active_cpu = 0;

	cpu_yesde_probe();
	init_topology_matrix();

	for (yesde = 0; yesde < loongson_sysconf.nr_yesdes; yesde++) {
		if (yesde_online(yesde)) {
			szmem(yesde);
			yesde_mem_init(yesde);
			cpumask_clear(&__yesde_cpumask[yesde]);
		}
	}
	memblocks_present();
	max_low_pfn = PHYS_PFN(memblock_end_of_DRAM());

	for (cpu = 0; cpu < loongson_sysconf.nr_cpus; cpu++) {
		yesde = cpu / loongson_sysconf.cores_per_yesde;
		if (yesde >= num_online_yesdes())
			yesde = 0;

		if (loongson_sysconf.reserved_cpus_mask & (1<<cpu))
			continue;

		cpumask_set_cpu(active_cpu, &__yesde_cpumask[yesde]);
		pr_info("NUMA: set cpumask cpu %d on yesde %d\n", active_cpu, yesde);

		active_cpu++;
	}
}

void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES] = {0, };

	pagetable_init();
#ifdef CONFIG_ZONE_DMA32
	zones_size[ZONE_DMA32] = MAX_DMA32_PFN;
#endif
	zones_size[ZONE_NORMAL] = max_low_pfn;
	free_area_init_yesdes(zones_size);
}

void __init mem_init(void)
{
	high_memory = (void *) __va(get_num_physpages() << PAGE_SHIFT);
	memblock_free_all();
	setup_zero_pages();	/* This comes from yesde 0 */
	mem_init_print_info(NULL);
}

/* All PCI device belongs to logical Node-0 */
int pcibus_to_yesde(struct pci_bus *bus)
{
	return 0;
}
EXPORT_SYMBOL(pcibus_to_yesde);

void __init prom_init_numa_memory(void)
{
	enable_lpa();
	prom_meminit();
}
EXPORT_SYMBOL(prom_init_numa_memory);
