// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Chen Liqin <liqin.chen@sunplusct.com>
 *  Lennox Wu <lennox.wu@sunplusct.com>
 * Copyright (C) 2012 Regents of the University of California
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

#include <asm/clint.h>
#include <asm/cpu_ops.h>
#include <asm/setup.h>
#include <asm/sections.h>
#include <asm/sbi.h>
#include <asm/tlbflush.h>
#include <asm/thread_info.h>
#include <asm/kasan.h>

#include "head.h"

#ifdef CONFIG_DUMMY_CONSOLE
struct screen_info screen_info = {
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
atomic_t hart_lottery __section(.sdata);
unsigned long boot_cpu_hartid;
static DEFINE_PER_CPU(struct cpu, cpu_devices);

void __init parse_dtb(void)
{
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
	init_mm.start_code = (unsigned long) _stext;
	init_mm.end_code   = (unsigned long) _etext;
	init_mm.end_data   = (unsigned long) _edata;
	init_mm.brk        = (unsigned long) _end;

	*cmdline_p = boot_command_line;

	parse_early_param();

	setup_bootmem();
	paging_init();
#if IS_ENABLED(CONFIG_BUILTIN_DTB)
	unflatten_and_copy_device_tree();
#else
	unflatten_device_tree();
#endif
	clint_init_boot_cpu();

#ifdef CONFIG_SWIOTLB
	swiotlb_init(1);
#endif

#ifdef CONFIG_KASAN
	kasan_init();
#endif

#if IS_ENABLED(CONFIG_RISCV_SBI)
	sbi_init();
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
