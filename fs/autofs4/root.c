/* -*- c -*- --------------------------------------------------------------- *
 *
 * linux/fs/autofs/root.c
 *
 *  Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 *  Copyright 1999-2000 Jeremy Fitzhardinge <jeremy@goop.org>
 *  Copyright 2001-2006 Ian Kent <raven@themaw.net>
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/param.h>
#include <linux/time.h>
#include <linux/smp_lock.h>
#include "autofs_i.h"

static int autofs4_dir_symlink(struct inode *,struct dentry *,const char *);
static int autofs4_dir_unlink(struct inode *,struct dentry *);
static int autofs4_dir_rmdir(struct inode *,struct dentry *);
static int autofs4_dir_mkdir(struct inode *,struct dentry *,int);
static int autofs4_root_ioctl(struct inode *, struct file *,unsigned int,unsigned long);
static int autofs4_dir_open(struct inode *inode, struct file *file);
static int autofs4_dir_close(struct inode *inode, struct file *file);
static int autofs4_dir_readdir(struct file * filp, void * dirent, filldir_t filldir);
static int autofs4_root_readdir(struct file * filp, void * dirent, filldir_t filldir);
static struct dentry *autofs4_lookup(struct inode *,struct dentry *, struct nameidata *);
static void *autofs4_follow_link(struct dentry *, struct nameidata *);

const struct file_operations autofs4_root_operations = {
	.open		= dcache_dir_open,
	.release	= dcache_dir_close,
	.read		= generic_read_dir,
	.readdir	= autofs4_root_readdir,
	.ioctl		= autofs4_root_ioctl,
};

const struct file_operations autofs4_dir_operations = {
	.open		= autofs4_dir_open,
	.release	= autofs4_dir_close,
	.read		= generic_read_dir,
	.readdir	= autofs4_dir_readdir,
};

struct inode_operations autofs4_indirect_root_inode_operations = {
	.lookup		= autofs4_lookup,
	.unlink		= autofs4_dir_unlink,
	.symlink	= autofs4_dir_symlink,
	.mkdir		= autofs4_dir_mkdir,
	.rmdir		= autofs4_dir_rmdir,
};

struct inode_operations autofs4_direct_root_inode_operations = {
	.lookup		= autofs4_lookup,
	.unlink		= autofs4_dir_unlink,
	.mkdir		= autofs4_dir_mkdir,
	.rmdir		= autofs4_dir_rmdir,
	.follow_link	= autofs4_follow_link,
};

struct inode_operations autofs4_dir_inode_operations = {
	.lookup		= autofs4_lookup,
	.unlink		= autofs4_dir_unlink,
	.symlink	= autofs4_dir_symlink,
	.mkdir		= autofs4_dir_mkdir,
	.rmdir		= autofs4_dir_rmdir,
};

static int autofs4_root_readdir(struct file *file, void *dirent,
				filldir_t filldir)
{
	struct autofs_sb_info *sbi = autofs4_sbi(file->f_dentry->d_sb);
	int oz_mode = autofs4_oz_mode(sbi);

	DPRINTK("called, filp->f_pos = %lld", file->f_pos);

	/*
	 * Don't set reghost flag if:
	 * 1) f_pos is larger than zero -- we've already been here.
	 * 2) we haven't even enabled reghosting in the 1st place.
	 * 3) this is the daemon doing a readdir
	 */
	if (oz_mode && file->f_pos == 0 && sbi->reghost_enabled)
		sbi->needs_reghost = 1;

	DPRINTK("needs_reghost = %d", sbi->needs_reghost);

	return dcache_readdir(file, dirent, filldir);
}

