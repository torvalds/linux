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
v9fs_vfs_mknod_dotl(struct inode *dir, struct dentry *dentry, umode_t omode,
		    dev_t rdev);

/**
 * v9fs_get_fsgid_for_create - Helper function to get the gid for creating a
 * new file system object. This checks the S_ISGID to determine the owning
 * group of the new file system object.
 */

static kgid_t v9fs_get_fsgid_for_create(struct inode *dir_inode)
{
	BUG_ON(dir_inode == NULL);

	if (dir_inode->i_mode & S_ISGID) {
		/* set_gid bit is set.*/
		return dir_inode->i_gid;
	}
	return current_fsgid();
}

static int v9fs_test_inode_dotl(struct inode *inode, void *data)
{
	struct v9fs_inode *v9inode = V9FS_I(inode);
	struct p9_stat_dotl *st = (struct p9_stat_dotl *)data;

	/* don't match inode of different type */
	if ((inode->i_mode & S_IFMT) != (st->st_mode & S_IFMT))
		return 0;

	if (inode->i_generation != st->st_gen)
		return 0;

	/* compare qid details */
	if (memcmp(&v9inode->qid.version,
		   &st->qid.version, sizeof(v9inode->qid.version)))
		return 0;

	if (v9inode->qid.type != st->qid.type)
		return 0;
	return 1;
}

/* Always get a new inode */
static int v9fs_test_new_inode_dotl(struct inode *inode, void *data)
{
	return 0;
}

static int v9fs_set_inode_dotl(struct inode *inode,  void *data)
{
	struct v9fs_inode *v9inode = V9FS_I(inode);
	struct p9_stat_dotl *st = (struct p9_stat_dotl *)data;

	memcpy(&v9inode->qid, &st->qid, sizeof(st->qid));
	inode->i_generation = st->st_gen;
	return 0;
}

static struct inode *v9fs_qid_iget_dotl(struct super_block *sb,
					struct p9_qid *qid,
					struct p9_fid *fid,
					struct p9_stat_dotl *st,
					int new)
{
	int retval;
	unsigned long i_ino;
	struct inode *inode;
	struct v9fs_session_info *v9ses = sb->s_fs_info;
	int (*test)(struct inode *, void *);

	if (new)
		test = v9fs_test_new_inode_dotl;
	else
		test = v9fs_test_inode_dotl;

	i_ino = v9fs_qid2ino(qid);
	inode = iget5_locked(sb, i_ino, test, v9fs_set_inode_dotl, st);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;
	/*
	 * initialize the inode with the stat info
	 * FIXME!! we may need support for stale inodes
	 * later.
	 */
	inode->i_ino = i_ino;
	retval = v9fs_init_inode(v9ses, inode,
				 st->st_mode, new_decode_dev(st->st_rdev));
	if (retval)
		goto error;

	v9fs_stat2inode_dotl(st, inode);
	v9fs_cache_inode_get_cookie(inode);
	retval = v9fs_get_acl(inode, fid);
	if (retval)
		goto error;

	unlock_new_inode(inode);
	return inode;
error:
	iget_failed(inode);
	return ERR_PTR(retval);

}

struct inode *
v9fs_inode_from_fid_dotl(struct v9fs_session_info *v9ses, struct p9_fid *fid,
			 struct super_block *sb, int new)
{
	struct p9_stat_dotl *st;
	struct inode *inode = NULL;

	st = p9_client_getattr_dotl(fid, P9_STATS_BASIC | P9_STATS_GEN);
	if (IS_ERR(st))
		return ERR_CAST(st);

	inode = v9fs_qid_iget_dotl(sb, &st->qid, fid, st, new);
	kfree(st);
	return inode;
}

struct dotl_openflag_map {
	int open_flag;
	int dotl_flag;
};

