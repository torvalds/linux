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

struct p9_fid *v9fs_fid_lookup(struct dentry *dentry);
static inline struct p9_fid *v9fs_parent_fid(struct dentry *dentry)
{
	return v9fs_fid_lookup(dentry->d_parent);
}
void v9fs_fid_add(struct dentry *dentry, struct p9_fid *fid);
struct p9_fid *v9fs_writeback_fid(struct dentry *dentry);
static inline struct p9_fid *clone_fid(struct p9_fid *fid)
{
	return IS_ERR(fid) ? fid :  p9_client_walk(fid, 0, NULL, 1);
}
static inline struct p9_fid *v9fs_fid_clone(struct dentry *dentry)
{
	return clone_fid(v9fs_fid_lookup(dentry));
}
#endif
