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
 * handling file/dir, and address_space operation
 */

#ifdef CONFIG_AUFS_DEBUG
#include <linux/migrate.h>
#endif
#include <linux/pagemap.h>
#include "aufs.h"

/* drop flags for writing */
unsigned int au_file_roflags(unsigned int flags)
{
	flags &= ~(O_WRONLY | O_RDWR | O_APPEND | O_CREAT | O_TRUNC);
	flags |= O_RDONLY | O_NOATIME;
	return flags;
}

/* common functions to regular file and dir */
struct file *au_h_open(struct dentry *dentry, aufs_bindex_t bindex, int flags,
		       struct file *file, int force_wr)
{
	struct file *h_file;
	struct dentry *h_dentry;
	struct inode *h_inode;
	struct super_block *sb;
	struct au_branch *br;
	struct path h_path;
	int err;

	/* a race condition can happen between open and unlink/rmdir */
	h_file = ERR_PTR(-ENOENT);
	h_dentry = au_h_dptr(dentry, bindex);
	if (au_test_nfsd() && (!h_dentry || d_is_negative(h_dentry)))
		goto out;
	h_inode = d_inode(h_dentry);
	spin_lock(&h_dentry->d_lock);
	err = (!d_unhashed(dentry) && d_unlinked(h_dentry))
		/* || !d_inode(dentry)->i_nlink */
		;
	spin_unlock(&h_dentry->d_lock);
	if (unlikely(err))
		goto out;

	sb = dentry->d_sb;
	br = au_sbr(sb, bindex);
	err = au_br_test_oflag(flags, br);
	h_file = ERR_PTR(err);
	if (unlikely(err))
		goto out;

	/* drop flags for writing */
	if (au_test_ro(sb, bindex, d_inode(dentry))) {
		if (force_wr && !(flags & O_WRONLY))
			force_wr = 0;
		flags = au_file_roflags(flags);
		if (force_wr) {
			h_file = ERR_PTR(-EROFS);
			flags = au_file_roflags(flags);
			if (unlikely(vfsub_native_ro(h_inode)
				     || IS_APPEND(h_inode)))
				goto out;
			flags &= ~O_ACCMODE;
			flags |= O_WRONLY;
		}
	}
	flags &= ~O_CREAT;
	au_br_get(br);
	h_path.dentry = h_dentry;
	h_path.mnt = au_br_mnt(br);
	h_file = vfsub_dentry_open(&h_path, flags);
	if (IS_ERR(h_file))
		goto out_br;

	if (flags & __FMODE_EXEC) {
		err = deny_write_access(h_file);
		if (unlikely(err)) {
			fput(h_file);
			h_file = ERR_PTR(err);
			goto out_br;
		}
	}
	fsnotify_open(h_file);
	goto out; /* success */

out_br:
	au_br_put(br);
out:
	return h_file;
}

