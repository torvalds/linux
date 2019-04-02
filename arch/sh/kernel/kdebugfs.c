// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/init.h>
#include <linux/defs.h>

struct dentry *arch_defs_dir;
EXPORT_SYMBOL(arch_defs_dir);

static int __init arch_kdefs_init(void)
{
	arch_defs_dir = defs_create_dir("sh", NULL);
	if (!arch_defs_dir)
		return -ENOMEM;

	return 0;
}
arch_initcall(arch_kdefs_init);
