/*
 * Copyright (C) 2005-2017 Junjiro R. Okajima
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * pseudo-link
 */

#include "aufs.h"

/*
 * the pseudo-link maintenance mode.
 * during a user process maintains the pseudo-links,
 * prohibit adding a new plink and branch manipulation.
 *
 * Flags
 * NOPLM:
 *	For entry functions which will handle plink, and i_mutex is already held
 *	in VFS.
 *	They cannot wait and should return an error at once.
 *	Callers has to check the error.
 * NOPLMW:
 *	For entry functions which will handle plink, but i_mutex is not held
 *	in VFS.
 *	They can wait the plink maintenance mode to finish.
 *
 * They behave like F_SETLK and F_SETLKW.
 * If the caller never handle plink, then both flags are unnecessary.
 */

int au_plink_maint(struct super_block *sb, int flags)
{
	int err;
	pid_t pid, ppid;
	struct task_struct *parent, *prev;
	struct au_sbinfo *sbi;

	SiMustAnyLock(sb);

	err = 0;
	if (!au_opt_test(au_mntflags(sb), PLINK))
		goto out;

	sbi = au_sbi(sb);
	pid = sbi->si_plink_maint_pid;
	if (!pid || pid == current->pid)
		goto out;

	/* todo: it highly depends upon /sbin/mount.aufs */
	prev = NULL;
	parent = current;
	ppid = 0;
	rcu_read_lock();
	while (1) {
		parent = rcu_dereference(parent->real_parent);
		if (parent == prev)
			break;
		ppid = task_pid_vnr(parent);
		if (pid == ppid) {
			rcu_read_unlock();
			goto out;
		}
		prev = parent;
	}
	rcu_read_unlock();

	if (au_ftest_lock(flags, NOPLMW)) {
		/* if there is no i_mutex lock in VFS, we don't need to wait */
		/* AuDebugOn(!lockdep_depth(current)); */
		while (sbi->si_plink_maint_pid) {
			si_read_unlock(sb);
			/* gave up wake_up_bit() */
			wait_event(sbi->si_plink_wq, !sbi->si_plink_maint_pid);

			if (au_ftest_lock(flags, FLUSH))
				au_nwt_flush(&sbi->si_nowait);
			si_noflush_read_lock(sb);
		}
	} else if (au_ftest_lock(flags, NOPLM)) {
		AuDbg("ppid %d, pid %d\n", ppid, pid);
		err = -EAGAIN;
	}

out:
	return err;
}

void au_plink_maint_leave(struct au_sbinfo *sbinfo)
{
	spin_lock(&sbinfo->si_plink_maint_lock);
	sbinfo->si_plink_maint_pid = 0;
	spin_unlock(&sbinfo->si_plink_maint_lock);
	wake_up_all(&sbinfo->si_plink_wq);
}

int au_plink_maint_enter(struct super_block *sb)
{
	int err;
	struct au_sbinfo *sbinfo;

	err = 0;
	sbinfo = au_sbi(sb);
	/* make sure i am the only one in this fs */
	si_write_lock(sb, AuLock_FLUSH);
	if (au_opt_test(au_mntflags(sb), PLINK)) {
		spin_lock(&sbinfo->si_plink_maint_lock);
		if (!sbinfo->si_plink_maint_pid)
			sbinfo->si_plink_maint_pid = current->pid;
		else
			err = -EBUSY;
		spin_unlock(&sbinfo->si_plink_maint_lock);
	}
	si_write_unlock(sb);

	return err;
}

/* ---------------------------------------------------------------------- */

#ifdef CONFIG_AUFS_DEBUG
void au_plink_list(struct super_block *sb)
{
	int i;
	struct au_sbinfo *sbinfo;
	struct hlist_bl_head *hbl;
	struct hlist_bl_node *pos;
	struct au_icntnr *icntnr;

	SiMustAnyLock(sb);

	sbinfo = au_sbi(sb);
	AuDebugOn(!au_opt_test(au_mntflags(sb), PLINK));
	AuDebugOn(au_plink_maint(sb, AuLock_NOPLM));

	for (i = 0; i < AuPlink_NHASH; i++) {
		hbl = sbinfo->si_plink + i;
		hlist_bl_lock(hbl);
		hlist_bl_for_each_entry(icntnr, pos, hbl, plink)
			AuDbg("%lu\n", icntnr->vfs_inode.i_ino);
		hlist_bl_unlock(hbl);
	}
}
#endif

