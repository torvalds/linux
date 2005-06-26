/* -*- c -*- --------------------------------------------------------------- *
 *
 * linux/fs/autofs/expire.c
 *
 *  Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 *  Copyright 1999-2000 Jeremy Fitzhardinge <jeremy@goop.org>
 *  Copyright 2001-2003 Ian Kent <raven@themaw.net>
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include "autofs_i.h"

static unsigned long now;

/* Check if a dentry can be expired return 1 if it can else return 0 */
static inline int autofs4_can_expire(struct dentry *dentry,
					unsigned long timeout, int do_now)
{
	struct autofs_info *ino = autofs4_dentry_ino(dentry);

	/* dentry in the process of being deleted */
	if (ino == NULL)
		return 0;

	/* No point expiring a pending mount */
	if (dentry->d_flags & DCACHE_AUTOFS_PENDING)
		return 0;

	if (!do_now) {
		/* Too young to die */
		if (time_after(ino->last_used + timeout, now))
			return 0;

		/* update last_used here :-
		   - obviously makes sense if it is in use now
		   - less obviously, prevents rapid-fire expire
		     attempts if expire fails the first time */
		ino->last_used = now;
	}

	return 1;
}

/* Check a mount point for busyness return 1 if not busy, otherwise */
static int autofs4_check_mount(struct vfsmount *mnt, struct dentry *dentry)
{
	int status = 0;

	DPRINTK("dentry %p %.*s",
		dentry, (int)dentry->d_name.len, dentry->d_name.name);

	mntget(mnt);
	dget(dentry);

	if (!autofs4_follow_mount(&mnt, &dentry))
		goto done;

	/* This is an autofs submount, we can't expire it */
	if (is_autofs4_dentry(dentry))
		goto done;

	/* The big question */
	if (may_umount_tree(mnt) == 0)
		status = 1;
done:
	DPRINTK("returning = %d", status);
	mntput(mnt);
	dput(dentry);
	return status;
}

/* Check a directory tree of mount points for busyness
 * The tree is not busy iff no mountpoints are busy
 * Return 1 if the tree is busy or 0 otherwise
 */
static int autofs4_check_tree(struct vfsmount *mnt,
	       		      struct dentry *top,
			      unsigned long timeout,
			      int do_now)
{
	struct dentry *this_parent = top;
	struct list_head *next;

	DPRINTK("parent %p %.*s",
		top, (int)top->d_name.len, top->d_name.name);

	/* Negative dentry - give up */
	if (!simple_positive(top))
		return 0;

	/* Timeout of a tree mount is determined by its top dentry */
	if (!autofs4_can_expire(top, timeout, do_now))
		return 0;

	/* Is someone visiting anywhere in the tree ? */
	if (may_umount_tree(mnt))
		return 0;

	spin_lock(&dcache_lock);
repeat:
	next = this_parent->d_subdirs.next;
resume:
	while (next != &this_parent->d_subdirs) {
		struct dentry *dentry = list_entry(next, struct dentry, d_child);

		/* Negative dentry - give up */
		if (!simple_positive(dentry)) {
			next = next->next;
			continue;
		}

		DPRINTK("dentry %p %.*s",
			dentry, (int)dentry->d_name.len, dentry->d_name.name);

		if (!simple_empty_nolock(dentry)) {
			this_parent = dentry;
			goto repeat;
		}

		dentry = dget(dentry);
		spin_unlock(&dcache_lock);

		if (d_mountpoint(dentry)) {
			/* First busy => tree busy */
			if (!autofs4_check_mount(mnt, dentry)) {
				dput(dentry);
				return 0;
			}
		}

		dput(dentry);
		spin_lock(&dcache_lock);
		next = next->next;
	}

	if (this_parent != top) {
		next = this_parent->d_child.next;
		this_parent = this_parent->d_parent;
		goto resume;
	}
	spin_unlock(&dcache_lock);

	return 1;
}

static struct dentry *autofs4_check_leaves(struct vfsmount *mnt,
					   struct dentry *parent,
					   unsigned long timeout,
					   int do_now)
{
	struct dentry *this_parent = parent;
	struct list_head *next;

	DPRINTK("parent %p %.*s",
		parent, (int)parent->d_name.len, parent->d_name.name);

	spin_lock(&dcache_lock);
repeat:
	next = this_parent->d_subdirs.next;
resume:
	while (next != &this_parent->d_subdirs) {
		struct dentry *dentry = list_entry(next, struct dentry, d_child);

		/* Negative dentry - give up */
		if (!simple_positive(dentry)) {
			next = next->next;
			continue;
		}

		DPRINTK("dentry %p %.*s",
			dentry, (int)dentry->d_name.len, dentry->d_name.name);

		if (!list_empty(&dentry->d_subdirs)) {
			this_parent = dentry;
			goto repeat;
		}

		dentry = dget(dentry);
		spin_unlock(&dcache_lock);

		if (d_mountpoint(dentry)) {
			/* Can we expire this guy */
			if (!autofs4_can_expire(dentry, timeout, do_now))
				goto cont;

			/* Can we umount this guy */
			if (autofs4_check_mount(mnt, dentry))
				return dentry;

		}
cont:
		dput(dentry);
		spin_lock(&dcache_lock);
		next = next->next;
	}

	if (this_parent != parent) {
		next = this_parent->d_child.next;
		this_parent = this_parent->d_parent;
		goto resume;
	}
	spin_unlock(&dcache_lock);

	return NULL;
}

