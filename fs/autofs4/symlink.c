/* -*- c -*- --------------------------------------------------------------- *
 *
 * linux/fs/autofs/symlink.c
 *
 *  Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include "autofs_i.h"

static void *autofs4_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	nd_set_link(nd, (char *)ino->u.symlink);
	return NULL;
}

const struct inode_operations autofs4_symlink_inode_operations = {
	.readlink	= generic_readlink,
	.follow_link	= autofs4_follow_link
};
