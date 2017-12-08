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
	struct super_block *sb;
	struct inode *inode, *h_dir;
	struct dentry *wh;

	bwh = -1;
	sb = dir->i_sb;
	if (wh_dentry) {
		h_dir = d_inode(wh_dentry->d_parent); /* dir inode is locked */
		IMustLock(h_dir);
		AuDebugOn(au_h_iptr(dir, bindex) != h_dir);
		bwh = au_dbwh(dentry);
		h_path.dentry = wh_dentry;
		h_path.mnt = au_sbr_mnt(sb, bindex);
		err = au_wh_unlink_dentry(au_h_iptr(dir, bindex), &h_path,
					  dentry);
		if (unlikely(err))
			goto out;
	}

	inode = au_new_inode(dentry, /*must_new*/1);
	if (!IS_ERR(inode)) {
		d_instantiate(dentry, inode);
		dir = d_inode(dentry->d_parent); /* dir inode is locked */
		IMustLock(dir);
		au_dir_ts(dir, bindex);
		dir->i_version++;
		au_fhsm_wrote(sb, bindex, /*force*/0);
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
		AuIOErr("%pd reverting whiteout failed(%d, %d)\n",
			dentry, err, rerr);
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
	if (unlikely(d_really_is_positive(dentry)))
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
	if (d_really_is_negative(dentry)) {
		err = -EEXIST;
		if (unlikely(d_is_positive(h_dentry)))
			goto out;
	} else {
		/* rename(2) case */
		err = -EIO;
		if (unlikely(d_is_negative(h_dentry)))
			goto out;
		h_inode = d_inode(h_dentry);
		if (unlikely(!h_inode->i_nlink))
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

	AuDbg("%pd\n", dentry);

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
	    && au_dbtop(dentry) == bcpup)
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
			.mnt	= au_br_mnt(br)
		};
		au_dtime_store(dt, au_pinned_parent(pin), &tmp);
	}

	wh_dentry = NULL;
	if (bcpup != au_dbwh(dentry))
		goto out; /* success */

	/*
	 * ENAMETOOLONG here means that if we allowed create such name, then it
	 * would not be able to removed in the future. So we don't allow such
	 * name here and we don't handle ENAMETOOLONG differently here.
	 */
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
			umode_t			mode;
			bool			want_excl;
			bool			try_aopen;
			struct vfsub_aopen_args	*aopen;
		} c;
		struct {
			const char *symname;
		} s;
		struct {
			umode_t mode;
			dev_t dev;
		} m;
	} u;
};

static int add_simple(struct inode *dir, struct dentry *dentry,
		      struct simple_arg *arg)
{
	int err, rerr;
	aufs_bindex_t btop;
	unsigned char created;
	const unsigned char try_aopen
		= (arg->type == Creat && arg->u.c.try_aopen);
	struct dentry *wh_dentry, *parent;
	struct inode *h_dir;
	struct super_block *sb;
	struct au_branch *br;
	/* to reuduce stack size */
	struct {
		struct au_dtime dt;
		struct au_pin pin;
		struct path h_path;
		struct au_wr_dir_args wr_dir_args;
	} *a;

	AuDbg("%pd\n", dentry);
	IMustLock(dir);

	err = -ENOMEM;
	a = kmalloc(sizeof(*a), GFP_NOFS);
	if (unlikely(!a))
		goto out;
	a->wr_dir_args.force_btgt = -1;
	a->wr_dir_args.flags = AuWrDir_ADD_ENTRY;

	parent = dentry->d_parent; /* dir inode is locked */
	if (!try_aopen) {
		err = aufs_read_lock(dentry, AuLock_DW | AuLock_GEN);
		if (unlikely(err))
			goto out_free;
	}
	err = au_d_may_add(dentry);
	if (unlikely(err))
		goto out_unlock;
	if (!try_aopen)
		di_write_lock_parent(parent);
	wh_dentry = lock_hdir_lkup_wh(dentry, &a->dt, /*src_dentry*/NULL,
				      &a->pin, &a->wr_dir_args);
	err = PTR_ERR(wh_dentry);
	if (IS_ERR(wh_dentry))
		goto out_parent;

