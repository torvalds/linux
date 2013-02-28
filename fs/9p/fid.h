/*
 * V9FS FID Management
 *
 *  Copyright (C) 2005 by Eric Van Hensbergen <ericvh@gmail.com>
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
#ifndef FS_9P_FID_H
#define FS_9P_FID_H
#include <linux/list.h>

/**
 * struct v9fs_dentry - 9p private data stored in dentry d_fsdata
 * @fidlist: list of FIDs currently associated with this dentry
 *
 * This structure defines the 9p private data associated with
 * a particular dentry.  In particular, this private data is used
 * to lookup which 9P FID handle should be used for a particular VFS
 * operation.  FID handles are associated with dentries instead of
 * inodes in order to more closely map functionality to the Plan 9
 * expected behavior for FID reclaimation and tracking.
 *
 * Protected by ->d_lock of dentry it belongs to.
 *
 * See Also: Mapping FIDs to Linux VFS model in
 * Design and Implementation of the Linux 9P File System documentation
 */
struct v9fs_dentry {
	struct list_head fidlist;
};

struct p9_fid *v9fs_fid_lookup(struct dentry *dentry);
struct p9_fid *v9fs_fid_clone(struct dentry *dentry);
int v9fs_fid_add(struct dentry *dentry, struct p9_fid *fid);
struct p9_fid *v9fs_writeback_fid(struct dentry *dentry);
#endif
