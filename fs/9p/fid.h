/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * V9FS FID Management
 *
 *  Copyright (C) 2005 by Eric Van Hensbergen <ericvh@gmail.com>
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