static int autofs4_dir_open(struct inode *inode, struct file *file)
{
	struct dentry *dentry = file->f_dentry;
	struct vfsmount *mnt = file->f_vfsmnt;
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	struct dentry *cursor;
	int status;

	status = dcache_dir_open(inode, file);
	if (status)
		goto out;

	cursor = file->private_data;
	cursor->d_fsdata = NULL;

	DPRINTK("file=%p dentry=%p %.*s",
		file, dentry, dentry->d_name.len, dentry->d_name.name);

	if (autofs4_oz_mode(sbi))
		goto out;

	if (autofs4_ispending(dentry)) {
		DPRINTK("dentry busy");
		dcache_dir_close(inode, file);
		status = -EBUSY;
		goto out;
	}

	status = -ENOENT;
	if (!d_mountpoint(dentry) && dentry->d_op && dentry->d_op->d_revalidate) {
		struct nameidata nd;
		int empty, ret;

		/* In case there are stale directory dentrys from a failed mount */
		spin_lock(&dcache_lock);
		empty = list_empty(&dentry->d_subdirs);
		spin_unlock(&dcache_lock);

		if (!empty)
			d_invalidate(dentry);

		nd.flags = LOOKUP_DIRECTORY;
		ret = (dentry->d_op->d_revalidate)(dentry, &nd);

		if (!ret) {
			dcache_dir_close(inode, file);
			goto out;
		}
	}

	if (d_mountpoint(dentry)) {
		struct file *fp = NULL;
		struct vfsmount *fp_mnt = mntget(mnt);
		struct dentry *fp_dentry = dget(dentry);

		if (!autofs4_follow_mount(&fp_mnt, &fp_dentry)) {
			dput(fp_dentry);
			mntput(fp_mnt);
			dcache_dir_close(inode, file);
			goto out;
		}

		fp = dentry_open(fp_dentry, fp_mnt, file->f_flags);
		status = PTR_ERR(fp);
		if (IS_ERR(fp)) {
			dcache_dir_close(inode, file);
			goto out;
		}
		cursor->d_fsdata = fp;
	}
	return 0;
out:
	return status;
}

static int autofs4_dir_close(struct inode *inode, struct file *file)
{
	struct dentry *dentry = file->f_dentry;
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	struct dentry *cursor = file->private_data;
	int status = 0;

	DPRINTK("file=%p dentry=%p %.*s",
		file, dentry, dentry->d_name.len, dentry->d_name.name);

	if (autofs4_oz_mode(sbi))
		goto out;

	if (autofs4_ispending(dentry)) {
		DPRINTK("dentry busy");
		status = -EBUSY;
		goto out;
	}

	if (d_mountpoint(dentry)) {
		struct file *fp = cursor->d_fsdata;
		if (!fp) {
			status = -ENOENT;
			goto out;
		}
		filp_close(fp, current->files);
	}
out:
	dcache_dir_close(inode, file);
	return status;
}

static int autofs4_dir_readdir(struct file *file, void *dirent, filldir_t filldir)
{
	struct dentry *dentry = file->f_dentry;
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	struct dentry *cursor = file->private_data;
	int status;

	DPRINTK("file=%p dentry=%p %.*s",
		file, dentry, dentry->d_name.len, dentry->d_name.name);

	if (autofs4_oz_mode(sbi))
		goto out;

	if (autofs4_ispending(dentry)) {
		DPRINTK("dentry busy");
		return -EBUSY;
	}

	if (d_mountpoint(dentry)) {
		struct file *fp = cursor->d_fsdata;

		if (!fp)
			return -ENOENT;

		if (!fp->f_op || !fp->f_op->readdir)
			goto out;

		status = vfs_readdir(fp, filldir, dirent);
		file->f_pos = fp->f_pos;
		if (status)
			autofs4_copy_atime(file, fp);
		return status;
	}
out:
	return dcache_readdir(file, dirent, filldir);
}

