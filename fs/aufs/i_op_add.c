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
 * inode operations (add entry)
 */

#include "aufs.h"

/*
 * final procedure of adding a new entry, except link(2).
 * remove whiteout, instantiate, copyup the parent dir's times and size
 * and update version.
 * if it failed, re-create the removed whiteout.
 */
static int epilog(struct inode *dir, aufs_bindex_t bindex,
		  struct dentry *wh_dentry, struct dentry *dentry)
{
	int err, rerr;
	aufs_bindex_t bwh;
	struct path h_path;
	struct inode *inode, *h_dir;
	struct dentry *wh;

	bwh = -1;
	if (wh_dentry) {
		h_dir = wh_dentry->d_parent->d_inode; /* dir inode is locked */
		IMustLock(h_dir);
		AuDebugOn(au_h_iptr(dir, bindex) != h_dir);
		bwh = au_dbwh(dentry);
		h_path.dentry = wh_dentry;
		h_path.mnt = au_sbr_mnt(dir->i_sb, bindex);
		err = au_wh_unlink_dentry(au_h_iptr(dir, bindex), &h_path,
					  dentry);
		if (unlikely(err))
			goto out;
	}

	inode = au_new_inode(dentry, /*must_new*/1);
	if (!IS_ERR(inode)) {
		d_instantiate(dentry, inode);
		dir = dentry->d_parent->d_inode; /* dir inode is locked */
		IMustLock(dir);
		if (au_ibstart(dir) == au_dbstart(dentry))
			au_cpup_attr_timesizes(dir);
		dir->i_version++;
		return 0; /* success */
	}

	err = PTR_ERR(inode);
	if (!wh_dentry)
		goto out;

	/* revert */
	/* dir inode is locked */
	wh = au_wh_create(dentry, bwh, wh_dentry->d_parent);
	rerr = PTR_ERR(wh);
	if (IS_ERR(wh)) {
		AuIOErr("%.*s reverting whiteout failed(%d, %d)\n",
			AuDLNPair(dentry), err, rerr);
		err = -EIO;
	} else
		dput(wh);

out:
	return err;
}

static int au_d_may_add(struct dentry *dentry)
{
	int err;

	err = 0;
	if (unlikely(d_unhashed(dentry)))
		err = -ENOENT;
	if (unlikely(dentry->d_inode))
		err = -EEXIST;
	return err;
}

/*
 * simple tests for the adding inode operations.
 * following the checks in vfs, plus the parent-child relationship.
 */
int au_may_add(struct dentry *dentry, aufs_bindex_t bindex,
	       struct dentry *h_parent, int isdir)
{
	int err;
	umode_t h_mode;
	struct dentry *h_dentry;
	struct inode *h_inode;

	err = -ENAMETOOLONG;
	if (unlikely(dentry->d_name.len > AUFS_MAX_NAMELEN))
		goto out;

	h_dentry = au_h_dptr(dentry, bindex);
	h_inode = h_dentry->d_inode;
	if (!dentry->d_inode) {
		err = -EEXIST;
		if (unlikely(h_inode))
			goto out;
	} else {
		/* rename(2) case */
		err = -EIO;
		if (unlikely(!h_inode || !h_inode->i_nlink))
			goto out;

		h_mode = h_inode->i_mode;
		if (!isdir) {
			err = -EISDIR;
			if (unlikely(S_ISDIR(h_mode)))
				goto out;
		} else if (unlikely(!S_ISDIR(h_mode))) {
			err = -ENOTDIR;
			goto out;
		}
	}

	err = 0;
	/* expected parent dir is locked */
	if (unlikely(h_parent != h_dentry->d_parent))
		err = -EIO;

out:
	AuTraceErr(err);
	return err;
}

/*
 * initial procedure of adding a new entry.
 * prepare writable branch and the parent dir, lock it,
 * and lookup whiteout for the new entry.
 */
static struct dentry*
lock_hdir_lkup_wh(struct dentry *dentry, struct au_dtime *dt,
		  struct dentry *src_dentry, struct au_pin *pin,
		  struct au_wr_dir_args *wr_dir_args)
{
	struct dentry *wh_dentry, *h_parent;
	struct super_block *sb;
	struct au_branch *br;
	int err;
	unsigned int udba;
	aufs_bindex_t bcpup;

	AuDbg("%.*s\n", AuDLNPair(dentry));

	err = au_wr_dir(dentry, src_dentry, wr_dir_args);
	bcpup = err;
	wh_dentry = ERR_PTR(err);
	if (unlikely(err < 0))
		goto out;

