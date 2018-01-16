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
 * dentry private data
 */

#include "aufs.h"

void au_di_init_once(void *_dinfo)
{
	struct au_dinfo *dinfo = _dinfo;

	au_rw_init(&dinfo->di_rwsem);
}

struct au_dinfo *au_di_alloc(struct super_block *sb, unsigned int lsc)
{
	struct au_dinfo *dinfo;
	int nbr, i;

	dinfo = au_cache_alloc_dinfo();
	if (unlikely(!dinfo))
		goto out;

	nbr = au_sbbot(sb) + 1;
	if (nbr <= 0)
		nbr = 1;
	dinfo->di_hdentry = kcalloc(nbr, sizeof(*dinfo->di_hdentry), GFP_NOFS);
	if (dinfo->di_hdentry) {
		au_rw_write_lock_nested(&dinfo->di_rwsem, lsc);
		dinfo->di_btop = -1;
		dinfo->di_bbot = -1;
		dinfo->di_bwh = -1;
		dinfo->di_bdiropq = -1;
		dinfo->di_tmpfile = 0;
		for (i = 0; i < nbr; i++)
			dinfo->di_hdentry[i].hd_id = -1;
		goto out;
	}

	au_cache_free_dinfo(dinfo);
	dinfo = NULL;

out:
	return dinfo;
}

void au_di_free(struct au_dinfo *dinfo)
{
	struct au_hdentry *p;
	aufs_bindex_t bbot, bindex;

	/* dentry may not be revalidated */
	bindex = dinfo->di_btop;
	if (bindex >= 0) {
		bbot = dinfo->di_bbot;
		p = au_hdentry(dinfo, bindex);
		while (bindex++ <= bbot)
			au_hdput(p++);
	}
	kfree(dinfo->di_hdentry);
	au_cache_free_dinfo(dinfo);
}

void au_di_swap(struct au_dinfo *a, struct au_dinfo *b)
{
	struct au_hdentry *p;
	aufs_bindex_t bi;

	AuRwMustWriteLock(&a->di_rwsem);
	AuRwMustWriteLock(&b->di_rwsem);

#define DiSwap(v, name)				\
	do {					\
		v = a->di_##name;		\
		a->di_##name = b->di_##name;	\
		b->di_##name = v;		\
	} while (0)

	DiSwap(p, hdentry);
	DiSwap(bi, btop);
	DiSwap(bi, bbot);
	DiSwap(bi, bwh);
	DiSwap(bi, bdiropq);
	/* smp_mb(); */

#undef DiSwap
}

void au_di_cp(struct au_dinfo *dst, struct au_dinfo *src)
{
	AuRwMustWriteLock(&dst->di_rwsem);
	AuRwMustWriteLock(&src->di_rwsem);

	dst->di_btop = src->di_btop;
	dst->di_bbot = src->di_bbot;
	dst->di_bwh = src->di_bwh;
	dst->di_bdiropq = src->di_bdiropq;
	/* smp_mb(); */
}

int au_di_init(struct dentry *dentry)
{
	int err;
	struct super_block *sb;
	struct au_dinfo *dinfo;

	err = 0;
	sb = dentry->d_sb;
	dinfo = au_di_alloc(sb, AuLsc_DI_CHILD);
	if (dinfo) {
		atomic_set(&dinfo->di_generation, au_sigen(sb));
		/* smp_mb(); */ /* atomic_set */
		dentry->d_fsdata = dinfo;
	} else
		err = -ENOMEM;

	return err;
}

void au_di_fin(struct dentry *dentry)
{
	struct au_dinfo *dinfo;

	dinfo = au_di(dentry);
	AuRwDestroy(&dinfo->di_rwsem);
	au_di_free(dinfo);
}

int au_di_realloc(struct au_dinfo *dinfo, int nbr, int may_shrink)
{
	int err, sz;
	struct au_hdentry *hdp;

	AuRwMustWriteLock(&dinfo->di_rwsem);

	err = -ENOMEM;
	sz = sizeof(*hdp) * (dinfo->di_bbot + 1);
	if (!sz)
		sz = sizeof(*hdp);
	hdp = au_kzrealloc(dinfo->di_hdentry, sz, sizeof(*hdp) * nbr, GFP_NOFS,
			   may_shrink);
	if (hdp) {
		dinfo->di_hdentry = hdp;
		err = 0;
	}

	return err;
}