static int v9fs_mapped_dotl_flags(int flags)
{
	int i;
	int rflags = 0;
	struct dotl_openflag_map dotl_oflag_map[] = {
		{ O_CREAT,	P9_DOTL_CREATE },
		{ O_EXCL,	P9_DOTL_EXCL },
		{ O_NOCTTY,	P9_DOTL_NOCTTY },
		{ O_APPEND,	P9_DOTL_APPEND },
		{ O_NONBLOCK,	P9_DOTL_NONBLOCK },
		{ O_DSYNC,	P9_DOTL_DSYNC },
		{ FASYNC,	P9_DOTL_FASYNC },
		{ O_DIRECT,	P9_DOTL_DIRECT },
		{ O_LARGEFILE,	P9_DOTL_LARGEFILE },
		{ O_DIRECTORY,	P9_DOTL_DIRECTORY },
		{ O_NOFOLLOW,	P9_DOTL_NOFOLLOW },
		{ O_NOATIME,	P9_DOTL_NOATIME },
		{ O_CLOEXEC,	P9_DOTL_CLOEXEC },
		{ O_SYNC,	P9_DOTL_SYNC},
	};
	for (i = 0; i < ARRAY_SIZE(dotl_oflag_map); i++) {
		if (flags & dotl_oflag_map[i].open_flag)
			rflags |= dotl_oflag_map[i].dotl_flag;
	}
	return rflags;
}

/**
 * v9fs_open_to_dotl_flags- convert Linux specific open flags to
 * plan 9 open flag.
 * @flags: flags to convert
 */
int v9fs_open_to_dotl_flags(int flags)
{
	int rflags = 0;

	/*
	 * We have same bits for P9_DOTL_READONLY, P9_DOTL_WRONLY
	 * and P9_DOTL_NOACCESS
	 */
	rflags |= flags & O_ACCMODE;
	rflags |= v9fs_mapped_dotl_flags(flags);

	return rflags;
}

/**
 * v9fs_vfs_create_dotl - VFS hook to create files for 9P2000.L protocol.
 * @dir: directory inode that is being created
 * @dentry:  dentry that is being deleted
 * @omode: create permissions
 *
 */

static int
v9fs_vfs_create_dotl(struct inode *dir, struct dentry *dentry, umode_t omode,
		bool excl)
{
	return v9fs_vfs_mknod_dotl(dir, dentry, omode, 0);
}

static int
v9fs_vfs_atomic_open_dotl(struct inode *dir, struct dentry *dentry,
			  struct file *file, unsigned flags, umode_t omode,
			  int *opened)
{
	int err = 0;
	kgid_t gid;
	umode_t mode;
	char *name = NULL;
	struct p9_qid qid;
	struct inode *inode;
	struct p9_fid *fid = NULL;
	struct v9fs_inode *v9inode;
	struct p9_fid *dfid, *ofid, *inode_fid;
	struct v9fs_session_info *v9ses;
	struct posix_acl *pacl = NULL, *dacl = NULL;
	struct dentry *res = NULL;

	if (d_unhashed(dentry)) {
		res = v9fs_vfs_lookup(dir, dentry, 0);
		if (IS_ERR(res))
			return PTR_ERR(res);

		if (res)
			dentry = res;
	}

	/* Only creates */
	if (!(flags & O_CREAT) || d_really_is_positive(dentry))
		return	finish_no_open(file, res);

	v9ses = v9fs_inode2v9ses(dir);

	name = (char *) dentry->d_name.name;
	p9_debug(P9_DEBUG_VFS, "name:%s flags:0x%x mode:0x%hx\n",
		 name, flags, omode);

	dfid = v9fs_parent_fid(dentry);
	if (IS_ERR(dfid)) {
		err = PTR_ERR(dfid);
		p9_debug(P9_DEBUG_VFS, "fid lookup failed %d\n", err);
		goto out;
	}

	/* clone a fid to use for creation */
	ofid = p9_client_walk(dfid, 0, NULL, 1);
	if (IS_ERR(ofid)) {
		err = PTR_ERR(ofid);
		p9_debug(P9_DEBUG_VFS, "p9_client_walk failed %d\n", err);
		goto out;
	}

	gid = v9fs_get_fsgid_for_create(dir);

	mode = omode;
	/* Update mode based on ACL value */
	err = v9fs_acl_mode(dir, &mode, &dacl, &pacl);
	if (err) {
		p9_debug(P9_DEBUG_VFS, "Failed to get acl values in creat %d\n",
			 err);
		goto error;
	}
	err = p9_client_create_dotl(ofid, name, v9fs_open_to_dotl_flags(flags),
				    mode, gid, &qid);
	if (err < 0) {
		p9_debug(P9_DEBUG_VFS, "p9_client_open_dotl failed in creat %d\n",
			 err);
		goto error;
	}
	v9fs_invalidate_inode_attr(dir);

	/* instantiate inode and assign the unopened fid to the dentry */
	fid = p9_client_walk(dfid, 1, &name, 1);
	if (IS_ERR(fid)) {
		err = PTR_ERR(fid);
		p9_debug(P9_DEBUG_VFS, "p9_client_walk failed %d\n", err);
		fid = NULL;
		goto error;
	}
	inode = v9fs_get_new_inode_from_fid(v9ses, fid, dir->i_sb);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		p9_debug(P9_DEBUG_VFS, "inode creation failed %d\n", err);
		goto error;
	}
	/* Now set the ACL based on the default value */
	v9fs_set_create_acl(inode, fid, dacl, pacl);

	v9fs_fid_add(dentry, fid);
	d_instantiate(dentry, inode);

	v9inode = V9FS_I(inode);
	mutex_lock(&v9inode->v_mutex);
	if ((v9ses->cache == CACHE_LOOSE || v9ses->cache == CACHE_FSCACHE) &&
	    !v9inode->writeback_fid &&
	    ((flags & O_ACCMODE) != O_RDONLY)) {
		/*
		 * clone a fid and add it to writeback_fid
		 * we do it during open time instead of
		 * page dirty time via write_begin/page_mkwrite
		 * because we want write after unlink usecase
		 * to work.
		 */
		inode_fid = v9fs_writeback_fid(dentry);
		if (IS_ERR(inode_fid)) {
			err = PTR_ERR(inode_fid);
			mutex_unlock(&v9inode->v_mutex);
			goto err_clunk_old_fid;
		}
		v9inode->writeback_fid = (void *) inode_fid;
	}
	mutex_unlock(&v9inode->v_mutex);
	/* Since we are opening a file, assign the open fid to the file */
	err = finish_open(file, dentry, generic_file_open, opened);
	if (err)
		goto err_clunk_old_fid;
	file->private_data = ofid;
	if (v9ses->cache == CACHE_LOOSE || v9ses->cache == CACHE_FSCACHE)
		v9fs_cache_inode_set_cookie(inode, file);
	*opened |= FILE_CREATED;
