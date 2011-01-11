/*
 *  linux/fs/9p/vfs_inode_dotl.c
 *
 * This file contains vfs inode ops for the 9P2000.L protocol.
 *
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
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
#include <linux/file.h>
#include <linux/pagemap.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/inet.h>
#include <linux/namei.h>
#include <linux/idr.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/xattr.h>
#include <linux/posix_acl.h>
#include <net/9p/9p.h>
#include <net/9p/client.h>

#include "v9fs.h"
#include "v9fs_vfs.h"
#include "fid.h"
#include "cache.h"
#include "xattr.h"
#include "acl.h"

static int
v9fs_vfs_mknod_dotl(struct inode *dir, struct dentry *dentry, int omode,
		    dev_t rdev);

/**
 * v9fs_get_fsgid_for_create - Helper function to get the gid for creating a
 * new file system object. This checks the S_ISGID to determine the owning
 * group of the new file system object.
 */

static gid_t v9fs_get_fsgid_for_create(struct inode *dir_inode)
{
	BUG_ON(dir_inode == NULL);

	if (dir_inode->i_mode & S_ISGID) {
		/* set_gid bit is set.*/
		return dir_inode->i_gid;
	}
	return current_fsgid();
}

/**
 * v9fs_dentry_from_dir_inode - helper function to get the dentry from
 * dir inode.
 *
 */

static struct dentry *v9fs_dentry_from_dir_inode(struct inode *inode)
{
	struct dentry *dentry;

	spin_lock(&inode->i_lock);
	/* Directory should have only one entry. */
	BUG_ON(S_ISDIR(inode->i_mode) && !list_is_singular(&inode->i_dentry));
	dentry = list_entry(inode->i_dentry.next, struct dentry, d_alias);
	spin_unlock(&inode->i_lock);
	return dentry;
}

struct inode *
v9fs_inode_dotl(struct v9fs_session_info *v9ses, struct p9_fid *fid,
	struct super_block *sb)
{
	struct inode *ret = NULL;
	int err;
	struct p9_stat_dotl *st;

	st = p9_client_getattr_dotl(fid, P9_STATS_BASIC);
	if (IS_ERR(st))
		return ERR_CAST(st);

	ret = v9fs_get_inode(sb, st->st_mode);
	if (IS_ERR(ret)) {
		err = PTR_ERR(ret);
		goto error;
	}

	v9fs_stat2inode_dotl(st, ret);
	ret->i_ino = v9fs_qid2ino(&st->qid);
#ifdef CONFIG_9P_FSCACHE
	v9fs_vcookie_set_qid(ret, &st->qid);
	v9fs_cache_inode_get_cookie(ret);
#endif
	err = v9fs_get_acl(ret, fid);
	if (err) {
		iput(ret);
		goto error;
	}
	kfree(st);
	return ret;
error:
	kfree(st);
	return ERR_PTR(err);
}

/**
 * v9fs_vfs_create_dotl - VFS hook to create files for 9P2000.L protocol.
 * @dir: directory inode that is being created
 * @dentry:  dentry that is being deleted
 * @mode: create permissions
 * @nd: path information
 *
 */

static int
v9fs_vfs_create_dotl(struct inode *dir, struct dentry *dentry, int omode,
		struct nameidata *nd)
{
	int err = 0;
	char *name = NULL;
	gid_t gid;
	int flags;
	mode_t mode;
	struct v9fs_session_info *v9ses;
	struct p9_fid *fid = NULL;
	struct p9_fid *dfid, *ofid;
	struct file *filp;
	struct p9_qid qid;
	struct inode *inode;
	struct posix_acl *pacl = NULL, *dacl = NULL;

	v9ses = v9fs_inode2v9ses(dir);
	if (nd && nd->flags & LOOKUP_OPEN)
		flags = nd->intent.open.flags - 1;
	else {
		/*
		 * create call without LOOKUP_OPEN is due
		 * to mknod of regular files. So use mknod
		 * operation.
		 */
		return v9fs_vfs_mknod_dotl(dir, dentry, omode, 0);
	}

