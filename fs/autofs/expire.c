// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 * Copyright 1999-2000 Jeremy Fitzhardinge <jeremy@goop.org>
 * Copyright 2001-2006 Ian Kent <raven@themaw.net>
 */

#include "autofs_i.h"

/* Check if a dentry can be expired */
static inline int autofs_can_expire(struct dentry *dentry,
				    unsigned long timeout, unsigned int how)
{
	struct autofs_info *ino = autofs_dentry_ino(dentry);

	/* dentry in the process of being deleted */
	if (ino == NULL)
		return 0;

	if (!(how & AUTOFS_EXP_IMMEDIATE)) {
		/* Too young to die */
		if (!timeout || time_after(ino->last_used + timeout, jiffies))
			return 0;
	}
	return 1;
}

/* Check a mount point for busyness */
static int autofs_mount_busy(struct vfsmount *mnt,
			     struct dentry *dentry, unsigned int how)
{
	struct dentry *top = dentry;
	struct path path = {.mnt = mnt, .dentry = dentry};
	int status = 1;

	pr_debug("dentry %p %pd\n", dentry, dentry);

	path_get(&path);

	if (!follow_down_one(&path))
		goto done;

	if (is_autofs_dentry(path.dentry)) {
		struct autofs_sb_info *sbi = autofs_sbi(path.dentry->d_sb);

		/* This is an autofs submount, we can't expire it */
		if (autofs_type_indirect(sbi->type))
			goto done;
	}

	/* Not a submount, has a forced expire been requested */
	if (how & AUTOFS_EXP_FORCED) {
		status = 0;
		goto done;
	}

	/* Update the expiry counter if fs is busy */
	if (!may_umount_tree(path.mnt)) {
		struct autofs_info *ino;

		ino = autofs_dentry_ino(top);
		ino->last_used = jiffies;
		goto done;
	}

	status = 0;
done:
	pr_debug("returning = %d\n", status);
	path_put(&path);
	return status;
}

/* p->d_lock held */
static struct dentry *positive_after(struct dentry *p, struct dentry *child)
{
	if (child)
		child = list_next_entry(child, d_child);
	else
		child = list_first_entry(&p->d_subdirs, struct dentry, d_child);

	list_for_each_entry_from(child, &p->d_subdirs, d_child) {
		spin_lock_nested(&child->d_lock, DENTRY_D_LOCK_NESTED);
		if (simple_positive(child)) {
			dget_dlock(child);
			spin_unlock(&child->d_lock);
			return child;
		}
		spin_unlock(&child->d_lock);
	}

	return NULL;
}

/*
 * Calculate and dget next entry in the subdirs list under root.
 */
static struct dentry *get_next_positive_subdir(struct dentry *prev,
					       struct dentry *root)
{
	struct autofs_sb_info *sbi = autofs_sbi(root->d_sb);
	struct dentry *q;

	spin_lock(&sbi->lookup_lock);
	spin_lock(&root->d_lock);
	q = positive_after(root, prev);
	spin_unlock(&root->d_lock);
	spin_unlock(&sbi->lookup_lock);
	dput(prev);
	return q;
}

/*
 * Calculate and dget next entry in top down tree traversal.
 */
static struct dentry *get_next_positive_dentry(struct dentry *prev,
					       struct dentry *root)
{
	struct autofs_sb_info *sbi = autofs_sbi(root->d_sb);
	struct dentry *p = prev, *ret = NULL, *d = NULL;

	if (prev == NULL)
		return dget(root);

	spin_lock(&sbi->lookup_lock);
	spin_lock(&p->d_lock);
	while (1) {
		struct dentry *parent;

		ret = positive_after(p, d);
		if (ret || p == root)
			break;
		parent = p->d_parent;
		spin_unlock(&p->d_lock);
		spin_lock(&parent->d_lock);
		d = p;
		p = parent;
	}
	spin_unlock(&p->d_lock);
	spin_unlock(&sbi->lookup_lock);
	dput(prev);
	return ret;
}

/*
 * Check a direct mount point for busyness.
 * Direct mounts have similar expiry semantics to tree mounts.
 * The tree is not busy iff no mountpoints are busy and there are no
 * autofs submounts.
 */
static int autofs_direct_busy(struct vfsmount *mnt,
			      struct dentry *top,
			      unsigned long timeout,
			      unsigned int how)
{
	pr_debug("top %p %pd\n", top, top);

	/* Forced expire, user space handles busy mounts */
	if (how & AUTOFS_EXP_FORCED)
		return 0;

	/* If it's busy update the expiry counters */
	if (!may_umount_tree(mnt)) {
		struct autofs_info *ino;

		ino = autofs_dentry_ino(top);
		if (ino)
			ino->last_used = jiffies;
		return 1;
	}

	/* Timeout of a direct mount is determined by its top dentry */
	if (!autofs_can_expire(top, timeout, how))
		return 1;

	return 0;
}