out:
	v9fs_put_acl(dacl, pacl);
	dput(res);
	return err;

error:
	if (fid)
		p9_client_clunk(fid);
err_clunk_old_fid:
	if (ofid)
		p9_client_clunk(ofid);
	goto out;
}

/**
 * v9fs_vfs_mkdir_dotl - VFS mkdir hook to create a directory
 * @dir:  inode that is being unlinked
 * @dentry: dentry that is being unlinked
 * @omode: mode for new directory
 *
 */

static int v9fs_vfs_mkdir_dotl(struct inode *dir,
			       struct dentry *dentry, umode_t omode)
{
	int err;
	struct v9fs_session_info *v9ses;
	struct p9_fid *fid = NULL, *dfid = NULL;
	kgid_t gid;
	char *name;
	umode_t mode;
	struct inode *inode;
	struct p9_qid qid;
	struct posix_acl *dacl = NULL, *pacl = NULL;

	p9_debug(P9_DEBUG_VFS, "name %pd\n", dentry);
	err = 0;
	v9ses = v9fs_inode2v9ses(dir);

	omode |= S_IFDIR;
	if (dir->i_mode & S_ISGID)
		omode |= S_ISGID;

	dfid = v9fs_parent_fid(dentry);
	if (IS_ERR(dfid)) {
		err = PTR_ERR(dfid);
		p9_debug(P9_DEBUG_VFS, "fid lookup failed %d\n", err);
		dfid = NULL;
		goto error;
	}

	gid = v9fs_get_fsgid_for_create(dir);
	mode = omode;
	/* Update mode based on ACL value */
	err = v9fs_acl_mode(dir, &mode, &dacl, &pacl);
	if (err) {
		p9_debug(P9_DEBUG_VFS, "Failed to get acl values in mkdir %d\n",
			 err);
		goto error;
	}
	name = (char *) dentry->d_name.name;
	err = p9_client_mkdir_dotl(dfid, name, mode, gid, &qid);
	if (err < 0)
		goto error;

	fid = p9_client_walk(dfid, 1, &name, 1);
	if (IS_ERR(fid)) {
		err = PTR_ERR(fid);
		p9_debug(P9_DEBUG_VFS, "p9_client_walk failed %d\n",
			 err);
		fid = NULL;
		goto error;
	}

