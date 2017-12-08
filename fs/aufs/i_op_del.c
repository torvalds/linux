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
 * inode operations (del entry)
 */

#include "aufs.h"

/*
 * decide if a new whiteout for @dentry is necessary or not.
 * when it is necessary, prepare the parent dir for the upper branch whose
 * branch index is @bcpup for creation. the actual creation of the whiteout will
 * be done by caller.
 * return value:
 * 0: wh is unnecessary
 * plus: wh is necessary
 * minus: error
 */
int au_wr_dir_need_wh(struct dentry *dentry, int isdir, aufs_bindex_t *bcpup)
{
	int need_wh, err;
	aufs_bindex_t btop;
	struct super_block *sb;

	sb = dentry->d_sb;
	btop = au_dbtop(dentry);
	if (*bcpup < 0) {
		*bcpup = btop;
		if (au_test_ro(sb, btop, d_inode(dentry))) {
			err = AuWbrCopyup(au_sbi(sb), dentry);
			*bcpup = err;
			if (unlikely(err < 0))
				goto out;
		}
	} else
		AuDebugOn(btop < *bcpup
			  || au_test_ro(sb, *bcpup, d_inode(dentry)));
	AuDbg("bcpup %d, btop %d\n", *bcpup, btop);

	if (*bcpup != btop) {
		err = au_cpup_dirs(dentry, *bcpup);
		if (unlikely(err))
			goto out;
		need_wh = 1;
	} else {
		struct au_dinfo *dinfo, *tmp;

		need_wh = -ENOMEM;
		dinfo = au_di(dentry);
		tmp = au_di_alloc(sb, AuLsc_DI_TMP);
		if (tmp) {
			au_di_cp(tmp, dinfo);
			au_di_swap(tmp, dinfo);
			/* returns the number of positive dentries */
			need_wh = au_lkup_dentry(dentry, btop + 1,
						 /* AuLkup_IGNORE_PERM */ 0);
			au_di_swap(tmp, dinfo);
			au_rw_write_unlock(&tmp->di_rwsem);
			au_di_free(tmp);
		}
	}
	AuDbg("need_wh %d\n", need_wh);
	err = need_wh;

out:
	return err;
}

/*
 * simple tests for the del-entry operations.
 * following the checks in vfs, plus the parent-child relationship.
 */
int au_may_del(struct dentry *dentry, aufs_bindex_t bindex,
	       struct dentry *h_parent, int isdir)
{
	int err;
	umode_t h_mode;
	struct dentry *h_dentry, *h_latest;
	struct inode *h_inode;

	h_dentry = au_h_dptr(dentry, bindex);
	if (d_really_is_positive(dentry)) {
		err = -ENOENT;
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
	} else {
		/* rename(2) case */
		err = -EIO;
		if (unlikely(d_is_positive(h_dentry)))
			goto out;
	}

	err = -ENOENT;
	/* expected parent dir is locked */
	if (unlikely(h_parent != h_dentry->d_parent))
		goto out;
	err = 0;

	/*
	 * rmdir a dir may break the consistency on some filesystem.
	 * let's try heavy test.
	 */
	err = -EACCES;
	if (unlikely(!au_opt_test(au_mntflags(dentry->d_sb), DIRPERM1)
		     && au_test_h_perm(d_inode(h_parent),
				       MAY_EXEC | MAY_WRITE)))
		goto out;

	h_latest = au_sio_lkup_one(&dentry->d_name, h_parent);
	err = -EIO;
	if (IS_ERR(h_latest))
		goto out;
	if (h_latest == h_dentry)
		err = 0;
	dput(h_latest);

out:
	return err;
}

/*
 * decide the branch where we operate for @dentry. the branch index will be set
 * @rbcpup. after diciding it, 'pin' it and store the timestamps of the parent
 * dir for reverting.
 * when a new whiteout is necessary, create it.
 */
static struct dentry*
lock_hdir_create_wh(struct dentry *dentry, int isdir, aufs_bindex_t *rbcpup,
		    struct au_dtime *dt, struct au_pin *pin)
{
	struct dentry *wh_dentry;
	struct super_block *sb;
	struct path h_path;
	int err, need_wh;
	unsigned int udba;
	aufs_bindex_t bcpup;

	need_wh = au_wr_dir_need_wh(dentry, isdir, rbcpup);
	wh_dentry = ERR_PTR(need_wh);
	if (unlikely(need_wh < 0))
		goto out;

	sb = dentry->d_sb;
	udba = au_opt_udba(sb);
	bcpup = *rbcpup;
	err = au_pin(pin, dentry, bcpup, udba,
		     AuPin_DI_LOCKED | AuPin_MNT_WRITE);
	wh_dentry = ERR_PTR(err);
	if (unlikely(err))
		goto out;

	h_path.dentry = au_pinned_h_parent(pin);
	if (udba != AuOpt_UDBA_NONE
	    && au_dbtop(dentry) == bcpup) {
		err = au_may_del(dentry, bcpup, h_path.dentry, isdir);
		wh_dentry = ERR_PTR(err);
		if (unlikely(err))
			goto out_unpin;
	}

