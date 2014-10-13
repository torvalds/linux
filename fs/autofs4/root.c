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
#include <linux/slab.h>
#include <linux/param.h>
#include <linux/time.h>
#include <linux/compat.h>
#include <linux/mutex.h>

#include "autofs_i.h"

static int autofs4_dir_symlink(struct inode *,struct dentry *,const char *);
static int autofs4_dir_unlink(struct inode *,struct dentry *);
static int autofs4_dir_rmdir(struct inode *,struct dentry *);
static int autofs4_dir_mkdir(struct inode *,struct dentry *,umode_t);
static long autofs4_root_ioctl(struct file *,unsigned int,unsigned long);
#ifdef CONFIG_COMPAT
static long autofs4_root_compat_ioctl(struct file *,unsigned int,unsigned long);
#endif
static int autofs4_dir_open(struct inode *inode, struct file *file);
static struct dentry *autofs4_lookup(struct inode *,struct dentry *, unsigned int);
static struct vfsmount *autofs4_d_automount(struct path *);
static int autofs4_d_manage(struct dentry *, bool);
static void autofs4_dentry_release(struct dentry *);

const struct file_operations autofs4_root_operations = {
	.open		= dcache_dir_open,
	.release	= dcache_dir_close,
	.read		= generic_read_dir,
	.iterate	= dcache_readdir,
	.llseek		= dcache_dir_lseek,
	.unlocked_ioctl	= autofs4_root_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= autofs4_root_compat_ioctl,
#endif
};

const struct file_operations autofs4_dir_operations = {
	.open		= autofs4_dir_open,
	.release	= dcache_dir_close,
	.read		= generic_read_dir,
	.iterate	= dcache_readdir,
	.llseek		= dcache_dir_lseek,
};

const struct inode_operations autofs4_dir_inode_operations = {
	.lookup		= autofs4_lookup,
	.unlink		= autofs4_dir_unlink,
	.symlink	= autofs4_dir_symlink,
	.mkdir		= autofs4_dir_mkdir,
	.rmdir		= autofs4_dir_rmdir,
};

const struct dentry_operations autofs4_dentry_operations = {
	.d_automount	= autofs4_d_automount,
	.d_manage	= autofs4_d_manage,
	.d_release	= autofs4_dentry_release,
};

static void autofs4_add_active(struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	if (ino) {
		spin_lock(&sbi->lookup_lock);
		if (!ino->active_count) {
			if (list_empty(&ino->active))
				list_add(&ino->active, &sbi->active_list);
		}
		ino->active_count++;
		spin_unlock(&sbi->lookup_lock);
	}
	return;
}

static void autofs4_del_active(struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	if (ino) {
		spin_lock(&sbi->lookup_lock);
		ino->active_count--;
		if (!ino->active_count) {
			if (!list_empty(&ino->active))
				list_del_init(&ino->active);
		}
		spin_unlock(&sbi->lookup_lock);
	}
	return;
}

static int autofs4_dir_open(struct inode *inode, struct file *file)
{
	struct dentry *dentry = file->f_path.dentry;
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);

	DPRINTK("file=%p dentry=%p %.*s",
		file, dentry, dentry->d_name.len, dentry->d_name.name);

	if (autofs4_oz_mode(sbi))
		goto out;

	/*
	 * An empty directory in an autofs file system is always a
	 * mount point. The daemon must have failed to mount this
	 * during lookup so it doesn't exist. This can happen, for
	 * example, if user space returns an incorrect status for a
	 * mount request. Otherwise we're doing a readdir on the
	 * autofs file system so just let the libfs routines handle
	 * it.
	 */
	spin_lock(&sbi->lookup_lock);
	if (!d_mountpoint(dentry) && simple_empty(dentry)) {
		spin_unlock(&sbi->lookup_lock);
		return -ENOENT;
	}
	spin_unlock(&sbi->lookup_lock);

out:
	return dcache_dir_open(inode, file);
}