/*
 * Find an eligible tree to time-out
 * A tree is eligible if :-
 *  - it is unused by any user process
 *  - it has been unused for exp_timeout time
 */
static struct dentry *autofs4_expire(struct super_block *sb,
				     struct vfsmount *mnt,
				     struct autofs_sb_info *sbi,
				     int how)
{
	unsigned long timeout;
	struct dentry *root = sb->s_root;
	struct dentry *expired = NULL;
	struct list_head *next;
	int do_now = how & AUTOFS_EXP_IMMEDIATE;
	int exp_leaves = how & AUTOFS_EXP_LEAVES;

	if ( !sbi->exp_timeout || !root )
		return NULL;

	now = jiffies;
	timeout = sbi->exp_timeout;

	spin_lock(&dcache_lock);
	next = root->d_subdirs.next;

	/* On exit from the loop expire is set to a dgot dentry
	 * to expire or it's NULL */
	while ( next != &root->d_subdirs ) {
		struct dentry *dentry = list_entry(next, struct dentry, d_child);

		/* Negative dentry - give up */
		if ( !simple_positive(dentry) ) {
			next = next->next;
			continue;
		}

		dentry = dget(dentry);
		spin_unlock(&dcache_lock);

		/* Case 1: indirect mount or top level direct mount */
		if (d_mountpoint(dentry)) {
			DPRINTK("checking mountpoint %p %.*s",
				dentry, (int)dentry->d_name.len, dentry->d_name.name);

			/* Can we expire this guy */
			if (!autofs4_can_expire(dentry, timeout, do_now))
				goto next;

			/* Can we umount this guy */
			if (autofs4_check_mount(mnt, dentry)) {
				expired = dentry;
				break;
			}
			goto next;
		}

		if ( simple_empty(dentry) )
			goto next;

		/* Case 2: tree mount, expire iff entire tree is not busy */
		if (!exp_leaves) {
			/* Lock the tree as we must expire as a whole */
			spin_lock(&sbi->fs_lock);
			if (autofs4_check_tree(mnt, dentry, timeout, do_now)) {
				struct autofs_info *inf = autofs4_dentry_ino(dentry);

				/* Set this flag early to catch sys_chdir and the like */
				inf->flags |= AUTOFS_INF_EXPIRING;
				spin_unlock(&sbi->fs_lock);
				expired = dentry;
				break;
			}
			spin_unlock(&sbi->fs_lock);
		/* Case 3: direct mount, expire individual leaves */
		} else {
			expired = autofs4_check_leaves(mnt, dentry, timeout, do_now);
			if (expired) {
				dput(dentry);
				break;
			}
		}
next:
		dput(dentry);
		spin_lock(&dcache_lock);
		next = next->next;
	}

	if ( expired ) {
		DPRINTK("returning %p %.*s",
			expired, (int)expired->d_name.len, expired->d_name.name);
		spin_lock(&dcache_lock);
		list_del(&expired->d_parent->d_subdirs);
		list_add(&expired->d_parent->d_subdirs, &expired->d_child);
		spin_unlock(&dcache_lock);
		return expired;
	}
	spin_unlock(&dcache_lock);

	return NULL;
}

/* Perform an expiry operation */
int autofs4_expire_run(struct super_block *sb,
		      struct vfsmount *mnt,
		      struct autofs_sb_info *sbi,
		      struct autofs_packet_expire __user *pkt_p)
{
	struct autofs_packet_expire pkt;
	struct dentry *dentry;

	memset(&pkt,0,sizeof pkt);

	pkt.hdr.proto_version = sbi->version;
	pkt.hdr.type = autofs_ptype_expire;

	if ((dentry = autofs4_expire(sb, mnt, sbi, 0)) == NULL)
		return -EAGAIN;

	pkt.len = dentry->d_name.len;
	memcpy(pkt.name, dentry->d_name.name, pkt.len);
	pkt.name[pkt.len] = '\0';
	dput(dentry);

	if ( copy_to_user(pkt_p, &pkt, sizeof(struct autofs_packet_expire)) )
		return -EFAULT;

	return 0;
}

/* Call repeatedly until it returns -EAGAIN, meaning there's nothing
   more to be done */
int autofs4_expire_multi(struct super_block *sb, struct vfsmount *mnt,
			struct autofs_sb_info *sbi, int __user *arg)
{
	struct dentry *dentry;
	int ret = -EAGAIN;
	int do_now = 0;

	if (arg && get_user(do_now, arg))
		return -EFAULT;

	if ((dentry = autofs4_expire(sb, mnt, sbi, do_now)) != NULL) {
		struct autofs_info *de_info = autofs4_dentry_ino(dentry);

		/* This is synchronous because it makes the daemon a
                   little easier */
		de_info->flags |= AUTOFS_INF_EXPIRING;
		ret = autofs4_wait(sbi, dentry, NFY_EXPIRE);
		de_info->flags &= ~AUTOFS_INF_EXPIRING;
		dput(dentry);
	}
		
	return ret;
}

