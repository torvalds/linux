/*
 * Greybus debugfs code
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/debugfs.h>

#include "greybus.h"

static struct dentry *gb_debug_root;

int gb_debugfs_init(void)
{
	gb_debug_root = debugfs_create_dir("greybus", NULL);
	if (!gb_debug_root)
		return -ENOENT;

	return 0;
}

void gb_debugfs_cleanup(void)
{
	debugfs_remove_recursive(gb_debug_root);
}