/* is the inode pseudo-linked? */
int au_plink_test(struct inode *inode)
{
	int found, i;
	struct au_sbinfo *sbinfo;
	struct hlist_bl_head *hbl;
	struct hlist_bl_node *pos;
	struct au_icntnr *icntnr;

	sbinfo = au_sbi(inode->i_sb);
	AuRwMustAnyLock(&sbinfo->si_rwsem);
	AuDebugOn(!au_opt_test(au_mntflags(inode->i_sb), PLINK));
	AuDebugOn(au_plink_maint(inode->i_sb, AuLock_NOPLM));

	found = 0;
	i = au_plink_hash(inode->i_ino);
	hbl =  sbinfo->si_plink + i;
	hlist_bl_lock(hbl);
	hlist_bl_for_each_entry(icntnr, pos, hbl, plink)
		if (&icntnr->vfs_inode == inode) {
			found = 1;
			break;
		}
	hlist_bl_unlock(hbl);
	return found;
}

/* ---------------------------------------------------------------------- */

/*
 * generate a name for plink.
 * the file will be stored under AUFS_WH_PLINKDIR.
 */
/* 20 is max digits length of ulong 64 */
#define PLINK_NAME_LEN	((20 + 1) * 2)

static int plink_name(char *name, int len, struct inode *inode,
		      aufs_bindex_t bindex)
{
	int rlen;
	struct inode *h_inode;

	h_inode = au_h_iptr(inode, bindex);
	rlen = snprintf(name, len, "%lu.%lu", inode->i_ino, h_inode->i_ino);
	return rlen;
}

struct au_do_plink_lkup_args {
	struct dentry **errp;
	struct qstr *tgtname;
	struct dentry *h_parent;
	struct au_branch *br;
};

static struct dentry *au_do_plink_lkup(struct qstr *tgtname,
				       struct dentry *h_parent,
				       struct au_branch *br)
{
	struct dentry *h_dentry;
	struct inode *h_inode;

	h_inode = d_inode(h_parent);
	vfsub_inode_lock_shared_nested(h_inode, AuLsc_I_CHILD2);
	h_dentry = vfsub_lkup_one(tgtname, h_parent);
	inode_unlock_shared(h_inode);
	return h_dentry;
}

static void au_call_do_plink_lkup(void *args)
{
	struct au_do_plink_lkup_args *a = args;
	*a->errp = au_do_plink_lkup(a->tgtname, a->h_parent, a->br);
}

/* lookup the plink-ed @inode under the branch at @bindex */
struct dentry *au_plink_lkup(struct inode *inode, aufs_bindex_t bindex)
{
	struct dentry *h_dentry, *h_parent;
	struct au_branch *br;
	int wkq_err;
	char a[PLINK_NAME_LEN];
	struct qstr tgtname = QSTR_INIT(a, 0);

	AuDebugOn(au_plink_maint(inode->i_sb, AuLock_NOPLM));

	br = au_sbr(inode->i_sb, bindex);
	h_parent = br->br_wbr->wbr_plink;
	tgtname.len = plink_name(a, sizeof(a), inode, bindex);

	if (!uid_eq(current_fsuid(), GLOBAL_ROOT_UID)) {
		struct au_do_plink_lkup_args args = {
			.errp		= &h_dentry,
			.tgtname	= &tgtname,
			.h_parent	= h_parent,
			.br		= br
		};

		wkq_err = au_wkq_wait(au_call_do_plink_lkup, &args);
		if (unlikely(wkq_err))
			h_dentry = ERR_PTR(wkq_err);
	} else
		h_dentry = au_do_plink_lkup(&tgtname, h_parent, br);

	return h_dentry;
}

/* create a pseudo-link */
static int do_whplink(struct qstr *tgt, struct dentry *h_parent,
		      struct dentry *h_dentry, struct au_branch *br)
{
	int err;
	struct path h_path = {
		.mnt = au_br_mnt(br)
	};
	struct inode *h_dir, *delegated;

