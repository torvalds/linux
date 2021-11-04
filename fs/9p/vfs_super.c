// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/inet.h>
#include <linux/pagemap.h>
#include <linux/mount.h>
#include <linux/idr.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/statfs.h>
#include <linux/magic.h>
#include <net/9p/9p.h>
#include <net/9p/client.h>

#include "v9fs.h"
#include "v9fs_vfs.h"
#include "fid.h"
#include "xattr.h"
#include "acl.h"

static const struct super_operations v9fs_super_ops, v9fs_super_ops_dotl;

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
 * @flags: flags propagated from v9fs_mount()
 *
 */

static int
v9fs_fill_super(struct super_block *sb, struct v9fs_session_info *v9ses,
		int flags)
{
	int ret;

	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_blocksize_bits = fls(v9ses->maxdata - 1);
	sb->s_blocksize = 1 << sb->s_blocksize_bits;
	sb->s_magic = V9FS_MAGIC;
	if (v9fs_proto_dotl(v9ses)) {
		sb->s_op = &v9fs_super_ops_dotl;
		sb->s_xattr = v9fs_xattr_handlers;
	} else {
		sb->s_op = &v9fs_super_ops;
		sb->s_time_max = U32_MAX;
	}

	sb->s_time_min = 0;

	ret = super_setup_bdi(sb);
	if (ret)
		return ret;

	if (!v9ses->cache) {
		sb->s_bdi->ra_pages = 0;
		sb->s_bdi->io_pages = 0;
	} else {
		sb->s_bdi->ra_pages = v9ses->maxdata >> PAGE_SHIFT;
		sb->s_bdi->io_pages = v9ses->maxdata >> PAGE_SHIFT;
	}

	sb->s_flags |= SB_ACTIVE | SB_DIRSYNC;
	if (!v9ses->cache)
		sb->s_flags |= SB_SYNCHRONOUS;

#ifdef CONFIG_9P_FS_POSIX_ACL
	if ((v9ses->flags & V9FS_ACL_MASK) == V9FS_POSIX_ACL)
		sb->s_flags |= SB_POSIXACL;
#endif

	return 0;
}

/**
 * v9fs_mount - mount a superblock
 * @fs_type: file system type
 * @flags: mount flags
 * @dev_name: device name that was mounted
 * @data: mount options
 *
 */

static struct dentry *v9fs_mount(struct file_system_type *fs_type, int flags,
		       const char *dev_name, void *data)
{
	struct super_block *sb = NULL;
	struct inode *inode = NULL;
	struct dentry *root = NULL;
	struct v9fs_session_info *v9ses = NULL;
	umode_t mode = S_IRWXUGO | S_ISVTX;
	struct p9_fid *fid;
	int retval = 0;

	p9_debug(P9_DEBUG_VFS, "\n");

	v9ses = kzalloc(sizeof(struct v9fs_session_info), GFP_KERNEL);
	if (!v9ses)
		return ERR_PTR(-ENOMEM);

	fid = v9fs_session_init(v9ses, dev_name, data);
	if (IS_ERR(fid)) {
		retval = PTR_ERR(fid);
		goto free_session;
	}

	sb = sget(fs_type, NULL, v9fs_set_super, flags, v9ses);
	if (IS_ERR(sb)) {
		retval = PTR_ERR(sb);
		goto clunk_fid;
	}
	retval = v9fs_fill_super(sb, v9ses, flags);
	if (retval)
		goto release_sb;

	if (v9ses->cache == CACHE_LOOSE || v9ses->cache == CACHE_FSCACHE)
		sb->s_d_op = &v9fs_cached_dentry_operations;
	else
		sb->s_d_op = &v9fs_dentry_operations;

	inode = v9fs_get_inode(sb, S_IFDIR | mode, 0);
	if (IS_ERR(inode)) {
		retval = PTR_ERR(inode);
		goto release_sb;
	}

	root = d_make_root(inode);
	if (!root) {
		retval = -ENOMEM;
		goto release_sb;
	}
	sb->s_root = root;
	if (v9fs_proto_dotl(v9ses)) {
		struct p9_stat_dotl *st = NULL;
		st = p9_client_getattr_dotl(fid, P9_STATS_BASIC);
		if (IS_ERR(st)) {
			retval = PTR_ERR(st);
			goto release_sb;
		}
		d_inode(root)->i_ino = v9fs_qid2ino(&st->qid);
		v9fs_stat2inode_dotl(st, d_inode(root), 0);
		kfree(st);
	} else {
		struct p9_wstat *st = NULL;
		st = p9_client_stat(fid);
		if (IS_ERR(st)) {
			retval = PTR_ERR(st);
			goto release_sb;
		}

		d_inode(root)->i_ino = v9fs_qid2ino(&st->qid);
		v9fs_stat2inode(st, d_inode(root), sb, 0);

		p9stat_free(st);
		kfree(st);
	}
	retval = v9fs_get_acl(inode, fid);
	if (retval)
		goto release_sb;
	v9fs_fid_add(root, fid);

	p9_debug(P9_DEBUG_VFS, " simple set mount, return 0\n");
	return dget(sb->s_root);

clunk_fid:
	p9_client_clunk(fid);
	v9fs_session_close(v9ses);
free_session:
	kfree(v9ses);
	return ERR_PTR(retval);

release_sb:
	/*
	 * we will do the session_close and root dentry release
	 * in the below call. But we need to clunk fid, because we haven't
	 * attached the fid to dentry so it won't get clunked
	 * automatically.
	 */
	p9_client_clunk(fid);
	deactivate_locked_super(sb);
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

	p9_debug(P9_DEBUG_VFS, " %p\n", s);

	kill_anon_super(s);

	v9fs_session_cancel(v9ses);
	v9fs_session_close(v9ses);
	kfree(v9ses);
	s->s_fs_info = NULL;
	p9_debug(P9_DEBUG_VFS, "exiting kill_super\n");
}