/* ---------------------------------------------------------------------- */

static void do_ii_write_lock(struct inode *inode, unsigned int lsc)
{
	switch (lsc) {
	case AuLsc_DI_CHILD:
		ii_write_lock_child(inode);
		break;
	case AuLsc_DI_CHILD2:
		ii_write_lock_child2(inode);
		break;
	case AuLsc_DI_CHILD3:
		ii_write_lock_child3(inode);
		break;
	case AuLsc_DI_PARENT:
		ii_write_lock_parent(inode);
		break;
	case AuLsc_DI_PARENT2:
		ii_write_lock_parent2(inode);
		break;
	case AuLsc_DI_PARENT3:
		ii_write_lock_parent3(inode);
		break;
	default:
		BUG();
	}
}

static void do_ii_read_lock(struct inode *inode, unsigned int lsc)
{
	switch (lsc) {
	case AuLsc_DI_CHILD:
		ii_read_lock_child(inode);
		break;
	case AuLsc_DI_CHILD2:
		ii_read_lock_child2(inode);
		break;
	case AuLsc_DI_CHILD3:
		ii_read_lock_child3(inode);
		break;
	case AuLsc_DI_PARENT:
		ii_read_lock_parent(inode);
		break;
	case AuLsc_DI_PARENT2:
		ii_read_lock_parent2(inode);
		break;
	case AuLsc_DI_PARENT3:
		ii_read_lock_parent3(inode);
		break;
	default:
		BUG();
	}
}

void di_read_lock(struct dentry *d, int flags, unsigned int lsc)
{
	struct inode *inode;

	au_rw_read_lock_nested(&au_di(d)->di_rwsem, lsc);
	if (d_really_is_positive(d)) {
		inode = d_inode(d);
		if (au_ftest_lock(flags, IW))
			do_ii_write_lock(inode, lsc);
		else if (au_ftest_lock(flags, IR))
			do_ii_read_lock(inode, lsc);
	}
}

void di_read_unlock(struct dentry *d, int flags)
{
	struct inode *inode;

	if (d_really_is_positive(d)) {
		inode = d_inode(d);
		if (au_ftest_lock(flags, IW)) {
			au_dbg_verify_dinode(d);
			ii_write_unlock(inode);
		} else if (au_ftest_lock(flags, IR)) {
			au_dbg_verify_dinode(d);
			ii_read_unlock(inode);
		}
	}
	au_rw_read_unlock(&au_di(d)->di_rwsem);
}

void di_downgrade_lock(struct dentry *d, int flags)
{
	if (d_really_is_positive(d) && au_ftest_lock(flags, IR))
		ii_downgrade_lock(d_inode(d));
	au_rw_dgrade_lock(&au_di(d)->di_rwsem);
}

void di_write_lock(struct dentry *d, unsigned int lsc)
{
	au_rw_write_lock_nested(&au_di(d)->di_rwsem, lsc);
	if (d_really_is_positive(d))
		do_ii_write_lock(d_inode(d), lsc);
}

void di_write_unlock(struct dentry *d)
{
	au_dbg_verify_dinode(d);
	if (d_really_is_positive(d))
		ii_write_unlock(d_inode(d));
	au_rw_write_unlock(&au_di(d)->di_rwsem);
}

void di_write_lock2_child(struct dentry *d1, struct dentry *d2, int isdir)
{
	AuDebugOn(d1 == d2
		  || d_inode(d1) == d_inode(d2)
		  || d1->d_sb != d2->d_sb);

	if ((isdir && au_test_subdir(d1, d2))
	    || d1 < d2) {
		di_write_lock_child(d1);
		di_write_lock_child2(d2);
	} else {
		di_write_lock_child(d2);
		di_write_lock_child2(d1);
	}
}

