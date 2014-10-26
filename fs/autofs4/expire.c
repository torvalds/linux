/* -*- c -*- --------------------------------------------------------------- *
 *
 * linux/fs/autofs/expire.c
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

#include "autofs_i.h"

static unsigned long now;

/* Check if a dentry can be expired */
static inline int autofs4_can_expire(struct dentry *dentry,
					unsigned long timeout, int do_now)
{
	struct autofs_info *ino = autofs4_dentry_ino(dentry);

	/* dentry in the process of being deleted */
	if (ino == NULL)
		return 0;

	if (!do_now) {
		/* Too young to die */
		if (!timeout || time_after(ino->last_used + timeout, now))
			return 0;

		/* update last_used here :-
		   - obviously makes sense if it is in use now
		   - less obviously, prevents rapid-fire expire
		     attempts if expire fails the first time */
		ino->last_used = now;
	}
	return 1;
}

/* Check a mount point for busyness */
static int autofs4_mount_busy(struct vfsmount *mnt, struct dentry *dentry)
{
	struct dentry *top = dentry;
	struct path path = {.mnt = mnt, .dentry = dentry};
	int status = 1;

	DPRINTK("dentry %p %.*s",
		dentry, (int)dentry->d_name.len, dentry->d_name.name);

	path_get(&path);

	if (!follow_down_one(&path))
		goto done;

	if (is_autofs4_dentry(path.dentry)) {
		struct autofs_sb_info *sbi = autofs4_sbi(path.dentry->d_sb);

		/* This is an autofs submount, we can't expire it */
		if (autofs_type_indirect(sbi->type))
			goto done;
	}

	/* Update the expiry counter if fs is busy */
	if (!may_umount_tree(path.mnt)) {
		struct autofs_info *ino = autofs4_dentry_ino(top);
		ino->last_used = jiffies;
		goto done;
	}

	status = 0;
done:
	DPRINTK("returning = %d", status);
	path_put(&path);
	return status;
}

/*
 * Calculate and dget next entry in the subdirs list under root.
 */
static struct dentry *get_next_positive_subdir(struct dentry *prev,
						struct dentry *root)
{
	struct autofs_sb_info *sbi = autofs4_sbi(root->d_sb);
	struct list_head *next;
	struct dentry *q;

	spin_lock(&sbi->lookup_lock);
	spin_lock(&root->d_lock);

	if (prev)
		next = prev->d_child.next;
	else {
		prev = dget_dlock(root);
		next = prev->d_subdirs.next;
	}

cont:
	if (next == &root->d_subdirs) {
		spin_unlock(&root->d_lock);
		spin_unlock(&sbi->lookup_lock);
		dput(prev);
		return NULL;
	}

	q = list_entry(next, struct dentry, d_child);

	spin_lock_nested(&q->d_lock, DENTRY_D_LOCK_NESTED);
	/* Already gone or negative dentry (under construction) - try next */
	if (q->d_count == 0 || !simple_positive(q)) {
		spin_unlock(&q->d_lock);
		next = q->d_child.next;
		goto cont;
	}
	dget_dlock(q);
	spin_unlock(&q->d_lock);
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
	struct autofs_sb_info *sbi = autofs4_sbi(root->d_sb);
	struct list_head *next;
	struct dentry *p, *ret;

	if (prev == NULL)
		return dget(root);

	spin_lock(&sbi->lookup_lock);
relock:
	p = prev;
	spin_lock(&p->d_lock);
again:
	next = p->d_subdirs.next;
	if (next == &p->d_subdirs) {
		while (1) {
			struct dentry *parent;

			if (p == root) {
				spin_unlock(&p->d_lock);
				spin_unlock(&sbi->lookup_lock);
				dput(prev);
				return NULL;
			}

			parent = p->d_parent;
			if (!spin_trylock(&parent->d_lock)) {
				spin_unlock(&p->d_lock);
				cpu_relax();
				goto relock;
			}
			spin_unlock(&p->d_lock);
			next = p->d_child.next;
			p = parent;
			if (next != &parent->d_subdirs)
				break;
		}
	}
	ret = list_entry(next, struct dentry, d_child);

