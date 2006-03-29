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
#include <linux/smp_lock.h>
#include <linux/inet.h>
#include <linux/namei.h>
#include <linux/idr.h>

#include "debug.h"
#include "v9fs.h"
#include "9p.h"
#include "v9fs_vfs.h"
#include "fid.h"

static struct inode_operations v9fs_dir_inode_operations;
static struct inode_operations v9fs_dir_inode_operations_ext;
static struct inode_operations v9fs_file_inode_operations;
static struct inode_operations v9fs_symlink_inode_operations;

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
		res |= V9FS_DMDIR;
	if (v9ses->extended) {
		if (S_ISLNK(mode))
			res |= V9FS_DMSYMLINK;
		if (v9ses->nodev == 0) {
			if (S_ISSOCK(mode))
				res |= V9FS_DMSOCKET;
			if (S_ISFIFO(mode))
				res |= V9FS_DMNAMEDPIPE;
			if (S_ISBLK(mode))
				res |= V9FS_DMDEVICE;
			if (S_ISCHR(mode))
				res |= V9FS_DMDEVICE;
		}

		if ((mode & S_ISUID) == S_ISUID)
			res |= V9FS_DMSETUID;
		if ((mode & S_ISGID) == S_ISGID)
			res |= V9FS_DMSETGID;
		if ((mode & V9FS_DMLINK))
			res |= V9FS_DMLINK;
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

	if ((mode & V9FS_DMDIR) == V9FS_DMDIR)
		res |= S_IFDIR;
	else if ((mode & V9FS_DMSYMLINK) && (v9ses->extended))
		res |= S_IFLNK;
	else if ((mode & V9FS_DMSOCKET) && (v9ses->extended)
		 && (v9ses->nodev == 0))
		res |= S_IFSOCK;
	else if ((mode & V9FS_DMNAMEDPIPE) && (v9ses->extended)
		 && (v9ses->nodev == 0))
		res |= S_IFIFO;
	else if ((mode & V9FS_DMDEVICE) && (v9ses->extended)
		 && (v9ses->nodev == 0))
		res |= S_IFBLK;
	else
		res |= S_IFREG;

	if (v9ses->extended) {
		if ((mode & V9FS_DMSETUID) == V9FS_DMSETUID)
			res |= S_ISUID;

		if ((mode & V9FS_DMSETGID) == V9FS_DMSETGID)
			res |= S_ISGID;
	}

	return res;
}

int v9fs_uflags2omode(int uflags)
{
	int ret;

	ret = 0;
	switch (uflags&3) {
	default:
	case O_RDONLY:
		ret = V9FS_OREAD;
		break;

	case O_WRONLY:
		ret = V9FS_OWRITE;
		break;

	case O_RDWR:
		ret = V9FS_ORDWR;
		break;
	}

	if (uflags & O_EXCL)
		ret |= V9FS_OEXCL;

	if (uflags & O_TRUNC)
		ret |= V9FS_OTRUNC;

	if (uflags & O_APPEND)
		ret |= V9FS_OAPPEND;

	return ret;
}

/**
 * v9fs_blank_wstat - helper function to setup a 9P stat structure
 * @v9ses: 9P session info (for determining extended mode)
 * @wstat: structure to initialize
 *
 */

static void
v9fs_blank_wstat(struct v9fs_wstat *wstat)
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
	struct inode *inode;
	struct v9fs_session_info *v9ses = sb->s_fs_info;

	dprintk(DEBUG_VFS, "super block: %p mode: %o\n", sb, mode);

	inode = new_inode(sb);
	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = current->fsuid;
		inode->i_gid = current->fsgid;
		inode->i_blksize = sb->s_blocksize;
		inode->i_blocks = 0;
		inode->i_rdev = 0;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		inode->i_mapping->a_ops = &v9fs_addr_operations;

		switch (mode & S_IFMT) {
		case S_IFIFO:
		case S_IFBLK:
		case S_IFCHR:
		case S_IFSOCK:
			if(!v9ses->extended) {
				dprintk(DEBUG_ERROR, "special files without extended mode\n");
				return ERR_PTR(-EINVAL);
			}
			init_special_inode(inode, inode->i_mode,
					   inode->i_rdev);
			break;
		case S_IFREG:
			inode->i_op = &v9fs_file_inode_operations;
			inode->i_fop = &v9fs_file_operations;
			break;
		case S_IFLNK:
			if(!v9ses->extended) {
				dprintk(DEBUG_ERROR, "extended modes used w/o 9P2000.u\n");
				return ERR_PTR(-EINVAL);
			}
			inode->i_op = &v9fs_symlink_inode_operations;
			break;
		case S_IFDIR:
			inode->i_nlink++;
			if(v9ses->extended)
				inode->i_op = &v9fs_dir_inode_operations_ext;
			else
				inode->i_op = &v9fs_dir_inode_operations;
			inode->i_fop = &v9fs_dir_operations;
			break;
		default:
			dprintk(DEBUG_ERROR, "BAD mode 0x%x S_IFMT 0x%x\n",
				mode, mode & S_IFMT);
			return ERR_PTR(-EINVAL);
		}
	} else {
		eprintk(KERN_WARNING, "Problem allocating inode\n");
		return ERR_PTR(-ENOMEM);
	}
	return inode;
}

