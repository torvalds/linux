/*
 * Copyright (C) 2011-2013 Junjiro R. Okajima
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

#include "aufs.h"

struct au_mvd_args {
	struct {
		struct super_block *h_sb;
		struct dentry *h_parent;
		struct au_hinode *hdir;
		struct inode *h_dir, *h_inode;
	} info[AUFS_MVDOWN_NARRAY];

	struct aufs_mvdown mvdown;
	struct dentry *dentry, *parent;
	struct inode *inode, *dir;
	struct super_block *sb;
	aufs_bindex_t bopq, bwh, bfound;
	unsigned char rename_lock;
	struct au_pin pin;
};

#define mvd_errno		mvdown.au_errno
#define mvd_bsrc		mvdown.a[AUFS_MVDOWN_UPPER].bindex
#define mvd_src_brid		mvdown.a[AUFS_MVDOWN_UPPER].brid
#define mvd_bdst		mvdown.a[AUFS_MVDOWN_LOWER].bindex
#define mvd_dst_brid		mvdown.a[AUFS_MVDOWN_LOWER].brid

#define mvd_h_src_sb		info[AUFS_MVDOWN_UPPER].h_sb
#define mvd_h_src_parent	info[AUFS_MVDOWN_UPPER].h_parent
#define mvd_hdir_src		info[AUFS_MVDOWN_UPPER].hdir
#define mvd_h_src_dir		info[AUFS_MVDOWN_UPPER].h_dir
#define mvd_h_src_inode		info[AUFS_MVDOWN_UPPER].h_inode

#define mvd_h_dst_sb		info[AUFS_MVDOWN_LOWER].h_sb
#define mvd_h_dst_parent	info[AUFS_MVDOWN_LOWER].h_parent
#define mvd_hdir_dst		info[AUFS_MVDOWN_LOWER].hdir
#define mvd_h_dst_dir		info[AUFS_MVDOWN_LOWER].h_dir
#define mvd_h_dst_inode		info[AUFS_MVDOWN_LOWER].h_inode

#define AU_MVD_PR(flag, ...) do {			\
		if (flag)				\
			pr_err(__VA_ARGS__);		\
	} while (0)

/* make the parent dir on bdst */
static int au_do_mkdir(const unsigned char dmsg, struct au_mvd_args *a)
{
	int err;

	err = 0;
	a->mvd_hdir_src = au_hi(a->dir, a->mvd_bsrc);
	a->mvd_hdir_dst = au_hi(a->dir, a->mvd_bdst);
	a->mvd_h_src_parent = au_h_dptr(a->parent, a->mvd_bsrc);
	a->mvd_h_dst_parent = NULL;
	if (au_dbend(a->parent) >= a->mvd_bdst)
		a->mvd_h_dst_parent = au_h_dptr(a->parent, a->mvd_bdst);
	if (!a->mvd_h_dst_parent) {
		err = au_cpdown_dirs(a->dentry, a->mvd_bdst);
		if (unlikely(err)) {
			AU_MVD_PR(dmsg, "cpdown_dirs failed\n");
			goto out;
		}
		a->mvd_h_dst_parent = au_h_dptr(a->parent, a->mvd_bdst);
	}

out:
	AuTraceErr(err);
	return err;
}

/* lock them all */
static int au_do_lock(const unsigned char dmsg, struct au_mvd_args *a)
{
	int err;
	struct dentry *h_trap;

	a->mvd_h_src_sb = au_sbr_sb(a->sb, a->mvd_bsrc);
	a->mvd_h_dst_sb = au_sbr_sb(a->sb, a->mvd_bdst);
	if (a->mvd_h_src_sb != a->mvd_h_dst_sb) {
		a->rename_lock = 0;
		err = au_pin(&a->pin, a->dentry, a->mvd_bdst,
			     au_opt_udba(a->sb),
			     AuPin_MNT_WRITE | AuPin_DI_LOCKED);
		if (!err) {
			a->mvd_h_src_dir = a->mvd_h_src_parent->d_inode;
			mutex_lock_nested(&a->mvd_h_src_dir->i_mutex,
					  AuLsc_I_PARENT3);
		} else
			AU_MVD_PR(dmsg, "pin failed\n");
		goto out;
	}

	err = 0;
	a->rename_lock = 1;
	h_trap = vfsub_lock_rename(a->mvd_h_src_parent, a->mvd_hdir_src,
				   a->mvd_h_dst_parent, a->mvd_hdir_dst);
	if (h_trap) {
		err = (h_trap != a->mvd_h_src_parent);
		if (err)
			err = (h_trap != a->mvd_h_dst_parent);
	}
	BUG_ON(err); /* it should never happen */

out:
	AuTraceErr(err);
	return err;
}

