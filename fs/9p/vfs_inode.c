/*
 *  linux/fs/9p/vfs_inode.c
 *
 * This file contains vfs inode ops for the 9P2000 protocol.
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
#include <net/9p/9p.h>
#include <net/9p/client.h>

#include "v9fs.h"
#include "v9fs_vfs.h"
#include "fid.h"

static const struct inode_operations v9fs_dir_inode_operations;
static const struct inode_operations v9fs_dir_inode_operations_ext;
static const struct inode_operations v9fs_file_inode_operations;
static const struct inode_operations v9fs_symlink_inode_operations;

/**
 * unixmode2p9mode - convert unix mode bits to plan 9
 * @v9ses: v9fs session information
 * @mode: mode to convert
 *
 */

static int unixmode2p9mode(struct v9fs_session_info *v9ses, int mode)
{
	int res;
	res = mode & 0777;
	if (S_ISDIR(mode))
		res |= P9_DMDIR;
	if (v9fs_extended(v9ses)) {
		if (S_ISLNK(mode))
			res |= P9_DMSYMLINK;
		if (v9ses->nodev == 0) {
			if (S_ISSOCK(mode))
				res |= P9_DMSOCKET;
			if (S_ISFIFO(mode))
				res |= P9_DMNAMEDPIPE;
			if (S_ISBLK(mode))
				res |= P9_DMDEVICE;
			if (S_ISCHR(mode))
				res |= P9_DMDEVICE;
		}

		if ((mode & S_ISUID) == S_ISUID)
			res |= P9_DMSETUID;
		if ((mode & S_ISGID) == S_ISGID)
			res |= P9_DMSETGID;
		if ((mode & S_ISVTX) == S_ISVTX)
			res |= P9_DMSETVTX;
		if ((mode & P9_DMLINK))
			res |= P9_DMLINK;
	}

	return res;
}

/**
 * p9mode2unixmode- convert plan9 mode bits to unix mode bits
 * @v9ses: v9fs session information
 * @mode: mode to convert
 *
 */

static int p9mode2unixmode(struct v9fs_session_info *v9ses, int mode)
{
	int res;

	res = mode & 0777;

	if ((mode & P9_DMDIR) == P9_DMDIR)
		res |= S_IFDIR;
	else if ((mode & P9_DMSYMLINK) && (v9fs_extended(v9ses)))
		res |= S_IFLNK;
	else if ((mode & P9_DMSOCKET) && (v9fs_extended(v9ses))
		 && (v9ses->nodev == 0))
		res |= S_IFSOCK;
	else if ((mode & P9_DMNAMEDPIPE) && (v9fs_extended(v9ses))
		 && (v9ses->nodev == 0))
		res |= S_IFIFO;
	else if ((mode & P9_DMDEVICE) && (v9fs_extended(v9ses))
		 && (v9ses->nodev == 0))
		res |= S_IFBLK;
	else
		res |= S_IFREG;

	if (v9fs_extended(v9ses)) {
		if ((mode & P9_DMSETUID) == P9_DMSETUID)
			res |= S_ISUID;

		if ((mode & P9_DMSETGID) == P9_DMSETGID)
			res |= S_ISGID;

		if ((mode & P9_DMSETVTX) == P9_DMSETVTX)
			res |= S_ISVTX;
	}

	return res;
}

/**
 * v9fs_uflags2omode- convert posix open flags to plan 9 mode bits
 * @uflags: flags to convert
 * @extended: if .u extensions are active
 */

int v9fs_uflags2omode(int uflags, int extended)
{
	int ret;

	ret = 0;
	switch (uflags&3) {
	default:
	case O_RDONLY:
		ret = P9_OREAD;
		break;

	case O_WRONLY:
		ret = P9_OWRITE;
		break;

	case O_RDWR:
		ret = P9_ORDWR;
		break;
	}

	if (uflags & O_TRUNC)
		ret |= P9_OTRUNC;

	if (extended) {
		if (uflags & O_EXCL)
			ret |= P9_OEXCL;

		if (uflags & O_APPEND)
			ret |= P9_OAPPEND;
	}

	return ret;
}

/**
 * v9fs_blank_wstat - helper function to setup a 9P stat structure
 * @wstat: structure to initialize
 *
 */