static int
v9fs_create(struct v9fs_session_info *v9ses, u32 pfid, char *name, u32 perm,
	u8 mode, char *extension, u32 *fidp, struct v9fs_qid *qid, u32 *iounit)
{
	u32 fid;
	int err;
	struct v9fs_fcall *fcall;

	fid = v9fs_get_idpool(&v9ses->fidpool);
	if (fid < 0) {
		eprintk(KERN_WARNING, "no free fids available\n");
		return -ENOSPC;
	}

	err = v9fs_t_walk(v9ses, pfid, fid, NULL, &fcall);
	if (err < 0) {
		PRINT_FCALL_ERROR("clone error", fcall);
		goto put_fid;
	}
	kfree(fcall);

	err = v9fs_t_create(v9ses, fid, name, perm, mode, extension, &fcall);
	if (err < 0) {
		PRINT_FCALL_ERROR("create fails", fcall);
		goto clunk_fid;
	}

	if (iounit)
		*iounit = fcall->params.rcreate.iounit;

	if (qid)
		*qid = fcall->params.rcreate.qid;

	if (fidp)
		*fidp = fid;

	kfree(fcall);
	return 0;

clunk_fid:
	v9fs_t_clunk(v9ses, fid);
	fid = V9FS_NOFID;

put_fid:
	if (fid >= 0)
		v9fs_put_idpool(fid, &v9ses->fidpool);

	kfree(fcall);
	return err;
}