	sb = dentry->d_sb;
	udba = au_opt_udba(sb);
	err = au_pin(pin, dentry, bcpup, udba,
		     AuPin_DI_LOCKED | AuPin_MNT_WRITE);
	wh_dentry = ERR_PTR(err);
	if (unlikely(err))
		goto out;

	h_parent = au_pinned_h_parent(pin);
	if (udba != AuOpt_UDBA_NONE
	    && au_dbstart(dentry) == bcpup)
		err = au_may_add(dentry, bcpup, h_parent,
				 au_ftest_wrdir(wr_dir_args->flags, ISDIR));
	else if (unlikely(dentry->d_name.len > AUFS_MAX_NAMELEN))
		err = -ENAMETOOLONG;
	wh_dentry = ERR_PTR(err);
	if (unlikely(err))
		goto out_unpin;

	br = au_sbr(sb, bcpup);
	if (dt) {
		struct path tmp = {
			.dentry	= h_parent,
			.mnt	= br->br_mnt
		};
		au_dtime_store(dt, au_pinned_parent(pin), &tmp);
	}

	wh_dentry = NULL;
	if (bcpup != au_dbwh(dentry))
		goto out; /* success */

	wh_dentry = au_wh_lkup(h_parent, &dentry->d_name, br);

out_unpin:
	if (IS_ERR(wh_dentry))
		au_unpin(pin);
out:
	return wh_dentry;
}

/* ---------------------------------------------------------------------- */

enum { Mknod, Symlink, Creat };
struct simple_arg {
	int type;
	union {
		struct {
			int mode;
			struct nameidata *nd;
		} c;
		struct {
			const char *symname;
		} s;
		struct {
			int mode;
			dev_t dev;
		} m;
	} u;
};

static int add_simple(struct inode *dir, struct dentry *dentry,
		      struct simple_arg *arg)
{
	int err;
	aufs_bindex_t bstart;
	unsigned char created;
	struct au_dtime dt;
	struct au_pin pin;
	struct path h_path;
	struct dentry *wh_dentry, *parent;
	struct inode *h_dir;
	struct au_wr_dir_args wr_dir_args = {
		.force_btgt	= -1,
		.flags		= AuWrDir_ADD_ENTRY
	};

	AuDbg("%.*s\n", AuDLNPair(dentry));
	IMustLock(dir);

	parent = dentry->d_parent; /* dir inode is locked */
	err = aufs_read_lock(dentry, AuLock_DW | AuLock_GEN);
	if (unlikely(err))
		goto out;
	err = au_d_may_add(dentry);
	if (unlikely(err))
		goto out_unlock;
	di_write_lock_parent(parent);
	wh_dentry = lock_hdir_lkup_wh(dentry, &dt, /*src_dentry*/NULL, &pin,
				      &wr_dir_args);
	err = PTR_ERR(wh_dentry);
	if (IS_ERR(wh_dentry))
		goto out_parent;

	bstart = au_dbstart(dentry);
	h_path.dentry = au_h_dptr(dentry, bstart);
	h_path.mnt = au_sbr_mnt(dentry->d_sb, bstart);
	h_dir = au_pinned_h_dir(&pin);
	switch (arg->type) {
	case Creat:
		err = vfsub_create(h_dir, &h_path, arg->u.c.mode);
		break;
	case Symlink:
		err = vfsub_symlink(h_dir, &h_path, arg->u.s.symname);
		break;
	case Mknod:
		err = vfsub_mknod(h_dir, &h_path, arg->u.m.mode, arg->u.m.dev);
		break;
	default:
		BUG();
	}
	created = !err;
	if (!err)
		err = epilog(dir, bstart, wh_dentry, dentry);

	/* revert */
	if (unlikely(created && err && h_path.dentry->d_inode)) {
		int rerr;
		rerr = vfsub_unlink(h_dir, &h_path, /*force*/0);
		if (rerr) {
			AuIOErr("%.*s revert failure(%d, %d)\n",
				AuDLNPair(dentry), err, rerr);
			err = -EIO;
		}
		au_dtime_revert(&dt);
	}

	au_unpin(&pin);
	dput(wh_dentry);

out_parent:
	di_write_unlock(parent);
out_unlock:
	if (unlikely(err)) {
		au_update_dbstart(dentry);
		d_drop(dentry);
	}
	aufs_read_unlock(dentry, AuLock_DW);
out:
	return err;
}

int aufs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	struct simple_arg arg = {
		.type = Mknod,
		.u.m = {
			.mode	= mode,
			.dev	= dev
		}
	};
	return add_simple(dir, dentry, &arg);
}