/*
 * Check a directory tree of mount points for busyness
 * The tree is not busy iff no mountpoints are busy
 */
static int autofs_tree_busy(struct vfsmount *mnt,
			    struct dentry *top,
			    unsigned long timeout,
			    unsigned int how)
{
	struct autofs_info *top_ino = autofs_dentry_ino(top);
	struct dentry *p;

	pr_debug("top %p %pd\n", top, top);

	/* Negative dentry - give up */
	if (!simple_positive(top))
		return 1;

	p = NULL;
	while ((p = get_next_positive_dentry(p, top))) {
		pr_debug("dentry %p %pd\n", p, p);

		/*
		 * Is someone visiting anywhere in the subtree ?
		 * If there's no mount we need to check the usage
		 * count for the autofs dentry.
		 * If the fs is busy update the expiry counter.
		 */
		if (d_mountpoint(p)) {
			if (autofs_mount_busy(mnt, p, how)) {
				top_ino->last_used = jiffies;
				dput(p);
				return 1;
			}
		} else {
			struct autofs_info *ino = autofs_dentry_ino(p);
			unsigned int ino_count = atomic_read(&ino->count);

			/* allow for dget above and top is already dgot */
			if (p == top)
				ino_count += 2;
			else
				ino_count++;

			if (d_count(p) > ino_count) {
				top_ino->last_used = jiffies;
				dput(p);
				return 1;
			}
		}
	}

	/* Forced expire, user space handles busy mounts */
	if (how & AUTOFS_EXP_FORCED)
		return 0;

	/* Timeout of a tree mount is ultimately determined by its top dentry */
	if (!autofs_can_expire(top, timeout, how))
		return 1;

	return 0;
}

static struct dentry *autofs_check_leaves(struct vfsmount *mnt,
					  struct dentry *parent,
					  unsigned long timeout,
					  unsigned int how)
{
	struct dentry *p;

	pr_debug("parent %p %pd\n", parent, parent);

	p = NULL;
	while ((p = get_next_positive_dentry(p, parent))) {
		pr_debug("dentry %p %pd\n", p, p);

		if (d_mountpoint(p)) {
			/* Can we umount this guy */
			if (autofs_mount_busy(mnt, p, how))
				continue;

			/* This isn't a submount so if a forced expire
			 * has been requested, user space handles busy
			 * mounts */
			if (how & AUTOFS_EXP_FORCED)
				return p;

			/* Can we expire this guy */
			if (autofs_can_expire(p, timeout, how))
				return p;
		}
	}
	return NULL;
}

/* Check if we can expire a direct mount (possibly a tree) */
static struct dentry *autofs_expire_direct(struct super_block *sb,
					   struct vfsmount *mnt,
					   struct autofs_sb_info *sbi,
					   unsigned int how)
{
	struct dentry *root = dget(sb->s_root);
	struct autofs_info *ino;
	unsigned long timeout;

	if (!root)
		return NULL;

	timeout = sbi->exp_timeout;

	if (!autofs_direct_busy(mnt, root, timeout, how)) {
		spin_lock(&sbi->fs_lock);
		ino = autofs_dentry_ino(root);
		/* No point expiring a pending mount */
		if (ino->flags & AUTOFS_INF_PENDING) {
			spin_unlock(&sbi->fs_lock);
			goto out;
		}
		ino->flags |= AUTOFS_INF_WANT_EXPIRE;
		spin_unlock(&sbi->fs_lock);
		synchronize_rcu();
		if (!autofs_direct_busy(mnt, root, timeout, how)) {
			spin_lock(&sbi->fs_lock);
			ino->flags |= AUTOFS_INF_EXPIRING;
			init_completion(&ino->expire_complete);
			spin_unlock(&sbi->fs_lock);
			return root;
		}
		spin_lock(&sbi->fs_lock);
		ino->flags &= ~AUTOFS_INF_WANT_EXPIRE;
		spin_unlock(&sbi->fs_lock);
	}
out:
	dput(root);

	return NULL;
}

/* Check if 'dentry' should expire, or return a nearby
 * dentry that is suitable.
 * If returned dentry is different from arg dentry,
 * then a dget() reference was taken, else not.
 */
