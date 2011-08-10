/*
 * Copyright (C) 2005-2011 Junjiro R. Okajima
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
 * abstraction to notify the direct changes on lower directories
 */

#include "aufs.h"

int au_hn_alloc(struct au_hinode *hinode, struct inode *inode)
{
	int err;
	struct au_hnotify *hn;

	err = -ENOMEM;
	hn = au_cache_alloc_hnotify();
	if (hn) {
		hn->hn_aufs_inode = inode;
		hinode->hi_notify = hn;
		err = au_hnotify_op.alloc(hinode);
		AuTraceErr(err);
		if (unlikely(err)) {
			hinode->hi_notify = NULL;
			au_cache_free_hnotify(hn);
			/*
			 * The upper dir was removed by udba, but the same named
			 * dir left. In this case, aufs assignes a new inode
			 * number and set the monitor again.
			 * For the lower dir, the old monitnor is still left.
			 */
			if (err == -EEXIST)
				err = 0;
		}
	}

	AuTraceErr(err);
	return err;
}

void au_hn_free(struct au_hinode *hinode)
{
	struct au_hnotify *hn;

	hn = hinode->hi_notify;
	if (hn) {
		au_hnotify_op.free(hinode);
		au_cache_free_hnotify(hn);
		hinode->hi_notify = NULL;
	}
}

/* ---------------------------------------------------------------------- */

void au_hn_ctl(struct au_hinode *hinode, int do_set)
{
	if (hinode->hi_notify)
		au_hnotify_op.ctl(hinode, do_set);
}

void au_hn_reset(struct inode *inode, unsigned int flags)
{
	aufs_bindex_t bindex, bend;
	struct inode *hi;
	struct dentry *iwhdentry;

	bend = au_ibend(inode);
	for (bindex = au_ibstart(inode); bindex <= bend; bindex++) {
		hi = au_h_iptr(inode, bindex);
		if (!hi)
			continue;

		/* mutex_lock_nested(&hi->i_mutex, AuLsc_I_CHILD); */
		iwhdentry = au_hi_wh(inode, bindex);
		if (iwhdentry)
			dget(iwhdentry);
		au_igrab(hi);
		au_set_h_iptr(inode, bindex, NULL, 0);
		au_set_h_iptr(inode, bindex, au_igrab(hi),
			      flags & ~AuHi_XINO);
		iput(hi);
		dput(iwhdentry);
		/* mutex_unlock(&hi->i_mutex); */
	}
}

/* ---------------------------------------------------------------------- */

static int hn_xino(struct inode *inode, struct inode *h_inode)
{
	int err;
	aufs_bindex_t bindex, bend, bfound, bstart;
	struct inode *h_i;

	err = 0;
	if (unlikely(inode->i_ino == AUFS_ROOT_INO)) {
		pr_warning("branch root dir was changed\n");
		goto out;
	}

	bfound = -1;
	bend = au_ibend(inode);
	bstart = au_ibstart(inode);
#if 0 /* reserved for future use */
	if (bindex == bend) {
		/* keep this ino in rename case */
		goto out;
	}
#endif
	for (bindex = bstart; bindex <= bend; bindex++)
		if (au_h_iptr(inode, bindex) == h_inode) {
			bfound = bindex;
			break;
		}
	if (bfound < 0)
		goto out;

	for (bindex = bstart; bindex <= bend; bindex++) {
		h_i = au_h_iptr(inode, bindex);
		if (!h_i)
			continue;

		err = au_xino_write(inode->i_sb, bindex, h_i->i_ino, /*ino*/0);
		/* ignore this error */
		/* bad action? */
	}

	/* children inode number will be broken */

out:
	AuTraceErr(err);
	return err;
}