static void
v9fs_blank_wstat(struct p9_wstat *wstat)
{
	wstat->type = ~0;
	wstat->dev = ~0;
	wstat->qid.type = ~0;
	wstat->qid.version = ~0;
	*((long long *)&wstat->qid.path) = ~0;
	wstat->mode = ~0;
	wstat->atime = ~0;
	wstat->mtime = ~0;
	wstat->length = ~0;
	wstat->name = NULL;
	wstat->uid = NULL;
	wstat->gid = NULL;
	wstat->muid = NULL;
	wstat->n_uid = ~0;
	wstat->n_gid = ~0;
	wstat->n_muid = ~0;
	wstat->extension = NULL;
}

/**
 * v9fs_get_inode - helper function to setup an inode
 * @sb: superblock
 * @mode: mode to setup inode with
 *
 */

struct inode *v9fs_get_inode(struct super_block *sb, int mode)
{
	int err;
	struct inode *inode;
	struct v9fs_session_info *v9ses = sb->s_fs_info;

	P9_DPRINTK(P9_DEBUG_VFS, "super block: %p mode: %o\n", sb, mode);

	inode = new_inode(sb);
	if (!inode) {
		P9_EPRINTK(KERN_WARNING, "Problem allocating inode\n");
		return ERR_PTR(-ENOMEM);
	}

	inode->i_mode = mode;
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();
	inode->i_blocks = 0;
	inode->i_rdev = 0;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_mapping->a_ops = &v9fs_addr_operations;

	switch (mode & S_IFMT) {
	case S_IFIFO:
	case S_IFBLK:
	case S_IFCHR:
	case S_IFSOCK:
		if (!v9fs_extended(v9ses)) {
			P9_DPRINTK(P9_DEBUG_ERROR,
				   "special files without extended mode\n");
			err = -EINVAL;
			goto error;
		}
		init_special_inode(inode, inode->i_mode, inode->i_rdev);
		break;
	case S_IFREG:
		inode->i_op = &v9fs_file_inode_operations;
		inode->i_fop = &v9fs_file_operations;
		break;
	case S_IFLNK:
		if (!v9fs_extended(v9ses)) {
			P9_DPRINTK(P9_DEBUG_ERROR,
				   "extended modes used w/o 9P2000.u\n");
			err = -EINVAL;
			goto error;
		}
		inode->i_op = &v9fs_symlink_inode_operations;
		break;
	case S_IFDIR:
		inc_nlink(inode);
		if (v9fs_extended(v9ses))
			inode->i_op = &v9fs_dir_inode_operations_ext;
		else
			inode->i_op = &v9fs_dir_inode_operations;
		inode->i_fop = &v9fs_dir_operations;
		break;
	default:
		P9_DPRINTK(P9_DEBUG_ERROR, "BAD mode 0x%x S_IFMT 0x%x\n",
			   mode, mode & S_IFMT);
		err = -EINVAL;
		goto error;
	}

	return inode;

error:
	iput(inode);
	return ERR_PTR(err);
}

/*
static struct v9fs_fid*
v9fs_clone_walk(struct v9fs_session_info *v9ses, u32 fid, struct dentry *dentry)
{
	int err;
	int nfid;
	struct v9fs_fid *ret;
	struct v9fs_fcall *fcall;

	nfid = v9fs_get_idpool(&v9ses->fidpool);
	if (nfid < 0) {
		eprintk(KERN_WARNING, "no free fids available\n");
		return ERR_PTR(-ENOSPC);
	}

	err = v9fs_t_walk(v9ses, fid, nfid, (char *) dentry->d_name.name,
		&fcall);

	if (err < 0) {
		if (fcall && fcall->id == RWALK)
			goto clunk_fid;

		PRINT_FCALL_ERROR("walk error", fcall);
		v9fs_put_idpool(nfid, &v9ses->fidpool);
		goto error;
	}

	kfree(fcall);
	fcall = NULL;
	ret = v9fs_fid_create(v9ses, nfid);
	if (!ret) {
		err = -ENOMEM;
		goto clunk_fid;
	}

	err = v9fs_fid_insert(ret, dentry);
	if (err < 0) {
		v9fs_fid_destroy(ret);
		goto clunk_fid;
	}

	return ret;

clunk_fid:
	v9fs_t_clunk(v9ses, nfid);

error:
	kfree(fcall);
	return ERR_PTR(err);
}
*/

