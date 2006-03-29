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

#include <linux/list.h>

#define FID_OP   0
#define FID_WALK 1
#define FID_CREATE 2

struct v9fs_fid {
	struct list_head list;	 /* list of fids associated with a dentry */
	struct list_head active; /* XXX - debug */

	u32 fid;
	unsigned char fidopen;	  /* set when fid is opened */
	unsigned char fidclunked; /* set when fid has already been clunked */

	struct v9fs_qid qid;
	u32 iounit;

	/* readdir stuff */
	int rdir_fpos;
	loff_t rdir_pos;
	struct v9fs_fcall *rdir_fcall;

	/* management stuff */
	uid_t uid;		/* user associated with this fid */

	/* private data */
	struct file *filp;	/* backpointer to File struct for open files */
	struct v9fs_session_info *v9ses;	/* session info for this FID */
};

struct v9fs_fid *v9fs_fid_lookup(struct dentry *dentry);
struct v9fs_fid *v9fs_fid_get_created(struct dentry *);
void v9fs_fid_destroy(struct v9fs_fid *fid);
struct v9fs_fid *v9fs_fid_create(struct v9fs_session_info *, int fid);
int v9fs_fid_insert(struct v9fs_fid *fid, struct dentry *dentry);
