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
 * inode operations (except add/del/rename)
 */

#include <linux/device_cgroup.h>
#include <linux/fs_stack.h>
#include <linux/namei.h>
#include <linux/security.h>
#include "aufs.h"

static int h_permission(struct inode *h_inode, int mask,
			struct vfsmount *h_mnt, int brperm)
{
	int err;
	const unsigned char write_mask = !!(mask & (MAY_WRITE | MAY_APPEND));

	err = -EACCES;
	if ((write_mask && IS_IMMUTABLE(h_inode))
	    || ((mask & MAY_EXEC)
		&& S_ISREG(h_inode->i_mode)
		&& ((h_mnt->mnt_flags & MNT_NOEXEC)
		    || !(h_inode->i_mode & S_IXUGO))))
		goto out;

	/*
	 * - skip the lower fs test in the case of write to ro branch.
	 * - nfs dir permission write check is optimized, but a policy for
	 *   link/rename requires a real check.
	 */
	if ((write_mask && !au_br_writable(brperm))
	    || (au_test_nfs(h_inode->i_sb) && S_ISDIR(h_inode->i_mode)
		&& write_mask && !(mask & MAY_READ))
	    || !h_inode->i_op->permission) {
		/* AuLabel(generic_permission); */
		err = generic_permission(h_inode, mask);
	} else {
		/* AuLabel(h_inode->permission); */
		err = h_inode->i_op->permission(h_inode, mask);
		AuTraceErr(err);
	}

	if (!err)
		err = devcgroup_inode_permission(h_inode, mask);
	if (!err)
		err = security_inode_permission(h_inode, mask);

#if 0
	if (!err) {
		/* todo: do we need to call ima_path_check()? */
		struct path h_path = {
			.dentry	=
			.mnt	= h_mnt
		};
		err = ima_path_check(&h_path,
				     mask & (MAY_READ | MAY_WRITE | MAY_EXEC),
				     IMA_COUNT_LEAVE);
	}
#endif

out:
	return err;
}

static int aufs_permission(struct inode *inode, int mask)
{
	int err;
	aufs_bindex_t bindex, bend;
	const unsigned char isdir = !!S_ISDIR(inode->i_mode),
		write_mask = !!(mask & (MAY_WRITE | MAY_APPEND));
	struct inode *h_inode;
	struct super_block *sb;
	struct au_branch *br;

	/* todo: support rcu-walk? */
	if (mask & MAY_NOT_BLOCK)
		return -ECHILD;

	sb = inode->i_sb;
	si_read_lock(sb, AuLock_FLUSH);
	ii_read_lock_child(inode);
#if 0
	err = au_iigen_test(inode, au_sigen(sb));
	if (unlikely(err))
		goto out;
#endif

	if (!isdir || write_mask) {
		err = au_busy_or_stale();
		h_inode = au_h_iptr(inode, au_ibstart(inode));
		if (unlikely(!h_inode
			     || (h_inode->i_mode & S_IFMT)
			     != (inode->i_mode & S_IFMT)))
			goto out;

		err = 0;
		bindex = au_ibstart(inode);
		br = au_sbr(sb, bindex);
		err = h_permission(h_inode, mask, au_br_mnt(br), br->br_perm);
		if (write_mask
		    && !err
		    && !special_file(h_inode->i_mode)) {
			/* test whether the upper writable branch exists */
			err = -EROFS;
			for (; bindex >= 0; bindex--)
				if (!au_br_rdonly(au_sbr(sb, bindex))) {
					err = 0;
					break;
				}
		}
		goto out;
	}

	/* non-write to dir */
	err = 0;
	bend = au_ibend(inode);
	for (bindex = au_ibstart(inode); !err && bindex <= bend; bindex++) {
		h_inode = au_h_iptr(inode, bindex);
		if (h_inode) {
			err = au_busy_or_stale();
			if (unlikely(!S_ISDIR(h_inode->i_mode)))
				break;

			br = au_sbr(sb, bindex);
			err = h_permission(h_inode, mask, au_br_mnt(br),
					   br->br_perm);
		}
	}

out:
	ii_read_unlock(inode);
	si_read_unlock(sb);
	return err;
}

/* ---------------------------------------------------------------------- */

