/* -*- c -*- --------------------------------------------------------------- *
 *
 * linux/fs/autofs/init.c
 *
 *  Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include <linux/module.h>
#include <linux/init.h>
#include "autofs_i.h"

static int autofs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_nodev(fs_type, flags, data, autofs4_fill_super, mnt);
}

static struct file_system_type autofs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "autofs",
	.get_sb		= autofs_get_sb,
	.kill_sb	= autofs4_kill_sb,
};

static int __init init_autofs4_fs(void)
{
	return register_filesystem(&autofs_fs_type);
}

static void __exit exit_autofs4_fs(void)
{
	unregister_filesystem(&autofs_fs_type);
}

module_init(init_autofs4_fs) 
module_exit(exit_autofs4_fs)
MODULE_LICENSE("GPL");
