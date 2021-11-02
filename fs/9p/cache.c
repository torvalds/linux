// SPDX-License-Identifier: GPL-2.0-only
/*
 * V9FS cache definitions.
 *
 *  Copyright (C) 2009 by Abhishek Kulkarni <adkulkar@umail.iu.edu>
 */

#include <linux/jiffies.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <net/9p/9p.h>

#include "v9fs.h"
#include "cache.h"

#define CACHETAG_LEN  11

struct fscache_netfs v9fs_cache_netfs = {
	.name 		= "9p",
	.version 	= 0,
};

/*
 * v9fs_random_cachetag - Generate a random tag to be associated
 *			  with a new cache session.
 *
 * The value of jiffies is used for a fairly randomly cache tag.
 */

static
int v9fs_random_cachetag(struct v9fs_session_info *v9ses)
{
	v9ses->cachetag = kmalloc(CACHETAG_LEN, GFP_KERNEL);
	if (!v9ses->cachetag)
		return -ENOMEM;

	return scnprintf(v9ses->cachetag, CACHETAG_LEN, "%lu", jiffies);
}

const struct fscache_cookie_def v9fs_cache_session_index_def = {
	.name		= "9P.session",
	.type		= FSCACHE_COOKIE_TYPE_INDEX,
};

void v9fs_cache_session_get_cookie(struct v9fs_session_info *v9ses)
{
	/* If no cache session tag was specified, we generate a random one. */
	if (!v9ses->cachetag) {
		if (v9fs_random_cachetag(v9ses) < 0) {
			v9ses->fscache = NULL;
			kfree(v9ses->cachetag);
			v9ses->cachetag = NULL;
			return;
		}
	}

	v9ses->fscache = fscache_acquire_cookie(v9fs_cache_netfs.primary_index,
						&v9fs_cache_session_index_def,
						v9ses->cachetag,
						strlen(v9ses->cachetag),
						NULL, 0,
						v9ses, 0, true);
	p9_debug(P9_DEBUG_FSC, "session %p get cookie %p\n",
		 v9ses, v9ses->fscache);
}

void v9fs_cache_session_put_cookie(struct v9fs_session_info *v9ses)
{
	p9_debug(P9_DEBUG_FSC, "session %p put cookie %p\n",
		 v9ses, v9ses->fscache);
	fscache_relinquish_cookie(v9ses->fscache, NULL, false);
	v9ses->fscache = NULL;
}

static enum
fscache_checkaux v9fs_cache_inode_check_aux(void *cookie_netfs_data,
					    const void *buffer,
					    uint16_t buflen,
					    loff_t object_size)
{
	const struct v9fs_inode *v9inode = cookie_netfs_data;

	if (buflen != sizeof(v9inode->qid.version))
		return FSCACHE_CHECKAUX_OBSOLETE;

	if (memcmp(buffer, &v9inode->qid.version,
		   sizeof(v9inode->qid.version)))
		return FSCACHE_CHECKAUX_OBSOLETE;

	return FSCACHE_CHECKAUX_OKAY;
}

const struct fscache_cookie_def v9fs_cache_inode_index_def = {
	.name		= "9p.inode",
	.type		= FSCACHE_COOKIE_TYPE_DATAFILE,
	.check_aux	= v9fs_cache_inode_check_aux,
};

void v9fs_cache_inode_get_cookie(struct inode *inode)
{
	struct v9fs_inode *v9inode;
	struct v9fs_session_info *v9ses;

	if (!S_ISREG(inode->i_mode))
		return;

	v9inode = V9FS_I(inode);
	if (v9inode->fscache)
		return;

	v9ses = v9fs_inode2v9ses(inode);
	v9inode->fscache = fscache_acquire_cookie(v9ses->fscache,
						  &v9fs_cache_inode_index_def,
						  &v9inode->qid.path,
						  sizeof(v9inode->qid.path),
						  &v9inode->qid.version,
						  sizeof(v9inode->qid.version),
						  v9inode,
						  i_size_read(&v9inode->vfs_inode),
						  true);

	p9_debug(P9_DEBUG_FSC, "inode %p get cookie %p\n",
		 inode, v9inode->fscache);
}

void v9fs_cache_inode_put_cookie(struct inode *inode)
{
	struct v9fs_inode *v9inode = V9FS_I(inode);

	if (!v9inode->fscache)
		return;
	p9_debug(P9_DEBUG_FSC, "inode %p put cookie %p\n",
		 inode, v9inode->fscache);

	fscache_relinquish_cookie(v9inode->fscache, &v9inode->qid.version,
				  false);
	v9inode->fscache = NULL;
}

void v9fs_cache_inode_flush_cookie(struct inode *inode)
{
	struct v9fs_inode *v9inode = V9FS_I(inode);

	if (!v9inode->fscache)
		return;
	p9_debug(P9_DEBUG_FSC, "inode %p flush cookie %p\n",
		 inode, v9inode->fscache);

	fscache_relinquish_cookie(v9inode->fscache, NULL, true);
	v9inode->fscache = NULL;
}

void v9fs_cache_inode_set_cookie(struct inode *inode, struct file *filp)
{
	struct v9fs_inode *v9inode = V9FS_I(inode);

	if (!v9inode->fscache)
		return;

	mutex_lock(&v9inode->fscache_lock);

	if ((filp->f_flags & O_ACCMODE) != O_RDONLY)
		v9fs_cache_inode_flush_cookie(inode);
	else
		v9fs_cache_inode_get_cookie(inode);

	mutex_unlock(&v9inode->fscache_lock);
}

void v9fs_cache_inode_reset_cookie(struct inode *inode)
{
	struct v9fs_inode *v9inode = V9FS_I(inode);
	struct v9fs_session_info *v9ses;
	struct fscache_cookie *old;

	if (!v9inode->fscache)
		return;

	old = v9inode->fscache;

	mutex_lock(&v9inode->fscache_lock);
	fscache_relinquish_cookie(v9inode->fscache, NULL, true);

	v9ses = v9fs_inode2v9ses(inode);
	v9inode->fscache = fscache_acquire_cookie(v9ses->fscache,
						  &v9fs_cache_inode_index_def,
						  &v9inode->qid.path,
						  sizeof(v9inode->qid.path),
						  &v9inode->qid.version,
						  sizeof(v9inode->qid.version),
						  v9inode,
						  i_size_read(&v9inode->vfs_inode),
						  true);
	p9_debug(P9_DEBUG_FSC, "inode %p revalidating cookie old %p new %p\n",
		 inode, old, v9inode->fscache);

	mutex_unlock(&v9inode->fscache_lock);
}