	spin_lock_nested(&ret->d_lock, DENTRY_D_LOCK_NESTED);
	/* Negative dentry - try next */
	if (!simple_positive(ret)) {
		spin_unlock(&p->d_lock);
		lock_set_subclass(&ret->d_lock.dep_map, 0, _RET_IP_);
		p = ret;
		goto again;
	}
	dget_dlock(ret);
	spin_unlock(&ret->d_lock);
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
static int autofs4_direct_busy(struct vfsmount *mnt,
				struct dentry *top,
				unsigned long timeout,
				int do_now)
{
	DPRINTK("top %p %.*s",
		top, (int) top->d_name.len, top->d_name.name);

	/* If it's busy update the expiry counters */
	if (!may_umount_tree(mnt)) {
		struct autofs_info *ino = autofs4_dentry_ino(top);
		if (ino)
			ino->last_used = jiffies;
		return 1;
	}

	/* Timeout of a direct mount is determined by its top dentry */
	if (!autofs4_can_expire(top, timeout, do_now))
		return 1;

	return 0;
}

/* Check a directory tree of mount points for busyness
 * The tree is not busy iff no mountpoints are busy
 */
static int autofs4_tree_busy(struct vfsmount *mnt,
	       		     struct dentry *top,
			     unsigned long timeout,
			     int do_now)
{
	struct autofs_info *top_ino = autofs4_dentry_ino(top);
	struct dentry *p;

	DPRINTK("top %p %.*s",
		top, (int)top->d_name.len, top->d_name.name);

	/* Negative dentry - give up */
	if (!simple_positive(top))
		return 1;

	p = NULL;
	while ((p = get_next_positive_dentry(p, top))) {
		DPRINTK("dentry %p %.*s",
			p, (int) p->d_name.len, p->d_name.name);

		/*
		 * Is someone visiting anywhere in the subtree ?
		 * If there's no mount we need to check the usage
		 * count for the autofs dentry.
		 * If the fs is busy update the expiry counter.
		 */
		if (d_mountpoint(p)) {
			if (autofs4_mount_busy(mnt, p)) {
				top_ino->last_used = jiffies;
				dput(p);
				return 1;
			}
		} else {
			struct autofs_info *ino = autofs4_dentry_ino(p);
			unsigned int ino_count = atomic_read(&ino->count);

			/*
			 * Clean stale dentries below that have not been
			 * invalidated after a mount fail during lookup
			 */
			d_invalidate(p);

			/* allow for dget above and top is already dgot */
			if (p == top)
				ino_count += 2;
			else
				ino_count++;

			if (p->d_count > ino_count) {
				top_ino->last_used = jiffies;
				dput(p);
				return 1;
			}
		}
	}

	/* Timeout of a tree mount is ultimately determined by its top dentry */
	if (!autofs4_can_expire(top, timeout, do_now))
		return 1;

	return 0;
}

static struct dentry *autofs4_check_leaves(struct vfsmount *mnt,
					   struct dentry *parent,
					   unsigned long timeout,
					   int do_now)
{
	struct dentry *p;

	DPRINTK("parent %p %.*s",
		parent, (int)parent->d_name.len, parent->d_name.name);

	p = NULL;
	while ((p = get_next_positive_dentry(p, parent))) {
		DPRINTK("dentry %p %.*s",
			p, (int) p->d_name.len, p->d_name.name);

		if (d_mountpoint(p)) {
			/* Can we umount this guy */
			if (autofs4_mount_busy(mnt, p))
				continue;

			/* Can we expire this guy */
			if (autofs4_can_expire(p, timeout, do_now))
				return p;
		}
	}
	return NULL;
}

/* Check if we can expire a direct mount (possibly a tree) */
struct dentry *autofs4_expire_direct(struct super_block *sb,
				     struct vfsmount *mnt,
				     struct autofs_sb_info *sbi,
				     int how)
{
	unsigned long timeout;
	struct dentry *root = dget(sb->s_root);
	int do_now = how & AUTOFS_EXP_IMMEDIATE;
	struct autofs_info *ino;

	if (!root)
		return NULL;

	now = jiffies;
	timeout = sbi->exp_timeout;

	spin_lock(&sbi->fs_lock);
	ino = autofs4_dentry_ino(root);
	/* No point expiring a pending mount */
	if (ino->flags & AUTOFS_INF_PENDING)
		goto out;
	if (!autofs4_direct_busy(mnt, root, timeout, do_now)) {
		struct autofs_info *ino = autofs4_dentry_ino(root);
		ino->flags |= AUTOFS_INF_EXPIRING;
		init_completion(&ino->expire_complete);
		spin_unlock(&sbi->fs_lock);
		return root;
	}
out:
	spin_unlock(&sbi->fs_lock);
	dput(root);

	return NULL;
}

/*
 * Find an eligible tree to time-out
 * A tree is eligible if :-
 *  - it is unused by any user process
 *  - it has been unused for exp_timeout time
 */
struct dentry *autofs4_expire_indirect(struct super_block *sb,
				       struct vfsmount *mnt,
				       struct autofs_sb_info *sbi,
				       int how)
{
	unsigned long timeout;
	struct dentry *root = sb->s_root;
	struct dentry *dentry;
	struct dentry *expired = NULL;
	int do_now = how & AUTOFS_EXP_IMMEDIATE;
	int exp_leaves = how & AUTOFS_EXP_LEAVES;
	struct autofs_info *ino;
	unsigned int ino_count;

	if (!root)
		return NULL;

	now = jiffies;
	timeout = sbi->exp_timeout;

	dentry = NULL;
	while ((dentry = get_next_positive_subdir(dentry, root))) {
		spin_lock(&sbi->fs_lock);
		ino = autofs4_dentry_ino(dentry);
		/* No point expiring a pending mount */
		if (ino->flags & AUTOFS_INF_PENDING)
			goto next;

		/*
		 * Case 1: (i) indirect mount or top level pseudo direct mount
		 *	   (autofs-4.1).
		 *	   (ii) indirect mount with offset mount, check the "/"
		 *	   offset (autofs-5.0+).
		 */
		if (d_mountpoint(dentry)) {
			DPRINTK("checking mountpoint %p %.*s",
				dentry, (int)dentry->d_name.len, dentry->d_name.name);

			/* Can we umount this guy */
			if (autofs4_mount_busy(mnt, dentry))
				goto next;

			/* Can we expire this guy */
			if (autofs4_can_expire(dentry, timeout, do_now)) {
				expired = dentry;
				goto found;
			}
			goto next;
		}

		if (simple_empty(dentry))
			goto next;

		/* Case 2: tree mount, expire iff entire tree is not busy */
		if (!exp_leaves) {
			/* Path walk currently on this dentry? */
			ino_count = atomic_read(&ino->count) + 1;
			if (dentry->d_count > ino_count)
				goto next;

			if (!autofs4_tree_busy(mnt, dentry, timeout, do_now)) {
				expired = dentry;
				goto found;
			}
		/*
		 * Case 3: pseudo direct mount, expire individual leaves
		 *	   (autofs-4.1).
		 */
		} else {
			/* Path walk currently on this dentry? */
			ino_count = atomic_read(&ino->count) + 1;
			if (dentry->d_count > ino_count)
				goto next;

			expired = autofs4_check_leaves(mnt, dentry, timeout, do_now);
			if (expired) {
				dput(dentry);
				goto found;
			}
		}
next:
		spin_unlock(&sbi->fs_lock);
	}
	return NULL;

found:
	DPRINTK("returning %p %.*s",
		expired, (int)expired->d_name.len, expired->d_name.name);
	ino = autofs4_dentry_ino(expired);
	ino->flags |= AUTOFS_INF_EXPIRING;
	init_completion(&ino->expire_complete);
	spin_unlock(&sbi->fs_lock);
	spin_lock(&sbi->lookup_lock);
	spin_lock(&expired->d_parent->d_lock);
	spin_lock_nested(&expired->d_lock, DENTRY_D_LOCK_NESTED);
	list_move(&expired->d_parent->d_subdirs, &expired->d_child);
	spin_unlock(&expired->d_lock);
	spin_unlock(&expired->d_parent->d_lock);
	spin_unlock(&sbi->lookup_lock);
	return expired;
}

int autofs4_expire_wait(struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	int status;

	/* Block on any pending expire */
	spin_lock(&sbi->fs_lock);
	if (ino->flags & AUTOFS_INF_EXPIRING) {
		spin_unlock(&sbi->fs_lock);

		DPRINTK("waiting for expire %p name=%.*s",
			 dentry, dentry->d_name.len, dentry->d_name.name);

		status = autofs4_wait(sbi, dentry, NFY_NONE);
		wait_for_completion(&ino->expire_complete);

		DPRINTK("expire done status=%d", status);

		if (d_unhashed(dentry))
			return -EAGAIN;

		return status;
	}
	spin_unlock(&sbi->fs_lock);

	return 0;
}

/* Perform an expiry operation */
int autofs4_expire_run(struct super_block *sb,
		      struct vfsmount *mnt,
		      struct autofs_sb_info *sbi,
		      struct autofs_packet_expire __user *pkt_p)
{
	struct autofs_packet_expire pkt;
	struct autofs_info *ino;
	struct dentry *dentry;
	int ret = 0;