static struct dentry *aufs_lookup(struct inode *dir, struct dentry *dentry,
				  struct nameidata *nd)
{
	struct dentry *ret, *parent;
	struct inode *inode;
	struct super_block *sb;
	int err, npositive;

	IMustLock(dir);

	/* todo: support rcu-walk? */
	ret = ERR_PTR(-ECHILD);
	if (nd && (nd->flags & LOOKUP_RCU))
		goto out;

	ret = ERR_PTR(-ENAMETOOLONG);
	if (unlikely(dentry->d_name.len > AUFS_MAX_NAMELEN))
		goto out;

	sb = dir->i_sb;
	err = si_read_lock(sb, AuLock_FLUSH | AuLock_NOPLM);
	ret = ERR_PTR(err);
	if (unlikely(err))
		goto out;

	err = au_di_init(dentry);
	ret = ERR_PTR(err);
	if (unlikely(err))
		goto out_si;

	inode = NULL;
	npositive = 0; /* suppress a warning */
	parent = dentry->d_parent; /* dir inode is locked */
	di_read_lock_parent(parent, AuLock_IR);
	err = au_alive_dir(parent);
	if (!err)
		err = au_digen_test(parent, au_sigen(sb));
	if (!err) {
		npositive = au_lkup_dentry(dentry, au_dbstart(parent),
					   /*type*/0, nd);
		err = npositive;
	}
	di_read_unlock(parent, AuLock_IR);
	ret = ERR_PTR(err);
	if (unlikely(err < 0))
		goto out_unlock;

	if (npositive) {
		inode = au_new_inode(dentry, /*must_new*/0);
		ret = (void *)inode;
	}
	if (IS_ERR(inode)) {
		inode = NULL;
		goto out_unlock;
	}

	ret = d_splice_alias(inode, dentry);
#if 0
	if (unlikely(d_need_lookup(dentry))) {
		spin_lock(&dentry->d_lock);
		dentry->d_flags &= ~DCACHE_NEED_LOOKUP;
		spin_unlock(&dentry->d_lock);
	} else
#endif
	if (unlikely(IS_ERR(ret) && inode)) {
		ii_write_unlock(inode);
		iput(inode);
		inode = NULL;
	}

out_unlock:
	di_write_unlock(dentry);
	if (inode) {
		/* verbose coding for lock class name */
		if (unlikely(S_ISLNK(inode->i_mode)))
			au_rw_class(&au_di(dentry)->di_rwsem,
				    au_lc_key + AuLcSymlink_DIINFO);
		else if (unlikely(S_ISDIR(inode->i_mode)))
			au_rw_class(&au_di(dentry)->di_rwsem,
				    au_lc_key + AuLcDir_DIINFO);
		else /* likely */
			au_rw_class(&au_di(dentry)->di_rwsem,
				    au_lc_key + AuLcNonDir_DIINFO);
	}
out_si:
	si_read_unlock(sb);
out:
	return ret;
}

/* ---------------------------------------------------------------------- */

static int au_wr_dir_cpup(struct dentry *dentry, struct dentry *parent,
			  const unsigned char add_entry, aufs_bindex_t bcpup,
			  aufs_bindex_t bstart)
{
	int err;
	struct dentry *h_parent;
	struct inode *h_dir;

	if (add_entry)
		IMustLock(parent->d_inode);
	else
		di_write_lock_parent(parent);

	err = 0;
	if (!au_h_dptr(parent, bcpup)) {
		if (bstart < bcpup)
			err = au_cpdown_dirs(dentry, bcpup);
		else
			err = au_cpup_dirs(dentry, bcpup);
	}
	if (!err && add_entry) {
		h_parent = au_h_dptr(parent, bcpup);
		h_dir = h_parent->d_inode;
		mutex_lock_nested(&h_dir->i_mutex, AuLsc_I_PARENT);
		err = au_lkup_neg(dentry, bcpup,
				  au_ftest_wrdir(add_entry, TMP_WHENTRY));
		/* todo: no unlock here */
		mutex_unlock(&h_dir->i_mutex);

		AuDbg("bcpup %d\n", bcpup);
		if (!err) {
			if (!dentry->d_inode)
				au_set_h_dptr(dentry, bstart, NULL);
			au_update_dbrange(dentry, /*do_put_zero*/0);
		}
	}

	if (!add_entry)
		di_write_unlock(parent);
	if (!err)
		err = bcpup; /* success */

	AuTraceErr(err);
	return err;
}