	name = (char *) dentry->d_name.name;
	P9_DPRINTK(P9_DEBUG_VFS, "v9fs_vfs_create_dotl: name:%s flags:0x%x "
			"mode:0x%x\n", name, flags, omode);

	dfid = v9fs_fid_lookup(dentry->d_parent);
	if (IS_ERR(dfid)) {
		err = PTR_ERR(dfid);
		P9_DPRINTK(P9_DEBUG_VFS, "fid lookup failed %d\n", err);
		return err;
	}

	/* clone a fid to use for creation */
	ofid = p9_client_walk(dfid, 0, NULL, 1);
	if (IS_ERR(ofid)) {
		err = PTR_ERR(ofid);
		P9_DPRINTK(P9_DEBUG_VFS, "p9_client_walk failed %d\n", err);
		return err;
	}

	gid = v9fs_get_fsgid_for_create(dir);

	mode = omode;
	/* Update mode based on ACL value */
	err = v9fs_acl_mode(dir, &mode, &dacl, &pacl);
	if (err) {
		P9_DPRINTK(P9_DEBUG_VFS,
			   "Failed to get acl values in creat %d\n", err);
		goto error;
	}
	err = p9_client_create_dotl(ofid, name, flags, mode, gid, &qid);
	if (err < 0) {
		P9_DPRINTK(P9_DEBUG_VFS,
				"p9_client_open_dotl failed in creat %d\n",
				err);
		goto error;
	}

	/* instantiate inode and assign the unopened fid to the dentry */
	fid = p9_client_walk(dfid, 1, &name, 1);
	if (IS_ERR(fid)) {
		err = PTR_ERR(fid);
		P9_DPRINTK(P9_DEBUG_VFS, "p9_client_walk failed %d\n", err);
		fid = NULL;
		goto error;
	}
	inode = v9fs_inode_from_fid(v9ses, fid, dir->i_sb);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		P9_DPRINTK(P9_DEBUG_VFS, "inode creation failed %d\n", err);
		goto error;
	}
	if (v9ses->cache)
		dentry->d_op = &v9fs_cached_dentry_operations;
	else
		dentry->d_op = &v9fs_dentry_operations;

	d_instantiate(dentry, inode);
	err = v9fs_fid_add(dentry, fid);
	if (err < 0)
		goto error;

	/* Now set the ACL based on the default value */
	v9fs_set_create_acl(dentry, dacl, pacl);

	/* Since we are opening a file, assign the open fid to the file */
	filp = lookup_instantiate_filp(nd, dentry, generic_file_open);
	if (IS_ERR(filp)) {
		p9_client_clunk(ofid);
		return PTR_ERR(filp);
	}
	filp->private_data = ofid;
	return 0;

error:
	if (ofid)
		p9_client_clunk(ofid);
	if (fid)
		p9_client_clunk(fid);
	return err;
}

/**
 * v9fs_vfs_mkdir_dotl - VFS mkdir hook to create a directory
 * @dir:  inode that is being unlinked
 * @dentry: dentry that is being unlinked
 * @mode: mode for new directory
 *
 */

static int v9fs_vfs_mkdir_dotl(struct inode *dir,
			       struct dentry *dentry, int omode)
{
	int err;
	struct v9fs_session_info *v9ses;
	struct p9_fid *fid = NULL, *dfid = NULL;
	gid_t gid;
	char *name;
	mode_t mode;
	struct inode *inode;
	struct p9_qid qid;
	struct dentry *dir_dentry;
	struct posix_acl *dacl = NULL, *pacl = NULL;

	P9_DPRINTK(P9_DEBUG_VFS, "name %s\n", dentry->d_name.name);
	err = 0;
	v9ses = v9fs_inode2v9ses(dir);

	omode |= S_IFDIR;
	if (dir->i_mode & S_ISGID)
		omode |= S_ISGID;

	dir_dentry = v9fs_dentry_from_dir_inode(dir);
	dfid = v9fs_fid_lookup(dir_dentry);
	if (IS_ERR(dfid)) {
		err = PTR_ERR(dfid);
		P9_DPRINTK(P9_DEBUG_VFS, "fid lookup failed %d\n", err);
		dfid = NULL;
		goto error;
	}