static int hn_gen_tree(struct dentry *dentry)
{
	int err, i, j, ndentry;
	struct au_dcsub_pages dpages;
	struct au_dpage *dpage;
	struct dentry **dentries;

	err = au_dpages_init(&dpages, GFP_NOFS);
	if (unlikely(err))
		goto out;
	err = au_dcsub_pages(&dpages, dentry, NULL, NULL);
	if (unlikely(err))
		goto out_dpages;

	for (i = 0; i < dpages.ndpage; i++) {
		dpage = dpages.dpages + i;
		dentries = dpage->dentries;
		ndentry = dpage->ndentry;
		for (j = 0; j < ndentry; j++) {
			struct dentry *d;

			d = dentries[j];
			if (IS_ROOT(d))
				continue;

			au_digen_dec(d);
			if (d->d_inode)
				/* todo: reset children xino?
				   cached children only? */
				au_iigen_dec(d->d_inode);
		}
	}

out_dpages:
	au_dpages_free(&dpages);

#if 0
	/* discard children */
	dentry_unhash(dentry);
	dput(dentry);
#endif
out:
	return err;
}

/*
 * return 0 if processed.
 */
static int hn_gen_by_inode(char *name, unsigned int nlen, struct inode *inode,
			   const unsigned int isdir)
{
	int err;
	struct dentry *d;
	struct qstr *dname;

	err = 1;
	if (unlikely(inode->i_ino == AUFS_ROOT_INO)) {
		pr_warning("branch root dir was changed\n");
		err = 0;
		goto out;
	}

	if (!isdir) {
		AuDebugOn(!name);
		au_iigen_dec(inode);
		spin_lock(&inode->i_lock);
		list_for_each_entry(d, &inode->i_dentry, d_alias) {
			spin_lock(&d->d_lock);
			dname = &d->d_name;
			if (dname->len != nlen
			    && memcmp(dname->name, name, nlen)) {
				spin_unlock(&d->d_lock);
				continue;
			}
			err = 0;
			au_digen_dec(d);
			spin_unlock(&d->d_lock);
			break;
		}
		spin_unlock(&inode->i_lock);
	} else {
		au_fset_si(au_sbi(inode->i_sb), FAILED_REFRESH_DIR);
		d = d_find_alias(inode);
		if (!d) {
			au_iigen_dec(inode);
			goto out;
		}

		spin_lock(&d->d_lock);
		dname = &d->d_name;
		if (dname->len == nlen && !memcmp(dname->name, name, nlen)) {
			spin_unlock(&d->d_lock);
			err = hn_gen_tree(d);
			spin_lock(&d->d_lock);
		}
		spin_unlock(&d->d_lock);
		dput(d);
	}

out:
	AuTraceErr(err);
	return err;
}

static int hn_gen_by_name(struct dentry *dentry, const unsigned int isdir)
{
	int err;
	struct inode *inode;

	inode = dentry->d_inode;
	if (IS_ROOT(dentry)
	    /* || (inode && inode->i_ino == AUFS_ROOT_INO) */
		) {
		pr_warning("branch root dir was changed\n");
		return 0;
	}

	err = 0;
	if (!isdir) {
		au_digen_dec(dentry);
		if (inode)
			au_iigen_dec(inode);
	} else {
		au_fset_si(au_sbi(dentry->d_sb), FAILED_REFRESH_DIR);
		if (inode)
			err = hn_gen_tree(dentry);
	}

	AuTraceErr(err);
	return err;
}

/* ---------------------------------------------------------------------- */