static struct dentry *should_expire(struct dentry *dentry,
				    struct vfsmount *mnt,
				    unsigned long timeout,
				    unsigned int how)
{
	struct autofs_info *ino = autofs_dentry_ino(dentry);
	unsigned int ino_count;

	/* No point expiring a pending mount */
	if (ino->flags & AUTOFS_INF_PENDING)
		return NULL;

	/*
	 * Case 1: (i) indirect mount or top level pseudo direct mount
	 *	   (autofs-4.1).
	 *	   (ii) indirect mount with offset mount, check the "/"
	 *	   offset (autofs-5.0+).
	 */
	if (d_mountpoint(dentry)) {
		pr_debug("checking mountpoint %p %pd\n", dentry, dentry);

		/* Can we umount this guy */
		if (autofs_mount_busy(mnt, dentry, how))
			return NULL;

		/* This isn't a submount so if a forced expire
		 * has been requested, user space handles busy
		 * mounts */
		if (how & AUTOFS_EXP_FORCED)
			return dentry;

		/* Can we expire this guy */
		if (autofs_can_expire(dentry, timeout, how))
			return dentry;
		return NULL;
	}

	if (d_really_is_positive(dentry) && d_is_symlink(dentry)) {
		pr_debug("checking symlink %p %pd\n", dentry, dentry);

		/* Forced expire, user space handles busy mounts */
		if (how & AUTOFS_EXP_FORCED)
			return dentry;

		/*
		 * A symlink can't be "busy" in the usual sense so
		 * just check last used for expire timeout.
		 */
		if (autofs_can_expire(dentry, timeout, how))
			return dentry;
		return NULL;
	}

	if (simple_empty(dentry))
		return NULL;

	/* Case 2: tree mount, expire iff entire tree is not busy */
	if (!(how & AUTOFS_EXP_LEAVES)) {
		/* Not a forced expire? */
		if (!(how & AUTOFS_EXP_FORCED)) {
			/* ref-walk currently on this dentry? */
			ino_count = atomic_read(&ino->count) + 1;
			if (d_count(dentry) > ino_count)
				return NULL;
		}

		if (!autofs_tree_busy(mnt, dentry, timeout, how))
			return dentry;
	/*
	 * Case 3: pseudo direct mount, expire individual leaves
	 *	   (autofs-4.1).
	 */
	} else {
		struct dentry *expired;

		/* Not a forced expire? */
		if (!(how & AUTOFS_EXP_FORCED)) {
			/* ref-walk currently on this dentry? */
			ino_count = atomic_read(&ino->count) + 1;
			if (d_count(dentry) > ino_count)
				return NULL;
		}

		expired = autofs_check_leaves(mnt, dentry, timeout, how);
		if (expired) {
			if (expired == dentry)
				dput(dentry);
			return expired;
		}
	}
	return NULL;
}

/*
 * Find an eligible tree to time-out
 * A tree is eligible if :-
 *  - it is unused by any user process
 *  - it has been unused for exp_timeout time
 */
static struct dentry *autofs_expire_indirect(struct super_block *sb,
					     struct vfsmount *mnt,
					     struct autofs_sb_info *sbi,
					     unsigned int how)
{
	unsigned long timeout;
	struct dentry *root = sb->s_root;
	struct dentry *dentry;
	struct dentry *expired;
	struct dentry *found;
	struct autofs_info *ino;

	if (!root)
		return NULL;

	timeout = sbi->exp_timeout;

	dentry = NULL;
	while ((dentry = get_next_positive_subdir(dentry, root))) {
		spin_lock(&sbi->fs_lock);
		ino = autofs_dentry_ino(dentry);
		if (ino->flags & AUTOFS_INF_WANT_EXPIRE) {
			spin_unlock(&sbi->fs_lock);
			continue;
		}
		spin_unlock(&sbi->fs_lock);

		expired = should_expire(dentry, mnt, timeout, how);
		if (!expired)
			continue;

		spin_lock(&sbi->fs_lock);
		ino = autofs_dentry_ino(expired);
		ino->flags |= AUTOFS_INF_WANT_EXPIRE;
		spin_unlock(&sbi->fs_lock);
		synchronize_rcu();

		/* Make sure a reference is not taken on found if
		 * things have changed.
		 */
		how &= ~AUTOFS_EXP_LEAVES;
		found = should_expire(expired, mnt, timeout, how);
		if (found != expired) { // something has changed, continue
			dput(found);
			goto next;
		}

		if (expired != dentry)
			dput(dentry);

		spin_lock(&sbi->fs_lock);
		goto found;
next:
		spin_lock(&sbi->fs_lock);
		ino->flags &= ~AUTOFS_INF_WANT_EXPIRE;
		spin_unlock(&sbi->fs_lock);
		if (expired != dentry)
			dput(expired);
	}
	return NULL;

found:
	pr_debug("returning %p %pd\n", expired, expired);
	ino->flags |= AUTOFS_INF_EXPIRING;
	init_completion(&ino->expire_complete);
	spin_unlock(&sbi->fs_lock);
	return expired;
}

