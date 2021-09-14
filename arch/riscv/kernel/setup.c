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
#include <linux/smp.h>
#include <linux/efi.h>
#include <linux/crash_dump.h>

#include <asm/cpu_ops.h>
#include <asm/early_ioremap.h>
#include <asm/pgtable.h>
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
atomic_t hart_lottery __section(".sdata")
#ifdef CONFIG_XIP_KERNEL
= ATOMIC_INIT(0xC001BEEF)
#endif
;
unsigned long boot_cpu_hartid;
static DEFINE_PER_CPU(struct cpu, cpu_devices);

/*
 * Place kernel memory regions on the resource tree so that
 * kexec-tools can retrieve them from /proc/iomem. While there
 * also add "System RAM" regions for compatibility with other
 * archs, and the rest of the known regions for completeness.
 */
static struct resource kimage_res = { .name = "Kernel image", };
static struct resource code_res = { .name = "Kernel code", };
static struct resource data_res = { .name = "Kernel data", };
static struct resource rodata_res = { .name = "Kernel rodata", };
static struct resource bss_res = { .name = "Kernel bss", };
#ifdef CONFIG_CRASH_DUMP
static struct resource elfcorehdr_res = { .name = "ELF Core hdr", };
#endif

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

static int __init add_kernel_resources(void)
{
	int ret = 0;

	/*
	 * The memory region of the kernel image is continuous and
	 * was reserved on setup_bootmem, register it here as a
	 * resource, with the various segments of the image as
	 * child nodes.
	 */

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

	kimage_res.start = code_res.start;
	kimage_res.end = bss_res.end;
	kimage_res.flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;

	ret = add_resource(&iomem_resource, &kimage_res);
	if (ret < 0)
		return ret;

	ret = add_resource(&kimage_res, &code_res);
	if (ret < 0)
		return ret;

	ret = add_resource(&kimage_res, &rodata_res);
	if (ret < 0)
		return ret;

	ret = add_resource(&kimage_res, &data_res);
	if (ret < 0)
		return ret;

	ret = add_resource(&kimage_res, &bss_res);

	return ret;
}

static void __init init_resources(void)
{
	struct memblock_region *region = NULL;
	struct resource *res = NULL;
	struct resource *mem_res = NULL;
	size_t mem_res_sz = 0;
	int num_resources = 0, res_idx = 0;
	int ret = 0;

	/* + 1 as memblock_alloc() might increase memblock.reserved.cnt */
	num_resources = memblock.memory.cnt + memblock.reserved.cnt + 1;
	res_idx = num_resources - 1;

	mem_res_sz = num_resources * sizeof(*mem_res);
	mem_res = memblock_alloc(mem_res_sz, SMP_CACHE_BYTES);
	if (!mem_res)
		panic("%s: Failed to allocate %zu bytes\n", __func__, mem_res_sz);

	/*
	 * Start by adding the reserved regions, if they overlap
	 * with /memory regions, insert_resource later on will take
	 * care of it.
	 */
	ret = add_kernel_resources();
	if (ret < 0)
		goto error;

#ifdef CONFIG_KEXEC_CORE
	if (crashk_res.start != crashk_res.end) {
		ret = add_resource(&iomem_resource, &crashk_res);
		if (ret < 0)
			goto error;
	}
#endif

#ifdef CONFIG_CRASH_DUMP
	if (elfcorehdr_size > 0) {
		elfcorehdr_res.start = elfcorehdr_addr;
		elfcorehdr_res.end = elfcorehdr_addr + elfcorehdr_size - 1;
		elfcorehdr_res.flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;
		add_resource(&iomem_resource, &elfcorehdr_res);
	}
#endif

	for_each_reserved_mem_region(region) {
		res = &mem_res[res_idx--];

		res->name = "Reserved";
		res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;
		res->start = __pfn_to_phys(memblock_region_reserved_base_pfn(region));
		res->end = __pfn_to_phys(memblock_region_reserved_end_pfn(region)) - 1;

		/*
		 * Ignore any other reserved regions within
		 * system memory.
		 */
		if (memblock_is_memory(res->start)) {
			/* Re-use this pre-allocated resource */
			res_idx++;
			continue;
		}

		ret = add_resource(&iomem_resource, res);
		if (ret < 0)
			goto error;
	}

	/* Add /memory regions to the resource tree */
	for_each_mem_region(region) {
		res = &mem_res[res_idx--];

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

	/* Clean-up any unused pre-allocated resources */
	if (res_idx >= 0)
		memblock_free(__pa(mem_res), (res_idx + 1) * sizeof(*mem_res));
	return;

 error:
	/* Better an empty resource tree than an inconsistent one */
	release_child_resources(&iomem_resource);
	memblock_free(__pa(mem_res), mem_res_sz);
}


static void __init parse_dtb(void)
{
	/* Early scan of device tree from init memory */
	if (early_init_dt_scan(dtb_early_va)) {
		const char *name = of_flat_dt_get_machine_name();

		if (name) {
			pr_info("Machine model: %s\n", name);
			dump_stack_set_arch_desc("%s (DT)", name);
		}
		return;
	}

	pr_err("No DTB passed to the kernel\n");
#ifdef CONFIG_CMDLINE_FORCE
	strscpy(boot_command_line, CONFIG_CMDLINE, COMMAND_LINE_SIZE);
	pr_info("Forcing kernel command line to: %s\n", boot_command_line);
#endif
}

void __init setup_arch(char **cmdline_p)
{
	parse_dtb();
	setup_initial_init_mm(_stext, _etext, _edata, _end);

	*cmdline_p = boot_command_line;

	early_ioremap_setup();
	jump_label_init();
	parse_early_param();

	efi_init();
	paging_init();
#if IS_ENABLED(CONFIG_BUILTIN_DTB)
	unflatten_and_copy_device_tree();
#else
	if (early_init_dt_verify(__va(XIP_FIXUP(dtb_early_pa))))
		unflatten_device_tree();
	else
		pr_err("No DTB found in kernel mappings\n");
#endif
	misc_mem_init();

	init_resources();
	sbi_init();

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
	int i, ret;

	for_each_online_node(i)
		register_one_node(i);

	for_each_possible_cpu(i) {
		struct cpu *cpu = &per_cpu(cpu_devices, i);

		cpu->hotpluggable = cpu_has_hotplug(i);
		ret = register_cpu(cpu, i);
		if (unlikely(ret))
			pr_warn("Warning: %s: register_cpu %d failed (%d)\n",
			       __func__, i, ret);
	}

	return 0;
}
subsys_initcall(topology_init);

void free_initmem(void)
{
	if (IS_ENABLED(CONFIG_STRICT_KERNEL_RWX))
		set_kernel_memory(lm_alias(__init_begin), lm_alias(__init_end),
				  IS_ENABLED(CONFIG_64BIT) ?
					set_memory_rw : set_memory_rw_nx);

	free_initmem_default(POISON_FREE_INITMEM);
}
