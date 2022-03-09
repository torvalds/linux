// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2022 IBM Corp.

#include <linux/debugfs.h>
#include <linux/export.h>
#include <linux/init.h>

struct dentry *arch_debugfs_dir;
EXPORT_SYMBOL(arch_debugfs_dir);

static int __init aspeed_debugfs_init(void)
{
	arch_debugfs_dir = debugfs_create_dir("aspeed", NULL);
	return 0;
}
arch_initcall(aspeed_debugfs_init);