int aufs_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	struct simple_arg arg = {
		.type = Symlink,
		.u.s.symname = symname
	};
	return add_simple(dir, dentry, &arg);
}

int aufs_create(struct inode *dir, struct dentry *dentry, int mode,
		struct nameidata *nd)
{
	struct simple_arg arg = {
		.type = Creat,
		.u.c = {
			.mode	= mode,
			.nd	= nd
		}
	};
	return add_simple(dir, dentry, &arg);
}

/* ---------------------------------------------------------------------- */

struct au_link_args {
	aufs_bindex_t bdst, bsrc;
	struct au_pin pin;
	struct path h_path;
	struct dentry *src_parent, *parent;
};

static int au_cpup_before_link(struct dentry *src_dentry,
			       struct au_link_args *a)
{
	int err;
	struct dentry *h_src_dentry;
	struct mutex *h_mtx;
	struct file *h_file;

	di_read_lock_parent(a->src_parent, AuLock_IR);
	err = au_test_and_cpup_dirs(src_dentry, a->bdst);
	if (unlikely(err))
		goto out;

	h_src_dentry = au_h_dptr(src_dentry, a->bsrc);
	h_mtx = &h_src_dentry->d_inode->i_mutex;
	err = au_pin(&a->pin, src_dentry, a->bdst,
		     au_opt_udba(src_dentry->d_sb),
		     AuPin_DI_LOCKED | AuPin_MNT_WRITE);
	if (unlikely(err))
		goto out;
	mutex_lock_nested(h_mtx, AuLsc_I_CHILD);
	h_file = au_h_open_pre(src_dentry, a->bsrc);
	if (IS_ERR(h_file)) {
		err = PTR_ERR(h_file);
		h_file = NULL;
	} else
		err = au_sio_cpup_simple(src_dentry, a->bdst, -1,
					 AuCpup_DTIME /* | AuCpup_KEEPLINO */);
	mutex_unlock(h_mtx);
	au_h_open_post(src_dentry, a->bsrc, h_file);
	au_unpin(&a->pin);

out:
	di_read_unlock(a->src_parent, AuLock_IR);
	return err;
}

static int au_cpup_or_link(struct dentry *src_dentry, struct au_link_args *a)
{
	int err;
	unsigned char plink;
	struct inode *h_inode, *inode;
	struct dentry *h_src_dentry;
	struct super_block *sb;
	struct file *h_file;

	plink = 0;
	h_inode = NULL;
	sb = src_dentry->d_sb;
	inode = src_dentry->d_inode;
	if (au_ibstart(inode) <= a->bdst)
		h_inode = au_h_iptr(inode, a->bdst);
	if (!h_inode || !h_inode->i_nlink) {
		/* copyup src_dentry as the name of dentry. */
		au_set_dbstart(src_dentry, a->bdst);
		au_set_h_dptr(src_dentry, a->bdst, dget(a->h_path.dentry));
		h_inode = au_h_dptr(src_dentry, a->bsrc)->d_inode;
		mutex_lock_nested(&h_inode->i_mutex, AuLsc_I_CHILD);
		h_file = au_h_open_pre(src_dentry, a->bsrc);
		if (IS_ERR(h_file)) {
			err = PTR_ERR(h_file);
			h_file = NULL;
		} else
			err = au_sio_cpup_single(src_dentry, a->bdst, a->bsrc,
						 -1, AuCpup_KEEPLINO,
						 a->parent);
		mutex_unlock(&h_inode->i_mutex);
		au_h_open_post(src_dentry, a->bsrc, h_file);
		au_set_h_dptr(src_dentry, a->bdst, NULL);
		au_set_dbstart(src_dentry, a->bsrc);
	} else {
		/* the inode of src_dentry already exists on a.bdst branch */
		h_src_dentry = d_find_alias(h_inode);
		if (!h_src_dentry && au_plink_test(inode)) {
			plink = 1;
			h_src_dentry = au_plink_lkup(inode, a->bdst);
			err = PTR_ERR(h_src_dentry);
			if (IS_ERR(h_src_dentry))
				goto out;

			if (unlikely(!h_src_dentry->d_inode)) {
				dput(h_src_dentry);
				h_src_dentry = NULL;
			}

		}
		if (h_src_dentry) {
			err = vfsub_link(h_src_dentry, au_pinned_h_dir(&a->pin),
					 &a->h_path);
			dput(h_src_dentry);
		} else {
			AuIOErr("no dentry found for hi%lu on b%d\n",
				h_inode->i_ino, a->bdst);
			err = -EIO;
		}
	}

	if (!err && !plink)
		au_plink_append(inode, a->bdst, a->h_path.dentry);

out:
	AuTraceErr(err);
	return err;
}

