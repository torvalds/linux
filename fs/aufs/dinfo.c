/*
 * Copyright (C) 2005-2014 Junjiro R. Okajima
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
	static struct lock_class_key aufs_di;

	au_rw_init(&dinfo->di_rwsem);
	au_rw_class(&dinfo->di_rwsem, &aufs_di);
}

struct au_dinfo *au_di_alloc(struct super_block *sb, unsigned int lsc)
{
	struct au_dinfo *dinfo;
	int nbr, i;

	dinfo = au_cache_alloc_dinfo();
	if (unlikely(!dinfo))
		goto out;

	nbr = au_sbend(sb) + 1;
	if (nbr <= 0)
		nbr = 1;
	dinfo->di_hdentry = kcalloc(nbr, sizeof(*dinfo->di_hdentry), GFP_NOFS);
	if (dinfo->di_hdentry) {
		au_rw_write_lock_nested(&dinfo->di_rwsem, lsc);
		dinfo->di_bstart = -1;
		dinfo->di_bend = -1;
		dinfo->di_bwh = -1;
		dinfo->di_bdiropq = -1;
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
	aufs_bindex_t bend, bindex;

	/* dentry may not be revalidated */
	bindex = dinfo->di_bstart;
	if (bindex >= 0) {
		bend = dinfo->di_bend;
		p = dinfo->di_hdentry + bindex;
		while (bindex++ <= bend)
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
	DiSwap(bi, bstart);
	DiSwap(bi, bend);
	DiSwap(bi, bwh);
	DiSwap(bi, bdiropq);
	/* smp_mb(); */

#undef DiSwap
}

