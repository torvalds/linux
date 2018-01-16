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
 * directory operations
 */

#include <linux/fs_stack.h>
#include "aufs.h"

void au_add_nlink(struct inode *dir, struct inode *h_dir)
{
	unsigned int nlink;

	AuDebugOn(!S_ISDIR(dir->i_mode) || !S_ISDIR(h_dir->i_mode));

	nlink = dir->i_nlink;
	nlink += h_dir->i_nlink - 2;
	if (h_dir->i_nlink < 2)
		nlink += 2;
	smp_mb(); /* for i_nlink */
	/* 0 can happen in revaliding */
	set_nlink(dir, nlink);
}

void au_sub_nlink(struct inode *dir, struct inode *h_dir)
{
	unsigned int nlink;

	AuDebugOn(!S_ISDIR(dir->i_mode) || !S_ISDIR(h_dir->i_mode));

	nlink = dir->i_nlink;
	nlink -= h_dir->i_nlink - 2;
	if (h_dir->i_nlink < 2)
		nlink -= 2;
	smp_mb(); /* for i_nlink */
	/* nlink == 0 means the branch-fs is broken */
	set_nlink(dir, nlink);
}

loff_t au_dir_size(struct file *file, struct dentry *dentry)
{
	loff_t sz;
	aufs_bindex_t bindex, bbot;
	struct file *h_file;
	struct dentry *h_dentry;

	sz = 0;
	if (file) {
		AuDebugOn(!d_is_dir(file->f_path.dentry));

		bbot = au_fbbot_dir(file);
		for (bindex = au_fbtop(file);
		     bindex <= bbot && sz < KMALLOC_MAX_SIZE;
		     bindex++) {
			h_file = au_hf_dir(file, bindex);
			if (h_file && file_inode(h_file))
				sz += vfsub_f_size_read(h_file);
		}
	} else {
		AuDebugOn(!dentry);
		AuDebugOn(!d_is_dir(dentry));

		bbot = au_dbtaildir(dentry);
		for (bindex = au_dbtop(dentry);
		     bindex <= bbot && sz < KMALLOC_MAX_SIZE;
		     bindex++) {
			h_dentry = au_h_dptr(dentry, bindex);
			if (h_dentry && d_is_positive(h_dentry))
				sz += i_size_read(d_inode(h_dentry));
		}
	}
	if (sz < KMALLOC_MAX_SIZE)
		sz = roundup_pow_of_two(sz);
	if (sz > KMALLOC_MAX_SIZE)
		sz = KMALLOC_MAX_SIZE;
	else if (sz < NAME_MAX) {
		BUILD_BUG_ON(AUFS_RDBLK_DEF < NAME_MAX);
		sz = AUFS_RDBLK_DEF;
	}
	return sz;
}

struct au_dir_ts_arg {
	struct dentry *dentry;
	aufs_bindex_t brid;
};

static void au_do_dir_ts(void *arg)
{
	struct au_dir_ts_arg *a = arg;
	struct au_dtime dt;
	struct path h_path;
	struct inode *dir, *h_dir;
	struct super_block *sb;
	struct au_branch *br;
	struct au_hinode *hdir;
	int err;
	aufs_bindex_t btop, bindex;

	sb = a->dentry->d_sb;
	if (d_really_is_negative(a->dentry))
		goto out;
	/* no dir->i_mutex lock */
	aufs_read_lock(a->dentry, AuLock_DW); /* noflush */

	dir = d_inode(a->dentry);
	btop = au_ibtop(dir);
	bindex = au_br_index(sb, a->brid);
	if (bindex < btop)
		goto out_unlock;

	br = au_sbr(sb, bindex);
	h_path.dentry = au_h_dptr(a->dentry, bindex);
	if (!h_path.dentry)
		goto out_unlock;
	h_path.mnt = au_br_mnt(br);
	au_dtime_store(&dt, a->dentry, &h_path);

	br = au_sbr(sb, btop);
	if (!au_br_writable(br->br_perm))
		goto out_unlock;
	h_path.dentry = au_h_dptr(a->dentry, btop);
	h_path.mnt = au_br_mnt(br);
	err = vfsub_mnt_want_write(h_path.mnt);
	if (err)
		goto out_unlock;
	hdir = au_hi(dir, btop);
	au_hn_inode_lock_nested(hdir, AuLsc_I_PARENT);
	h_dir = au_h_iptr(dir, btop);
	if (h_dir->i_nlink
	    && timespec_compare(&h_dir->i_mtime, &dt.dt_mtime) < 0) {
		dt.dt_h_path = h_path;
		au_dtime_revert(&dt);
	}
	au_hn_inode_unlock(hdir);
	vfsub_mnt_drop_write(h_path.mnt);
	au_cpup_attr_timesizes(dir);

out_unlock:
	aufs_read_unlock(a->dentry, AuLock_DW);
out:
	dput(a->dentry);
	au_nwt_done(&au_sbi(sb)->si_nowait);
	kfree(arg);
}