/**
 * v9fs_inode_from_fid - populate an inode by issuing a attribute request
 * @v9ses: session information
 * @fid: fid to issue attribute request for
 * @sb: superblock on which to create inode
 *
 */

static struct inode *
v9fs_inode_from_fid(struct v9fs_session_info *v9ses, struct p9_fid *fid,
	struct super_block *sb)
{
	int err, umode;
	struct inode *ret;
	struct p9_wstat *st;

	ret = NULL;
	st = p9_client_stat(fid);
	if (IS_ERR(st))
		return ERR_CAST(st);

	umode = p9mode2unixmode(v9ses, st->mode);
	ret = v9fs_get_inode(sb, umode);
	if (IS_ERR(ret)) {
		err = PTR_ERR(ret);
		goto error;
	}

	v9fs_stat2inode(st, ret, sb);
	ret->i_ino = v9fs_qid2ino(&st->qid);
	p9stat_free(st);
	kfree(st);
	return ret;

error:
	p9stat_free(st);
	kfree(st);
	return ERR_PTR(err);
}

/**
 * v9fs_remove - helper function to remove files and directories
 * @dir: directory inode that is being deleted
 * @file:  dentry that is being deleted
 * @rmdir: removing a directory
 *
 */

static int v9fs_remove(struct inode *dir, struct dentry *file, int rmdir)
{
	struct inode *file_inode;
	struct v9fs_session_info *v9ses;
	struct p9_fid *v9fid;

	P9_DPRINTK(P9_DEBUG_VFS, "inode: %p dentry: %p rmdir: %d\n", dir, file,
		rmdir);

	file_inode = file->d_inode;
	v9ses = v9fs_inode2v9ses(file_inode);
	v9fid = v9fs_fid_clone(file);
	if (IS_ERR(v9fid))
		return PTR_ERR(v9fid);

	return p9_client_remove(v9fid);
}

static int
v9fs_open_created(struct inode *inode, struct file *file)
{
	return 0;
}


/**
 * v9fs_create - Create a file
 * @v9ses: session information
 * @dir: directory that dentry is being created in
 * @dentry:  dentry that is being created
 * @extension: 9p2000.u extension string to support devices, etc.
 * @perm: create permissions
 * @mode: open mode
 *
 */
static struct p9_fid *
v9fs_create(struct v9fs_session_info *v9ses, struct inode *dir,
		struct dentry *dentry, char *extension, u32 perm, u8 mode)
{
	int err;
	char *name;
	struct p9_fid *dfid, *ofid, *fid;
	struct inode *inode;

	P9_DPRINTK(P9_DEBUG_VFS, "name %s\n", dentry->d_name.name);

	err = 0;
	ofid = NULL;
	fid = NULL;
	name = (char *) dentry->d_name.name;
	dfid = v9fs_fid_clone(dentry->d_parent);
	if (IS_ERR(dfid)) {
		err = PTR_ERR(dfid);
		P9_DPRINTK(P9_DEBUG_VFS, "fid clone failed %d\n", err);
		dfid = NULL;
		goto error;
	}

	/* clone a fid to use for creation */
	ofid = p9_client_walk(dfid, 0, NULL, 1);
	if (IS_ERR(ofid)) {
		err = PTR_ERR(ofid);
		P9_DPRINTK(P9_DEBUG_VFS, "p9_client_walk failed %d\n", err);
		ofid = NULL;
		goto error;
	}

	err = p9_client_fcreate(ofid, name, perm, mode, extension);
	if (err < 0) {
		P9_DPRINTK(P9_DEBUG_VFS, "p9_client_fcreate failed %d\n", err);
		goto error;
	}

	/* now walk from the parent so we can get unopened fid */
	fid = p9_client_walk(dfid, 1, &name, 0);
	if (IS_ERR(fid)) {
		err = PTR_ERR(fid);
		P9_DPRINTK(P9_DEBUG_VFS, "p9_client_walk failed %d\n", err);
		fid = NULL;
		goto error;
	} else
		dfid = NULL;

	/* instantiate inode and assign the unopened fid to the dentry */
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

	return ofid;

error:
	if (dfid)
		p9_client_clunk(dfid);

