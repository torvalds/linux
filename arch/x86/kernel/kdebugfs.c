/*
 * Architecture specific debugfs files
 *
 * Copyright (C) 2007, Intel Corp.
 *	Huang Ying <ying.huang@intel.com>
 *
 * This file is released under the GPLv2.
 */

#include <linux/debugfs.h>
#include <linux/stat.h>
#include <linux/init.h>

#include <asm/setup.h>

#ifdef CONFIG_DEBUG_BOOT_PARAMS
static struct debugfs_blob_wrapper boot_params_blob = {
	.data = &boot_params,
	.size = sizeof(boot_params),
};

static int __init boot_params_kdebugfs_init(void)
{
	int error;
	struct dentry *dbp, *version, *data;

	dbp = debugfs_create_dir("boot_params", NULL);
	if (!dbp) {
		error = -ENOMEM;
		goto err_return;
	}
	version = debugfs_create_x16("version", S_IRUGO, dbp,
				     &boot_params.hdr.version);
	if (!version) {
		error = -ENOMEM;
		goto err_dir;
	}
	data = debugfs_create_blob("data", S_IRUGO, dbp,
				   &boot_params_blob);
	if (!data) {
		error = -ENOMEM;
		goto err_version;
	}
	return 0;
err_version:
	debugfs_remove(version);
err_dir:
	debugfs_remove(dbp);
err_return:
	return error;
}
#endif

static int __init arch_kdebugfs_init(void)
{
	int error = 0;

#ifdef CONFIG_DEBUG_BOOT_PARAMS
	error = boot_params_kdebugfs_init();
#endif

	return error;
}

arch_initcall(arch_kdebugfs_init);
