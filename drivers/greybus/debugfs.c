// SPDX-License-Identifier: GPL-2.0
/*
 * Greybus debugfs code
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 */

#include <linux/debugfs.h>
#include <linux/greybus.h>

static struct dentry *gb_debug_root;

void __init gb_debugfs_init(void)
{
	gb_debug_root = debugfs_create_dir("greybus", NULL);
}

void gb_debugfs_cleanup(void)
{
	debugfs_remove_recursive(gb_debug_root);
	gb_debug_root = NULL;
}

struct dentry *gb_debugfs_get(void)
{
	return gb_debug_root;
}
EXPORT_SYMBOL_GPL(gb_debugfs_get);