	h_path.mnt = au_sbr_mnt(sb, bcpup);
	au_dtime_store(dt, au_pinned_parent(pin), &h_path);
	wh_dentry = NULL;
	if (!need_wh)
		goto out; /* success, no need to create whiteout */

	wh_dentry = au_wh_create(dentry, bcpup, h_path.dentry);
	if (IS_ERR(wh_dentry))
		goto out_unpin;

	/* returns with the parent is locked and wh_dentry is dget-ed */
	goto out; /* success */

out_unpin:
	au_unpin(pin);
out:
	return wh_dentry;
}

/*
 * when removing a dir, rename it to a unique temporary whiteout-ed name first
 * in order to be revertible and save time for removing many child whiteouts
 * under the dir.
 * returns 1 when there are too many child whiteout and caller should remove
 * them asynchronously. returns 0 when the number of children is enough small to
 * remove now or the branch fs is a remote fs.
 * otherwise return an error.
 */
static int renwh_and_rmdir(struct dentry *dentry, aufs_bindex_t bindex,
			   struct au_nhash *whlist, struct inode *dir)
{
	int rmdir_later, err, dirwh;
	struct dentry *h_dentry;
	struct super_block *sb;
	struct inode *inode;

	sb = dentry->d_sb;
	SiMustAnyLock(sb);
	h_dentry = au_h_dptr(dentry, bindex);
	err = au_whtmp_ren(h_dentry, au_sbr(sb, bindex));
	if (unlikely(err))
		goto out;

	/* stop monitoring */
	inode = d_inode(dentry);
	au_hn_free(au_hi(inode, bindex));

	if (!au_test_fs_remote(h_dentry->d_sb)) {
		dirwh = au_sbi(sb)->si_dirwh;
		rmdir_later = (dirwh <= 1);
		if (!rmdir_later)
			rmdir_later = au_nhash_test_longer_wh(whlist, bindex,
							      dirwh);
		if (rmdir_later)
			return rmdir_later;
	}

	err = au_whtmp_rmdir(dir, bindex, h_dentry, whlist);
	if (unlikely(err)) {
		AuIOErr("rmdir %pd, b%d failed, %d. ignored\n",
			h_dentry, bindex, err);
		err = 0;
	}

out:
	AuTraceErr(err);
	return err;
}

/*
 * final procedure for deleting a entry.
 * maintain dentry and iattr.
 */
static void epilog(struct inode *dir, struct dentry *dentry,
		   aufs_bindex_t bindex)
{
	struct inode *inode;

	inode = d_inode(dentry);
	d_drop(dentry);
	inode->i_ctime = dir->i_ctime;

	au_dir_ts(dir, bindex);
	dir->i_version++;
}

/*
 * when an error happened, remove the created whiteout and revert everything.
 */
static int do_revert(int err, struct inode *dir, aufs_bindex_t bindex,
		     aufs_bindex_t bwh, struct dentry *wh_dentry,
		     struct dentry *dentry, struct au_dtime *dt)
{
	int rerr;
	struct path h_path = {
		.dentry	= wh_dentry,
		.mnt	= au_sbr_mnt(dir->i_sb, bindex)
	};

	rerr = au_wh_unlink_dentry(au_h_iptr(dir, bindex), &h_path, dentry);
	if (!rerr) {
		au_set_dbwh(dentry, bwh);
		au_dtime_revert(dt);
		return 0;
	}

	AuIOErr("%pd reverting whiteout failed(%d, %d)\n", dentry, err, rerr);
	return -EIO;
}

/* ---------------------------------------------------------------------- */