/*
 * decide the branch and the parent dir where we will create a new entry.
 * returns new bindex or an error.
 * copyup the parent dir if needed.
 */
int au_wr_dir(struct dentry *dentry, struct dentry *src_dentry,
	      struct au_wr_dir_args *args)
{
	int err;
	aufs_bindex_t bcpup, bstart, src_bstart;
	const unsigned char add_entry
		= au_ftest_wrdir(args->flags, ADD_ENTRY)
		| au_ftest_wrdir(args->flags, TMP_WHENTRY);
	struct super_block *sb;
	struct dentry *parent;
	struct au_sbinfo *sbinfo;

	sb = dentry->d_sb;
	sbinfo = au_sbi(sb);
	parent = dget_parent(dentry);
	bstart = au_dbstart(dentry);
	bcpup = bstart;
	if (args->force_btgt < 0) {
		if (src_dentry) {
			src_bstart = au_dbstart(src_dentry);
			if (src_bstart < bstart)
				bcpup = src_bstart;
		} else if (add_entry) {
			err = AuWbrCreate(sbinfo, dentry,
					  au_ftest_wrdir(args->flags, ISDIR));
			bcpup = err;
		}

		if (bcpup < 0 || au_test_ro(sb, bcpup, dentry->d_inode)) {
			if (add_entry)
				err = AuWbrCopyup(sbinfo, dentry);
			else {
				if (!IS_ROOT(dentry)) {
					di_read_lock_parent(parent, !AuLock_IR);
					err = AuWbrCopyup(sbinfo, dentry);
					di_read_unlock(parent, !AuLock_IR);
				} else
					err = AuWbrCopyup(sbinfo, dentry);
			}
			bcpup = err;
			if (unlikely(err < 0))
				goto out;
		}
	} else {
		bcpup = args->force_btgt;
		AuDebugOn(au_test_ro(sb, bcpup, dentry->d_inode));
	}

	AuDbg("bstart %d, bcpup %d\n", bstart, bcpup);
	err = bcpup;
	if (bcpup == bstart)
		goto out; /* success */

	/* copyup the new parent into the branch we process */
	err = au_wr_dir_cpup(dentry, parent, add_entry, bcpup, bstart);
	if (err >= 0) {
		if (!dentry->d_inode) {
			au_set_h_dptr(dentry, bstart, NULL);
			au_set_dbstart(dentry, bcpup);
			au_set_dbend(dentry, bcpup);
		}
		AuDebugOn(add_entry && !au_h_dptr(dentry, bcpup));
	}

out:
	dput(parent);
	return err;
}

/* ---------------------------------------------------------------------- */

void au_pin_hdir_unlock(struct au_pin *p)
{
	if (p->hdir)
		au_hn_imtx_unlock(p->hdir);
}

static int au_pin_hdir_lock(struct au_pin *p)
{
	int err;

	err = 0;
	if (!p->hdir)
		goto out;

	/* even if an error happens later, keep this lock */
	au_hn_imtx_lock_nested(p->hdir, p->lsc_hi);

	err = -EBUSY;
	if (unlikely(p->hdir->hi_inode != p->h_parent->d_inode))
		goto out;

	err = 0;
	if (p->h_dentry)
		err = au_h_verify(p->h_dentry, p->udba, p->hdir->hi_inode,
				  p->h_parent, p->br);

out:
	return err;
}

int au_pin_hdir_relock(struct au_pin *p)
{
	int err, i;
	struct inode *h_i;
	struct dentry *h_d[] = {
		p->h_dentry,
		p->h_parent
	};

	err = au_pin_hdir_lock(p);
	if (unlikely(err))
		goto out;

	for (i = 0; !err && i < sizeof(h_d)/sizeof(*h_d); i++) {
		if (!h_d[i])
			continue;
		h_i = h_d[i]->d_inode;
		if (h_i)
			err = !h_i->i_nlink;
	}

out:
	return err;
}

void au_pin_hdir_set_owner(struct au_pin *p, struct task_struct *task)
{
#if defined(CONFIG_DEBUG_MUTEXES) || defined(CONFIG_SMP)
	p->hdir->hi_inode->i_mutex.owner = task;
#endif
}