static void au_do_unlock(const unsigned char dmsg, struct au_mvd_args *a)
{
	if (!a->rename_lock) {
		mutex_unlock(&a->mvd_h_src_dir->i_mutex);
		au_unpin(&a->pin);
	} else
		vfsub_unlock_rename(a->mvd_h_src_parent, a->mvd_hdir_src,
				    a->mvd_h_dst_parent, a->mvd_hdir_dst);
}

/* copy-down the file */
static int au_do_cpdown(const unsigned char dmsg, struct au_mvd_args *a)
{
	int err;
	struct au_cp_generic cpg = {
		.dentry	= a->dentry,
		.bdst	= a->mvd_bdst,
		.bsrc	= a->mvd_bsrc,
		.len	= -1,
		.pin	= &a->pin,
		.flags	= AuCpup_DTIME | AuCpup_HOPEN
	};

	AuDbg("b%d, b%d\n", cpg.bsrc, cpg.bdst);
	if (a->mvdown.flags & AUFS_MVDOWN_OWLOWER)
		au_fset_cpup(cpg.flags, OVERWRITE);
	if (a->mvdown.flags & AUFS_MVDOWN_ROLOWER)
		au_fset_cpup(cpg.flags, RWDST);
	err = au_sio_cpdown_simple(&cpg);
	if (unlikely(err))
		AU_MVD_PR(dmsg, "cpdown failed\n");

	AuTraceErr(err);
	return err;
}

/*
 * unlink the whiteout on bdst if exist which may be created by UDBA while we
 * were sleeping
 */
static int au_do_unlink_wh(const unsigned char dmsg, struct au_mvd_args *a)
{
	int err;
	struct path h_path;
	struct au_branch *br;

	br = au_sbr(a->sb, a->mvd_bdst);
	h_path.dentry = au_wh_lkup(a->mvd_h_dst_parent, &a->dentry->d_name, br);
	err = PTR_ERR(h_path.dentry);
	if (IS_ERR(h_path.dentry)) {
		AU_MVD_PR(dmsg, "wh_lkup failed\n");
		goto out;
	}

	err = 0;
	if (h_path.dentry->d_inode) {
		h_path.mnt = au_br_mnt(br);
		err = vfsub_unlink(a->mvd_h_dst_parent->d_inode, &h_path,
				   /*force*/0);
		if (unlikely(err))
			AU_MVD_PR(dmsg, "wh_unlink failed\n");
	}
	dput(h_path.dentry);

out:
	AuTraceErr(err);
	return err;
}

/*
 * unlink the topmost h_dentry
 * Note: the target file MAY be modified by UDBA between this mutex_unlock() and
 *	mutex_lock() in vfs_unlink(). in this case, such changes may be lost.
 */
static int au_do_unlink(const unsigned char dmsg, struct au_mvd_args *a)
{
	int err;
	struct path h_path;

	h_path.mnt = au_sbr_mnt(a->sb, a->mvd_bsrc);
	h_path.dentry = au_h_dptr(a->dentry, a->mvd_bsrc);
	err = vfsub_unlink(a->mvd_h_src_dir, &h_path, /*force*/0);
	if (unlikely(err))
		AU_MVD_PR(dmsg, "unlink failed\n");

	AuTraceErr(err);
	return err;
}

/*
 * copy-down the file and unlink the bsrc file.
 * - unlink the bdst whout if exist
 * - copy-down the file (with whtmp name and rename)
 * - unlink the bsrc file
 */
static int au_do_mvdown(const unsigned char dmsg, struct au_mvd_args *a)
{
	int err;

	err = au_do_mkdir(dmsg, a);
	if (!err)
		err = au_do_lock(dmsg, a);
	if (unlikely(err))
		goto out;

	/*
	 * do not revert the activities we made on bdst since they should be
	 * harmless in aufs.
	 */

	err = au_do_cpdown(dmsg, a);
	if (!err)
		err = au_do_unlink_wh(dmsg, a);
	if (!err && !(a->mvdown.flags & AUFS_MVDOWN_KUPPER))
		err = au_do_unlink(dmsg, a);
	if (unlikely(err))
		goto out_unlock;

	/* maintain internal array */
	if (!(a->mvdown.flags & AUFS_MVDOWN_KUPPER)) {
		au_set_h_dptr(a->dentry, a->mvd_bsrc, NULL);
		au_set_dbstart(a->dentry, a->mvd_bdst);
		au_set_h_iptr(a->inode, a->mvd_bsrc, NULL, /*flags*/0);
		au_set_ibstart(a->inode, a->mvd_bdst);
	}
	if (au_dbend(a->dentry) < a->mvd_bdst)
		au_set_dbend(a->dentry, a->mvd_bdst);
	if (au_ibend(a->inode) < a->mvd_bdst)
		au_set_ibend(a->inode, a->mvd_bdst);

out_unlock:
	au_do_unlock(dmsg, a);
out:
	AuTraceErr(err);
	return err;
}

