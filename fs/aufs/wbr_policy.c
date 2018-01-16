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
 * policies for selecting one among multiple writable branches
 */

#include <linux/statfs.h>
#include "aufs.h"

/* subset of cpup_attr() */
static noinline_for_stack
int au_cpdown_attr(struct path *h_path, struct dentry *h_src)
{
	int err, sbits;
	struct iattr ia;
	struct inode *h_isrc;

	h_isrc = d_inode(h_src);
	ia.ia_valid = ATTR_FORCE | ATTR_MODE | ATTR_UID | ATTR_GID;
	ia.ia_mode = h_isrc->i_mode;
	ia.ia_uid = h_isrc->i_uid;
	ia.ia_gid = h_isrc->i_gid;
	sbits = !!(ia.ia_mode & (S_ISUID | S_ISGID));
	au_cpup_attr_flags(d_inode(h_path->dentry), h_isrc->i_flags);
	/* no delegation since it is just created */
	err = vfsub_sio_notify_change(h_path, &ia, /*delegated*/NULL);

	/* is this nfs only? */
	if (!err && sbits && au_test_nfs(h_path->dentry->d_sb)) {
		ia.ia_valid = ATTR_FORCE | ATTR_MODE;
		ia.ia_mode = h_isrc->i_mode;
		err = vfsub_sio_notify_change(h_path, &ia, /*delegated*/NULL);
	}

	return err;
}