static int au_cmoo(struct dentry *dentry)
{
	int err, cmoo, matched;
	unsigned int udba;
	struct path h_path;
	struct au_pin pin;
	struct au_cp_generic cpg = {
		.dentry	= dentry,
		.bdst	= -1,
		.bsrc	= -1,
		.len	= -1,
		.pin	= &pin,
		.flags	= AuCpup_DTIME | AuCpup_HOPEN
	};
	struct inode *delegated;
	struct super_block *sb;
	struct au_sbinfo *sbinfo;
	struct au_fhsm *fhsm;
	pid_t pid;
	struct au_branch *br;
	struct dentry *parent;
	struct au_hinode *hdir;

	DiMustWriteLock(dentry);
	IiMustWriteLock(d_inode(dentry));

	err = 0;
	if (IS_ROOT(dentry))
		goto out;
	cpg.bsrc = au_dbtop(dentry);
	if (!cpg.bsrc)
		goto out;

	sb = dentry->d_sb;
	sbinfo = au_sbi(sb);
	fhsm = &sbinfo->si_fhsm;
	pid = au_fhsm_pid(fhsm);
	rcu_read_lock();
	matched = (pid
		   && (current->pid == pid
		       || rcu_dereference(current->real_parent)->pid == pid));
	rcu_read_unlock();
	if (matched)
		goto out;

	br = au_sbr(sb, cpg.bsrc);
	cmoo = au_br_cmoo(br->br_perm);
	if (!cmoo)
		goto out;
	if (!d_is_reg(dentry))
		cmoo &= AuBrAttr_COO_ALL;
	if (!cmoo)
		goto out;

	parent = dget_parent(dentry);
	di_write_lock_parent(parent);
	err = au_wbr_do_copyup_bu(dentry, cpg.bsrc - 1);
	cpg.bdst = err;
	if (unlikely(err < 0)) {
		err = 0;	/* there is no upper writable branch */
		goto out_dgrade;
	}
	AuDbg("bsrc %d, bdst %d\n", cpg.bsrc, cpg.bdst);

	/* do not respect the coo attrib for the target branch */
	err = au_cpup_dirs(dentry, cpg.bdst);
	if (unlikely(err))
		goto out_dgrade;

	di_downgrade_lock(parent, AuLock_IR);
	udba = au_opt_udba(sb);
	err = au_pin(&pin, dentry, cpg.bdst, udba,
		     AuPin_DI_LOCKED | AuPin_MNT_WRITE);
	if (unlikely(err))
		goto out_parent;

	err = au_sio_cpup_simple(&cpg);
	au_unpin(&pin);
	if (unlikely(err))
		goto out_parent;
	if (!(cmoo & AuBrWAttr_MOO))
		goto out_parent; /* success */

	err = au_pin(&pin, dentry, cpg.bsrc, udba,
		     AuPin_DI_LOCKED | AuPin_MNT_WRITE);
	if (unlikely(err))
		goto out_parent;

	h_path.mnt = au_br_mnt(br);
	h_path.dentry = au_h_dptr(dentry, cpg.bsrc);
	hdir = au_hi(d_inode(parent), cpg.bsrc);
	delegated = NULL;
	err = vfsub_unlink(hdir->hi_inode, &h_path, &delegated, /*force*/1);
	au_unpin(&pin);
	/* todo: keep h_dentry or not? */
	if (unlikely(err == -EWOULDBLOCK)) {
		pr_warn("cannot retry for NFSv4 delegation"
			" for an internal unlink\n");
		iput(delegated);
	}
	if (unlikely(err)) {
		pr_err("unlink %pd after coo failed (%d), ignored\n",
		       dentry, err);
		err = 0;
	}
	goto out_parent; /* success */

out_dgrade:
	di_downgrade_lock(parent, AuLock_IR);
out_parent:
	di_read_unlock(parent, AuLock_IR);
	dput(parent);
out:
	AuTraceErr(err);
	return err;
}

int au_do_open(struct file *file, struct au_do_open_args *args)
{
	int err, aopen = args->aopen;
	struct dentry *dentry;
	struct au_finfo *finfo;

	if (!aopen)
		err = au_finfo_init(file, args->fidir);
	else {
		lockdep_off();
		err = au_finfo_init(file, args->fidir);
		lockdep_on();
	}
	if (unlikely(err))
		goto out;

	dentry = file->f_path.dentry;
	AuDebugOn(IS_ERR_OR_NULL(dentry));
	di_write_lock_child(dentry);
	err = au_cmoo(dentry);
	di_downgrade_lock(dentry, AuLock_IR);
	if (!err) {
		if (!aopen)
			err = args->open(file, vfsub_file_flags(file), NULL);
		else {
			lockdep_off();
			err = args->open(file, vfsub_file_flags(file), NULL);
			lockdep_on();
		}
	}
	di_read_unlock(dentry, AuLock_IR);

	finfo = au_fi(file);
	if (!err) {
		finfo->fi_file = file;
		au_hbl_add(&finfo->fi_hlist,
			   &au_sbi(file->f_path.dentry->d_sb)->si_files);
	}
	if (!aopen)
		fi_write_unlock(file);
	else {
		lockdep_off();
		fi_write_unlock(file);
		lockdep_on();
	}
	if (unlikely(err)) {
		finfo->fi_hdir = NULL;
		au_finfo_fin(file);
	}

out:
	AuTraceErr(err);
	return err;
}