/* ---------------------------------------------------------------------- */

static int find_lower_writable(struct au_mvd_args *a)
{
	struct super_block *sb;
	aufs_bindex_t bindex, bend;
	struct au_branch *br;

	sb = a->sb;
	bindex = a->mvd_bsrc;
	bend = au_sbend(sb);
	if (!(a->mvdown.flags & AUFS_MVDOWN_ROLOWER)) {
		for (bindex++; bindex <= bend; bindex++) {
			br = au_sbr(sb, bindex);
			if (!au_br_rdonly(br))
				return bindex;
		}
	} else {
		for (bindex++; bindex <= bend; bindex++) {
			br = au_sbr(sb, bindex);
			if (!(au_br_sb(br)->s_flags & MS_RDONLY)) {
				if (au_br_rdonly(br))
					a->mvdown.flags
						|= AUFS_MVDOWN_ROLOWER_R;
				return bindex;
			}
		}
	}

	return -1;
}

/* make sure the file is idle */
static int au_mvd_args_busy(const unsigned char dmsg, struct au_mvd_args *a)
{
	int err, plinked;

	err = 0;
	plinked = !!au_opt_test(au_mntflags(a->sb), PLINK);
	if (au_dbstart(a->dentry) == a->mvd_bsrc
	    && d_count(a->dentry) == 1
	    && atomic_read(&a->inode->i_count) == 1
	    /* && a->mvd_h_src_inode->i_nlink == 1 */
	    && (!plinked || !au_plink_test(a->inode))
	    && a->inode->i_nlink == 1)
		goto out;

	err = -EBUSY;
	AU_MVD_PR(dmsg,
		  "b%d, d{b%d, c%u?}, i{c%d?, l%u}, hi{l%u}, p{%d, %d}\n",
		  a->mvd_bsrc, au_dbstart(a->dentry), d_count(a->dentry),
		  atomic_read(&a->inode->i_count), a->inode->i_nlink,
		  a->mvd_h_src_inode->i_nlink,
		  plinked, plinked ? au_plink_test(a->inode) : 0);

out:
	AuTraceErr(err);
	return err;
}

/* make sure the parent dir is fine */
static int au_mvd_args_parent(const unsigned char dmsg,
			      struct au_mvd_args *a)
{
	int err;
	aufs_bindex_t bindex;

	err = 0;
	if (unlikely(au_alive_dir(a->parent))) {
		err = -ENOENT;
		AU_MVD_PR(dmsg, "parent dir is dead\n");
		goto out;
	}

	a->bopq = au_dbdiropq(a->parent);
	bindex = au_wbr_nonopq(a->dentry, a->mvd_bdst);
	AuDbg("b%d\n", bindex);
	if (unlikely((bindex >= 0 && bindex < a->mvd_bdst)
		     || (a->bopq != -1 && a->bopq < a->mvd_bdst))) {
		err = -EINVAL;
		a->mvd_errno = EAU_MVDOWN_OPAQUE;
		AU_MVD_PR(dmsg, "ancestor is opaque b%d, b%d\n",
			  a->bopq, a->mvd_bdst);
	}

out:
	AuTraceErr(err);
	return err;
}

static int au_mvd_args_intermediate(const unsigned char dmsg,
				    struct au_mvd_args *a)
{
	int err;
	struct au_dinfo *dinfo, *tmp;

	/* lookup the next lower positive entry */
	err = -ENOMEM;
	tmp = au_di_alloc(a->sb, AuLsc_DI_TMP);
	if (unlikely(!tmp))
		goto out;

	a->bfound = -1;
	a->bwh = -1;
	dinfo = au_di(a->dentry);
	au_di_cp(tmp, dinfo);
	au_di_swap(tmp, dinfo);

	/* returns the number of positive dentries */
	err = au_lkup_dentry(a->dentry, a->mvd_bsrc + 1, /*type*/0);
	if (!err)
		a->bwh = au_dbwh(a->dentry);
	else if (err > 0)
		a->bfound = au_dbstart(a->dentry);