static struct v9fs_fid*
v9fs_clone_walk(struct v9fs_session_info *v9ses, u32 fid, struct dentry *dentry)
{
	int err;
	u32 nfid;
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

static struct inode *
v9fs_inode_from_fid(struct v9fs_session_info *v9ses, u32 fid,
	struct super_block *sb)
{
	int err, umode;
	struct inode *ret;
	struct v9fs_fcall *fcall;

	ret = NULL;
	err = v9fs_t_stat(v9ses, fid, &fcall);
	if (err) {
		PRINT_FCALL_ERROR("stat error", fcall);
		goto error;
	}

	umode = p9mode2unixmode(v9ses, fcall->params.rstat.stat.mode);
	ret = v9fs_get_inode(sb, umode);
	if (IS_ERR(ret)) {
		err = PTR_ERR(ret);
		ret = NULL;
		goto error;
	}

	v9fs_stat2inode(&fcall->params.rstat.stat, ret, sb);
	kfree(fcall);
	return ret;

error:
	kfree(fcall);
	if (ret)
		iput(ret);

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
	struct v9fs_fcall *fcall = NULL;
	struct super_block *sb = NULL;
	struct v9fs_session_info *v9ses = NULL;
	struct v9fs_fid *v9fid = NULL;
	struct inode *file_inode = NULL;
	int fid = -1;
	int result = 0;

	dprintk(DEBUG_VFS, "inode: %p dentry: %p rmdir: %d\n", dir, file,
		rmdir);

	file_inode = file->d_inode;
	sb = file_inode->i_sb;
	v9ses = v9fs_inode2v9ses(file_inode);
	v9fid = v9fs_fid_lookup(file);

	if (!v9fid) {
		dprintk(DEBUG_ERROR,
			"no v9fs_fid\n");
		return -EBADF;
	}

	fid = v9fid->fid;
	if (fid < 0) {
		dprintk(DEBUG_ERROR, "inode #%lu, no fid!\n",
			file_inode->i_ino);
		return -EBADF;
	}

	result = v9fs_t_remove(v9ses, fid, &fcall);
	if (result < 0) {
		PRINT_FCALL_ERROR("remove fails", fcall);
	} else {
		v9fs_put_idpool(fid, &v9ses->fidpool);
		v9fs_fid_destroy(v9fid);
	}

	kfree(fcall);
	return result;
}

static int
v9fs_open_created(struct inode *inode, struct file *file)
{
	return 0;
}

/**
 * v9fs_vfs_create - VFS hook to create files
 * @inode: directory inode that is being deleted
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
	u32 fid, perm, iounit;
	int flags;
	struct v9fs_session_info *v9ses;
	struct v9fs_fid *dfid, *vfid, *ffid;
	struct inode *inode;
	struct v9fs_qid qid;
	struct file *filp;

	inode = NULL;
	vfid = NULL;
	v9ses = v9fs_inode2v9ses(dir);
	dfid = v9fs_fid_lookup(dentry->d_parent);
	perm = unixmode2p9mode(v9ses, mode);

	if (nd && nd->flags & LOOKUP_OPEN)
		flags = nd->intent.open.flags - 1;
	else
		flags = O_RDWR;

	err = v9fs_create(v9ses, dfid->fid, (char *) dentry->d_name.name,
		perm, v9fs_uflags2omode(flags), NULL, &fid, &qid, &iounit);

	if (err)
		goto error;

	vfid = v9fs_clone_walk(v9ses, dfid->fid, dentry);
	if (IS_ERR(vfid)) {
		err = PTR_ERR(vfid);
		vfid = NULL;
		goto error;
	}

	inode = v9fs_inode_from_fid(v9ses, vfid->fid, dir->i_sb);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		inode = NULL;
		goto error;
	}

	dentry->d_op = &v9fs_dentry_operations;
	d_instantiate(dentry, inode);

	if (nd && nd->flags & LOOKUP_OPEN) {
		ffid = v9fs_fid_create(v9ses, fid);
		if (!ffid)
			return -ENOMEM;

		filp = lookup_instantiate_filp(nd, dentry, v9fs_open_created);
		if (IS_ERR(filp)) {
			v9fs_fid_destroy(ffid);
			return PTR_ERR(filp);
		}

		ffid->rdir_pos = 0;
		ffid->rdir_fcall = NULL;
		ffid->fidopen = 1;
		ffid->iounit = iounit;
		ffid->filp = filp;
		filp->private_data = ffid;
	}

	return 0;

error:
	if (vfid)
		v9fs_fid_destroy(vfid);

	if (inode)
		iput(inode);

	return err;
}

/**
 * v9fs_vfs_mkdir - VFS mkdir hook to create a directory
 * @inode:  inode that is being unlinked
 * @dentry: dentry that is being unlinked
 * @mode: mode for new directory
 *
 */

static int v9fs_vfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int err;
	u32 fid, perm;
	struct v9fs_session_info *v9ses;
	struct v9fs_fid *dfid, *vfid;
	struct inode *inode;

	inode = NULL;
	vfid = NULL;
	v9ses = v9fs_inode2v9ses(dir);
	dfid = v9fs_fid_lookup(dentry->d_parent);
	perm = unixmode2p9mode(v9ses, mode | S_IFDIR);

	err = v9fs_create(v9ses, dfid->fid, (char *) dentry->d_name.name,
		perm, V9FS_OREAD, NULL, &fid, NULL, NULL);

	if (err) {
		dprintk(DEBUG_ERROR, "create error %d\n", err);
		goto error;
	}

	err = v9fs_t_clunk(v9ses, fid);
	if (err) {
		dprintk(DEBUG_ERROR, "clunk error %d\n", err);
		goto error;
	}

	vfid = v9fs_clone_walk(v9ses, dfid->fid, dentry);
	if (IS_ERR(vfid)) {
		err = PTR_ERR(vfid);
		vfid = NULL;
		goto error;
	}

	inode = v9fs_inode_from_fid(v9ses, vfid->fid, dir->i_sb);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		inode = NULL;
		goto error;
	}

	dentry->d_op = &v9fs_dentry_operations;
	d_instantiate(dentry, inode);
	return 0;