int au_reopen_nondir(struct file *file)
{
	int err;
	aufs_bindex_t btop;
	struct dentry *dentry;
	struct file *h_file, *h_file_tmp;

	dentry = file->f_path.dentry;
	btop = au_dbtop(dentry);
	h_file_tmp = NULL;
	if (au_fbtop(file) == btop) {
		h_file = au_hf_top(file);
		if (file->f_mode == h_file->f_mode)
			return 0; /* success */
		h_file_tmp = h_file;
		get_file(h_file_tmp);
		au_set_h_fptr(file, btop, NULL);
	}
	AuDebugOn(au_fi(file)->fi_hdir);
	/*
	 * it can happen
	 * file exists on both of rw and ro
	 * open --> dbtop and fbtop are both 0
	 * prepend a branch as rw, "rw" become ro
	 * remove rw/file
	 * delete the top branch, "rw" becomes rw again
	 *	--> dbtop is 1, fbtop is still 0
	 * write --> fbtop is 0 but dbtop is 1
	 */
	/* AuDebugOn(au_fbtop(file) < btop); */

	h_file = au_h_open(dentry, btop, vfsub_file_flags(file) & ~O_TRUNC,
			   file, /*force_wr*/0);
	err = PTR_ERR(h_file);
	if (IS_ERR(h_file)) {
		if (h_file_tmp) {
			au_sbr_get(dentry->d_sb, btop);
			au_set_h_fptr(file, btop, h_file_tmp);
			h_file_tmp = NULL;
		}
		goto out; /* todo: close all? */
	}

	err = 0;
	au_set_fbtop(file, btop);
	au_set_h_fptr(file, btop, h_file);
	au_update_figen(file);
	/* todo: necessary? */
	/* file->f_ra = h_file->f_ra; */

out:
	if (h_file_tmp)
		fput(h_file_tmp);
	return err;
}

/* ---------------------------------------------------------------------- */

static int au_reopen_wh(struct file *file, aufs_bindex_t btgt,
			struct dentry *hi_wh)
{
	int err;
	aufs_bindex_t btop;
	struct au_dinfo *dinfo;
	struct dentry *h_dentry;
	struct au_hdentry *hdp;

	dinfo = au_di(file->f_path.dentry);
	AuRwMustWriteLock(&dinfo->di_rwsem);

	btop = dinfo->di_btop;
	dinfo->di_btop = btgt;
	hdp = au_hdentry(dinfo, btgt);
	h_dentry = hdp->hd_dentry;
	hdp->hd_dentry = hi_wh;
	err = au_reopen_nondir(file);
	hdp->hd_dentry = h_dentry;
	dinfo->di_btop = btop;

	return err;
}

static int au_ready_to_write_wh(struct file *file, loff_t len,
				aufs_bindex_t bcpup, struct au_pin *pin)
{
	int err;
	struct inode *inode, *h_inode;
	struct dentry *h_dentry, *hi_wh;
	struct au_cp_generic cpg = {
		.dentry	= file->f_path.dentry,
		.bdst	= bcpup,
		.bsrc	= -1,
		.len	= len,
		.pin	= pin
	};

	au_update_dbtop(cpg.dentry);
	inode = d_inode(cpg.dentry);
	h_inode = NULL;
	if (au_dbtop(cpg.dentry) <= bcpup
	    && au_dbbot(cpg.dentry) >= bcpup) {
		h_dentry = au_h_dptr(cpg.dentry, bcpup);
		if (h_dentry && d_is_positive(h_dentry))
			h_inode = d_inode(h_dentry);
	}
	hi_wh = au_hi_wh(inode, bcpup);
	if (!hi_wh && !h_inode)
		err = au_sio_cpup_wh(&cpg, file);
	else
		/* already copied-up after unlink */
		err = au_reopen_wh(file, bcpup, hi_wh);

	if (!err
	    && (inode->i_nlink > 1
		|| (inode->i_state & I_LINKABLE))
	    && au_opt_test(au_mntflags(cpg.dentry->d_sb), PLINK))
		au_plink_append(inode, bcpup, au_h_dptr(cpg.dentry, bcpup));

	return err;
}

