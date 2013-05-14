/*
 * Device tree support
 *
 * Copyright (C) 2013 Altera Corporation
 * Copyright (C) 2010 Thomas Chou <thomas@wytron.com.tw>
 *
 * Based on MIPS support for CONFIG_OF device tree support
 *
 * Copyright (C) 2010 Cisco Systems Inc. <dediao@cisco.com>
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/init.h>
#include <linux/export.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/bootmem.h>
#include <linux/initrd.h>
#include <linux/debugfs.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/io.h>

#include <asm/page.h>
#include <asm/prom.h>
#include <asm/sections.h>
#include <asm/setup.h>

void __init early_init_dt_add_memory_arch(u64 base, u64 size)
{
	u64 kernel_start = (u64)virt_to_phys(_text);

	if (!memory_size &&
	    (kernel_start >= base) && (kernel_start < (base + size)))
		memory_size = size;

	return;
}

void * __init early_init_dt_alloc_memory_arch(u64 size, u64 align)
{
	return __alloc_bootmem(size, align, __pa(MAX_DMA_ADDRESS));
}

#ifdef CONFIG_BLK_DEV_INITRD
void __init early_init_dt_setup_initrd_arch(unsigned long start,
					    unsigned long end)
{
	initrd_start = (unsigned long)__va(start);
	initrd_end = (unsigned long)__va(end);
	initrd_below_start_ok = 1;
}
#endif

void __init early_init_devtree(void *params)
{
	if (params && be32_to_cpup((__be32 *)params) == OF_DT_HEADER)
		initial_boot_params = params;
#if defined(CONFIG_NIOS2_DTB_AT_PHYS_ADDR)
	else if (be32_to_cpup((__be32 *)CONFIG_NIOS2_DTB_PHYS_ADDR) ==
		 OF_DT_HEADER)
		initial_boot_params = (void *)CONFIG_NIOS2_DTB_PHYS_ADDR;
#endif
	else if (be32_to_cpu((__be32)__dtb_start) == OF_DT_HEADER)
		initial_boot_params = (void *)&__dtb_start;
	else
		return;

	/* Retrieve various informations from the /chosen node of the
	 * device-tree, including the platform type, initrd location and
	 * size, and more ...
	 */
	of_scan_flat_dt(early_init_dt_scan_chosen, cmd_line);

	/* Scan memory nodes */
	of_scan_flat_dt(early_init_dt_scan_root, NULL);
	of_scan_flat_dt(early_init_dt_scan_memory, NULL);
}

void __init device_tree_init(void)
{
	unsigned long base, size;

	if (!initial_boot_params)
		return;

	base = virt_to_phys((void *)initial_boot_params);
	size = be32_to_cpu(initial_boot_params->totalsize);

	/*
	 * If the chosen DTB is not the built-in one (passed via
	 * bootloader or found at a built-in physical address) and
	 * it is within main memory above the kernel binary itself
	 * (> memory_start), we need to reserve_bootmem().
	 */
	if ((base >= memory_start) && (base < memory_end)) {
		reserve_bootmem(base, size, BOOTMEM_DEFAULT);
		unflatten_device_tree();
		free_bootmem(base, size);
	} else
		unflatten_device_tree();
}

#ifdef CONFIG_EARLY_PRINTK
static int __init early_init_dt_scan_serial(unsigned long node,
			const char *uname, int depth, void *data)
{
	u64 *addr64 = (u64 *) data;
	char *p;

	/* only consider serial nodes */
	if (strncmp(uname, "serial", 6) != 0)
		return 0;

	p = of_get_flat_dt_prop(node, "compatible", NULL);
	if (!p)
		return 0;

	/*
	 * We found an altera_jtaguart but it wasn't configured for console, so
	 * skip it.
	 */
#ifndef CONFIG_SERIAL_ALTERA_JTAGUART_CONSOLE
	if (strncmp(p, "ALTR,juart", 10) == 0)
		return 0;
#endif

	/*
	 * Same for altera_uart.
	 */
#ifndef CONFIG_SERIAL_ALTERA_UART_CONSOLE
	if (strncmp(p, "ALTR,uart", 9) == 0)
		return 0;
#endif

	*addr64 = of_get_flat_dt_translate_address(node);
	return *addr64 == OF_BAD_ADDR ? 0 : 1;
}

unsigned long __init early_altera_uart_or_juart_console(void)
{
	u64 base = 0;

	if (of_scan_flat_dt(early_init_dt_scan_serial, &base))
		return (unsigned long)(base + CONFIG_IO_REGION_BASE);
	else
		return 0;
}
#endif /* CONFIG_EARLY_PRINTK */