static void autofs4_dentry_release(struct dentry *de)
{
	struct autofs_info *ino = autofs4_dentry_ino(de);
	struct autofs_sb_info *sbi = autofs4_sbi(de->d_sb);

	DPRINTK("releasing %p", de);

	if (!ino)
		return;

	if (sbi) {
		spin_lock(&sbi->lookup_lock);
		if (!list_empty(&ino->active))
			list_del(&ino->active);
		if (!list_empty(&ino->expiring))
			list_del(&ino->expiring);
		spin_unlock(&sbi->lookup_lock);
	}

	autofs4_free_ino(ino);
}

static struct dentry *autofs4_lookup_active(struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	struct dentry *parent = dentry->d_parent;
	struct qstr *name = &dentry->d_name;
	unsigned int len = name->len;
	unsigned int hash = name->hash;
	const unsigned char *str = name->name;
	struct list_head *p, *head;

	head = &sbi->active_list;
	if (list_empty(head))
		return NULL;
	spin_lock(&sbi->lookup_lock);
	list_for_each(p, head) {
		struct autofs_info *ino;
		struct dentry *active;
		struct qstr *qstr;

		ino = list_entry(p, struct autofs_info, active);
		active = ino->dentry;

		spin_lock(&active->d_lock);

		/* Already gone? */
		if ((int) d_count(active) <= 0)
			goto next;

		qstr = &active->d_name;

		if (active->d_name.hash != hash)
			goto next;
		if (active->d_parent != parent)
			goto next;

		if (qstr->len != len)
			goto next;
		if (memcmp(qstr->name, str, len))
			goto next;

		if (d_unhashed(active)) {
			dget_dlock(active);
			spin_unlock(&active->d_lock);
			spin_unlock(&sbi->lookup_lock);
			return active;
		}
next:
		spin_unlock(&active->d_lock);
	}
	spin_unlock(&sbi->lookup_lock);

	return NULL;
}

static struct dentry *autofs4_lookup_expiring(struct dentry *dentry,
					      bool rcu_walk)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	struct dentry *parent = dentry->d_parent;
	struct qstr *name = &dentry->d_name;
	unsigned int len = name->len;
	unsigned int hash = name->hash;
	const unsigned char *str = name->name;
	struct list_head *p, *head;

	head = &sbi->expiring_list;
	if (list_empty(head))
		return NULL;
	spin_lock(&sbi->lookup_lock);
	list_for_each(p, head) {
		struct autofs_info *ino;
		struct dentry *expiring;
		struct qstr *qstr;

		if (rcu_walk) {
			spin_unlock(&sbi->lookup_lock);
			return ERR_PTR(-ECHILD);
		}

		ino = list_entry(p, struct autofs_info, expiring);
		expiring = ino->dentry;

		spin_lock(&expiring->d_lock);

		/* We've already been dentry_iput or unlinked */
		if (!expiring->d_inode)
			goto next;

		qstr = &expiring->d_name;

		if (expiring->d_name.hash != hash)
			goto next;
		if (expiring->d_parent != parent)
			goto next;

		if (qstr->len != len)
			goto next;
		if (memcmp(qstr->name, str, len))
			goto next;

		if (d_unhashed(expiring)) {
			dget_dlock(expiring);
			spin_unlock(&expiring->d_lock);
			spin_unlock(&sbi->lookup_lock);
			return expiring;
		}
next:
		spin_unlock(&expiring->d_lock);
	}
	spin_unlock(&sbi->lookup_lock);

	return NULL;
}

static int autofs4_mount_wait(struct dentry *dentry, bool rcu_walk)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	int status = 0;

	if (ino->flags & AUTOFS_INF_PENDING) {
		if (rcu_walk)
			return -ECHILD;
		DPRINTK("waiting for mount name=%.*s",
			dentry->d_name.len, dentry->d_name.name);
		status = autofs4_wait(sbi, dentry, NFY_MOUNT);
		DPRINTK("mount wait done status=%d", status);
	}
	ino->last_used = jiffies;
	return status;
}

static int do_expire_wait(struct dentry *dentry, bool rcu_walk)
{
	struct dentry *expiring;

	expiring = autofs4_lookup_expiring(dentry, rcu_walk);
	if (IS_ERR(expiring))
		return PTR_ERR(expiring);
	if (!expiring)
		return autofs4_expire_wait(dentry, rcu_walk);
	else {
		/*
		 * If we are racing with expire the request might not
		 * be quite complete, but the directory has been removed
		 * so it must have been successful, just wait for it.
		 */
		autofs4_expire_wait(expiring, 0);
		autofs4_del_expiring(expiring);
		dput(expiring);
	}
	return 0;
}