	gid = v9fs_get_fsgid_for_create(dir);
	mode = omode;
	/* Update mode based on ACL value */
	err = v9fs_acl_mode(dir, &mode, &dacl, &pacl);
	if (err) {
		P9_DPRINTK(P9_DEBUG_VFS,
			   "Failed to get acl values in mkdir %d\n", err);
		goto error;
	}
	name = (char *) dentry->d_name.name;
	err = p9_client_mkdir_dotl(dfid, name, mode, gid, &qid);
	if (err < 0)
		goto error;

	/* instantiate inode and assign the unopened fid to the dentry */
	if (v9ses->cache == CACHE_LOOSE || v9ses->cache == CACHE_FSCACHE) {
		fid = p9_client_walk(dfid, 1, &name, 1);
		if (IS_ERR(fid)) {
			err = PTR_ERR(fid);
			P9_DPRINTK(P9_DEBUG_VFS, "p9_client_walk failed %d\n",
				err);
			fid = NULL;
			goto error;
		}

		inode = v9fs_inode_from_fid(v9ses, fid, dir->i_sb);
		if (IS_ERR(inode)) {
			err = PTR_ERR(inode);
			P9_DPRINTK(P9_DEBUG_VFS, "inode creation failed %d\n",
				err);
			goto error;
		}
		d_set_d_op(dentry, &v9fs_cached_dentry_operations);
		d_instantiate(dentry, inode);
		err = v9fs_fid_add(dentry, fid);
		if (err < 0)
			goto error;
		fid = NULL;
	} else {
		/*
		 * Not in cached mode. No need to populate
		 * inode with stat. We need to get an inode
		 * so that we can set the acl with dentry
		 */
		inode = v9fs_get_inode(dir->i_sb, mode);
		if (IS_ERR(inode)) {
			err = PTR_ERR(inode);
			goto error;
		}
		d_set_d_op(dentry, &v9fs_dentry_operations);
		d_instantiate(dentry, inode);
	}
	/* Now set the ACL based on the default value */
	v9fs_set_create_acl(dentry, dacl, pacl);

error:
	if (fid)
		p9_client_clunk(fid);
	return err;
}

static int
v9fs_vfs_getattr_dotl(struct vfsmount *mnt, struct dentry *dentry,
		 struct kstat *stat)
{
	int err;
	struct v9fs_session_info *v9ses;
	struct p9_fid *fid;
	struct p9_stat_dotl *st;

	P9_DPRINTK(P9_DEBUG_VFS, "dentry: %p\n", dentry);
	err = -EPERM;
	v9ses = v9fs_inode2v9ses(dentry->d_inode);
	if (v9ses->cache == CACHE_LOOSE || v9ses->cache == CACHE_FSCACHE)
		return simple_getattr(mnt, dentry, stat);

	fid = v9fs_fid_lookup(dentry);
	if (IS_ERR(fid))
		return PTR_ERR(fid);

	/* Ask for all the fields in stat structure. Server will return
	 * whatever it supports
	 */

	st = p9_client_getattr_dotl(fid, P9_STATS_ALL);
	if (IS_ERR(st))
		return PTR_ERR(st);

	v9fs_stat2inode_dotl(st, dentry->d_inode);
	generic_fillattr(dentry->d_inode, stat);
	/* Change block size to what the server returned */
	stat->blksize = st->st_blksize;

	kfree(st);
	return 0;
}

/**
 * v9fs_vfs_setattr_dotl - set file metadata
 * @dentry: file whose metadata to set
 * @iattr: metadata assignment structure
 *
 */