/*
 * prepare the @file for writing.
 */
int au_ready_to_write(struct file *file, loff_t len, struct au_pin *pin)
{
	int err;
	aufs_bindex_t dbtop;
	struct dentry *parent;
	struct inode *inode;
	struct super_block *sb;
	struct file *h_file;
	struct au_cp_generic cpg = {
		.dentry	= file->f_path.dentry,
		.bdst	= -1,
		.bsrc	= -1,
		.len	= len,
		.pin	= pin,
		.flags	= AuCpup_DTIME
	};

	sb = cpg.dentry->d_sb;
	inode = d_inode(cpg.dentry);
	cpg.bsrc = au_fbtop(file);
	err = au_test_ro(sb, cpg.bsrc, inode);
	if (!err && (au_hf_top(file)->f_mode & FMODE_WRITE)) {
		err = au_pin(pin, cpg.dentry, cpg.bsrc, AuOpt_UDBA_NONE,
			     /*flags*/0);
		goto out;
	}

	/* need to cpup or reopen */
	parent = dget_parent(cpg.dentry);
	di_write_lock_parent(parent);
	err = AuWbrCopyup(au_sbi(sb), cpg.dentry);
	cpg.bdst = err;
	if (unlikely(err < 0))
		goto out_dgrade;
	err = 0;

	if (!d_unhashed(cpg.dentry) && !au_h_dptr(parent, cpg.bdst)) {
		err = au_cpup_dirs(cpg.dentry, cpg.bdst);
		if (unlikely(err))
			goto out_dgrade;
	}

	err = au_pin(pin, cpg.dentry, cpg.bdst, AuOpt_UDBA_NONE,
		     AuPin_DI_LOCKED | AuPin_MNT_WRITE);
	if (unlikely(err))
		goto out_dgrade;

	dbtop = au_dbtop(cpg.dentry);
	if (dbtop <= cpg.bdst)
		cpg.bsrc = cpg.bdst;

	if (dbtop <= cpg.bdst		/* just reopen */
	    || !d_unhashed(cpg.dentry)	/* copyup and reopen */
		) {
		h_file = au_h_open_pre(cpg.dentry, cpg.bsrc, /*force_wr*/0);
		if (IS_ERR(h_file))
			err = PTR_ERR(h_file);
		else {
			di_downgrade_lock(parent, AuLock_IR);
			if (dbtop > cpg.bdst)
				err = au_sio_cpup_simple(&cpg);
			if (!err)
				err = au_reopen_nondir(file);
			au_h_open_post(cpg.dentry, cpg.bsrc, h_file);
		}
	} else {			/* copyup as wh and reopen */
		/*
		 * since writable hfsplus branch is not supported,
		 * h_open_pre/post() are unnecessary.
		 */
		err = au_ready_to_write_wh(file, len, cpg.bdst, pin);
		di_downgrade_lock(parent, AuLock_IR);
	}

	if (!err) {
		au_pin_set_parent_lflag(pin, /*lflag*/0);
		goto out_dput; /* success */
	}
	au_unpin(pin);
	goto out_unlock;

out_dgrade:
	di_downgrade_lock(parent, AuLock_IR);
out_unlock:
	di_read_unlock(parent, AuLock_IR);
out_dput:
	dput(parent);
out:
	return err;
}

/* ---------------------------------------------------------------------- */

int au_do_flush(struct file *file, fl_owner_t id,
		int (*flush)(struct file *file, fl_owner_t id))
{
	int err;
	struct super_block *sb;
	struct inode *inode;

	inode = file_inode(file);
	sb = inode->i_sb;
	si_noflush_read_lock(sb);
	fi_read_lock(file);
	ii_read_lock_child(inode);

	err = flush(file, id);
	au_cpup_attr_timesizes(inode);

	ii_read_unlock(inode);
	fi_read_unlock(file);
	si_read_unlock(sb);
	return err;
}