static int try_to_fill_dentry(struct dentry *dentry, int flags)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	int status = 0;

	/* Block on any pending expiry here; invalidate the dentry
           when expiration is done to trigger mount request with a new
           dentry */
	if (ino && (ino->flags & AUTOFS_INF_EXPIRING)) {
		DPRINTK("waiting for expire %p name=%.*s",
			 dentry, dentry->d_name.len, dentry->d_name.name);

		status = autofs4_wait(sbi, dentry, NFY_NONE);

		DPRINTK("expire done status=%d", status);

		/*
		 * If the directory still exists the mount request must
		 * continue otherwise it can't be followed at the right
		 * time during the walk.
		 */
		status = d_invalidate(dentry);
		if (status != -EBUSY)
			return -ENOENT;
	}

	DPRINTK("dentry=%p %.*s ino=%p",
		 dentry, dentry->d_name.len, dentry->d_name.name, dentry->d_inode);

	/*
	 * Wait for a pending mount, triggering one if there
	 * isn't one already
	 */
	if (dentry->d_inode == NULL) {
		DPRINTK("waiting for mount name=%.*s",
			 dentry->d_name.len, dentry->d_name.name);

		status = autofs4_wait(sbi, dentry, NFY_MOUNT);

		DPRINTK("mount done status=%d", status);

		if (status && dentry->d_inode)
			return status; /* Try to get the kernel to invalidate this dentry */

		/* Turn this into a real negative dentry? */
		if (status == -ENOENT) {
			spin_lock(&dentry->d_lock);
			dentry->d_flags &= ~DCACHE_AUTOFS_PENDING;
			spin_unlock(&dentry->d_lock);
			return status;
		} else if (status) {
			/* Return a negative dentry, but leave it "pending" */
			return status;
		}
	/* Trigger mount for path component or follow link */
	} else if (flags & (LOOKUP_CONTINUE | LOOKUP_DIRECTORY) ||
			current->link_count) {
		DPRINTK("waiting for mount name=%.*s",
			dentry->d_name.len, dentry->d_name.name);

		spin_lock(&dentry->d_lock);
		dentry->d_flags |= DCACHE_AUTOFS_PENDING;
		spin_unlock(&dentry->d_lock);
		status = autofs4_wait(sbi, dentry, NFY_MOUNT);

		DPRINTK("mount done status=%d", status);

		if (status) {
			spin_lock(&dentry->d_lock);
			dentry->d_flags &= ~DCACHE_AUTOFS_PENDING;
			spin_unlock(&dentry->d_lock);
			return status;
		}
	}

	/* Initialize expiry counter after successful mount */
	if (ino)
		ino->last_used = jiffies;

	spin_lock(&dentry->d_lock);
	dentry->d_flags &= ~DCACHE_AUTOFS_PENDING;
	spin_unlock(&dentry->d_lock);
	return status;
}

/* For autofs direct mounts the follow link triggers the mount */
static void *autofs4_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	int oz_mode = autofs4_oz_mode(sbi);
	unsigned int lookup_type;
	int status;

	DPRINTK("dentry=%p %.*s oz_mode=%d nd->flags=%d",
		dentry, dentry->d_name.len, dentry->d_name.name, oz_mode,
		nd->flags);

	/* If it's our master or we shouldn't trigger a mount we're done */
	lookup_type = nd->flags & (LOOKUP_CONTINUE | LOOKUP_DIRECTORY);
	if (oz_mode || !lookup_type)
		goto done;

	/*
	 * If a request is pending wait for it.
	 * If it's a mount then it won't be expired till at least
	 * a liitle later and if it's an expire then we might need
	 * to mount it again.
	 */
	if (autofs4_ispending(dentry)) {
		DPRINTK("waiting for active request %p name=%.*s",
			dentry, dentry->d_name.len, dentry->d_name.name);

		status = autofs4_wait(sbi, dentry, NFY_NONE);

		DPRINTK("request done status=%d", status);
	}

	/*
	 * If the dentry contains directories then it is an
	 * autofs multi-mount with no root mount offset. So
	 * don't try to mount it again.
	 */
	spin_lock(&dcache_lock);
	if (!d_mountpoint(dentry) && list_empty(&dentry->d_subdirs)) {
		spin_unlock(&dcache_lock);

		status = try_to_fill_dentry(dentry, 0);
		if (status)
			goto out_error;

		/*
		 * The mount succeeded but if there is no root mount
		 * it must be an autofs multi-mount with no root offset
		 * so we don't need to follow the mount.
		 */
		if (d_mountpoint(dentry)) {
			if (!autofs4_follow_mount(&nd->mnt, &nd->dentry)) {
				status = -ENOENT;
				goto out_error;
			}
		}

		goto done;
	}
	spin_unlock(&dcache_lock);

