/*
 *  linux/fs/9p/vfs_dentry.c
 *
 * This file contians vfs dentry ops for the 9P2000 protocol.
 *
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to:
 *  Free Software Foundation
 *  51 Franklin Street, Fifth Floor
 *  Boston, MA  02111-1301  USA
 *
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/pagemap.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/smp_lock.h>
#include <linux/inet.h>
#include <linux/namei.h>
#include <linux/idr.h>

#include "debug.h"
#include "v9fs.h"
#include "9p.h"
#include "v9fs_vfs.h"
#include "fid.h"

/**
 * v9fs_dentry_delete - called when dentry refcount equals 0
 * @dentry:  dentry in question
 *
 * By returning 1 here we should remove cacheing of unused
 * dentry components.
 *
 */

static int v9fs_dentry_delete(struct dentry *dentry)
{
	dprintk(DEBUG_VFS, " dentry: %s (%p)\n", dentry->d_iname, dentry);
	return 1;
}

/**
 * v9fs_dentry_release - called when dentry is going to be freed
 * @dentry:  dentry that is being release
 *
 */

void v9fs_dentry_release(struct dentry *dentry)
{
	int err;

	dprintk(DEBUG_VFS, " dentry: %s (%p)\n", dentry->d_iname, dentry);

	if (dentry->d_fsdata != NULL) {
		struct list_head *fid_list = dentry->d_fsdata;
		struct v9fs_fid *temp = NULL;
		struct v9fs_fid *current_fid = NULL;

		list_for_each_entry_safe(current_fid, temp, fid_list, list) {
			err = v9fs_t_clunk(current_fid->v9ses, current_fid->fid);

			if (err < 0)
				dprintk(DEBUG_ERROR, "clunk failed: %d name %s\n",
					err, dentry->d_iname);

			v9fs_fid_destroy(current_fid);
		}

		kfree(dentry->d_fsdata);	/* free the list_head */
	}
}

struct dentry_operations v9fs_dentry_operations = {
	.d_delete = v9fs_dentry_delete,
	.d_release = v9fs_dentry_release,
};
