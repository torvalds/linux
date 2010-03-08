/*
 * V9FS FID Management
 *
 *  Copyright (C) 2007 by Latchesar Ionkov <lucho@ionkov.net>
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
#include <net/9p/9p.h>
#include <net/9p/client.h>

#include "v9fs.h"
#include "v9fs_vfs.h"
#include "fid.h"

/**
 * v9fs_fid_add - add a fid to a dentry
 * @dentry: dentry that the fid is being added to
 * @fid: fid to add
 *
 */

int v9fs_fid_add(struct dentry *dentry, struct p9_fid *fid)
{
	struct v9fs_dentry *dent;

	P9_DPRINTK(P9_DEBUG_VFS, "fid %d dentry %s\n",
					fid->fid, dentry->d_name.name);

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
 * v9fs_fid_find - retrieve a fid that belongs to the specified uid
 * @dentry: dentry to look for fid in
 * @uid: return fid that belongs to the specified user
 * @any: if non-zero, return any fid associated with the dentry
 *
 */

static struct p9_fid *v9fs_fid_find(struct dentry *dentry, u32 uid, int any)
{
	struct v9fs_dentry *dent;
	struct p9_fid *fid, *ret;

	P9_DPRINTK(P9_DEBUG_VFS, " dentry: %s (%p) uid %d any %d\n",
		dentry->d_name.name, dentry, uid, any);
	dent = (struct v9fs_dentry *) dentry->d_fsdata;
	ret = NULL;
	if (dent) {
		spin_lock(&dent->lock);
		list_for_each_entry(fid, &dent->fidlist, dlist) {
			if (any || fid->uid == uid) {
				ret = fid;
				break;
			}
		}
		spin_unlock(&dent->lock);
	}

	return ret;
}

/**
 * v9fs_fid_lookup - lookup for a fid, try to walk if not found
 * @dentry: dentry to look for fid in
 *
 * Look for a fid in the specified dentry for the current user.
 * If no fid is found, try to create one walking from a fid from the parent
 * dentry (if it has one), or the root dentry. If the user haven't accessed
 * the fs yet, attach now and walk from the root.
 */

struct p9_fid *v9fs_fid_lookup(struct dentry *dentry)
{
	int i, n, l, clone, any, access;
	u32 uid;
	struct p9_fid *fid;
	struct dentry *d, *ds;
	struct v9fs_session_info *v9ses;
	char **wnames, *uname;

	v9ses = v9fs_inode2v9ses(dentry->d_inode);
	access = v9ses->flags & V9FS_ACCESS_MASK;
	switch (access) {
	case V9FS_ACCESS_SINGLE:
	case V9FS_ACCESS_USER:
		uid = current_fsuid();
		any = 0;
		break;

	case V9FS_ACCESS_ANY:
		uid = v9ses->uid;
		any = 1;
		break;

	default:
		uid = ~0;
		any = 0;
		break;
	}

	fid = v9fs_fid_find(dentry, uid, any);
	if (fid)
		return fid;

	ds = dentry->d_parent;
	fid = v9fs_fid_find(ds, uid, any);
	if (!fid) { /* walk from the root */
		n = 0;
		for (ds = dentry; !IS_ROOT(ds); ds = ds->d_parent)
			n++;

		fid = v9fs_fid_find(ds, uid, any);
		if (!fid) { /* the user is not attached to the fs yet */
			if (access == V9FS_ACCESS_SINGLE)
				return ERR_PTR(-EPERM);

			if (v9fs_proto_dotu(v9ses))
				uname = NULL;
			else
				uname = v9ses->uname;

			fid = p9_client_attach(v9ses->clnt, NULL, uname, uid,
				v9ses->aname);

			if (IS_ERR(fid))
				return fid;

			v9fs_fid_add(ds, fid);
		}
	} else /* walk from the parent */
		n = 1;

	if (ds == dentry)
		return fid;

	wnames = kmalloc(sizeof(char *) * n, GFP_KERNEL);
	if (!wnames)
		return ERR_PTR(-ENOMEM);

	for (d = dentry, i = (n-1); i >= 0; i--, d = d->d_parent)
		wnames[i] = (char *) d->d_name.name;

	clone = 1;
	i = 0;
	while (i < n) {
		l = min(n - i, P9_MAXWELEM);
		fid = p9_client_walk(fid, l, &wnames[i], clone);
		if (IS_ERR(fid)) {
			kfree(wnames);
			return fid;
		}

		i += l;
		clone = 0;
	}

	kfree(wnames);
	v9fs_fid_add(dentry, fid);
	return fid;
}

struct p9_fid *v9fs_fid_clone(struct dentry *dentry)
{
	struct p9_fid *fid, *ret;

	fid = v9fs_fid_lookup(dentry);
	if (IS_ERR(fid))
		return fid;

	ret = p9_client_walk(fid, 0, NULL, 1);
	return ret;
}
