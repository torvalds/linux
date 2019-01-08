/*
 * Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 */

#include <linux/module.h>
#include <linux/init.h>
#include "autofs_i.h"

static struct dentry *autofs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_nodev(fs_type, flags, data, autofs_fill_super);
}

struct file_system_type autofs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "autofs",
	.mount		= autofs_mount,
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
MODULE_LICENSE("GPL");