int aufs_link(struct dentry *src_dentry, struct inode *dir,
	      struct dentry *dentry)
{
	int err, rerr;
	struct au_dtime dt;
	struct au_link_args *a;
	struct dentry *wh_dentry, *h_src_dentry;
	struct inode *inode;
	struct super_block *sb;
	struct au_wr_dir_args wr_dir_args = {
		/* .force_btgt	= -1, */
		.flags		= AuWrDir_ADD_ENTRY
	};

	IMustLock(dir);
	inode = src_dentry->d_inode;
	IMustLock(inode);

	err = -ENOMEM;
	a = kzalloc(sizeof(*a), GFP_NOFS);
	if (unlikely(!a))
		goto out;

	a->parent = dentry->d_parent; /* dir inode is locked */
	err = aufs_read_and_write_lock2(dentry, src_dentry,
					AuLock_NOPLM | AuLock_GEN);
	if (unlikely(err))
		goto out_kfree;
	err = au_d_hashed_positive(src_dentry);
	if (unlikely(err))
		goto out_unlock;
	err = au_d_may_add(dentry);
	if (unlikely(err))
		goto out_unlock;

	a->src_parent = dget_parent(src_dentry);
	wr_dir_args.force_btgt = au_ibstart(inode);

	di_write_lock_parent(a->parent);
	wr_dir_args.force_btgt = au_wbr(dentry, wr_dir_args.force_btgt);
	wh_dentry = lock_hdir_lkup_wh(dentry, &dt, src_dentry, &a->pin,
				      &wr_dir_args);
	err = PTR_ERR(wh_dentry);
	if (IS_ERR(wh_dentry))
		goto out_parent;

	err = 0;
	sb = dentry->d_sb;
	a->bdst = au_dbstart(dentry);
	a->h_path.dentry = au_h_dptr(dentry, a->bdst);
	a->h_path.mnt = au_sbr_mnt(sb, a->bdst);
	a->bsrc = au_ibstart(inode);
	h_src_dentry = au_h_d_alias(src_dentry, a->bsrc);
	if (!h_src_dentry) {
		a->bsrc = au_dbstart(src_dentry);
		h_src_dentry = au_h_d_alias(src_dentry, a->bsrc);
		AuDebugOn(!h_src_dentry);
	} else if (IS_ERR(h_src_dentry))
		goto out_parent;

	if (au_opt_test(au_mntflags(sb), PLINK)) {
		if (a->bdst < a->bsrc
		    /* && h_src_dentry->d_sb != a->h_path.dentry->d_sb */)
			err = au_cpup_or_link(src_dentry, a);
		else
			err = vfsub_link(h_src_dentry, au_pinned_h_dir(&a->pin),
					 &a->h_path);
		dput(h_src_dentry);
	} else {
		/*
		 * copyup src_dentry to the branch we process,
		 * and then link(2) to it.
		 */
		dput(h_src_dentry);
		if (a->bdst < a->bsrc
		    /* && h_src_dentry->d_sb != a->h_path.dentry->d_sb */) {
			au_unpin(&a->pin);
			di_write_unlock(a->parent);
			err = au_cpup_before_link(src_dentry, a);
			di_write_lock_parent(a->parent);
			if (!err)
				err = au_pin(&a->pin, dentry, a->bdst,
					     au_opt_udba(sb),
					     AuPin_DI_LOCKED | AuPin_MNT_WRITE);
			if (unlikely(err))
				goto out_wh;
		}
		if (!err) {
			h_src_dentry = au_h_dptr(src_dentry, a->bdst);
			err = -ENOENT;
			if (h_src_dentry && h_src_dentry->d_inode)
				err = vfsub_link(h_src_dentry,
						 au_pinned_h_dir(&a->pin),
						 &a->h_path);
		}
	}
	if (unlikely(err))
		goto out_unpin;

	if (wh_dentry) {
		a->h_path.dentry = wh_dentry;
		err = au_wh_unlink_dentry(au_pinned_h_dir(&a->pin), &a->h_path,
					  dentry);
		if (unlikely(err))
			goto out_revert;
	}

