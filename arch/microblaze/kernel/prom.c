/*
 * Procedures for creating, accessing and interpreting the device tree.
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996-2005 Paul Mackerras.
 *
 *  Adapted for 64bit PowerPC by Dave Engebretsen and Peter Bergner.
 *    {engebret|bergner}@us.ibm.com
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/threads.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/stringify.h>
#include <linux/delay.h>
#include <linux/initrd.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/kexec.h>
#include <linux/debugfs.h>
#include <linux/irq.h>
#include <linux/lmb.h>

#include <asm/prom.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <linux/io.h>
#include <asm/system.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/sections.h>
#include <asm/pci-bridge.h>

void __init early_init_dt_scan_chosen_arch(unsigned long node)
{
	/* No Microblaze specific code here */
}

void __init early_init_dt_add_memory_arch(u64 base, u64 size)
{
	lmb_add(base, size);
}

u64 __init early_init_dt_alloc_memory_arch(u64 size, u64 align)
{
	return lmb_alloc(size, align);
}

#ifdef CONFIG_EARLY_PRINTK
/* MS this is Microblaze specifig function */
static int __init early_init_dt_scan_serial(unsigned long node,
				const char *uname, int depth, void *data)
{
	unsigned long l;
	char *p;
	int *addr;

	pr_debug("search \"chosen\", depth: %d, uname: %s\n", depth, uname);

/* find all serial nodes */
	if (strncmp(uname, "serial", 6) != 0)
		return 0;

	early_init_dt_check_for_initrd(node);

/* find compatible node with uartlite */
	p = of_get_flat_dt_prop(node, "compatible", &l);
	if ((strncmp(p, "xlnx,xps-uartlite", 17) != 0) &&
			(strncmp(p, "xlnx,opb-uartlite", 17) != 0))
		return 0;

	addr = of_get_flat_dt_prop(node, "reg", &l);
	return *addr; /* return address */
}

/* this function is looking for early uartlite console - Microblaze specific */
int __init early_uartlite_console(void)
{
	return of_scan_flat_dt(early_init_dt_scan_serial, NULL);
}
#endif

void __init early_init_devtree(void *params)
{
	pr_debug(" -> early_init_devtree(%p)\n", params);

	/* Setup flat device-tree pointer */
	initial_boot_params = params;

	/* Retrieve various informations from the /chosen node of the
	 * device-tree, including the platform type, initrd location and
	 * size, TCE reserve, and more ...
	 */
	of_scan_flat_dt(early_init_dt_scan_chosen, NULL);

	/* Scan memory nodes and rebuild LMBs */
	lmb_init();
	of_scan_flat_dt(early_init_dt_scan_root, NULL);
	of_scan_flat_dt(early_init_dt_scan_memory, NULL);

	/* Save command line for /proc/cmdline and then parse parameters */
	strlcpy(boot_command_line, cmd_line, COMMAND_LINE_SIZE);
	parse_early_param();

	lmb_analyze();

	pr_debug("Phys. mem: %lx\n", (unsigned long) lmb_phys_mem_size());

	pr_debug(" <- early_init_devtree()\n");
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

/*******
 *
 * New implementation of the OF "find" APIs, return a refcounted
 * object, call of_node_put() when done.  The device tree and list
 * are protected by a rw_lock.
 *
 * Note that property management will need some locking as well,
 * this isn't dealt with yet.
 *
 *******/

#if defined(CONFIG_DEBUG_FS) && defined(DEBUG)
static struct debugfs_blob_wrapper flat_dt_blob;

static int __init export_flat_device_tree(void)
{
	struct dentry *d;

	flat_dt_blob.data = initial_boot_params;
	flat_dt_blob.size = initial_boot_params->totalsize;

	d = debugfs_create_blob("flat-device-tree", S_IFREG | S_IRUSR,
				of_debugfs_root, &flat_dt_blob);
	if (!d)
		return 1;

	return 0;
}
device_initcall(export_flat_device_tree);
#endif