	if (ofid)
		p9_client_clunk(ofid);

	if (fid)
		p9_client_clunk(fid);

	return ERR_PTR(err);
}

/**
 * v9fs_vfs_create - VFS hook to create files
 * @dir: directory inode that is being created
 * @dentry:  dentry that is being deleted
 * @mode: create permissions
 * @nd: path information
 *
 */

static int
v9fs_vfs_create(struct inode *dir, struct dentry *dentry, int mode,
		struct nameidata *nd)
{
	int err;
	u32 perm;
	int flags;
	struct v9fs_session_info *v9ses;
	struct p9_fid *fid;
	struct file *filp;

	err = 0;
	fid = NULL;
	v9ses = v9fs_inode2v9ses(dir);
	perm = unixmode2p9mode(v9ses, mode);
	if (nd && nd->flags & LOOKUP_OPEN)
		flags = nd->intent.open.flags - 1;
	else
		flags = O_RDWR;

	fid = v9fs_create(v9ses, dir, dentry, NULL, perm,
				v9fs_uflags2omode(flags, v9fs_extended(v9ses)));
	if (IS_ERR(fid)) {
		err = PTR_ERR(fid);
		fid = NULL;
		goto error;
	}

	/* if we are opening a file, assign the open fid to the file */
	if (nd && nd->flags & LOOKUP_OPEN) {
		filp = lookup_instantiate_filp(nd, dentry, v9fs_open_created);
		if (IS_ERR(filp)) {
			err = PTR_ERR(filp);
			goto error;
		}

		filp->private_data = fid;
	} else
		p9_client_clunk(fid);

	return 0;

error:
	if (fid)
		p9_client_clunk(fid);

	return err;
}

/**
 * v9fs_vfs_mkdir - VFS mkdir hook to create a directory
 * @dir:  inode that is being unlinked
 * @dentry: dentry that is being unlinked
 * @mode: mode for new directory
 *
 */

static int v9fs_vfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int err;
	u32 perm;
	struct v9fs_session_info *v9ses;
	struct p9_fid *fid;

	P9_DPRINTK(P9_DEBUG_VFS, "name %s\n", dentry->d_name.name);
	err = 0;
	v9ses = v9fs_inode2v9ses(dir);
	perm = unixmode2p9mode(v9ses, mode | S_IFDIR);
	fid = v9fs_create(v9ses, dir, dentry, NULL, perm, P9_OREAD);
	if (IS_ERR(fid)) {
		err = PTR_ERR(fid);
		fid = NULL;
	}

	if (fid)
		p9_client_clunk(fid);

	return err;
}

/**
 * v9fs_vfs_lookup - VFS lookup hook to "walk" to a new inode
 * @dir:  inode that is being walked from
 * @dentry: dentry that is being walked to?
 * @nameidata: path data
 *
 */

static struct dentry *v9fs_vfs_lookup(struct inode *dir, struct dentry *dentry,
				      struct nameidata *nameidata)
{
	struct super_block *sb;
	struct v9fs_session_info *v9ses;
	struct p9_fid *dfid, *fid;
	struct inode *inode;
	char *name;
	int result = 0;

	P9_DPRINTK(P9_DEBUG_VFS, "dir: %p dentry: (%s) %p nameidata: %p\n",
		dir, dentry->d_name.name, dentry, nameidata);

	sb = dir->i_sb;
	v9ses = v9fs_inode2v9ses(dir);
	dfid = v9fs_fid_lookup(dentry->d_parent);
	if (IS_ERR(dfid))
		return ERR_CAST(dfid);

	name = (char *) dentry->d_name.name;
	fid = p9_client_walk(dfid, 1, &name, 1);
	if (IS_ERR(fid)) {
		result = PTR_ERR(fid);
		if (result == -ENOENT) {
			d_add(dentry, NULL);
			return NULL;
		}

		return ERR_PTR(result);
	}

	inode = v9fs_inode_from_fid(v9ses, fid, dir->i_sb);
	if (IS_ERR(inode)) {
		result = PTR_ERR(inode);
		inode = NULL;
		goto error;
	}

	result = v9fs_fid_add(dentry, fid);
	if (result < 0)
		goto error;