void au_pin_hdir_acquire_nest(struct au_pin *p)
{
	if (p->hdir) {
		mutex_acquire_nest(&p->hdir->hi_inode->i_mutex.dep_map,
				   p->lsc_hi, 0, NULL, _RET_IP_);
		au_pin_hdir_set_owner(p, current);
	}
}

void au_pin_hdir_release(struct au_pin *p)
{
	if (p->hdir) {
		au_pin_hdir_set_owner(p, p->task);
		mutex_release(&p->hdir->hi_inode->i_mutex.dep_map, 1, _RET_IP_);
	}
}

struct dentry *au_pinned_h_parent(struct au_pin *pin)
{
	if (pin && pin->parent)
		return au_h_dptr(pin->parent, pin->bindex);
	return NULL;
}

void au_unpin(struct au_pin *p)
{
	if (p->h_mnt && au_ftest_pin(p->flags, MNT_WRITE))
		mnt_drop_write(p->h_mnt);
	if (!p->hdir)
		return;

	au_pin_hdir_unlock(p);
	if (!au_ftest_pin(p->flags, DI_LOCKED))
		di_read_unlock(p->parent, AuLock_IR);
	iput(p->hdir->hi_inode);
	dput(p->parent);
	p->parent = NULL;
	p->hdir = NULL;
	p->h_mnt = NULL;
	/* do not clear p->task */
}

int au_do_pin(struct au_pin *p)
{
	int err;
	struct super_block *sb;
	struct inode *h_dir;

	err = 0;
	sb = p->dentry->d_sb;
	p->br = au_sbr(sb, p->bindex);
	if (IS_ROOT(p->dentry)) {
		if (au_ftest_pin(p->flags, MNT_WRITE)) {
			p->h_mnt = au_br_mnt(p->br);
			err = mnt_want_write(p->h_mnt);
			if (unlikely(err)) {
				au_fclr_pin(p->flags, MNT_WRITE);
				goto out_err;
			}
		}
		goto out;
	}

	p->h_dentry = NULL;
	if (p->bindex <= au_dbend(p->dentry))
		p->h_dentry = au_h_dptr(p->dentry, p->bindex);

	p->parent = dget_parent(p->dentry);
	if (!au_ftest_pin(p->flags, DI_LOCKED))
		di_read_lock(p->parent, AuLock_IR, p->lsc_di);

	h_dir = NULL;
	p->h_parent = au_h_dptr(p->parent, p->bindex);
	p->hdir = au_hi(p->parent->d_inode, p->bindex);
	if (p->hdir)
		h_dir = p->hdir->hi_inode;

	/*
	 * udba case, or
	 * if DI_LOCKED is not set, then p->parent may be different
	 * and h_parent can be NULL.
	 */
	if (unlikely(!p->hdir || !h_dir || !p->h_parent)) {
		err = -EBUSY;
		if (!au_ftest_pin(p->flags, DI_LOCKED))
			di_read_unlock(p->parent, AuLock_IR);
		dput(p->parent);
		p->parent = NULL;
		goto out_err;
	}

	au_igrab(h_dir);
	err = au_pin_hdir_lock(p);
	if (unlikely(err))
		goto out_unpin;

	if (au_ftest_pin(p->flags, MNT_WRITE)) {
		p->h_mnt = au_br_mnt(p->br);
		err = mnt_want_write(p->h_mnt);
		if (unlikely(err)) {
			au_fclr_pin(p->flags, MNT_WRITE);
			goto out_unpin;
		}
	}
	goto out; /* success */

out_unpin:
	au_unpin(p);
out_err:
	pr_err("err %d\n", err);
	err = au_busy_or_stale();
out:
	return err;
}

void au_pin_init(struct au_pin *p, struct dentry *dentry,
		 aufs_bindex_t bindex, int lsc_di, int lsc_hi,
		 unsigned int udba, unsigned char flags)
{
	p->dentry = dentry;
	p->udba = udba;
	p->lsc_di = lsc_di;
	p->lsc_hi = lsc_hi;
	p->flags = flags;
	p->bindex = bindex;

	p->parent = NULL;
	p->hdir = NULL;
	p->h_mnt = NULL;

	p->h_dentry = NULL;
	p->h_parent = NULL;
	p->br = NULL;
	p->task = current;
}