error:
	if (vfid)
		v9fs_fid_destroy(vfid);

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
	struct v9fs_fid *dirfid;
	struct v9fs_fid *fid;
	struct inode *inode;
	struct v9fs_fcall *fcall = NULL;
	int dirfidnum = -1;
	int newfid = -1;
	int result = 0;

	dprintk(DEBUG_VFS, "dir: %p dentry: (%s) %p nameidata: %p\n",
		dir, dentry->d_name.name, dentry, nameidata);

	sb = dir->i_sb;
	v9ses = v9fs_inode2v9ses(dir);
	dentry->d_op = &v9fs_dentry_operations;
	dirfid = v9fs_fid_lookup(dentry->d_parent);

	if (!dirfid) {
		dprintk(DEBUG_ERROR, "no dirfid\n");
		return ERR_PTR(-EINVAL);
	}

	dirfidnum = dirfid->fid;

	if (dirfidnum < 0) {
		dprintk(DEBUG_ERROR, "no dirfid for inode %p, #%lu\n",
			dir, dir->i_ino);
		return ERR_PTR(-EBADF);
	}

	newfid = v9fs_get_idpool(&v9ses->fidpool);
	if (newfid < 0) {
		eprintk(KERN_WARNING, "newfid fails!\n");
		return ERR_PTR(-ENOSPC);
	}

	result = v9fs_t_walk(v9ses, dirfidnum, newfid,
		(char *)dentry->d_name.name, NULL);
	if (result < 0) {
		v9fs_put_idpool(newfid, &v9ses->fidpool);
		if (result == -ENOENT) {
			d_add(dentry, NULL);
			dprintk(DEBUG_VFS,
				"Return negative dentry %p count %d\n",
				dentry, atomic_read(&dentry->d_count));
			return NULL;
		}
		dprintk(DEBUG_ERROR, "walk error:%d\n", result);
		goto FreeFcall;
	}

	result = v9fs_t_stat(v9ses, newfid, &fcall);
	if (result < 0) {
		dprintk(DEBUG_ERROR, "stat error\n");
		goto FreeFcall;
	}

	inode = v9fs_get_inode(sb, p9mode2unixmode(v9ses,
		fcall->params.rstat.stat.mode));

	if (IS_ERR(inode) && (PTR_ERR(inode) == -ENOSPC)) {
		eprintk(KERN_WARNING, "inode alloc failes, returns %ld\n",
			PTR_ERR(inode));

		result = -ENOSPC;
		goto FreeFcall;
	}

	inode->i_ino = v9fs_qid2ino(&fcall->params.rstat.stat.qid);

	fid = v9fs_fid_create(v9ses, newfid);
	if (fid == NULL) {
		dprintk(DEBUG_ERROR, "couldn't insert\n");
		result = -ENOMEM;
		goto FreeFcall;
	}

	result = v9fs_fid_insert(fid, dentry);
	if (result < 0)
		goto FreeFcall;

	fid->qid = fcall->params.rstat.stat.qid;
	v9fs_stat2inode(&fcall->params.rstat.stat, inode, inode->i_sb);

	d_add(dentry, inode);
	kfree(fcall);

	return NULL;

      FreeFcall:
	kfree(fcall);
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
	struct inode *old_inode = old_dentry->d_inode;
	struct v9fs_session_info *v9ses = v9fs_inode2v9ses(old_inode);
	struct v9fs_fid *oldfid = v9fs_fid_lookup(old_dentry);
	struct v9fs_fid *olddirfid =
	    v9fs_fid_lookup(old_dentry->d_parent);
	struct v9fs_fid *newdirfid =
	    v9fs_fid_lookup(new_dentry->d_parent);
	struct v9fs_wstat wstat;
	struct v9fs_fcall *fcall = NULL;
	int fid = -1;
	int olddirfidnum = -1;
	int newdirfidnum = -1;
	int retval = 0;

	dprintk(DEBUG_VFS, "\n");

	if ((!oldfid) || (!olddirfid) || (!newdirfid)) {
		dprintk(DEBUG_ERROR, "problem with arguments\n");
		return -EBADF;
	}

	/* 9P can only handle file rename in the same directory */
	if (memcmp(&olddirfid->qid, &newdirfid->qid, sizeof(newdirfid->qid))) {
		dprintk(DEBUG_ERROR, "old dir and new dir are different\n");
		retval = -EPERM;
		goto FreeFcallnBail;
	}

	fid = oldfid->fid;
	olddirfidnum = olddirfid->fid;
	newdirfidnum = newdirfid->fid;

	if (fid < 0) {
		dprintk(DEBUG_ERROR, "no fid for old file #%lu\n",
			old_inode->i_ino);
		retval = -EBADF;
		goto FreeFcallnBail;
	}

	v9fs_blank_wstat(&wstat);
	wstat.muid = v9ses->name;
	wstat.name = (char *) new_dentry->d_name.name;

	retval = v9fs_t_wstat(v9ses, fid, &wstat, &fcall);

      FreeFcallnBail:
	if (retval < 0)
		PRINT_FCALL_ERROR("wstat error", fcall);

	kfree(fcall);
	return retval;
}

