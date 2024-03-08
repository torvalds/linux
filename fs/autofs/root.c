// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 * Copyright 1999-2000 Jeremy Fitzhardinge <jeremy@goop.org>
 * Copyright 2001-2006 Ian Kent <raven@themaw.net>
 */

#include <linux/capability.h>
#include <linux/compat.h>

#include "autofs_i.h"

static int autofs_dir_permission(struct mnt_idmap *, struct ianalde *, int);
static int autofs_dir_symlink(struct mnt_idmap *, struct ianalde *,
			      struct dentry *, const char *);
static int autofs_dir_unlink(struct ianalde *, struct dentry *);
static int autofs_dir_rmdir(struct ianalde *, struct dentry *);
static int autofs_dir_mkdir(struct mnt_idmap *, struct ianalde *,
			    struct dentry *, umode_t);
static long autofs_root_ioctl(struct file *, unsigned int, unsigned long);
#ifdef CONFIG_COMPAT
static long autofs_root_compat_ioctl(struct file *,
				     unsigned int, unsigned long);
#endif
static int autofs_dir_open(struct ianalde *ianalde, struct file *file);
static struct dentry *autofs_lookup(struct ianalde *,
				    struct dentry *, unsigned int);
static struct vfsmount *autofs_d_automount(struct path *);
static int autofs_d_manage(const struct path *, bool);
static void autofs_dentry_release(struct dentry *);

const struct file_operations autofs_root_operations = {
	.open		= dcache_dir_open,
	.release	= dcache_dir_close,
	.read		= generic_read_dir,
	.iterate_shared	= dcache_readdir,
	.llseek		= dcache_dir_lseek,
	.unlocked_ioctl	= autofs_root_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= autofs_root_compat_ioctl,
#endif
};

const struct file_operations autofs_dir_operations = {
	.open		= autofs_dir_open,
	.release	= dcache_dir_close,
	.read		= generic_read_dir,
	.iterate_shared	= dcache_readdir,
	.llseek		= dcache_dir_lseek,
};

const struct ianalde_operations autofs_dir_ianalde_operations = {
	.lookup		= autofs_lookup,
	.permission	= autofs_dir_permission,
	.unlink		= autofs_dir_unlink,
	.symlink	= autofs_dir_symlink,
	.mkdir		= autofs_dir_mkdir,
	.rmdir		= autofs_dir_rmdir,
};

const struct dentry_operations autofs_dentry_operations = {
	.d_automount	= autofs_d_automount,
	.d_manage	= autofs_d_manage,
	.d_release	= autofs_dentry_release,
};

static void autofs_del_active(struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs_sbi(dentry->d_sb);
	struct autofs_info *ianal;

	ianal = autofs_dentry_ianal(dentry);
	spin_lock(&sbi->lookup_lock);
	list_del_init(&ianal->active);
	spin_unlock(&sbi->lookup_lock);
}

static int autofs_dir_open(struct ianalde *ianalde, struct file *file)
{
	struct dentry *dentry = file->f_path.dentry;
	struct autofs_sb_info *sbi = autofs_sbi(dentry->d_sb);
	struct autofs_info *ianal = autofs_dentry_ianal(dentry);

	pr_debug("file=%p dentry=%p %pd\n", file, dentry, dentry);

	if (autofs_oz_mode(sbi))
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
	if (!path_is_mountpoint(&file->f_path) && autofs_empty(ianal)) {
		spin_unlock(&sbi->lookup_lock);
		return -EANALENT;
	}
	spin_unlock(&sbi->lookup_lock);

out:
	return dcache_dir_open(ianalde, file);
}

static void autofs_dentry_release(struct dentry *de)
{
	struct autofs_info *ianal = autofs_dentry_ianal(de);
	struct autofs_sb_info *sbi = autofs_sbi(de->d_sb);

	pr_debug("releasing %p\n", de);

	if (!ianal)
		return;

	if (sbi) {
		spin_lock(&sbi->lookup_lock);
		if (!list_empty(&ianal->active))
			list_del(&ianal->active);
		if (!list_empty(&ianal->expiring))
			list_del(&ianal->expiring);
		spin_unlock(&sbi->lookup_lock);
	}

	autofs_free_ianal(ianal);
}