done:
	return NULL;

out_error:
	path_release(nd);
	return ERR_PTR(status);
}

/*
 * Revalidate is called on every cache lookup.  Some of those
 * cache lookups may actually happen while the dentry is not
 * yet completely filled in, and revalidate has to delay such
 * lookups..
 */
static int autofs4_revalidate(struct dentry *dentry, struct nameidata *nd)
{
	struct inode *dir = dentry->d_parent->d_inode;
	struct autofs_sb_info *sbi = autofs4_sbi(dir->i_sb);
	int oz_mode = autofs4_oz_mode(sbi);
	int flags = nd ? nd->flags : 0;
	int status = 0;

	/* Pending dentry */
	if (autofs4_ispending(dentry)) {
		if (!oz_mode)
			status = try_to_fill_dentry(dentry, flags);
		return !status;
	}

	/* Negative dentry.. invalidate if "old" */
	if (dentry->d_inode == NULL)
		return 0;

	/* Check for a non-mountpoint directory with no contents */
	spin_lock(&dcache_lock);
	if (S_ISDIR(dentry->d_inode->i_mode) &&
	    !d_mountpoint(dentry) && 
	    __simple_empty(dentry)) {
		DPRINTK("dentry=%p %.*s, emptydir",
			 dentry, dentry->d_name.len, dentry->d_name.name);
		spin_unlock(&dcache_lock);
		if (!oz_mode)
			status = try_to_fill_dentry(dentry, flags);
		return !status;
	}
	spin_unlock(&dcache_lock);

	return 1;
}

void autofs4_dentry_release(struct dentry *de)
{
	struct autofs_info *inf;

	DPRINTK("releasing %p", de);

	inf = autofs4_dentry_ino(de);
	de->d_fsdata = NULL;

	if (inf) {
		inf->dentry = NULL;
		inf->inode = NULL;

		autofs4_free_ino(inf);
	}
}

/* For dentries of directories in the root dir */
static struct dentry_operations autofs4_root_dentry_operations = {
	.d_revalidate	= autofs4_revalidate,
	.d_release	= autofs4_dentry_release,
};

/* For other dentries */
static struct dentry_operations autofs4_dentry_operations = {
	.d_revalidate	= autofs4_revalidate,
	.d_release	= autofs4_dentry_release,
};

/* Lookups in the root directory */
static struct dentry *autofs4_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
{
	struct autofs_sb_info *sbi;
	int oz_mode;

	DPRINTK("name = %.*s",
		dentry->d_name.len, dentry->d_name.name);

	/* File name too long to exist */
	if (dentry->d_name.len > NAME_MAX)
		return ERR_PTR(-ENAMETOOLONG);

	sbi = autofs4_sbi(dir->i_sb);
	oz_mode = autofs4_oz_mode(sbi);

	DPRINTK("pid = %u, pgrp = %u, catatonic = %d, oz_mode = %d",
		 current->pid, process_group(current), sbi->catatonic, oz_mode);

	/*
	 * Mark the dentry incomplete, but add it. This is needed so
	 * that the VFS layer knows about the dentry, and we can count
	 * on catching any lookups through the revalidate.
	 *
	 * Let all the hard work be done by the revalidate function that
	 * needs to be able to do this anyway..
	 *
	 * We need to do this before we release the directory semaphore.
	 */
	dentry->d_op = &autofs4_root_dentry_operations;

