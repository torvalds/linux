/*
 * V9FS FID Management
 *
 *  Copyright (C) 2005 by Eric Van Hensbergen <ericvh@gmail.com>
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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/idr.h>

#include "debug.h"
#include "v9fs.h"
#include "9p.h"
#include "v9fs_vfs.h"
#include "transport.h"
#include "mux.h"
#include "conv.h"
#include "fid.h"

/**
 * v9fs_fid_insert - add a fid to a dentry
 * @fid: fid to add
 * @dentry: dentry that it is being added to
 *
 */

static int v9fs_fid_insert(struct v9fs_fid *fid, struct dentry *dentry)
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
	fid->pid = current->pid;
	list_add(&fid->list, fid_list);
	return 0;
}

/**
 * v9fs_fid_create - allocate a FID structure
 * @dentry - dentry to link newly created fid to
 *
 */

struct v9fs_fid *v9fs_fid_create(struct dentry *dentry)
{
	struct v9fs_fid *new;

	new = kmalloc(sizeof(struct v9fs_fid), GFP_KERNEL);
	if (new == NULL) {
		dprintk(DEBUG_ERROR, "Out of Memory\n");
		return ERR_PTR(-ENOMEM);
	}

	new->fid = -1;
	new->fidopen = 0;
	new->fidcreate = 0;
	new->fidclunked = 0;
	new->iounit = 0;

	if (v9fs_fid_insert(new, dentry) == 0)
		return new;
	else {
		dprintk(DEBUG_ERROR, "Problems inserting to dentry\n");
		kfree(new);
		return NULL;
	}
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
 * search list of fids associated with a dentry for a fid with a matching
 * thread id or uid.  If that fails, look up the dentry's parents to see if you
 * can find a matching fid.
 *
 */

struct v9fs_fid *v9fs_fid_lookup(struct dentry *dentry, int type)
{
	struct list_head *fid_list = (struct list_head *)dentry->d_fsdata;
	struct v9fs_fid *current_fid = NULL;
	struct v9fs_fid *temp = NULL;
	struct v9fs_fid *return_fid = NULL;
	int found_parent = 0;
	int found_user = 0;

	dprintk(DEBUG_9P, " dentry: %s (%p) type %d\n", dentry->d_iname, dentry,
		type);

	if (fid_list && !list_empty(fid_list)) {
		list_for_each_entry_safe(current_fid, temp, fid_list, list) {
			if (current_fid->uid == current->uid) {
				if (return_fid == NULL) {
					if ((type == FID_OP)
					    || (!current_fid->fidopen)) {
						return_fid = current_fid;
						found_user = 1;
					}
				}
			}
			if (current_fid->pid == current->real_parent->pid) {
				if ((return_fid == NULL) || (found_parent)
				    || (found_user)) {
					if ((type == FID_OP)
					    || (!current_fid->fidopen)) {
						return_fid = current_fid;
						found_parent = 1;
						found_user = 0;
					}
				}
			}
			if (current_fid->pid == current->pid) {
				if ((type == FID_OP) ||
				    (!current_fid->fidopen)) {
					return_fid = current_fid;
					found_parent = 0;
					found_user = 0;
				}
			}
		}
	}

	/* we are at the root but didn't match */
	if ((!return_fid) && (dentry->d_parent == dentry)) {
		/* TODO: clone attach with new uid */
		return_fid = current_fid;
	}

	if (!return_fid) {
		struct dentry *par = current->fs->pwd->d_parent;
		int count = 1;
		while (par != NULL) {
			if (par == dentry)
				break;
			count++;
			if (par == par->d_parent) {
				dprintk(DEBUG_ERROR,
					"got to root without finding dentry\n");
				break;
			}
			par = par->d_parent;
		}

/* XXX - there may be some duplication we can get rid of */
		if (par == dentry) {
			/* we need to fid_lookup the starting point */
			int fidnum = -1;
			int oldfid = -1;
			int result = -1;
			struct v9fs_session_info *v9ses =
			    v9fs_inode2v9ses(current->fs->pwd->d_inode);

			current_fid =
			    v9fs_fid_lookup(current->fs->pwd, FID_WALK);
			if (current_fid == NULL) {
				dprintk(DEBUG_ERROR,
					"process cwd doesn't have a fid\n");
				return return_fid;
			}
			oldfid = current_fid->fid;
			par = current->fs->pwd;
			/* TODO: take advantage of multiwalk */

			fidnum = v9fs_get_idpool(&v9ses->fidpool);
			if (fidnum < 0) {
				dprintk(DEBUG_ERROR,
					"could not get a new fid num\n");
				return return_fid;
			}

			while (par != dentry) {
				result =
				    v9fs_t_walk(v9ses, oldfid, fidnum, "..",
						NULL);
				if (result < 0) {
					dprintk(DEBUG_ERROR,
						"problem walking to parent\n");

					break;
				}
				oldfid = fidnum;
				if (par == par->d_parent) {
					dprintk(DEBUG_ERROR,
						"can't find dentry\n");
					break;
				}
				par = par->d_parent;
			}
			if (par == dentry) {
				return_fid = v9fs_fid_create(dentry);
				return_fid->fid = fidnum;
			}
		}
	}

	return return_fid;
}