void au_dir_ts(struct inode *dir, aufs_bindex_t bindex)
{
	int perm, wkq_err;
	aufs_bindex_t btop;
	struct au_dir_ts_arg *arg;
	struct dentry *dentry;
	struct super_block *sb;

	IMustLock(dir);

	dentry = d_find_any_alias(dir);
	AuDebugOn(!dentry);
	sb = dentry->d_sb;
	btop = au_ibtop(dir);
	if (btop == bindex) {
		au_cpup_attr_timesizes(dir);
		goto out;
	}

	perm = au_sbr_perm(sb, btop);
	if (!au_br_writable(perm))
		goto out;

	arg = kmalloc(sizeof(*arg), GFP_NOFS);
	if (!arg)
		goto out;

	arg->dentry = dget(dentry); /* will be dput-ted by au_do_dir_ts() */
	arg->brid = au_sbr_id(sb, bindex);
	wkq_err = au_wkq_nowait(au_do_dir_ts, arg, sb, /*flags*/0);
	if (unlikely(wkq_err)) {
		pr_err("wkq %d\n", wkq_err);
		dput(dentry);
		kfree(arg);
	}

out:
	dput(dentry);
}

/* ---------------------------------------------------------------------- */

static int reopen_dir(struct file *file)
{
	int err;
	unsigned int flags;
	aufs_bindex_t bindex, btail, btop;
	struct dentry *dentry, *h_dentry;
	struct file *h_file;

	/* open all lower dirs */
	dentry = file->f_path.dentry;
	btop = au_dbtop(dentry);
	for (bindex = au_fbtop(file); bindex < btop; bindex++)
		au_set_h_fptr(file, bindex, NULL);
	au_set_fbtop(file, btop);

	btail = au_dbtaildir(dentry);
	for (bindex = au_fbbot_dir(file); btail < bindex; bindex--)
		au_set_h_fptr(file, bindex, NULL);
	au_set_fbbot_dir(file, btail);

	flags = vfsub_file_flags(file);
	for (bindex = btop; bindex <= btail; bindex++) {
		h_dentry = au_h_dptr(dentry, bindex);
		if (!h_dentry)
			continue;
		h_file = au_hf_dir(file, bindex);
		if (h_file)
			continue;

		h_file = au_h_open(dentry, bindex, flags, file, /*force_wr*/0);
		err = PTR_ERR(h_file);
		if (IS_ERR(h_file))
			goto out; /* close all? */
		au_set_h_fptr(file, bindex, h_file);
	}
	au_update_figen(file);
	/* todo: necessary? */
	/* file->f_ra = h_file->f_ra; */
	err = 0;

out:
	return err;
}