	/* instantiate inode and assign the unopened fid to the dentry */
	if (v9ses->cache == CACHE_LOOSE || v9ses->cache == CACHE_FSCACHE) {
		inode = v9fs_get_new_inode_from_fid(v9ses, fid, dir->i_sb);
		if (IS_ERR(inode)) {
			err = PTR_ERR(inode);
			p9_debug(P9_DEBUG_VFS, "inode creation failed %d\n",
				 err);
			goto error;
		}
		v9fs_fid_add(dentry, fid);
		v9fs_set_create_acl(inode, fid, dacl, pacl);
		d_instantiate(dentry, inode);
		fid = NULL;
		err = 0;
	} else {
		/*
		 * Not in cached mode. No need to populate
		 * inode with stat. We need to get an inode
		 * so that we can set the acl with dentry
		 */
		inode = v9fs_get_inode(dir->i_sb, mode, 0);
		if (IS_ERR(inode)) {
			err = PTR_ERR(inode);
			goto error;
		}
		v9fs_set_create_acl(inode, fid, dacl, pacl);
		d_instantiate(dentry, inode);
	}
	inc_nlink(dir);
	v9fs_invalidate_inode_attr(dir);
error:
	if (fid)
		p9_client_clunk(fid);
	v9fs_put_acl(dacl, pacl);
	return err;
}

static int
v9fs_vfs_getattr_dotl(struct vfsmount *mnt, struct dentry *dentry,
		 struct kstat *stat)
{
	struct v9fs_session_info *v9ses;
	struct p9_fid *fid;
	struct p9_stat_dotl *st;

	p9_debug(P9_DEBUG_VFS, "dentry: %p\n", dentry);
	v9ses = v9fs_dentry2v9ses(dentry);
	if (v9ses->cache == CACHE_LOOSE || v9ses->cache == CACHE_FSCACHE) {
		generic_fillattr(d_inode(dentry), stat);
		return 0;
	}
	fid = v9fs_fid_lookup(dentry);
	if (IS_ERR(fid))
		return PTR_ERR(fid);

	/* Ask for all the fields in stat structure. Server will return
	 * whatever it supports
	 */

	st = p9_client_getattr_dotl(fid, P9_STATS_ALL);
	if (IS_ERR(st))
		return PTR_ERR(st);

	v9fs_stat2inode_dotl(st, d_inode(dentry));
	generic_fillattr(d_inode(dentry), stat);
	/* Change block size to what the server returned */
	stat->blksize = st->st_blksize;

	kfree(st);
	return 0;
}

/*
 * Attribute flags.
 */
#define P9_ATTR_MODE		(1 << 0)
#define P9_ATTR_UID		(1 << 1)
#define P9_ATTR_GID		(1 << 2)
#define P9_ATTR_SIZE		(1 << 3)
#define P9_ATTR_ATIME		(1 << 4)
#define P9_ATTR_MTIME		(1 << 5)
#define P9_ATTR_CTIME		(1 << 6)
#define P9_ATTR_ATIME_SET	(1 << 7)
#define P9_ATTR_MTIME_SET	(1 << 8)

struct dotl_iattr_map {
	int iattr_valid;
	int p9_iattr_valid;
};

