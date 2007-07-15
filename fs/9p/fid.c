/*
 * V9FS FID Management
 *
 *  Copyright (C) 2005, 2006 by Eric Van Hensbergen <ericvh@gmail.com>
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
#include <linux/sched.h>
#include <linux/idr.h>
#include <asm/semaphore.h>
#include <net/9p/9p.h>
#include <net/9p/client.h>

#include "v9fs.h"
#include "v9fs_vfs.h"
#include "fid.h"

/**
 * v9fs_fid_insert - add a fid to a dentry
 * @fid: fid to add
 * @dentry: dentry that it is being added to
 *
 */

int v9fs_fid_add(struct dentry *dentry, struct p9_fid *fid)
{
	struct v9fs_dentry *dent;

	P9_DPRINTK(P9_DEBUG_VFS, "fid %d dentry %s\n",
					fid->fid, dentry->d_iname);

	dent = dentry->d_fsdata;
	if (!dent) {
		dent = kmalloc(sizeof(struct v9fs_dentry), GFP_KERNEL);
		if (!dent)
			return -ENOMEM;

		spin_lock_init(&dent->lock);
		INIT_LIST_HEAD(&dent->fidlist);
		dentry->d_fsdata = dent;
	}

	spin_lock(&dent->lock);
	list_add(&fid->dlist, &dent->fidlist);
	spin_unlock(&dent->lock);

	return 0;
}

/**
 * v9fs_fid_lookup - return a locked fid from a dentry
 * @dentry: dentry to look for fid in
 *
 * find a fid in the dentry, obtain its semaphore and return a reference to it.
 * code calling lookup is responsible for releasing lock
 *
 * TODO: only match fids that have the same uid as current user
 *
 */

struct p9_fid *v9fs_fid_lookup(struct dentry *dentry)
{
	struct v9fs_dentry *dent;
	struct p9_fid *fid;

	P9_DPRINTK(P9_DEBUG_VFS, " dentry: %s (%p)\n", dentry->d_iname, dentry);
	dent = dentry->d_fsdata;
	if (dent)
		fid = list_entry(dent->fidlist.next, struct p9_fid, dlist);
	else
		fid = ERR_PTR(-EBADF);

	P9_DPRINTK(P9_DEBUG_VFS, " fid: %p\n", fid);
	return fid;
}

struct p9_fid *v9fs_fid_lookup_remove(struct dentry *dentry)
{
	struct p9_fid *fid;
	struct v9fs_dentry *dent;

	dent = dentry->d_fsdata;
	fid = v9fs_fid_lookup(dentry);
	if (!IS_ERR(fid)) {
		spin_lock(&dent->lock);
		list_del(&fid->dlist);
		spin_unlock(&dent->lock);
	}

	return fid;
}


/**
 * v9fs_fid_clone - lookup the fid for a dentry, clone a private copy and
 * 	release it
 * @dentry: dentry to look for fid in
 *
 * find a fid in the dentry and then clone to a new private fid
 *
 * TODO: only match fids that have the same uid as current user
 *
 */

struct p9_fid *v9fs_fid_clone(struct dentry *dentry)
{
	struct p9_fid *ofid, *fid;

	P9_DPRINTK(P9_DEBUG_VFS, " dentry: %s (%p)\n", dentry->d_iname, dentry);
	ofid = v9fs_fid_lookup(dentry);
	if (IS_ERR(ofid))
		return ofid;

	fid = p9_client_walk(ofid, 0, NULL, 1);
	return fid;
}