/* ---------------------------------------------------------------------- */

static int au_file_refresh_by_inode(struct file *file, int *need_reopen)
{
	int err;
	struct au_pin pin;
	struct au_finfo *finfo;
	struct dentry *parent, *hi_wh;
	struct inode *inode;
	struct super_block *sb;
	struct au_cp_generic cpg = {
		.dentry	= file->f_path.dentry,
		.bdst	= -1,
		.bsrc	= -1,
		.len	= -1,
		.pin	= &pin,
		.flags	= AuCpup_DTIME
	};

	FiMustWriteLock(file);

	err = 0;
	finfo = au_fi(file);
	sb = cpg.dentry->d_sb;
	inode = d_inode(cpg.dentry);
	cpg.bdst = au_ibtop(inode);
	if (cpg.bdst == finfo->fi_btop || IS_ROOT(cpg.dentry))
		goto out;

	parent = dget_parent(cpg.dentry);
	if (au_test_ro(sb, cpg.bdst, inode)) {
		di_read_lock_parent(parent, !AuLock_IR);
		err = AuWbrCopyup(au_sbi(sb), cpg.dentry);
		cpg.bdst = err;
		di_read_unlock(parent, !AuLock_IR);
		if (unlikely(err < 0))
			goto out_parent;
		err = 0;
	}

	di_read_lock_parent(parent, AuLock_IR);
	hi_wh = au_hi_wh(inode, cpg.bdst);
	if (!S_ISDIR(inode->i_mode)
	    && au_opt_test(au_mntflags(sb), PLINK)
	    && au_plink_test(inode)
	    && !d_unhashed(cpg.dentry)
	    && cpg.bdst < au_dbtop(cpg.dentry)) {
		err = au_test_and_cpup_dirs(cpg.dentry, cpg.bdst);
		if (unlikely(err))
			goto out_unlock;

		/* always superio. */
		err = au_pin(&pin, cpg.dentry, cpg.bdst, AuOpt_UDBA_NONE,
			     AuPin_DI_LOCKED | AuPin_MNT_WRITE);
		if (!err) {
			err = au_sio_cpup_simple(&cpg);
			au_unpin(&pin);
		}
	} else if (hi_wh) {
		/* already copied-up after unlink */
		err = au_reopen_wh(file, cpg.bdst, hi_wh);
		*need_reopen = 0;
	}

out_unlock:
	di_read_unlock(parent, AuLock_IR);
out_parent:
	dput(parent);
out:
	return err;
}

static void au_do_refresh_dir(struct file *file)
{
	aufs_bindex_t bindex, bbot, new_bindex, brid;
	struct au_hfile *p, tmp, *q;
	struct au_finfo *finfo;
	struct super_block *sb;
	struct au_fidir *fidir;

	FiMustWriteLock(file);

	sb = file->f_path.dentry->d_sb;
	finfo = au_fi(file);
	fidir = finfo->fi_hdir;
	AuDebugOn(!fidir);
	p = fidir->fd_hfile + finfo->fi_btop;
	brid = p->hf_br->br_id;
	bbot = fidir->fd_bbot;
	for (bindex = finfo->fi_btop; bindex <= bbot; bindex++, p++) {
		if (!p->hf_file)
			continue;

		new_bindex = au_br_index(sb, p->hf_br->br_id);
		if (new_bindex == bindex)
			continue;
		if (new_bindex < 0) {
			au_set_h_fptr(file, bindex, NULL);
			continue;
		}

		/* swap two lower inode, and loop again */
		q = fidir->fd_hfile + new_bindex;
		tmp = *q;
		*q = *p;
		*p = tmp;
		if (tmp.hf_file) {
			bindex--;
			p--;
		}
	}

	p = fidir->fd_hfile;
	if (!au_test_mmapped(file) && !d_unlinked(file->f_path.dentry)) {
		bbot = au_sbbot(sb);
		for (finfo->fi_btop = 0; finfo->fi_btop <= bbot;
		     finfo->fi_btop++, p++)
			if (p->hf_file) {
				if (file_inode(p->hf_file))
					break;
				au_hfput(p, /*execed*/0);
			}
	} else {
		bbot = au_br_index(sb, brid);
		for (finfo->fi_btop = 0; finfo->fi_btop < bbot;
		     finfo->fi_btop++, p++)
			if (p->hf_file)
				au_hfput(p, /*execed*/0);
		bbot = au_sbbot(sb);
	}

	p = fidir->fd_hfile + bbot;
	for (fidir->fd_bbot = bbot; fidir->fd_bbot >= finfo->fi_btop;
	     fidir->fd_bbot--, p--)
		if (p->hf_file) {
			if (file_inode(p->hf_file))
				break;
			au_hfput(p, /*execed*/0);
		}
	AuDebugOn(fidir->fd_bbot < finfo->fi_btop);
}

