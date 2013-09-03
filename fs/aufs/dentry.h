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
 * lookup and dentry operations
 */

#ifndef __AUFS_DENTRY_H__
#define __AUFS_DENTRY_H__

#ifdef __KERNEL__

#include <linux/dcache.h>
#include "rwsem.h"

struct au_hdentry {
	struct dentry		*hd_dentry;
	aufs_bindex_t		hd_id;
};

struct au_dinfo {
	atomic_t		di_generation;

	struct au_rwsem		di_rwsem;
	aufs_bindex_t		di_bstart, di_bend, di_bwh, di_bdiropq;
	struct au_hdentry	*di_hdentry;
} ____cacheline_aligned_in_smp;

/* ---------------------------------------------------------------------- */

/* dentry.c */
extern const struct dentry_operations aufs_dop;
struct au_branch;
struct dentry *au_sio_lkup_one(struct qstr *name, struct dentry *parent,
			       struct au_branch *br);
int au_h_verify(struct dentry *h_dentry, unsigned int udba, struct inode *h_dir,
		struct dentry *h_parent, struct au_branch *br);

int au_lkup_dentry(struct dentry *dentry, aufs_bindex_t bstart, mode_t type);
int au_lkup_neg(struct dentry *dentry, aufs_bindex_t bindex, int wh);
int au_refresh_dentry(struct dentry *dentry, struct dentry *parent);
int au_reval_dpath(struct dentry *dentry, unsigned int sigen);

/* dinfo.c */
void au_di_init_once(void *_di);
struct au_dinfo *au_di_alloc(struct super_block *sb, unsigned int lsc);
void au_di_free(struct au_dinfo *dinfo);
void au_di_swap(struct au_dinfo *a, struct au_dinfo *b);
void au_di_cp(struct au_dinfo *dst, struct au_dinfo *src);
int au_di_init(struct dentry *dentry);
void au_di_fin(struct dentry *dentry);
int au_di_realloc(struct au_dinfo *dinfo, int nbr);

void di_read_lock(struct dentry *d, int flags, unsigned int lsc);
void di_read_unlock(struct dentry *d, int flags);
void di_downgrade_lock(struct dentry *d, int flags);
void di_write_lock(struct dentry *d, unsigned int lsc);
void di_write_unlock(struct dentry *d);
void di_write_lock2_child(struct dentry *d1, struct dentry *d2, int isdir);
void di_write_lock2_parent(struct dentry *d1, struct dentry *d2, int isdir);
void di_write_unlock2(struct dentry *d1, struct dentry *d2);

struct dentry *au_h_dptr(struct dentry *dentry, aufs_bindex_t bindex);
struct dentry *au_h_d_alias(struct dentry *dentry, aufs_bindex_t bindex);
aufs_bindex_t au_dbtail(struct dentry *dentry);
aufs_bindex_t au_dbtaildir(struct dentry *dentry);

void au_set_h_dptr(struct dentry *dentry, aufs_bindex_t bindex,
		   struct dentry *h_dentry);
int au_digen_test(struct dentry *dentry, unsigned int sigen);
int au_dbrange_test(struct dentry *dentry);
void au_update_digen(struct dentry *dentry);
void au_update_dbrange(struct dentry *dentry, int do_put_zero);
void au_update_dbstart(struct dentry *dentry);
void au_update_dbend(struct dentry *dentry);
int au_find_dbindex(struct dentry *dentry, struct dentry *h_dentry);

/* ---------------------------------------------------------------------- */

static inline struct au_dinfo *au_di(struct dentry *dentry)
{
	return dentry->d_fsdata;
}

/* ---------------------------------------------------------------------- */

/* lock subclass for dinfo */
enum {
	AuLsc_DI_CHILD,		/* child first */
	AuLsc_DI_CHILD2,	/* rename(2), link(2), and cpup at hnotify */
	AuLsc_DI_CHILD3,	/* copyup dirs */
	AuLsc_DI_PARENT,
	AuLsc_DI_PARENT2,
	AuLsc_DI_PARENT3,
	AuLsc_DI_TMP		/* temp for replacing dinfo */
};