	au_di_swap(tmp, dinfo);
	au_rw_write_unlock(&tmp->di_rwsem);
	au_di_free(tmp);
	if (unlikely(err < 0))
		AU_MVD_PR(dmsg, "failed look-up lower\n");

	/*
	 * here, we have these cases.
	 * bfound == -1
	 *	no positive dentry under bsrc. there are more sub-cases.
	 *	bwh < 0
	 *		there no whiteout, we can safely move-down.
	 *	bwh <= bsrc
	 *		impossible
	 *	bsrc < bwh && bwh < bdst
	 *		there is a whiteout on RO branch. cannot proceed.
	 *	bwh == bdst
	 *		there is a whiteout on the RW target branch. it should
	 *		be removed.
	 *	bdst < bwh
	 *		there is a whiteout somewhere unrelated branch.
	 * -1 < bfound && bfound <= bsrc
	 *	impossible.
	 * bfound < bdst
	 *	found, but it is on RO branch between bsrc and bdst. cannot
	 *	proceed.
	 * bfound == bdst
	 *	found, replace it if AUFS_MVDOWN_FORCE is set. otherwise return
	 *	error.
	 * bdst < bfound
	 *	found, after we create the file on bdst, it will be hidden.
	 */

	AuDebugOn(a->bfound == -1
		  && a->bwh != -1
		  && a->bwh <= a->mvd_bsrc);
	AuDebugOn(-1 < a->bfound
		  && a->bfound <= a->mvd_bsrc);

	err = -EINVAL;
	if (a->bfound == -1
	    && a->mvd_bsrc < a->bwh
	    && a->bwh != -1
	    && a->bwh < a->mvd_bdst) {
		a->mvd_errno = EAU_MVDOWN_WHITEOUT;
		AU_MVD_PR(dmsg, "bsrc %d, bdst %d, bfound %d, bwh %d\n",
			  a->mvd_bsrc, a->mvd_bdst, a->bfound, a->bwh);
		goto out;
	} else if (a->bfound != -1 && a->bfound < a->mvd_bdst) {
		a->mvd_errno = EAU_MVDOWN_UPPER;
		AU_MVD_PR(dmsg, "bdst %d, bfound %d\n",
			  a->mvd_bdst, a->bfound);
		goto out;
	}

	err = 0; /* success */

out:
	AuTraceErr(err);
	return err;
}

static int au_mvd_args_exist(const unsigned char dmsg, struct au_mvd_args *a)
{
	int err;

	err = 0;
	if (!(a->mvdown.flags & AUFS_MVDOWN_OWLOWER)
	    && a->bfound == a->mvd_bdst)
		err = -EEXIST;
	AuTraceErr(err);
	return err;
}

static int au_mvd_args(const unsigned char dmsg, struct au_mvd_args *a)
{
	int err;
	struct au_branch *br;

	err = -EISDIR;
	if (unlikely(S_ISDIR(a->inode->i_mode)))
		goto out;

	err = -EINVAL;
	if (!(a->mvdown.flags & AUFS_MVDOWN_BRID_UPPER))
		a->mvd_bsrc = au_ibstart(a->inode);
	else {
		a->mvd_bsrc = au_br_index(a->sb, a->mvd_src_brid);
		if (unlikely(a->mvd_bsrc < 0
			     || (a->mvd_bsrc < au_dbstart(a->dentry)
				 || au_dbend(a->dentry) < a->mvd_bsrc
				 || !au_h_dptr(a->dentry, a->mvd_bsrc))
			     || (a->mvd_bsrc < au_ibstart(a->inode)
				 || au_ibend(a->inode) < a->mvd_bsrc
				 || !au_h_iptr(a->inode, a->mvd_bsrc)))) {
			a->mvd_errno = EAU_MVDOWN_NOUPPER;
			AU_MVD_PR(dmsg, "no upper\n");
			goto out;
		}
	}
	if (unlikely(a->mvd_bsrc == au_sbend(a->sb))) {
		a->mvd_errno = EAU_MVDOWN_BOTTOM;
		AU_MVD_PR(dmsg, "on the bottom\n");
		goto out;
	}
	a->mvd_h_src_inode = au_h_iptr(a->inode, a->mvd_bsrc);
	br = au_sbr(a->sb, a->mvd_bsrc);
	err = au_br_rdonly(br);
	if (!(a->mvdown.flags & AUFS_MVDOWN_ROUPPER)) {
		if (unlikely(err))
			goto out;
	} else if (!(vfsub_native_ro(a->mvd_h_src_inode)
		     || IS_APPEND(a->mvd_h_src_inode))) {
		if (err)
			a->mvdown.flags |= AUFS_MVDOWN_ROUPPER_R;
		/* go on */
	} else
		goto out;

	err = -EINVAL;
	if (!(a->mvdown.flags & AUFS_MVDOWN_BRID_LOWER)) {
		a->mvd_bdst = find_lower_writable(a);
		if (unlikely(a->mvd_bdst < 0)) {
			a->mvd_errno = EAU_MVDOWN_BOTTOM;
			AU_MVD_PR(dmsg, "no writable lower branch\n");
			goto out;
		}
	} else {
		a->mvd_bdst = au_br_index(a->sb, a->mvd_dst_brid);
		if (unlikely(a->mvd_bdst < 0
			     || au_sbend(a->sb) < a->mvd_bdst)) {
			a->mvd_errno = EAU_MVDOWN_NOLOWERBR;
			AU_MVD_PR(dmsg, "no lower brid\n");
			goto out;
		}
	}

	err = au_mvd_args_busy(dmsg, a);
	if (!err)
		err = au_mvd_args_parent(dmsg, a);
	if (!err)
		err = au_mvd_args_intermediate(dmsg, a);
	if (!err)
		err = au_mvd_args_exist(dmsg, a);
	if (!err)
		AuDbg("b%d, b%d\n", a->mvd_bsrc, a->mvd_bdst);

out:
	AuTraceErr(err);
	return err;
}