#define AuCpdown_PARENT_OPQ	1
#define AuCpdown_WHED		(1 << 1)
#define AuCpdown_MADE_DIR	(1 << 2)
#define AuCpdown_DIROPQ		(1 << 3)
#define au_ftest_cpdown(flags, name)	((flags) & AuCpdown_##name)
#define au_fset_cpdown(flags, name) \
	do { (flags) |= AuCpdown_##name; } while (0)
#define au_fclr_cpdown(flags, name) \
	do { (flags) &= ~AuCpdown_##name; } while (0)

static int au_cpdown_dir_opq(struct dentry *dentry, aufs_bindex_t bdst,
			     unsigned int *flags)
{
	int err;
	struct dentry *opq_dentry;

	opq_dentry = au_diropq_create(dentry, bdst);
	err = PTR_ERR(opq_dentry);
	if (IS_ERR(opq_dentry))
		goto out;
	dput(opq_dentry);
	au_fset_cpdown(*flags, DIROPQ);

out:
	return err;
}

static int au_cpdown_dir_wh(struct dentry *dentry, struct dentry *h_parent,
			    struct inode *dir, aufs_bindex_t bdst)
{
	int err;
	struct path h_path;
	struct au_branch *br;

	br = au_sbr(dentry->d_sb, bdst);
	h_path.dentry = au_wh_lkup(h_parent, &dentry->d_name, br);
	err = PTR_ERR(h_path.dentry);
	if (IS_ERR(h_path.dentry))
		goto out;

	err = 0;
	if (d_is_positive(h_path.dentry)) {
		h_path.mnt = au_br_mnt(br);
		err = au_wh_unlink_dentry(au_h_iptr(dir, bdst), &h_path,
					  dentry);
	}
	dput(h_path.dentry);

out:
	return err;
}

static int au_cpdown_dir(struct dentry *dentry, aufs_bindex_t bdst,
			 struct au_pin *pin,
			 struct dentry *h_parent, void *arg)
{
	int err, rerr;
	aufs_bindex_t bopq, btop;
	struct path h_path;
	struct dentry *parent;
	struct inode *h_dir, *h_inode, *inode, *dir;
	unsigned int *flags = arg;

	btop = au_dbtop(dentry);
	/* dentry is di-locked */
	parent = dget_parent(dentry);
	dir = d_inode(parent);
	h_dir = d_inode(h_parent);
	AuDebugOn(h_dir != au_h_iptr(dir, bdst));
	IMustLock(h_dir);

	err = au_lkup_neg(dentry, bdst, /*wh*/0);
	if (unlikely(err < 0))
		goto out;
	h_path.dentry = au_h_dptr(dentry, bdst);
	h_path.mnt = au_sbr_mnt(dentry->d_sb, bdst);
	err = vfsub_sio_mkdir(au_h_iptr(dir, bdst), &h_path,
			      S_IRWXU | S_IRUGO | S_IXUGO);
	if (unlikely(err))
		goto out_put;
	au_fset_cpdown(*flags, MADE_DIR);

	bopq = au_dbdiropq(dentry);
	au_fclr_cpdown(*flags, WHED);
	au_fclr_cpdown(*flags, DIROPQ);
	if (au_dbwh(dentry) == bdst)
		au_fset_cpdown(*flags, WHED);
	if (!au_ftest_cpdown(*flags, PARENT_OPQ) && bopq <= bdst)
		au_fset_cpdown(*flags, PARENT_OPQ);
	h_inode = d_inode(h_path.dentry);
	inode_lock_nested(h_inode, AuLsc_I_CHILD);
	if (au_ftest_cpdown(*flags, WHED)) {
		err = au_cpdown_dir_opq(dentry, bdst, flags);
		if (unlikely(err)) {
			inode_unlock(h_inode);
			goto out_dir;
		}
	}

	err = au_cpdown_attr(&h_path, au_h_dptr(dentry, btop));
	inode_unlock(h_inode);
	if (unlikely(err))
		goto out_opq;

	if (au_ftest_cpdown(*flags, WHED)) {
		err = au_cpdown_dir_wh(dentry, h_parent, dir, bdst);
		if (unlikely(err))
			goto out_opq;
	}

	inode = d_inode(dentry);
	if (au_ibbot(inode) < bdst)
		au_set_ibbot(inode, bdst);
	au_set_h_iptr(inode, bdst, au_igrab(h_inode),
		      au_hi_flags(inode, /*isdir*/1));
	au_fhsm_wrote(dentry->d_sb, bdst, /*force*/0);
	goto out; /* success */

	/* revert */
out_opq:
	if (au_ftest_cpdown(*flags, DIROPQ)) {
		inode_lock_nested(h_inode, AuLsc_I_CHILD);
		rerr = au_diropq_remove(dentry, bdst);
		inode_unlock(h_inode);
		if (unlikely(rerr)) {
			AuIOErr("failed removing diropq for %pd b%d (%d)\n",
				dentry, bdst, rerr);
			err = -EIO;
			goto out;
		}
	}
out_dir:
	if (au_ftest_cpdown(*flags, MADE_DIR)) {
		rerr = vfsub_sio_rmdir(au_h_iptr(dir, bdst), &h_path);
		if (unlikely(rerr)) {
			AuIOErr("failed removing %pd b%d (%d)\n",
				dentry, bdst, rerr);
			err = -EIO;
		}
	}
out_put:
	au_set_h_dptr(dentry, bdst, NULL);
	if (au_dbbot(dentry) == bdst)
		au_update_dbbot(dentry);
out:
	dput(parent);
	return err;
}

int au_cpdown_dirs(struct dentry *dentry, aufs_bindex_t bdst)
{
	int err;
	unsigned int flags;

	flags = 0;
	err = au_cp_dirs(dentry, bdst, au_cpdown_dir, &flags);

	return err;
}

/* ---------------------------------------------------------------------- */

/* policies for create */

int au_wbr_nonopq(struct dentry *dentry, aufs_bindex_t bindex)
{
	int err, i, j, ndentry;
	aufs_bindex_t bopq;
	struct au_dcsub_pages dpages;
	struct au_dpage *dpage;
	struct dentry **dentries, *parent, *d;

	err = au_dpages_init(&dpages, GFP_NOFS);
	if (unlikely(err))
		goto out;
	parent = dget_parent(dentry);
	err = au_dcsub_pages_rev_aufs(&dpages, parent, /*do_include*/0);
	if (unlikely(err))
		goto out_free;

	err = bindex;
	for (i = 0; i < dpages.ndpage; i++) {
		dpage = dpages.dpages + i;
		dentries = dpage->dentries;
		ndentry = dpage->ndentry;
		for (j = 0; j < ndentry; j++) {
			d = dentries[j];
			di_read_lock_parent2(d, !AuLock_IR);
			bopq = au_dbdiropq(d);
			di_read_unlock(d, !AuLock_IR);
			if (bopq >= 0 && bopq < err)
				err = bopq;
		}
	}

out_free:
	dput(parent);
	au_dpages_free(&dpages);
out:
	return err;
}

static int au_wbr_bu(struct super_block *sb, aufs_bindex_t bindex)
{
	for (; bindex >= 0; bindex--)
		if (!au_br_rdonly(au_sbr(sb, bindex)))
			return bindex;
	return -EROFS;
}

/* top down parent */
static int au_wbr_create_tdp(struct dentry *dentry,
			     unsigned int flags __maybe_unused)
{
	int err;
	aufs_bindex_t btop, bindex;
	struct super_block *sb;
	struct dentry *parent, *h_parent;

	sb = dentry->d_sb;
	btop = au_dbtop(dentry);
	err = btop;
	if (!au_br_rdonly(au_sbr(sb, btop)))
		goto out;

	err = -EROFS;
	parent = dget_parent(dentry);
	for (bindex = au_dbtop(parent); bindex < btop; bindex++) {
		h_parent = au_h_dptr(parent, bindex);
		if (!h_parent || d_is_negative(h_parent))
			continue;

		if (!au_br_rdonly(au_sbr(sb, bindex))) {
			err = bindex;
			break;
		}
	}
	dput(parent);

	/* bottom up here */
	if (unlikely(err < 0)) {
		err = au_wbr_bu(sb, btop - 1);
		if (err >= 0)
			err = au_wbr_nonopq(dentry, err);
	}

out:
	AuDbg("b%d\n", err);
	return err;
}

/* ---------------------------------------------------------------------- */

/* an exception for the policy other than tdp */
static int au_wbr_create_exp(struct dentry *dentry)
{
	int err;
	aufs_bindex_t bwh, bdiropq;
	struct dentry *parent;

	err = -1;
	bwh = au_dbwh(dentry);
	parent = dget_parent(dentry);
	bdiropq = au_dbdiropq(parent);
	if (bwh >= 0) {
		if (bdiropq >= 0)
			err = min(bdiropq, bwh);
		else
			err = bwh;
		AuDbg("%d\n", err);
	} else if (bdiropq >= 0) {
		err = bdiropq;
		AuDbg("%d\n", err);
	}
	dput(parent);

	if (err >= 0)
		err = au_wbr_nonopq(dentry, err);

	if (err >= 0 && au_br_rdonly(au_sbr(dentry->d_sb, err)))
		err = -1;

	AuDbg("%d\n", err);
	return err;
}

/* ---------------------------------------------------------------------- */

/* round robin */
static int au_wbr_create_init_rr(struct super_block *sb)
{
	int err;

	err = au_wbr_bu(sb, au_sbbot(sb));
	atomic_set(&au_sbi(sb)->si_wbr_rr_next, -err); /* less important */
	/* smp_mb(); */

	AuDbg("b%d\n", err);
	return err;
}

static int au_wbr_create_rr(struct dentry *dentry, unsigned int flags)
{
	int err, nbr;
	unsigned int u;
	aufs_bindex_t bindex, bbot;
	struct super_block *sb;
	atomic_t *next;

	err = au_wbr_create_exp(dentry);
	if (err >= 0)
		goto out;

	sb = dentry->d_sb;
	next = &au_sbi(sb)->si_wbr_rr_next;
	bbot = au_sbbot(sb);
	nbr = bbot + 1;
	for (bindex = 0; bindex <= bbot; bindex++) {
		if (!au_ftest_wbr(flags, DIR)) {
			err = atomic_dec_return(next) + 1;
			/* modulo for 0 is meaningless */
			if (unlikely(!err))
				err = atomic_dec_return(next) + 1;
		} else
			err = atomic_read(next);
		AuDbg("%d\n", err);
		u = err;
		err = u % nbr;
		AuDbg("%d\n", err);
		if (!au_br_rdonly(au_sbr(sb, err)))
			break;
		err = -EROFS;
	}

	if (err >= 0)
		err = au_wbr_nonopq(dentry, err);

out:
	AuDbg("%d\n", err);
	return err;
}

/* ---------------------------------------------------------------------- */

/* most free space */
static void au_mfs(struct dentry *dentry, struct dentry *parent)
{
	struct super_block *sb;
	struct au_branch *br;
	struct au_wbr_mfs *mfs;
	struct dentry *h_parent;
	aufs_bindex_t bindex, bbot;
	int err;
	unsigned long long b, bavail;
	struct path h_path;
	/* reduce the stack usage */
	struct kstatfs *st;

	st = kmalloc(sizeof(*st), GFP_NOFS);
	if (unlikely(!st)) {
		AuWarn1("failed updating mfs(%d), ignored\n", -ENOMEM);
		return;
	}

	bavail = 0;
	sb = dentry->d_sb;
	mfs = &au_sbi(sb)->si_wbr_mfs;
	MtxMustLock(&mfs->mfs_lock);
	mfs->mfs_bindex = -EROFS;
	mfs->mfsrr_bytes = 0;
	if (!parent) {
		bindex = 0;
		bbot = au_sbbot(sb);
	} else {
		bindex = au_dbtop(parent);
		bbot = au_dbtaildir(parent);
	}

	for (; bindex <= bbot; bindex++) {
		if (parent) {
			h_parent = au_h_dptr(parent, bindex);
			if (!h_parent || d_is_negative(h_parent))
				continue;
		}
		br = au_sbr(sb, bindex);
		if (au_br_rdonly(br))
			continue;

		/* sb->s_root for NFS is unreliable */
		h_path.mnt = au_br_mnt(br);
		h_path.dentry = h_path.mnt->mnt_root;
		err = vfs_statfs(&h_path, st);
		if (unlikely(err)) {
			AuWarn1("failed statfs, b%d, %d\n", bindex, err);
			continue;
		}

		/* when the available size is equal, select the lower one */
		BUILD_BUG_ON(sizeof(b) < sizeof(st->f_bavail)
			     || sizeof(b) < sizeof(st->f_bsize));
		b = st->f_bavail * st->f_bsize;
		br->br_wbr->wbr_bytes = b;
		if (b >= bavail) {
			bavail = b;
			mfs->mfs_bindex = bindex;
			mfs->mfs_jiffy = jiffies;
		}
	}

	mfs->mfsrr_bytes = bavail;
	AuDbg("b%d\n", mfs->mfs_bindex);
	kfree(st);
}

static int au_wbr_create_mfs(struct dentry *dentry, unsigned int flags)
{
	int err;
	struct dentry *parent;
	struct super_block *sb;
	struct au_wbr_mfs *mfs;

	err = au_wbr_create_exp(dentry);
	if (err >= 0)
		goto out;

	sb = dentry->d_sb;
	parent = NULL;
	if (au_ftest_wbr(flags, PARENT))
		parent = dget_parent(dentry);
	mfs = &au_sbi(sb)->si_wbr_mfs;
	mutex_lock(&mfs->mfs_lock);
	if (time_after(jiffies, mfs->mfs_jiffy + mfs->mfs_expire)
	    || mfs->mfs_bindex < 0
	    || au_br_rdonly(au_sbr(sb, mfs->mfs_bindex)))
		au_mfs(dentry, parent);
	mutex_unlock(&mfs->mfs_lock);
	err = mfs->mfs_bindex;
	dput(parent);

	if (err >= 0)
		err = au_wbr_nonopq(dentry, err);

out:
	AuDbg("b%d\n", err);
	return err;
}

static int au_wbr_create_init_mfs(struct super_block *sb)
{
	struct au_wbr_mfs *mfs;

	mfs = &au_sbi(sb)->si_wbr_mfs;
	mutex_init(&mfs->mfs_lock);
	mfs->mfs_jiffy = 0;
	mfs->mfs_bindex = -EROFS;

	return 0;
}

static int au_wbr_create_fin_mfs(struct super_block *sb __maybe_unused)
{
	mutex_destroy(&au_sbi(sb)->si_wbr_mfs.mfs_lock);
	return 0;
}

/* ---------------------------------------------------------------------- */

/* top down regardless parent, and then mfs */
static int au_wbr_create_tdmfs(struct dentry *dentry,
			       unsigned int flags __maybe_unused)
{
	int err;
	aufs_bindex_t bwh, btail, bindex, bfound, bmfs;
	unsigned long long watermark;
	struct super_block *sb;
	struct au_wbr_mfs *mfs;
	struct au_branch *br;
	struct dentry *parent;

	sb = dentry->d_sb;
	mfs = &au_sbi(sb)->si_wbr_mfs;
	mutex_lock(&mfs->mfs_lock);
	if (time_after(jiffies, mfs->mfs_jiffy + mfs->mfs_expire)
	    || mfs->mfs_bindex < 0)
		au_mfs(dentry, /*parent*/NULL);
	watermark = mfs->mfsrr_watermark;
	bmfs = mfs->mfs_bindex;
	mutex_unlock(&mfs->mfs_lock);

	/* another style of au_wbr_create_exp() */
	bwh = au_dbwh(dentry);
	parent = dget_parent(dentry);
	btail = au_dbtaildir(parent);
	if (bwh >= 0 && bwh < btail)
		btail = bwh;

	err = au_wbr_nonopq(dentry, btail);
	if (unlikely(err < 0))
		goto out;
	btail = err;
	bfound = -1;
	for (bindex = 0; bindex <= btail; bindex++) {
		br = au_sbr(sb, bindex);
		if (au_br_rdonly(br))
			continue;
		if (br->br_wbr->wbr_bytes > watermark) {
			bfound = bindex;
			break;
		}
	}
	err = bfound;
	if (err < 0)
		err = bmfs;

out:
	dput(parent);
	AuDbg("b%d\n", err);
	return err;
}

/* ---------------------------------------------------------------------- */

/* most free space and then round robin */
static int au_wbr_create_mfsrr(struct dentry *dentry, unsigned int flags)
{
	int err;
	struct au_wbr_mfs *mfs;

	err = au_wbr_create_mfs(dentry, flags);
	if (err >= 0) {
		mfs = &au_sbi(dentry->d_sb)->si_wbr_mfs;
		mutex_lock(&mfs->mfs_lock);
		if (mfs->mfsrr_bytes < mfs->mfsrr_watermark)
			err = au_wbr_create_rr(dentry, flags);
		mutex_unlock(&mfs->mfs_lock);
	}

	AuDbg("b%d\n", err);
	return err;
}

static int au_wbr_create_init_mfsrr(struct super_block *sb)
{
	int err;

	au_wbr_create_init_mfs(sb); /* ignore */
	err = au_wbr_create_init_rr(sb);

	return err;
}

/* ---------------------------------------------------------------------- */

/* top down parent and most free space */
static int au_wbr_create_pmfs(struct dentry *dentry, unsigned int flags)
{
	int err, e2;
	unsigned long long b;
	aufs_bindex_t bindex, btop, bbot;
	struct super_block *sb;
	struct dentry *parent, *h_parent;
	struct au_branch *br;

	err = au_wbr_create_tdp(dentry, flags);
	if (unlikely(err < 0))
		goto out;
	parent = dget_parent(dentry);
	btop = au_dbtop(parent);
	bbot = au_dbtaildir(parent);
	if (btop == bbot)
		goto out_parent; /* success */

	e2 = au_wbr_create_mfs(dentry, flags);
	if (e2 < 0)
		goto out_parent; /* success */

	/* when the available size is equal, select upper one */
	sb = dentry->d_sb;
	br = au_sbr(sb, err);
	b = br->br_wbr->wbr_bytes;
	AuDbg("b%d, %llu\n", err, b);

	for (bindex = btop; bindex <= bbot; bindex++) {
		h_parent = au_h_dptr(parent, bindex);
		if (!h_parent || d_is_negative(h_parent))
			continue;

		br = au_sbr(sb, bindex);
		if (!au_br_rdonly(br) && br->br_wbr->wbr_bytes > b) {
			b = br->br_wbr->wbr_bytes;
			err = bindex;
			AuDbg("b%d, %llu\n", err, b);
		}
	}

	if (err >= 0)
		err = au_wbr_nonopq(dentry, err);

out_parent:
	dput(parent);
out:
	AuDbg("b%d\n", err);
	return err;
}

/* ---------------------------------------------------------------------- */

/*
 * - top down parent
 * - most free space with parent
 * - most free space round-robin regardless parent
 */
static int au_wbr_create_pmfsrr(struct dentry *dentry, unsigned int flags)
{
	int err;
	unsigned long long watermark;
	struct super_block *sb;
	struct au_branch *br;
	struct au_wbr_mfs *mfs;

	err = au_wbr_create_pmfs(dentry, flags | AuWbr_PARENT);
	if (unlikely(err < 0))
		goto out;

	sb = dentry->d_sb;
	br = au_sbr(sb, err);
	mfs = &au_sbi(sb)->si_wbr_mfs;
	mutex_lock(&mfs->mfs_lock);
	watermark = mfs->mfsrr_watermark;
	mutex_unlock(&mfs->mfs_lock);
	if (br->br_wbr->wbr_bytes < watermark)
		/* regardless the parent dir */
		err = au_wbr_create_mfsrr(dentry, flags);

out:
	AuDbg("b%d\n", err);
	return err;
}

/* ---------------------------------------------------------------------- */

/* policies for copyup */

/* top down parent */
static int au_wbr_copyup_tdp(struct dentry *dentry)
{
	return au_wbr_create_tdp(dentry, /*flags, anything is ok*/0);
}

/* bottom up parent */
static int au_wbr_copyup_bup(struct dentry *dentry)
{
	int err;
	aufs_bindex_t bindex, btop;
	struct dentry *parent, *h_parent;
	struct super_block *sb;

	err = -EROFS;
	sb = dentry->d_sb;
	parent = dget_parent(dentry);
	btop = au_dbtop(parent);
	for (bindex = au_dbtop(dentry); bindex >= btop; bindex--) {
		h_parent = au_h_dptr(parent, bindex);
		if (!h_parent || d_is_negative(h_parent))
			continue;

		if (!au_br_rdonly(au_sbr(sb, bindex))) {
			err = bindex;
			break;
		}
	}
	dput(parent);

	/* bottom up here */
	if (unlikely(err < 0))
		err = au_wbr_bu(sb, btop - 1);

	AuDbg("b%d\n", err);
	return err;
}

/* bottom up */
int au_wbr_do_copyup_bu(struct dentry *dentry, aufs_bindex_t btop)
{
	int err;

	err = au_wbr_bu(dentry->d_sb, btop);
	AuDbg("b%d\n", err);
	if (err > btop)
		err = au_wbr_nonopq(dentry, err);

	AuDbg("b%d\n", err);
	return err;
}

static int au_wbr_copyup_bu(struct dentry *dentry)
{
	int err;
	aufs_bindex_t btop;

	btop = au_dbtop(dentry);
	err = au_wbr_do_copyup_bu(dentry, btop);
	return err;
}

/* ---------------------------------------------------------------------- */

struct au_wbr_copyup_operations au_wbr_copyup_ops[] = {
	[AuWbrCopyup_TDP] = {
		.copyup	= au_wbr_copyup_tdp
	},
	[AuWbrCopyup_BUP] = {
		.copyup	= au_wbr_copyup_bup
	},
	[AuWbrCopyup_BU] = {
		.copyup	= au_wbr_copyup_bu
	}
};

struct au_wbr_create_operations au_wbr_create_ops[] = {
	[AuWbrCreate_TDP] = {
		.create	= au_wbr_create_tdp
	},
	[AuWbrCreate_RR] = {
		.create	= au_wbr_create_rr,
		.init	= au_wbr_create_init_rr
	},
	[AuWbrCreate_MFS] = {
		.create	= au_wbr_create_mfs,
		.init	= au_wbr_create_init_mfs,
		.fin	= au_wbr_create_fin_mfs
	},
	[AuWbrCreate_MFSV] = {
		.create	= au_wbr_create_mfs,
		.init	= au_wbr_create_init_mfs,
		.fin	= au_wbr_create_fin_mfs
	},
	[AuWbrCreate_MFSRR] = {
		.create	= au_wbr_create_mfsrr,
		.init	= au_wbr_create_init_mfsrr,
		.fin	= au_wbr_create_fin_mfs
	},
	[AuWbrCreate_MFSRRV] = {
		.create	= au_wbr_create_mfsrr,
		.init	= au_wbr_create_init_mfsrr,
		.fin	= au_wbr_create_fin_mfs
	},
	[AuWbrCreate_TDMFS] = {
		.create	= au_wbr_create_tdmfs,
		.init	= au_wbr_create_init_mfs,
		.fin	= au_wbr_create_fin_mfs
	},
	[AuWbrCreate_TDMFSV] = {
		.create	= au_wbr_create_tdmfs,
		.init	= au_wbr_create_init_mfs,
		.fin	= au_wbr_create_fin_mfs
	},
	[AuWbrCreate_PMFS] = {
		.create	= au_wbr_create_pmfs,
		.init	= au_wbr_create_init_mfs,
		.fin	= au_wbr_create_fin_mfs
	},
	[AuWbrCreate_PMFSV] = {
		.create	= au_wbr_create_pmfs,
		.init	= au_wbr_create_init_mfs,
		.fin	= au_wbr_create_fin_mfs
	},
	[AuWbrCreate_PMFSRR] = {
		.create	= au_wbr_create_pmfsrr,
		.init	= au_wbr_create_init_mfsrr,
		.fin	= au_wbr_create_fin_mfs
	},
	[AuWbrCreate_PMFSRRV] = {
		.create	= au_wbr_create_pmfsrr,
		.init	= au_wbr_create_init_mfsrr,
		.fin	= au_wbr_create_fin_mfs
	}
};