int v9fs_vfs_setattr_dotl(struct dentry *dentry, struct iattr *iattr)
{
	int retval;
	struct v9fs_session_info *v9ses;
	struct p9_fid *fid;
	struct p9_iattr_dotl p9attr;

	P9_DPRINTK(P9_DEBUG_VFS, "\n");

	retval = inode_change_ok(dentry->d_inode, iattr);
	if (retval)
		return retval;

	p9attr.valid = iattr->ia_valid;
	p9attr.mode = iattr->ia_mode;
	p9attr.uid = iattr->ia_uid;
	p9attr.gid = iattr->ia_gid;
	p9attr.size = iattr->ia_size;
	p9attr.atime_sec = iattr->ia_atime.tv_sec;
	p9attr.atime_nsec = iattr->ia_atime.tv_nsec;
	p9attr.mtime_sec = iattr->ia_mtime.tv_sec;
	p9attr.mtime_nsec = iattr->ia_mtime.tv_nsec;

	retval = -EPERM;
	v9ses = v9fs_inode2v9ses(dentry->d_inode);
	fid = v9fs_fid_lookup(dentry);
	if (IS_ERR(fid))
		return PTR_ERR(fid);

	retval = p9_client_setattr(fid, &p9attr);
	if (retval < 0)
		return retval;

	if ((iattr->ia_valid & ATTR_SIZE) &&
	    iattr->ia_size != i_size_read(dentry->d_inode)) {
		retval = vmtruncate(dentry->d_inode, iattr->ia_size);
		if (retval)
			return retval;
	}

	setattr_copy(dentry->d_inode, iattr);
	mark_inode_dirty(dentry->d_inode);
	if (iattr->ia_valid & ATTR_MODE) {
		/* We also want to update ACL when we update mode bits */
		retval = v9fs_acl_chmod(dentry);
		if (retval < 0)
			return retval;
	}
	return 0;
}

/**
 * v9fs_stat2inode_dotl - populate an inode structure with stat info
 * @stat: stat structure
 * @inode: inode to populate
 * @sb: superblock of filesystem
 *
 */

void
v9fs_stat2inode_dotl(struct p9_stat_dotl *stat, struct inode *inode)
{

	if ((stat->st_result_mask & P9_STATS_BASIC) == P9_STATS_BASIC) {
		inode->i_atime.tv_sec = stat->st_atime_sec;
		inode->i_atime.tv_nsec = stat->st_atime_nsec;
		inode->i_mtime.tv_sec = stat->st_mtime_sec;
		inode->i_mtime.tv_nsec = stat->st_mtime_nsec;
		inode->i_ctime.tv_sec = stat->st_ctime_sec;
		inode->i_ctime.tv_nsec = stat->st_ctime_nsec;
		inode->i_uid = stat->st_uid;
		inode->i_gid = stat->st_gid;
		inode->i_nlink = stat->st_nlink;
		inode->i_mode = stat->st_mode;
		inode->i_rdev = new_decode_dev(stat->st_rdev);

		if ((S_ISBLK(inode->i_mode)) || (S_ISCHR(inode->i_mode)))
			init_special_inode(inode, inode->i_mode, inode->i_rdev);

		i_size_write(inode, stat->st_size);
		inode->i_blocks = stat->st_blocks;
	} else {
		if (stat->st_result_mask & P9_STATS_ATIME) {
			inode->i_atime.tv_sec = stat->st_atime_sec;
			inode->i_atime.tv_nsec = stat->st_atime_nsec;
		}
		if (stat->st_result_mask & P9_STATS_MTIME) {
			inode->i_mtime.tv_sec = stat->st_mtime_sec;
			inode->i_mtime.tv_nsec = stat->st_mtime_nsec;
		}
		if (stat->st_result_mask & P9_STATS_CTIME) {
			inode->i_ctime.tv_sec = stat->st_ctime_sec;
			inode->i_ctime.tv_nsec = stat->st_ctime_nsec;
		}
		if (stat->st_result_mask & P9_STATS_UID)
			inode->i_uid = stat->st_uid;
		if (stat->st_result_mask & P9_STATS_GID)
			inode->i_gid = stat->st_gid;
		if (stat->st_result_mask & P9_STATS_NLINK)
			inode->i_nlink = stat->st_nlink;
		if (stat->st_result_mask & P9_STATS_MODE) {
			inode->i_mode = stat->st_mode;
			if ((S_ISBLK(inode->i_mode)) ||
						(S_ISCHR(inode->i_mode)))
				init_special_inode(inode, inode->i_mode,
								inode->i_rdev);
		}
		if (stat->st_result_mask & P9_STATS_RDEV)
			inode->i_rdev = new_decode_dev(stat->st_rdev);
		if (stat->st_result_mask & P9_STATS_SIZE)
			i_size_write(inode, stat->st_size);
		if (stat->st_result_mask & P9_STATS_BLOCKS)
			inode->i_blocks = stat->st_blocks;
	}
	if (stat->st_result_mask & P9_STATS_GEN)
			inode->i_generation = stat->st_gen;

	/* Currently we don't support P9_STATS_BTIME and P9_STATS_DATA_VERSION
	 * because the inode structure does not have fields for them.
	 */
}

