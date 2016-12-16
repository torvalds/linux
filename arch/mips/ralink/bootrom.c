/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Copyright (C) 2013 John Crispin <john@phrozen.org>
 */

#include <linux/debugfs.h>
#include <linux/seq_file.h>

#define BOOTROM_OFFSET	0x10118000
#define BOOTROM_SIZE	0x8000

static void __iomem *membase = (void __iomem *) KSEG1ADDR(BOOTROM_OFFSET);

static int bootrom_show(struct seq_file *s, void *unused)
{
	seq_write(s, membase, BOOTROM_SIZE);

	return 0;
}

static int bootrom_open(struct inode *inode, struct file *file)
{
	return single_open(file, bootrom_show, NULL);
}

static const struct file_operations bootrom_file_ops = {
	.open		= bootrom_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int bootrom_setup(void)
{
	if (!debugfs_create_file("bootrom", 0444,
			NULL, NULL, &bootrom_file_ops)) {
		pr_err("Failed to create bootrom debugfs file\n");

		return -EINVAL;
	}

	return 0;
}

postcore_initcall(bootrom_setup);
