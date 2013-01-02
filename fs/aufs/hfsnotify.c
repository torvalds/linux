/*
 * Copyright (C) 2005-2013 Junjiro R. Okajima
 *
 * This program, aufs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
 * fsnotify for the lower directories
 */

#include "aufs.h"

/* FS_IN_IGNORED is unnecessary */
static const __u32 AuHfsnMask = (FS_MOVED_TO | FS_MOVED_FROM | FS_DELETE
				 | FS_CREATE | FS_EVENT_ON_CHILD);
static DECLARE_WAIT_QUEUE_HEAD(au_hfsn_wq);
static __cacheline_aligned_in_smp atomic64_t au_hfsn_ifree = ATOMIC64_INIT(0);

static void au_hfsn_free_mark(struct fsnotify_mark *mark)
{
	struct au_hnotify *hn = container_of(mark, struct au_hnotify,
					     hn_mark);
	AuDbg("here\n");
	au_cache_free_hnotify(hn);
	smp_mb__before_atomic_dec();
	atomic64_dec(&au_hfsn_ifree);
	wake_up(&au_hfsn_wq);
}

static int au_hfsn_alloc(struct au_hinode *hinode)
{
	struct au_hnotify *hn;
	struct super_block *sb;
	struct au_branch *br;
	struct fsnotify_mark *mark;
	aufs_bindex_t bindex;

	hn = hinode->hi_notify;
	sb = hn->hn_aufs_inode->i_sb;
	bindex = au_br_index(sb, hinode->hi_id);
	br = au_sbr(sb, bindex);
	mark = &hn->hn_mark;
	fsnotify_init_mark(mark, au_hfsn_free_mark);
	mark->mask = AuHfsnMask;
	/*
	 * by udba rename or rmdir, aufs assign a new inode to the known
	 * h_inode, so specify 1 to allow dups.
	 */
	return fsnotify_add_mark(mark, br->br_hfsn_group, hinode->hi_inode,
				 /*mnt*/NULL, /*allow_dups*/1);
}

static int au_hfsn_free(struct au_hinode *hinode, struct au_hnotify *hn)
{
	struct fsnotify_mark *mark;
	unsigned long long ull;

	ull = atomic64_inc_return(&au_hfsn_ifree);
	BUG_ON(!ull);

	mark = &hn->hn_mark;
	fsnotify_destroy_mark(mark);
	fsnotify_put_mark(mark);

	/* free hn by myself */
	return 0;
}

/* ---------------------------------------------------------------------- */

static void au_hfsn_ctl(struct au_hinode *hinode, int do_set)
{
	struct fsnotify_mark *mark;

	mark = &hinode->hi_notify->hn_mark;
	spin_lock(&mark->lock);
	if (do_set) {
		AuDebugOn(mark->mask & AuHfsnMask);
		mark->mask |= AuHfsnMask;
	} else {
		AuDebugOn(!(mark->mask & AuHfsnMask));
		mark->mask &= ~AuHfsnMask;
	}
	spin_unlock(&mark->lock);
	/* fsnotify_recalc_inode_mask(hinode->hi_inode); */
}

/* ---------------------------------------------------------------------- */

/* #define AuDbgHnotify */
#ifdef AuDbgHnotify
static char *au_hfsn_name(u32 mask)
{
#ifdef CONFIG_AUFS_DEBUG
#define test_ret(flag)	if (mask & flag) \
				return #flag;
	test_ret(FS_ACCESS);
	test_ret(FS_MODIFY);
	test_ret(FS_ATTRIB);
	test_ret(FS_CLOSE_WRITE);
	test_ret(FS_CLOSE_NOWRITE);
	test_ret(FS_OPEN);
	test_ret(FS_MOVED_FROM);
	test_ret(FS_MOVED_TO);
	test_ret(FS_CREATE);
	test_ret(FS_DELETE);
	test_ret(FS_DELETE_SELF);
	test_ret(FS_MOVE_SELF);
	test_ret(FS_UNMOUNT);
	test_ret(FS_Q_OVERFLOW);
	test_ret(FS_IN_IGNORED);
	test_ret(FS_IN_ISDIR);
	test_ret(FS_IN_ONESHOT);
	test_ret(FS_EVENT_ON_CHILD);
	return "";
#undef test_ret
#else
	return "??";
#endif
}
#endif