static int
v9fs_vfs_symlink_dotl(struct inode *dir, struct dentry *dentry,
		const char *symname)
{
	struct v9fs_session_info *v9ses;
	struct p9_fid *dfid;
	struct p9_fid *fid = NULL;
	struct inode *inode;
	struct p9_qid qid;
	char *name;
	int err;
	gid_t gid;

	name = (char *) dentry->d_name.name;
	P9_DPRINTK(P9_DEBUG_VFS, "v9fs_vfs_symlink_dotl : %lu,%s,%s\n",
			dir->i_ino, name, symname);
	v9ses = v9fs_inode2v9ses(dir);

	dfid = v9fs_fid_lookup(dentry->d_parent);
	if (IS_ERR(dfid)) {
		err = PTR_ERR(dfid);
		P9_DPRINTK(P9_DEBUG_VFS, "fid lookup failed %d\n", err);
		return err;
	}

	gid = v9fs_get_fsgid_for_create(dir);

	/* Server doesn't alter fid on TSYMLINK. Hence no need to clone it. */
	err = p9_client_symlink(dfid, name, (char *)symname, gid, &qid);

	if (err < 0) {
		P9_DPRINTK(P9_DEBUG_VFS, "p9_client_symlink failed %d\n", err);
		goto error;
	}

	if (v9ses->cache) {
		/* Now walk from the parent so we can get an unopened fid. */
		fid = p9_client_walk(dfid, 1, &name, 1);
		if (IS_ERR(fid)) {
			err = PTR_ERR(fid);
			P9_DPRINTK(P9_DEBUG_VFS, "p9_client_walk failed %d\n",
					err);
			fid = NULL;
			goto error;
		}

		/* instantiate inode and assign the unopened fid to dentry */
		inode = v9fs_inode_from_fid(v9ses, fid, dir->i_sb);
		if (IS_ERR(inode)) {
			err = PTR_ERR(inode);
			P9_DPRINTK(P9_DEBUG_VFS, "inode creation failed %d\n",
					err);
			goto error;
		}
		d_set_d_op(dentry, &v9fs_cached_dentry_operations);
		d_instantiate(dentry, inode);
		err = v9fs_fid_add(dentry, fid);
		if (err < 0)
			goto error;
		fid = NULL;
	} else {
		/* Not in cached mode. No need to populate inode with stat */
		inode = v9fs_get_inode(dir->i_sb, S_IFLNK);
		if (IS_ERR(inode)) {
			err = PTR_ERR(inode);
			goto error;
		}
		d_set_d_op(dentry, &v9fs_dentry_operations);
		d_instantiate(dentry, inode);
	}

error:
	if (fid)
		p9_client_clunk(fid);

	return err;
}

/**
 * v9fs_vfs_link_dotl - create a hardlink for dotl
 * @old_dentry: dentry for file to link to
 * @dir: inode destination for new link
 * @dentry: dentry for link
 *
 */

static int
v9fs_vfs_link_dotl(struct dentry *old_dentry, struct inode *dir,
		struct dentry *dentry)
{
	int err;
	struct p9_fid *dfid, *oldfid;
	char *name;
	struct v9fs_session_info *v9ses;
	struct dentry *dir_dentry;

	P9_DPRINTK(P9_DEBUG_VFS, "dir ino: %lu, old_name: %s, new_name: %s\n",
			dir->i_ino, old_dentry->d_name.name,
			dentry->d_name.name);