/*
 * after branch manipulating, refresh the file.
 */
static int refresh_file(struct file *file, int (*reopen)(struct file *file))
{
	int err, need_reopen, nbr;
	aufs_bindex_t bbot, bindex;
	struct dentry *dentry;
	struct super_block *sb;
	struct au_finfo *finfo;
	struct au_hfile *hfile;

	dentry = file->f_path.dentry;
	sb = dentry->d_sb;
	nbr = au_sbbot(sb) + 1;
	finfo = au_fi(file);
	if (!finfo->fi_hdir) {
		hfile = &finfo->fi_htop;
		AuDebugOn(!hfile->hf_file);
		bindex = au_br_index(sb, hfile->hf_br->br_id);
		AuDebugOn(bindex < 0);
		if (bindex != finfo->fi_btop)
			au_set_fbtop(file, bindex);
	} else {
		err = au_fidir_realloc(finfo, nbr, /*may_shrink*/0);
		if (unlikely(err))
			goto out;
		au_do_refresh_dir(file);
	}

	err = 0;
	need_reopen = 1;
	if (!au_test_mmapped(file))
		err = au_file_refresh_by_inode(file, &need_reopen);
	if (finfo->fi_hdir)
		/* harmless if err */
		au_fidir_realloc(finfo, nbr, /*may_shrink*/1);
	if (!err && need_reopen && !d_unlinked(dentry))
		err = reopen(file);
	if (!err) {
		au_update_figen(file);
		goto out; /* success */
	}

	/* error, close all lower files */
	if (finfo->fi_hdir) {
		bbot = au_fbbot_dir(file);
		for (bindex = au_fbtop(file); bindex <= bbot; bindex++)
			au_set_h_fptr(file, bindex, NULL);
	}

out:
	return err;
}

/* common function to regular file and dir */
int au_reval_and_lock_fdi(struct file *file, int (*reopen)(struct file *file),
			  int wlock, unsigned int fi_lsc)
{
	int err;
	unsigned int sigen, figen;
	aufs_bindex_t btop;
	unsigned char pseudo_link;
	struct dentry *dentry;
	struct inode *inode;

	err = 0;
	dentry = file->f_path.dentry;
	inode = d_inode(dentry);
	sigen = au_sigen(dentry->d_sb);
	fi_write_lock_nested(file, fi_lsc);
	figen = au_figen(file);
	if (!fi_lsc)
		di_write_lock_child(dentry);
	else
		di_write_lock_child2(dentry);
	btop = au_dbtop(dentry);
	pseudo_link = (btop != au_ibtop(inode));
	if (sigen == figen && !pseudo_link && au_fbtop(file) == btop) {
		if (!wlock) {
			di_downgrade_lock(dentry, AuLock_IR);
			fi_downgrade_lock(file);
		}
		goto out; /* success */
	}

	AuDbg("sigen %d, figen %d\n", sigen, figen);
	if (au_digen_test(dentry, sigen)) {
		err = au_reval_dpath(dentry, sigen);
		AuDebugOn(!err && au_digen_test(dentry, sigen));
	}

	if (!err)
		err = refresh_file(file, reopen);
	if (!err) {
		if (!wlock) {
			di_downgrade_lock(dentry, AuLock_IR);
			fi_downgrade_lock(file);
		}
	} else {
		di_write_unlock(dentry);
		fi_write_unlock(file);
	}

out:
	return err;
}

