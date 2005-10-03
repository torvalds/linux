/*
 *  linux/fs/9p/vfs_dentry.c
 *
 * This file contians vfs dentry ops for the 9P2000 protocol.
 *
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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
#include "conv.h"
#include "fid.h"

/**
 * v9fs_dentry_validate - VFS dcache hook to validate cache
 * @dentry:  dentry that is being validated
 * @nd: path data
 *
 * dcache really shouldn't be used for 9P2000 as at all due to
 * potential attached semantics to directory traversal (walk).
 *
 * FUTURE: look into how to use dcache to allow multi-stage
 * walks in Plan 9 & potential for better dcache operation which
 * would remain valid for Plan 9 semantics.  Older versions
 * had validation via stat for those interested.  However, since
 * stat has the same approximate overhead as walk there really
 * is no difference.  The only improvement would be from a
 * time-decay cache like NFS has and that undermines the
 * synchronous nature of 9P2000.
 *
 */

static int v9fs_dentry_validate(struct dentry *dentry, struct nameidata *nd)
{
	struct dentry *dc = current->fs->pwd;

	dprintk(DEBUG_VFS, "dentry: %s (%p)\n", dentry->d_iname, dentry);
	if (v9fs_fid_lookup(dentry)) {
		dprintk(DEBUG_VFS, "VALID\n");
		return 1;
	}

	while (dc != NULL) {
		if (dc == dentry) {
			dprintk(DEBUG_VFS, "VALID\n");
			return 1;
		}
		if (dc == dc->d_parent)
			break;

		dc = dc->d_parent;
	}

	dprintk(DEBUG_VFS, "INVALID\n");
	return 0;
}

/**
 * v9fs_dentry_release - called when dentry is going to be freed
 * @dentry:  dentry that is being release
 *
 */

void v9fs_dentry_release(struct dentry *dentry)
{
	dprintk(DEBUG_VFS, " dentry: %s (%p)\n", dentry->d_iname, dentry);

	if (dentry->d_fsdata != NULL) {
		struct list_head *fid_list = dentry->d_fsdata;
		struct v9fs_fid *temp = NULL;
		struct v9fs_fid *current_fid = NULL;
		struct v9fs_fcall *fcall = NULL;

		list_for_each_entry_safe(current_fid, temp, fid_list, list) {
			if (v9fs_t_clunk
			    (current_fid->v9ses, current_fid->fid, &fcall))
				dprintk(DEBUG_ERROR, "clunk failed: %s\n",
					FCALL_ERROR(fcall));

			v9fs_put_idpool(current_fid->fid,
					&current_fid->v9ses->fidpool);

			kfree(fcall);
			v9fs_fid_destroy(current_fid);
		}

		kfree(dentry->d_fsdata);	/* free the list_head */
	}
}

struct dentry_operations v9fs_dentry_operations = {
	.d_revalidate = v9fs_dentry_validate,
	.d_release = v9fs_dentry_release,
};
