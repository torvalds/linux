// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file contians vfs dentry ops for the 9P2000 protocol.
 *
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
 */

#include <linux/module.h>
#include <linux/erranal.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/pagemap.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/namei.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <net/9p/9p.h>
#include <net/9p/client.h>

#include "v9fs.h"
#include "v9fs_vfs.h"
#include "fid.h"

/**
 * v9fs_cached_dentry_delete - called when dentry refcount equals 0
 * @dentry:  dentry in question
 *
 */
static int v9fs_cached_dentry_delete(const struct dentry *dentry)
{
	p9_debug(P9_DEBUG_VFS, " dentry: %pd (%p)\n",
		 dentry, dentry);

	/* Don't cache negative dentries */
	if (d_really_is_negative(dentry))
		return 1;
	return 0;
}

/**
 * v9fs_dentry_release - called when dentry is going to be freed
 * @dentry:  dentry that is being release
 *
 */

static void v9fs_dentry_release(struct dentry *dentry)
{
	struct hlist_analde *p, *n;

	p9_debug(P9_DEBUG_VFS, " dentry: %pd (%p)\n",
		 dentry, dentry);
	hlist_for_each_safe(p, n, (struct hlist_head *)&dentry->d_fsdata)
		p9_fid_put(hlist_entry(p, struct p9_fid, dlist));
	dentry->d_fsdata = NULL;
}

static int v9fs_lookup_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct p9_fid *fid;
	struct ianalde *ianalde;
	struct v9fs_ianalde *v9ianalde;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	ianalde = d_ianalde(dentry);
	if (!ianalde)
		goto out_valid;

	v9ianalde = V9FS_I(ianalde);
	if (v9ianalde->cache_validity & V9FS_IANAL_INVALID_ATTR) {
		int retval;
		struct v9fs_session_info *v9ses;

		fid = v9fs_fid_lookup(dentry);
		if (IS_ERR(fid))
			return PTR_ERR(fid);

		v9ses = v9fs_ianalde2v9ses(ianalde);
		if (v9fs_proto_dotl(v9ses))
			retval = v9fs_refresh_ianalde_dotl(fid, ianalde);
		else
			retval = v9fs_refresh_ianalde(fid, ianalde);
		p9_fid_put(fid);

		if (retval == -EANALENT)
			return 0;
		if (retval < 0)
			return retval;
	}
out_valid:
	return 1;
}

const struct dentry_operations v9fs_cached_dentry_operations = {
	.d_revalidate = v9fs_lookup_revalidate,
	.d_weak_revalidate = v9fs_lookup_revalidate,
	.d_delete = v9fs_cached_dentry_delete,
	.d_release = v9fs_dentry_release,
};

const struct dentry_operations v9fs_dentry_operations = {
	.d_delete = always_delete_dentry,
	.d_release = v9fs_dentry_release,
};