	memset(&pkt,0,sizeof pkt);

	pkt.hdr.proto_version = sbi->version;
	pkt.hdr.type = autofs_ptype_expire;

	if ((dentry = autofs4_expire_indirect(sb, mnt, sbi, 0)) == NULL)
		return -EAGAIN;

	pkt.len = dentry->d_name.len;
	memcpy(pkt.name, dentry->d_name.name, pkt.len);
	pkt.name[pkt.len] = '\0';
	dput(dentry);

	if ( copy_to_user(pkt_p, &pkt, sizeof(struct autofs_packet_expire)) )
		ret = -EFAULT;

	spin_lock(&sbi->fs_lock);
	ino = autofs4_dentry_ino(dentry);
	ino->flags &= ~AUTOFS_INF_EXPIRING;
	complete_all(&ino->expire_complete);
	spin_unlock(&sbi->fs_lock);

	return ret;
}

int autofs4_do_expire_multi(struct super_block *sb, struct vfsmount *mnt,
			    struct autofs_sb_info *sbi, int when)
{
	struct dentry *dentry;
	int ret = -EAGAIN;

	if (autofs_type_trigger(sbi->type))
		dentry = autofs4_expire_direct(sb, mnt, sbi, when);
	else
		dentry = autofs4_expire_indirect(sb, mnt, sbi, when);

	if (dentry) {
		struct autofs_info *ino = autofs4_dentry_ino(dentry);

		/* This is synchronous because it makes the daemon a
                   little easier */
		ret = autofs4_wait(sbi, dentry, NFY_EXPIRE);

		spin_lock(&sbi->fs_lock);
		ino->flags &= ~AUTOFS_INF_EXPIRING;
		complete_all(&ino->expire_complete);
		spin_unlock(&sbi->fs_lock);
		dput(dentry);
	}

	return ret;
}

/* Call repeatedly until it returns -EAGAIN, meaning there's nothing
   more to be done */
int autofs4_expire_multi(struct super_block *sb, struct vfsmount *mnt,
			struct autofs_sb_info *sbi, int __user *arg)
{
	int do_now = 0;

	if (arg && get_user(do_now, arg))
		return -EFAULT;

	return autofs4_do_expire_multi(sb, mnt, sbi, do_now);
}