int autofs_expire_wait(const struct path *path, int rcu_walk)
{
	struct dentry *dentry = path->dentry;
	struct autofs_sb_info *sbi = autofs_sbi(dentry->d_sb);
	struct autofs_info *ino = autofs_dentry_ino(dentry);
	int status;
	int state;

	/* Block on any pending expire */
	if (!(ino->flags & AUTOFS_INF_WANT_EXPIRE))
		return 0;
	if (rcu_walk)
		return -ECHILD;

retry:
	spin_lock(&sbi->fs_lock);
	state = ino->flags & (AUTOFS_INF_WANT_EXPIRE | AUTOFS_INF_EXPIRING);
	if (state == AUTOFS_INF_WANT_EXPIRE) {
		spin_unlock(&sbi->fs_lock);
		/*
		 * Possibly being selected for expire, wait until
		 * it's selected or not.
		 */
		schedule_timeout_uninterruptible(HZ/10);
		goto retry;
	}
	if (state & AUTOFS_INF_EXPIRING) {
		spin_unlock(&sbi->fs_lock);

		pr_debug("waiting for expire %p name=%pd\n", dentry, dentry);

		status = autofs_wait(sbi, path, NFY_NONE);
		wait_for_completion(&ino->expire_complete);

		pr_debug("expire done status=%d\n", status);

		if (d_unhashed(dentry))
			return -EAGAIN;

		return status;
	}
	spin_unlock(&sbi->fs_lock);

	return 0;
}

/* Perform an expiry operation */
int autofs_expire_run(struct super_block *sb,
		      struct vfsmount *mnt,
		      struct autofs_sb_info *sbi,
		      struct autofs_packet_expire __user *pkt_p)
{
	struct autofs_packet_expire pkt;
	struct autofs_info *ino;
	struct dentry *dentry;
	int ret = 0;

	memset(&pkt, 0, sizeof(pkt));

	pkt.hdr.proto_version = sbi->version;
	pkt.hdr.type = autofs_ptype_expire;

	dentry = autofs_expire_indirect(sb, mnt, sbi, 0);
	if (!dentry)
		return -EAGAIN;

	pkt.len = dentry->d_name.len;
	memcpy(pkt.name, dentry->d_name.name, pkt.len);
	pkt.name[pkt.len] = '\0';

	if (copy_to_user(pkt_p, &pkt, sizeof(struct autofs_packet_expire)))
		ret = -EFAULT;

	spin_lock(&sbi->fs_lock);
	ino = autofs_dentry_ino(dentry);
	/* avoid rapid-fire expire attempts if expiry fails */
	ino->last_used = jiffies;
	ino->flags &= ~(AUTOFS_INF_EXPIRING|AUTOFS_INF_WANT_EXPIRE);
	complete_all(&ino->expire_complete);
	spin_unlock(&sbi->fs_lock);

	dput(dentry);

	return ret;
}

int autofs_do_expire_multi(struct super_block *sb, struct vfsmount *mnt,
			   struct autofs_sb_info *sbi, unsigned int how)
{
	struct dentry *dentry;
	int ret = -EAGAIN;

	if (autofs_type_trigger(sbi->type))
		dentry = autofs_expire_direct(sb, mnt, sbi, how);
	else
		dentry = autofs_expire_indirect(sb, mnt, sbi, how);

	if (dentry) {
		struct autofs_info *ino = autofs_dentry_ino(dentry);
		const struct path path = { .mnt = mnt, .dentry = dentry };

		/* This is synchronous because it makes the daemon a
		 * little easier
		 */
		ret = autofs_wait(sbi, &path, NFY_EXPIRE);

		spin_lock(&sbi->fs_lock);
		/* avoid rapid-fire expire attempts if expiry fails */
		ino->last_used = jiffies;
		ino->flags &= ~(AUTOFS_INF_EXPIRING|AUTOFS_INF_WANT_EXPIRE);
		complete_all(&ino->expire_complete);
		spin_unlock(&sbi->fs_lock);
		dput(dentry);
	}

	return ret;
}

/*
 * Call repeatedly until it returns -EAGAIN, meaning there's nothing
 * more to be done.
 */
int autofs_expire_multi(struct super_block *sb, struct vfsmount *mnt,
			struct autofs_sb_info *sbi, int __user *arg)
{
	unsigned int how = 0;

	if (arg && get_user(how, arg))
		return -EFAULT;

	return autofs_do_expire_multi(sb, mnt, sbi, how);
}