	if ((fid->qid.version) && (v9ses->cache))
		dentry->d_op = &v9fs_cached_dentry_operations;
	else
		dentry->d_op = &v9fs_dentry_operations;

	d_add(dentry, inode);
	return NULL;

error:
	p9_client_clunk(fid);

	return ERR_PTR(result);
}

/**
 * v9fs_vfs_unlink - VFS unlink hook to delete an inode
 * @i:  inode that is being unlinked
 * @d: dentry that is being unlinked
 *
 */

static int v9fs_vfs_unlink(struct inode *i, struct dentry *d)
{
	return v9fs_remove(i, d, 0);
}

/**
 * v9fs_vfs_rmdir - VFS unlink hook to delete a directory
 * @i:  inode that is being unlinked
 * @d: dentry that is being unlinked
 *
 */

static int v9fs_vfs_rmdir(struct inode *i, struct dentry *d)
{
	return v9fs_remove(i, d, 1);
}

/**
 * v9fs_vfs_rename - VFS hook to rename an inode
 * @old_dir:  old dir inode
 * @old_dentry: old dentry
 * @new_dir: new dir inode
 * @new_dentry: new dentry
 *
 */

static int
v9fs_vfs_rename(struct inode *old_dir, struct dentry *old_dentry,
		struct inode *new_dir, struct dentry *new_dentry)
{
	struct inode *old_inode;
	struct v9fs_session_info *v9ses;
	struct p9_fid *oldfid;
	struct p9_fid *olddirfid;
	struct p9_fid *newdirfid;
	struct p9_wstat wstat;
	int retval;

	P9_DPRINTK(P9_DEBUG_VFS, "\n");
	retval = 0;
	old_inode = old_dentry->d_inode;
	v9ses = v9fs_inode2v9ses(old_inode);
	oldfid = v9fs_fid_lookup(old_dentry);
	if (IS_ERR(oldfid))
		return PTR_ERR(oldfid);

	olddirfid = v9fs_fid_clone(old_dentry->d_parent);
	if (IS_ERR(olddirfid)) {
		retval = PTR_ERR(olddirfid);
		goto done;
	}

	newdirfid = v9fs_fid_clone(new_dentry->d_parent);
	if (IS_ERR(newdirfid)) {
		retval = PTR_ERR(newdirfid);
		goto clunk_olddir;
	}

	/* 9P can only handle file rename in the same directory */
	if (memcmp(&olddirfid->qid, &newdirfid->qid, sizeof(newdirfid->qid))) {
		P9_DPRINTK(P9_DEBUG_ERROR,
				"old dir and new dir are different\n");
		retval = -EXDEV;
		goto clunk_newdir;
	}

	v9fs_blank_wstat(&wstat);
	wstat.muid = v9ses->uname;
	wstat.name = (char *) new_dentry->d_name.name;
	retval = p9_client_wstat(oldfid, &wstat);

clunk_newdir:
	p9_client_clunk(newdirfid);

clunk_olddir:
	p9_client_clunk(olddirfid);

done:
	return retval;
}

/**
 * v9fs_vfs_getattr - retrieve file metadata
 * @mnt: mount information
 * @dentry: file to get attributes on
 * @stat: metadata structure to populate
 *
 */

static int
v9fs_vfs_getattr(struct vfsmount *mnt, struct dentry *dentry,
		 struct kstat *stat)
{
	int err;
	struct v9fs_session_info *v9ses;
	struct p9_fid *fid;
	struct p9_wstat *st;

	P9_DPRINTK(P9_DEBUG_VFS, "dentry: %p\n", dentry);
	err = -EPERM;
	v9ses = v9fs_inode2v9ses(dentry->d_inode);
	if (v9ses->cache == CACHE_LOOSE)
		return simple_getattr(mnt, dentry, stat);

	fid = v9fs_fid_lookup(dentry);
	if (IS_ERR(fid))
		return PTR_ERR(fid);

	st = p9_client_stat(fid);
	if (IS_ERR(st))
		return PTR_ERR(st);

	v9fs_stat2inode(st, dentry->d_inode, dentry->d_inode->i_sb);
		generic_fillattr(dentry->d_inode, stat);

	kfree(st);
	return 0;
}

/**
 * v9fs_vfs_setattr - set file metadata
 * @dentry: file whose metadata to set
 * @iattr: metadata assignment structure
 *
 */