static int do_open_dir(struct file *file, int flags, struct file *h_file)
{
	int err;
	aufs_bindex_t bindex, btail;
	struct dentry *dentry, *h_dentry;
	struct vfsmount *mnt;

	FiMustWriteLock(file);
	AuDebugOn(h_file);

	err = 0;
	mnt = file->f_path.mnt;
	dentry = file->f_path.dentry;
	file->f_version = d_inode(dentry)->i_version;
	bindex = au_dbtop(dentry);
	au_set_fbtop(file, bindex);
	btail = au_dbtaildir(dentry);
	au_set_fbbot_dir(file, btail);
	for (; !err && bindex <= btail; bindex++) {
		h_dentry = au_h_dptr(dentry, bindex);
		if (!h_dentry)
			continue;

		err = vfsub_test_mntns(mnt, h_dentry->d_sb);
		if (unlikely(err))
			break;
		h_file = au_h_open(dentry, bindex, flags, file, /*force_wr*/0);
		if (IS_ERR(h_file)) {
			err = PTR_ERR(h_file);
			break;
		}
		au_set_h_fptr(file, bindex, h_file);
	}
	au_update_figen(file);
	/* todo: necessary? */
	/* file->f_ra = h_file->f_ra; */
	if (!err)
		return 0; /* success */

	/* close all */
	for (bindex = au_fbtop(file); bindex <= btail; bindex++)
		au_set_h_fptr(file, bindex, NULL);
	au_set_fbtop(file, -1);
	au_set_fbbot_dir(file, -1);

	return err;
}

static int aufs_open_dir(struct inode *inode __maybe_unused,
			 struct file *file)
{
	int err;
	struct super_block *sb;
	struct au_fidir *fidir;

	err = -ENOMEM;
	sb = file->f_path.dentry->d_sb;
	si_read_lock(sb, AuLock_FLUSH);
	fidir = au_fidir_alloc(sb);
	if (fidir) {
		struct au_do_open_args args = {
			.open	= do_open_dir,
			.fidir	= fidir
		};
		err = au_do_open(file, &args);
		if (unlikely(err))
			kfree(fidir);
	}
	si_read_unlock(sb);
	return err;
}

static int aufs_release_dir(struct inode *inode __maybe_unused,
			    struct file *file)
{
	struct au_vdir *vdir_cache;
	struct au_finfo *finfo;
	struct au_fidir *fidir;
	struct au_hfile *hf;
	aufs_bindex_t bindex, bbot;

	finfo = au_fi(file);
	fidir = finfo->fi_hdir;
	if (fidir) {
		au_hbl_del(&finfo->fi_hlist,
			   &au_sbi(file->f_path.dentry->d_sb)->si_files);
		vdir_cache = fidir->fd_vdir_cache; /* lock-free */
		if (vdir_cache)
			au_vdir_free(vdir_cache);

		bindex = finfo->fi_btop;
		if (bindex >= 0) {
			hf = fidir->fd_hfile + bindex;
			/*
			 * calls fput() instead of filp_close(),
			 * since no dnotify or lock for the lower file.
			 */
			bbot = fidir->fd_bbot;
			for (; bindex <= bbot; bindex++, hf++)
				if (hf->hf_file)
					au_hfput(hf, /*execed*/0);
		}
		kfree(fidir);
		finfo->fi_hdir = NULL;
	}
	au_finfo_fin(file);
	return 0;
}

/* ---------------------------------------------------------------------- */

static int au_do_flush_dir(struct file *file, fl_owner_t id)
{
	int err;
	aufs_bindex_t bindex, bbot;
	struct file *h_file;

	err = 0;
	bbot = au_fbbot_dir(file);
	for (bindex = au_fbtop(file); !err && bindex <= bbot; bindex++) {
		h_file = au_hf_dir(file, bindex);
		if (h_file)
			err = vfsub_flush(h_file, id);
	}
	return err;
}

static int aufs_flush_dir(struct file *file, fl_owner_t id)
{
	return au_do_flush(file, id, au_do_flush_dir);
}

/* ---------------------------------------------------------------------- */

