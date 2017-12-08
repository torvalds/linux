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
 * inode operation (rename entry)
 * todo: this is crazy monster
 */

#include "aufs.h"

enum { AuSRC, AuDST, AuSrcDst };
enum { AuPARENT, AuCHILD, AuParentChild };

#define AuRen_ISDIR_SRC		1
#define AuRen_ISDIR_DST		(1 << 1)
#define AuRen_ISSAMEDIR		(1 << 2)
#define AuRen_WHSRC		(1 << 3)
#define AuRen_WHDST		(1 << 4)
#define AuRen_MNT_WRITE		(1 << 5)
#define AuRen_DT_DSTDIR		(1 << 6)
#define AuRen_DIROPQ_SRC	(1 << 7)
#define AuRen_DIROPQ_DST	(1 << 8)
#define AuRen_DIRREN		(1 << 9)
#define AuRen_DROPPED_SRC	(1 << 10)
#define AuRen_DROPPED_DST	(1 << 11)
#define au_ftest_ren(flags, name)	((flags) & AuRen_##name)
#define au_fset_ren(flags, name) \
	do { (flags) |= AuRen_##name; } while (0)
#define au_fclr_ren(flags, name) \
	do { (flags) &= ~AuRen_##name; } while (0)

#ifndef CONFIG_AUFS_DIRREN
#undef AuRen_DIRREN
#define AuRen_DIRREN		0
#endif

struct au_ren_args {
	struct {
		struct dentry *dentry, *h_dentry, *parent, *h_parent,
			*wh_dentry;
		struct inode *dir, *inode;
		struct au_hinode *hdir, *hinode;
		struct au_dtime dt[AuParentChild];
		aufs_bindex_t btop, bdiropq;
	} sd[AuSrcDst];

#define src_dentry	sd[AuSRC].dentry
#define src_dir		sd[AuSRC].dir
#define src_inode	sd[AuSRC].inode
#define src_h_dentry	sd[AuSRC].h_dentry
#define src_parent	sd[AuSRC].parent
#define src_h_parent	sd[AuSRC].h_parent
#define src_wh_dentry	sd[AuSRC].wh_dentry
#define src_hdir	sd[AuSRC].hdir
#define src_hinode	sd[AuSRC].hinode
#define src_h_dir	sd[AuSRC].hdir->hi_inode
#define src_dt		sd[AuSRC].dt
#define src_btop	sd[AuSRC].btop
#define src_bdiropq	sd[AuSRC].bdiropq

#define dst_dentry	sd[AuDST].dentry
#define dst_dir		sd[AuDST].dir
#define dst_inode	sd[AuDST].inode
#define dst_h_dentry	sd[AuDST].h_dentry
#define dst_parent	sd[AuDST].parent
#define dst_h_parent	sd[AuDST].h_parent
#define dst_wh_dentry	sd[AuDST].wh_dentry
#define dst_hdir	sd[AuDST].hdir
#define dst_hinode	sd[AuDST].hinode
#define dst_h_dir	sd[AuDST].hdir->hi_inode
#define dst_dt		sd[AuDST].dt
#define dst_btop	sd[AuDST].btop
#define dst_bdiropq	sd[AuDST].bdiropq

	struct dentry *h_trap;
	struct au_branch *br;
	struct path h_path;
	struct au_nhash whlist;
	aufs_bindex_t btgt, src_bwh;

	struct {
		unsigned short auren_flags;
		unsigned char flags;	/* syscall parameter */
		unsigned char exchange;
	} __packed;

	struct au_whtmp_rmdir *thargs;
	struct dentry *h_dst;
	struct au_hinode *h_root;
};

/* ---------------------------------------------------------------------- */

/*
 * functions for reverting.
 * when an error happened in a single rename systemcall, we should revert
 * everything as if nothing happened.
 * we don't need to revert the copied-up/down the parent dir since they are
 * harmless.
 */

#define RevertFailure(fmt, ...) do { \
	AuIOErr("revert failure: " fmt " (%d, %d)\n", \
		##__VA_ARGS__, err, rerr); \
	err = -EIO; \
} while (0)

static void au_ren_do_rev_diropq(int err, struct au_ren_args *a, int idx)
{
	int rerr;
	struct dentry *d;
#define src_or_dst(member) a->sd[idx].member

	d = src_or_dst(dentry); /* {src,dst}_dentry */
	au_hn_inode_lock_nested(src_or_dst(hinode), AuLsc_I_CHILD);
	rerr = au_diropq_remove(d, a->btgt);
	au_hn_inode_unlock(src_or_dst(hinode));
	au_set_dbdiropq(d, src_or_dst(bdiropq));
	if (rerr)
		RevertFailure("remove diropq %pd", d);

#undef src_or_dst_
}

static void au_ren_rev_diropq(int err, struct au_ren_args *a)
{
	if (au_ftest_ren(a->auren_flags, DIROPQ_SRC))
		au_ren_do_rev_diropq(err, a, AuSRC);
	if (au_ftest_ren(a->auren_flags, DIROPQ_DST))
		au_ren_do_rev_diropq(err, a, AuDST);
}

static void au_ren_rev_rename(int err, struct au_ren_args *a)
{
	int rerr;
	struct inode *delegated;

	a->h_path.dentry = vfsub_lkup_one(&a->src_dentry->d_name,
					  a->src_h_parent);
	rerr = PTR_ERR(a->h_path.dentry);
	if (IS_ERR(a->h_path.dentry)) {
		RevertFailure("lkup one %pd", a->src_dentry);
		return;
	}

	delegated = NULL;
	rerr = vfsub_rename(a->dst_h_dir,
			    au_h_dptr(a->src_dentry, a->btgt),
			    a->src_h_dir, &a->h_path, &delegated, a->flags);
	if (unlikely(rerr == -EWOULDBLOCK)) {
		pr_warn("cannot retry for NFSv4 delegation"
			" for an internal rename\n");
		iput(delegated);
	}
	d_drop(a->h_path.dentry);
	dput(a->h_path.dentry);
	/* au_set_h_dptr(a->src_dentry, a->btgt, NULL); */
	if (rerr)
		RevertFailure("rename %pd", a->src_dentry);
}

static void au_ren_rev_whtmp(int err, struct au_ren_args *a)
{
	int rerr;
	struct inode *delegated;

	a->h_path.dentry = vfsub_lkup_one(&a->dst_dentry->d_name,
					  a->dst_h_parent);
	rerr = PTR_ERR(a->h_path.dentry);
	if (IS_ERR(a->h_path.dentry)) {
		RevertFailure("lkup one %pd", a->dst_dentry);
		return;
	}
	if (d_is_positive(a->h_path.dentry)) {
		d_drop(a->h_path.dentry);
		dput(a->h_path.dentry);
		return;
	}

	delegated = NULL;
	rerr = vfsub_rename(a->dst_h_dir, a->h_dst, a->dst_h_dir, &a->h_path,
			    &delegated, a->flags);
	if (unlikely(rerr == -EWOULDBLOCK)) {
		pr_warn("cannot retry for NFSv4 delegation"
			" for an internal rename\n");
		iput(delegated);
	}
	d_drop(a->h_path.dentry);
	dput(a->h_path.dentry);
	if (!rerr)
		au_set_h_dptr(a->dst_dentry, a->btgt, dget(a->h_dst));
	else
		RevertFailure("rename %pd", a->h_dst);
}

static void au_ren_rev_whsrc(int err, struct au_ren_args *a)
{
	int rerr;

	a->h_path.dentry = a->src_wh_dentry;
	rerr = au_wh_unlink_dentry(a->src_h_dir, &a->h_path, a->src_dentry);
	au_set_dbwh(a->src_dentry, a->src_bwh);
	if (rerr)
		RevertFailure("unlink %pd", a->src_wh_dentry);
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
	struct inode *delegated;

	d = a->src_dentry;
	if (au_dbtop(d) == a->btgt) {
		a->h_path.dentry = a->dst_h_dentry;
		AuDebugOn(au_dbtop(d) != a->btgt);
		delegated = NULL;
		err = vfsub_rename(a->src_h_dir, au_h_dptr(d, a->btgt),
				   a->dst_h_dir, &a->h_path, &delegated,
				   a->flags);
		if (unlikely(err == -EWOULDBLOCK)) {
			pr_warn("cannot retry for NFSv4 delegation"
				" for an internal rename\n");
			iput(delegated);
		}
	} else
		BUG();

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
			pr_warn("failed removing whtmp dir %pd (%d), "
				"ignored.\n", a->h_dst, err);
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
static int au_ren_do_diropq(struct au_ren_args *a, int idx)
{
	int err;
	struct dentry *d, *diropq;
#define src_or_dst(member) a->sd[idx].member

	err = 0;
	d = src_or_dst(dentry); /* {src,dst}_dentry */
	src_or_dst(bdiropq) = au_dbdiropq(d);
	src_or_dst(hinode) = au_hi(src_or_dst(inode), a->btgt);
	au_hn_inode_lock_nested(src_or_dst(hinode), AuLsc_I_CHILD);
	diropq = au_diropq_create(d, a->btgt);
	au_hn_inode_unlock(src_or_dst(hinode));
	if (IS_ERR(diropq))
		err = PTR_ERR(diropq);
	else
		dput(diropq);

#undef src_or_dst_
	return err;
}

static int au_ren_diropq(struct au_ren_args *a)
{
	int err;
	unsigned char always;
	struct dentry *d;

	err = 0;
	d = a->dst_dentry; /* already renamed on the branch */
	always = !!au_opt_test(au_mntflags(d->d_sb), ALWAYS_DIROPQ);
	if (au_ftest_ren(a->auren_flags, ISDIR_SRC)
	    && !au_ftest_ren(a->auren_flags, DIRREN)
	    && a->btgt != au_dbdiropq(a->src_dentry)
	    && (a->dst_wh_dentry
		|| a->btgt <= au_dbdiropq(d)
		/* hide the lower to keep xino */
		/* the lowers may not be a dir, but we hide them anyway */
		|| a->btgt < au_dbbot(d)
		|| always)) {
		AuDbg("here\n");
		err = au_ren_do_diropq(a, AuSRC);
		if (unlikely(err))
			goto out;
		au_fset_ren(a->auren_flags, DIROPQ_SRC);
	}
	if (!a->exchange)
		goto out; /* success */

	d = a->src_dentry; /* already renamed on the branch */
	if (au_ftest_ren(a->auren_flags, ISDIR_DST)
	    && a->btgt != au_dbdiropq(a->dst_dentry)
	    && (a->btgt < au_dbdiropq(d)
		|| a->btgt < au_dbbot(d)
		|| always)) {
		AuDbgDentry(a->src_dentry);
		AuDbgDentry(a->dst_dentry);
		err = au_ren_do_diropq(a, AuDST);
		if (unlikely(err))
			goto out_rev_src;
		au_fset_ren(a->auren_flags, DIROPQ_DST);
	}
	goto out; /* success */

out_rev_src:
	AuDbg("err %d, reverting src\n", err);
	au_ren_rev_diropq(err, a);
out:
	return err;
}

static int do_rename(struct au_ren_args *a)
{
	int err;
	struct dentry *d, *h_d;

	if (!a->exchange) {
		/* prepare workqueue args for asynchronous rmdir */
		h_d = a->dst_h_dentry;
		if (au_ftest_ren(a->auren_flags, ISDIR_DST)
		    /* && !au_ftest_ren(a->auren_flags, DIRREN) */
		    && d_is_positive(h_d)) {
			err = -ENOMEM;
			a->thargs = au_whtmp_rmdir_alloc(a->src_dentry->d_sb,
							 GFP_NOFS);
			if (unlikely(!a->thargs))
				goto out;
			a->h_dst = dget(h_d);
		}

		/* create whiteout for src_dentry */
		if (au_ftest_ren(a->auren_flags, WHSRC)) {
			a->src_bwh = au_dbwh(a->src_dentry);
			AuDebugOn(a->src_bwh >= 0);
			a->src_wh_dentry = au_wh_create(a->src_dentry, a->btgt,
							a->src_h_parent);
			err = PTR_ERR(a->src_wh_dentry);
			if (IS_ERR(a->src_wh_dentry))
				goto out_thargs;
		}

		/* lookup whiteout for dentry */
		if (au_ftest_ren(a->auren_flags, WHDST)) {
			h_d = au_wh_lkup(a->dst_h_parent,
					 &a->dst_dentry->d_name, a->br);
			err = PTR_ERR(h_d);
			if (IS_ERR(h_d))
				goto out_whsrc;
			if (d_is_negative(h_d))
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
			err = au_lkup_neg(d, a->btgt, /*wh*/0);
			if (unlikely(err))
				goto out_whtmp;
			a->dst_h_dentry = au_h_dptr(d, a->btgt);
		}
	}

	BUG_ON(d_is_positive(a->dst_h_dentry) && a->src_btop != a->btgt);
#if 0
	BUG_ON(!au_ftest_ren(a->auren_flags, DIRREN)
	       && d_is_positive(a->dst_h_dentry)
	       && a->src_btop != a->btgt);
#endif

	/* rename by vfs_rename or cpup */
	err = au_ren_or_cpup(a);
	if (unlikely(err))
		/* leave the copied-up one */
		goto out_whtmp;

	/* make dir opaque */
	err = au_ren_diropq(a);
	if (unlikely(err))
		goto out_rename;

	/* update target timestamps */
	if (a->exchange) {
		AuDebugOn(au_dbtop(a->dst_dentry) != a->btgt);
		a->h_path.dentry = au_h_dptr(a->dst_dentry, a->btgt);
		vfsub_update_h_iattr(&a->h_path, /*did*/NULL); /*ignore*/
		a->dst_inode->i_ctime = d_inode(a->h_path.dentry)->i_ctime;
	}
	AuDebugOn(au_dbtop(a->src_dentry) != a->btgt);
	a->h_path.dentry = au_h_dptr(a->src_dentry, a->btgt);
	vfsub_update_h_iattr(&a->h_path, /*did*/NULL); /*ignore*/
	a->src_inode->i_ctime = d_inode(a->h_path.dentry)->i_ctime;

	if (!a->exchange) {
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

		au_fhsm_wrote(a->src_dentry->d_sb, a->btgt, /*force*/0);
	}
	err = 0;
	goto out_success;

out_diropq:
	au_ren_rev_diropq(err, a);
out_rename:
	au_ren_rev_rename(err, a);
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
 * test if @a->src_dentry dir can be rename source or not.
 * if it can, return 0.
 * success means,
 * - it is a logically empty dir.
 * - or, it exists on writable branch and has no children including whiteouts
 *   on the lower branch unless DIRREN is on.
 */
static int may_rename_srcdir(struct au_ren_args *a)
{
	int err;
	unsigned int rdhash;
	aufs_bindex_t btop, btgt;
	struct dentry *dentry;
	struct super_block *sb;
	struct au_sbinfo *sbinfo;

	dentry = a->src_dentry;
	sb = dentry->d_sb;
	sbinfo = au_sbi(sb);
	if (au_opt_test(sbinfo->si_mntflags, DIRREN))
		au_fset_ren(a->auren_flags, DIRREN);

	btgt = a->btgt;
	btop = au_dbtop(dentry);
	if (btop != btgt) {
		struct au_nhash whlist;

		SiMustAnyLock(sb);
		rdhash = sbinfo->si_rdhash;
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

	if (btop == au_dbtaildir(dentry))
		return 0; /* success */

	err = au_test_empty_lower(dentry);

out:
	if (err == -ENOTEMPTY) {
		if (au_ftest_ren(a->auren_flags, DIRREN)) {
			err = 0;
		} else {
			AuWarn1("renaming dir who has child(ren) on multiple "
				"branches, is not supported\n");
			err = -EXDEV;
		}
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
	if (au_ftest_ren(a->auren_flags, ISDIR_DST) && a->dst_inode) {
		rdhash = au_sbi(d->d_sb)->si_rdhash;
		if (!rdhash)
			rdhash = au_rdhash_est(au_dir_size(/*file*/NULL, d));
		err = au_nhash_alloc(&a->whlist, rdhash, GFP_NOFS);
		if (unlikely(err))
			goto out;

		if (!a->exchange) {
			au_set_dbtop(d, a->dst_btop);
			err = may_rename_dstdir(d, &a->whlist);
			au_set_dbtop(d, a->btgt);
		} else
			err = may_rename_srcdir(a);
	}
	a->dst_h_dentry = au_h_dptr(d, au_dbtop(d));
	if (unlikely(err))
		goto out;

	d = a->src_dentry;
	a->src_h_dentry = au_h_dptr(d, au_dbtop(d));
	if (au_ftest_ren(a->auren_flags, ISDIR_SRC)) {
		err = may_rename_srcdir(a);
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

	if (a->src_btop == a->btgt) {
		err = au_may_del(a->src_dentry, a->btgt, a->src_h_parent,
				 au_ftest_ren(a->auren_flags, ISDIR_SRC));
		if (unlikely(err))
			goto out;
		err = -EINVAL;
		if (unlikely(a->src_h_dentry == a->h_trap))
			goto out;
	}

	err = 0;
	if (a->dst_btop != a->btgt)
		goto out;

	err = -ENOTEMPTY;
	if (unlikely(a->dst_h_dentry == a->h_trap))
		goto out;

	err = -EIO;
	isdir = !!au_ftest_ren(a->auren_flags, ISDIR_DST);
	if (d_really_is_negative(a->dst_dentry)) {
		if (d_is_negative(a->dst_h_dentry))
			err = au_may_add(a->dst_dentry, a->btgt,
					 a->dst_h_parent, isdir);
	} else {
		if (unlikely(d_is_negative(a->dst_h_dentry)))
			goto out;
		h_inode = d_inode(a->dst_h_dentry);
		if (h_inode->i_nlink)
			err = au_may_del(a->dst_dentry, a->btgt,
					 a->dst_h_parent, isdir);
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
	vfsub_unlock_rename(a->src_h_parent, a->src_hdir,
			    a->dst_h_parent, a->dst_hdir);
	if (au_ftest_ren(a->auren_flags, DIRREN)
	    && a->h_root)
		au_hn_inode_unlock(a->h_root);
	if (au_ftest_ren(a->auren_flags, MNT_WRITE))
		vfsub_mnt_drop_write(au_br_mnt(a->br));
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

	err = vfsub_mnt_want_write(au_br_mnt(a->br));
	if (unlikely(err))
		goto out;
	au_fset_ren(a->auren_flags, MNT_WRITE);
	if (au_ftest_ren(a->auren_flags, DIRREN)) {
		struct dentry *root;
		struct inode *dir;

		/*
		 * sbinfo is already locked, so this ii_read_lock is
		 * unnecessary. but our debugging feature checks it.
		 */
		root = a->src_inode->i_sb->s_root;
		if (root != a->src_parent && root != a->dst_parent) {
			dir = d_inode(root);
			ii_read_lock_parent3(dir);
			a->h_root = au_hi(dir, a->btgt);
			ii_read_unlock(dir);
			au_hn_inode_lock_nested(a->h_root, AuLsc_I_PARENT3);
		}
	}
	a->h_trap = vfsub_lock_rename(a->src_h_parent, a->src_hdir,
				      a->dst_h_parent, a->dst_hdir);
	udba = au_opt_udba(a->src_dentry->d_sb);
	if (unlikely(a->src_hdir->hi_inode != d_inode(a->src_h_parent)
		     || a->dst_hdir->hi_inode != d_inode(a->dst_h_parent)))
		err = au_busy_or_stale();
	if (!err && au_dbtop(a->src_dentry) == a->btgt)
		err = au_h_verify(a->src_h_dentry, udba,
				  d_inode(a->src_h_parent), a->src_h_parent,
				  a->br);
	if (!err && au_dbtop(a->dst_dentry) == a->btgt)
		err = au_h_verify(a->dst_h_dentry, udba,
				  d_inode(a->dst_h_parent), a->dst_h_parent,
				  a->br);
	if (!err)
		goto out; /* success */

	err = au_busy_or_stale();
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
	if (au_ftest_ren(a->auren_flags, ISDIR_SRC)) {
		/* is this updating defined in POSIX? */
		au_cpup_attr_timesizes(a->src_inode);
		au_cpup_attr_nlink(dir, /*force*/1);
	}
	au_dir_ts(dir, a->btgt);

	if (a->exchange) {
		dir = a->src_dir;
		dir->i_version++;
		if (au_ftest_ren(a->auren_flags, ISDIR_DST)) {
			/* is this updating defined in POSIX? */
			au_cpup_attr_timesizes(a->dst_inode);
			au_cpup_attr_nlink(dir, /*force*/1);
		}
		au_dir_ts(dir, a->btgt);
	}

	if (au_ftest_ren(a->auren_flags, ISSAMEDIR))
		return;

	dir = a->src_dir;
	dir->i_version++;
	if (au_ftest_ren(a->auren_flags, ISDIR_SRC))
		au_cpup_attr_nlink(dir, /*force*/1);
	au_dir_ts(dir, a->btgt);
}

static void au_ren_refresh(struct au_ren_args *a)
{
	aufs_bindex_t bbot, bindex;
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
		if (!a->exchange) {
			if (!au_ftest_ren(a->auren_flags, ISDIR_DST))
				vfsub_drop_nlink(i);
			else {
				vfsub_dead_dir(i);
				au_cpup_attr_timesizes(i);
			}
			au_update_dbrange(d, /*do_put_zero*/1);
		} else
			au_cpup_attr_nlink(i, /*force*/1);
	} else {
		bbot = a->btgt;
		for (bindex = au_dbtop(d); bindex < bbot; bindex++)
			au_set_h_dptr(d, bindex, NULL);
		bbot = au_dbbot(d);
		for (bindex = a->btgt + 1; bindex <= bbot; bindex++)
			au_set_h_dptr(d, bindex, NULL);
		au_update_dbrange(d, /*do_put_zero*/0);
	}

	if (a->exchange
	    || au_ftest_ren(a->auren_flags, DIRREN)) {
		d_drop(a->src_dentry);
		if (au_ftest_ren(a->auren_flags, DIRREN))
			au_set_dbwh(a->src_dentry, -1);
		return;
	}

	d = a->src_dentry;
	au_set_dbwh(d, -1);
	bbot = au_dbbot(d);
	for (bindex = a->btgt + 1; bindex <= bbot; bindex++) {
		h_d = au_h_dptr(d, bindex);
		if (h_d)
			au_set_h_dptr(d, bindex, NULL);
	}
	au_set_dbbot(d, a->btgt);

	sb = d->d_sb;
	i = a->src_inode;
	if (au_opt_test(au_mntflags(sb), PLINK) && au_plink_test(i))
		return; /* success */

	bbot = au_ibbot(i);
	for (bindex = a->btgt + 1; bindex <= bbot; bindex++) {
		h_i = au_h_iptr(i, bindex);
		if (h_i) {
			au_xino_write(sb, bindex, h_i->i_ino, /*ino*/0);
			/* ignore this error */
			au_set_h_iptr(i, bindex, NULL, 0);
		}
	}
	au_set_ibbot(i, a->btgt);
}

/* ---------------------------------------------------------------------- */

/* mainly for link(2) and rename(2) */
int au_wbr(struct dentry *dentry, aufs_bindex_t btgt)
{
	aufs_bindex_t bdiropq, bwh;
	struct dentry *parent;
	struct au_branch *br;

	parent = dentry->d_parent;
	IMustLock(d_inode(parent)); /* dir is locked */

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

/* sets src_btop, dst_btop and btgt */
static int au_ren_wbr(struct au_ren_args *a)
{
	int err;
	struct au_wr_dir_args wr_dir_args = {
		/* .force_btgt	= -1, */
		.flags		= AuWrDir_ADD_ENTRY
	};

	a->src_btop = au_dbtop(a->src_dentry);
	a->dst_btop = au_dbtop(a->dst_dentry);
	if (au_ftest_ren(a->auren_flags, ISDIR_SRC)
	    || au_ftest_ren(a->auren_flags, ISDIR_DST))
		au_fset_wrdir(wr_dir_args.flags, ISDIR);
	wr_dir_args.force_btgt = a->src_btop;
	if (a->dst_inode && a->dst_btop < a->src_btop)
		wr_dir_args.force_btgt = a->dst_btop;
	wr_dir_args.force_btgt = au_wbr(a->dst_dentry, wr_dir_args.force_btgt);
	err = au_wr_dir(a->dst_dentry, a->src_dentry, &wr_dir_args);
	a->btgt = err;
	if (a->exchange)
		au_update_dbtop(a->dst_dentry);

	return err;
}

static void au_ren_dt(struct au_ren_args *a)
{
	a->h_path.dentry = a->src_h_parent;
	au_dtime_store(a->src_dt + AuPARENT, a->src_parent, &a->h_path);
	if (!au_ftest_ren(a->auren_flags, ISSAMEDIR)) {
		a->h_path.dentry = a->dst_h_parent;
		au_dtime_store(a->dst_dt + AuPARENT, a->dst_parent, &a->h_path);
	}

	au_fclr_ren(a->auren_flags, DT_DSTDIR);
	if (!au_ftest_ren(a->auren_flags, ISDIR_SRC)
	    && !a->exchange)
		return;

	a->h_path.dentry = a->src_h_dentry;
	au_dtime_store(a->src_dt + AuCHILD, a->src_dentry, &a->h_path);
	if (d_is_positive(a->dst_h_dentry)) {
		au_fset_ren(a->auren_flags, DT_DSTDIR);
		a->h_path.dentry = a->dst_h_dentry;
		au_dtime_store(a->dst_dt + AuCHILD, a->dst_dentry, &a->h_path);
	}
}

static void au_ren_rev_dt(int err, struct au_ren_args *a)
{
	struct dentry *h_d;
	struct inode *h_inode;

	au_dtime_revert(a->src_dt + AuPARENT);
	if (!au_ftest_ren(a->auren_flags, ISSAMEDIR))
		au_dtime_revert(a->dst_dt + AuPARENT);

	if (au_ftest_ren(a->auren_flags, ISDIR_SRC) && err != -EIO) {
		h_d = a->src_dt[AuCHILD].dt_h_path.dentry;
		h_inode = d_inode(h_d);
		inode_lock_nested(h_inode, AuLsc_I_CHILD);
		au_dtime_revert(a->src_dt + AuCHILD);
		inode_unlock(h_inode);

		if (au_ftest_ren(a->auren_flags, DT_DSTDIR)) {
			h_d = a->dst_dt[AuCHILD].dt_h_path.dentry;
			h_inode = d_inode(h_d);
			inode_lock_nested(h_inode, AuLsc_I_CHILD);
			au_dtime_revert(a->dst_dt + AuCHILD);
			inode_unlock(h_inode);
		}
	}
}

/* ---------------------------------------------------------------------- */

int aufs_rename(struct inode *_src_dir, struct dentry *_src_dentry,
		struct inode *_dst_dir, struct dentry *_dst_dentry,
		unsigned int _flags)
{
	int err, lock_flags;
	void *rev;
	/* reduce stack space */
	struct au_ren_args *a;
	struct au_pin pin;

	AuDbg("%pd, %pd, 0x%x\n", _src_dentry, _dst_dentry, _flags);
	IMustLock(_src_dir);
	IMustLock(_dst_dir);

	err = -EINVAL;
	if (unlikely(_flags & RENAME_WHITEOUT))
		goto out;

	err = -ENOMEM;
	BUILD_BUG_ON(sizeof(*a) > PAGE_SIZE);
	a = kzalloc(sizeof(*a), GFP_NOFS);
	if (unlikely(!a))
		goto out;

	a->flags = _flags;
	a->exchange = _flags & RENAME_EXCHANGE;
	a->src_dir = _src_dir;
	a->src_dentry = _src_dentry;
	a->src_inode = NULL;
	if (d_really_is_positive(a->src_dentry))
		a->src_inode = d_inode(a->src_dentry);
	a->src_parent = a->src_dentry->d_parent; /* dir inode is locked */
	a->dst_dir = _dst_dir;
	a->dst_dentry = _dst_dentry;
	a->dst_inode = NULL;
	if (d_really_is_positive(a->dst_dentry))
		a->dst_inode = d_inode(a->dst_dentry);
	a->dst_parent = a->dst_dentry->d_parent; /* dir inode is locked */
	if (a->dst_inode) {
		/*
		 * if EXCHANGE && src is non-dir && dst is dir,
		 * dst is not locked.
		 */
		/* IMustLock(a->dst_inode); */
		au_igrab(a->dst_inode);
	}

	err = -ENOTDIR;
	lock_flags = AuLock_FLUSH | AuLock_NOPLM | AuLock_GEN;
	if (d_is_dir(a->src_dentry)) {
		au_fset_ren(a->auren_flags, ISDIR_SRC);
		if (unlikely(!a->exchange
			     && d_really_is_positive(a->dst_dentry)
			     && !d_is_dir(a->dst_dentry)))
			goto out_free;
		lock_flags |= AuLock_DIRS;
	}
	if (a->dst_inode && d_is_dir(a->dst_dentry)) {
		au_fset_ren(a->auren_flags, ISDIR_DST);
		if (unlikely(!a->exchange
			     && d_really_is_positive(a->src_dentry)
			     && !d_is_dir(a->src_dentry)))
			goto out_free;
		lock_flags |= AuLock_DIRS;
	}
	err = aufs_read_and_write_lock2(a->dst_dentry, a->src_dentry,
					lock_flags);
	if (unlikely(err))
		goto out_free;

	err = au_d_hashed_positive(a->src_dentry);
	if (unlikely(err))
		goto out_unlock;
	err = -ENOENT;
	if (a->dst_inode) {
		/*
		 * If it is a dir, VFS unhash it before this
		 * function. It means we cannot rely upon d_unhashed().
		 */
		if (unlikely(!a->dst_inode->i_nlink))
			goto out_unlock;
		if (!au_ftest_ren(a->auren_flags, ISDIR_DST)) {
			err = au_d_hashed_positive(a->dst_dentry);
			if (unlikely(err && !a->exchange))
				goto out_unlock;
		} else if (unlikely(IS_DEADDIR(a->dst_inode)))
			goto out_unlock;
	} else if (unlikely(d_unhashed(a->dst_dentry)))
		goto out_unlock;

	/*
	 * is it possible?
	 * yes, it happened (in linux-3.3-rcN) but I don't know why.
	 * there may exist a problem somewhere else.
	 */
	err = -EINVAL;
	if (unlikely(d_inode(a->dst_parent) == d_inode(a->src_dentry)))
		goto out_unlock;

	au_fset_ren(a->auren_flags, ISSAMEDIR); /* temporary */
	di_write_lock_parent(a->dst_parent);

	/* which branch we process */
	err = au_ren_wbr(a);
	if (unlikely(err < 0))
		goto out_parent;
	a->br = au_sbr(a->dst_dentry->d_sb, a->btgt);
	a->h_path.mnt = au_br_mnt(a->br);

	/* are they available to be renamed */
	err = au_ren_may_dir(a);
	if (unlikely(err))
		goto out_children;

	/* prepare the writable parent dir on the same branch */
	if (a->dst_btop == a->btgt) {
		au_fset_ren(a->auren_flags, WHDST);
	} else {
		err = au_cpup_dirs(a->dst_dentry, a->btgt);
		if (unlikely(err))
			goto out_children;
	}

	err = 0;
	if (!a->exchange) {
		if (a->src_dir != a->dst_dir) {
			/*
			 * this temporary unlock is safe,
			 * because both dir->i_mutex are locked.
			 */
			di_write_unlock(a->dst_parent);
			di_write_lock_parent(a->src_parent);
			err = au_wr_dir_need_wh(a->src_dentry,
						au_ftest_ren(a->auren_flags,
							     ISDIR_SRC),
						&a->btgt);
			di_write_unlock(a->src_parent);
			di_write_lock2_parent(a->src_parent, a->dst_parent,
					      /*isdir*/1);
			au_fclr_ren(a->auren_flags, ISSAMEDIR);
		} else
			err = au_wr_dir_need_wh(a->src_dentry,
						au_ftest_ren(a->auren_flags,
							     ISDIR_SRC),
						&a->btgt);
	}
	if (unlikely(err < 0))
		goto out_children;
	if (err)
		au_fset_ren(a->auren_flags, WHSRC);

	/* cpup src */
	if (a->src_btop != a->btgt) {
		err = au_pin(&pin, a->src_dentry, a->btgt,
			     au_opt_udba(a->src_dentry->d_sb),
			     AuPin_DI_LOCKED | AuPin_MNT_WRITE);
		if (!err) {
			struct au_cp_generic cpg = {
				.dentry	= a->src_dentry,
				.bdst	= a->btgt,
				.bsrc	= a->src_btop,
				.len	= -1,
				.pin	= &pin,
				.flags	= AuCpup_DTIME | AuCpup_HOPEN
			};
			AuDebugOn(au_dbtop(a->src_dentry) != a->src_btop);
			err = au_sio_cpup_simple(&cpg);
			au_unpin(&pin);
		}
		if (unlikely(err))
			goto out_children;
		a->src_btop = a->btgt;
		a->src_h_dentry = au_h_dptr(a->src_dentry, a->btgt);
		if (!a->exchange)
			au_fset_ren(a->auren_flags, WHSRC);
	}

	/* cpup dst */
	if (a->exchange && a->dst_inode
	    && a->dst_btop != a->btgt) {
		err = au_pin(&pin, a->dst_dentry, a->btgt,
			     au_opt_udba(a->dst_dentry->d_sb),
			     AuPin_DI_LOCKED | AuPin_MNT_WRITE);
		if (!err) {
			struct au_cp_generic cpg = {
				.dentry	= a->dst_dentry,
				.bdst	= a->btgt,
				.bsrc	= a->dst_btop,
				.len	= -1,
				.pin	= &pin,
				.flags	= AuCpup_DTIME | AuCpup_HOPEN
			};
			err = au_sio_cpup_simple(&cpg);
			au_unpin(&pin);
		}
		if (unlikely(err))
			goto out_children;
		a->dst_btop = a->btgt;
		a->dst_h_dentry = au_h_dptr(a->dst_dentry, a->btgt);
	}

	/* lock them all */
	err = au_ren_lock(a);
	if (unlikely(err))
		/* leave the copied-up one */
		goto out_children;

	if (!a->exchange) {
		if (!au_opt_test(au_mntflags(a->dst_dir->i_sb), UDBA_NONE))
			err = au_may_ren(a);
		else if (unlikely(a->dst_dentry->d_name.len > AUFS_MAX_NAMELEN))
			err = -ENAMETOOLONG;
		if (unlikely(err))
			goto out_hdir;
	}

	/* store timestamps to be revertible */
	au_ren_dt(a);

	/* store dirren info */
	if (au_ftest_ren(a->auren_flags, DIRREN)) {
		err = au_dr_rename(a->src_dentry, a->btgt,
				   &a->dst_dentry->d_name, &rev);
		AuTraceErr(err);
		if (unlikely(err))
			goto out_dt;
	}

	/* here we go */
	err = do_rename(a);
	if (unlikely(err))
		goto out_dirren;

	if (au_ftest_ren(a->auren_flags, DIRREN))
		au_dr_rename_fin(a->src_dentry, a->btgt, rev);

	/* update dir attributes */
	au_ren_refresh_dir(a);

	/* dput/iput all lower dentries */
	au_ren_refresh(a);

	goto out_hdir; /* success */

out_dirren:
	if (au_ftest_ren(a->auren_flags, DIRREN))
		au_dr_rename_rev(a->src_dentry, a->btgt, rev);
out_dt:
	au_ren_rev_dt(err, a);
out_hdir:
	au_ren_unlock(a);
out_children:
	au_nhash_wh_free(&a->whlist);
	if (err && a->dst_inode && a->dst_btop != a->btgt) {
		AuDbg("btop %d, btgt %d\n", a->dst_btop, a->btgt);
		au_set_h_dptr(a->dst_dentry, a->btgt, NULL);
		au_set_dbtop(a->dst_dentry, a->dst_btop);
	}
out_parent:
	if (!err) {
		if (d_unhashed(a->src_dentry))
			au_fset_ren(a->auren_flags, DROPPED_SRC);
		if (d_unhashed(a->dst_dentry))
			au_fset_ren(a->auren_flags, DROPPED_DST);
		if (!a->exchange)
			d_move(a->src_dentry, a->dst_dentry);
		else {
			d_exchange(a->src_dentry, a->dst_dentry);
			if (au_ftest_ren(a->auren_flags, DROPPED_DST))
				d_drop(a->dst_dentry);
		}
		if (au_ftest_ren(a->auren_flags, DROPPED_SRC))
			d_drop(a->src_dentry);
	} else {
		au_update_dbtop(a->dst_dentry);
		if (!a->dst_inode)
			d_drop(a->dst_dentry);
	}
	if (au_ftest_ren(a->auren_flags, ISSAMEDIR))
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