	btop = au_dbtop(dentry);
	sb = dentry->d_sb;
	br = au_sbr(sb, btop);
	a->h_path.dentry = au_h_dptr(dentry, btop);
	a->h_path.mnt = au_br_mnt(br);
	h_dir = au_pinned_h_dir(&a->pin);
	switch (arg->type) {
	case Creat:
		err = 0;
		if (!try_aopen || !h_dir->i_op->atomic_open)
			err = vfsub_create(h_dir, &a->h_path, arg->u.c.mode,
					   arg->u.c.want_excl);
		else
			err = vfsub_atomic_open(h_dir, a->h_path.dentry,
						arg->u.c.aopen, br);
		break;
	case Symlink:
		err = vfsub_symlink(h_dir, &a->h_path, arg->u.s.symname);
		break;
	case Mknod:
		err = vfsub_mknod(h_dir, &a->h_path, arg->u.m.mode,
				  arg->u.m.dev);
		break;
	default:
		BUG();
	}
	created = !err;
	if (!err)
		err = epilog(dir, btop, wh_dentry, dentry);

	/* revert */
	if (unlikely(created && err && d_is_positive(a->h_path.dentry))) {
		/* no delegation since it is just created */
		rerr = vfsub_unlink(h_dir, &a->h_path, /*delegated*/NULL,
				    /*force*/0);
		if (rerr) {
			AuIOErr("%pd revert failure(%d, %d)\n",
				dentry, err, rerr);
			err = -EIO;
		}
		au_dtime_revert(&a->dt);
	}

	if (!err && try_aopen && !h_dir->i_op->atomic_open)
		*arg->u.c.aopen->opened |= FILE_CREATED;

	au_unpin(&a->pin);
	dput(wh_dentry);

out_parent:
	if (!try_aopen)
		di_write_unlock(parent);
out_unlock:
	if (unlikely(err)) {
		au_update_dbtop(dentry);
		d_drop(dentry);
	}
	if (!try_aopen)
		aufs_read_unlock(dentry, AuLock_DW);
out_free:
	kfree(a);
out:
	return err;
}

int aufs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode,
	       dev_t dev)
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

int aufs_create(struct inode *dir, struct dentry *dentry, umode_t mode,
		bool want_excl)
{
	struct simple_arg arg = {
		.type = Creat,
		.u.c = {
			.mode		= mode,
			.want_excl	= want_excl
		}
	};
	return add_simple(dir, dentry, &arg);
}

int au_aopen_or_create(struct inode *dir, struct dentry *dentry,
		       struct vfsub_aopen_args *aopen_args)
{
	struct simple_arg arg = {
		.type = Creat,
		.u.c = {
			.mode		= aopen_args->create_mode,
			.want_excl	= aopen_args->open_flag & O_EXCL,
			.try_aopen	= true,
			.aopen		= aopen_args
		}
	};
	return add_simple(dir, dentry, &arg);
}

