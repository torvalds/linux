// SPDX-License-Identifier: GPL-2.0-only
/*
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
DEFINE_SHOW_ATTRIBUTE(bootrom);

static int __init bootrom_setup(void)
{
	debugfs_create_file("bootrom", 0444, NULL, NULL, &bootrom_fops);
	return 0;
}

postcore_initcall(bootrom_setup);