void di_write_lock2_parent(struct dentry *d1, struct dentry *d2, int isdir)
{
	AuDebugOn(d1 == d2
		  || d_inode(d1) == d_inode(d2)
		  || d1->d_sb != d2->d_sb);

	if ((isdir && au_test_subdir(d1, d2))
	    || d1 < d2) {
		di_write_lock_parent(d1);
		di_write_lock_parent2(d2);
	} else {
		di_write_lock_parent(d2);
		di_write_lock_parent2(d1);
	}
}

void di_write_unlock2(struct dentry *d1, struct dentry *d2)
{
	di_write_unlock(d1);
	if (d_inode(d1) == d_inode(d2))
		au_rw_write_unlock(&au_di(d2)->di_rwsem);
	else
		di_write_unlock(d2);
}

/* ---------------------------------------------------------------------- */

struct dentry *au_h_dptr(struct dentry *dentry, aufs_bindex_t bindex)
{
	struct dentry *d;

	DiMustAnyLock(dentry);

	if (au_dbtop(dentry) < 0 || bindex < au_dbtop(dentry))
		return NULL;
	AuDebugOn(bindex < 0);
	d = au_hdentry(au_di(dentry), bindex)->hd_dentry;
	AuDebugOn(d && au_dcount(d) <= 0);
	return d;
}

/*
 * extended version of au_h_dptr().
 * returns a hashed and positive (or linkable) h_dentry in bindex, NULL, or
 * error.
 */
struct dentry *au_h_d_alias(struct dentry *dentry, aufs_bindex_t bindex)
{
	struct dentry *h_dentry;
	struct inode *inode, *h_inode;

	AuDebugOn(d_really_is_negative(dentry));

	h_dentry = NULL;
	if (au_dbtop(dentry) <= bindex
	    && bindex <= au_dbbot(dentry))
		h_dentry = au_h_dptr(dentry, bindex);
	if (h_dentry && !au_d_linkable(h_dentry)) {
		dget(h_dentry);
		goto out; /* success */
	}

	inode = d_inode(dentry);
	AuDebugOn(bindex < au_ibtop(inode));
	AuDebugOn(au_ibbot(inode) < bindex);
	h_inode = au_h_iptr(inode, bindex);
	h_dentry = d_find_alias(h_inode);
	if (h_dentry) {
		if (!IS_ERR(h_dentry)) {
			if (!au_d_linkable(h_dentry))
				goto out; /* success */
			dput(h_dentry);
		} else
			goto out;
	}

	if (au_opt_test(au_mntflags(dentry->d_sb), PLINK)) {
		h_dentry = au_plink_lkup(inode, bindex);
		AuDebugOn(!h_dentry);
		if (!IS_ERR(h_dentry)) {
			if (!au_d_hashed_positive(h_dentry))
				goto out; /* success */
			dput(h_dentry);
			h_dentry = NULL;
		}
	}

out:
	AuDbgDentry(h_dentry);
	return h_dentry;
}

aufs_bindex_t au_dbtail(struct dentry *dentry)
{
	aufs_bindex_t bbot, bwh;

	bbot = au_dbbot(dentry);
	if (0 <= bbot) {
		bwh = au_dbwh(dentry);
		if (!bwh)
			return bwh;
		if (0 < bwh && bwh < bbot)
			return bwh - 1;
	}
	return bbot;
}

aufs_bindex_t au_dbtaildir(struct dentry *dentry)
{
	aufs_bindex_t bbot, bopq;

	bbot = au_dbtail(dentry);
	if (0 <= bbot) {
		bopq = au_dbdiropq(dentry);
		if (0 <= bopq && bopq < bbot)
			bbot = bopq;
	}
	return bbot;
}

/* ---------------------------------------------------------------------- */

void au_set_h_dptr(struct dentry *dentry, aufs_bindex_t bindex,
		   struct dentry *h_dentry)
{
	struct au_dinfo *dinfo;
	struct au_hdentry *hd;
	struct au_branch *br;

	DiMustWriteLock(dentry);

