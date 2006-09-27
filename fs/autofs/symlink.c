/* -*- linux-c -*- --------------------------------------------------------- *
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

/* Nothing to release.. */
static void *autofs_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	char *s=((struct autofs_symlink *)dentry->d_inode->i_private)->data;
	nd_set_link(nd, s);
	return NULL;
}

struct inode_operations autofs_symlink_inode_operations = {
	.readlink	= generic_readlink,
	.follow_link	= autofs_follow_link
};