	if (!oz_mode) {
		spin_lock(&dentry->d_lock);
		dentry->d_flags |= DCACHE_AUTOFS_PENDING;
		spin_unlock(&dentry->d_lock);
	}
	dentry->d_fsdata = NULL;
	d_add(dentry, NULL);

	if (dentry->d_op && dentry->d_op->d_revalidate) {
		mutex_unlock(&dir->i_mutex);
		(dentry->d_op->d_revalidate)(dentry, nd);
		mutex_lock(&dir->i_mutex);
	}

	/*
	 * If we are still pending, check if we had to handle
	 * a signal. If so we can force a restart..
	 */
	if (dentry->d_flags & DCACHE_AUTOFS_PENDING) {
		/* See if we were interrupted */
		if (signal_pending(current)) {
			sigset_t *sigset = &current->pending.signal;
			if (sigismember (sigset, SIGKILL) ||
			    sigismember (sigset, SIGQUIT) ||
			    sigismember (sigset, SIGINT)) {
			    return ERR_PTR(-ERESTARTNOINTR);
			}
		}
	}

	/*
	 * If this dentry is unhashed, then we shouldn't honour this
	 * lookup even if the dentry is positive.  Returning ENOENT here
	 * doesn't do the right thing for all system calls, but it should
	 * be OK for the operations we permit from an autofs.
	 */
	if (dentry->d_inode && d_unhashed(dentry))
		return ERR_PTR(-ENOENT);

	return NULL;
}

static int autofs4_dir_symlink(struct inode *dir, 
			       struct dentry *dentry,
			       const char *symname)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dir->i_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	struct autofs_info *p_ino;
	struct inode *inode;
	char *cp;

	DPRINTK("%s <- %.*s", symname,
		dentry->d_name.len, dentry->d_name.name);

	if (!autofs4_oz_mode(sbi))
		return -EACCES;

	ino = autofs4_init_ino(ino, sbi, S_IFLNK | 0555);
	if (ino == NULL)
		return -ENOSPC;

	ino->size = strlen(symname);
	ino->u.symlink = cp = kmalloc(ino->size + 1, GFP_KERNEL);

	if (cp == NULL) {
		kfree(ino);
		return -ENOSPC;
	}

	strcpy(cp, symname);

	inode = autofs4_get_inode(dir->i_sb, ino);
	d_instantiate(dentry, inode);

	if (dir == dir->i_sb->s_root->d_inode)
		dentry->d_op = &autofs4_root_dentry_operations;
	else
		dentry->d_op = &autofs4_dentry_operations;

	dentry->d_fsdata = ino;
	ino->dentry = dget(dentry);
	atomic_inc(&ino->count);
	p_ino = autofs4_dentry_ino(dentry->d_parent);
	if (p_ino && dentry->d_parent != dentry)
		atomic_inc(&p_ino->count);
	ino->inode = inode;

	dir->i_mtime = CURRENT_TIME;

	return 0;
}

/*
 * NOTE!
 *
 * Normal filesystems would do a "d_delete()" to tell the VFS dcache
 * that the file no longer exists. However, doing that means that the
 * VFS layer can turn the dentry into a negative dentry.  We don't want
 * this, because since the unlink is probably the result of an expire.
 * We simply d_drop it, which allows the dentry lookup to remount it
 * if necessary.
 *
 * If a process is blocked on the dentry waiting for the expire to finish,
 * it will invalidate the dentry and try to mount with a new one.
 *
 * Also see autofs4_dir_rmdir()..
 */