static int au_do_fsync_dir_no_file(struct dentry *dentry, int datasync)
{
	int err;
	aufs_bindex_t bbot, bindex;
	struct inode *inode;
	struct super_block *sb;

	err = 0;
	sb = dentry->d_sb;
	inode = d_inode(dentry);
	IMustLock(inode);
	bbot = au_dbbot(dentry);
	for (bindex = au_dbtop(dentry); !err && bindex <= bbot; bindex++) {
		struct path h_path;

		if (au_test_ro(sb, bindex, inode))
			continue;
		h_path.dentry = au_h_dptr(dentry, bindex);
		if (!h_path.dentry)
			continue;

		h_path.mnt = au_sbr_mnt(sb, bindex);
		err = vfsub_fsync(NULL, &h_path, datasync);
	}

	return err;
}

static int au_do_fsync_dir(struct file *file, int datasync)
{
	int err;
	aufs_bindex_t bbot, bindex;
	struct file *h_file;
	struct super_block *sb;
	struct inode *inode;

	err = au_reval_and_lock_fdi(file, reopen_dir, /*wlock*/1, /*fi_lsc*/0);
	if (unlikely(err))
		goto out;

	inode = file_inode(file);
	sb = inode->i_sb;
	bbot = au_fbbot_dir(file);
	for (bindex = au_fbtop(file); !err && bindex <= bbot; bindex++) {
		h_file = au_hf_dir(file, bindex);
		if (!h_file || au_test_ro(sb, bindex, inode))
			continue;

		err = vfsub_fsync(h_file, &h_file->f_path, datasync);
	}

out:
	return err;
}

/*
 * @file may be NULL
 */
static int aufs_fsync_dir(struct file *file, loff_t start, loff_t end,
			  int datasync)
{
	int err;
	struct dentry *dentry;
	struct inode *inode;
	struct super_block *sb;

	err = 0;
	dentry = file->f_path.dentry;
	inode = d_inode(dentry);
	inode_lock(inode);
	sb = dentry->d_sb;
	si_noflush_read_lock(sb);
	if (file)
		err = au_do_fsync_dir(file, datasync);
	else {
		di_write_lock_child(dentry);
		err = au_do_fsync_dir_no_file(dentry, datasync);
	}
	au_cpup_attr_timesizes(inode);
	di_write_unlock(dentry);
	if (file)
		fi_write_unlock(file);

	si_read_unlock(sb);
	inode_unlock(inode);
	return err;
}

/* ---------------------------------------------------------------------- */

static int aufs_iterate_shared(struct file *file, struct dir_context *ctx)
{
	int err;
	struct dentry *dentry;
	struct inode *inode, *h_inode;
	struct super_block *sb;

	AuDbg("%pD, ctx{%pf, %llu}\n", file, ctx->actor, ctx->pos);

	dentry = file->f_path.dentry;
	inode = d_inode(dentry);
	IMustLock(inode);

	sb = dentry->d_sb;
	si_read_lock(sb, AuLock_FLUSH);
	err = au_reval_and_lock_fdi(file, reopen_dir, /*wlock*/1, /*fi_lsc*/0);
	if (unlikely(err))
		goto out;
	err = au_alive_dir(dentry);
	if (!err)
		err = au_vdir_init(file);
	di_downgrade_lock(dentry, AuLock_IR);
	if (unlikely(err))
		goto out_unlock;

	h_inode = au_h_iptr(inode, au_ibtop(inode));
	if (!au_test_nfsd()) {
		err = au_vdir_fill_de(file, ctx);
		fsstack_copy_attr_atime(inode, h_inode);
	} else {
		/*
		 * nfsd filldir may call lookup_one_len(), vfs_getattr(),
		 * encode_fh() and others.
		 */
		atomic_inc(&h_inode->i_count);
		di_read_unlock(dentry, AuLock_IR);
		si_read_unlock(sb);
		err = au_vdir_fill_de(file, ctx);
		fsstack_copy_attr_atime(inode, h_inode);
		fi_write_unlock(file);
		iput(h_inode);

		AuTraceErr(err);
		return err;
	}

out_unlock:
	di_read_unlock(dentry, AuLock_IR);
	fi_write_unlock(file);
out:
	si_read_unlock(sb);
	return err;
}