/**
 * v9fs_vfs_getattr - retrieve file metadata
 * @mnt - mount information
 * @dentry - file to get attributes on
 * @stat - metadata structure to populate
 *
 */

static int
v9fs_vfs_getattr(struct vfsmount *mnt, struct dentry *dentry,
		 struct kstat *stat)
{
	struct v9fs_fcall *fcall = NULL;
	struct v9fs_session_info *v9ses = v9fs_inode2v9ses(dentry->d_inode);
	struct v9fs_fid *fid = v9fs_fid_lookup(dentry);
	int err = -EPERM;

	dprintk(DEBUG_VFS, "dentry: %p\n", dentry);
	if (!fid) {
		dprintk(DEBUG_ERROR,
			"couldn't find fid associated with dentry\n");
		return -EBADF;
	}

	err = v9fs_t_stat(v9ses, fid->fid, &fcall);

	if (err < 0)
		dprintk(DEBUG_ERROR, "stat error\n");
	else {
		v9fs_stat2inode(&fcall->params.rstat.stat, dentry->d_inode,
				  dentry->d_inode->i_sb);
		generic_fillattr(dentry->d_inode, stat);
	}

	kfree(fcall);
	return err;
}

/**
 * v9fs_vfs_setattr - set file metadata
 * @dentry: file whose metadata to set
 * @iattr: metadata assignment structure
 *
 */

static int v9fs_vfs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct v9fs_session_info *v9ses = v9fs_inode2v9ses(dentry->d_inode);
	struct v9fs_fid *fid = v9fs_fid_lookup(dentry);
	struct v9fs_fcall *fcall = NULL;
	struct v9fs_wstat wstat;
	int res = -EPERM;

	dprintk(DEBUG_VFS, "\n");

	if (!fid) {
		dprintk(DEBUG_ERROR,
			"Couldn't find fid associated with dentry\n");
		return -EBADF;
	}

	v9fs_blank_wstat(&wstat);
	if (iattr->ia_valid & ATTR_MODE)
		wstat.mode = unixmode2p9mode(v9ses, iattr->ia_mode);

	if (iattr->ia_valid & ATTR_MTIME)
		wstat.mtime = iattr->ia_mtime.tv_sec;

	if (iattr->ia_valid & ATTR_ATIME)
		wstat.atime = iattr->ia_atime.tv_sec;

	if (iattr->ia_valid & ATTR_SIZE)
		wstat.length = iattr->ia_size;

	if (v9ses->extended) {
		if (iattr->ia_valid & ATTR_UID)
			wstat.n_uid = iattr->ia_uid;

		if (iattr->ia_valid & ATTR_GID)
			wstat.n_gid = iattr->ia_gid;
	}

	res = v9fs_t_wstat(v9ses, fid->fid, &wstat, &fcall);

	if (res < 0)
		PRINT_FCALL_ERROR("wstat error", fcall);

	kfree(fcall);
	if (res >= 0)
		res = inode_setattr(dentry->d_inode, iattr);

	return res;
}

/**
 * v9fs_stat2inode - populate an inode structure with mistat info
 * @stat: Plan 9 metadata (mistat) structure
 * @inode: inode to populate
 * @sb: superblock of filesystem
 *
 */

void
v9fs_stat2inode(struct v9fs_stat *stat, struct inode *inode,
	struct super_block *sb)
{
	int n;
	char ext[32];
	struct v9fs_session_info *v9ses = sb->s_fs_info;

	inode->i_nlink = 1;

	inode->i_atime.tv_sec = stat->atime;
	inode->i_mtime.tv_sec = stat->mtime;
	inode->i_ctime.tv_sec = stat->mtime;

