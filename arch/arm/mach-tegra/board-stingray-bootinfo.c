/*
 * bootinfo.c: This code provides boot information via /proc/bootinfo.
 * The information currently includes:
 *	- the powerup reason
 *	- the hardware revision
 * All new user-space consumers of the powerup reason should use
 * the /proc/bootinfo interface; all kernel-space consumers of the
 * powerup reason should use the stingray_powerup_reason interface.
 *
 * Copyright (C) 2009 Motorola, Inc.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Revision History:
 *
 * Date		 Author	Comment
 * ----------   --------  -----------
 * 30/06/2009   Motorola  Initialize version
 * 25/10/2010   Motorola  Modified for stingray
 * 30/10/2010   Motorola  Converted to seq_file interface
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <asm/setup.h>
#include "board-stingray.h"

static int bootinfo_show(struct seq_file *m, void *v)
{
	seq_printf(m, "POWERUPREASON : 0x%08x\n",
		stingray_powerup_reason());

	seq_printf(m, "BOARDREVISION : 0x%08x\n",
		stingray_revision());

	return 0;
}

static int bootinfo_open(struct inode *inode, struct file *file)
{
	return single_open(file, bootinfo_show, NULL);
}

static const struct file_operations bootinfo_operations = {
	.open		= bootinfo_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int __init bootinfo_init(void)
{
	struct proc_dir_entry *pe;

	pe = proc_create("bootinfo", S_IRUGO, NULL, &bootinfo_operations);
	if (!pe)
		return -ENOMEM;

	return 0;
}

device_initcall(bootinfo_init);
