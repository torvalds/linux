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
	init_MUTEX(&new->lock);
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
 * v9fs_fid_lookup - return a locked fid from a dentry
 * @dentry: dentry to look for fid in
 *
 * find a fid in the dentry, obtain its semaphore and return a reference to it.
 * code calling lookup is responsible for releasing lock
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
		return_fid = ERR_PTR(-EBADF);
	}

	if(down_interruptible(&return_fid->lock))
		return ERR_PTR(-EINTR);

	return return_fid;
}

/**
 * v9fs_fid_clone - lookup the fid for a dentry, clone a private copy and
 * 			release it
 * @dentry: dentry to look for fid in
 *
 * find a fid in the dentry and then clone to a new private fid
 *
 * TODO: only match fids that have the same uid as current user
 *
 */

struct v9fs_fid *v9fs_fid_clone(struct dentry *dentry)
{
	struct v9fs_session_info *v9ses = v9fs_inode2v9ses(dentry->d_inode);
	struct v9fs_fid *base_fid, *new_fid = ERR_PTR(-EBADF);
	struct v9fs_fcall *fcall = NULL;
	int fid, err;

	base_fid = v9fs_fid_lookup(dentry);

	if(IS_ERR(base_fid))
		return base_fid;

	if(base_fid) {  /* clone fid */
		fid = v9fs_get_idpool(&v9ses->fidpool);
		if (fid < 0) {
			eprintk(KERN_WARNING, "newfid fails!\n");
			new_fid = ERR_PTR(-ENOSPC);
			goto Release_Fid;
		}

		err = v9fs_t_walk(v9ses, base_fid->fid, fid, NULL, &fcall);
		if (err < 0) {
			dprintk(DEBUG_ERROR, "clone walk didn't work\n");
			v9fs_put_idpool(fid, &v9ses->fidpool);
			new_fid = ERR_PTR(err);
			goto Free_Fcall;
		}
		new_fid = v9fs_fid_create(v9ses, fid);
		if (new_fid == NULL) {
			dprintk(DEBUG_ERROR, "out of memory\n");
			new_fid = ERR_PTR(-ENOMEM);
		}
Free_Fcall:
		kfree(fcall);
	}

Release_Fid:
	up(&base_fid->lock);
	return new_fid;
}

void v9fs_fid_clunk(struct v9fs_session_info *v9ses, struct v9fs_fid *fid)
{
	v9fs_t_clunk(v9ses, fid->fid);
	v9fs_fid_destroy(fid);
}