	inode->i_uid = v9ses->uid;
	inode->i_gid = v9ses->gid;

	if (v9ses->extended) {
		inode->i_uid = stat->n_uid;
		inode->i_gid = stat->n_gid;
	}

	inode->i_mode = p9mode2unixmode(v9ses, stat->mode);
	if ((S_ISBLK(inode->i_mode)) || (S_ISCHR(inode->i_mode))) {
		char type = 0;
		int major = -1;
		int minor = -1;

		n = stat->extension.len;
		if (n > sizeof(ext)-1)
			n = sizeof(ext)-1;
		memmove(ext, stat->extension.str, n);
		ext[n] = 0;
		sscanf(ext, "%c %u %u", &type, &major, &minor);
		switch (type) {
		case 'c':
			inode->i_mode &= ~S_IFBLK;
			inode->i_mode |= S_IFCHR;
			break;
		case 'b':
			break;
		default:
			dprintk(DEBUG_ERROR, "Unknown special type %c (%.*s)\n",
				type, stat->extension.len, stat->extension.str);
		};
		inode->i_rdev = MKDEV(major, minor);
	} else
		inode->i_rdev = 0;

	inode->i_size = stat->length;

	inode->i_blksize = sb->s_blocksize;
	inode->i_blocks =
	    (inode->i_size + inode->i_blksize - 1) >> sb->s_blocksize_bits;
}

/**
 * v9fs_qid2ino - convert qid into inode number
 * @qid: qid to hash
 *
 * BUG: potential for inode number collisions?
 */

ino_t v9fs_qid2ino(struct v9fs_qid *qid)
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
	int retval = -EPERM;

	struct v9fs_fcall *fcall = NULL;
	struct v9fs_session_info *v9ses = v9fs_inode2v9ses(dentry->d_inode);
	struct v9fs_fid *fid = v9fs_fid_lookup(dentry);

	if (!fid) {
		dprintk(DEBUG_ERROR, "could not resolve fid from dentry\n");
		retval = -EBADF;
		goto FreeFcall;
	}

	if (!v9ses->extended) {
		retval = -EBADF;
		dprintk(DEBUG_ERROR, "not extended\n");
		goto FreeFcall;
	}

	dprintk(DEBUG_VFS, " %s\n", dentry->d_name.name);
	retval = v9fs_t_stat(v9ses, fid->fid, &fcall);

	if (retval < 0) {
		dprintk(DEBUG_ERROR, "stat error\n");
		goto FreeFcall;
	}

	if (!fcall)
		return -EIO;

	if (!(fcall->params.rstat.stat.mode & V9FS_DMSYMLINK)) {
		retval = -EINVAL;
		goto FreeFcall;
	}

	/* copy extension buffer into buffer */
	if (fcall->params.rstat.stat.extension.len < buflen)
		buflen = fcall->params.rstat.stat.extension.len + 1;

	memmove(buffer, fcall->params.rstat.stat.extension.str, buflen - 1);
	buffer[buflen-1] = 0;

	dprintk(DEBUG_ERROR, "%s -> %.*s (%s)\n", dentry->d_name.name, fcall->params.rstat.stat.extension.len,
		fcall->params.rstat.stat.extension.str, buffer);
	retval = buflen;

      FreeFcall:
	kfree(fcall);

	return retval;
}

/**
 * v9fs_vfs_readlink - read a symlink's location
 * @dentry: dentry for symlink
 * @buf: buffer to load symlink location into
 * @buflen: length of buffer
 *
 */

