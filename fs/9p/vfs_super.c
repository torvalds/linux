/*
 *  linux/fs/9p/vfs_super.c
 *
 * This file contians superblock ops for 9P2000. It is intended that
 * you mount this file system on directories.
 *
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
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

#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/smp_lock.h>
#include <linux/inet.h>
#include <linux/pagemap.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/idr.h>

#include "debug.h"
#include "v9fs.h"
#include "9p.h"
#include "v9fs_vfs.h"
#include "fid.h"

static void v9fs_clear_inode(struct inode *);
static struct super_operations v9fs_super_ops;

/**
 * v9fs_clear_inode - release an inode
 * @inode: inode to release
 *
 */

static void v9fs_clear_inode(struct inode *inode)
{
	filemap_fdatawrite(inode->i_mapping);
}

/**
 * v9fs_set_super - set the superblock
 * @s: super block
 * @data: file system specific data
 *
 */

static int v9fs_set_super(struct super_block *s, void *data)
{
	s->s_fs_info = data;
	return set_anon_super(s, data);
}

/**
 * v9fs_fill_super - populate superblock with info
 * @sb: superblock
 * @v9ses: session information
 *
 */

static void
v9fs_fill_super(struct super_block *sb, struct v9fs_session_info *v9ses,
		int flags)
{
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_blocksize_bits = fls(v9ses->maxdata - 1);
	sb->s_blocksize = 1 << sb->s_blocksize_bits;
	sb->s_magic = V9FS_MAGIC;
	sb->s_op = &v9fs_super_ops;

	sb->s_flags = flags | MS_ACTIVE | MS_SYNCHRONOUS | MS_DIRSYNC |
	    MS_NOATIME;
}

/**
 * v9fs_get_sb - mount a superblock
 * @fs_type: file system type
 * @flags: mount flags
 * @dev_name: device name that was mounted
 * @data: mount options
 *
 */

static struct super_block *v9fs_get_sb(struct file_system_type
				       *fs_type, int flags,
				       const char *dev_name, void *data)
{
	struct super_block *sb = NULL;
	struct v9fs_fcall *fcall = NULL;
	struct inode *inode = NULL;
	struct dentry *root = NULL;
	struct v9fs_session_info *v9ses = NULL;
	struct v9fs_fid *root_fid = NULL;
	int mode = S_IRWXUGO | S_ISVTX;
	uid_t uid = current->fsuid;
	gid_t gid = current->fsgid;
	int stat_result = 0;
	int newfid = 0;
	int retval = 0;

	dprintk(DEBUG_VFS, " \n");

	v9ses = kzalloc(sizeof(struct v9fs_session_info), GFP_KERNEL);
	if (!v9ses)
		return ERR_PTR(-ENOMEM);

	if ((newfid = v9fs_session_init(v9ses, dev_name, data)) < 0) {
		dprintk(DEBUG_ERROR, "problem initiating session\n");
		kfree(v9ses);
		return ERR_PTR(newfid);
	}

	sb = sget(fs_type, NULL, v9fs_set_super, v9ses);

	v9fs_fill_super(sb, v9ses, flags);

	inode = v9fs_get_inode(sb, S_IFDIR | mode);
	if (IS_ERR(inode)) {
		retval = PTR_ERR(inode);
		goto put_back_sb;
	}

	inode->i_uid = uid;
	inode->i_gid = gid;

	root = d_alloc_root(inode);
	if (!root) {
		retval = -ENOMEM;
		goto put_back_sb;
	}

	sb->s_root = root;

	stat_result = v9fs_t_stat(v9ses, newfid, &fcall);
	if (stat_result < 0) {
		dprintk(DEBUG_ERROR, "stat error\n");
		v9fs_t_clunk(v9ses, newfid);
	} else {
		/* Setup the Root Inode */
		root_fid = v9fs_fid_create(v9ses, newfid);
		if (root_fid == NULL) {
			retval = -ENOMEM;
			goto put_back_sb;
		}

		retval = v9fs_fid_insert(root_fid, root);
		if (retval < 0) {
			kfree(fcall);
			goto put_back_sb;
		}

		root_fid->qid = fcall->params.rstat.stat.qid;
		root->d_inode->i_ino =
		    v9fs_qid2ino(&fcall->params.rstat.stat.qid);
		v9fs_stat2inode(&fcall->params.rstat.stat, root->d_inode, sb);
	}

	kfree(fcall);

	if (stat_result < 0) {
		retval = stat_result;
		goto put_back_sb;
	}

	return sb;

put_back_sb:
	/* deactivate_super calls v9fs_kill_super which will frees the rest */
	up_write(&sb->s_umount);
	deactivate_super(sb);
	return ERR_PTR(retval);
}

/**
 * v9fs_kill_super - Kill Superblock
 * @s: superblock
 *
 */

static void v9fs_kill_super(struct super_block *s)
{
	struct v9fs_session_info *v9ses = s->s_fs_info;

	dprintk(DEBUG_VFS, " %p\n", s);

	v9fs_dentry_release(s->s_root);	/* clunk root */

	kill_anon_super(s);

	v9fs_session_close(v9ses);
	kfree(v9ses);
	dprintk(DEBUG_VFS, "exiting kill_super\n");
}

/**
 * v9fs_show_options - Show mount options in /proc/mounts
 * @m: seq_file to write to
 * @mnt: mount descriptor
 *
 */

static int v9fs_show_options(struct seq_file *m, struct vfsmount *mnt)
{
	struct v9fs_session_info *v9ses = mnt->mnt_sb->s_fs_info;

	if (v9ses->debug != 0)
		seq_printf(m, ",debug=%u", v9ses->debug);
	if (v9ses->port != V9FS_PORT)
		seq_printf(m, ",port=%u", v9ses->port);
	if (v9ses->maxdata != 9000)
		seq_printf(m, ",msize=%u", v9ses->maxdata);
	if (v9ses->afid != ~0)
		seq_printf(m, ",afid=%u", v9ses->afid);
	if (v9ses->proto == PROTO_UNIX)
		seq_puts(m, ",proto=unix");
	if (v9ses->extended == 0)
		seq_puts(m, ",noextend");
	if (v9ses->nodev == 1)
		seq_puts(m, ",nodevmap");
	seq_printf(m, ",name=%s", v9ses->name);
	seq_printf(m, ",aname=%s", v9ses->remotename);
	seq_printf(m, ",uid=%u", v9ses->uid);
	seq_printf(m, ",gid=%u", v9ses->gid);
	return 0;
}

static void
v9fs_umount_begin(struct super_block *sb)
{
	struct v9fs_session_info *v9ses = sb->s_fs_info;

	v9fs_session_cancel(v9ses);
}

static struct super_operations v9fs_super_ops = {
	.statfs = simple_statfs,
	.clear_inode = v9fs_clear_inode,
	.show_options = v9fs_show_options,
	.umount_begin = v9fs_umount_begin,
};

struct file_system_type v9fs_fs_type = {
	.name = "9P",
	.get_sb = v9fs_get_sb,
	.kill_sb = v9fs_kill_super,
	.owner = THIS_MODULE,
};