int aufs_unlink(struct inode *dir, struct dentry *dentry)
{
	int err;
	aufs_bindex_t bwh, bindex, btop;
	struct inode *inode, *h_dir, *delegated;
	struct dentry *parent, *wh_dentry;
	/* to reuduce stack size */
	struct {
		struct au_dtime dt;
		struct au_pin pin;
		struct path h_path;
	} *a;

	IMustLock(dir);

	err = -ENOMEM;
	a = kmalloc(sizeof(*a), GFP_NOFS);
	if (unlikely(!a))
		goto out;

	err = aufs_read_lock(dentry, AuLock_DW | AuLock_GEN);
	if (unlikely(err))
		goto out_free;
	err = au_d_hashed_positive(dentry);
	if (unlikely(err))
		goto out_unlock;
	inode = d_inode(dentry);
	IMustLock(inode);
	err = -EISDIR;
	if (unlikely(d_is_dir(dentry)))
		goto out_unlock; /* possible? */

	btop = au_dbtop(dentry);
	bwh = au_dbwh(dentry);
	bindex = -1;
	parent = dentry->d_parent; /* dir inode is locked */
	di_write_lock_parent(parent);
	wh_dentry = lock_hdir_create_wh(dentry, /*isdir*/0, &bindex, &a->dt,
					&a->pin);
	err = PTR_ERR(wh_dentry);
	if (IS_ERR(wh_dentry))
		goto out_parent;

	a->h_path.mnt = au_sbr_mnt(dentry->d_sb, btop);
	a->h_path.dentry = au_h_dptr(dentry, btop);
	dget(a->h_path.dentry);
	if (bindex == btop) {
		h_dir = au_pinned_h_dir(&a->pin);
		delegated = NULL;
		err = vfsub_unlink(h_dir, &a->h_path, &delegated, /*force*/0);
		if (unlikely(err == -EWOULDBLOCK)) {
			pr_warn("cannot retry for NFSv4 delegation"
				" for an internal unlink\n");
			iput(delegated);
		}
	} else {
		/* dir inode is locked */
		h_dir = d_inode(wh_dentry->d_parent);
		IMustLock(h_dir);
		err = 0;
	}

	if (!err) {
		vfsub_drop_nlink(inode);
		epilog(dir, dentry, bindex);

		/* update target timestamps */
		if (bindex == btop) {
			vfsub_update_h_iattr(&a->h_path, /*did*/NULL);
			/*ignore*/
			inode->i_ctime = d_inode(a->h_path.dentry)->i_ctime;
		} else
			/* todo: this timestamp may be reverted later */
			inode->i_ctime = h_dir->i_ctime;
		goto out_unpin; /* success */
	}

	/* revert */
	if (wh_dentry) {
		int rerr;

		rerr = do_revert(err, dir, bindex, bwh, wh_dentry, dentry,
				 &a->dt);
		if (rerr)
			err = rerr;
	}

out_unpin:
	au_unpin(&a->pin);
	dput(wh_dentry);
	dput(a->h_path.dentry);
out_parent:
	di_write_unlock(parent);
out_unlock:
	aufs_read_unlock(dentry, AuLock_DW);
out_free:
	kfree(a);
out:
	return err;
}

int aufs_rmdir(struct inode *dir, struct dentry *dentry)
{
	int err, rmdir_later;
	aufs_bindex_t bwh, bindex, btop;
	struct inode *inode;
	struct dentry *parent, *wh_dentry, *h_dentry;
	struct au_whtmp_rmdir *args;
	/* to reuduce stack size */
	struct {
		struct au_dtime dt;
		struct au_pin pin;
	} *a;

	IMustLock(dir);

	err = -ENOMEM;
	a = kmalloc(sizeof(*a), GFP_NOFS);
	if (unlikely(!a))
		goto out;

	err = aufs_read_lock(dentry, AuLock_DW | AuLock_FLUSH | AuLock_GEN);
	if (unlikely(err))
		goto out_free;
	err = au_alive_dir(dentry);
	if (unlikely(err))
		goto out_unlock;
	inode = d_inode(dentry);
	IMustLock(inode);
	err = -ENOTDIR;
	if (unlikely(!d_is_dir(dentry)))
		goto out_unlock; /* possible? */

	err = -ENOMEM;
	args = au_whtmp_rmdir_alloc(dir->i_sb, GFP_NOFS);
	if (unlikely(!args))
		goto out_unlock;

	parent = dentry->d_parent; /* dir inode is locked */
	di_write_lock_parent(parent);
	err = au_test_empty(dentry, &args->whlist);
	if (unlikely(err))
		goto out_parent;

	btop = au_dbtop(dentry);
	bwh = au_dbwh(dentry);
	bindex = -1;
	wh_dentry = lock_hdir_create_wh(dentry, /*isdir*/1, &bindex, &a->dt,
					&a->pin);
	err = PTR_ERR(wh_dentry);
	if (IS_ERR(wh_dentry))
		goto out_parent;

	h_dentry = au_h_dptr(dentry, btop);
	dget(h_dentry);
	rmdir_later = 0;
	if (bindex == btop) {
		err = renwh_and_rmdir(dentry, btop, &args->whlist, dir);
		if (err > 0) {
			rmdir_later = err;
			err = 0;
		}
	} else {
		/* stop monitoring */
		au_hn_free(au_hi(inode, btop));

		/* dir inode is locked */
		IMustLock(d_inode(wh_dentry->d_parent));
		err = 0;
	}

	if (!err) {
		vfsub_dead_dir(inode);
		au_set_dbdiropq(dentry, -1);
		epilog(dir, dentry, bindex);

		if (rmdir_later) {
			au_whtmp_kick_rmdir(dir, btop, h_dentry, args);
			args = NULL;
		}

		goto out_unpin; /* success */
	}

	/* revert */
	AuLabel(revert);
	if (wh_dentry) {
		int rerr;

		rerr = do_revert(err, dir, bindex, bwh, wh_dentry, dentry,
				 &a->dt);
		if (rerr)
			err = rerr;
	}

out_unpin:
	au_unpin(&a->pin);
	dput(wh_dentry);
	dput(h_dentry);
out_parent:
	di_write_unlock(parent);
	if (args)
		au_whtmp_rmdir_free(args);
out_unlock:
	aufs_read_unlock(dentry, AuLock_DW);
out_free:
	kfree(a);
out:
	AuTraceErr(err);
	return err;
}