/* hnotify job flags */
#define AuHnJob_XINO0		1
#define AuHnJob_GEN		(1 << 1)
#define AuHnJob_DIRENT		(1 << 2)
#define AuHnJob_ISDIR		(1 << 3)
#define AuHnJob_TRYXINO0	(1 << 4)
#define AuHnJob_MNTPNT		(1 << 5)
#define au_ftest_hnjob(flags, name)	((flags) & AuHnJob_##name)
#define au_fset_hnjob(flags, name) \
	do { (flags) |= AuHnJob_##name; } while (0)
#define au_fclr_hnjob(flags, name) \
	do { (flags) &= ~AuHnJob_##name; } while (0)

enum {
	AuHn_CHILD,
	AuHn_PARENT,
	AuHnLast
};

struct au_hnotify_args {
	struct inode *h_dir, *dir, *h_child_inode;
	u32 mask;
	unsigned int flags[AuHnLast];
	unsigned int h_child_nlen;
	char h_child_name[];
};

struct hn_job_args {
	unsigned int flags;
	struct inode *inode, *h_inode, *dir, *h_dir;
	struct dentry *dentry;
	char *h_name;
	int h_nlen;
};

static int hn_job(struct hn_job_args *a)
{
	const unsigned int isdir = au_ftest_hnjob(a->flags, ISDIR);

	/* reset xino */
	if (au_ftest_hnjob(a->flags, XINO0) && a->inode)
		hn_xino(a->inode, a->h_inode); /* ignore this error */

	if (au_ftest_hnjob(a->flags, TRYXINO0)
	    && a->inode
	    && a->h_inode) {
		mutex_lock_nested(&a->h_inode->i_mutex, AuLsc_I_CHILD);
		if (!a->h_inode->i_nlink)
			hn_xino(a->inode, a->h_inode); /* ignore this error */
		mutex_unlock(&a->h_inode->i_mutex);
	}

	/* make the generation obsolete */
	if (au_ftest_hnjob(a->flags, GEN)) {
		int err = -1;
		if (a->inode)
			err = hn_gen_by_inode(a->h_name, a->h_nlen, a->inode,
					      isdir);
		if (err && a->dentry)
			hn_gen_by_name(a->dentry, isdir);
		/* ignore this error */
	}

	/* make dir entries obsolete */
	if (au_ftest_hnjob(a->flags, DIRENT) && a->inode) {
		struct au_vdir *vdir;

		vdir = au_ivdir(a->inode);
		if (vdir)
			vdir->vd_jiffy = 0;
		/* IMustLock(a->inode); */
		/* a->inode->i_version++; */
	}

	/* can do nothing but warn */
	if (au_ftest_hnjob(a->flags, MNTPNT)
	    && a->dentry
	    && d_mountpoint(a->dentry))
		pr_warning("mount-point %.*s is removed or renamed\n",
			   AuDLNPair(a->dentry));

	return 0;
}

/* ---------------------------------------------------------------------- */

static struct dentry *lookup_wlock_by_name(char *name, unsigned int nlen,
					   struct inode *dir)
{
	struct dentry *dentry, *d, *parent;
	struct qstr *dname;

	parent = d_find_alias(dir);
	if (!parent)
		return NULL;

	dentry = NULL;
	spin_lock(&parent->d_lock);
	list_for_each_entry(d, &parent->d_subdirs, d_u.d_child) {
		/* AuDbg("%.*s\n", AuDLNPair(d)); */
		spin_lock_nested(&d->d_lock, DENTRY_D_LOCK_NESTED);
		dname = &d->d_name;
		if (dname->len != nlen || memcmp(dname->name, name, nlen))
			goto cont_unlock;
		if (au_di(d))
			au_digen_dec(d);
		else
			goto cont_unlock;
		if (d->d_count) {
			dentry = dget_dlock(d);
			spin_unlock(&d->d_lock);
			break;
		}

	cont_unlock:
		spin_unlock(&d->d_lock);
	}
	spin_unlock(&parent->d_lock);
	dput(parent);

	if (dentry)
		di_write_lock_child(dentry);

	return dentry;
}

static struct inode *lookup_wlock_by_ino(struct super_block *sb,
					 aufs_bindex_t bindex, ino_t h_ino)
{
	struct inode *inode;
	ino_t ino;
	int err;

	inode = NULL;
	err = au_xino_read(sb, bindex, h_ino, &ino);
	if (!err && ino)
		inode = ilookup(sb, ino);
	if (!inode)
		goto out;

	if (unlikely(inode->i_ino == AUFS_ROOT_INO)) {
		pr_warning("wrong root branch\n");
		iput(inode);
		inode = NULL;
		goto out;
	}

	ii_write_lock_child(inode);

out:
	return inode;
}

static void au_hn_bh(void *_args)
{
	struct au_hnotify_args *a = _args;
	struct super_block *sb;
	aufs_bindex_t bindex, bend, bfound;
	unsigned char xino, try_iput;
	int err;
	struct inode *inode;
	ino_t h_ino;
	struct hn_job_args args;
	struct dentry *dentry;
	struct au_sbinfo *sbinfo;

	AuDebugOn(!_args);
	AuDebugOn(!a->h_dir);
	AuDebugOn(!a->dir);
	AuDebugOn(!a->mask);
	AuDbg("mask 0x%x, i%lu, hi%lu, hci%lu\n",
	      a->mask, a->dir->i_ino, a->h_dir->i_ino,
	      a->h_child_inode ? a->h_child_inode->i_ino : 0);

	inode = NULL;
	dentry = NULL;
	/*
	 * do not lock a->dir->i_mutex here
	 * because of d_revalidate() may cause a deadlock.
	 */
	sb = a->dir->i_sb;
	AuDebugOn(!sb);
	sbinfo = au_sbi(sb);
	AuDebugOn(!sbinfo);
	si_write_lock(sb, AuLock_NOPLMW);

	ii_read_lock_parent(a->dir);
	bfound = -1;
	bend = au_ibend(a->dir);
	for (bindex = au_ibstart(a->dir); bindex <= bend; bindex++)
		if (au_h_iptr(a->dir, bindex) == a->h_dir) {
			bfound = bindex;
			break;
		}
	ii_read_unlock(a->dir);
	if (unlikely(bfound < 0))
		goto out;

	xino = !!au_opt_test(au_mntflags(sb), XINO);
	h_ino = 0;
	if (a->h_child_inode)
		h_ino = a->h_child_inode->i_ino;

	if (a->h_child_nlen
	    && (au_ftest_hnjob(a->flags[AuHn_CHILD], GEN)
		|| au_ftest_hnjob(a->flags[AuHn_CHILD], MNTPNT)))
		dentry = lookup_wlock_by_name(a->h_child_name, a->h_child_nlen,
					      a->dir);
	try_iput = 0;
	if (dentry)
		inode = dentry->d_inode;
	if (xino && !inode && h_ino
	    && (au_ftest_hnjob(a->flags[AuHn_CHILD], XINO0)
		|| au_ftest_hnjob(a->flags[AuHn_CHILD], TRYXINO0)
		|| au_ftest_hnjob(a->flags[AuHn_CHILD], GEN))) {
		inode = lookup_wlock_by_ino(sb, bfound, h_ino);
		try_iput = 1;
	    }

	args.flags = a->flags[AuHn_CHILD];
	args.dentry = dentry;
	args.inode = inode;
	args.h_inode = a->h_child_inode;
	args.dir = a->dir;
	args.h_dir = a->h_dir;
	args.h_name = a->h_child_name;
	args.h_nlen = a->h_child_nlen;
	err = hn_job(&args);
	if (dentry) {
		if (au_di(dentry))
			di_write_unlock(dentry);
		dput(dentry);
	}
	if (inode && try_iput) {
		ii_write_unlock(inode);
		iput(inode);
	}

	ii_write_lock_parent(a->dir);
	args.flags = a->flags[AuHn_PARENT];
	args.dentry = NULL;
	args.inode = a->dir;
	args.h_inode = a->h_dir;
	args.dir = NULL;
	args.h_dir = NULL;
	args.h_name = NULL;
	args.h_nlen = 0;
	err = hn_job(&args);
	ii_write_unlock(a->dir);

out:
	iput(a->h_child_inode);
	iput(a->h_dir);
	iput(a->dir);
	si_write_unlock(sb);
	au_nwt_done(&sbinfo->si_nowait);
	kfree(a);
}

/* ---------------------------------------------------------------------- */

int au_hnotify(struct inode *h_dir, struct au_hnotify *hnotify, u32 mask,
	       struct qstr *h_child_qstr, struct inode *h_child_inode)
{
	int err, len;
	unsigned int flags[AuHnLast], f;
	unsigned char isdir, isroot, wh;
	struct inode *dir;
	struct au_hnotify_args *args;
	char *p, *h_child_name;

	err = 0;
	AuDebugOn(!hnotify || !hnotify->hn_aufs_inode);
	dir = igrab(hnotify->hn_aufs_inode);
	if (!dir)
		goto out;

	isroot = (dir->i_ino == AUFS_ROOT_INO);
	wh = 0;
	h_child_name = (void *)h_child_qstr->name;
	len = h_child_qstr->len;
	if (h_child_name) {
		if (len > AUFS_WH_PFX_LEN
		    && !memcmp(h_child_name, AUFS_WH_PFX, AUFS_WH_PFX_LEN)) {
			h_child_name += AUFS_WH_PFX_LEN;
			len -= AUFS_WH_PFX_LEN;
			wh = 1;
		}
	}

	isdir = 0;
	if (h_child_inode)
		isdir = !!S_ISDIR(h_child_inode->i_mode);
	flags[AuHn_PARENT] = AuHnJob_ISDIR;
	flags[AuHn_CHILD] = 0;
	if (isdir)
		flags[AuHn_CHILD] = AuHnJob_ISDIR;
	au_fset_hnjob(flags[AuHn_PARENT], DIRENT);
	au_fset_hnjob(flags[AuHn_CHILD], GEN);
	switch (mask & FS_EVENTS_POSS_ON_CHILD) {
	case FS_MOVED_FROM:
	case FS_MOVED_TO:
		au_fset_hnjob(flags[AuHn_CHILD], XINO0);
		au_fset_hnjob(flags[AuHn_CHILD], MNTPNT);
		/*FALLTHROUGH*/
	case FS_CREATE:
		AuDebugOn(!h_child_name || !h_child_inode);
		break;

	case FS_DELETE:
		/*
		 * aufs never be able to get this child inode.
		 * revalidation should be in d_revalidate()
		 * by checking i_nlink, i_generation or d_unhashed().
		 */
		AuDebugOn(!h_child_name);
		au_fset_hnjob(flags[AuHn_CHILD], TRYXINO0);
		au_fset_hnjob(flags[AuHn_CHILD], MNTPNT);
		break;

	default:
		AuDebugOn(1);
	}

	if (wh)
		h_child_inode = NULL;

	err = -ENOMEM;
	/* iput() and kfree() will be called in au_hnotify() */
	args = kmalloc(sizeof(*args) + len + 1, GFP_NOFS);
	if (unlikely(!args)) {
		AuErr1("no memory\n");
		iput(dir);
		goto out;
	}
	args->flags[AuHn_PARENT] = flags[AuHn_PARENT];
	args->flags[AuHn_CHILD] = flags[AuHn_CHILD];
	args->mask = mask;
	args->dir = dir;
	args->h_dir = igrab(h_dir);
	if (h_child_inode)
		h_child_inode = igrab(h_child_inode); /* can be NULL */
	args->h_child_inode = h_child_inode;
	args->h_child_nlen = len;
	if (len) {
		p = (void *)args;
		p += sizeof(*args);
		memcpy(p, h_child_name, len);
		p[len] = 0;
	}

	f = 0;
	if (!dir->i_nlink)
		f = AuWkq_NEST;
	err = au_wkq_nowait(au_hn_bh, args, dir->i_sb, f);
	if (unlikely(err)) {
		pr_err("wkq %d\n", err);
		iput(args->h_child_inode);
		iput(args->h_dir);
		iput(args->dir);
		kfree(args);
	}

out:
	return err;
}

/* ---------------------------------------------------------------------- */

int au_hnotify_reset_br(unsigned int udba, struct au_branch *br, int perm)
{
	int err;

	AuDebugOn(!(udba & AuOptMask_UDBA));

	err = 0;
	if (au_hnotify_op.reset_br)
		err = au_hnotify_op.reset_br(udba, br, perm);

	return err;
}

int au_hnotify_init_br(struct au_branch *br, int perm)
{
	int err;

	err = 0;
	if (au_hnotify_op.init_br)
		err = au_hnotify_op.init_br(br, perm);

	return err;
}

void au_hnotify_fin_br(struct au_branch *br)
{
	if (au_hnotify_op.fin_br)
		au_hnotify_op.fin_br(br);
}

static void au_hn_destroy_cache(void)
{
	kmem_cache_destroy(au_cachep[AuCache_HNOTIFY]);
	au_cachep[AuCache_HNOTIFY] = NULL;
}

int __init au_hnotify_init(void)
{
	int err;

	err = -ENOMEM;
	au_cachep[AuCache_HNOTIFY] = AuCache(au_hnotify);
	if (au_cachep[AuCache_HNOTIFY]) {
		err = 0;
		if (au_hnotify_op.init)
			err = au_hnotify_op.init();
		if (unlikely(err))
			au_hn_destroy_cache();
	}
	AuTraceErr(err);
	return err;
}

void au_hnotify_fin(void)
{
	if (au_hnotify_op.fin)
		au_hnotify_op.fin();
	/* cf. au_cache_fin() */
	if (au_cachep[AuCache_HNOTIFY])
		au_hn_destroy_cache();
}