static int v9fs_vfs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	int retval;
	struct v9fs_session_info *v9ses;
	struct p9_fid *fid;
	struct p9_wstat wstat;

	P9_DPRINTK(P9_DEBUG_VFS, "\n");
	retval = -EPERM;
	v9ses = v9fs_inode2v9ses(dentry->d_inode);
	fid = v9fs_fid_lookup(dentry);
	if(IS_ERR(fid))
		return PTR_ERR(fid);

	v9fs_blank_wstat(&wstat);
	if (iattr->ia_valid & ATTR_MODE)
		wstat.mode = unixmode2p9mode(v9ses, iattr->ia_mode);

	if (iattr->ia_valid & ATTR_MTIME)
		wstat.mtime = iattr->ia_mtime.tv_sec;

	if (iattr->ia_valid & ATTR_ATIME)
		wstat.atime = iattr->ia_atime.tv_sec;

	if (iattr->ia_valid & ATTR_SIZE)
		wstat.length = iattr->ia_size;

	if (v9fs_extended(v9ses)) {
		if (iattr->ia_valid & ATTR_UID)
			wstat.n_uid = iattr->ia_uid;

		if (iattr->ia_valid & ATTR_GID)
			wstat.n_gid = iattr->ia_gid;
	}

	retval = p9_client_wstat(fid, &wstat);
	if (retval >= 0)
		retval = inode_setattr(dentry->d_inode, iattr);

	return retval;
}

/**
 * v9fs_stat2inode - populate an inode structure with mistat info
 * @stat: Plan 9 metadata (mistat) structure
 * @inode: inode to populate
 * @sb: superblock of filesystem
 *
 */

void
v9fs_stat2inode(struct p9_wstat *stat, struct inode *inode,
	struct super_block *sb)
{
	char ext[32];
	struct v9fs_session_info *v9ses = sb->s_fs_info;

	inode->i_nlink = 1;

	inode->i_atime.tv_sec = stat->atime;
	inode->i_mtime.tv_sec = stat->mtime;
	inode->i_ctime.tv_sec = stat->mtime;

	inode->i_uid = v9ses->dfltuid;
	inode->i_gid = v9ses->dfltgid;

	if (v9fs_extended(v9ses)) {
		inode->i_uid = stat->n_uid;
		inode->i_gid = stat->n_gid;
	}

	inode->i_mode = p9mode2unixmode(v9ses, stat->mode);
	if ((S_ISBLK(inode->i_mode)) || (S_ISCHR(inode->i_mode))) {
		char type = 0;
		int major = -1;
		int minor = -1;

		strncpy(ext, stat->extension, sizeof(ext));
		sscanf(ext, "%c %u %u", &type, &major, &minor);
		switch (type) {
		case 'c':
			inode->i_mode &= ~S_IFBLK;
			inode->i_mode |= S_IFCHR;
			break;
		case 'b':
			break;
		default:
			P9_DPRINTK(P9_DEBUG_ERROR,
				"Unknown special type %c %s\n", type,
				stat->extension);
		};
		inode->i_rdev = MKDEV(major, minor);
		init_special_inode(inode, inode->i_mode, inode->i_rdev);
	} else
		inode->i_rdev = 0;

	inode->i_size = stat->length;

	/* not real number of blocks, but 512 byte ones ... */
	inode->i_blocks = (inode->i_size + 512 - 1) >> 9;
}

/**
 * v9fs_qid2ino - convert qid into inode number
 * @qid: qid to hash
 *
 * BUG: potential for inode number collisions?
 */

ino_t v9fs_qid2ino(struct p9_qid *qid)
{
	u64 path = qid->path + 2;
	ino_t i = 0;

	if (sizeof(ino_t) == sizeof(path))
		memcpy(&i, &path, sizeof(ino_t));
	else
		i = (ino_t) (path ^ (path >> 32));

	return i;
}

/**
 * v9fs_readlink - read a symlink's location (internal version)
 * @dentry: dentry for symlink
 * @buffer: buffer to load symlink location into
 * @buflen: length of buffer
 *
 */