static int v9fs_mapped_iattr_valid(int iattr_valid)
{
	int i;
	int p9_iattr_valid = 0;
	struct dotl_iattr_map dotl_iattr_map[] = {
		{ ATTR_MODE,		P9_ATTR_MODE },
		{ ATTR_UID,		P9_ATTR_UID },
		{ ATTR_GID,		P9_ATTR_GID },
		{ ATTR_SIZE,		P9_ATTR_SIZE },
		{ ATTR_ATIME,		P9_ATTR_ATIME },
		{ ATTR_MTIME,		P9_ATTR_MTIME },
		{ ATTR_CTIME,		P9_ATTR_CTIME },
		{ ATTR_ATIME_SET,	P9_ATTR_ATIME_SET },
		{ ATTR_MTIME_SET,	P9_ATTR_MTIME_SET },
	};
	for (i = 0; i < ARRAY_SIZE(dotl_iattr_map); i++) {
		if (iattr_valid & dotl_iattr_map[i].iattr_valid)
			p9_iattr_valid |= dotl_iattr_map[i].p9_iattr_valid;
	}
	return p9_iattr_valid;
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
	struct p9_fid *fid;
	struct p9_iattr_dotl p9attr;
	struct inode *inode = d_inode(dentry);

	p9_debug(P9_DEBUG_VFS, "\n");

	retval = inode_change_ok(inode, iattr);
	if (retval)
		return retval;

	p9attr.valid = v9fs_mapped_iattr_valid(iattr->ia_valid);
	p9attr.mode = iattr->ia_mode;
	p9attr.uid = iattr->ia_uid;
	p9attr.gid = iattr->ia_gid;
	p9attr.size = iattr->ia_size;
	p9attr.atime_sec = iattr->ia_atime.tv_sec;
	p9attr.atime_nsec = iattr->ia_atime.tv_nsec;
	p9attr.mtime_sec = iattr->ia_mtime.tv_sec;
	p9attr.mtime_nsec = iattr->ia_mtime.tv_nsec;

	fid = v9fs_fid_lookup(dentry);
	if (IS_ERR(fid))
		return PTR_ERR(fid);

	/* Write all dirty data */
	if (S_ISREG(inode->i_mode))
		filemap_write_and_wait(inode->i_mapping);

	retval = p9_client_setattr(fid, &p9attr);
	if (retval < 0)
		return retval;

	if ((iattr->ia_valid & ATTR_SIZE) &&
	    iattr->ia_size != i_size_read(inode))
		truncate_setsize(inode, iattr->ia_size);

	v9fs_invalidate_inode_attr(inode);
	setattr_copy(inode, iattr);
	mark_inode_dirty(inode);
	if (iattr->ia_valid & ATTR_MODE) {
		/* We also want to update ACL when we update mode bits */
		retval = v9fs_acl_chmod(inode, fid);
		if (retval < 0)
			return retval;
	}
	return 0;
}

/**
 * v9fs_stat2inode_dotl - populate an inode structure with stat info
 * @stat: stat structure
 * @inode: inode to populate
 *
 */