	v9ses = v9fs_inode2v9ses(dir);
	dir_dentry = v9fs_dentry_from_dir_inode(dir);
	dfid = v9fs_fid_lookup(dir_dentry);
	if (IS_ERR(dfid))
		return PTR_ERR(dfid);

	oldfid = v9fs_fid_lookup(old_dentry);
	if (IS_ERR(oldfid))
		return PTR_ERR(oldfid);

	name = (char *) dentry->d_name.name;

	err = p9_client_link(dfid, oldfid, (char *)dentry->d_name.name);

	if (err < 0) {
		P9_DPRINTK(P9_DEBUG_VFS, "p9_client_link failed %d\n", err);
		return err;
	}

	if (v9ses->cache == CACHE_LOOSE || v9ses->cache == CACHE_FSCACHE) {
		/* Get the latest stat info from server. */
		struct p9_fid *fid;
		struct p9_stat_dotl *st;

		fid = v9fs_fid_lookup(old_dentry);
		if (IS_ERR(fid))
			return PTR_ERR(fid);

		st = p9_client_getattr_dotl(fid, P9_STATS_BASIC);
		if (IS_ERR(st))
			return PTR_ERR(st);

		v9fs_stat2inode_dotl(st, old_dentry->d_inode);

		kfree(st);
	} else {
		/* Caching disabled. No need to get upto date stat info.
		 * This dentry will be released immediately. So, just hold the
		 * inode
		 */
		ihold(old_dentry->d_inode);
	}

	d_set_d_op(dentry, old_dentry->d_op);
	d_instantiate(dentry, old_dentry->d_inode);

	return err;
}

/**
 * v9fs_vfs_mknod_dotl - create a special file
 * @dir: inode destination for new link
 * @dentry: dentry for file
 * @mode: mode for creation
 * @rdev: device associated with special file
 *
 */
static int
v9fs_vfs_mknod_dotl(struct inode *dir, struct dentry *dentry, int omode,
		dev_t rdev)
{
	int err;
	char *name;
	mode_t mode;
	struct v9fs_session_info *v9ses;
	struct p9_fid *fid = NULL, *dfid = NULL;
	struct inode *inode;
	gid_t gid;
	struct p9_qid qid;
	struct dentry *dir_dentry;
	struct posix_acl *dacl = NULL, *pacl = NULL;

	P9_DPRINTK(P9_DEBUG_VFS,
		" %lu,%s mode: %x MAJOR: %u MINOR: %u\n", dir->i_ino,
		dentry->d_name.name, omode, MAJOR(rdev), MINOR(rdev));

	if (!new_valid_dev(rdev))
		return -EINVAL;

	v9ses = v9fs_inode2v9ses(dir);
	dir_dentry = v9fs_dentry_from_dir_inode(dir);
	dfid = v9fs_fid_lookup(dir_dentry);
	if (IS_ERR(dfid)) {
		err = PTR_ERR(dfid);
		P9_DPRINTK(P9_DEBUG_VFS, "fid lookup failed %d\n", err);
		dfid = NULL;
		goto error;
	}

	gid = v9fs_get_fsgid_for_create(dir);
	mode = omode;
	/* Update mode based on ACL value */
	err = v9fs_acl_mode(dir, &mode, &dacl, &pacl);
	if (err) {
		P9_DPRINTK(P9_DEBUG_VFS,
			   "Failed to get acl values in mknod %d\n", err);
		goto error;
	}
	name = (char *) dentry->d_name.name;

	err = p9_client_mknod_dotl(dfid, name, mode, rdev, gid, &qid);
	if (err < 0)
		goto error;

	/* instantiate inode and assign the unopened fid to the dentry */
	if (v9ses->cache == CACHE_LOOSE || v9ses->cache == CACHE_FSCACHE) {
		fid = p9_client_walk(dfid, 1, &name, 1);
		if (IS_ERR(fid)) {
			err = PTR_ERR(fid);
			P9_DPRINTK(P9_DEBUG_VFS, "p9_client_walk failed %d\n",
				err);
			fid = NULL;
			goto error;
		}

		inode = v9fs_inode_from_fid(v9ses, fid, dir->i_sb);
		if (IS_ERR(inode)) {
			err = PTR_ERR(inode);
			P9_DPRINTK(P9_DEBUG_VFS, "inode creation failed %d\n",
				err);
			goto error;
		}
		d_set_d_op(dentry, &v9fs_cached_dentry_operations);
		d_instantiate(dentry, inode);
		err = v9fs_fid_add(dentry, fid);
		if (err < 0)
			goto error;
		fid = NULL;
	} else {
		/*
		 * Not in cached mode. No need to populate inode with stat.
		 * socket syscall returns a fd, so we need instantiate
		 */
		inode = v9fs_get_inode(dir->i_sb, mode);
		if (IS_ERR(inode)) {
			err = PTR_ERR(inode);
			goto error;
		}
		d_set_d_op(dentry, &v9fs_dentry_operations);
		d_instantiate(dentry, inode);
	}
	/* Now set the ACL based on the default value */
	v9fs_set_create_acl(dentry, dacl, pacl);
error:
	if (fid)
		p9_client_clunk(fid);
	return err;
}