int aufs_tmpfile(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	int err;
	aufs_bindex_t bindex;
	struct super_block *sb;
	struct dentry *parent, *h_parent, *h_dentry;
	struct inode *h_dir, *inode;
	struct vfsmount *h_mnt;
	struct au_wr_dir_args wr_dir_args = {
		.force_btgt	= -1,
		.flags		= AuWrDir_TMPFILE
	};

	/* copy-up may happen */
	inode_lock(dir);

	sb = dir->i_sb;
	err = si_read_lock(sb, AuLock_FLUSH | AuLock_NOPLM);
	if (unlikely(err))
		goto out;

	err = au_di_init(dentry);
	if (unlikely(err))
		goto out_si;

	err = -EBUSY;
	parent = d_find_any_alias(dir);
	AuDebugOn(!parent);
	di_write_lock_parent(parent);
	if (unlikely(d_inode(parent) != dir))
		goto out_parent;

	err = au_digen_test(parent, au_sigen(sb));
	if (unlikely(err))
		goto out_parent;

	bindex = au_dbtop(parent);
	au_set_dbtop(dentry, bindex);
	au_set_dbbot(dentry, bindex);
	err = au_wr_dir(dentry, /*src_dentry*/NULL, &wr_dir_args);
	bindex = err;
	if (unlikely(err < 0))
		goto out_parent;

	err = -EOPNOTSUPP;
	h_dir = au_h_iptr(dir, bindex);
	if (unlikely(!h_dir->i_op->tmpfile))
		goto out_parent;

	h_mnt = au_sbr_mnt(sb, bindex);
	err = vfsub_mnt_want_write(h_mnt);
	if (unlikely(err))
		goto out_parent;

	h_parent = au_h_dptr(parent, bindex);
	h_dentry = vfs_tmpfile(h_parent, mode, /*open_flag*/0);
	if (IS_ERR(h_dentry)) {
		err = PTR_ERR(h_dentry);
		goto out_mnt;
	}

	au_set_dbtop(dentry, bindex);
	au_set_dbbot(dentry, bindex);
	au_set_h_dptr(dentry, bindex, dget(h_dentry));
	inode = au_new_inode(dentry, /*must_new*/1);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		au_set_h_dptr(dentry, bindex, NULL);
		au_set_dbtop(dentry, -1);
		au_set_dbbot(dentry, -1);
	} else {
		if (!inode->i_nlink)
			set_nlink(inode, 1);
		d_tmpfile(dentry, inode);
		au_di(dentry)->di_tmpfile = 1;

		/* update without i_mutex */
		if (au_ibtop(dir) == au_dbtop(dentry))
			au_cpup_attr_timesizes(dir);
	}
	dput(h_dentry);

out_mnt:
	vfsub_mnt_drop_write(h_mnt);
out_parent:
	di_write_unlock(parent);
	dput(parent);
	di_write_unlock(dentry);
	if (unlikely(err)) {
		au_di_fin(dentry);
		dentry->d_fsdata = NULL;
	}
out_si:
	si_read_unlock(sb);
out:
	inode_unlock(dir);
	return err;
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
	struct au_cp_generic cpg = {
		.dentry	= src_dentry,
		.bdst	= a->bdst,
		.bsrc	= a->bsrc,
		.len	= -1,
		.pin	= &a->pin,
		.flags	= AuCpup_DTIME | AuCpup_HOPEN /* | AuCpup_KEEPLINO */
	};

	di_read_lock_parent(a->src_parent, AuLock_IR);
	err = au_test_and_cpup_dirs(src_dentry, a->bdst);
	if (unlikely(err))
		goto out;

	h_src_dentry = au_h_dptr(src_dentry, a->bsrc);
	err = au_pin(&a->pin, src_dentry, a->bdst,
		     au_opt_udba(src_dentry->d_sb),
		     AuPin_DI_LOCKED | AuPin_MNT_WRITE);
	if (unlikely(err))
		goto out;

	err = au_sio_cpup_simple(&cpg);
	au_unpin(&a->pin);

out:
	di_read_unlock(a->src_parent, AuLock_IR);
	return err;
}

static int au_cpup_or_link(struct dentry *src_dentry, struct dentry *dentry,
			   struct au_link_args *a)
{
	int err;
	unsigned char plink;
	aufs_bindex_t bbot;
	struct dentry *h_src_dentry;
	struct inode *h_inode, *inode, *delegated;
	struct super_block *sb;
	struct file *h_file;