	dir->i_version++;
	if (au_ibstart(dir) == au_dbstart(dentry))
		au_cpup_attr_timesizes(dir);
	inc_nlink(inode);
	inode->i_ctime = dir->i_ctime;
	d_instantiate(dentry, au_igrab(inode));
	if (d_unhashed(a->h_path.dentry))
		/* some filesystem calls d_drop() */
		d_drop(dentry);
	goto out_unpin; /* success */

out_revert:
	rerr = vfsub_unlink(au_pinned_h_dir(&a->pin), &a->h_path, /*force*/0);
	if (unlikely(rerr)) {
		AuIOErr("%.*s reverting failed(%d, %d)\n",
			AuDLNPair(dentry), err, rerr);
		err = -EIO;
	}
	au_dtime_revert(&dt);
out_unpin:
	au_unpin(&a->pin);
out_wh:
	dput(wh_dentry);
out_parent:
	di_write_unlock(a->parent);
	dput(a->src_parent);
out_unlock:
	if (unlikely(err)) {
		au_update_dbstart(dentry);
		d_drop(dentry);
	}
	aufs_read_and_write_unlock2(dentry, src_dentry);
out_kfree:
	kfree(a);
out:
	return err;
}

int aufs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int err, rerr;
	aufs_bindex_t bindex;
	unsigned char diropq;
	struct path h_path;
	struct dentry *wh_dentry, *parent, *opq_dentry;
	struct mutex *h_mtx;
	struct super_block *sb;
	struct {
		struct au_pin pin;
		struct au_dtime dt;
	} *a; /* reduce the stack usage */
	struct au_wr_dir_args wr_dir_args = {
		.force_btgt	= -1,
		.flags		= AuWrDir_ADD_ENTRY | AuWrDir_ISDIR
	};

	IMustLock(dir);

	err = -ENOMEM;
	a = kmalloc(sizeof(*a), GFP_NOFS);
	if (unlikely(!a))
		goto out;

	err = aufs_read_lock(dentry, AuLock_DW | AuLock_GEN);
	if (unlikely(err))
		goto out_free;
	err = au_d_may_add(dentry);
	if (unlikely(err))
		goto out_unlock;

	parent = dentry->d_parent; /* dir inode is locked */
	di_write_lock_parent(parent);
	wh_dentry = lock_hdir_lkup_wh(dentry, &a->dt, /*src_dentry*/NULL,
				      &a->pin, &wr_dir_args);
	err = PTR_ERR(wh_dentry);
	if (IS_ERR(wh_dentry))
		goto out_parent;

	sb = dentry->d_sb;
	bindex = au_dbstart(dentry);
	h_path.dentry = au_h_dptr(dentry, bindex);
	h_path.mnt = au_sbr_mnt(sb, bindex);
	err = vfsub_mkdir(au_pinned_h_dir(&a->pin), &h_path, mode);
	if (unlikely(err))
		goto out_unpin;

	/* make the dir opaque */
	diropq = 0;
	h_mtx = &h_path.dentry->d_inode->i_mutex;
	if (wh_dentry
	    || au_opt_test(au_mntflags(sb), ALWAYS_DIROPQ)) {
		mutex_lock_nested(h_mtx, AuLsc_I_CHILD);
		opq_dentry = au_diropq_create(dentry, bindex);
		mutex_unlock(h_mtx);
		err = PTR_ERR(opq_dentry);
		if (IS_ERR(opq_dentry))
			goto out_dir;
		dput(opq_dentry);
		diropq = 1;
	}

	err = epilog(dir, bindex, wh_dentry, dentry);
	if (!err) {
		inc_nlink(dir);
		goto out_unpin; /* success */
	}

	/* revert */
	if (diropq) {
		AuLabel(revert opq);
		mutex_lock_nested(h_mtx, AuLsc_I_CHILD);
		rerr = au_diropq_remove(dentry, bindex);
		mutex_unlock(h_mtx);
		if (rerr) {
			AuIOErr("%.*s reverting diropq failed(%d, %d)\n",
				AuDLNPair(dentry), err, rerr);
			err = -EIO;
		}
	}

out_dir:
	AuLabel(revert dir);
	rerr = vfsub_rmdir(au_pinned_h_dir(&a->pin), &h_path);
	if (rerr) {
		AuIOErr("%.*s reverting dir failed(%d, %d)\n",
			AuDLNPair(dentry), err, rerr);
		err = -EIO;
	}
	au_dtime_revert(&a->dt);
out_unpin:
	au_unpin(&a->pin);
	dput(wh_dentry);
out_parent:
	di_write_unlock(parent);
out_unlock:
	if (unlikely(err)) {
		au_update_dbstart(dentry);
		d_drop(dentry);
	}
	aufs_read_unlock(dentry, AuLock_DW);
out_free:
	kfree(a);
out:
	return err;
}