static int v9fs_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	int retval;

	struct v9fs_session_info *v9ses;
	struct p9_fid *fid;
	struct p9_wstat *st;

	P9_DPRINTK(P9_DEBUG_VFS, " %s\n", dentry->d_name.name);
	retval = -EPERM;
	v9ses = v9fs_inode2v9ses(dentry->d_inode);
	fid = v9fs_fid_lookup(dentry);
	if (IS_ERR(fid))
		return PTR_ERR(fid);

	if (!v9fs_extended(v9ses))
		return -EBADF;

	st = p9_client_stat(fid);
	if (IS_ERR(st))
		return PTR_ERR(st);

	if (!(st->mode & P9_DMSYMLINK)) {
		retval = -EINVAL;
		goto done;
	}

	/* copy extension buffer into buffer */
	strncpy(buffer, st->extension, buflen);

	P9_DPRINTK(P9_DEBUG_VFS,
		"%s -> %s (%s)\n", dentry->d_name.name, st->extension, buffer);

	retval = buflen;

done:
	kfree(st);
	return retval;
}

/**
 * v9fs_vfs_readlink - read a symlink's location
 * @dentry: dentry for symlink
 * @buffer: buffer to load symlink location into
 * @buflen: length of buffer
 *
 */

static int v9fs_vfs_readlink(struct dentry *dentry, char __user * buffer,
			     int buflen)
{
	int retval;
	int ret;
	char *link = __getname();

	if (unlikely(!link))
		return -ENOMEM;

	if (buflen > PATH_MAX)
		buflen = PATH_MAX;

	P9_DPRINTK(P9_DEBUG_VFS, " dentry: %s (%p)\n", dentry->d_name.name,
									dentry);

	retval = v9fs_readlink(dentry, link, buflen);

	if (retval > 0) {
		if ((ret = copy_to_user(buffer, link, retval)) != 0) {
			P9_DPRINTK(P9_DEBUG_ERROR,
					"problem copying to user: %d\n", ret);
			retval = ret;
		}
	}

	__putname(link);
	return retval;
}

/**
 * v9fs_vfs_follow_link - follow a symlink path
 * @dentry: dentry for symlink
 * @nd: nameidata
 *
 */

static void *v9fs_vfs_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	int len = 0;
	char *link = __getname();

	P9_DPRINTK(P9_DEBUG_VFS, "%s n", dentry->d_name.name);

	if (!link)
		link = ERR_PTR(-ENOMEM);
	else {
		len = v9fs_readlink(dentry, link, PATH_MAX);

		if (len < 0) {
			__putname(link);
			link = ERR_PTR(len);
		} else
			link[len] = 0;
	}
	nd_set_link(nd, link);

	return NULL;
}

/**
 * v9fs_vfs_put_link - release a symlink path
 * @dentry: dentry for symlink
 * @nd: nameidata
 * @p: unused
 *
 */

static void
v9fs_vfs_put_link(struct dentry *dentry, struct nameidata *nd, void *p)
{
	char *s = nd_get_link(nd);

	P9_DPRINTK(P9_DEBUG_VFS, " %s %s\n", dentry->d_name.name,
		IS_ERR(s) ? "<error>" : s);
	if (!IS_ERR(s))
		__putname(s);
}

/**
 * v9fs_vfs_mkspecial - create a special file
 * @dir: inode to create special file in
 * @dentry: dentry to create
 * @mode: mode to create special file
 * @extension: 9p2000.u format extension string representing special file
 *
 */

static int v9fs_vfs_mkspecial(struct inode *dir, struct dentry *dentry,
	int mode, const char *extension)
{
	u32 perm;
	struct v9fs_session_info *v9ses;
	struct p9_fid *fid;

	v9ses = v9fs_inode2v9ses(dir);
	if (!v9fs_extended(v9ses)) {
		P9_DPRINTK(P9_DEBUG_ERROR, "not extended\n");
		return -EPERM;
	}

	perm = unixmode2p9mode(v9ses, mode);
	fid = v9fs_create(v9ses, dir, dentry, (char *) extension, perm,
								P9_OREAD);
	if (IS_ERR(fid))
		return PTR_ERR(fid);

	p9_client_clunk(fid);
	return 0;
}

/**
 * v9fs_vfs_symlink - helper function to create symlinks
 * @dir: directory inode containing symlink
 * @dentry: dentry for symlink
 * @symname: symlink data
 *
 * See Also: 9P2000.u RFC for more information
 *
 */

