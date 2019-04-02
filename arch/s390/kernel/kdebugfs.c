// SPDX-License-Identifier: GPL-2.0
#include <linux/defs.h>
#include <linux/export.h>
#include <linux/init.h>

struct dentry *arch_defs_dir;
EXPORT_SYMBOL(arch_defs_dir);

static int __init arch_kdefs_init(void)
{
	arch_defs_dir = defs_create_dir("s390", NULL);
	return 0;
}
postcore_initcall(arch_kdefs_init);
