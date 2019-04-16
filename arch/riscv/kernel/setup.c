/*
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Chen Liqin <liqin.chen@sunplusct.com>
 *  Lennox Wu <lennox.wu@sunplusct.com>
 * Copyright (C) 2012 Regents of the University of California
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
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

#include <asm/setup.h>
#include <asm/sections.h>
#include <asm/pgtable.h>
#include <asm/smp.h>
#include <asm/tlbflush.h>
#include <asm/thread_info.h>

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

unsigned long va_pa_offset;
EXPORT_SYMBOL(va_pa_offset);
unsigned long pfn_base;
EXPORT_SYMBOL(pfn_base);

unsigned long empty_zero_page[PAGE_SIZE / sizeof(unsigned long)] __page_aligned_bss;
EXPORT_SYMBOL(empty_zero_page);

/* The lucky hart to first increment this variable will boot the other cores */
atomic_t hart_lottery;
unsigned long boot_cpu_hartid;

void __init parse_dtb(unsigned int hartid, void *dtb)
{
	if (early_init_dt_scan(__va(dtb)))
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
	unflatten_device_tree();

#ifdef CONFIG_SWIOTLB
	swiotlb_init(1);
#endif

#ifdef CONFIG_SMP
	setup_smp();
#endif

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	riscv_fill_hwcap();
}
