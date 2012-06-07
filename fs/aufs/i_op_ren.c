/*
 * Copyright (C) 2005-2012 Junjiro R. Okajima
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
 * inode operation (rename entry)
 * todo: this is crazy monster
 */

#include "aufs.h"

enum { AuSRC, AuDST, AuSrcDst };
enum { AuPARENT, AuCHILD, AuParentChild };

#define AuRen_ISDIR	1
#define AuRen_ISSAMEDIR	(1 << 1)
#define AuRen_WHSRC	(1 << 2)
#define AuRen_WHDST	(1 << 3)
#define AuRen_MNT_WRITE	(1 << 4)
#define AuRen_DT_DSTDIR	(1 << 5)
#define AuRen_DIROPQ	(1 << 6)
#define AuRen_CPUP	(1 << 7)
#define au_ftest_ren(flags, name)	((flags) & AuRen_##name)
#define au_fset_ren(flags, name) \
	do { (flags) |= AuRen_##name; } while (0)
#define au_fclr_ren(flags, name) \
	do { (flags) &= ~AuRen_##name; } while (0)

struct au_ren_args {
	struct {
		struct dentry *dentry, *h_dentry, *parent, *h_parent,
			*wh_dentry;
		struct inode *dir, *inode;
		struct au_hinode *hdir;
		struct au_dtime dt[AuParentChild];
		aufs_bindex_t bstart;
	} sd[AuSrcDst];

#define src_dentry	sd[AuSRC].dentry
#define src_dir		sd[AuSRC].dir
#define src_inode	sd[AuSRC].inode
#define src_h_dentry	sd[AuSRC].h_dentry
#define src_parent	sd[AuSRC].parent
#define src_h_parent	sd[AuSRC].h_parent
#define src_wh_dentry	sd[AuSRC].wh_dentry
#define src_hdir	sd[AuSRC].hdir
#define src_h_dir	sd[AuSRC].hdir->hi_inode
#define src_dt		sd[AuSRC].dt
#define src_bstart	sd[AuSRC].bstart

#define dst_dentry	sd[AuDST].dentry
#define dst_dir		sd[AuDST].dir
#define dst_inode	sd[AuDST].inode
#define dst_h_dentry	sd[AuDST].h_dentry
#define dst_parent	sd[AuDST].parent
#define dst_h_parent	sd[AuDST].h_parent
#define dst_wh_dentry	sd[AuDST].wh_dentry
#define dst_hdir	sd[AuDST].hdir
#define dst_h_dir	sd[AuDST].hdir->hi_inode
#define dst_dt		sd[AuDST].dt
#define dst_bstart	sd[AuDST].bstart

	struct dentry *h_trap;
	struct au_branch *br;
	struct au_hinode *src_hinode;
	struct path h_path;
	struct au_nhash whlist;
	aufs_bindex_t btgt, src_bwh, src_bdiropq;

	unsigned int flags;

	struct au_whtmp_rmdir *thargs;
	struct dentry *h_dst;
};

/* ---------------------------------------------------------------------- */

/*
 * functions for reverting.
 * when an error happened in a single rename systemcall, we should revert
 * everything as if nothing happend.
 * we don't need to revert the copied-up/down the parent dir since they are
 * harmless.
 */

#define RevertFailure(fmt, ...) do { \
	AuIOErr("revert failure: " fmt " (%d, %d)\n", \
		##__VA_ARGS__, err, rerr); \
	err = -EIO; \
} while (0)

static void au_ren_rev_diropq(int err, struct au_ren_args *a)
{
	int rerr;

	au_hn_imtx_lock_nested(a->src_hinode, AuLsc_I_CHILD);
	rerr = au_diropq_remove(a->src_dentry, a->btgt);
	au_hn_imtx_unlock(a->src_hinode);
	au_set_dbdiropq(a->src_dentry, a->src_bdiropq);
	if (rerr)
		RevertFailure("remove diropq %.*s", AuDLNPair(a->src_dentry));
}

static void au_ren_rev_rename(int err, struct au_ren_args *a)
{
	int rerr;

	a->h_path.dentry = au_lkup_one(&a->src_dentry->d_name, a->src_h_parent,
				       a->br, /*nd*/NULL);
	rerr = PTR_ERR(a->h_path.dentry);
	if (IS_ERR(a->h_path.dentry)) {
		RevertFailure("au_lkup_one %.*s", AuDLNPair(a->src_dentry));
		return;
	}

	rerr = vfsub_rename(a->dst_h_dir,
			    au_h_dptr(a->src_dentry, a->btgt),
			    a->src_h_dir, &a->h_path);
	d_drop(a->h_path.dentry);
	dput(a->h_path.dentry);
	/* au_set_h_dptr(a->src_dentry, a->btgt, NULL); */
	if (rerr)
		RevertFailure("rename %.*s", AuDLNPair(a->src_dentry));
}

static void au_ren_rev_cpup(int err, struct au_ren_args *a)
{
	int rerr;

	a->h_path.dentry = a->dst_h_dentry;
	rerr = vfsub_unlink(a->dst_h_dir, &a->h_path, /*force*/0);
	au_set_h_dptr(a->src_dentry, a->btgt, NULL);
	au_set_dbstart(a->src_dentry, a->src_bstart);
	if (rerr)
		RevertFailure("unlink %.*s", AuDLNPair(a->dst_h_dentry));
}

static void au_ren_rev_whtmp(int err, struct au_ren_args *a)
{
	int rerr;

	a->h_path.dentry = au_lkup_one(&a->dst_dentry->d_name, a->dst_h_parent,
				       a->br, /*nd*/NULL);
	rerr = PTR_ERR(a->h_path.dentry);
	if (IS_ERR(a->h_path.dentry)) {
		RevertFailure("lookup %.*s", AuDLNPair(a->dst_dentry));
		return;
	}
	if (a->h_path.dentry->d_inode) {
		d_drop(a->h_path.dentry);
		dput(a->h_path.dentry);
		return;
	}

	rerr = vfsub_rename(a->dst_h_dir, a->h_dst, a->dst_h_dir, &a->h_path);
	d_drop(a->h_path.dentry);
	dput(a->h_path.dentry);
	if (!rerr)
		au_set_h_dptr(a->dst_dentry, a->btgt, dget(a->h_dst));
	else
		RevertFailure("rename %.*s", AuDLNPair(a->h_dst));
}

static void au_ren_rev_whsrc(int err, struct au_ren_args *a)
{
	int rerr;

	a->h_path.dentry = a->src_wh_dentry;
	rerr = au_wh_unlink_dentry(a->src_h_dir, &a->h_path, a->src_dentry);
	au_set_dbwh(a->src_dentry, a->src_bwh);
	if (rerr)
		RevertFailure("unlink %.*s", AuDLNPair(a->src_wh_dentry));
}
#undef RevertFailure

/* ---------------------------------------------------------------------- */

/*
 * when we have to copyup the renaming entry, do it with the rename-target name
 * in order to minimize the cost (the later actual rename is unnecessary).
 * otherwise rename it on the target branch.
 */
static int au_ren_or_cpup(struct au_ren_args *a)
{
	int err;
	struct dentry *d;

	d = a->src_dentry;
	if (au_dbstart(d) == a->btgt) {
		a->h_path.dentry = a->dst_h_dentry;
		if (au_ftest_ren(a->flags, DIROPQ)
		    && au_dbdiropq(d) == a->btgt)
			au_fclr_ren(a->flags, DIROPQ);
		AuDebugOn(au_dbstart(d) != a->btgt);
		err = vfsub_rename(a->src_h_dir, au_h_dptr(d, a->btgt),
				   a->dst_h_dir, &a->h_path);
	} else {
		struct mutex *h_mtx = &a->src_h_dentry->d_inode->i_mutex;
		struct file *h_file;

		au_fset_ren(a->flags, CPUP);
		mutex_lock_nested(h_mtx, AuLsc_I_CHILD);
		au_set_dbstart(d, a->btgt);
		au_set_h_dptr(d, a->btgt, dget(a->dst_h_dentry));
		h_file = au_h_open_pre(d, a->src_bstart);
		if (IS_ERR(h_file)) {
			err = PTR_ERR(h_file);
			h_file = NULL;
		} else
			err = au_sio_cpup_single(d, a->btgt, a->src_bstart, -1,
						 !AuCpup_DTIME, a->dst_parent);
		mutex_unlock(h_mtx);
		au_h_open_post(d, a->src_bstart, h_file);
		if (!err) {
			d = a->dst_dentry;
			au_set_h_dptr(d, a->btgt, NULL);
			au_update_dbstart(d);
		} else {
			au_set_h_dptr(d, a->btgt, NULL);
			au_set_dbstart(d, a->src_bstart);
		}
	}
	if (!err && a->h_dst)
		/* it will be set to dinfo later */
		dget(a->h_dst);

	return err;
}

/* cf. aufs_rmdir() */
static int au_ren_del_whtmp(struct au_ren_args *a)
{
	int err;
	struct inode *dir;

	dir = a->dst_dir;
	SiMustAnyLock(dir->i_sb);
	if (!au_nhash_test_longer_wh(&a->whlist, a->btgt,
				     au_sbi(dir->i_sb)->si_dirwh)
	    || au_test_fs_remote(a->h_dst->d_sb)) {
		err = au_whtmp_rmdir(dir, a->btgt, a->h_dst, &a->whlist);
		if (unlikely(err))
			pr_warn("failed removing whtmp dir %.*s (%d), "
				"ignored.\n", AuDLNPair(a->h_dst), err);
	} else {
		au_nhash_wh_free(&a->thargs->whlist);
		a->thargs->whlist = a->whlist;
		a->whlist.nh_num = 0;
		au_whtmp_kick_rmdir(dir, a->btgt, a->h_dst, a->thargs);
		dput(a->h_dst);
		a->thargs = NULL;
	}

	return 0;
}

/* make it 'opaque' dir. */
static int au_ren_diropq(struct au_ren_args *a)
{
	int err;
	struct dentry *diropq;

	err = 0;
	a->src_bdiropq = au_dbdiropq(a->src_dentry);
	a->src_hinode = au_hi(a->src_inode, a->btgt);
	au_hn_imtx_lock_nested(a->src_hinode, AuLsc_I_CHILD);
	diropq = au_diropq_create(a->src_dentry, a->btgt);
	au_hn_imtx_unlock(a->src_hinode);
	if (IS_ERR(diropq))
		err = PTR_ERR(diropq);
	dput(diropq);

	return err;
}

static int do_rename(struct au_ren_args *a)
{
	int err;
	struct dentry *d, *h_d;

	/* prepare workqueue args for asynchronous rmdir */
	h_d = a->dst_h_dentry;
	if (au_ftest_ren(a->flags, ISDIR) && h_d->d_inode) {
		err = -ENOMEM;
		a->thargs = au_whtmp_rmdir_alloc(a->src_dentry->d_sb, GFP_NOFS);
		if (unlikely(!a->thargs))
			goto out;
		a->h_dst = dget(h_d);
	}

	/* create whiteout for src_dentry */
	if (au_ftest_ren(a->flags, WHSRC)) {
		a->src_bwh = au_dbwh(a->src_dentry);
		AuDebugOn(a->src_bwh >= 0);
		a->src_wh_dentry
			= au_wh_create(a->src_dentry, a->btgt, a->src_h_parent);
		err = PTR_ERR(a->src_wh_dentry);
		if (IS_ERR(a->src_wh_dentry))
			goto out_thargs;
	}

	/* lookup whiteout for dentry */
	if (au_ftest_ren(a->flags, WHDST)) {
		h_d = au_wh_lkup(a->dst_h_parent, &a->dst_dentry->d_name,
				 a->br);
		err = PTR_ERR(h_d);
		if (IS_ERR(h_d))
			goto out_whsrc;
		if (!h_d->d_inode)
			dput(h_d);
		else
			a->dst_wh_dentry = h_d;
	}

	/* rename dentry to tmpwh */
	if (a->thargs) {
		err = au_whtmp_ren(a->dst_h_dentry, a->br);
		if (unlikely(err))
			goto out_whdst;

		d = a->dst_dentry;
		au_set_h_dptr(d, a->btgt, NULL);
		err = au_lkup_neg(d, a->btgt);
		if (unlikely(err))
			goto out_whtmp;
		a->dst_h_dentry = au_h_dptr(d, a->btgt);
	}

	/* cpup src */
	if (a->dst_h_dentry->d_inode && a->src_bstart != a->btgt) {
		struct mutex *h_mtx = &a->src_h_dentry->d_inode->i_mutex;
		struct file *h_file;

		mutex_lock_nested(h_mtx, AuLsc_I_CHILD);
		AuDebugOn(au_dbstart(a->src_dentry) != a->src_bstart);
		h_file = au_h_open_pre(a->src_dentry, a->src_bstart);
		if (IS_ERR(h_file)) {
			err = PTR_ERR(h_file);
			h_file = NULL;
		} else
			err = au_sio_cpup_simple(a->src_dentry, a->btgt, -1,
						 !AuCpup_DTIME);
		mutex_unlock(h_mtx);
		au_h_open_post(a->src_dentry, a->src_bstart, h_file);
		if (unlikely(err))
			goto out_whtmp;
	}

	/* rename by vfs_rename or cpup */
	d = a->dst_dentry;
	if (au_ftest_ren(a->flags, ISDIR)
	    && (a->dst_wh_dentry
		|| au_dbdiropq(d) == a->btgt
		/* hide the lower to keep xino */
		|| a->btgt < au_dbend(d)
		|| au_opt_test(au_mntflags(d->d_sb), ALWAYS_DIROPQ)))
		au_fset_ren(a->flags, DIROPQ);
	err = au_ren_or_cpup(a);
	if (unlikely(err))
		/* leave the copied-up one */
		goto out_whtmp;

	/* make dir opaque */
	if (au_ftest_ren(a->flags, DIROPQ)) {
		err = au_ren_diropq(a);
		if (unlikely(err))
			goto out_rename;
	}

	/* update target timestamps */
	AuDebugOn(au_dbstart(a->src_dentry) != a->btgt);
	a->h_path.dentry = au_h_dptr(a->src_dentry, a->btgt);
	vfsub_update_h_iattr(&a->h_path, /*did*/NULL); /*ignore*/
	a->src_inode->i_ctime = a->h_path.dentry->d_inode->i_ctime;

	/* remove whiteout for dentry */
	if (a->dst_wh_dentry) {
		a->h_path.dentry = a->dst_wh_dentry;
		err = au_wh_unlink_dentry(a->dst_h_dir, &a->h_path,
					  a->dst_dentry);
		if (unlikely(err))
			goto out_diropq;
	}

	/* remove whtmp */
	if (a->thargs)
		au_ren_del_whtmp(a); /* ignore this error */

	err = 0;
	goto out_success;

out_diropq:
	if (au_ftest_ren(a->flags, DIROPQ))
		au_ren_rev_diropq(err, a);
out_rename:
	if (!au_ftest_ren(a->flags, CPUP))
		au_ren_rev_rename(err, a);
	else
		au_ren_rev_cpup(err, a);
	dput(a->h_dst);
out_whtmp:
	if (a->thargs)
		au_ren_rev_whtmp(err, a);
out_whdst:
	dput(a->dst_wh_dentry);
	a->dst_wh_dentry = NULL;
out_whsrc:
	if (a->src_wh_dentry)
		au_ren_rev_whsrc(err, a);
out_success:
	dput(a->src_wh_dentry);
	dput(a->dst_wh_dentry);
out_thargs:
	if (a->thargs) {
		dput(a->h_dst);
		au_whtmp_rmdir_free(a->thargs);
		a->thargs = NULL;
	}
out:
	return err;
}

/* ---------------------------------------------------------------------- */

/*
 * test if @dentry dir can be rename destination or not.
 * success means, it is a logically empty dir.
 */
static int may_rename_dstdir(struct dentry *dentry, struct au_nhash *whlist)
{
	return au_test_empty(dentry, whlist);
}

/*
 * test if @dentry dir can be rename source or not.
 * if it can, return 0 and @children is filled.
 * success means,
 * - it is a logically empty dir.
 * - or, it exists on writable branch and has no children including whiteouts
 *       on the lower branch.
 */
static int may_rename_srcdir(struct dentry *dentry, aufs_bindex_t btgt)
{
	int err;
	unsigned int rdhash;
	aufs_bindex_t bstart;

	bstart = au_dbstart(dentry);
	if (bstart != btgt) {
		struct au_nhash whlist;

		SiMustAnyLock(dentry->d_sb);
		rdhash = au_sbi(dentry->d_sb)->si_rdhash;
		if (!rdhash)
			rdhash = au_rdhash_est(au_dir_size(/*file*/NULL,
							   dentry));
		err = au_nhash_alloc(&whlist, rdhash, GFP_NOFS);
		if (unlikely(err))
			goto out;
		err = au_test_empty(dentry, &whlist);
		au_nhash_wh_free(&whlist);
		goto out;
	}

	if (bstart == au_dbtaildir(dentry))
		return 0; /* success */

	err = au_test_empty_lower(dentry);

out:
	if (err == -ENOTEMPTY) {
		AuWarn1("renaming dir who has child(ren) on multiple branches,"
			" is not supported\n");
		err = -EXDEV;
	}
	return err;
}

/* side effect: sets whlist and h_dentry */
static int au_ren_may_dir(struct au_ren_args *a)
{
	int err;
	unsigned int rdhash;
	struct dentry *d;

	d = a->dst_dentry;
	SiMustAnyLock(d->d_sb);

	err = 0;
	if (au_ftest_ren(a->flags, ISDIR) && a->dst_inode) {
		rdhash = au_sbi(d->d_sb)->si_rdhash;
		if (!rdhash)
			rdhash = au_rdhash_est(au_dir_size(/*file*/NULL, d));
		err = au_nhash_alloc(&a->whlist, rdhash, GFP_NOFS);
		if (unlikely(err))
			goto out;

		au_set_dbstart(d, a->dst_bstart);
		err = may_rename_dstdir(d, &a->whlist);
		au_set_dbstart(d, a->btgt);
	}
	a->dst_h_dentry = au_h_dptr(d, au_dbstart(d));
	if (unlikely(err))
		goto out;

	d = a->src_dentry;
	a->src_h_dentry = au_h_dptr(d, au_dbstart(d));
	if (au_ftest_ren(a->flags, ISDIR)) {
		err = may_rename_srcdir(d, a->btgt);
		if (unlikely(err)) {
			au_nhash_wh_free(&a->whlist);
			a->whlist.nh_num = 0;
		}
	}
out:
	return err;
}

/* ---------------------------------------------------------------------- */

/*
 * simple tests for rename.
 * following the checks in vfs, plus the parent-child relationship.
 */
static int au_may_ren(struct au_ren_args *a)
{
	int err, isdir;
	struct inode *h_inode;

	if (a->src_bstart == a->btgt) {
		err = au_may_del(a->src_dentry, a->btgt, a->src_h_parent,
				 au_ftest_ren(a->flags, ISDIR));
		if (unlikely(err))
			goto out;
		err = -EINVAL;
		if (unlikely(a->src_h_dentry == a->h_trap))
			goto out;
	}

	err = 0;
	if (a->dst_bstart != a->btgt)
		goto out;

	err = -ENOTEMPTY;
	if (unlikely(a->dst_h_dentry == a->h_trap))
		goto out;

	err = -EIO;
	h_inode = a->dst_h_dentry->d_inode;
	isdir = !!au_ftest_ren(a->flags, ISDIR);
	if (!a->dst_dentry->d_inode) {
		if (unlikely(h_inode))
			goto out;
		err = au_may_add(a->dst_dentry, a->btgt, a->dst_h_parent,
				 isdir);
	} else {
		if (unlikely(!h_inode || !h_inode->i_nlink))
			goto out;
		err = au_may_del(a->dst_dentry, a->btgt, a->dst_h_parent,
				 isdir);
		if (unlikely(err))
			goto out;
	}

out:
	if (unlikely(err == -ENOENT || err == -EEXIST))
		err = -EIO;
	AuTraceErr(err);
	return err;
}

/* ---------------------------------------------------------------------- */

/*
 * locking order
 * (VFS)
 * - src_dir and dir by lock_rename()
 * - inode if exitsts
 * (aufs)
 * - lock all
 *   + src_dentry and dentry by aufs_read_and_write_lock2() which calls,
 *     + si_read_lock
 *     + di_write_lock2_child()
 *       + di_write_lock_child()
 *	   + ii_write_lock_child()
 *       + di_write_lock_child2()
 *	   + ii_write_lock_child2()
 *     + src_parent and parent
 *       + di_write_lock_parent()
 *	   + ii_write_lock_parent()
 *       + di_write_lock_parent2()
 *	   + ii_write_lock_parent2()
 *   + lower src_dir and dir by vfsub_lock_rename()
 *   + verify the every relationships between child and parent. if any
 *     of them failed, unlock all and return -EBUSY.
 */
static void au_ren_unlock(struct au_ren_args *a)
{
	struct super_block *sb;

	sb = a->dst_dentry->d_sb;
	if (au_ftest_ren(a->flags, MNT_WRITE))
		mnt_drop_write(a->br->br_mnt);
	vfsub_unlock_rename(a->src_h_parent, a->src_hdir,
			    a->dst_h_parent, a->dst_hdir);
}

static int au_ren_lock(struct au_ren_args *a)
{
	int err;
	unsigned int udba;

	err = 0;
	a->src_h_parent = au_h_dptr(a->src_parent, a->btgt);
	a->src_hdir = au_hi(a->src_dir, a->btgt);
	a->dst_h_parent = au_h_dptr(a->dst_parent, a->btgt);
	a->dst_hdir = au_hi(a->dst_dir, a->btgt);
	a->h_trap = vfsub_lock_rename(a->src_h_parent, a->src_hdir,
				      a->dst_h_parent, a->dst_hdir);
	udba = au_opt_udba(a->src_dentry->d_sb);
	if (unlikely(a->src_hdir->hi_inode != a->src_h_parent->d_inode
		     || a->dst_hdir->hi_inode != a->dst_h_parent->d_inode))
		err = au_busy_or_stale();
	if (!err && au_dbstart(a->src_dentry) == a->btgt)
		err = au_h_verify(a->src_h_dentry, udba,
				  a->src_h_parent->d_inode, a->src_h_parent,
				  a->br);
	if (!err && au_dbstart(a->dst_dentry) == a->btgt)
		err = au_h_verify(a->dst_h_dentry, udba,
				  a->dst_h_parent->d_inode, a->dst_h_parent,
				  a->br);
	if (!err) {
		err = mnt_want_write(a->br->br_mnt);
		if (unlikely(err))
			goto out_unlock;
		au_fset_ren(a->flags, MNT_WRITE);
		goto out; /* success */
	}

	err = au_busy_or_stale();

out_unlock:
	au_ren_unlock(a);
out:
	return err;
}

/* ---------------------------------------------------------------------- */

static void au_ren_refresh_dir(struct au_ren_args *a)
{
	struct inode *dir;

	dir = a->dst_dir;
	dir->i_version++;
	if (au_ftest_ren(a->flags, ISDIR)) {
		/* is this updating defined in POSIX? */
		au_cpup_attr_timesizes(a->src_inode);
		au_cpup_attr_nlink(dir, /*force*/1);
	}

	if (au_ibstart(dir) == a->btgt)
		au_cpup_attr_timesizes(dir);

	if (au_ftest_ren(a->flags, ISSAMEDIR))
		return;

	dir = a->src_dir;
	dir->i_version++;
	if (au_ftest_ren(a->flags, ISDIR))
		au_cpup_attr_nlink(dir, /*force*/1);
	if (au_ibstart(dir) == a->btgt)
		au_cpup_attr_timesizes(dir);
}

static void au_ren_refresh(struct au_ren_args *a)
{
	aufs_bindex_t bend, bindex;
	struct dentry *d, *h_d;
	struct inode *i, *h_i;
	struct super_block *sb;

	d = a->dst_dentry;
	d_drop(d);
	if (a->h_dst)
		/* already dget-ed by au_ren_or_cpup() */
		au_set_h_dptr(d, a->btgt, a->h_dst);

	i = a->dst_inode;
	if (i) {
		if (!au_ftest_ren(a->flags, ISDIR))
			vfsub_drop_nlink(i);
		else {
			vfsub_dead_dir(i);
			au_cpup_attr_timesizes(i);
		}
		au_update_dbrange(d, /*do_put_zero*/1);
	} else {
		bend = a->btgt;
		for (bindex = au_dbstart(d); bindex < bend; bindex++)
			au_set_h_dptr(d, bindex, NULL);
		bend = au_dbend(d);
		for (bindex = a->btgt + 1; bindex <= bend; bindex++)
			au_set_h_dptr(d, bindex, NULL);
		au_update_dbrange(d, /*do_put_zero*/0);
	}

	d = a->src_dentry;
	au_set_dbwh(d, -1);
	bend = au_dbend(d);
	for (bindex = a->btgt + 1; bindex <= bend; bindex++) {
		h_d = au_h_dptr(d, bindex);
		if (h_d)
			au_set_h_dptr(d, bindex, NULL);
	}
	au_set_dbend(d, a->btgt);

	sb = d->d_sb;
	i = a->src_inode;
	if (au_opt_test(au_mntflags(sb), PLINK) && au_plink_test(i))
		return; /* success */

	bend = au_ibend(i);
	for (bindex = a->btgt + 1; bindex <= bend; bindex++) {
		h_i = au_h_iptr(i, bindex);
		if (h_i) {
			au_xino_write(sb, bindex, h_i->i_ino, /*ino*/0);
			/* ignore this error */
			au_set_h_iptr(i, bindex, NULL, 0);
		}
	}
	au_set_ibend(i, a->btgt);
}

/* ---------------------------------------------------------------------- */

/* mainly for link(2) and rename(2) */
int au_wbr(struct dentry *dentry, aufs_bindex_t btgt)
{
	aufs_bindex_t bdiropq, bwh;
	struct dentry *parent;
	struct au_branch *br;

	parent = dentry->d_parent;
	IMustLock(parent->d_inode); /* dir is locked */

	bdiropq = au_dbdiropq(parent);
	bwh = au_dbwh(dentry);
	br = au_sbr(dentry->d_sb, btgt);
	if (au_br_rdonly(br)
	    || (0 <= bdiropq && bdiropq < btgt)
	    || (0 <= bwh && bwh < btgt))
		btgt = -1;

	AuDbg("btgt %d\n", btgt);
	return btgt;
}

/* sets src_bstart, dst_bstart and btgt */
static int au_ren_wbr(struct au_ren_args *a)
{
	int err;
	struct au_wr_dir_args wr_dir_args = {
		/* .force_btgt	= -1, */
		.flags		= AuWrDir_ADD_ENTRY
	};

	a->src_bstart = au_dbstart(a->src_dentry);
	a->dst_bstart = au_dbstart(a->dst_dentry);
	if (au_ftest_ren(a->flags, ISDIR))
		au_fset_wrdir(wr_dir_args.flags, ISDIR);
	wr_dir_args.force_btgt = a->src_bstart;
	if (a->dst_inode && a->dst_bstart < a->src_bstart)
		wr_dir_args.force_btgt = a->dst_bstart;
	wr_dir_args.force_btgt = au_wbr(a->dst_dentry, wr_dir_args.force_btgt);
	err = au_wr_dir(a->dst_dentry, a->src_dentry, &wr_dir_args);
	a->btgt = err;

	return err;
}

static void au_ren_dt(struct au_ren_args *a)
{
	a->h_path.dentry = a->src_h_parent;
	au_dtime_store(a->src_dt + AuPARENT, a->src_parent, &a->h_path);
	if (!au_ftest_ren(a->flags, ISSAMEDIR)) {
		a->h_path.dentry = a->dst_h_parent;
		au_dtime_store(a->dst_dt + AuPARENT, a->dst_parent, &a->h_path);
	}

	au_fclr_ren(a->flags, DT_DSTDIR);
	if (!au_ftest_ren(a->flags, ISDIR))
		return;

	a->h_path.dentry = a->src_h_dentry;
	au_dtime_store(a->src_dt + AuCHILD, a->src_dentry, &a->h_path);
	if (a->dst_h_dentry->d_inode) {
		au_fset_ren(a->flags, DT_DSTDIR);
		a->h_path.dentry = a->dst_h_dentry;
		au_dtime_store(a->dst_dt + AuCHILD, a->dst_dentry, &a->h_path);
	}
}

static void au_ren_rev_dt(int err, struct au_ren_args *a)
{
	struct dentry *h_d;
	struct mutex *h_mtx;

	au_dtime_revert(a->src_dt + AuPARENT);
	if (!au_ftest_ren(a->flags, ISSAMEDIR))
		au_dtime_revert(a->dst_dt + AuPARENT);

	if (au_ftest_ren(a->flags, ISDIR) && err != -EIO) {
		h_d = a->src_dt[AuCHILD].dt_h_path.dentry;
		h_mtx = &h_d->d_inode->i_mutex;
		mutex_lock_nested(h_mtx, AuLsc_I_CHILD);
		au_dtime_revert(a->src_dt + AuCHILD);
		mutex_unlock(h_mtx);

		if (au_ftest_ren(a->flags, DT_DSTDIR)) {
			h_d = a->dst_dt[AuCHILD].dt_h_path.dentry;
			h_mtx = &h_d->d_inode->i_mutex;
			mutex_lock_nested(h_mtx, AuLsc_I_CHILD);
			au_dtime_revert(a->dst_dt + AuCHILD);
			mutex_unlock(h_mtx);
		}
	}
}

/* ---------------------------------------------------------------------- */

int aufs_rename(struct inode *_src_dir, struct dentry *_src_dentry,
		struct inode *_dst_dir, struct dentry *_dst_dentry)
{
	int err, flags;
	/* reduce stack space */
	struct au_ren_args *a;

	AuDbg("%.*s, %.*s\n", AuDLNPair(_src_dentry), AuDLNPair(_dst_dentry));
	IMustLock(_src_dir);
	IMustLock(_dst_dir);

	err = -ENOMEM;
	BUILD_BUG_ON(sizeof(*a) > PAGE_SIZE);
	a = kzalloc(sizeof(*a), GFP_NOFS);
	if (unlikely(!a))
		goto out;

	a->src_dir = _src_dir;
	a->src_dentry = _src_dentry;
	a->src_inode = a->src_dentry->d_inode;
	a->src_parent = a->src_dentry->d_parent; /* dir inode is locked */
	a->dst_dir = _dst_dir;
	a->dst_dentry = _dst_dentry;
	a->dst_inode = a->dst_dentry->d_inode;
	a->dst_parent = a->dst_dentry->d_parent; /* dir inode is locked */
	if (a->dst_inode) {
		IMustLock(a->dst_inode);
		au_igrab(a->dst_inode);
	}

	err = -ENOTDIR;
	flags = AuLock_FLUSH | AuLock_NOPLM | AuLock_GEN;
	if (S_ISDIR(a->src_inode->i_mode)) {
		au_fset_ren(a->flags, ISDIR);
		if (unlikely(a->dst_inode && !S_ISDIR(a->dst_inode->i_mode)))
			goto out_free;
		err = aufs_read_and_write_lock2(a->dst_dentry, a->src_dentry,
						AuLock_DIR | flags);
	} else
		err = aufs_read_and_write_lock2(a->dst_dentry, a->src_dentry,
						flags);
	if (unlikely(err))
		goto out_free;

	err = au_d_hashed_positive(a->src_dentry);
	if (unlikely(err))
		goto out_unlock;
	err = -ENOENT;
	if (a->dst_inode) {
		/*
		 * If it is a dir, VFS unhash dst_dentry before this
		 * function. It means we cannot rely upon d_unhashed().
		 */
		if (unlikely(!a->dst_inode->i_nlink))
			goto out_unlock;
		if (!S_ISDIR(a->dst_inode->i_mode)) {
			err = au_d_hashed_positive(a->dst_dentry);
			if (unlikely(err))
				goto out_unlock;
		} else if (unlikely(IS_DEADDIR(a->dst_inode)))
			goto out_unlock;
	} else if (unlikely(d_unhashed(a->dst_dentry)))
		goto out_unlock;

	/*
	 * is it possible?
	 * yes, it happend (in linux-3.3-rcN) but I don't know why.
	 * there may exist a problem somewhere else.
	 */
	err = -EINVAL;
	if (unlikely(a->dst_parent->d_inode == a->src_dentry->d_inode))
		goto out_unlock;

	au_fset_ren(a->flags, ISSAMEDIR); /* temporary */
	di_write_lock_parent(a->dst_parent);

	/* which branch we process */
	err = au_ren_wbr(a);
	if (unlikely(err < 0))
		goto out_parent;
	a->br = au_sbr(a->dst_dentry->d_sb, a->btgt);
	a->h_path.mnt = a->br->br_mnt;

	/* are they available to be renamed */
	err = au_ren_may_dir(a);
	if (unlikely(err))
		goto out_children;

	/* prepare the writable parent dir on the same branch */
	if (a->dst_bstart == a->btgt) {
		au_fset_ren(a->flags, WHDST);
	} else {
		err = au_cpup_dirs(a->dst_dentry, a->btgt);
		if (unlikely(err))
			goto out_children;
	}

	if (a->src_dir != a->dst_dir) {
		/*
		 * this temporary unlock is safe,
		 * because both dir->i_mutex are locked.
		 */
		di_write_unlock(a->dst_parent);
		di_write_lock_parent(a->src_parent);
		err = au_wr_dir_need_wh(a->src_dentry,
					au_ftest_ren(a->flags, ISDIR),
					&a->btgt);
		di_write_unlock(a->src_parent);
		di_write_lock2_parent(a->src_parent, a->dst_parent, /*isdir*/1);
		au_fclr_ren(a->flags, ISSAMEDIR);
	} else
		err = au_wr_dir_need_wh(a->src_dentry,
					au_ftest_ren(a->flags, ISDIR),
					&a->btgt);
	if (unlikely(err < 0))
		goto out_children;
	if (err)
		au_fset_ren(a->flags, WHSRC);

	/* lock them all */
	err = au_ren_lock(a);
	if (unlikely(err))
		goto out_children;

	if (!au_opt_test(au_mntflags(a->dst_dir->i_sb), UDBA_NONE))
		err = au_may_ren(a);
	else if (unlikely(a->dst_dentry->d_name.len > AUFS_MAX_NAMELEN))
		err = -ENAMETOOLONG;
	if (unlikely(err))
		goto out_hdir;

	/* store timestamps to be revertible */
	au_ren_dt(a);

	/* here we go */
	err = do_rename(a);
	if (unlikely(err))
		goto out_dt;

	/* update dir attributes */
	au_ren_refresh_dir(a);

	/* dput/iput all lower dentries */
	au_ren_refresh(a);

	goto out_hdir; /* success */

out_dt:
	au_ren_rev_dt(err, a);
out_hdir:
	au_ren_unlock(a);
out_children:
	au_nhash_wh_free(&a->whlist);
	if (err && a->dst_inode && a->dst_bstart != a->btgt) {
		AuDbg("bstart %d, btgt %d\n", a->dst_bstart, a->btgt);
		au_set_h_dptr(a->dst_dentry, a->btgt, NULL);
		au_set_dbstart(a->dst_dentry, a->dst_bstart);
	}
out_parent:
	if (!err)
		d_move(a->src_dentry, a->dst_dentry);
	else {
		au_update_dbstart(a->dst_dentry);
		if (!a->dst_inode)
			d_drop(a->dst_dentry);
	}
	if (au_ftest_ren(a->flags, ISSAMEDIR))
		di_write_unlock(a->dst_parent);
	else
		di_write_unlock2(a->src_parent, a->dst_parent);
out_unlock:
	aufs_read_and_write_unlock2(a->dst_dentry, a->src_dentry);
out_free:
	iput(a->dst_inode);
	if (a->thargs)
		au_whtmp_rmdir_free(a->thargs);
	kfree(a);
out:
	AuTraceErr(err);
	return err;
}