static struct dentry *autofs4_mountpoint_changed(struct path *path)
{
	struct dentry *dentry = path->dentry;
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);

	/*
	 * If this is an indirect mount the dentry could have gone away
	 * as a result of an expire and a new one created.
	 */
	if (autofs_type_indirect(sbi->type) && d_unhashed(dentry)) {
		struct dentry *parent = dentry->d_parent;
		struct autofs_info *ino;
		struct dentry *new = d_lookup(parent, &dentry->d_name);
		if (!new)
			return NULL;
		ino = autofs4_dentry_ino(new);
		ino->last_used = jiffies;
		dput(path->dentry);
		path->dentry = new;
	}
	return path->dentry;
}

static struct vfsmount *autofs4_d_automount(struct path *path)
{
	struct dentry *dentry = path->dentry;
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	int status;

	DPRINTK("dentry=%p %.*s",
		dentry, dentry->d_name.len, dentry->d_name.name);

	/* The daemon never triggers a mount. */
	if (autofs4_oz_mode(sbi))
		return NULL;

	/*
	 * If an expire request is pending everyone must wait.
	 * If the expire fails we're still mounted so continue
	 * the follow and return. A return of -EAGAIN (which only
	 * happens with indirect mounts) means the expire completed
	 * and the directory was removed, so just go ahead and try
	 * the mount.
	 */
	status = do_expire_wait(dentry, 0);
	if (status && status != -EAGAIN)
		return NULL;

	/* Callback to the daemon to perform the mount or wait */
	spin_lock(&sbi->fs_lock);
	if (ino->flags & AUTOFS_INF_PENDING) {
		spin_unlock(&sbi->fs_lock);
		status = autofs4_mount_wait(dentry, 0);
		if (status)
			return ERR_PTR(status);
		goto done;
	}

	/*
	 * If the dentry is a symlink it's equivalent to a directory
	 * having d_mountpoint() true, so there's no need to call back
	 * to the daemon.
	 */
	if (dentry->d_inode && S_ISLNK(dentry->d_inode->i_mode)) {
		spin_unlock(&sbi->fs_lock);
		goto done;
	}

	if (!d_mountpoint(dentry)) {
		/*
		 * It's possible that user space hasn't removed directories
		 * after umounting a rootless multi-mount, although it
		 * should. For v5 have_submounts() is sufficient to handle
		 * this because the leaves of the directory tree under the
		 * mount never trigger mounts themselves (they have an autofs
		 * trigger mount mounted on them). But v4 pseudo direct mounts
		 * do need the leaves to trigger mounts. In this case we
		 * have no choice but to use the list_empty() check and
		 * require user space behave.
		 */
		if (sbi->version > 4) {
			if (have_submounts(dentry)) {
				spin_unlock(&sbi->fs_lock);
				goto done;
			}
		} else {
			if (!simple_empty(dentry)) {
				spin_unlock(&sbi->fs_lock);
				goto done;
			}
		}
		ino->flags |= AUTOFS_INF_PENDING;
		spin_unlock(&sbi->fs_lock);
		status = autofs4_mount_wait(dentry, 0);
		spin_lock(&sbi->fs_lock);
		ino->flags &= ~AUTOFS_INF_PENDING;
		if (status) {
			spin_unlock(&sbi->fs_lock);
			return ERR_PTR(status);
		}
	}
	spin_unlock(&sbi->fs_lock);
done:
	/* Mount succeeded, check if we ended up with a new dentry */
	dentry = autofs4_mountpoint_changed(path);
	if (!dentry)
		return ERR_PTR(-ENOENT);

	return NULL;
}