	h_dir = d_inode(h_parent);
	inode_lock_nested(h_dir, AuLsc_I_CHILD2);
again:
	h_path.dentry = vfsub_lkup_one(tgt, h_parent);
	err = PTR_ERR(h_path.dentry);
	if (IS_ERR(h_path.dentry))
		goto out;

	err = 0;
	/* wh.plink dir is not monitored */
	/* todo: is it really safe? */
	if (d_is_positive(h_path.dentry)
	    && d_inode(h_path.dentry) != d_inode(h_dentry)) {
		delegated = NULL;
		err = vfsub_unlink(h_dir, &h_path, &delegated, /*force*/0);
		if (unlikely(err == -EWOULDBLOCK)) {
			pr_warn("cannot retry for NFSv4 delegation"
				" for an internal unlink\n");
			iput(delegated);
		}
		dput(h_path.dentry);
		h_path.dentry = NULL;
		if (!err)
			goto again;
	}
	if (!err && d_is_negative(h_path.dentry)) {
		delegated = NULL;
		err = vfsub_link(h_dentry, h_dir, &h_path, &delegated);
		if (unlikely(err == -EWOULDBLOCK)) {
			pr_warn("cannot retry for NFSv4 delegation"
				" for an internal link\n");
			iput(delegated);
		}
	}
	dput(h_path.dentry);

out:
	inode_unlock(h_dir);
	return err;
}

struct do_whplink_args {
	int *errp;
	struct qstr *tgt;
	struct dentry *h_parent;
	struct dentry *h_dentry;
	struct au_branch *br;
};

static void call_do_whplink(void *args)
{
	struct do_whplink_args *a = args;
	*a->errp = do_whplink(a->tgt, a->h_parent, a->h_dentry, a->br);
}

static int whplink(struct dentry *h_dentry, struct inode *inode,
		   aufs_bindex_t bindex, struct au_branch *br)
{
	int err, wkq_err;
	struct au_wbr *wbr;
	struct dentry *h_parent;
	char a[PLINK_NAME_LEN];
	struct qstr tgtname = QSTR_INIT(a, 0);

	wbr = au_sbr(inode->i_sb, bindex)->br_wbr;
	h_parent = wbr->wbr_plink;
	tgtname.len = plink_name(a, sizeof(a), inode, bindex);

	/* always superio. */
	if (!uid_eq(current_fsuid(), GLOBAL_ROOT_UID)) {
		struct do_whplink_args args = {
			.errp		= &err,
			.tgt		= &tgtname,
			.h_parent	= h_parent,
			.h_dentry	= h_dentry,
			.br		= br
		};
		wkq_err = au_wkq_wait(call_do_whplink, &args);
		if (unlikely(wkq_err))
			err = wkq_err;
	} else
		err = do_whplink(&tgtname, h_parent, h_dentry, br);

	return err;
}

/*
 * create a new pseudo-link for @h_dentry on @bindex.
 * the linked inode is held in aufs @inode.
 */
void au_plink_append(struct inode *inode, aufs_bindex_t bindex,
		     struct dentry *h_dentry)
{
	struct super_block *sb;
	struct au_sbinfo *sbinfo;
	struct hlist_bl_head *hbl;
	struct hlist_bl_node *pos;
	struct au_icntnr *icntnr;
	int found, err, cnt, i;

	sb = inode->i_sb;
	sbinfo = au_sbi(sb);
	AuDebugOn(!au_opt_test(au_mntflags(sb), PLINK));
	AuDebugOn(au_plink_maint(sb, AuLock_NOPLM));

	found = au_plink_test(inode);
	if (found)
		return;

	i = au_plink_hash(inode->i_ino);
	hbl = sbinfo->si_plink + i;
	au_igrab(inode);

