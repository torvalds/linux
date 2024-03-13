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

int v9fs_cache_session_get_cookie(struct v9fs_session_info *v9ses,
				  const char *dev_name)
{
	struct fscache_volume *vcookie;
	char *name, *p;

	name = kasprintf(GFP_KERNEL, "9p,%s,%s",
			 dev_name, v9ses->cachetag ?: v9ses->aname);
	if (!name)
		return -ENOMEM;

	for (p = name; *p; p++)
		if (*p == '/')
			*p = ';';

	vcookie = fscache_acquire_volume(name, NULL, NULL, 0);
	p9_debug(P9_DEBUG_FSC, "session %p get volume %p (%s)\n",
		 v9ses, vcookie, name);
	if (IS_ERR(vcookie)) {
		if (vcookie != ERR_PTR(-EBUSY)) {
			kfree(name);
			return PTR_ERR(vcookie);
		}
		pr_err("Cache volume key already in use (%s)\n", name);
		vcookie = NULL;
	}
	v9ses->fscache = vcookie;
	kfree(name);
	return 0;
}

void v9fs_cache_inode_get_cookie(struct inode *inode)
{
	struct v9fs_inode *v9inode = V9FS_I(inode);
	struct v9fs_session_info *v9ses;
	__le32 version;
	__le64 path;

	if (!S_ISREG(inode->i_mode))
		return;
	if (WARN_ON(v9fs_inode_cookie(v9inode)))
		return;

	version = cpu_to_le32(v9inode->qid.version);
	path = cpu_to_le64(v9inode->qid.path);
	v9ses = v9fs_inode2v9ses(inode);
	v9inode->netfs.cache =
		fscache_acquire_cookie(v9fs_session_cache(v9ses),
				       0,
				       &path, sizeof(path),
				       &version, sizeof(version),
				       i_size_read(&v9inode->netfs.inode));

	p9_debug(P9_DEBUG_FSC, "inode %p get cookie %p\n",
		 inode, v9fs_inode_cookie(v9inode));
}