static int v9fs_vfs_readlink(struct dentry *dentry, char __user * buffer,
			     int buflen)
{
	int retval;
	int ret;
	char *link = __getname();

	if (buflen > PATH_MAX)
		buflen = PATH_MAX;

	dprintk(DEBUG_VFS, " dentry: %s (%p)\n", dentry->d_iname, dentry);

	retval = v9fs_readlink(dentry, link, buflen);

	if (retval > 0) {
		if ((ret = copy_to_user(buffer, link, retval)) != 0) {
			dprintk(DEBUG_ERROR, "problem copying to user: %d\n",
				ret);
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

	dprintk(DEBUG_VFS, "%s n", dentry->d_name.name);

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
 *
 */

static void v9fs_vfs_put_link(struct dentry *dentry, struct nameidata *nd, void *p)
{
	char *s = nd_get_link(nd);

	dprintk(DEBUG_VFS, " %s %s\n", dentry->d_name.name, s);
	if (!IS_ERR(s))
		__putname(s);
}

static int v9fs_vfs_mkspecial(struct inode *dir, struct dentry *dentry,
	int mode, const char *extension)
{
	int err;
	u32 fid, perm;
	struct v9fs_session_info *v9ses;
	struct v9fs_fid *dfid, *vfid;
	struct inode *inode;

	inode = NULL;
	vfid = NULL;
	v9ses = v9fs_inode2v9ses(dir);
	dfid = v9fs_fid_lookup(dentry->d_parent);
	perm = unixmode2p9mode(v9ses, mode);

	if (!v9ses->extended) {
		dprintk(DEBUG_ERROR, "not extended\n");
		return -EPERM;
	}

	err = v9fs_create(v9ses, dfid->fid, (char *) dentry->d_name.name,
		perm, V9FS_OREAD, (char *) extension, &fid, NULL, NULL);

	if (err)
		goto error;

	err = v9fs_t_clunk(v9ses, fid);
	if (err)
		goto error;

	vfid = v9fs_clone_walk(v9ses, dfid->fid, dentry);
	if (IS_ERR(vfid)) {
		err = PTR_ERR(vfid);
		vfid = NULL;
		goto error;
	}

	inode = v9fs_inode_from_fid(v9ses, vfid->fid, dir->i_sb);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		inode = NULL;
		goto error;
	}

	dentry->d_op = &v9fs_dentry_operations;
	d_instantiate(dentry, inode);
	return 0;

error:
	if (vfid)
		v9fs_fid_destroy(vfid);

	if (inode)
		iput(inode);

	return err;

}

/**
 * v9fs_vfs_symlink - helper function to create symlinks
 * @dir: directory inode containing symlink
 * @dentry: dentry for symlink
 * @symname: symlink data
 *
 * See 9P2000.u RFC for more information
 *
 */

static int
v9fs_vfs_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	dprintk(DEBUG_VFS, " %lu,%s,%s\n", dir->i_ino, dentry->d_name.name,
		symname);

	return v9fs_vfs_mkspecial(dir, dentry, S_IFLNK, symname);
}

/**
 * v9fs_vfs_link - create a hardlink
 * @old_dentry: dentry for file to link to
 * @dir: inode destination for new link
 * @dentry: dentry for link
 *
 */

/* XXX - lots of code dup'd from symlink and creates,
 * figure out a better reuse strategy
 */

static int
v9fs_vfs_link(struct dentry *old_dentry, struct inode *dir,
	      struct dentry *dentry)
{
	int retval;
	struct v9fs_fid *oldfid;
	char *name;

	dprintk(DEBUG_VFS, " %lu,%s,%s\n", dir->i_ino, dentry->d_name.name,
		old_dentry->d_name.name);

	oldfid = v9fs_fid_lookup(old_dentry);
	if (!oldfid) {
		dprintk(DEBUG_ERROR, "can't find oldfid\n");
		return -EPERM;
	}

	name = __getname();
	sprintf(name, "%d\n", oldfid->fid);
	retval = v9fs_vfs_mkspecial(dir, dentry, V9FS_DMLINK, name);
	__putname(name);

	return retval;
}

/**
 * v9fs_vfs_mknod - create a special file
 * @dir: inode destination for new link
 * @dentry: dentry for file
 * @mode: mode for creation
 * @dev_t: device associated with special file
 *
 */

static int
v9fs_vfs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t rdev)
{
	int retval;
	char *name;

	dprintk(DEBUG_VFS, " %lu,%s mode: %x MAJOR: %u MINOR: %u\n", dir->i_ino,
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

static struct inode_operations v9fs_dir_inode_operations_ext = {
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

static struct inode_operations v9fs_dir_inode_operations = {
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

static struct inode_operations v9fs_file_inode_operations = {
	.getattr = v9fs_vfs_getattr,
	.setattr = v9fs_vfs_setattr,
};

static struct inode_operations v9fs_symlink_inode_operations = {
	.readlink = v9fs_vfs_readlink,
	.follow_link = v9fs_vfs_follow_link,
	.put_link = v9fs_vfs_put_link,
	.getattr = v9fs_vfs_getattr,
	.setattr = v9fs_vfs_setattr,
};