int au_pin(struct au_pin *pin, struct dentry *dentry, aufs_bindex_t bindex,
	   unsigned int udba, unsigned char flags)
{
	au_pin_init(pin, dentry, bindex, AuLsc_DI_PARENT, AuLsc_I_PARENT2,
		    udba, flags);
	return au_do_pin(pin);
}

/* ---------------------------------------------------------------------- */

/*
 * ->setattr() and ->getattr() are called in various cases.
 * chmod, stat: dentry is revalidated.
 * fchmod, fstat: file and dentry are not revalidated, additionally they may be
 *		  unhashed.
 * for ->setattr(), ia->ia_file is passed from ftruncate only.
 */
/* todo: consolidate with do_refresh() and simple_reval_dpath() */
static int au_reval_for_attr(struct dentry *dentry, unsigned int sigen)
{
	int err;
	struct inode *inode;
	struct dentry *parent;

	err = 0;
	inode = dentry->d_inode;
	if (au_digen_test(dentry, sigen)) {
		parent = dget_parent(dentry);
		di_read_lock_parent(parent, AuLock_IR);
		err = au_refresh_dentry(dentry, parent);
		di_read_unlock(parent, AuLock_IR);
		dput(parent);
	}

	AuTraceErr(err);
	return err;
}

#define AuIcpup_DID_CPUP	1
#define au_ftest_icpup(flags, name)	((flags) & AuIcpup_##name)
#define au_fset_icpup(flags, name) \
	do { (flags) |= AuIcpup_##name; } while (0)
#define au_fclr_icpup(flags, name) \
	do { (flags) &= ~AuIcpup_##name; } while (0)

struct au_icpup_args {
	unsigned char flags;
	unsigned char pin_flags;
	aufs_bindex_t btgt;
	unsigned int udba;
	struct au_pin pin;
	struct path h_path;
	struct inode *h_inode;
};

static int au_pin_and_icpup(struct dentry *dentry, struct iattr *ia,
			    struct au_icpup_args *a)
{
	int err;
	loff_t sz;
	aufs_bindex_t bstart, ibstart;
	struct dentry *hi_wh, *parent;
	struct inode *inode;
	struct file *h_file;
	struct au_wr_dir_args wr_dir_args = {
		.force_btgt	= -1,
		.flags		= 0
	};

	bstart = au_dbstart(dentry);
	inode = dentry->d_inode;
	if (S_ISDIR(inode->i_mode))
		au_fset_wrdir(wr_dir_args.flags, ISDIR);
	/* plink or hi_wh() case */
	ibstart = au_ibstart(inode);
	if (bstart != ibstart && !au_test_ro(inode->i_sb, ibstart, inode))
		wr_dir_args.force_btgt = ibstart;
	err = au_wr_dir(dentry, /*src_dentry*/NULL, &wr_dir_args);
	if (unlikely(err < 0))
		goto out;
	a->btgt = err;
	if (err != bstart)
		au_fset_icpup(a->flags, DID_CPUP);

	err = 0;
	a->pin_flags = AuPin_MNT_WRITE;
	parent = NULL;
	if (!IS_ROOT(dentry)) {
		au_fset_pin(a->pin_flags, DI_LOCKED);
		parent = dget_parent(dentry);
		di_write_lock_parent(parent);
	}

	err = au_pin(&a->pin, dentry, a->btgt, a->udba, a->pin_flags);
	if (unlikely(err))
		goto out_parent;

	a->h_path.dentry = au_h_dptr(dentry, bstart);
	a->h_inode = a->h_path.dentry->d_inode;
	mutex_lock_nested(&a->h_inode->i_mutex, AuLsc_I_CHILD);
	sz = -1;
	if ((ia->ia_valid & ATTR_SIZE) && ia->ia_size < i_size_read(a->h_inode))
		sz = ia->ia_size;
	mutex_unlock(&a->h_inode->i_mutex);

	h_file = NULL;
	hi_wh = NULL;
	if (au_ftest_icpup(a->flags, DID_CPUP) && d_unlinked(dentry)) {
		hi_wh = au_hi_wh(inode, a->btgt);
		if (!hi_wh) {
			err = au_sio_cpup_wh(dentry, a->btgt, sz, /*file*/NULL,
					     &a->pin);
			if (unlikely(err))
				goto out_unlock;
			hi_wh = au_hi_wh(inode, a->btgt);
			/* todo: revalidate hi_wh? */
		}
	}

	if (parent) {
		au_pin_set_parent_lflag(&a->pin, /*lflag*/0);
		di_downgrade_lock(parent, AuLock_IR);
		dput(parent);
		parent = NULL;
	}
	if (!au_ftest_icpup(a->flags, DID_CPUP))
		goto out; /* success */

	if (!d_unhashed(dentry)) {
		h_file = au_h_open_pre(dentry, bstart);
		if (IS_ERR(h_file))
			err = PTR_ERR(h_file);
		else {
			err = au_sio_cpup_simple(dentry, a->btgt, sz,
						 AuCpup_DTIME, &a->pin);
			au_h_open_post(dentry, bstart, h_file);
		}
		if (!err)
			a->h_path.dentry = au_h_dptr(dentry, a->btgt);
	} else if (!hi_wh)
		a->h_path.dentry = au_h_dptr(dentry, a->btgt);
	else
		a->h_path.dentry = hi_wh; /* do not dget here */

out_unlock:
	a->h_inode = a->h_path.dentry->d_inode;
	if (!err)
		goto out; /* success */
	au_unpin(&a->pin);
out_parent:
	if (parent) {
		di_write_unlock(parent);
		dput(parent);
	}
out:
	if (!err)
		mutex_lock_nested(&a->h_inode->i_mutex, AuLsc_I_CHILD);
	return err;
}

static int aufs_setattr(struct dentry *dentry, struct iattr *ia)
{
	int err;
	struct inode *inode;
	struct super_block *sb;
	struct file *file;
	struct au_icpup_args *a;

	inode = dentry->d_inode;
	IMustLock(inode);

	err = -ENOMEM;
	a = kzalloc(sizeof(*a), GFP_NOFS);
	if (unlikely(!a))
		goto out;

	if (ia->ia_valid & (ATTR_KILL_SUID | ATTR_KILL_SGID))
		ia->ia_valid &= ~ATTR_MODE;

	file = NULL;
	sb = dentry->d_sb;
	err = si_read_lock(sb, AuLock_FLUSH | AuLock_NOPLM);
	if (unlikely(err))
		goto out_kfree;

	if (ia->ia_valid & ATTR_FILE) {
		/* currently ftruncate(2) only */
		AuDebugOn(!S_ISREG(inode->i_mode));
		file = ia->ia_file;
		err = au_reval_and_lock_fdi(file, au_reopen_nondir, /*wlock*/1);
		if (unlikely(err))
			goto out_si;
		ia->ia_file = au_hf_top(file);
		a->udba = AuOpt_UDBA_NONE;
	} else {
		/* fchmod() doesn't pass ia_file */
		a->udba = au_opt_udba(sb);
		di_write_lock_child(dentry);
		/* no d_unlinked(), to set UDBA_NONE for root */
		if (d_unhashed(dentry))
			a->udba = AuOpt_UDBA_NONE;
		if (a->udba != AuOpt_UDBA_NONE) {
			AuDebugOn(IS_ROOT(dentry));
			err = au_reval_for_attr(dentry, au_sigen(sb));
			if (unlikely(err))
				goto out_dentry;
		}
	}

	err = au_pin_and_icpup(dentry, ia, a);
	if (unlikely(err < 0))
		goto out_dentry;
	if (au_ftest_icpup(a->flags, DID_CPUP)) {
		ia->ia_file = NULL;
		ia->ia_valid &= ~ATTR_FILE;
	}

	a->h_path.mnt = au_sbr_mnt(sb, a->btgt);
	if ((ia->ia_valid & (ATTR_MODE | ATTR_CTIME))
	    == (ATTR_MODE | ATTR_CTIME)) {
		err = security_path_chmod(&a->h_path, ia->ia_mode);
		if (unlikely(err))
			goto out_unlock;
	} else if ((ia->ia_valid & (ATTR_UID | ATTR_GID))
		   && (ia->ia_valid & ATTR_CTIME)) {
		err = security_path_chown(&a->h_path, ia->ia_uid, ia->ia_gid);
		if (unlikely(err))
			goto out_unlock;
	}

	if (ia->ia_valid & ATTR_SIZE) {
		struct file *f;

		if (ia->ia_size < i_size_read(inode))
			/* unmap only */
			truncate_setsize(inode, ia->ia_size);

		f = NULL;
		if (ia->ia_valid & ATTR_FILE)
			f = ia->ia_file;
		mutex_unlock(&a->h_inode->i_mutex);
		err = vfsub_trunc(&a->h_path, ia->ia_size, ia->ia_valid, f);
		mutex_lock_nested(&a->h_inode->i_mutex, AuLsc_I_CHILD);
	} else
		err = vfsub_notify_change(&a->h_path, ia);
	if (!err)
		au_cpup_attr_changeable(inode);

out_unlock:
	mutex_unlock(&a->h_inode->i_mutex);
	au_unpin(&a->pin);
	if (unlikely(err))
		au_update_dbstart(dentry);
out_dentry:
	di_write_unlock(dentry);
	if (file) {
		fi_write_unlock(file);
		ia->ia_file = file;
		ia->ia_valid |= ATTR_FILE;
	}
out_si:
	si_read_unlock(sb);
out_kfree:
	kfree(a);
out:
	AuTraceErr(err);
	return err;
}

static void au_refresh_iattr(struct inode *inode, struct kstat *st,
			     unsigned int nlink)
{
	unsigned int n;

	inode->i_mode = st->mode;
	inode->i_uid = st->uid;
	inode->i_gid = st->gid;
	inode->i_atime = st->atime;
	inode->i_mtime = st->mtime;
	inode->i_ctime = st->ctime;

	au_cpup_attr_nlink(inode, /*force*/0);
	if (S_ISDIR(inode->i_mode)) {
		n = inode->i_nlink;
		n -= nlink;
		n += st->nlink;
		smp_mb();
		/* 0 can happen */
		set_nlink(inode, n);
	}

	spin_lock(&inode->i_lock);
	inode->i_blocks = st->blocks;
	i_size_write(inode, st->size);
	spin_unlock(&inode->i_lock);
}

static int aufs_getattr(struct vfsmount *mnt __maybe_unused,
			struct dentry *dentry, struct kstat *st)
{
	int err;
	unsigned int mnt_flags;
	aufs_bindex_t bindex;
	unsigned char udba_none, positive;
	struct super_block *sb, *h_sb;
	struct inode *inode;
	struct vfsmount *h_mnt;
	struct dentry *h_dentry;

	sb = dentry->d_sb;
	inode = dentry->d_inode;
	err = si_read_lock(sb, AuLock_FLUSH | AuLock_NOPLM);
	if (unlikely(err))
		goto out;
	mnt_flags = au_mntflags(sb);
	udba_none = !!au_opt_test(mnt_flags, UDBA_NONE);

	/* support fstat(2) */
	if (!d_unlinked(dentry) && !udba_none) {
		unsigned int sigen = au_sigen(sb);
		err = au_digen_test(dentry, sigen);
		if (!err) {
			di_read_lock_child(dentry, AuLock_IR);
			err = au_dbrange_test(dentry);
			if (unlikely(err))
				goto out_unlock;
		} else {
			AuDebugOn(IS_ROOT(dentry));
			di_write_lock_child(dentry);
			err = au_dbrange_test(dentry);
			if (!err)
				err = au_reval_for_attr(dentry, sigen);
			di_downgrade_lock(dentry, AuLock_IR);
			if (unlikely(err))
				goto out_unlock;
		}
	} else
		di_read_lock_child(dentry, AuLock_IR);

	bindex = au_ibstart(inode);
	h_mnt = au_sbr_mnt(sb, bindex);
	h_sb = h_mnt->mnt_sb;
	if (!au_test_fs_bad_iattr(h_sb) && udba_none)
		goto out_fill; /* success */

	h_dentry = NULL;
	if (au_dbstart(dentry) == bindex)
		h_dentry = dget(au_h_dptr(dentry, bindex));
	else if (au_opt_test(mnt_flags, PLINK) && au_plink_test(inode)) {
		h_dentry = au_plink_lkup(inode, bindex);
		if (IS_ERR(h_dentry))
			goto out_fill; /* pretending success */
	}
	/* illegally overlapped or something */
	if (unlikely(!h_dentry))
		goto out_fill; /* pretending success */

	positive = !!h_dentry->d_inode;
	if (positive)
		err = vfs_getattr(h_mnt, h_dentry, st);
	dput(h_dentry);
	if (!err) {
		if (positive)
			au_refresh_iattr(inode, st, h_dentry->d_inode->i_nlink);
		goto out_fill; /* success */
	}
	AuTraceErr(err);
	goto out_unlock;

out_fill:
	generic_fillattr(inode, st);
out_unlock:
	di_read_unlock(dentry, AuLock_IR);
	si_read_unlock(sb);
out:
	AuTraceErr(err);
	return err;
}

/* ---------------------------------------------------------------------- */

static int h_readlink(struct dentry *dentry, int bindex, char __user *buf,
		      int bufsiz)
{
	int err;
	struct super_block *sb;
	struct dentry *h_dentry;

	err = -EINVAL;
	h_dentry = au_h_dptr(dentry, bindex);
	if (unlikely(!h_dentry->d_inode->i_op->readlink))
		goto out;

	err = security_inode_readlink(h_dentry);
	if (unlikely(err))
		goto out;

	sb = dentry->d_sb;
	if (!au_test_ro(sb, bindex, dentry->d_inode)) {
		vfsub_touch_atime(au_sbr_mnt(sb, bindex), h_dentry);
		fsstack_copy_attr_atime(dentry->d_inode, h_dentry->d_inode);
	}
	err = h_dentry->d_inode->i_op->readlink(h_dentry, buf, bufsiz);

out:
	return err;
}

static int aufs_readlink(struct dentry *dentry, char __user *buf, int bufsiz)
{
	int err;

	err = aufs_read_lock(dentry, AuLock_IR | AuLock_GEN);
	if (unlikely(err))
		goto out;
	err = au_d_hashed_positive(dentry);
	if (!err)
		err = h_readlink(dentry, au_dbstart(dentry), buf, bufsiz);
	aufs_read_unlock(dentry, AuLock_IR);

out:
	return err;
}

static void *aufs_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	int err;
	mm_segment_t old_fs;
	union {
		char *k;
		char __user *u;
	} buf;

	err = -ENOMEM;
	buf.k = __getname_gfp(GFP_NOFS);
	if (unlikely(!buf.k))
		goto out;

	err = aufs_read_lock(dentry, AuLock_IR | AuLock_GEN);
	if (unlikely(err))
		goto out_name;

	err = au_d_hashed_positive(dentry);
	if (!err) {
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		err = h_readlink(dentry, au_dbstart(dentry), buf.u, PATH_MAX);
		set_fs(old_fs);
	}
	aufs_read_unlock(dentry, AuLock_IR);

	if (err >= 0) {
		buf.k[err] = 0;
		/* will be freed by put_link */
		nd_set_link(nd, buf.k);
		return NULL; /* success */
	}

out_name:
	__putname(buf.k);
out:
	path_put(&nd->path);
	AuTraceErr(err);
	return ERR_PTR(err);
}

static void aufs_put_link(struct dentry *dentry __maybe_unused,
			  struct nameidata *nd, void *cookie __maybe_unused)
{
	__putname(nd_get_link(nd));
}

/* ---------------------------------------------------------------------- */

static void aufs_truncate_range(struct inode *inode __maybe_unused,
				loff_t start __maybe_unused,
				loff_t end __maybe_unused)
{
	AuUnsupport();
}

/* ---------------------------------------------------------------------- */

struct inode_operations aufs_symlink_iop = {
	.permission	= aufs_permission,
	.setattr	= aufs_setattr,
	.getattr	= aufs_getattr,
	.readlink	= aufs_readlink,
	.follow_link	= aufs_follow_link,
	.put_link	= aufs_put_link
};

struct inode_operations aufs_dir_iop = {
	.create		= aufs_create,
	.lookup		= aufs_lookup,
	.link		= aufs_link,
	.unlink		= aufs_unlink,
	.symlink	= aufs_symlink,
	.mkdir		= aufs_mkdir,
	.rmdir		= aufs_rmdir,
	.mknod		= aufs_mknod,
	.rename		= aufs_rename,

	.permission	= aufs_permission,
	.setattr	= aufs_setattr,
	.getattr	= aufs_getattr
};

struct inode_operations aufs_iop = {
	.permission	= aufs_permission,
	.setattr	= aufs_setattr,
	.getattr	= aufs_getattr,
	.truncate_range	= aufs_truncate_range
};