void au_di_cp(struct au_dinfo *dst, struct au_dinfo *src)
{
	AuRwMustWriteLock(&dst->di_rwsem);
	AuRwMustWriteLock(&src->di_rwsem);

	dst->di_bstart = src->di_bstart;
	dst->di_bend = src->di_bend;
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

int au_di_realloc(struct au_dinfo *dinfo, int nbr)
{
	int err, sz;
	struct au_hdentry *hdp;

	AuRwMustWriteLock(&dinfo->di_rwsem);

	err = -ENOMEM;
	sz = sizeof(*hdp) * (dinfo->di_bend + 1);
	if (!sz)
		sz = sizeof(*hdp);
	hdp = au_kzrealloc(dinfo->di_hdentry, sz, sizeof(*hdp) * nbr, GFP_NOFS);
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
	au_rw_read_lock_nested(&au_di(d)->di_rwsem, lsc);
	if (d->d_inode) {
		if (au_ftest_lock(flags, IW))
			do_ii_write_lock(d->d_inode, lsc);
		else if (au_ftest_lock(flags, IR))
			do_ii_read_lock(d->d_inode, lsc);
	}
}

void di_read_unlock(struct dentry *d, int flags)
{
	if (d->d_inode) {
		if (au_ftest_lock(flags, IW)) {
			au_dbg_verify_dinode(d);
			ii_write_unlock(d->d_inode);
		} else if (au_ftest_lock(flags, IR)) {
			au_dbg_verify_dinode(d);
			ii_read_unlock(d->d_inode);
		}
	}
	au_rw_read_unlock(&au_di(d)->di_rwsem);
}

void di_downgrade_lock(struct dentry *d, int flags)
{
	if (d->d_inode && au_ftest_lock(flags, IR))
		ii_downgrade_lock(d->d_inode);
	au_rw_dgrade_lock(&au_di(d)->di_rwsem);
}

void di_write_lock(struct dentry *d, unsigned int lsc)
{
	au_rw_write_lock_nested(&au_di(d)->di_rwsem, lsc);
	if (d->d_inode)
		do_ii_write_lock(d->d_inode, lsc);
}

void di_write_unlock(struct dentry *d)
{
	au_dbg_verify_dinode(d);
	if (d->d_inode)
		ii_write_unlock(d->d_inode);
	au_rw_write_unlock(&au_di(d)->di_rwsem);
}

void di_write_lock2_child(struct dentry *d1, struct dentry *d2, int isdir)
{
	AuDebugOn(d1 == d2
		  || d1->d_inode == d2->d_inode
		  || d1->d_sb != d2->d_sb);

	if (isdir && au_test_subdir(d1, d2)) {
		di_write_lock_child(d1);
		di_write_lock_child2(d2);
	} else {
		/* there should be no races */
		di_write_lock_child(d2);
		di_write_lock_child2(d1);
	}
}

void di_write_lock2_parent(struct dentry *d1, struct dentry *d2, int isdir)
{
	AuDebugOn(d1 == d2
		  || d1->d_inode == d2->d_inode
		  || d1->d_sb != d2->d_sb);

	if (isdir && au_test_subdir(d1, d2)) {
		di_write_lock_parent(d1);
		di_write_lock_parent2(d2);
	} else {
		/* there should be no races */
		di_write_lock_parent(d2);
		di_write_lock_parent2(d1);
	}
}

void di_write_unlock2(struct dentry *d1, struct dentry *d2)
{
	di_write_unlock(d1);
	if (d1->d_inode == d2->d_inode)
		au_rw_write_unlock(&au_di(d2)->di_rwsem);
	else
		di_write_unlock(d2);
}

/* ---------------------------------------------------------------------- */

struct dentry *au_h_dptr(struct dentry *dentry, aufs_bindex_t bindex)
{
	struct dentry *d;

	DiMustAnyLock(dentry);

	if (au_dbstart(dentry) < 0 || bindex < au_dbstart(dentry))
		return NULL;
	AuDebugOn(bindex < 0);
	d = au_di(dentry)->di_hdentry[0 + bindex].hd_dentry;
	AuDebugOn(d && d->d_count <= 0);
	return d;
}

/*
 * extended version of au_h_dptr().
 * returns a hashed and positive h_dentry in bindex, NULL, or error.
 */
struct dentry *au_h_d_alias(struct dentry *dentry, aufs_bindex_t bindex)
{
	struct dentry *h_dentry;
	struct inode *inode, *h_inode;

	inode = dentry->d_inode;
	AuDebugOn(!inode);

	h_dentry = NULL;
	if (au_dbstart(dentry) <= bindex
	    && bindex <= au_dbend(dentry))
		h_dentry = au_h_dptr(dentry, bindex);
	if (h_dentry && !au_d_hashed_positive(h_dentry)) {
		dget(h_dentry);
		goto out; /* success */
	}

	AuDebugOn(bindex < au_ibstart(inode));
	AuDebugOn(au_ibend(inode) < bindex);
	h_inode = au_h_iptr(inode, bindex);
	h_dentry = d_find_alias(h_inode);
	if (h_dentry) {
		if (!IS_ERR(h_dentry)) {
			if (!au_d_hashed_positive(h_dentry))
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
	aufs_bindex_t bend, bwh;

	bend = au_dbend(dentry);
	if (0 <= bend) {
		bwh = au_dbwh(dentry);
		if (!bwh)
			return bwh;
		if (0 < bwh && bwh < bend)
			return bwh - 1;
	}
	return bend;
}

aufs_bindex_t au_dbtaildir(struct dentry *dentry)
{
	aufs_bindex_t bend, bopq;

	bend = au_dbtail(dentry);
	if (0 <= bend) {
		bopq = au_dbdiropq(dentry);
		if (0 <= bopq && bopq < bend)
			bend = bopq;
	}
	return bend;
}

/* ---------------------------------------------------------------------- */

void au_set_h_dptr(struct dentry *dentry, aufs_bindex_t bindex,
		   struct dentry *h_dentry)
{
	struct au_hdentry *hd = au_di(dentry)->di_hdentry + bindex;
	struct au_branch *br;

	DiMustWriteLock(dentry);

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
	aufs_bindex_t bstart, bend;

	err = 0;
	bstart = au_dbstart(dentry);
	bend = au_dbend(dentry);
	if (bstart >= 0)
		AuDebugOn(bend < 0 && bstart > bend);
	else {
		err = -EIO;
		AuDebugOn(bend >= 0);
	}

	return err;
}

int au_digen_test(struct dentry *dentry, unsigned int sigen)
{
	int err;

	err = 0;
	if (unlikely(au_digen(dentry) != sigen
		     || au_iigen_test(dentry->d_inode, sigen)))
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

	DiMustWriteLock(dentry);

	dinfo = au_di(dentry);
	if (!dinfo || dinfo->di_bstart < 0)
		return;

	hdp = dinfo->di_hdentry;
	if (do_put_zero) {
		aufs_bindex_t bindex, bend;

		bend = dinfo->di_bend;
		for (bindex = dinfo->di_bstart; bindex <= bend; bindex++) {
			h_d = hdp[0 + bindex].hd_dentry;
			if (h_d && !h_d->d_inode)
				au_set_h_dptr(dentry, bindex, NULL);
		}
	}

	dinfo->di_bstart = -1;
	while (++dinfo->di_bstart <= dinfo->di_bend)
		if (hdp[0 + dinfo->di_bstart].hd_dentry)
			break;
	if (dinfo->di_bstart > dinfo->di_bend) {
		dinfo->di_bstart = -1;
		dinfo->di_bend = -1;
		return;
	}

	dinfo->di_bend++;
	while (0 <= --dinfo->di_bend)
		if (hdp[0 + dinfo->di_bend].hd_dentry)
			break;
	AuDebugOn(dinfo->di_bstart > dinfo->di_bend || dinfo->di_bend < 0);
}

void au_update_dbstart(struct dentry *dentry)
{
	aufs_bindex_t bindex, bend;
	struct dentry *h_dentry;

	bend = au_dbend(dentry);
	for (bindex = au_dbstart(dentry); bindex <= bend; bindex++) {
		h_dentry = au_h_dptr(dentry, bindex);
		if (!h_dentry)
			continue;
		if (h_dentry->d_inode) {
			au_set_dbstart(dentry, bindex);
			return;
		}
		au_set_h_dptr(dentry, bindex, NULL);
	}
}

void au_update_dbend(struct dentry *dentry)
{
	aufs_bindex_t bindex, bstart;
	struct dentry *h_dentry;

	bstart = au_dbstart(dentry);
	for (bindex = au_dbend(dentry); bindex >= bstart; bindex--) {
		h_dentry = au_h_dptr(dentry, bindex);
		if (!h_dentry)
			continue;
		if (h_dentry->d_inode) {
			au_set_dbend(dentry, bindex);
			return;
		}
		au_set_h_dptr(dentry, bindex, NULL);
	}
}

int au_find_dbindex(struct dentry *dentry, struct dentry *h_dentry)
{
	aufs_bindex_t bindex, bend;

	bend = au_dbend(dentry);
	for (bindex = au_dbstart(dentry); bindex <= bend; bindex++)
		if (au_h_dptr(dentry, bindex) == h_dentry)
			return bindex;
	return -1;
}
