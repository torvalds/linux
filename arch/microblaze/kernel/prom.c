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

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/memblock.h>
#include <linux/of_fdt.h>

void __init early_init_devtree(void *params)
{
	pr_debug(" -> early_init_devtree(%p)\n", params);

	early_init_dt_scan(params);
	if (!strlen(boot_command_line))
		strlcpy(boot_command_line, cmd_line, COMMAND_LINE_SIZE);

	memblock_allow_resize();

	pr_debug("Phys. mem: %lx\n", (unsigned long) memblock_phys_mem_size());

	pr_debug(" <- early_init_devtree()\n");
}
