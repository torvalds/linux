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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/idr.h>

#include "debug.h"
#include "v9fs.h"
#include "9p.h"
#include "v9fs_vfs.h"
#include "fid.h"

/**
 * v9fs_fid_insert - add a fid to a dentry
 * @fid: fid to add
 * @dentry: dentry that it is being added to
 *
 */

int v9fs_fid_insert(struct v9fs_fid *fid, struct dentry *dentry)
{
	struct list_head *fid_list = (struct list_head *)dentry->d_fsdata;
	dprintk(DEBUG_9P, "fid %d (%p) dentry %s (%p)\n", fid->fid, fid,
		dentry->d_iname, dentry);
	if (dentry->d_fsdata == NULL) {
		dentry->d_fsdata =
		    kmalloc(sizeof(struct list_head), GFP_KERNEL);
		if (dentry->d_fsdata == NULL) {
			dprintk(DEBUG_ERROR, "Out of memory\n");
			return -ENOMEM;
		}
		fid_list = (struct list_head *)dentry->d_fsdata;
		INIT_LIST_HEAD(fid_list);	/* Initialize list head */
	}

	fid->uid = current->uid;
	list_add(&fid->list, fid_list);
	return 0;
}

/**
 * v9fs_fid_create - allocate a FID structure
 * @dentry - dentry to link newly created fid to
 *
 */

struct v9fs_fid *v9fs_fid_create(struct v9fs_session_info *v9ses, int fid)
{
	struct v9fs_fid *new;

	dprintk(DEBUG_9P, "fid create fid %d\n", fid);
	new = kmalloc(sizeof(struct v9fs_fid), GFP_KERNEL);
	if (new == NULL) {
		dprintk(DEBUG_ERROR, "Out of Memory\n");
		return ERR_PTR(-ENOMEM);
	}

	new->fid = fid;
	new->v9ses = v9ses;
	new->fidopen = 0;
	new->fidclunked = 0;
	new->iounit = 0;
	new->rdir_pos = 0;
	new->rdir_fcall = NULL;
	INIT_LIST_HEAD(&new->list);

	return new;
}

/**
 * v9fs_fid_destroy - deallocate a FID structure
 * @fid: fid to destroy
 *
 */

void v9fs_fid_destroy(struct v9fs_fid *fid)
{
	list_del(&fid->list);
	kfree(fid);
}

/**
 * v9fs_fid_lookup - retrieve the right fid from a  particular dentry
 * @dentry: dentry to look for fid in
 * @type: intent of lookup (operation or traversal)
 *
 * find a fid in the dentry
 *
 * TODO: only match fids that have the same uid as current user
 *
 */

struct v9fs_fid *v9fs_fid_lookup(struct dentry *dentry)
{
	struct list_head *fid_list = (struct list_head *)dentry->d_fsdata;
	struct v9fs_fid *return_fid = NULL;

	dprintk(DEBUG_9P, " dentry: %s (%p)\n", dentry->d_iname, dentry);

	if (fid_list)
		return_fid = list_entry(fid_list->next, struct v9fs_fid, list);

	if (!return_fid) {
		dprintk(DEBUG_ERROR, "Couldn't find a fid in dentry\n");
	}

	return return_fid;
}