static int autofs4_d_manage(struct dentry *dentry, bool rcu_walk)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	int status;

	DPRINTK("dentry=%p %.*s",
		dentry, dentry->d_name.len, dentry->d_name.name);

	/* The daemon never waits. */
	if (autofs4_oz_mode(sbi)) {
		if (rcu_walk)
			return 0;
		if (!d_mountpoint(dentry))
			return -EISDIR;
		return 0;
	}

	/* Wait for pending expires */
	if (do_expire_wait(dentry, rcu_walk) == -ECHILD)
		return -ECHILD;

	/*
	 * This dentry may be under construction so wait on mount
	 * completion.
	 */
	status = autofs4_mount_wait(dentry, rcu_walk);
	if (status)
		return status;

	if (rcu_walk)
		/* it is always safe to return 0 as the worst that
		 * will happen is we retry in REF-walk mode.
		 * Better than always taking a lock.
		 */
		return 0;

	spin_lock(&sbi->fs_lock);
	/*
	 * If the dentry has been selected for expire while we slept
	 * on the lock then it might go away. We'll deal with that in
	 * ->d_automount() and wait on a new mount if the expire
	 * succeeds or return here if it doesn't (since there's no
	 * mount to follow with a rootless multi-mount).
	 */
	if (!(ino->flags & AUTOFS_INF_EXPIRING)) {
		/*
		 * Any needed mounting has been completed and the path
		 * updated so check if this is a rootless multi-mount so
		 * we can avoid needless calls ->d_automount() and avoid
		 * an incorrect ELOOP error return.
		 */
		if ((!d_mountpoint(dentry) && !simple_empty(dentry)) ||
		    (dentry->d_inode && S_ISLNK(dentry->d_inode->i_mode)))
			status = -EISDIR;
	}
	spin_unlock(&sbi->fs_lock);

	return status;
}

/* Lookups in the root directory */
static struct dentry *autofs4_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
	struct autofs_sb_info *sbi;
	struct autofs_info *ino;
	struct dentry *active;

	DPRINTK("name = %.*s", dentry->d_name.len, dentry->d_name.name);

	/* File name too long to exist */
	if (dentry->d_name.len > NAME_MAX)
		return ERR_PTR(-ENAMETOOLONG);

	sbi = autofs4_sbi(dir->i_sb);

	DPRINTK("pid = %u, pgrp = %u, catatonic = %d, oz_mode = %d",
		current->pid, task_pgrp_nr(current), sbi->catatonic,
		autofs4_oz_mode(sbi));

	active = autofs4_lookup_active(dentry);
	if (active) {
		return active;
	} else {
		/*
		 * A dentry that is not within the root can never trigger a
		 * mount operation, unless the directory already exists, so we
		 * can return fail immediately.  The daemon however does need
		 * to create directories within the file system.
		 */
		if (!autofs4_oz_mode(sbi) && !IS_ROOT(dentry->d_parent))
			return ERR_PTR(-ENOENT);

		/* Mark entries in the root as mount triggers */
		if (autofs_type_indirect(sbi->type) && IS_ROOT(dentry->d_parent))
			__managed_dentry_set_managed(dentry);

		ino = autofs4_new_ino(sbi);
		if (!ino)
			return ERR_PTR(-ENOMEM);

		dentry->d_fsdata = ino;
		ino->dentry = dentry;

		autofs4_add_active(dentry);

		d_instantiate(dentry, NULL);
	}
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
	size_t size = strlen(symname);
	char *cp;

	DPRINTK("%s <- %.*s", symname,
		dentry->d_name.len, dentry->d_name.name);

	if (!autofs4_oz_mode(sbi))
		return -EACCES;

	BUG_ON(!ino);

	autofs4_clean_ino(ino);

	autofs4_del_active(dentry);

	cp = kmalloc(size + 1, GFP_KERNEL);
	if (!cp)
		return -ENOMEM;

	strcpy(cp, symname);

	inode = autofs4_get_inode(dir->i_sb, S_IFLNK | 0555);
	if (!inode) {
		kfree(cp);
		if (!dentry->d_fsdata)
			kfree(ino);
		return -ENOMEM;
	}
	inode->i_private = cp;
	inode->i_size = size;
	d_add(dentry, inode);

	dget(dentry);
	atomic_inc(&ino->count);
	p_ino = autofs4_dentry_ino(dentry->d_parent);
	if (p_ino && !IS_ROOT(dentry))
		atomic_inc(&p_ino->count);

	dir->i_mtime = CURRENT_TIME;

	return 0;
}