	hlist_bl_lock(hbl);
	hlist_bl_for_each_entry(icntnr, pos, hbl, plink) {
		if (&icntnr->vfs_inode == inode) {
			found = 1;
			break;
		}
	}
	if (!found) {
		icntnr = container_of(inode, struct au_icntnr, vfs_inode);
		hlist_bl_add_head(&icntnr->plink, hbl);
	}
	hlist_bl_unlock(hbl);
	if (!found) {
		cnt = au_hbl_count(hbl);
#define msg "unexpectedly unblanced or too many pseudo-links"
		if (cnt > AUFS_PLINK_WARN)
			AuWarn1(msg ", %d\n", cnt);
#undef msg
		err = whplink(h_dentry, inode, bindex, au_sbr(sb, bindex));
		if (unlikely(err)) {
			pr_warn("err %d, damaged pseudo link.\n", err);
			au_hbl_del(&icntnr->plink, hbl);
			iput(&icntnr->vfs_inode);
		}
	} else
		iput(&icntnr->vfs_inode);
}

/* free all plinks */
void au_plink_put(struct super_block *sb, int verbose)
{
	int i, warned;
	struct au_sbinfo *sbinfo;
	struct hlist_bl_head *hbl;
	struct hlist_bl_node *pos, *tmp;
	struct au_icntnr *icntnr;

	SiMustWriteLock(sb);

	sbinfo = au_sbi(sb);
	AuDebugOn(!au_opt_test(au_mntflags(sb), PLINK));
	AuDebugOn(au_plink_maint(sb, AuLock_NOPLM));

	/* no spin_lock since sbinfo is write-locked */
	warned = 0;
	for (i = 0; i < AuPlink_NHASH; i++) {
		hbl = sbinfo->si_plink + i;
		if (!warned && verbose && !hlist_bl_empty(hbl)) {
			pr_warn("pseudo-link is not flushed");
			warned = 1;
		}
		hlist_bl_for_each_entry_safe(icntnr, pos, tmp, hbl, plink)
			iput(&icntnr->vfs_inode);
		INIT_HLIST_BL_HEAD(hbl);
	}
}

void au_plink_clean(struct super_block *sb, int verbose)
{
	struct dentry *root;

	root = sb->s_root;
	aufs_write_lock(root);
	if (au_opt_test(au_mntflags(sb), PLINK))
		au_plink_put(sb, verbose);
	aufs_write_unlock(root);
}

static int au_plink_do_half_refresh(struct inode *inode, aufs_bindex_t br_id)
{
	int do_put;
	aufs_bindex_t btop, bbot, bindex;

	do_put = 0;
	btop = au_ibtop(inode);
	bbot = au_ibbot(inode);
	if (btop >= 0) {
		for (bindex = btop; bindex <= bbot; bindex++) {
			if (!au_h_iptr(inode, bindex)
			    || au_ii_br_id(inode, bindex) != br_id)
				continue;
			au_set_h_iptr(inode, bindex, NULL, 0);
			do_put = 1;
			break;
		}
		if (do_put)
			for (bindex = btop; bindex <= bbot; bindex++)
				if (au_h_iptr(inode, bindex)) {
					do_put = 0;
					break;
				}
	} else
		do_put = 1;

	return do_put;
}

/* free the plinks on a branch specified by @br_id */
void au_plink_half_refresh(struct super_block *sb, aufs_bindex_t br_id)
{
	struct au_sbinfo *sbinfo;
	struct hlist_bl_head *hbl;
	struct hlist_bl_node *pos, *tmp;
	struct au_icntnr *icntnr;
	struct inode *inode;
	int i, do_put;

	SiMustWriteLock(sb);

	sbinfo = au_sbi(sb);
	AuDebugOn(!au_opt_test(au_mntflags(sb), PLINK));
	AuDebugOn(au_plink_maint(sb, AuLock_NOPLM));

	/* no bit_lock since sbinfo is write-locked */
	for (i = 0; i < AuPlink_NHASH; i++) {
		hbl = sbinfo->si_plink + i;
		hlist_bl_for_each_entry_safe(icntnr, pos, tmp, hbl, plink) {
			inode = au_igrab(&icntnr->vfs_inode);
			ii_write_lock_child(inode);
			do_put = au_plink_do_half_refresh(inode, br_id);
			if (do_put) {
				hlist_bl_del(&icntnr->plink);
				iput(inode);
			}
			ii_write_unlock(inode);
			iput(inode);
		}
	}
}