static void
v9fs_umount_begin(struct super_block *sb)
{
	struct v9fs_session_info *v9ses;

	v9ses = sb->s_fs_info;
	v9fs_session_begin_cancel(v9ses);
}

static int v9fs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct v9fs_session_info *v9ses;
	struct p9_fid *fid;
	struct p9_rstatfs rs;
	int res;

	fid = v9fs_fid_lookup(dentry);
	if (IS_ERR(fid)) {
		res = PTR_ERR(fid);
		goto done;
	}

	v9ses = v9fs_dentry2v9ses(dentry);
	if (v9fs_proto_dotl(v9ses)) {
		res = p9_client_statfs(fid, &rs);
		if (res == 0) {
			buf->f_type = rs.type;
			buf->f_bsize = rs.bsize;
			buf->f_blocks = rs.blocks;
			buf->f_bfree = rs.bfree;
			buf->f_bavail = rs.bavail;
			buf->f_files = rs.files;
			buf->f_ffree = rs.ffree;
			buf->f_fsid = u64_to_fsid(rs.fsid);
			buf->f_namelen = rs.namelen;
		}
		if (res != -ENOSYS)
			goto done;
	}
	res = simple_statfs(dentry, buf);
done:
	p9_client_clunk(fid);
	return res;
}

static int v9fs_drop_inode(struct inode *inode)
{
	struct v9fs_session_info *v9ses;
	v9ses = v9fs_inode2v9ses(inode);
	if (v9ses->cache == CACHE_LOOSE || v9ses->cache == CACHE_FSCACHE)
		return generic_drop_inode(inode);
	/*
	 * in case of non cached mode always drop the
	 * the inode because we want the inode attribute
	 * to always match that on the server.
	 */
	return 1;
}

static int v9fs_write_inode(struct inode *inode,
			    struct writeback_control *wbc)
{
	int ret;
	struct p9_wstat wstat;
	struct v9fs_inode *v9inode;
	/*
	 * send an fsync request to server irrespective of
	 * wbc->sync_mode.
	 */
	p9_debug(P9_DEBUG_VFS, "%s: inode %p\n", __func__, inode);
	v9inode = V9FS_I(inode);
	if (!v9inode->writeback_fid)
		return 0;
	v9fs_blank_wstat(&wstat);

	ret = p9_client_wstat(v9inode->writeback_fid, &wstat);
	if (ret < 0) {
		__mark_inode_dirty(inode, I_DIRTY_DATASYNC);
		return ret;
	}
	return 0;
}

static int v9fs_write_inode_dotl(struct inode *inode,
				 struct writeback_control *wbc)
{
	int ret;
	struct v9fs_inode *v9inode;
	/*
	 * send an fsync request to server irrespective of
	 * wbc->sync_mode.
	 */
	v9inode = V9FS_I(inode);
	p9_debug(P9_DEBUG_VFS, "%s: inode %p, writeback_fid %p\n",
		 __func__, inode, v9inode->writeback_fid);
	if (!v9inode->writeback_fid)
		return 0;

	ret = p9_client_fsync(v9inode->writeback_fid, 0);
	if (ret < 0) {
		__mark_inode_dirty(inode, I_DIRTY_DATASYNC);
		return ret;
	}
	return 0;
}

static const struct super_operations v9fs_super_ops = {
	.alloc_inode = v9fs_alloc_inode,
	.free_inode = v9fs_free_inode,
	.statfs = simple_statfs,
	.evict_inode = v9fs_evict_inode,
	.show_options = v9fs_show_options,
	.umount_begin = v9fs_umount_begin,
	.write_inode = v9fs_write_inode,
};

static const struct super_operations v9fs_super_ops_dotl = {
	.alloc_inode = v9fs_alloc_inode,
	.free_inode = v9fs_free_inode,
	.statfs = v9fs_statfs,
	.drop_inode = v9fs_drop_inode,
	.evict_inode = v9fs_evict_inode,
	.show_options = v9fs_show_options,
	.umount_begin = v9fs_umount_begin,
	.write_inode = v9fs_write_inode_dotl,
};

struct file_system_type v9fs_fs_type = {
	.name = "9p",
	.mount = v9fs_mount,
	.kill_sb = v9fs_kill_super,
	.owner = THIS_MODULE,
	.fs_flags = FS_RENAME_DOES_D_MOVE,
};
MODULE_ALIAS_FS("9p");
