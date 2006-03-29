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

/* Check a mount point for busyness */
static int autofs4_mount_busy(struct vfsmount *mnt, struct dentry *dentry)
{
	struct dentry *top = dentry;
	int status = 1;

	DPRINTK("dentry %p %.*s",
		dentry, (int)dentry->d_name.len, dentry->d_name.name);

	mntget(mnt);
	dget(dentry);

	if (!autofs4_follow_mount(&mnt, &dentry))
		goto done;

	/* This is an autofs submount, we can't expire it */
	if (is_autofs4_dentry(dentry))
		goto done;

	/* Update the expiry counter if fs is busy */
	if (!may_umount_tree(mnt)) {
		struct autofs_info *ino = autofs4_dentry_ino(top);
		ino->last_used = jiffies;
		goto done;
	}

	status = 0;
done:
	DPRINTK("returning = %d", status);
	mntput(mnt);
	dput(dentry);
	return status;
}

/*
 * Calculate next entry in top down tree traversal.
 * From next_mnt in namespace.c - elegant.
 */
static struct dentry *next_dentry(struct dentry *p, struct dentry *root)
{
	struct list_head *next = p->d_subdirs.next;

	if (next == &p->d_subdirs) {
		while (1) {
			if (p == root)
				return NULL;
			next = p->d_u.d_child.next;
			if (next != &p->d_parent->d_subdirs)
				break;
			p = p->d_parent;
		}
	}
	return list_entry(next, struct dentry, d_u.d_child);
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

	spin_lock(&dcache_lock);
	for (p = top; p; p = next_dentry(p, top)) {
		/* Negative dentry - give up */
		if (!simple_positive(p))
			continue;

		DPRINTK("dentry %p %.*s",
			p, (int) p->d_name.len, p->d_name.name);

		p = dget(p);
		spin_unlock(&dcache_lock);

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

			/* allow for dget above and top is already dgot */
			if (p == top)
				ino_count += 2;
			else
				ino_count++;

			if (atomic_read(&p->d_count) > ino_count) {
				top_ino->last_used = jiffies;
				dput(p);
				return 1;
			}
		}
		dput(p);
		spin_lock(&dcache_lock);
	}
	spin_unlock(&dcache_lock);

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

	spin_lock(&dcache_lock);
	for (p = parent; p; p = next_dentry(p, parent)) {
		/* Negative dentry - give up */
		if (!simple_positive(p))
			continue;

		DPRINTK("dentry %p %.*s",
			p, (int) p->d_name.len, p->d_name.name);

		p = dget(p);
		spin_unlock(&dcache_lock);

		if (d_mountpoint(p)) {
			/* Can we umount this guy */
			if (autofs4_mount_busy(mnt, p))
				goto cont;

			/* Can we expire this guy */
			if (autofs4_can_expire(p, timeout, do_now))
				return p;
		}
cont:
		dput(p);
		spin_lock(&dcache_lock);
	}
	spin_unlock(&dcache_lock);
	return NULL;
}

/* Check if we can expire a direct mount (possibly a tree) */
static struct dentry *autofs4_expire_direct(struct super_block *sb,
					    struct vfsmount *mnt,
					    struct autofs_sb_info *sbi,
					    int how)
{
	unsigned long timeout;
	struct dentry *root = dget(sb->s_root);
	int do_now = how & AUTOFS_EXP_IMMEDIATE;

	if (!sbi->exp_timeout || !root)
		return NULL;

	now = jiffies;
	timeout = sbi->exp_timeout;

	/* Lock the tree as we must expire as a whole */
	spin_lock(&sbi->fs_lock);
	if (!autofs4_direct_busy(mnt, root, timeout, do_now)) {
		struct autofs_info *ino = autofs4_dentry_ino(root);

		/* Set this flag early to catch sys_chdir and the like */
		ino->flags |= AUTOFS_INF_EXPIRING;
		spin_unlock(&sbi->fs_lock);
		return root;
	}
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
static struct dentry *autofs4_expire_indirect(struct super_block *sb,
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
		struct dentry *dentry = list_entry(next, struct dentry, d_u.d_child);

		/* Negative dentry - give up */
		if (!simple_positive(dentry)) {
			next = next->next;
			continue;
		}

		dentry = dget(dentry);
		spin_unlock(&dcache_lock);

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
				break;
			}
			goto next;
		}

		if (simple_empty(dentry))
			goto next;

		/* Case 2: tree mount, expire iff entire tree is not busy */
		if (!exp_leaves) {
			/* Lock the tree as we must expire as a whole */
			spin_lock(&sbi->fs_lock);
			if (!autofs4_tree_busy(mnt, dentry, timeout, do_now)) {
				struct autofs_info *inf = autofs4_dentry_ino(dentry);

				/* Set this flag early to catch sys_chdir and the like */
				inf->flags |= AUTOFS_INF_EXPIRING;
				spin_unlock(&sbi->fs_lock);
				expired = dentry;
				break;
			}
			spin_unlock(&sbi->fs_lock);
		/*
		 * Case 3: pseudo direct mount, expire individual leaves
		 *	   (autofs-4.1).
		 */
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

	if (expired) {
		DPRINTK("returning %p %.*s",
			expired, (int)expired->d_name.len, expired->d_name.name);
		spin_lock(&dcache_lock);
		list_del(&expired->d_parent->d_subdirs);
		list_add(&expired->d_parent->d_subdirs, &expired->d_u.d_child);
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

	if ((dentry = autofs4_expire_indirect(sb, mnt, sbi, 0)) == NULL)
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

	if (sbi->type & AUTOFS_TYPE_DIRECT)
		dentry = autofs4_expire_direct(sb, mnt, sbi, do_now);
	else
		dentry = autofs4_expire_indirect(sb, mnt, sbi, do_now);

	if (dentry) {
		struct autofs_info *ino = autofs4_dentry_ino(dentry);

		/* This is synchronous because it makes the daemon a
                   little easier */
		ino->flags |= AUTOFS_INF_EXPIRING;
		ret = autofs4_wait(sbi, dentry, NFY_EXPIRE);
		ino->flags &= ~AUTOFS_INF_EXPIRING;
		dput(dentry);
	}

	return ret;
}

