// SPDX-License-Identifier: GPL-2.0
/*
 * Greybus defs code
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 */

#include <linux/defs.h>

#include "greybus.h"

static struct dentry *gb_de_root;

void __init gb_defs_init(void)
{
	gb_de_root = defs_create_dir("greybus", NULL);
}

void gb_defs_cleanup(void)
{
	defs_remove_recursive(gb_de_root);
	gb_de_root = NULL;
}

struct dentry *gb_defs_get(void)
{
	return gb_de_root;
}
EXPORT_SYMBOL_GPL(gb_defs_get);