static int autofs4_dir_unlink(struct inode *dir, struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dir->i_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	struct autofs_info *p_ino;
	
	/* This allows root to remove symlinks */
	if ( !autofs4_oz_mode(sbi) && !capable(CAP_SYS_ADMIN) )
		return -EACCES;

	if (atomic_dec_and_test(&ino->count)) {
		p_ino = autofs4_dentry_ino(dentry->d_parent);
		if (p_ino && dentry->d_parent != dentry)
			atomic_dec(&p_ino->count);
	}
	dput(ino->dentry);

	dentry->d_inode->i_size = 0;
	dentry->d_inode->i_nlink = 0;

	dir->i_mtime = CURRENT_TIME;

	d_drop(dentry);

	return 0;
}

static int autofs4_dir_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dir->i_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	struct autofs_info *p_ino;
	
	if (!autofs4_oz_mode(sbi))
		return -EACCES;

	spin_lock(&dcache_lock);
	if (!list_empty(&dentry->d_subdirs)) {
		spin_unlock(&dcache_lock);
		return -ENOTEMPTY;
	}
	spin_lock(&dentry->d_lock);
	__d_drop(dentry);
	spin_unlock(&dentry->d_lock);
	spin_unlock(&dcache_lock);

	if (atomic_dec_and_test(&ino->count)) {
		p_ino = autofs4_dentry_ino(dentry->d_parent);
		if (p_ino && dentry->d_parent != dentry)
			atomic_dec(&p_ino->count);
	}
	dput(ino->dentry);
	dentry->d_inode->i_size = 0;
	dentry->d_inode->i_nlink = 0;

	if (dir->i_nlink)
		dir->i_nlink--;

	return 0;
}

static int autofs4_dir_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dir->i_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	struct autofs_info *p_ino;
	struct inode *inode;

	if ( !autofs4_oz_mode(sbi) )
		return -EACCES;

	DPRINTK("dentry %p, creating %.*s",
		dentry, dentry->d_name.len, dentry->d_name.name);

	ino = autofs4_init_ino(ino, sbi, S_IFDIR | 0555);
	if (ino == NULL)
		return -ENOSPC;

	inode = autofs4_get_inode(dir->i_sb, ino);
	d_instantiate(dentry, inode);

	if (dir == dir->i_sb->s_root->d_inode)
		dentry->d_op = &autofs4_root_dentry_operations;
	else
		dentry->d_op = &autofs4_dentry_operations;

	dentry->d_fsdata = ino;
	ino->dentry = dget(dentry);
	atomic_inc(&ino->count);
	p_ino = autofs4_dentry_ino(dentry->d_parent);
	if (p_ino && dentry->d_parent != dentry)
		atomic_inc(&p_ino->count);
	ino->inode = inode;
	dir->i_nlink++;
	dir->i_mtime = CURRENT_TIME;

	return 0;
}

/* Get/set timeout ioctl() operation */
static inline int autofs4_get_set_timeout(struct autofs_sb_info *sbi,
					 unsigned long __user *p)
{
	int rv;
	unsigned long ntimeout;

	if ( (rv = get_user(ntimeout, p)) ||
	     (rv = put_user(sbi->exp_timeout/HZ, p)) )
		return rv;

	if ( ntimeout > ULONG_MAX/HZ )
		sbi->exp_timeout = 0;
	else
		sbi->exp_timeout = ntimeout * HZ;

	return 0;
}

/* Return protocol version */
static inline int autofs4_get_protover(struct autofs_sb_info *sbi, int __user *p)
{
	return put_user(sbi->version, p);
}

/* Return protocol sub version */
static inline int autofs4_get_protosubver(struct autofs_sb_info *sbi, int __user *p)
{
	return put_user(sbi->sub_version, p);
}

/*
 * Tells the daemon whether we need to reghost or not. Also, clears
 * the reghost_needed flag.
 */
static inline int autofs4_ask_reghost(struct autofs_sb_info *sbi, int __user *p)
{
	int status;

	DPRINTK("returning %d", sbi->needs_reghost);

	status = put_user(sbi->needs_reghost, p);
	if ( status )
		return status;

	sbi->needs_reghost = 0;
	return 0;
}