static int
v9fs_vfs_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	P9_DPRINTK(P9_DEBUG_VFS, " %lu,%s,%s\n", dir->i_ino,
					dentry->d_name.name, symname);

	return v9fs_vfs_mkspecial(dir, dentry, S_IFLNK, symname);
}

/**
 * v9fs_vfs_link - create a hardlink
 * @old_dentry: dentry for file to link to
 * @dir: inode destination for new link
 * @dentry: dentry for link
 *
 */

static int
v9fs_vfs_link(struct dentry *old_dentry, struct inode *dir,
	      struct dentry *dentry)
{
	int retval;
	struct p9_fid *oldfid;
	char *name;

	P9_DPRINTK(P9_DEBUG_VFS,
		" %lu,%s,%s\n", dir->i_ino, dentry->d_name.name,
		old_dentry->d_name.name);

	oldfid = v9fs_fid_clone(old_dentry);
	if (IS_ERR(oldfid))
		return PTR_ERR(oldfid);

	name = __getname();
	if (unlikely(!name)) {
		retval = -ENOMEM;
		goto clunk_fid;
	}

	sprintf(name, "%d\n", oldfid->fid);
	retval = v9fs_vfs_mkspecial(dir, dentry, P9_DMLINK, name);
	__putname(name);

clunk_fid:
	p9_client_clunk(oldfid);
	return retval;
}

/**
 * v9fs_vfs_mknod - create a special file
 * @dir: inode destination for new link
 * @dentry: dentry for file
 * @mode: mode for creation
 * @rdev: device associated with special file
 *
 */

static int
v9fs_vfs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t rdev)
{
	int retval;
	char *name;

	P9_DPRINTK(P9_DEBUG_VFS,
		" %lu,%s mode: %x MAJOR: %u MINOR: %u\n", dir->i_ino,
		dentry->d_name.name, mode, MAJOR(rdev), MINOR(rdev));

	if (!new_valid_dev(rdev))
		return -EINVAL;

	name = __getname();
	if (!name)
		return -ENOMEM;
	/* build extension */
	if (S_ISBLK(mode))
		sprintf(name, "b %u %u", MAJOR(rdev), MINOR(rdev));
	else if (S_ISCHR(mode))
		sprintf(name, "c %u %u", MAJOR(rdev), MINOR(rdev));
	else if (S_ISFIFO(mode))
		*name = 0;
	else {
		__putname(name);
		return -EINVAL;
	}

	retval = v9fs_vfs_mkspecial(dir, dentry, mode, name);
	__putname(name);

	return retval;
}

static const struct inode_operations v9fs_dir_inode_operations_ext = {
	.create = v9fs_vfs_create,
	.lookup = v9fs_vfs_lookup,
	.symlink = v9fs_vfs_symlink,
	.link = v9fs_vfs_link,
	.unlink = v9fs_vfs_unlink,
	.mkdir = v9fs_vfs_mkdir,
	.rmdir = v9fs_vfs_rmdir,
	.mknod = v9fs_vfs_mknod,
	.rename = v9fs_vfs_rename,
	.readlink = v9fs_vfs_readlink,
	.getattr = v9fs_vfs_getattr,
	.setattr = v9fs_vfs_setattr,
};

static const struct inode_operations v9fs_dir_inode_operations = {
	.create = v9fs_vfs_create,
	.lookup = v9fs_vfs_lookup,
	.unlink = v9fs_vfs_unlink,
	.mkdir = v9fs_vfs_mkdir,
	.rmdir = v9fs_vfs_rmdir,
	.mknod = v9fs_vfs_mknod,
	.rename = v9fs_vfs_rename,
	.getattr = v9fs_vfs_getattr,
	.setattr = v9fs_vfs_setattr,
};

static const struct inode_operations v9fs_file_inode_operations = {
	.getattr = v9fs_vfs_getattr,
	.setattr = v9fs_vfs_setattr,
};

static const struct inode_operations v9fs_symlink_inode_operations = {
	.readlink = v9fs_vfs_readlink,
	.follow_link = v9fs_vfs_follow_link,
	.put_link = v9fs_vfs_put_link,
	.getattr = v9fs_vfs_getattr,
	.setattr = v9fs_vfs_setattr,
};
