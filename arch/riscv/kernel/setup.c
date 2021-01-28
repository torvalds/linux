// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Chen Liqin <liqin.chen@sunplusct.com>
 *  Lennox Wu <lennox.wu@sunplusct.com>
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2020 FORTH-ICS/CARV
 *  Nick Kossifidis <mick@ics.forth.gr>
 */

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#include <linux/sched.h>
#include <linux/console.h>
#include <linux/screen_info.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/sched/task.h>
#include <linux/swiotlb.h>
#include <linux/smp.h>
#include <linux/efi.h>

#include <asm/cpu_ops.h>
#include <asm/early_ioremap.h>
#include <asm/setup.h>
#include <asm/set_memory.h>
#include <asm/sections.h>
#include <asm/sbi.h>
#include <asm/tlbflush.h>
#include <asm/thread_info.h>
#include <asm/kasan.h>
#include <asm/efi.h>

#include "head.h"

#if defined(CONFIG_DUMMY_CONSOLE) || defined(CONFIG_EFI)
struct screen_info screen_info __section(".data") = {
	.orig_video_lines	= 30,
	.orig_video_cols	= 80,
	.orig_video_mode	= 0,
	.orig_video_ega_bx	= 0,
	.orig_video_isVGA	= 1,
	.orig_video_points	= 8
};
#endif

/*
 * The lucky hart to first increment this variable will boot the other cores.
 * This is used before the kernel initializes the BSS so it can't be in the
 * BSS.
 */
atomic_t hart_lottery __section(".sdata");
unsigned long boot_cpu_hartid;
static DEFINE_PER_CPU(struct cpu, cpu_devices);

/*
 * Place kernel memory regions on the resource tree so that
 * kexec-tools can retrieve them from /proc/iomem. While there
 * also add "System RAM" regions for compatibility with other
 * archs, and the rest of the known regions for completeness.
 */
static struct resource code_res = { .name = "Kernel code", };
static struct resource data_res = { .name = "Kernel data", };
static struct resource rodata_res = { .name = "Kernel rodata", };
static struct resource bss_res = { .name = "Kernel bss", };

static int __init add_resource(struct resource *parent,
				struct resource *res)
{
	int ret = 0;

	ret = insert_resource(parent, res);
	if (ret < 0) {
		pr_err("Failed to add a %s resource at %llx\n",
			res->name, (unsigned long long) res->start);
		return ret;
	}

	return 1;
}

static int __init add_kernel_resources(struct resource *res)
{
	int ret = 0;

	/*
	 * The memory region of the kernel image is continuous and
	 * was reserved on setup_bootmem, find it here and register
	 * it as a resource, then register the various segments of
	 * the image as child nodes
	 */
	if (!(res->start <= code_res.start && res->end >= data_res.end))
		return 0;

	res->name = "Kernel image";
	res->flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;

	/*
	 * We removed a part of this region on setup_bootmem so
	 * we need to expand the resource for the bss to fit in.
	 */
	res->end = bss_res.end;

	ret = add_resource(&iomem_resource, res);
	if (ret < 0)
		return ret;

	ret = add_resource(res, &code_res);
	if (ret < 0)
		return ret;

	ret = add_resource(res, &rodata_res);
	if (ret < 0)
		return ret;

	ret = add_resource(res, &data_res);
	if (ret < 0)
		return ret;

	ret = add_resource(res, &bss_res);

	return ret;
}