/*
 * NOTE!
 *
 * Normal filesystems would do a "d_delete()" to tell the VFS dcache
 * that the file no longer exists. However, doing that means that the
 * VFS layer can turn the dentry into a negative dentry.  We don't want
 * this, because the unlink is probably the result of an expire.
 * We simply d_drop it and add it to a expiring list in the super block,
 * which allows the dentry lookup to check for an incomplete expire.
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
	if (!autofs4_oz_mode(sbi) && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (atomic_dec_and_test(&ino->count)) {
		p_ino = autofs4_dentry_ino(dentry->d_parent);
		if (p_ino && !IS_ROOT(dentry))
			atomic_dec(&p_ino->count);
	}
	dput(ino->dentry);

	dentry->d_inode->i_size = 0;
	clear_nlink(dentry->d_inode);

	dir->i_mtime = CURRENT_TIME;

	spin_lock(&sbi->lookup_lock);
	__autofs4_add_expiring(dentry);
	d_drop(dentry);
	spin_unlock(&sbi->lookup_lock);

	return 0;
}

/*
 * Version 4 of autofs provides a pseudo direct mount implementation
 * that relies on directories at the leaves of a directory tree under
 * an indirect mount to trigger mounts. To allow for this we need to
 * set the DMANAGED_AUTOMOUNT and DMANAGED_TRANSIT flags on the leaves
 * of the directory tree. There is no need to clear the automount flag
 * following a mount or restore it after an expire because these mounts
 * are always covered. However, it is necessary to ensure that these
 * flags are clear on non-empty directories to avoid unnecessary calls
 * during path walks.
 */
static void autofs_set_leaf_automount_flags(struct dentry *dentry)
{
	struct dentry *parent;

	/* root and dentrys in the root are already handled */
	if (IS_ROOT(dentry->d_parent))
		return;

	managed_dentry_set_managed(dentry);

	parent = dentry->d_parent;
	/* only consider parents below dentrys in the root */
	if (IS_ROOT(parent->d_parent))
		return;
	managed_dentry_clear_managed(parent);
	return;
}

static void autofs_clear_leaf_automount_flags(struct dentry *dentry)
{
	struct list_head *d_child;
	struct dentry *parent;

	/* flags for dentrys in the root are handled elsewhere */
	if (IS_ROOT(dentry->d_parent))
		return;

	managed_dentry_clear_managed(dentry);

	parent = dentry->d_parent;
	/* only consider parents below dentrys in the root */
	if (IS_ROOT(parent->d_parent))
		return;
	d_child = &dentry->d_u.d_child;
	/* Set parent managed if it's becoming empty */
	if (d_child->next == &parent->d_subdirs &&
	    d_child->prev == &parent->d_subdirs)
		managed_dentry_set_managed(parent);
	return;
}

static int autofs4_dir_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dir->i_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	struct autofs_info *p_ino;
	
	DPRINTK("dentry %p, removing %.*s",
		dentry, dentry->d_name.len, dentry->d_name.name);

	if (!autofs4_oz_mode(sbi))
		return -EACCES;

	spin_lock(&sbi->lookup_lock);
	if (!simple_empty(dentry)) {
		spin_unlock(&sbi->lookup_lock);
		return -ENOTEMPTY;
	}
	__autofs4_add_expiring(dentry);
	d_drop(dentry);
	spin_unlock(&sbi->lookup_lock);

	if (sbi->version < 5)
		autofs_clear_leaf_automount_flags(dentry);

	if (atomic_dec_and_test(&ino->count)) {
		p_ino = autofs4_dentry_ino(dentry->d_parent);
		if (p_ino && dentry->d_parent != dentry)
			atomic_dec(&p_ino->count);
	}
	dput(ino->dentry);
	dentry->d_inode->i_size = 0;
	clear_nlink(dentry->d_inode);

	if (dir->i_nlink)
		drop_nlink(dir);

	return 0;
}