int au_mvdown(struct dentry *dentry, struct aufs_mvdown __user *uarg)
{
	int err, e;
	unsigned char dmsg;
	struct au_mvd_args *args;

	err = -EPERM;
	if (unlikely(!capable(CAP_SYS_ADMIN)))
		goto out;

	WARN_ONCE(1, "move-down is still testing...\n");

	err = -ENOMEM;
	args = kmalloc(sizeof(*args), GFP_NOFS);
	if (unlikely(!args))
		goto out;

	err = copy_from_user(&args->mvdown, uarg, sizeof(args->mvdown));
	if (!err)
		err = !access_ok(VERIFY_WRITE, uarg, sizeof(*uarg));
	if (unlikely(err)) {
		err = -EFAULT;
		AuTraceErr(err);
		goto out_free;
	}
	AuDbg("flags 0x%x\n", args->mvdown.flags);
	args->mvdown.flags &= ~(AUFS_MVDOWN_ROLOWER_R | AUFS_MVDOWN_ROUPPER_R);
	args->mvdown.au_errno = 0;
	args->dentry = dentry;
	args->inode = dentry->d_inode;
	args->sb = dentry->d_sb;

	err = -ENOENT;
	dmsg = !!(args->mvdown.flags & AUFS_MVDOWN_DMSG);
	args->parent = dget_parent(dentry);
	args->dir = args->parent->d_inode;
	mutex_lock_nested(&args->dir->i_mutex, I_MUTEX_PARENT);
	dput(args->parent);
	if (unlikely(args->parent != dentry->d_parent)) {
		AU_MVD_PR(dmsg, "parent dir is moved\n");
		goto out_dir;
	}

	mutex_lock_nested(&args->inode->i_mutex, I_MUTEX_CHILD);
	err = aufs_read_lock(dentry, AuLock_DW | AuLock_FLUSH);
	if (unlikely(err))
		goto out_inode;

	di_write_lock_parent(args->parent);
	err = au_mvd_args(dmsg, args);
	if (unlikely(err))
		goto out_parent;

	AuDbgDentry(dentry);
	AuDbgInode(args->inode);
	err = au_do_mvdown(dmsg, args);
	if (unlikely(err))
		goto out_parent;
	AuDbgDentry(dentry);
	AuDbgInode(args->inode);

	au_cpup_attr_timesizes(args->dir);
	au_cpup_attr_timesizes(args->inode);
	au_cpup_igen(args->inode, au_h_iptr(args->inode, args->mvd_bdst));
	/* au_digen_dec(dentry); */

out_parent:
	di_write_unlock(args->parent);
	aufs_read_unlock(dentry, AuLock_DW);
out_inode:
	mutex_unlock(&args->inode->i_mutex);
out_dir:
	mutex_unlock(&args->dir->i_mutex);
out_free:
	e = copy_to_user(uarg, &args->mvdown, sizeof(args->mvdown));
	if (unlikely(e))
		err = -EFAULT;
	kfree(args);
out:
	AuTraceErr(err);
	return err;
}