static void __init init_resources(void)
{
	struct memblock_region *region = NULL;
	struct resource *res = NULL;
	struct resource *mem_res = NULL;
	size_t mem_res_sz = 0;
	int ret = 0, i = 0;

	code_res.start = __pa_symbol(_text);
	code_res.end = __pa_symbol(_etext) - 1;
	code_res.flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;

	rodata_res.start = __pa_symbol(__start_rodata);
	rodata_res.end = __pa_symbol(__end_rodata) - 1;
	rodata_res.flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;

	data_res.start = __pa_symbol(_data);
	data_res.end = __pa_symbol(_edata) - 1;
	data_res.flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;

	bss_res.start = __pa_symbol(__bss_start);
	bss_res.end = __pa_symbol(__bss_stop) - 1;
	bss_res.flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;

	mem_res_sz = (memblock.memory.cnt + memblock.reserved.cnt) * sizeof(*mem_res);
	mem_res = memblock_alloc(mem_res_sz, SMP_CACHE_BYTES);
	if (!mem_res)
		panic("%s: Failed to allocate %zu bytes\n", __func__, mem_res_sz);
	/*
	 * Start by adding the reserved regions, if they overlap
	 * with /memory regions, insert_resource later on will take
	 * care of it.
	 */
	for_each_reserved_mem_region(region) {
		res = &mem_res[i++];

		res->name = "Reserved";
		res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;
		res->start = __pfn_to_phys(memblock_region_reserved_base_pfn(region));
		res->end = __pfn_to_phys(memblock_region_reserved_end_pfn(region)) - 1;

		ret = add_kernel_resources(res);
		if (ret < 0)
			goto error;
		else if (ret)
			continue;

		/*
		 * Ignore any other reserved regions within
		 * system memory.
		 */
		if (memblock_is_memory(res->start)) {
			memblock_free((phys_addr_t) res, sizeof(struct resource));
			continue;
		}

		ret = add_resource(&iomem_resource, res);
		if (ret < 0)
			goto error;
	}

	/* Add /memory regions to the resource tree */
	for_each_mem_region(region) {
		res = &mem_res[i++];

		if (unlikely(memblock_is_nomap(region))) {
			res->name = "Reserved";
			res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;
		} else {
			res->name = "System RAM";
			res->flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;
		}

		res->start = __pfn_to_phys(memblock_region_memory_base_pfn(region));
		res->end = __pfn_to_phys(memblock_region_memory_end_pfn(region)) - 1;

		ret = add_resource(&iomem_resource, res);
		if (ret < 0)
			goto error;
	}

	return;

 error:
	/* Better an empty resource tree than an inconsistent one */
	release_child_resources(&iomem_resource);
	memblock_free((phys_addr_t) mem_res, mem_res_sz);
}


static void __init parse_dtb(void)
{
	/* Early scan of device tree from init memory */
	if (early_init_dt_scan(dtb_early_va))
		return;

	pr_err("No DTB passed to the kernel\n");
#ifdef CONFIG_CMDLINE_FORCE
	strlcpy(boot_command_line, CONFIG_CMDLINE, COMMAND_LINE_SIZE);
	pr_info("Forcing kernel command line to: %s\n", boot_command_line);
#endif
}

void __init setup_arch(char **cmdline_p)
{
	parse_dtb();
	init_mm.start_code = (unsigned long) _stext;
	init_mm.end_code   = (unsigned long) _etext;
	init_mm.end_data   = (unsigned long) _edata;
	init_mm.brk        = (unsigned long) _end;

	*cmdline_p = boot_command_line;

	early_ioremap_setup();
	jump_label_init();
	parse_early_param();

	efi_init();
	setup_bootmem();
	paging_init();
	init_resources();
#if IS_ENABLED(CONFIG_BUILTIN_DTB)
	unflatten_and_copy_device_tree();
#else
	if (early_init_dt_verify(__va(dtb_early_pa)))
		unflatten_device_tree();
	else
		pr_err("No DTB found in kernel mappings\n");
#endif

	if (IS_ENABLED(CONFIG_RISCV_SBI))
		sbi_init();

	if (IS_ENABLED(CONFIG_STRICT_KERNEL_RWX))
		protect_kernel_text_data();
#ifdef CONFIG_SWIOTLB
	swiotlb_init(1);
#endif

#ifdef CONFIG_KASAN
	kasan_init();
#endif

#ifdef CONFIG_SMP
	setup_smp();
#endif

	riscv_fill_hwcap();
}

static int __init topology_init(void)
{
	int i;

	for_each_possible_cpu(i) {
		struct cpu *cpu = &per_cpu(cpu_devices, i);

		cpu->hotpluggable = cpu_has_hotplug(i);
		register_cpu(cpu, i);
	}

	return 0;
}
subsys_initcall(topology_init);

void free_initmem(void)
{
	unsigned long init_begin = (unsigned long)__init_begin;
	unsigned long init_end = (unsigned long)__init_end;

	set_memory_rw_nx(init_begin, (init_end - init_begin) >> PAGE_SHIFT);
	free_initmem_default(POISON_FREE_INITMEM);
}
