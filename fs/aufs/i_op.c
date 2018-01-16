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
 * inode operations (except add/del/rename)
 */

#include <linux/device_cgroup.h>
#include <linux/fs_stack.h>
#include <linux/namei.h>
#include <linux/security.h>
#include "aufs.h"

static int h_permission(struct inode *h_inode, int mask,
			struct path *h_path, int brperm)
{
	int err;
	const unsigned char write_mask = !!(mask & (MAY_WRITE | MAY_APPEND));

	err = -EPERM;
	if (write_mask && IS_IMMUTABLE(h_inode))
		goto out;

	err = -EACCES;
	if (((mask & MAY_EXEC)
	     && S_ISREG(h_inode->i_mode)
	     && (path_noexec(h_path)
		 || !(h_inode->i_mode & S_IXUGO))))
		goto out;

	/*
	 * - skip the lower fs test in the case of write to ro branch.
	 * - nfs dir permission write check is optimized, but a policy for
	 *   link/rename requires a real check.
	 * - nfs always sets SB_POSIXACL regardless its mount option 'noacl.'
	 *   in this case, generic_permission() returns -EOPNOTSUPP.
	 */
	if ((write_mask && !au_br_writable(brperm))
	    || (au_test_nfs(h_inode->i_sb) && S_ISDIR(h_inode->i_mode)
		&& write_mask && !(mask & MAY_READ))
	    || !h_inode->i_op->permission) {
		/* AuLabel(generic_permission); */
		/* AuDbg("get_acl %pf\n", h_inode->i_op->get_acl); */
		err = generic_permission(h_inode, mask);
		if (err == -EOPNOTSUPP && au_test_nfs_noacl(h_inode))
			err = h_inode->i_op->permission(h_inode, mask);
		AuTraceErr(err);
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
	aufs_bindex_t bindex, bbot;
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

	if (!isdir
	    || write_mask
	    || au_opt_test(au_mntflags(sb), DIRPERM1)) {
		err = au_busy_or_stale();
		h_inode = au_h_iptr(inode, au_ibtop(inode));
		if (unlikely(!h_inode
			     || (h_inode->i_mode & S_IFMT)
			     != (inode->i_mode & S_IFMT)))
			goto out;

		err = 0;
		bindex = au_ibtop(inode);
		br = au_sbr(sb, bindex);
		err = h_permission(h_inode, mask, &br->br_path, br->br_perm);
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
	bbot = au_ibbot(inode);
	for (bindex = au_ibtop(inode); !err && bindex <= bbot; bindex++) {
		h_inode = au_h_iptr(inode, bindex);
		if (h_inode) {
			err = au_busy_or_stale();
			if (unlikely(!S_ISDIR(h_inode->i_mode)))
				break;

			br = au_sbr(sb, bindex);
			err = h_permission(h_inode, mask, &br->br_path,
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
				  unsigned int flags)
{
	struct dentry *ret, *parent;
	struct inode *inode;
	struct super_block *sb;
	int err, npositive;

	IMustLock(dir);

	/* todo: support rcu-walk? */
	ret = ERR_PTR(-ECHILD);
	if (flags & LOOKUP_RCU)
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
		/* regardless LOOKUP_CREATE, always ALLOW_NEG */
		npositive = au_lkup_dentry(dentry, au_dbtop(parent),
					   AuLkup_ALLOW_NEG);
		err = npositive;
	}
	di_read_unlock(parent, AuLock_IR);
	ret = ERR_PTR(err);
	if (unlikely(err < 0))
		goto out_unlock;

	if (npositive) {
		inode = au_new_inode(dentry, /*must_new*/0);
		if (IS_ERR(inode)) {
			ret = (void *)inode;
			inode = NULL;
			goto out_unlock;
		}
	}

	if (inode)
		atomic_inc(&inode->i_count);
	ret = d_splice_alias(inode, dentry);
#if 0
	if (unlikely(d_need_lookup(dentry))) {
		spin_lock(&dentry->d_lock);
		dentry->d_flags &= ~DCACHE_NEED_LOOKUP;
		spin_unlock(&dentry->d_lock);
	} else
#endif
	if (inode) {
		if (!IS_ERR(ret)) {
			iput(inode);
			if (ret && ret != dentry)
				ii_write_unlock(inode);
		} else {
			ii_write_unlock(inode);
			iput(inode);
			inode = NULL;
		}
	}

out_unlock:
	di_write_unlock(dentry);
out_si:
	si_read_unlock(sb);
out:
	return ret;
}

/* ---------------------------------------------------------------------- */

struct aopen_node {
	struct hlist_bl_node hblist;
	struct file *file, *h_file;
};

static int au_do_aopen(struct inode *inode, struct file *file)
{
	struct hlist_bl_head *aopen;
	struct hlist_bl_node *pos;
	struct aopen_node *node;
	struct au_do_open_args args = {
		.aopen	= 1,
		.open	= au_do_open_nondir
	};

	aopen = &au_sbi(inode->i_sb)->si_aopen;
	hlist_bl_lock(aopen);
	hlist_bl_for_each_entry(node, pos, aopen, hblist)
		if (node->file == file) {
			args.h_file = node->h_file;
			break;
		}
	hlist_bl_unlock(aopen);
	/* AuDebugOn(!args.h_file); */

	return au_do_open(file, &args);
}

static int aufs_atomic_open(struct inode *dir, struct dentry *dentry,
			    struct file *file, unsigned int open_flag,
			    umode_t create_mode, int *opened)
{
	int err, unlocked, h_opened = *opened;
	unsigned int lkup_flags;
	struct dentry *parent, *d;
	struct hlist_bl_head *aopen;
	struct vfsub_aopen_args args = {
		.open_flag	= open_flag,
		.create_mode	= create_mode,
		.opened		= &h_opened
	};
	struct aopen_node aopen_node = {
		.file	= file
	};

	IMustLock(dir);
	AuDbg("open_flag 0%o\n", open_flag);
	AuDbgDentry(dentry);

	err = 0;
	if (!au_di(dentry)) {
		lkup_flags = LOOKUP_OPEN;
		if (open_flag & O_CREAT)
			lkup_flags |= LOOKUP_CREATE;
		d = aufs_lookup(dir, dentry, lkup_flags);
		if (IS_ERR(d)) {
			err = PTR_ERR(d);
			AuTraceErr(err);
			goto out;
		} else if (d) {
			/*
			 * obsoleted dentry found.
			 * another error will be returned later.
			 */
			d_drop(d);
			AuDbgDentry(d);
			dput(d);
		}
		AuDbgDentry(dentry);
	}

	if (d_is_positive(dentry)
	    || d_unhashed(dentry)
	    || d_unlinked(dentry)
	    || !(open_flag & O_CREAT))
		goto out_no_open;

	unlocked = 0;
	err = aufs_read_lock(dentry, AuLock_DW | AuLock_FLUSH | AuLock_GEN);
	if (unlikely(err))
		goto out;

	parent = dentry->d_parent;	/* dir is locked */
	di_write_lock_parent(parent);
	err = au_lkup_dentry(dentry, /*btop*/0, AuLkup_ALLOW_NEG);
	if (unlikely(err))
		goto out_unlock;

	AuDbgDentry(dentry);
	if (d_is_positive(dentry))
		goto out_unlock;

	args.file = get_empty_filp();
	err = PTR_ERR(args.file);
	if (IS_ERR(args.file))
		goto out_unlock;

	args.file->f_flags = file->f_flags;
	err = au_aopen_or_create(dir, dentry, &args);
	AuTraceErr(err);
	AuDbgFile(args.file);
	if (unlikely(err < 0)) {
		if (h_opened & FILE_OPENED)
			fput(args.file);
		else
			put_filp(args.file);
		goto out_unlock;
	}
	di_write_unlock(parent);
	di_write_unlock(dentry);
	unlocked = 1;

	/* some filesystems don't set FILE_CREATED while succeeded? */
	*opened |= FILE_CREATED;
	if (h_opened & FILE_OPENED)
		aopen_node.h_file = args.file;
	else {
		put_filp(args.file);
		args.file = NULL;
	}
	aopen = &au_sbi(dir->i_sb)->si_aopen;
	au_hbl_add(&aopen_node.hblist, aopen);
	err = finish_open(file, dentry, au_do_aopen, opened);
	au_hbl_del(&aopen_node.hblist, aopen);
	AuTraceErr(err);
	AuDbgFile(file);
	if (aopen_node.h_file)
		fput(aopen_node.h_file);

out_unlock:
	if (unlocked)
		si_read_unlock(dentry->d_sb);
	else {
		di_write_unlock(parent);
		aufs_read_unlock(dentry, AuLock_DW);
	}
	AuDbgDentry(dentry);
	if (unlikely(err < 0))
		goto out;
out_no_open:
	if (err >= 0 && !(*opened & FILE_CREATED)) {
		AuLabel(out_no_open);
		dget(dentry);
		err = finish_no_open(file, dentry);
	}
out:
	AuDbg("%pd%s%s\n", dentry,
	      (*opened & FILE_CREATED) ? " created" : "",
	      (*opened & FILE_OPENED) ? " opened" : "");
	AuTraceErr(err);
	return err;
}


/* ---------------------------------------------------------------------- */

static int au_wr_dir_cpup(struct dentry *dentry, struct dentry *parent,
			  const unsigned char add_entry, aufs_bindex_t bcpup,
			  aufs_bindex_t btop)
{
	int err;
	struct dentry *h_parent;
	struct inode *h_dir;

	if (add_entry)
		IMustLock(d_inode(parent));
	else
		di_write_lock_parent(parent);

	err = 0;
	if (!au_h_dptr(parent, bcpup)) {
		if (btop > bcpup)
			err = au_cpup_dirs(dentry, bcpup);
		else if (btop < bcpup)
			err = au_cpdown_dirs(dentry, bcpup);
		else
			BUG();
	}
	if (!err && add_entry && !au_ftest_wrdir(add_entry, TMPFILE)) {
		h_parent = au_h_dptr(parent, bcpup);
		h_dir = d_inode(h_parent);
		vfsub_inode_lock_shared_nested(h_dir, AuLsc_I_PARENT);
		err = au_lkup_neg(dentry, bcpup, /*wh*/0);
		/* todo: no unlock here */
		inode_unlock_shared(h_dir);

		AuDbg("bcpup %d\n", bcpup);
		if (!err) {
			if (d_really_is_negative(dentry))
				au_set_h_dptr(dentry, btop, NULL);
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
	unsigned int flags;
	aufs_bindex_t bcpup, btop, src_btop;
	const unsigned char add_entry
		= au_ftest_wrdir(args->flags, ADD_ENTRY)
		| au_ftest_wrdir(args->flags, TMPFILE);
	struct super_block *sb;
	struct dentry *parent;
	struct au_sbinfo *sbinfo;

	sb = dentry->d_sb;
	sbinfo = au_sbi(sb);
	parent = dget_parent(dentry);
	btop = au_dbtop(dentry);
	bcpup = btop;
	if (args->force_btgt < 0) {
		if (src_dentry) {
			src_btop = au_dbtop(src_dentry);
			if (src_btop < btop)
				bcpup = src_btop;
		} else if (add_entry) {
			flags = 0;
			if (au_ftest_wrdir(args->flags, ISDIR))
				au_fset_wbr(flags, DIR);
			err = AuWbrCreate(sbinfo, dentry, flags);
			bcpup = err;
		}

		if (bcpup < 0 || au_test_ro(sb, bcpup, d_inode(dentry))) {
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
		AuDebugOn(au_test_ro(sb, bcpup, d_inode(dentry)));
	}

	AuDbg("btop %d, bcpup %d\n", btop, bcpup);
	err = bcpup;
	if (bcpup == btop)
		goto out; /* success */

	/* copyup the new parent into the branch we process */
	err = au_wr_dir_cpup(dentry, parent, add_entry, bcpup, btop);
	if (err >= 0) {
		if (d_really_is_negative(dentry)) {
			au_set_h_dptr(dentry, btop, NULL);
			au_set_dbtop(dentry, bcpup);
			au_set_dbbot(dentry, bcpup);
		}
		AuDebugOn(add_entry
			  && !au_ftest_wrdir(args->flags, TMPFILE)
			  && !au_h_dptr(dentry, bcpup));
	}

out:
	dput(parent);
	return err;
}

/* ---------------------------------------------------------------------- */

void au_pin_hdir_unlock(struct au_pin *p)
{
	if (p->hdir)
		au_hn_inode_unlock(p->hdir);
}

int au_pin_hdir_lock(struct au_pin *p)
{
	int err;

	err = 0;
	if (!p->hdir)
		goto out;

	/* even if an error happens later, keep this lock */
	au_hn_inode_lock_nested(p->hdir, p->lsc_hi);

	err = -EBUSY;
	if (unlikely(p->hdir->hi_inode != d_inode(p->h_parent)))
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
		if (d_is_positive(h_d[i])) {
			h_i = d_inode(h_d[i]);
			err = !h_i->i_nlink;
		}
	}

out:
	return err;
}

static void au_pin_hdir_set_owner(struct au_pin *p, struct task_struct *task)
{
#if !defined(CONFIG_RWSEM_GENERIC_SPINLOCK) && defined(CONFIG_RWSEM_SPIN_ON_OWNER)
	p->hdir->hi_inode->i_rwsem.owner = task;
#endif
}

void au_pin_hdir_acquire_nest(struct au_pin *p)
{
	if (p->hdir) {
		rwsem_acquire_nest(&p->hdir->hi_inode->i_rwsem.dep_map,
				   p->lsc_hi, 0, NULL, _RET_IP_);
		au_pin_hdir_set_owner(p, current);
	}
}

void au_pin_hdir_release(struct au_pin *p)
{
	if (p->hdir) {
		au_pin_hdir_set_owner(p, p->task);
		rwsem_release(&p->hdir->hi_inode->i_rwsem.dep_map, 1, _RET_IP_);
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
	if (p->hdir)
		au_pin_hdir_unlock(p);
	if (p->h_mnt && au_ftest_pin(p->flags, MNT_WRITE))
		vfsub_mnt_drop_write(p->h_mnt);
	if (!p->hdir)
		return;

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
			err = vfsub_mnt_want_write(p->h_mnt);
			if (unlikely(err)) {
				au_fclr_pin(p->flags, MNT_WRITE);
				goto out_err;
			}
		}
		goto out;
	}

	p->h_dentry = NULL;
	if (p->bindex <= au_dbbot(p->dentry))
		p->h_dentry = au_h_dptr(p->dentry, p->bindex);

	p->parent = dget_parent(p->dentry);
	if (!au_ftest_pin(p->flags, DI_LOCKED))
		di_read_lock(p->parent, AuLock_IR, p->lsc_di);

	h_dir = NULL;
	p->h_parent = au_h_dptr(p->parent, p->bindex);
	p->hdir = au_hi(d_inode(p->parent), p->bindex);
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

	if (au_ftest_pin(p->flags, MNT_WRITE)) {
		p->h_mnt = au_br_mnt(p->br);
		err = vfsub_mnt_want_write(p->h_mnt);
		if (unlikely(err)) {
			au_fclr_pin(p->flags, MNT_WRITE);
			if (!au_ftest_pin(p->flags, DI_LOCKED))
				di_read_unlock(p->parent, AuLock_IR);
			dput(p->parent);
			p->parent = NULL;
			goto out_err;
		}
	}

	au_igrab(h_dir);
	err = au_pin_hdir_lock(p);
	if (!err)
		goto out; /* success */

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
int au_reval_for_attr(struct dentry *dentry, unsigned int sigen)
{
	int err;
	struct dentry *parent;

	err = 0;
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

int au_pin_and_icpup(struct dentry *dentry, struct iattr *ia,
		     struct au_icpup_args *a)
{
	int err;
	loff_t sz;
	aufs_bindex_t btop, ibtop;
	struct dentry *hi_wh, *parent;
	struct inode *inode;
	struct au_wr_dir_args wr_dir_args = {
		.force_btgt	= -1,
		.flags		= 0
	};

	if (d_is_dir(dentry))
		au_fset_wrdir(wr_dir_args.flags, ISDIR);
	/* plink or hi_wh() case */
	btop = au_dbtop(dentry);
	inode = d_inode(dentry);
	ibtop = au_ibtop(inode);
	if (btop != ibtop && !au_test_ro(inode->i_sb, ibtop, inode))
		wr_dir_args.force_btgt = ibtop;
	err = au_wr_dir(dentry, /*src_dentry*/NULL, &wr_dir_args);
	if (unlikely(err < 0))
		goto out;
	a->btgt = err;
	if (err != btop)
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

	sz = -1;
	a->h_path.dentry = au_h_dptr(dentry, btop);
	a->h_inode = d_inode(a->h_path.dentry);
	if (ia && (ia->ia_valid & ATTR_SIZE)) {
		vfsub_inode_lock_shared_nested(a->h_inode, AuLsc_I_CHILD);
		if (ia->ia_size < i_size_read(a->h_inode))
			sz = ia->ia_size;
		inode_unlock_shared(a->h_inode);
	}

	hi_wh = NULL;
	if (au_ftest_icpup(a->flags, DID_CPUP) && d_unlinked(dentry)) {
		hi_wh = au_hi_wh(inode, a->btgt);
		if (!hi_wh) {
			struct au_cp_generic cpg = {
				.dentry	= dentry,
				.bdst	= a->btgt,
				.bsrc	= -1,
				.len	= sz,
				.pin	= &a->pin
			};
			err = au_sio_cpup_wh(&cpg, /*file*/NULL);
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
		struct au_cp_generic cpg = {
			.dentry	= dentry,
			.bdst	= a->btgt,
			.bsrc	= btop,
			.len	= sz,
			.pin	= &a->pin,
			.flags	= AuCpup_DTIME | AuCpup_HOPEN
		};
		err = au_sio_cpup_simple(&cpg);
		if (!err)
			a->h_path.dentry = au_h_dptr(dentry, a->btgt);
	} else if (!hi_wh)
		a->h_path.dentry = au_h_dptr(dentry, a->btgt);
	else
		a->h_path.dentry = hi_wh; /* do not dget here */

out_unlock:
	a->h_inode = d_inode(a->h_path.dentry);
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
		inode_lock_nested(a->h_inode, AuLsc_I_CHILD);
	return err;
}

static int aufs_setattr(struct dentry *dentry, struct iattr *ia)
{
	int err;
	struct inode *inode, *delegated;
	struct super_block *sb;
	struct file *file;
	struct au_icpup_args *a;

	inode = d_inode(dentry);
	IMustLock(inode);

	err = setattr_prepare(dentry, ia);
	if (unlikely(err))
		goto out;

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
		AuDebugOn(!d_is_reg(dentry));
		file = ia->ia_file;
		err = au_reval_and_lock_fdi(file, au_reopen_nondir, /*wlock*/1,
					    /*fi_lsc*/0);
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
		inode_unlock(a->h_inode);
		err = vfsub_trunc(&a->h_path, ia->ia_size, ia->ia_valid, f);
		inode_lock_nested(a->h_inode, AuLsc_I_CHILD);
	} else {
		delegated = NULL;
		while (1) {
			err = vfsub_notify_change(&a->h_path, ia, &delegated);
			if (delegated) {
				err = break_deleg_wait(&delegated);
				if (!err)
					continue;
			}
			break;
		}
	}
	/*
	 * regardless aufs 'acl' option setting.
	 * why don't all acl-aware fs call this func from their ->setattr()?
	 */
	if (!err && (ia->ia_valid & ATTR_MODE))
		err = vfsub_acl_chmod(a->h_inode, ia->ia_mode);
	if (!err)
		au_cpup_attr_changeable(inode);

out_unlock:
	inode_unlock(a->h_inode);
	au_unpin(&a->pin);
	if (unlikely(err))
		au_update_dbtop(dentry);
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

#if IS_ENABLED(CONFIG_AUFS_XATTR) || IS_ENABLED(CONFIG_FS_POSIX_ACL)
static int au_h_path_to_set_attr(struct dentry *dentry,
				 struct au_icpup_args *a, struct path *h_path)
{
	int err;
	struct super_block *sb;

	sb = dentry->d_sb;
	a->udba = au_opt_udba(sb);
	/* no d_unlinked(), to set UDBA_NONE for root */
	if (d_unhashed(dentry))
		a->udba = AuOpt_UDBA_NONE;
	if (a->udba != AuOpt_UDBA_NONE) {
		AuDebugOn(IS_ROOT(dentry));
		err = au_reval_for_attr(dentry, au_sigen(sb));
		if (unlikely(err))
			goto out;
	}
	err = au_pin_and_icpup(dentry, /*ia*/NULL, a);
	if (unlikely(err < 0))
		goto out;

	h_path->dentry = a->h_path.dentry;
	h_path->mnt = au_sbr_mnt(sb, a->btgt);

out:
	return err;
}

ssize_t au_sxattr(struct dentry *dentry, struct inode *inode,
		  struct au_sxattr *arg)
{
	int err;
	struct path h_path;
	struct super_block *sb;
	struct au_icpup_args *a;
	struct inode *h_inode;

	IMustLock(inode);

	err = -ENOMEM;
	a = kzalloc(sizeof(*a), GFP_NOFS);
	if (unlikely(!a))
		goto out;

	sb = dentry->d_sb;
	err = si_read_lock(sb, AuLock_FLUSH | AuLock_NOPLM);
	if (unlikely(err))
		goto out_kfree;

	h_path.dentry = NULL;	/* silence gcc */
	di_write_lock_child(dentry);
	err = au_h_path_to_set_attr(dentry, a, &h_path);
	if (unlikely(err))
		goto out_di;

	inode_unlock(a->h_inode);
	switch (arg->type) {
	case AU_XATTR_SET:
		AuDebugOn(d_is_negative(h_path.dentry));
		err = vfsub_setxattr(h_path.dentry,
				     arg->u.set.name, arg->u.set.value,
				     arg->u.set.size, arg->u.set.flags);
		break;
	case AU_ACL_SET:
		err = -EOPNOTSUPP;
		h_inode = d_inode(h_path.dentry);
		if (h_inode->i_op->set_acl)
			/* this will call posix_acl_update_mode */
			err = h_inode->i_op->set_acl(h_inode,
						     arg->u.acl_set.acl,
						     arg->u.acl_set.type);
		break;
	}
	if (!err)
		au_cpup_attr_timesizes(inode);

	au_unpin(&a->pin);
	if (unlikely(err))
		au_update_dbtop(dentry);

out_di:
	di_write_unlock(dentry);
	si_read_unlock(sb);
out_kfree:
	kfree(a);
out:
	AuTraceErr(err);
	return err;
}
#endif

static void au_refresh_iattr(struct inode *inode, struct kstat *st,
			     unsigned int nlink)
{
	unsigned int n;

	inode->i_mode = st->mode;
	/* don't i_[ug]id_write() here */
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
		smp_mb(); /* for i_nlink */
		/* 0 can happen */
		set_nlink(inode, n);
	}

	spin_lock(&inode->i_lock);
	inode->i_blocks = st->blocks;
	i_size_write(inode, st->size);
	spin_unlock(&inode->i_lock);
}

/*
 * common routine for aufs_getattr() and au_getxattr().
 * returns zero or negative (an error).
 * @dentry will be read-locked in success.
 */
int au_h_path_getattr(struct dentry *dentry, int force, struct path *h_path,
		      int locked)
{
	int err;
	unsigned int mnt_flags, sigen;
	unsigned char udba_none;
	aufs_bindex_t bindex;
	struct super_block *sb, *h_sb;
	struct inode *inode;

	h_path->mnt = NULL;
	h_path->dentry = NULL;

	err = 0;
	sb = dentry->d_sb;
	mnt_flags = au_mntflags(sb);
	udba_none = !!au_opt_test(mnt_flags, UDBA_NONE);

	if (unlikely(locked))
		goto body; /* skip locking dinfo */

	/* support fstat(2) */
	if (!d_unlinked(dentry) && !udba_none) {
		sigen = au_sigen(sb);
		err = au_digen_test(dentry, sigen);
		if (!err) {
			di_read_lock_child(dentry, AuLock_IR);
			err = au_dbrange_test(dentry);
			if (unlikely(err)) {
				di_read_unlock(dentry, AuLock_IR);
				goto out;
			}
		} else {
			AuDebugOn(IS_ROOT(dentry));
			di_write_lock_child(dentry);
			err = au_dbrange_test(dentry);
			if (!err)
				err = au_reval_for_attr(dentry, sigen);
			if (!err)
				di_downgrade_lock(dentry, AuLock_IR);
			else {
				di_write_unlock(dentry);
				goto out;
			}
		}
	} else
		di_read_lock_child(dentry, AuLock_IR);

body:
	inode = d_inode(dentry);
	bindex = au_ibtop(inode);
	h_path->mnt = au_sbr_mnt(sb, bindex);
	h_sb = h_path->mnt->mnt_sb;
	if (!force
	    && !au_test_fs_bad_iattr(h_sb)
	    && udba_none)
		goto out; /* success */

	if (au_dbtop(dentry) == bindex)
		h_path->dentry = au_h_dptr(dentry, bindex);
	else if (au_opt_test(mnt_flags, PLINK) && au_plink_test(inode)) {
		h_path->dentry = au_plink_lkup(inode, bindex);
		if (IS_ERR(h_path->dentry))
			/* pretending success */
			h_path->dentry = NULL;
		else
			dput(h_path->dentry);
	}

out:
	return err;
}

static int aufs_getattr(const struct path *path, struct kstat *st,
			u32 request, unsigned int query)
{
	int err;
	unsigned char positive;
	struct path h_path;
	struct dentry *dentry;
	struct inode *inode;
	struct super_block *sb;

	dentry = path->dentry;
	inode = d_inode(dentry);
	sb = dentry->d_sb;
	err = si_read_lock(sb, AuLock_FLUSH | AuLock_NOPLM);
	if (unlikely(err))
		goto out;
	err = au_h_path_getattr(dentry, /*force*/0, &h_path, /*locked*/0);
	if (unlikely(err))
		goto out_si;
	if (unlikely(!h_path.dentry))
		/* illegally overlapped or something */
		goto out_fill; /* pretending success */

	positive = d_is_positive(h_path.dentry);
	if (positive)
		/* no vfsub version */
		err = vfs_getattr(&h_path, st, request, query);
	if (!err) {
		if (positive)
			au_refresh_iattr(inode, st,
					 d_inode(h_path.dentry)->i_nlink);
		goto out_fill; /* success */
	}
	AuTraceErr(err);
	goto out_di;

out_fill:
	generic_fillattr(inode, st);
out_di:
	di_read_unlock(dentry, AuLock_IR);
out_si:
	si_read_unlock(sb);
out:
	AuTraceErr(err);
	return err;
}

/* ---------------------------------------------------------------------- */

static const char *aufs_get_link(struct dentry *dentry, struct inode *inode,
				 struct delayed_call *done)
{
	const char *ret;
	struct dentry *h_dentry;
	struct inode *h_inode;
	int err;
	aufs_bindex_t bindex;

	ret = NULL; /* suppress a warning */
	err = -ECHILD;
	if (!dentry)
		goto out;

	err = aufs_read_lock(dentry, AuLock_IR | AuLock_GEN);
	if (unlikely(err))
		goto out;

	err = au_d_hashed_positive(dentry);
	if (unlikely(err))
		goto out_unlock;

	err = -EINVAL;
	inode = d_inode(dentry);
	bindex = au_ibtop(inode);
	h_inode = au_h_iptr(inode, bindex);
	if (unlikely(!h_inode->i_op->get_link))
		goto out_unlock;

	err = -EBUSY;
	h_dentry = NULL;
	if (au_dbtop(dentry) <= bindex) {
		h_dentry = au_h_dptr(dentry, bindex);
		if (h_dentry)
			dget(h_dentry);
	}
	if (!h_dentry) {
		h_dentry = d_find_any_alias(h_inode);
		if (IS_ERR(h_dentry)) {
			err = PTR_ERR(h_dentry);
			goto out_unlock;
		}
	}
	if (unlikely(!h_dentry))
		goto out_unlock;

	err = 0;
	AuDbg("%pf\n", h_inode->i_op->get_link);
	AuDbgDentry(h_dentry);
	ret = vfs_get_link(h_dentry, done);
	dput(h_dentry);
	if (IS_ERR(ret))
		err = PTR_ERR(ret);

out_unlock:
	aufs_read_unlock(dentry, AuLock_IR);
out:
	if (unlikely(err))
		ret = ERR_PTR(err);
	AuTraceErrPtr(ret);
	return ret;
}

/* ---------------------------------------------------------------------- */

static int au_is_special(struct inode *inode)
{
	return (inode->i_mode & (S_IFBLK | S_IFCHR | S_IFIFO | S_IFSOCK));
}

static int aufs_update_time(struct inode *inode, struct timespec *ts, int flags)
{
	int err;
	aufs_bindex_t bindex;
	struct super_block *sb;
	struct inode *h_inode;
	struct vfsmount *h_mnt;

	sb = inode->i_sb;
	WARN_ONCE((flags & S_ATIME) && !IS_NOATIME(inode),
		  "unexpected s_flags 0x%lx", sb->s_flags);

	/* mmap_sem might be acquired already, cf. aufs_mmap() */
	lockdep_off();
	si_read_lock(sb, AuLock_FLUSH);
	ii_write_lock_child(inode);

	err = 0;
	bindex = au_ibtop(inode);
	h_inode = au_h_iptr(inode, bindex);
	if (!au_test_ro(sb, bindex, inode)) {
		h_mnt = au_sbr_mnt(sb, bindex);
		err = vfsub_mnt_want_write(h_mnt);
		if (!err) {
			err = vfsub_update_time(h_inode, ts, flags);
			vfsub_mnt_drop_write(h_mnt);
		}
	} else if (au_is_special(h_inode)) {
		/*
		 * Never copy-up here.
		 * These special files may already be opened and used for
		 * communicating. If we copied it up, then the communication
		 * would be corrupted.
		 */
		AuWarn1("timestamps for i%lu are ignored "
			"since it is on readonly branch (hi%lu).\n",
			inode->i_ino, h_inode->i_ino);
	} else if (flags & ~S_ATIME) {
		err = -EIO;
		AuIOErr1("unexpected flags 0x%x\n", flags);
		AuDebugOn(1);
	}

	if (!err)
		au_cpup_attr_timesizes(inode);
	ii_write_unlock(inode);
	si_read_unlock(sb);
	lockdep_on();

	if (!err && (flags & S_VERSION))
		inode_inc_iversion(inode);

	return err;
}

/* ---------------------------------------------------------------------- */

/* no getattr version will be set by module.c:aufs_init() */
struct inode_operations aufs_iop_nogetattr[AuIop_Last],
	aufs_iop[] = {
	[AuIop_SYMLINK] = {
		.permission	= aufs_permission,
#ifdef CONFIG_FS_POSIX_ACL
		.get_acl	= aufs_get_acl,
		.set_acl	= aufs_set_acl, /* unsupport for symlink? */
#endif

		.setattr	= aufs_setattr,
		.getattr	= aufs_getattr,

#ifdef CONFIG_AUFS_XATTR
		.listxattr	= aufs_listxattr,
#endif

		.get_link	= aufs_get_link,

		/* .update_time	= aufs_update_time */
	},
	[AuIop_DIR] = {
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
#ifdef CONFIG_FS_POSIX_ACL
		.get_acl	= aufs_get_acl,
		.set_acl	= aufs_set_acl,
#endif

		.setattr	= aufs_setattr,
		.getattr	= aufs_getattr,

#ifdef CONFIG_AUFS_XATTR
		.listxattr	= aufs_listxattr,
#endif

		.update_time	= aufs_update_time,
		.atomic_open	= aufs_atomic_open,
		.tmpfile	= aufs_tmpfile
	},
	[AuIop_OTHER] = {
		.permission	= aufs_permission,
#ifdef CONFIG_FS_POSIX_ACL
		.get_acl	= aufs_get_acl,
		.set_acl	= aufs_set_acl,
#endif

		.setattr	= aufs_setattr,
		.getattr	= aufs_getattr,

#ifdef CONFIG_AUFS_XATTR
		.listxattr	= aufs_listxattr,
#endif

		.update_time	= aufs_update_time
	}
};