/* ---------------------------------------------------------------------- */

static int au_hfsn_handle_event(struct fsnotify_group *group,
				struct fsnotify_mark *inode_mark,
				struct fsnotify_mark *vfsmount_mark,
				struct fsnotify_event *event)
{
	int err;
	struct au_hnotify *hnotify;
	struct inode *h_dir, *h_inode;
	__u32 mask;
	struct qstr h_child_qstr = {
		.name	= event->file_name,
		.len	= event->name_len
	};

	AuDebugOn(event->data_type != FSNOTIFY_EVENT_INODE);

	err = 0;
	/* if FS_UNMOUNT happens, there must be another bug */
	mask = event->mask;
	AuDebugOn(mask & FS_UNMOUNT);
	if (mask & (FS_IN_IGNORED | FS_UNMOUNT))
		goto out;

	h_dir = event->to_tell;
	h_inode = event->inode;
#ifdef AuDbgHnotify
	au_debug(1);
	if (1 || h_child_qstr.len != sizeof(AUFS_XINO_FNAME) - 1
	    || strncmp(h_child_qstr.name, AUFS_XINO_FNAME, h_child_qstr.len)) {
		AuDbg("i%lu, mask 0x%x %s, hcname %.*s, hi%lu\n",
		      h_dir->i_ino, mask, au_hfsn_name(mask),
		      AuLNPair(&h_child_qstr), h_inode ? h_inode->i_ino : 0);
		/* WARN_ON(1); */
	}
	au_debug(0);
#endif

	AuDebugOn(!inode_mark);
	hnotify = container_of(inode_mark, struct au_hnotify, hn_mark);
	err = au_hnotify(h_dir, hnotify, mask, &h_child_qstr, h_inode);

out:
	return err;
}

/* isn't it waste to ask every registered 'group'? */
/* copied from linux/fs/notify/inotify/inotify_fsnotiry.c */
/* it should be exported to modules */
static bool au_hfsn_should_send_event(struct fsnotify_group *group,
				      struct inode *h_inode,
				      struct fsnotify_mark *inode_mark,
				      struct fsnotify_mark *vfsmount_mark,
				      __u32 mask, void *data, int data_type)
{
	mask = (mask & ~FS_EVENT_ON_CHILD);
	return inode_mark->mask & mask;
}

static struct fsnotify_ops au_hfsn_ops = {
	.should_send_event	= au_hfsn_should_send_event,
	.handle_event		= au_hfsn_handle_event
};

/* ---------------------------------------------------------------------- */

static void au_hfsn_fin_br(struct au_branch *br)
{
	if (br->br_hfsn_group)
		fsnotify_put_group(br->br_hfsn_group);
}

static int au_hfsn_init_br(struct au_branch *br, int perm)
{
	br->br_hfsn_group = NULL;
	br->br_hfsn_ops = au_hfsn_ops;
	return 0;
}

static int au_hfsn_reset_br(unsigned int udba, struct au_branch *br, int perm)
{
	int err;

	err = 0;
	if (udba != AuOpt_UDBA_HNOTIFY
	    || !au_br_hnotifyable(perm)) {
		au_hfsn_fin_br(br);
		br->br_hfsn_group = NULL;
		goto out;
	}

	if (br->br_hfsn_group)
		goto out;

	br->br_hfsn_group = fsnotify_alloc_group(&br->br_hfsn_ops);
	if (IS_ERR(br->br_hfsn_group)) {
		err = PTR_ERR(br->br_hfsn_group);
		pr_err("fsnotify_alloc_group() failed, %d\n", err);
		br->br_hfsn_group = NULL;
	}

out:
	AuTraceErr(err);
	return err;
}

/* ---------------------------------------------------------------------- */

static void au_hfsn_fin(void)
{
	AuDbg("au_hfsn_ifree %lld\n", (long long)atomic64_read(&au_hfsn_ifree));
	wait_event(au_hfsn_wq, !atomic64_read(&au_hfsn_ifree));
}

const struct au_hnotify_op au_hnotify_op = {
	.ctl		= au_hfsn_ctl,
	.alloc		= au_hfsn_alloc,
	.free		= au_hfsn_free,

	.fin		= au_hfsn_fin,

	.reset_br	= au_hfsn_reset_br,
	.fin_br		= au_hfsn_fin_br,
	.init_br	= au_hfsn_init_br
};