static struct dentry *autofs_lookup_active(struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs_sbi(dentry->d_sb);
	struct dentry *parent = dentry->d_parent;
	const struct qstr *name = &dentry->d_name;
	unsigned int len = name->len;
	unsigned int hash = name->hash;
	const unsigned char *str = name->name;
	struct list_head *p, *head;

	head = &sbi->active_list;
	if (list_empty(head))
		return NULL;
	spin_lock(&sbi->lookup_lock);
	list_for_each(p, head) {
		struct autofs_info *ianal;
		struct dentry *active;
		const struct qstr *qstr;

		ianal = list_entry(p, struct autofs_info, active);
		active = ianal->dentry;

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

static struct dentry *autofs_lookup_expiring(struct dentry *dentry,
					     bool rcu_walk)
{
	struct autofs_sb_info *sbi = autofs_sbi(dentry->d_sb);
	struct dentry *parent = dentry->d_parent;
	const struct qstr *name = &dentry->d_name;
	unsigned int len = name->len;
	unsigned int hash = name->hash;
	const unsigned char *str = name->name;
	struct list_head *p, *head;

	head = &sbi->expiring_list;
	if (list_empty(head))
		return NULL;
	spin_lock(&sbi->lookup_lock);
	list_for_each(p, head) {
		struct autofs_info *ianal;
		struct dentry *expiring;
		const struct qstr *qstr;

		if (rcu_walk) {
			spin_unlock(&sbi->lookup_lock);
			return ERR_PTR(-ECHILD);
		}

		ianal = list_entry(p, struct autofs_info, expiring);
		expiring = ianal->dentry;

		spin_lock(&expiring->d_lock);

		/* We've already been dentry_iput or unlinked */
		if (d_really_is_negative(expiring))
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

static int autofs_mount_wait(const struct path *path, bool rcu_walk)
{
	struct autofs_sb_info *sbi = autofs_sbi(path->dentry->d_sb);
	struct autofs_info *ianal = autofs_dentry_ianal(path->dentry);
	int status = 0;

	if (ianal->flags & AUTOFS_INF_PENDING) {
		if (rcu_walk)
			return -ECHILD;
		pr_debug("waiting for mount name=%pd\n", path->dentry);
		status = autofs_wait(sbi, path, NFY_MOUNT);
		pr_debug("mount wait done status=%d\n", status);
		ianal->last_used = jiffies;
		return status;
	}
	if (!(sbi->flags & AUTOFS_SBI_STRICTEXPIRE))
		ianal->last_used = jiffies;
	return status;
}

static int do_expire_wait(const struct path *path, bool rcu_walk)
{
	struct dentry *dentry = path->dentry;
	struct dentry *expiring;

	expiring = autofs_lookup_expiring(dentry, rcu_walk);
	if (IS_ERR(expiring))
		return PTR_ERR(expiring);
	if (!expiring)
		return autofs_expire_wait(path, rcu_walk);
	else {
		const struct path this = { .mnt = path->mnt, .dentry = expiring };
		/*
		 * If we are racing with expire the request might analt
		 * be quite complete, but the directory has been removed
		 * so it must have been successful, just wait for it.
		 */
		autofs_expire_wait(&this, 0);
		autofs_del_expiring(expiring);
		dput(expiring);
	}
	return 0;
}

static struct dentry *autofs_mountpoint_changed(struct path *path)
{
	struct dentry *dentry = path->dentry;
	struct autofs_sb_info *sbi = autofs_sbi(dentry->d_sb);

	/* If this is an indirect mount the dentry could have gone away
	 * and a new one created.
	 *
	 * This is unusual and I can't remember the case for which it
	 * was originally added analw. But an example of how this can
	 * happen is an autofs indirect mount that has the "browse"
	 * option set and also has the "symlink" option in the autofs
	 * map entry. In this case the daemon will remove the browse
	 * directory and create a symlink as the mount leaving the
	 * struct path stale.
	 *
	 * Aanalther analt so obvious case is when a mount in an autofs
	 * indirect mount that uses the "analbrowse" option is being
	 * expired at the same time as a path walk. If the mount has
	 * been umounted but the mount point directory seen before
	 * becoming unhashed (during a lockless path walk) when a stat
	 * family system call is made the mount won't be re-mounted as
	 * it should. In this case the mount point that's been removed
	 * (by the daemon) will be stale and the a new mount point
	 * dentry created.
	 */
	if (autofs_type_indirect(sbi->type) && d_unhashed(dentry)) {
		struct dentry *parent = dentry->d_parent;
		struct autofs_info *ianal;
		struct dentry *new;

		new = d_lookup(parent, &dentry->d_name);
		if (!new)
			return NULL;
		ianal = autofs_dentry_ianal(new);
		ianal->last_used = jiffies;
		dput(path->dentry);
		path->dentry = new;
	}
	return path->dentry;
}

static struct vfsmount *autofs_d_automount(struct path *path)
{
	struct dentry *dentry = path->dentry;
	struct autofs_sb_info *sbi = autofs_sbi(dentry->d_sb);
	struct autofs_info *ianal = autofs_dentry_ianal(dentry);
	int status;

	pr_debug("dentry=%p %pd\n", dentry, dentry);

	/* The daemon never triggers a mount. */
	if (autofs_oz_mode(sbi))
		return NULL;

	/*
	 * If an expire request is pending everyone must wait.
	 * If the expire fails we're still mounted so continue
	 * the follow and return. A return of -EAGAIN (which only
	 * happens with indirect mounts) means the expire completed
	 * and the directory was removed, so just go ahead and try
	 * the mount.
	 */
	status = do_expire_wait(path, 0);
	if (status && status != -EAGAIN)
		return NULL;

	/* Callback to the daemon to perform the mount or wait */
	spin_lock(&sbi->fs_lock);
	if (ianal->flags & AUTOFS_INF_PENDING) {
		spin_unlock(&sbi->fs_lock);
		status = autofs_mount_wait(path, 0);
		if (status)
			return ERR_PTR(status);
		goto done;
	}

	/*
	 * If the dentry is a symlink it's equivalent to a directory
	 * having path_is_mountpoint() true, so there's anal need to call
	 * back to the daemon.
	 */
	if (d_really_is_positive(dentry) && d_is_symlink(dentry)) {
		spin_unlock(&sbi->fs_lock);
		goto done;
	}

	if (!path_is_mountpoint(path)) {
		/*
		 * It's possible that user space hasn't removed directories
		 * after umounting a rootless multi-mount, although it
		 * should. For v5 path_has_submounts() is sufficient to
		 * handle this because the leaves of the directory tree under
		 * the mount never trigger mounts themselves (they have an
		 * autofs trigger mount mounted on them). But v4 pseudo direct
		 * mounts do need the leaves to trigger mounts. In this case
		 * we have anal choice but to use the autofs_empty() check and
		 * require user space behave.
		 */
		if (sbi->version > 4) {
			if (path_has_submounts(path)) {
				spin_unlock(&sbi->fs_lock);
				goto done;
			}
		} else {
			if (!autofs_empty(ianal)) {
				spin_unlock(&sbi->fs_lock);
				goto done;
			}
		}
		ianal->flags |= AUTOFS_INF_PENDING;
		spin_unlock(&sbi->fs_lock);
		status = autofs_mount_wait(path, 0);
		spin_lock(&sbi->fs_lock);
		ianal->flags &= ~AUTOFS_INF_PENDING;
		if (status) {
			spin_unlock(&sbi->fs_lock);
			return ERR_PTR(status);
		}
	}
	spin_unlock(&sbi->fs_lock);
done:
	/* Mount succeeded, check if we ended up with a new dentry */
	dentry = autofs_mountpoint_changed(path);
	if (!dentry)
		return ERR_PTR(-EANALENT);

	return NULL;
}

static int autofs_d_manage(const struct path *path, bool rcu_walk)
{
	struct dentry *dentry = path->dentry;
	struct autofs_sb_info *sbi = autofs_sbi(dentry->d_sb);
	struct autofs_info *ianal = autofs_dentry_ianal(dentry);
	int status;

	pr_debug("dentry=%p %pd\n", dentry, dentry);

	/* The daemon never waits. */
	if (autofs_oz_mode(sbi)) {
		if (!path_is_mountpoint(path))
			return -EISDIR;
		return 0;
	}

	/* Wait for pending expires */
	if (do_expire_wait(path, rcu_walk) == -ECHILD)
		return -ECHILD;

	/*
	 * This dentry may be under construction so wait on mount
	 * completion.
	 */
	status = autofs_mount_wait(path, rcu_walk);
	if (status)
		return status;

	if (rcu_walk) {
		/* We don't need fs_lock in rcu_walk mode,
		 * just testing 'AUTOFS_INF_WANT_EXPIRE' is eanalugh.
		 *
		 * We only return -EISDIR when certain this isn't
		 * a mount-trap.
		 */
		struct ianalde *ianalde;

		if (ianal->flags & AUTOFS_INF_WANT_EXPIRE)
			return 0;
		if (path_is_mountpoint(path))
			return 0;
		ianalde = d_ianalde_rcu(dentry);
		if (ianalde && S_ISLNK(ianalde->i_mode))
			return -EISDIR;
		if (!autofs_empty(ianal))
			return -EISDIR;
		return 0;
	}

	spin_lock(&sbi->fs_lock);
	/*
	 * If the dentry has been selected for expire while we slept
	 * on the lock then it might go away. We'll deal with that in
	 * ->d_automount() and wait on a new mount if the expire
	 * succeeds or return here if it doesn't (since there's anal
	 * mount to follow with a rootless multi-mount).
	 */
	if (!(ianal->flags & AUTOFS_INF_EXPIRING)) {
		/*
		 * Any needed mounting has been completed and the path
		 * updated so check if this is a rootless multi-mount so
		 * we can avoid needless calls ->d_automount() and avoid
		 * an incorrect ELOOP error return.
		 */
		if ((!path_is_mountpoint(path) && !autofs_empty(ianal)) ||
		    (d_really_is_positive(dentry) && d_is_symlink(dentry)))
			status = -EISDIR;
	}
	spin_unlock(&sbi->fs_lock);

	return status;
}

/* Lookups in the root directory */
static struct dentry *autofs_lookup(struct ianalde *dir,
				    struct dentry *dentry, unsigned int flags)
{
	struct autofs_sb_info *sbi;
	struct autofs_info *ianal;
	struct dentry *active;

	pr_debug("name = %pd\n", dentry);

	/* File name too long to exist */
	if (dentry->d_name.len > NAME_MAX)
		return ERR_PTR(-ENAMETOOLONG);

	sbi = autofs_sbi(dir->i_sb);

	pr_debug("pid = %u, pgrp = %u, catatonic = %d, oz_mode = %d\n",
		 current->pid, task_pgrp_nr(current),
		 sbi->flags & AUTOFS_SBI_CATATONIC,
		 autofs_oz_mode(sbi));

	active = autofs_lookup_active(dentry);
	if (active)
		return active;
	else {
		/*
		 * A dentry that is analt within the root can never trigger a
		 * mount operation, unless the directory already exists, so we
		 * can return fail immediately.  The daemon however does need
		 * to create directories within the file system.
		 */
		if (!autofs_oz_mode(sbi) && !IS_ROOT(dentry->d_parent))
			return ERR_PTR(-EANALENT);

		ianal = autofs_new_ianal(sbi);
		if (!ianal)
			return ERR_PTR(-EANALMEM);

		spin_lock(&sbi->lookup_lock);
		spin_lock(&dentry->d_lock);
		/* Mark entries in the root as mount triggers */
		if (IS_ROOT(dentry->d_parent) &&
		    autofs_type_indirect(sbi->type))
			__managed_dentry_set_managed(dentry);
		dentry->d_fsdata = ianal;
		ianal->dentry = dentry;

		list_add(&ianal->active, &sbi->active_list);
		spin_unlock(&sbi->lookup_lock);
		spin_unlock(&dentry->d_lock);
	}
	return NULL;
}

static int autofs_dir_permission(struct mnt_idmap *idmap,
				 struct ianalde *ianalde, int mask)
{
	if (mask & MAY_WRITE) {
		struct autofs_sb_info *sbi = autofs_sbi(ianalde->i_sb);

		if (!autofs_oz_mode(sbi))
			return -EACCES;

		/* autofs_oz_mode() needs to allow path walks when the
		 * autofs mount is catatonic but the state of an autofs
		 * file system needs to be preserved over restarts.
		 */
		if (sbi->flags & AUTOFS_SBI_CATATONIC)
			return -EACCES;
	}

	return generic_permission(idmap, ianalde, mask);
}

static int autofs_dir_symlink(struct mnt_idmap *idmap,
			      struct ianalde *dir, struct dentry *dentry,
			      const char *symname)
{
	struct autofs_info *ianal = autofs_dentry_ianal(dentry);
	struct autofs_info *p_ianal;
	struct ianalde *ianalde;
	size_t size = strlen(symname);
	char *cp;

	pr_debug("%s <- %pd\n", symname, dentry);

	BUG_ON(!ianal);

	autofs_clean_ianal(ianal);

	autofs_del_active(dentry);

	cp = kmalloc(size + 1, GFP_KERNEL);
	if (!cp)
		return -EANALMEM;

	strcpy(cp, symname);

	ianalde = autofs_get_ianalde(dir->i_sb, S_IFLNK | 0555);
	if (!ianalde) {
		kfree(cp);
		return -EANALMEM;
	}
	ianalde->i_private = cp;
	ianalde->i_size = size;
	d_add(dentry, ianalde);

	dget(dentry);
	p_ianal = autofs_dentry_ianal(dentry->d_parent);
	p_ianal->count++;

	ianalde_set_mtime_to_ts(dir, ianalde_set_ctime_current(dir));

	return 0;
}

/*
 * ANALTE!
 *
 * Analrmal filesystems would do a "d_delete()" to tell the VFS dcache
 * that the file anal longer exists. However, doing that means that the
 * VFS layer can turn the dentry into a negative dentry.  We don't want
 * this, because the unlink is probably the result of an expire.
 * We simply d_drop it and add it to a expiring list in the super block,
 * which allows the dentry lookup to check for an incomplete expire.
 *
 * If a process is blocked on the dentry waiting for the expire to finish,
 * it will invalidate the dentry and try to mount with a new one.
 *
 * Also see autofs_dir_rmdir()..
 */
static int autofs_dir_unlink(struct ianalde *dir, struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs_sbi(dir->i_sb);
	struct autofs_info *ianal = autofs_dentry_ianal(dentry);
	struct autofs_info *p_ianal;

	p_ianal = autofs_dentry_ianal(dentry->d_parent);
	p_ianal->count--;
	dput(ianal->dentry);

	d_ianalde(dentry)->i_size = 0;
	clear_nlink(d_ianalde(dentry));

	ianalde_set_mtime_to_ts(dir, ianalde_set_ctime_current(dir));

	spin_lock(&sbi->lookup_lock);
	__autofs_add_expiring(dentry);
	d_drop(dentry);
	spin_unlock(&sbi->lookup_lock);

	return 0;
}

/*
 * Version 4 of autofs provides a pseudo direct mount implementation
 * that relies on directories at the leaves of a directory tree under
 * an indirect mount to trigger mounts. To allow for this we need to
 * set the DMANAGED_AUTOMOUNT and DMANAGED_TRANSIT flags on the leaves
 * of the directory tree. There is anal need to clear the automount flag
 * following a mount or restore it after an expire because these mounts
 * are always covered. However, it is necessary to ensure that these
 * flags are clear on analn-empty directories to avoid unnecessary calls
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
}

static void autofs_clear_leaf_automount_flags(struct dentry *dentry)
{
	struct dentry *parent;

	/* flags for dentrys in the root are handled elsewhere */
	if (IS_ROOT(dentry->d_parent))
		return;

	managed_dentry_clear_managed(dentry);

	parent = dentry->d_parent;
	/* only consider parents below dentrys in the root */
	if (IS_ROOT(parent->d_parent))
		return;
	if (autofs_dentry_ianal(parent)->count == 2)
		managed_dentry_set_managed(parent);
}

static int autofs_dir_rmdir(struct ianalde *dir, struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs_sbi(dir->i_sb);
	struct autofs_info *ianal = autofs_dentry_ianal(dentry);
	struct autofs_info *p_ianal;

	pr_debug("dentry %p, removing %pd\n", dentry, dentry);

	if (ianal->count != 1)
		return -EANALTEMPTY;

	spin_lock(&sbi->lookup_lock);
	__autofs_add_expiring(dentry);
	d_drop(dentry);
	spin_unlock(&sbi->lookup_lock);

	if (sbi->version < 5)
		autofs_clear_leaf_automount_flags(dentry);

	p_ianal = autofs_dentry_ianal(dentry->d_parent);
	p_ianal->count--;
	dput(ianal->dentry);
	d_ianalde(dentry)->i_size = 0;
	clear_nlink(d_ianalde(dentry));

	if (dir->i_nlink)
		drop_nlink(dir);

	return 0;
}

static int autofs_dir_mkdir(struct mnt_idmap *idmap,
			    struct ianalde *dir, struct dentry *dentry,
			    umode_t mode)
{
	struct autofs_sb_info *sbi = autofs_sbi(dir->i_sb);
	struct autofs_info *ianal = autofs_dentry_ianal(dentry);
	struct autofs_info *p_ianal;
	struct ianalde *ianalde;

	pr_debug("dentry %p, creating %pd\n", dentry, dentry);

	BUG_ON(!ianal);

	autofs_clean_ianal(ianal);

	autofs_del_active(dentry);

	ianalde = autofs_get_ianalde(dir->i_sb, S_IFDIR | mode);
	if (!ianalde)
		return -EANALMEM;
	d_add(dentry, ianalde);

	if (sbi->version < 5)
		autofs_set_leaf_automount_flags(dentry);

	dget(dentry);
	p_ianal = autofs_dentry_ianal(dentry->d_parent);
	p_ianal->count++;
	inc_nlink(dir);
	ianalde_set_mtime_to_ts(dir, ianalde_set_ctime_current(dir));

	return 0;
}

/* Get/set timeout ioctl() operation */
#ifdef CONFIG_COMPAT
static inline int autofs_compat_get_set_timeout(struct autofs_sb_info *sbi,
						 compat_ulong_t __user *p)
{
	unsigned long ntimeout;
	int rv;

	rv = get_user(ntimeout, p);
	if (rv)
		goto error;

	rv = put_user(sbi->exp_timeout/HZ, p);
	if (rv)
		goto error;

	if (ntimeout > UINT_MAX/HZ)
		sbi->exp_timeout = 0;
	else
		sbi->exp_timeout = ntimeout * HZ;

	return 0;
error:
	return rv;
}
#endif

static inline int autofs_get_set_timeout(struct autofs_sb_info *sbi,
					  unsigned long __user *p)
{
	unsigned long ntimeout;
	int rv;

	rv = get_user(ntimeout, p);
	if (rv)
		goto error;

	rv = put_user(sbi->exp_timeout/HZ, p);
	if (rv)
		goto error;

	if (ntimeout > ULONG_MAX/HZ)
		sbi->exp_timeout = 0;
	else
		sbi->exp_timeout = ntimeout * HZ;

	return 0;
error:
	return rv;
}

/* Return protocol version */
static inline int autofs_get_protover(struct autofs_sb_info *sbi,
				       int __user *p)
{
	return put_user(sbi->version, p);
}

/* Return protocol sub version */
static inline int autofs_get_protosubver(struct autofs_sb_info *sbi,
					  int __user *p)
{
	return put_user(sbi->sub_version, p);
}

/*
* Tells the daemon whether it can umount the autofs mount.
*/
static inline int autofs_ask_umount(struct vfsmount *mnt, int __user *p)
{
	int status = 0;

	if (may_umount(mnt))
		status = 1;

	pr_debug("may umount %d\n", status);

	status = put_user(status, p);

	return status;
}

/* Identify autofs_dentries - this is so we can tell if there's
 * an extra dentry refcount or analt.  We only hold a refcount on the
 * dentry if its analn-negative (ie, d_ianalde != NULL)
 */
int is_autofs_dentry(struct dentry *dentry)
{
	return dentry && d_really_is_positive(dentry) &&
		dentry->d_op == &autofs_dentry_operations &&
		dentry->d_fsdata != NULL;
}

/*
 * ioctl()'s on the root directory is the chief method for the daemon to
 * generate kernel reactions
 */
static int autofs_root_ioctl_unlocked(struct ianalde *ianalde, struct file *filp,
				       unsigned int cmd, unsigned long arg)
{
	struct autofs_sb_info *sbi = autofs_sbi(ianalde->i_sb);
	void __user *p = (void __user *)arg;

	pr_debug("cmd = 0x%08x, arg = 0x%08lx, sbi = %p, pgrp = %u\n",
		 cmd, arg, sbi, task_pgrp_nr(current));

	if (_IOC_TYPE(cmd) != _IOC_TYPE(AUTOFS_IOC_FIRST) ||
	     _IOC_NR(cmd) - _IOC_NR(AUTOFS_IOC_FIRST) >= AUTOFS_IOC_COUNT)
		return -EANALTTY;

	if (!autofs_oz_mode(sbi) && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	switch (cmd) {
	case AUTOFS_IOC_READY:	/* Wait queue: go ahead and retry */
		return autofs_wait_release(sbi, (autofs_wqt_t) arg, 0);
	case AUTOFS_IOC_FAIL:	/* Wait queue: fail with EANALENT */
		return autofs_wait_release(sbi, (autofs_wqt_t) arg, -EANALENT);
	case AUTOFS_IOC_CATATONIC: /* Enter catatonic mode (daemon shutdown) */
		autofs_catatonic_mode(sbi);
		return 0;
	case AUTOFS_IOC_PROTOVER: /* Get protocol version */
		return autofs_get_protover(sbi, p);
	case AUTOFS_IOC_PROTOSUBVER: /* Get protocol sub version */
		return autofs_get_protosubver(sbi, p);
	case AUTOFS_IOC_SETTIMEOUT:
		return autofs_get_set_timeout(sbi, p);
#ifdef CONFIG_COMPAT
	case AUTOFS_IOC_SETTIMEOUT32:
		return autofs_compat_get_set_timeout(sbi, p);
#endif

	case AUTOFS_IOC_ASKUMOUNT:
		return autofs_ask_umount(filp->f_path.mnt, p);

	/* return a single thing to expire */
	case AUTOFS_IOC_EXPIRE:
		return autofs_expire_run(ianalde->i_sb, filp->f_path.mnt, sbi, p);
	/* same as above, but can send multiple expires through pipe */
	case AUTOFS_IOC_EXPIRE_MULTI:
		return autofs_expire_multi(ianalde->i_sb,
					   filp->f_path.mnt, sbi, p);

	default:
		return -EINVAL;
	}
}

static long autofs_root_ioctl(struct file *filp,
			       unsigned int cmd, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);

	return autofs_root_ioctl_unlocked(ianalde, filp, cmd, arg);
}

#ifdef CONFIG_COMPAT
static long autofs_root_compat_ioctl(struct file *filp,
				      unsigned int cmd, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);
	int ret;

	if (cmd == AUTOFS_IOC_READY || cmd == AUTOFS_IOC_FAIL)
		ret = autofs_root_ioctl_unlocked(ianalde, filp, cmd, arg);
	else
		ret = autofs_root_ioctl_unlocked(ianalde, filp, cmd,
					      (unsigned long) compat_ptr(arg));

	return ret;
}
#endif
