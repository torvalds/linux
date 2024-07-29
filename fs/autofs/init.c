// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 */

#include <linux/module.h>
#include <linux/init.h>
#include "autofs_i.h"

struct file_system_type autofs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "autofs",
	.init_fs_context = autofs_init_fs_context,
	.parameters	= autofs_param_specs,
	.kill_sb	= autofs_kill_sb,
};
MODULE_ALIAS_FS("autofs");
MODULE_ALIAS("autofs");

static int __init init_autofs_fs(void)
{
	int err;

	autofs_dev_ioctl_init();

	err = register_filesystem(&autofs_fs_type);
	if (err)
		autofs_dev_ioctl_exit();

	return err;
}

static void __exit exit_autofs_fs(void)
{
	autofs_dev_ioctl_exit();
	unregister_filesystem(&autofs_fs_type);
}

module_init(init_autofs_fs)
module_exit(exit_autofs_fs)
MODULE_DESCRIPTION("Kernel automounter support");
MODULE_LICENSE("GPL");