/* ---------------------------------------------------------------------- */

#define AuTestEmpty_WHONLY	1
#define AuTestEmpty_CALLED	(1 << 1)
#define AuTestEmpty_SHWH	(1 << 2)
#define au_ftest_testempty(flags, name)	((flags) & AuTestEmpty_##name)
#define au_fset_testempty(flags, name) \
	do { (flags) |= AuTestEmpty_##name; } while (0)
#define au_fclr_testempty(flags, name) \
	do { (flags) &= ~AuTestEmpty_##name; } while (0)

#ifndef CONFIG_AUFS_SHWH
#undef AuTestEmpty_SHWH
#define AuTestEmpty_SHWH	0
#endif

struct test_empty_arg {
	struct dir_context ctx;
	struct au_nhash *whlist;
	unsigned int flags;
	int err;
	aufs_bindex_t bindex;
};

static int test_empty_cb(struct dir_context *ctx, const char *__name,
			 int namelen, loff_t offset __maybe_unused, u64 ino,
			 unsigned int d_type)
{
	struct test_empty_arg *arg = container_of(ctx, struct test_empty_arg,
						  ctx);
	char *name = (void *)__name;

	arg->err = 0;
	au_fset_testempty(arg->flags, CALLED);
	/* smp_mb(); */
	if (name[0] == '.'
	    && (namelen == 1 || (name[1] == '.' && namelen == 2)))
		goto out; /* success */

	if (namelen <= AUFS_WH_PFX_LEN
	    || memcmp(name, AUFS_WH_PFX, AUFS_WH_PFX_LEN)) {
		if (au_ftest_testempty(arg->flags, WHONLY)
		    && !au_nhash_test_known_wh(arg->whlist, name, namelen))
			arg->err = -ENOTEMPTY;
		goto out;
	}

	name += AUFS_WH_PFX_LEN;
	namelen -= AUFS_WH_PFX_LEN;
	if (!au_nhash_test_known_wh(arg->whlist, name, namelen))
		arg->err = au_nhash_append_wh
			(arg->whlist, name, namelen, ino, d_type, arg->bindex,
			 au_ftest_testempty(arg->flags, SHWH));

out:
	/* smp_mb(); */
	AuTraceErr(arg->err);
	return arg->err;
}

static int do_test_empty(struct dentry *dentry, struct test_empty_arg *arg)
{
	int err;
	struct file *h_file;

	h_file = au_h_open(dentry, arg->bindex,
			   O_RDONLY | O_NONBLOCK | O_DIRECTORY | O_LARGEFILE,
			   /*file*/NULL, /*force_wr*/0);
	err = PTR_ERR(h_file);
	if (IS_ERR(h_file))
		goto out;

	err = 0;
	if (!au_opt_test(au_mntflags(dentry->d_sb), UDBA_NONE)
	    && !file_inode(h_file)->i_nlink)
		goto out_put;

	do {
		arg->err = 0;
		au_fclr_testempty(arg->flags, CALLED);
		/* smp_mb(); */
		err = vfsub_iterate_dir(h_file, &arg->ctx);
		if (err >= 0)
			err = arg->err;
	} while (!err && au_ftest_testempty(arg->flags, CALLED));

out_put:
	fput(h_file);
	au_sbr_put(dentry->d_sb, arg->bindex);
out:
	return err;
}

struct do_test_empty_args {
	int *errp;
	struct dentry *dentry;
	struct test_empty_arg *arg;
};

static void call_do_test_empty(void *args)
{
	struct do_test_empty_args *a = args;
	*a->errp = do_test_empty(a->dentry, a->arg);
}

static int sio_test_empty(struct dentry *dentry, struct test_empty_arg *arg)
{
	int err, wkq_err;
	struct dentry *h_dentry;
	struct inode *h_inode;

	h_dentry = au_h_dptr(dentry, arg->bindex);
	h_inode = d_inode(h_dentry);
	/* todo: i_mode changes anytime? */
	vfsub_inode_lock_shared_nested(h_inode, AuLsc_I_CHILD);
	err = au_test_h_perm_sio(h_inode, MAY_EXEC | MAY_READ);
	inode_unlock_shared(h_inode);
	if (!err)
		err = do_test_empty(dentry, arg);
	else {
		struct do_test_empty_args args = {
			.errp	= &err,
			.dentry	= dentry,
			.arg	= arg
		};
		unsigned int flags = arg->flags;

		wkq_err = au_wkq_wait(call_do_test_empty, &args);
		if (unlikely(wkq_err))
			err = wkq_err;
		arg->flags = flags;
	}

	return err;
}

int au_test_empty_lower(struct dentry *dentry)
{
	int err;
	unsigned int rdhash;
	aufs_bindex_t bindex, btop, btail;
	struct au_nhash whlist;
	struct test_empty_arg arg = {
		.ctx = {
			.actor = test_empty_cb
		}
	};
	int (*test_empty)(struct dentry *dentry, struct test_empty_arg *arg);

	SiMustAnyLock(dentry->d_sb);

	rdhash = au_sbi(dentry->d_sb)->si_rdhash;
	if (!rdhash)
		rdhash = au_rdhash_est(au_dir_size(/*file*/NULL, dentry));
	err = au_nhash_alloc(&whlist, rdhash, GFP_NOFS);
	if (unlikely(err))
		goto out;

	arg.flags = 0;
	arg.whlist = &whlist;
	btop = au_dbtop(dentry);
	if (au_opt_test(au_mntflags(dentry->d_sb), SHWH))
		au_fset_testempty(arg.flags, SHWH);
	test_empty = do_test_empty;
	if (au_opt_test(au_mntflags(dentry->d_sb), DIRPERM1))
		test_empty = sio_test_empty;
	arg.bindex = btop;
	err = test_empty(dentry, &arg);
	if (unlikely(err))
		goto out_whlist;

	au_fset_testempty(arg.flags, WHONLY);
	btail = au_dbtaildir(dentry);
	for (bindex = btop + 1; !err && bindex <= btail; bindex++) {
		struct dentry *h_dentry;

		h_dentry = au_h_dptr(dentry, bindex);
		if (h_dentry && d_is_positive(h_dentry)) {
			arg.bindex = bindex;
			err = test_empty(dentry, &arg);
		}
	}

out_whlist:
	au_nhash_wh_free(&whlist);
out:
	return err;
}

int au_test_empty(struct dentry *dentry, struct au_nhash *whlist)
{
	int err;
	struct test_empty_arg arg = {
		.ctx = {
			.actor = test_empty_cb
		}
	};
	aufs_bindex_t bindex, btail;

	err = 0;
	arg.whlist = whlist;
	arg.flags = AuTestEmpty_WHONLY;
	if (au_opt_test(au_mntflags(dentry->d_sb), SHWH))
		au_fset_testempty(arg.flags, SHWH);
	btail = au_dbtaildir(dentry);
	for (bindex = au_dbtop(dentry); !err && bindex <= btail; bindex++) {
		struct dentry *h_dentry;

		h_dentry = au_h_dptr(dentry, bindex);
		if (h_dentry && d_is_positive(h_dentry)) {
			arg.bindex = bindex;
			err = sio_test_empty(dentry, &arg);
		}
	}

	return err;
}

/* ---------------------------------------------------------------------- */

const struct file_operations aufs_dir_fop = {
	.owner		= THIS_MODULE,
	.llseek		= default_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= aufs_iterate_shared,
	.unlocked_ioctl	= aufs_ioctl_dir,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= aufs_compat_ioctl_dir,
#endif
	.open		= aufs_open_dir,
	.release	= aufs_release_dir,
	.flush		= aufs_flush_dir,
	.fsync		= aufs_fsync_dir
};