/* ---------------------------------------------------------------------- */

/* cf. aufs_nopage() */
/* for madvise(2) */
static int aufs_readpage(struct file *file __maybe_unused, struct page *page)
{
	unlock_page(page);
	return 0;
}

/* it will never be called, but necessary to support O_DIRECT */
static ssize_t aufs_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{ BUG(); return 0; }

/* they will never be called. */
#ifdef CONFIG_AUFS_DEBUG
static int aufs_write_begin(struct file *file, struct address_space *mapping,
			    loff_t pos, unsigned len, unsigned flags,
			    struct page **pagep, void **fsdata)
{ AuUnsupport(); return 0; }
static int aufs_write_end(struct file *file, struct address_space *mapping,
			  loff_t pos, unsigned len, unsigned copied,
			  struct page *page, void *fsdata)
{ AuUnsupport(); return 0; }
static int aufs_writepage(struct page *page, struct writeback_control *wbc)
{ AuUnsupport(); return 0; }

static int aufs_set_page_dirty(struct page *page)
{ AuUnsupport(); return 0; }
static void aufs_invalidatepage(struct page *page, unsigned int offset,
				unsigned int length)
{ AuUnsupport(); }
static int aufs_releasepage(struct page *page, gfp_t gfp)
{ AuUnsupport(); return 0; }
#if 0 /* called by memory compaction regardless file */
static int aufs_migratepage(struct address_space *mapping, struct page *newpage,
			    struct page *page, enum migrate_mode mode)
{ AuUnsupport(); return 0; }
#endif
static bool aufs_isolate_page(struct page *page, isolate_mode_t mode)
{ AuUnsupport(); return true; }
static void aufs_putback_page(struct page *page)
{ AuUnsupport(); }
static int aufs_launder_page(struct page *page)
{ AuUnsupport(); return 0; }
static int aufs_is_partially_uptodate(struct page *page,
				      unsigned long from,
				      unsigned long count)
{ AuUnsupport(); return 0; }
static void aufs_is_dirty_writeback(struct page *page, bool *dirty,
				    bool *writeback)
{ AuUnsupport(); }
static int aufs_error_remove_page(struct address_space *mapping,
				  struct page *page)
{ AuUnsupport(); return 0; }
static int aufs_swap_activate(struct swap_info_struct *sis, struct file *file,
			      sector_t *span)
{ AuUnsupport(); return 0; }
static void aufs_swap_deactivate(struct file *file)
{ AuUnsupport(); }
#endif /* CONFIG_AUFS_DEBUG */

const struct address_space_operations aufs_aop = {
	.readpage		= aufs_readpage,
	.direct_IO		= aufs_direct_IO,
#ifdef CONFIG_AUFS_DEBUG
	.writepage		= aufs_writepage,
	/* no writepages, because of writepage */
	.set_page_dirty		= aufs_set_page_dirty,
	/* no readpages, because of readpage */
	.write_begin		= aufs_write_begin,
	.write_end		= aufs_write_end,
	/* no bmap, no block device */
	.invalidatepage		= aufs_invalidatepage,
	.releasepage		= aufs_releasepage,
	/* is fallback_migrate_page ok? */
	/* .migratepage		= aufs_migratepage, */
	.isolate_page		= aufs_isolate_page,
	.putback_page		= aufs_putback_page,
	.launder_page		= aufs_launder_page,
	.is_partially_uptodate	= aufs_is_partially_uptodate,
	.is_dirty_writeback	= aufs_is_dirty_writeback,
	.error_remove_page	= aufs_error_remove_page,
	.swap_activate		= aufs_swap_activate,
	.swap_deactivate	= aufs_swap_deactivate
#endif /* CONFIG_AUFS_DEBUG */
};