	plink = 0;
	h_inode = NULL;
	sb = src_dentry->d_sb;
	inode = d_inode(src_dentry);
	if (au_ibtop(inode) <= a->bdst)
		h_inode = au_h_iptr(inode, a->bdst);
	if (!h_inode || !h_inode->i_nlink) {
		/* copyup src_dentry as the name of dentry. */
		bbot = au_dbbot(dentry);
		if (bbot < a->bsrc)
			au_set_dbbot(dentry, a->bsrc);
		au_set_h_dptr(dentry, a->bsrc,
			      dget(au_h_dptr(src_dentry, a->bsrc)));
		dget(a->h_path.dentry);
		au_set_h_dptr(dentry, a->bdst, NULL);
		AuDbg("temporary d_inode...\n");
		spin_lock(&dentry->d_lock);
		dentry->d_inode = d_inode(src_dentry); /* tmp */
		spin_unlock(&dentry->d_lock);
		h_file = au_h_open_pre(dentry, a->bsrc, /*force_wr*/0);
		if (IS_ERR(h_file))
			err = PTR_ERR(h_file);
		else {
			struct au_cp_generic cpg = {
				.dentry	= dentry,
				.bdst	= a->bdst,
				.bsrc	= -1,
				.len	= -1,
				.pin	= &a->pin,
				.flags	= AuCpup_KEEPLINO
			};
			err = au_sio_cpup_simple(&cpg);
			au_h_open_post(dentry, a->bsrc, h_file);
			if (!err) {
				dput(a->h_path.dentry);
				a->h_path.dentry = au_h_dptr(dentry, a->bdst);
			} else
				au_set_h_dptr(dentry, a->bdst,
					      a->h_path.dentry);
		}
		spin_lock(&dentry->d_lock);
		dentry->d_inode = NULL; /* restore */
		spin_unlock(&dentry->d_lock);
		AuDbg("temporary d_inode...done\n");
		au_set_h_dptr(dentry, a->bsrc, NULL);
		au_set_dbbot(dentry, bbot);
	} else {
		/* the inode of src_dentry already exists on a.bdst branch */
		h_src_dentry = d_find_alias(h_inode);
		if (!h_src_dentry && au_plink_test(inode)) {
			plink = 1;
			h_src_dentry = au_plink_lkup(inode, a->bdst);
			err = PTR_ERR(h_src_dentry);
			if (IS_ERR(h_src_dentry))
				goto out;

			if (unlikely(d_is_negative(h_src_dentry))) {
				dput(h_src_dentry);
				h_src_dentry = NULL;
			}

		}
		if (h_src_dentry) {
			delegated = NULL;
			err = vfsub_link(h_src_dentry, au_pinned_h_dir(&a->pin),
					 &a->h_path, &delegated);
			if (unlikely(err == -EWOULDBLOCK)) {
				pr_warn("cannot retry for NFSv4 delegation"
					" for an internal link\n");
				iput(delegated);
			}
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
	struct inode *inode, *delegated;
	struct super_block *sb;
	struct au_wr_dir_args wr_dir_args = {
		/* .force_btgt	= -1, */
		.flags		= AuWrDir_ADD_ENTRY
	};

	IMustLock(dir);
	inode = d_inode(src_dentry);
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
	err = au_d_linkable(src_dentry);
	if (unlikely(err))
		goto out_unlock;
	err = au_d_may_add(dentry);
	if (unlikely(err))
		goto out_unlock;

	a->src_parent = dget_parent(src_dentry);
	wr_dir_args.force_btgt = au_ibtop(inode);

	di_write_lock_parent(a->parent);
	wr_dir_args.force_btgt = au_wbr(dentry, wr_dir_args.force_btgt);
	wh_dentry = lock_hdir_lkup_wh(dentry, &dt, src_dentry, &a->pin,
				      &wr_dir_args);
	err = PTR_ERR(wh_dentry);
	if (IS_ERR(wh_dentry))
		goto out_parent;

	err = 0;
	sb = dentry->d_sb;
	a->bdst = au_dbtop(dentry);
	a->h_path.dentry = au_h_dptr(dentry, a->bdst);
	a->h_path.mnt = au_sbr_mnt(sb, a->bdst);
	a->bsrc = au_ibtop(inode);
	h_src_dentry = au_h_d_alias(src_dentry, a->bsrc);
	if (!h_src_dentry && au_di(src_dentry)->di_tmpfile)
		h_src_dentry = dget(au_hi_wh(inode, a->bsrc));
	if (!h_src_dentry) {
		a->bsrc = au_dbtop(src_dentry);
		h_src_dentry = au_h_d_alias(src_dentry, a->bsrc);
		AuDebugOn(!h_src_dentry);
	} else if (IS_ERR(h_src_dentry)) {
		err = PTR_ERR(h_src_dentry);
		goto out_parent;
	}

	/*
	 * aufs doesn't touch the credential so
	 * security_dentry_create_files_as() is unnecrssary.
	 */
	if (au_opt_test(au_mntflags(sb), PLINK)) {
		if (a->bdst < a->bsrc
		    /* && h_src_dentry->d_sb != a->h_path.dentry->d_sb */)
			err = au_cpup_or_link(src_dentry, dentry, a);
		else {
			delegated = NULL;
			err = vfsub_link(h_src_dentry, au_pinned_h_dir(&a->pin),
					 &a->h_path, &delegated);
			if (unlikely(err == -EWOULDBLOCK)) {
				pr_warn("cannot retry for NFSv4 delegation"
					" for an internal link\n");
				iput(delegated);
			}
		}
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
			if (h_src_dentry && d_is_positive(h_src_dentry)) {
				delegated = NULL;
				err = vfsub_link(h_src_dentry,
						 au_pinned_h_dir(&a->pin),
						 &a->h_path, &delegated);
				if (unlikely(err == -EWOULDBLOCK)) {
					pr_warn("cannot retry"
						" for NFSv4 delegation"
						" for an internal link\n");
					iput(delegated);
				}
			}
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

	au_dir_ts(dir, a->bdst);
	dir->i_version++;
	inc_nlink(inode);
	inode->i_ctime = dir->i_ctime;
	d_instantiate(dentry, au_igrab(inode));
	if (d_unhashed(a->h_path.dentry))
		/* some filesystem calls d_drop() */
		d_drop(dentry);
	/* some filesystems consume an inode even hardlink */
	au_fhsm_wrote(sb, a->bdst, /*force*/0);
	goto out_unpin; /* success */

out_revert:
	/* no delegation since it is just created */
	rerr = vfsub_unlink(au_pinned_h_dir(&a->pin), &a->h_path,
			    /*delegated*/NULL, /*force*/0);
	if (unlikely(rerr)) {
		AuIOErr("%pd reverting failed(%d, %d)\n", dentry, err, rerr);
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
		au_update_dbtop(dentry);
		d_drop(dentry);
	}
	aufs_read_and_write_unlock2(dentry, src_dentry);
out_kfree:
	kfree(a);
out:
	AuTraceErr(err);
	return err;
}

int aufs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	int err, rerr;
	aufs_bindex_t bindex;
	unsigned char diropq;
	struct path h_path;
	struct dentry *wh_dentry, *parent, *opq_dentry;
	struct inode *h_inode;
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
	bindex = au_dbtop(dentry);
	h_path.dentry = au_h_dptr(dentry, bindex);
	h_path.mnt = au_sbr_mnt(sb, bindex);
	err = vfsub_mkdir(au_pinned_h_dir(&a->pin), &h_path, mode);
	if (unlikely(err))
		goto out_unpin;

	/* make the dir opaque */
	diropq = 0;
	h_inode = d_inode(h_path.dentry);
	if (wh_dentry
	    || au_opt_test(au_mntflags(sb), ALWAYS_DIROPQ)) {
		inode_lock_nested(h_inode, AuLsc_I_CHILD);
		opq_dentry = au_diropq_create(dentry, bindex);
		inode_unlock(h_inode);
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
		inode_lock_nested(h_inode, AuLsc_I_CHILD);
		rerr = au_diropq_remove(dentry, bindex);
		inode_unlock(h_inode);
		if (rerr) {
			AuIOErr("%pd reverting diropq failed(%d, %d)\n",
				dentry, err, rerr);
			err = -EIO;
		}
	}

out_dir:
	AuLabel(revert dir);
	rerr = vfsub_rmdir(au_pinned_h_dir(&a->pin), &h_path);
	if (rerr) {
		AuIOErr("%pd reverting dir failed(%d, %d)\n",
			dentry, err, rerr);
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
		au_update_dbtop(dentry);
		d_drop(dentry);
	}
	aufs_read_unlock(dentry, AuLock_DW);
out_free:
	kfree(a);
out:
	return err;
}