/*
 * Enable / Disable reghosting ioctl() operation
 */
static inline int autofs4_toggle_reghost(struct autofs_sb_info *sbi, int __user *p)
{
	int status;
	int val;

	status = get_user(val, p);

	DPRINTK("reghost = %d", val);

	if (status)
		return status;

	/* turn on/off reghosting, with the val */
	sbi->reghost_enabled = val;
	return 0;
}

/*
* Tells the daemon whether it can umount the autofs mount.
*/
static inline int autofs4_ask_umount(struct vfsmount *mnt, int __user *p)
{
	int status = 0;

	if (may_umount(mnt))
		status = 1;

	DPRINTK("returning %d", status);

	status = put_user(status, p);

	return status;
}

/* Identify autofs4_dentries - this is so we can tell if there's
   an extra dentry refcount or not.  We only hold a refcount on the
   dentry if its non-negative (ie, d_inode != NULL)
*/
int is_autofs4_dentry(struct dentry *dentry)
{
	return dentry && dentry->d_inode &&
		(dentry->d_op == &autofs4_root_dentry_operations ||
		 dentry->d_op == &autofs4_dentry_operations) &&
		dentry->d_fsdata != NULL;
}

/*
 * ioctl()'s on the root directory is the chief method for the daemon to
 * generate kernel reactions
 */
static int autofs4_root_ioctl(struct inode *inode, struct file *filp,
			     unsigned int cmd, unsigned long arg)
{
	struct autofs_sb_info *sbi = autofs4_sbi(inode->i_sb);
	void __user *p = (void __user *)arg;

	DPRINTK("cmd = 0x%08x, arg = 0x%08lx, sbi = %p, pgrp = %u",
		cmd,arg,sbi,process_group(current));

	if ( _IOC_TYPE(cmd) != _IOC_TYPE(AUTOFS_IOC_FIRST) ||
	     _IOC_NR(cmd) - _IOC_NR(AUTOFS_IOC_FIRST) >= AUTOFS_IOC_COUNT )
		return -ENOTTY;
	
	if ( !autofs4_oz_mode(sbi) && !capable(CAP_SYS_ADMIN) )
		return -EPERM;
	
	switch(cmd) {
	case AUTOFS_IOC_READY:	/* Wait queue: go ahead and retry */
		return autofs4_wait_release(sbi,(autofs_wqt_t)arg,0);
	case AUTOFS_IOC_FAIL:	/* Wait queue: fail with ENOENT */
		return autofs4_wait_release(sbi,(autofs_wqt_t)arg,-ENOENT);
	case AUTOFS_IOC_CATATONIC: /* Enter catatonic mode (daemon shutdown) */
		autofs4_catatonic_mode(sbi);
		return 0;
	case AUTOFS_IOC_PROTOVER: /* Get protocol version */
		return autofs4_get_protover(sbi, p);
	case AUTOFS_IOC_PROTOSUBVER: /* Get protocol sub version */
		return autofs4_get_protosubver(sbi, p);
	case AUTOFS_IOC_SETTIMEOUT:
		return autofs4_get_set_timeout(sbi, p);

	case AUTOFS_IOC_TOGGLEREGHOST:
		return autofs4_toggle_reghost(sbi, p);
	case AUTOFS_IOC_ASKREGHOST:
		return autofs4_ask_reghost(sbi, p);

	case AUTOFS_IOC_ASKUMOUNT:
		return autofs4_ask_umount(filp->f_vfsmnt, p);

	/* return a single thing to expire */
	case AUTOFS_IOC_EXPIRE:
		return autofs4_expire_run(inode->i_sb,filp->f_vfsmnt,sbi, p);
	/* same as above, but can send multiple expires through pipe */
	case AUTOFS_IOC_EXPIRE_MULTI:
		return autofs4_expire_multi(inode->i_sb,filp->f_vfsmnt,sbi, p);

	default:
		return -ENOSYS;
	}
}