/**
 * v9fs_vfs_follow_link_dotl - follow a symlink path
 * @dentry: dentry for symlink
 * @nd: nameidata
 *
 */

static void *
v9fs_vfs_follow_link_dotl(struct dentry *dentry, struct nameidata *nd)
{
	int retval;
	struct p9_fid *fid;
	char *link = __getname();
	char *target;

	P9_DPRINTK(P9_DEBUG_VFS, "%s\n", dentry->d_name.name);

	if (!link) {
		link = ERR_PTR(-ENOMEM);
		goto ndset;
	}
	fid = v9fs_fid_lookup(dentry);
	if (IS_ERR(fid)) {
		__putname(link);
		link = ERR_PTR(PTR_ERR(fid));
		goto ndset;
	}
	retval = p9_client_readlink(fid, &target);
	if (!retval) {
		strcpy(link, target);
		kfree(target);
		goto ndset;
	}
	__putname(link);
	link = ERR_PTR(retval);
ndset:
	nd_set_link(nd, link);
	return NULL;
}

const struct inode_operations v9fs_dir_inode_operations_dotl = {
	.create = v9fs_vfs_create_dotl,
	.lookup = v9fs_vfs_lookup,
	.link = v9fs_vfs_link_dotl,
	.symlink = v9fs_vfs_symlink_dotl,
	.unlink = v9fs_vfs_unlink,
	.mkdir = v9fs_vfs_mkdir_dotl,
	.rmdir = v9fs_vfs_rmdir,
	.mknod = v9fs_vfs_mknod_dotl,
	.rename = v9fs_vfs_rename,
	.getattr = v9fs_vfs_getattr_dotl,
	.setattr = v9fs_vfs_setattr_dotl,
	.setxattr = generic_setxattr,
	.getxattr = generic_getxattr,
	.removexattr = generic_removexattr,
	.listxattr = v9fs_listxattr,
	.check_acl = v9fs_check_acl,
};

const struct inode_operations v9fs_file_inode_operations_dotl = {
	.getattr = v9fs_vfs_getattr_dotl,
	.setattr = v9fs_vfs_setattr_dotl,
	.setxattr = generic_setxattr,
	.getxattr = generic_getxattr,
	.removexattr = generic_removexattr,
	.listxattr = v9fs_listxattr,
	.check_acl = v9fs_check_acl,
};

const struct inode_operations v9fs_symlink_inode_operations_dotl = {
	.readlink = generic_readlink,
	.follow_link = v9fs_vfs_follow_link_dotl,
	.put_link = v9fs_vfs_put_link,
	.getattr = v9fs_vfs_getattr_dotl,
	.setattr = v9fs_vfs_setattr_dotl,
	.setxattr = generic_setxattr,
	.getxattr = generic_getxattr,
	.removexattr = generic_removexattr,
	.listxattr = v9fs_listxattr,
};