static int autofs4_dir_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dir->i_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	struct autofs_info *p_ino;
	struct inode *inode;

	if (!autofs4_oz_mode(sbi))
		return -EACCES;

	DPRINTK("dentry %p, creating %.*s",
		dentry, dentry->d_name.len, dentry->d_name.name);

	BUG_ON(!ino);

	autofs4_clean_ino(ino);

	autofs4_del_active(dentry);

	inode = autofs4_get_inode(dir->i_sb, S_IFDIR | 0555);
	if (!inode)
		return -ENOMEM;
	d_add(dentry, inode);

	if (sbi->version < 5)
		autofs_set_leaf_automount_flags(dentry);

	dget(dentry);
	atomic_inc(&ino->count);
	p_ino = autofs4_dentry_ino(dentry->d_parent);
	if (p_ino && !IS_ROOT(dentry))
		atomic_inc(&p_ino->count);
	inc_nlink(dir);
	dir->i_mtime = CURRENT_TIME;

	return 0;
}

/* Get/set timeout ioctl() operation */
#ifdef CONFIG_COMPAT
static inline int autofs4_compat_get_set_timeout(struct autofs_sb_info *sbi,
					 compat_ulong_t __user *p)
{
	int rv;
	unsigned long ntimeout;

	if ((rv = get_user(ntimeout, p)) ||
	     (rv = put_user(sbi->exp_timeout/HZ, p)))
		return rv;

	if (ntimeout > UINT_MAX/HZ)
		sbi->exp_timeout = 0;
	else
		sbi->exp_timeout = ntimeout * HZ;

	return 0;
}
#endif

static inline int autofs4_get_set_timeout(struct autofs_sb_info *sbi,
					 unsigned long __user *p)
{
	int rv;
	unsigned long ntimeout;

	if ((rv = get_user(ntimeout, p)) ||
	     (rv = put_user(sbi->exp_timeout/HZ, p)))
		return rv;

	if (ntimeout > ULONG_MAX/HZ)
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
		dentry->d_op == &autofs4_dentry_operations &&
		dentry->d_fsdata != NULL;
}

/*
 * ioctl()'s on the root directory is the chief method for the daemon to
 * generate kernel reactions
 */
static int autofs4_root_ioctl_unlocked(struct inode *inode, struct file *filp,
				       unsigned int cmd, unsigned long arg)
{
	struct autofs_sb_info *sbi = autofs4_sbi(inode->i_sb);
	void __user *p = (void __user *)arg;

	DPRINTK("cmd = 0x%08x, arg = 0x%08lx, sbi = %p, pgrp = %u",
		cmd,arg,sbi,task_pgrp_nr(current));

	if (_IOC_TYPE(cmd) != _IOC_TYPE(AUTOFS_IOC_FIRST) ||
	     _IOC_NR(cmd) - _IOC_NR(AUTOFS_IOC_FIRST) >= AUTOFS_IOC_COUNT)
		return -ENOTTY;
	
	if (!autofs4_oz_mode(sbi) && !capable(CAP_SYS_ADMIN))
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
#ifdef CONFIG_COMPAT
	case AUTOFS_IOC_SETTIMEOUT32:
		return autofs4_compat_get_set_timeout(sbi, p);
#endif

	case AUTOFS_IOC_ASKUMOUNT:
		return autofs4_ask_umount(filp->f_path.mnt, p);

	/* return a single thing to expire */
	case AUTOFS_IOC_EXPIRE:
		return autofs4_expire_run(inode->i_sb,filp->f_path.mnt,sbi, p);
	/* same as above, but can send multiple expires through pipe */
	case AUTOFS_IOC_EXPIRE_MULTI:
		return autofs4_expire_multi(inode->i_sb,filp->f_path.mnt,sbi, p);

	default:
		return -ENOSYS;
	}
}

static long autofs4_root_ioctl(struct file *filp,
			       unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	return autofs4_root_ioctl_unlocked(inode, filp, cmd, arg);
}

#ifdef CONFIG_COMPAT
static long autofs4_root_compat_ioctl(struct file *filp,
			     unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	int ret;

	if (cmd == AUTOFS_IOC_READY || cmd == AUTOFS_IOC_FAIL)
		ret = autofs4_root_ioctl_unlocked(inode, filp, cmd, arg);
	else
		ret = autofs4_root_ioctl_unlocked(inode, filp, cmd,
			(unsigned long)compat_ptr(arg));

	return ret;
}
#endif