void
v9fs_stat2inode_dotl(struct p9_stat_dotl *stat, struct inode *inode)
{
	umode_t mode;
	struct v9fs_inode *v9inode = V9FS_I(inode);

	if ((stat->st_result_mask & P9_STATS_BASIC) == P9_STATS_BASIC) {
		inode->i_atime.tv_sec = stat->st_atime_sec;
		inode->i_atime.tv_nsec = stat->st_atime_nsec;
		inode->i_mtime.tv_sec = stat->st_mtime_sec;
		inode->i_mtime.tv_nsec = stat->st_mtime_nsec;
		inode->i_ctime.tv_sec = stat->st_ctime_sec;
		inode->i_ctime.tv_nsec = stat->st_ctime_nsec;
		inode->i_uid = stat->st_uid;
		inode->i_gid = stat->st_gid;
		set_nlink(inode, stat->st_nlink);

		mode = stat->st_mode & S_IALLUGO;
		mode |= inode->i_mode & ~S_IALLUGO;
		inode->i_mode = mode;

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
			set_nlink(inode, stat->st_nlink);
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
	v9inode->cache_validity &= ~V9FS_INO_INVALID_ATTR;
}

static int
v9fs_vfs_symlink_dotl(struct inode *dir, struct dentry *dentry,
		const char *symname)
{
	int err;
	kgid_t gid;
	char *name;
	struct p9_qid qid;
	struct inode *inode;
	struct p9_fid *dfid;
	struct p9_fid *fid = NULL;
	struct v9fs_session_info *v9ses;

	name = (char *) dentry->d_name.name;
	p9_debug(P9_DEBUG_VFS, "%lu,%s,%s\n", dir->i_ino, name, symname);
	v9ses = v9fs_inode2v9ses(dir);

	dfid = v9fs_parent_fid(dentry);
	if (IS_ERR(dfid)) {
		err = PTR_ERR(dfid);
		p9_debug(P9_DEBUG_VFS, "fid lookup failed %d\n", err);
		return err;
	}

	gid = v9fs_get_fsgid_for_create(dir);

	/* Server doesn't alter fid on TSYMLINK. Hence no need to clone it. */
	err = p9_client_symlink(dfid, name, (char *)symname, gid, &qid);

	if (err < 0) {
		p9_debug(P9_DEBUG_VFS, "p9_client_symlink failed %d\n", err);
		goto error;
	}

	v9fs_invalidate_inode_attr(dir);
	if (v9ses->cache == CACHE_LOOSE || v9ses->cache == CACHE_FSCACHE) {
		/* Now walk from the parent so we can get an unopened fid. */
		fid = p9_client_walk(dfid, 1, &name, 1);
		if (IS_ERR(fid)) {
			err = PTR_ERR(fid);
			p9_debug(P9_DEBUG_VFS, "p9_client_walk failed %d\n",
				 err);
			fid = NULL;
			goto error;
		}

		/* instantiate inode and assign the unopened fid to dentry */
		inode = v9fs_get_new_inode_from_fid(v9ses, fid, dir->i_sb);
		if (IS_ERR(inode)) {
			err = PTR_ERR(inode);
			p9_debug(P9_DEBUG_VFS, "inode creation failed %d\n",
				 err);
			goto error;
		}
		v9fs_fid_add(dentry, fid);
		d_instantiate(dentry, inode);
		fid = NULL;
		err = 0;
	} else {
		/* Not in cached mode. No need to populate inode with stat */
		inode = v9fs_get_inode(dir->i_sb, S_IFLNK, 0);
		if (IS_ERR(inode)) {
			err = PTR_ERR(inode);
			goto error;
		}
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
	struct v9fs_session_info *v9ses;

	p9_debug(P9_DEBUG_VFS, "dir ino: %lu, old_name: %pd, new_name: %pd\n",
		 dir->i_ino, old_dentry, dentry);

	v9ses = v9fs_inode2v9ses(dir);
	dfid = v9fs_parent_fid(dentry);
	if (IS_ERR(dfid))
		return PTR_ERR(dfid);

	oldfid = v9fs_fid_lookup(old_dentry);
	if (IS_ERR(oldfid))
		return PTR_ERR(oldfid);

	err = p9_client_link(dfid, oldfid, (char *)dentry->d_name.name);

	if (err < 0) {
		p9_debug(P9_DEBUG_VFS, "p9_client_link failed %d\n", err);
		return err;
	}

	v9fs_invalidate_inode_attr(dir);
	if (v9ses->cache == CACHE_LOOSE || v9ses->cache == CACHE_FSCACHE) {
		/* Get the latest stat info from server. */
		struct p9_fid *fid;
		fid = v9fs_fid_lookup(old_dentry);
		if (IS_ERR(fid))
			return PTR_ERR(fid);

		v9fs_refresh_inode_dotl(fid, d_inode(old_dentry));
	}
	ihold(d_inode(old_dentry));
	d_instantiate(dentry, d_inode(old_dentry));

	return err;
}

/**
 * v9fs_vfs_mknod_dotl - create a special file
 * @dir: inode destination for new link
 * @dentry: dentry for file
 * @omode: mode for creation
 * @rdev: device associated with special file
 *
 */
static int
v9fs_vfs_mknod_dotl(struct inode *dir, struct dentry *dentry, umode_t omode,
		dev_t rdev)
{
	int err;
	kgid_t gid;
	char *name;
	umode_t mode;
	struct v9fs_session_info *v9ses;
	struct p9_fid *fid = NULL, *dfid = NULL;
	struct inode *inode;
	struct p9_qid qid;
	struct posix_acl *dacl = NULL, *pacl = NULL;

	p9_debug(P9_DEBUG_VFS, " %lu,%pd mode: %hx MAJOR: %u MINOR: %u\n",
		 dir->i_ino, dentry, omode,
		 MAJOR(rdev), MINOR(rdev));

	v9ses = v9fs_inode2v9ses(dir);
	dfid = v9fs_parent_fid(dentry);
	if (IS_ERR(dfid)) {
		err = PTR_ERR(dfid);
		p9_debug(P9_DEBUG_VFS, "fid lookup failed %d\n", err);
		dfid = NULL;
		goto error;
	}

	gid = v9fs_get_fsgid_for_create(dir);
	mode = omode;
	/* Update mode based on ACL value */
	err = v9fs_acl_mode(dir, &mode, &dacl, &pacl);
	if (err) {
		p9_debug(P9_DEBUG_VFS, "Failed to get acl values in mknod %d\n",
			 err);
		goto error;
	}
	name = (char *) dentry->d_name.name;

	err = p9_client_mknod_dotl(dfid, name, mode, rdev, gid, &qid);
	if (err < 0)
		goto error;

	v9fs_invalidate_inode_attr(dir);
	fid = p9_client_walk(dfid, 1, &name, 1);
	if (IS_ERR(fid)) {
		err = PTR_ERR(fid);
		p9_debug(P9_DEBUG_VFS, "p9_client_walk failed %d\n",
			 err);
		fid = NULL;
		goto error;
	}

	/* instantiate inode and assign the unopened fid to the dentry */
	if (v9ses->cache == CACHE_LOOSE || v9ses->cache == CACHE_FSCACHE) {
		inode = v9fs_get_new_inode_from_fid(v9ses, fid, dir->i_sb);
		if (IS_ERR(inode)) {
			err = PTR_ERR(inode);
			p9_debug(P9_DEBUG_VFS, "inode creation failed %d\n",
				 err);
			goto error;
		}
		v9fs_set_create_acl(inode, fid, dacl, pacl);
		v9fs_fid_add(dentry, fid);
		d_instantiate(dentry, inode);
		fid = NULL;
		err = 0;
	} else {
		/*
		 * Not in cached mode. No need to populate inode with stat.
		 * socket syscall returns a fd, so we need instantiate
		 */
		inode = v9fs_get_inode(dir->i_sb, mode, rdev);
		if (IS_ERR(inode)) {
			err = PTR_ERR(inode);
			goto error;
		}
		v9fs_set_create_acl(inode, fid, dacl, pacl);
		d_instantiate(dentry, inode);
	}
error:
	if (fid)
		p9_client_clunk(fid);
	v9fs_put_acl(dacl, pacl);
	return err;
}

/**
 * v9fs_vfs_get_link_dotl - follow a symlink path
 * @dentry: dentry for symlink
 * @inode: inode for symlink
 * @done: destructor for return value
 */

static const char *
v9fs_vfs_get_link_dotl(struct dentry *dentry,
		       struct inode *inode,
		       struct delayed_call *done)
{
	struct p9_fid *fid;
	char *target;
	int retval;

	if (!dentry)
		return ERR_PTR(-ECHILD);

	p9_debug(P9_DEBUG_VFS, "%pd\n", dentry);

	fid = v9fs_fid_lookup(dentry);
	if (IS_ERR(fid))
		return ERR_CAST(fid);
	retval = p9_client_readlink(fid, &target);
	if (retval)
		return ERR_PTR(retval);
	set_delayed_call(done, kfree_link, target);
	return target;
}

int v9fs_refresh_inode_dotl(struct p9_fid *fid, struct inode *inode)
{
	loff_t i_size;
	struct p9_stat_dotl *st;
	struct v9fs_session_info *v9ses;

	v9ses = v9fs_inode2v9ses(inode);
	st = p9_client_getattr_dotl(fid, P9_STATS_ALL);
	if (IS_ERR(st))
		return PTR_ERR(st);
	/*
	 * Don't update inode if the file type is different
	 */
	if ((inode->i_mode & S_IFMT) != (st->st_mode & S_IFMT))
		goto out;

	spin_lock(&inode->i_lock);
	/*
	 * We don't want to refresh inode->i_size,
	 * because we may have cached data
	 */
	i_size = inode->i_size;
	v9fs_stat2inode_dotl(st, inode);
	if (v9ses->cache == CACHE_LOOSE || v9ses->cache == CACHE_FSCACHE)
		inode->i_size = i_size;
	spin_unlock(&inode->i_lock);
out:
	kfree(st);
	return 0;
}

const struct inode_operations v9fs_dir_inode_operations_dotl = {
	.create = v9fs_vfs_create_dotl,
	.atomic_open = v9fs_vfs_atomic_open_dotl,
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
	.get_acl = v9fs_iop_get_acl,
};

const struct inode_operations v9fs_file_inode_operations_dotl = {
	.getattr = v9fs_vfs_getattr_dotl,
	.setattr = v9fs_vfs_setattr_dotl,
	.setxattr = generic_setxattr,
	.getxattr = generic_getxattr,
	.removexattr = generic_removexattr,
	.listxattr = v9fs_listxattr,
	.get_acl = v9fs_iop_get_acl,
};

const struct inode_operations v9fs_symlink_inode_operations_dotl = {
	.readlink = generic_readlink,
	.get_link = v9fs_vfs_get_link_dotl,
	.getattr = v9fs_vfs_getattr_dotl,
	.setattr = v9fs_vfs_setattr_dotl,
	.setxattr = generic_setxattr,
	.getxattr = generic_getxattr,
	.removexattr = generic_removexattr,
	.listxattr = v9fs_listxattr,
};