	dinfo = au_di(dentry);
	hd = au_hdentry(dinfo, bindex);
	au_hdput(hd);
	hd->hd_dentry = h_dentry;
	if (h_dentry) {
		br = au_sbr(dentry->d_sb, bindex);
		hd->hd_id = br->br_id;
	}
}

int au_dbrange_test(struct dentry *dentry)
{
	int err;
	aufs_bindex_t btop, bbot;

	err = 0;
	btop = au_dbtop(dentry);
	bbot = au_dbbot(dentry);
	if (btop >= 0)
		AuDebugOn(bbot < 0 && btop > bbot);
	else {
		err = -EIO;
		AuDebugOn(bbot >= 0);
	}

	return err;
}

int au_digen_test(struct dentry *dentry, unsigned int sigen)
{
	int err;

	err = 0;
	if (unlikely(au_digen(dentry) != sigen
		     || au_iigen_test(d_inode(dentry), sigen)))
		err = -EIO;

	return err;
}

void au_update_digen(struct dentry *dentry)
{
	atomic_set(&au_di(dentry)->di_generation, au_sigen(dentry->d_sb));
	/* smp_mb(); */ /* atomic_set */
}

void au_update_dbrange(struct dentry *dentry, int do_put_zero)
{
	struct au_dinfo *dinfo;
	struct dentry *h_d;
	struct au_hdentry *hdp;
	aufs_bindex_t bindex, bbot;

	DiMustWriteLock(dentry);

	dinfo = au_di(dentry);
	if (!dinfo || dinfo->di_btop < 0)
		return;

	if (do_put_zero) {
		bbot = dinfo->di_bbot;
		bindex = dinfo->di_btop;
		hdp = au_hdentry(dinfo, bindex);
		for (; bindex <= bbot; bindex++, hdp++) {
			h_d = hdp->hd_dentry;
			if (h_d && d_is_negative(h_d))
				au_set_h_dptr(dentry, bindex, NULL);
		}
	}

	dinfo->di_btop = 0;
	hdp = au_hdentry(dinfo, dinfo->di_btop);
	for (; dinfo->di_btop <= dinfo->di_bbot; dinfo->di_btop++, hdp++)
		if (hdp->hd_dentry)
			break;
	if (dinfo->di_btop > dinfo->di_bbot) {
		dinfo->di_btop = -1;
		dinfo->di_bbot = -1;
		return;
	}

	hdp = au_hdentry(dinfo, dinfo->di_bbot);
	for (; dinfo->di_bbot >= 0; dinfo->di_bbot--, hdp--)
		if (hdp->hd_dentry)
			break;
	AuDebugOn(dinfo->di_btop > dinfo->di_bbot || dinfo->di_bbot < 0);
}

void au_update_dbtop(struct dentry *dentry)
{
	aufs_bindex_t bindex, bbot;
	struct dentry *h_dentry;

	bbot = au_dbbot(dentry);
	for (bindex = au_dbtop(dentry); bindex <= bbot; bindex++) {
		h_dentry = au_h_dptr(dentry, bindex);
		if (!h_dentry)
			continue;
		if (d_is_positive(h_dentry)) {
			au_set_dbtop(dentry, bindex);
			return;
		}
		au_set_h_dptr(dentry, bindex, NULL);
	}
}

void au_update_dbbot(struct dentry *dentry)
{
	aufs_bindex_t bindex, btop;
	struct dentry *h_dentry;

	btop = au_dbtop(dentry);
	for (bindex = au_dbbot(dentry); bindex >= btop; bindex--) {
		h_dentry = au_h_dptr(dentry, bindex);
		if (!h_dentry)
			continue;
		if (d_is_positive(h_dentry)) {
			au_set_dbbot(dentry, bindex);
			return;
		}
		au_set_h_dptr(dentry, bindex, NULL);
	}
}

int au_find_dbindex(struct dentry *dentry, struct dentry *h_dentry)
{
	aufs_bindex_t bindex, bbot;

	bbot = au_dbbot(dentry);
	for (bindex = au_dbtop(dentry); bindex <= bbot; bindex++)
		if (au_h_dptr(dentry, bindex) == h_dentry)
			return bindex;
	return -1;
}