/*
 * di_read_lock_child, di_write_lock_child,
 * di_read_lock_child2, di_write_lock_child2,
 * di_read_lock_child3, di_write_lock_child3,
 * di_read_lock_parent, di_write_lock_parent,
 * di_read_lock_parent2, di_write_lock_parent2,
 * di_read_lock_parent3, di_write_lock_parent3,
 */
#define AuReadLockFunc(name, lsc) \
static inline void di_read_lock_##name(struct dentry *d, int flags) \
{ di_read_lock(d, flags, AuLsc_DI_##lsc); }

#define AuWriteLockFunc(name, lsc) \
static inline void di_write_lock_##name(struct dentry *d) \
{ di_write_lock(d, AuLsc_DI_##lsc); }

#define AuRWLockFuncs(name, lsc) \
	AuReadLockFunc(name, lsc) \
	AuWriteLockFunc(name, lsc)

AuRWLockFuncs(child, CHILD);
AuRWLockFuncs(child2, CHILD2);
AuRWLockFuncs(child3, CHILD3);
AuRWLockFuncs(parent, PARENT);
AuRWLockFuncs(parent2, PARENT2);
AuRWLockFuncs(parent3, PARENT3);

#undef AuReadLockFunc
#undef AuWriteLockFunc
#undef AuRWLockFuncs

#define DiMustNoWaiters(d)	AuRwMustNoWaiters(&au_di(d)->di_rwsem)
#define DiMustAnyLock(d)	AuRwMustAnyLock(&au_di(d)->di_rwsem)
#define DiMustWriteLock(d)	AuRwMustWriteLock(&au_di(d)->di_rwsem)

/* ---------------------------------------------------------------------- */

/* todo: memory barrier? */
static inline unsigned int au_digen(struct dentry *d)
{
	return atomic_read(&au_di(d)->di_generation);
}

static inline void au_h_dentry_init(struct au_hdentry *hdentry)
{
	hdentry->hd_dentry = NULL;
}

static inline void au_hdput(struct au_hdentry *hd)
{
	if (hd)
		dput(hd->hd_dentry);
}

static inline aufs_bindex_t au_dbstart(struct dentry *dentry)
{
	DiMustAnyLock(dentry);
	return au_di(dentry)->di_bstart;
}

static inline aufs_bindex_t au_dbend(struct dentry *dentry)
{
	DiMustAnyLock(dentry);
	return au_di(dentry)->di_bend;
}

static inline aufs_bindex_t au_dbwh(struct dentry *dentry)
{
	DiMustAnyLock(dentry);
	return au_di(dentry)->di_bwh;
}

static inline aufs_bindex_t au_dbdiropq(struct dentry *dentry)
{
	DiMustAnyLock(dentry);
	return au_di(dentry)->di_bdiropq;
}

/* todo: hard/soft set? */
static inline void au_set_dbstart(struct dentry *dentry, aufs_bindex_t bindex)
{
	DiMustWriteLock(dentry);
	au_di(dentry)->di_bstart = bindex;
}

static inline void au_set_dbend(struct dentry *dentry, aufs_bindex_t bindex)
{
	DiMustWriteLock(dentry);
	au_di(dentry)->di_bend = bindex;
}

static inline void au_set_dbwh(struct dentry *dentry, aufs_bindex_t bindex)
{
	DiMustWriteLock(dentry);
	/* dbwh can be outside of bstart - bend range */
	au_di(dentry)->di_bwh = bindex;
}

static inline void au_set_dbdiropq(struct dentry *dentry, aufs_bindex_t bindex)
{
	DiMustWriteLock(dentry);
	au_di(dentry)->di_bdiropq = bindex;
}

/* ---------------------------------------------------------------------- */

#ifdef CONFIG_AUFS_HNOTIFY
static inline void au_digen_dec(struct dentry *d)
{
	atomic_dec(&au_di(d)->di_generation);
}

static inline void au_hn_di_reinit(struct dentry *dentry)
{
	dentry->d_fsdata = NULL;
}
#else
AuStubVoid(au_hn_di_reinit, struct dentry *dentry __maybe_unused)
#endif /* CONFIG_AUFS_HNOTIFY */

#endif /* __KERNEL__ */
#endif /* __AUFS_DENTRY_H__ */
