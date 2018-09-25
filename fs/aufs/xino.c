// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2018 Junjiro R. Okajima
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
 * external inode number translation table and bitmap
 *
 * things to consider
 * - the lifetime
 *   + au_xino object
 *   + XINO files (xino, xib, xigen)
 *   + dynamic debugfs entries (xiN)
 *   + static debugfs entries (xib, xigen)
 *   + static sysfs entry (xi_path)
 * - several entry points to handle them.
 *   + mount(2) without xino option (default)
 *   + mount(2) with xino option
 *   + mount(2) with noxino option
 *   + umount(2)
 *   + remount with add/del branches
 *   + remount with xino/noxino options
 */

#include <linux/seq_file.h>
#include <linux/statfs.h>
#include "aufs.h"

static aufs_bindex_t sbr_find_shared(struct super_block *sb, aufs_bindex_t btop,
				     aufs_bindex_t bbot,
				     struct super_block *h_sb)
{
	/* todo: try binary-search if the branches are many */
	for (; btop <= bbot; btop++)
		if (h_sb == au_sbr_sb(sb, btop))
			return btop;
	return -1;
}

/*
 * find another branch who is on the same filesystem of the specified
 * branch{@btgt}. search until @bbot.
 */
static aufs_bindex_t is_sb_shared(struct super_block *sb, aufs_bindex_t btgt,
				  aufs_bindex_t bbot)
{
	aufs_bindex_t bindex;
	struct super_block *tgt_sb;

	tgt_sb = au_sbr_sb(sb, btgt);
	bindex = sbr_find_shared(sb, /*btop*/0, btgt - 1, tgt_sb);
	if (bindex < 0)
		bindex = sbr_find_shared(sb, btgt + 1, bbot, tgt_sb);

	return bindex;
}

/* ---------------------------------------------------------------------- */

/*
 * stop unnecessary notify events at creating xino files
 */

aufs_bindex_t au_xi_root(struct super_block *sb, struct dentry *dentry)
{
	aufs_bindex_t bfound, bindex, bbot;
	struct dentry *parent;
	struct au_branch *br;

	bfound = -1;
	parent = dentry->d_parent; /* safe d_parent access */
	bbot = au_sbbot(sb);
	for (bindex = 0; bindex <= bbot; bindex++) {
		br = au_sbr(sb, bindex);
		if (au_br_dentry(br) == parent) {
			bfound = bindex;
			break;
		}
	}

	AuDbg("bfound b%d\n", bfound);
	return bfound;
}

struct au_xino_lock_dir {
	struct au_hinode *hdir;
	struct dentry *parent;
	struct inode *dir;
};

static struct dentry *au_dget_parent_lock(struct dentry *dentry,
					  unsigned int lsc)
{
	struct dentry *parent;
	struct inode *dir;

	parent = dget_parent(dentry);
	dir = d_inode(parent);
	inode_lock_nested(dir, lsc);
#if 0 /* it should not happen */
	spin_lock(&dentry->d_lock);
	if (unlikely(dentry->d_parent != parent)) {
		spin_unlock(&dentry->d_lock);
		inode_unlock(dir);
		dput(parent);
		parent = NULL;
		goto out;
	}
	spin_unlock(&dentry->d_lock);

out:
#endif
	return parent;
}

static void au_xino_lock_dir(struct super_block *sb, struct path *xipath,
			     struct au_xino_lock_dir *ldir)
{
	aufs_bindex_t bindex;

	ldir->hdir = NULL;
	bindex = au_xi_root(sb, xipath->dentry);
	if (bindex >= 0) {
		/* rw branch root */
		ldir->hdir = au_hi(d_inode(sb->s_root), bindex);
		au_hn_inode_lock_nested(ldir->hdir, AuLsc_I_PARENT);
	} else {
		/* other */
		ldir->parent = au_dget_parent_lock(xipath->dentry,
						   AuLsc_I_PARENT);
		ldir->dir = d_inode(ldir->parent);
	}
}

static void au_xino_unlock_dir(struct au_xino_lock_dir *ldir)
{
	if (ldir->hdir)
		au_hn_inode_unlock(ldir->hdir);
	else {
		inode_unlock(ldir->dir);
		dput(ldir->parent);
	}
}

/* ---------------------------------------------------------------------- */

/*
 * create and set a new xino file
 */
struct file *au_xino_create(struct super_block *sb, char *fpath, int silent)
{
	struct file *file;
	struct dentry *h_parent, *d;
	struct inode *h_dir, *inode;
	int err;

	/*
	 * at mount-time, and the xino file is the default path,
	 * hnotify is disabled so we have no notify events to ignore.
	 * when a user specified the xino, we cannot get au_hdir to be ignored.
	 */
	file = vfsub_filp_open(fpath, O_RDWR | O_CREAT | O_EXCL | O_LARGEFILE
			       /* | __FMODE_NONOTIFY */,
			       0666);
	if (IS_ERR(file)) {
		if (!silent)
			pr_err("open %s(%ld)\n", fpath, PTR_ERR(file));
		return file;
	}

	/* keep file count */
	err = 0;
	d = file->f_path.dentry;
	h_parent = au_dget_parent_lock(d, AuLsc_I_PARENT);
	/* mnt_want_write() is unnecessary here */
	h_dir = d_inode(h_parent);
	inode = file_inode(file);
	/* no delegation since it is just created */
	if (inode->i_nlink)
		err = vfsub_unlink(h_dir, &file->f_path, /*delegated*/NULL,
				   /*force*/0);
	inode_unlock(h_dir);
	dput(h_parent);
	if (unlikely(err)) {
		if (!silent)
			pr_err("unlink %s(%d)\n", fpath, err);
		goto out;
	}

	err = -EINVAL;
	if (unlikely(sb == d->d_sb)) {
		if (!silent)
			pr_err("%s must be outside\n", fpath);
		goto out;
	}
	if (unlikely(au_test_fs_bad_xino(d->d_sb))) {
		if (!silent)
			pr_err("xino doesn't support %s(%s)\n",
			       fpath, au_sbtype(d->d_sb));
		goto out;
	}
	return file; /* success */

out:
	fput(file);
	file = ERR_PTR(err);
	return file;
}

/*
 * create a new xinofile at the same place/path as @base.
 */
struct file *au_xino_create2(struct super_block *sb, struct path *base,
			     struct file *copy_src)
{
	struct file *file;
	struct dentry *dentry, *parent;
	struct inode *dir, *delegated;
	struct qstr *name;
	struct path path;
	int err, do_unlock;
	struct au_xino_lock_dir ldir;

	do_unlock = 1;
	au_xino_lock_dir(sb, base, &ldir);
	dentry = base->dentry;
	parent = dentry->d_parent; /* dir inode is locked */
	dir = d_inode(parent);
	IMustLock(dir);

	name = &dentry->d_name;
	path.dentry = vfsub_lookup_one_len(name->name, parent, name->len);
	if (IS_ERR(path.dentry)) {
		file = (void *)path.dentry;
		pr_err("%pd lookup err %ld\n", dentry, PTR_ERR(path.dentry));
		goto out;
	}

	/* no need to mnt_want_write() since we call dentry_open() later */
	err = vfs_create(dir, path.dentry, 0666, NULL);
	if (unlikely(err)) {
		file = ERR_PTR(err);
		pr_err("%pd create err %d\n", dentry, err);
		goto out_dput;
	}

	path.mnt = base->mnt;
	file = vfsub_dentry_open(&path,
				 O_RDWR | O_CREAT | O_EXCL | O_LARGEFILE
				 /* | __FMODE_NONOTIFY */);
	if (IS_ERR(file)) {
		pr_err("%pd open err %ld\n", dentry, PTR_ERR(file));
		goto out_dput;
	}

	delegated = NULL;
	err = vfsub_unlink(dir, &file->f_path, &delegated, /*force*/0);
	au_xino_unlock_dir(&ldir);
	do_unlock = 0;
	if (unlikely(err == -EWOULDBLOCK)) {
		pr_warn("cannot retry for NFSv4 delegation"
			" for an internal unlink\n");
		iput(delegated);
	}
	if (unlikely(err)) {
		pr_err("%pd unlink err %d\n", dentry, err);
		goto out_fput;
	}

	if (copy_src) {
		/* no one can touch copy_src xino */
		err = au_copy_file(file, copy_src, vfsub_f_size_read(copy_src));
		if (unlikely(err)) {
			pr_err("%pd copy err %d\n", dentry, err);
			goto out_fput;
		}
	}
	goto out_dput; /* success */

out_fput:
	fput(file);
	file = ERR_PTR(err);
out_dput:
	dput(path.dentry);
out:
	if (do_unlock)
		au_xino_unlock_dir(&ldir);
	return file;
}

struct file *au_xino_file1(struct au_xino *xi)
{
	struct file *file;
	unsigned int u, nfile;

	file = NULL;
	nfile = xi->xi_nfile;
	for (u = 0; u < nfile; u++) {
		file = xi->xi_file[u];
		if (file)
			break;
	}

	return file;
}

static int au_xino_file_set(struct au_xino *xi, int idx, struct file *file)
{
	int err;
	struct file *f;
	void *p;

	if (file)
		get_file(file);

	err = 0;
	f = NULL;
	if (idx < xi->xi_nfile) {
		f = xi->xi_file[idx];
		if (f)
			fput(f);
	} else {
		p = au_kzrealloc(xi->xi_file,
				 sizeof(*xi->xi_file) * xi->xi_nfile,
				 sizeof(*xi->xi_file) * (idx + 1),
				 GFP_NOFS, /*may_shrink*/0);
		if (p) {
			MtxMustLock(&xi->xi_mtx);
			xi->xi_file = p;
			xi->xi_nfile = idx + 1;
		} else {
			err = -ENOMEM;
			if (file)
				fput(file);
			goto out;
		}
	}
	xi->xi_file[idx] = file;

out:
	return err;
}

/*
 * if @xinew->xi is not set, then create new xigen file.
 */
struct file *au_xi_new(struct super_block *sb, struct au_xi_new *xinew)
{
	struct file *file;
	int err;

	SiMustAnyLock(sb);

	file = au_xino_create2(sb, xinew->base, xinew->copy_src);
	if (IS_ERR(file)) {
		err = PTR_ERR(file);
		pr_err("%s[%d], err %d\n",
		       xinew->xi ? "xino" : "xigen",
		       xinew->idx, err);
		goto out;
	}

	if (xinew->xi)
		err = au_xino_file_set(xinew->xi, xinew->idx, file);
	else {
		BUG();
		/* todo: make xigen file an array */
		/* err = au_xigen_file_set(sb, xinew->idx, file); */
	}
	fput(file);
	if (unlikely(err))
		file = ERR_PTR(err);

out:
	return file;
}

/* ---------------------------------------------------------------------- */

/*
 * truncate xino files
 */
static int au_xino_do_trunc(struct super_block *sb, aufs_bindex_t bindex,
			    int idx, struct kstatfs *st)
{
	int err;
	blkcnt_t blocks;
	struct file *file, *new_xino;
	struct au_xi_new xinew = {
		.idx = idx
	};

	err = 0;
	xinew.xi = au_sbr(sb, bindex)->br_xino;
	file = au_xino_file(xinew.xi, idx);
	if (!file)
		goto out;

	xinew.base = &file->f_path;
	err = vfs_statfs(xinew.base, st);
	if (unlikely(err)) {
		AuErr1("statfs err %d, ignored\n", err);
		err = 0;
		goto out;
	}

	blocks = file_inode(file)->i_blocks;
	pr_info("begin truncating xino(b%d-%d), ib%llu, %llu/%llu free blks\n",
		bindex, idx, (u64)blocks, st->f_bfree, st->f_blocks);

	xinew.copy_src = file;
	new_xino = au_xi_new(sb, &xinew);
	if (IS_ERR(new_xino)) {
		err = PTR_ERR(new_xino);
		pr_err("xino(b%d-%d), err %d, ignored\n", bindex, idx, err);
		goto out;
	}

	err = vfs_statfs(&new_xino->f_path, st);
	if (!err)
		pr_info("end truncating xino(b%d-%d), ib%llu, %llu/%llu free blks\n",
			bindex, idx, (u64)file_inode(new_xino)->i_blocks,
			st->f_bfree, st->f_blocks);
	else {
		AuErr1("statfs err %d, ignored\n", err);
		err = 0;
	}

out:
	return err;
}

int au_xino_trunc(struct super_block *sb, aufs_bindex_t bindex, int idx_begin)
{
	int err, i;
	unsigned long jiffy;
	aufs_bindex_t bbot;
	struct kstatfs *st;
	struct au_branch *br;
	struct au_xino *xi;

	err = -ENOMEM;
	st = kmalloc(sizeof(*st), GFP_NOFS);
	if (unlikely(!st))
		goto out;

	err = -EINVAL;
	bbot = au_sbbot(sb);
	if (unlikely(bindex < 0 || bbot < bindex))
		goto out_st;

	err = 0;
	jiffy = jiffies;
	br = au_sbr(sb, bindex);
	xi = br->br_xino;
	for (i = idx_begin; !err && i < xi->xi_nfile; i++)
		err = au_xino_do_trunc(sb, bindex, i, st);
	if (!err)
		au_sbi(sb)->si_xino_jiffy = jiffy;

out_st:
	au_kfree_rcu(st);
out:
	return err;
}

struct xino_do_trunc_args {
	struct super_block *sb;
	struct au_branch *br;
	int idx;
};

static void xino_do_trunc(void *_args)
{
	struct xino_do_trunc_args *args = _args;
	struct super_block *sb;
	struct au_branch *br;
	struct inode *dir;
	int err, idx;
	aufs_bindex_t bindex;

	err = 0;
	sb = args->sb;
	dir = d_inode(sb->s_root);
	br = args->br;
	idx = args->idx;

	si_noflush_write_lock(sb);
	ii_read_lock_parent(dir);
	bindex = au_br_index(sb, br->br_id);
	err = au_xino_trunc(sb, bindex, idx);
	ii_read_unlock(dir);
	if (unlikely(err))
		pr_warn("err b%d, (%d)\n", bindex, err);
	atomic_dec(&br->br_xino->xi_truncating);
	au_lcnt_dec(&br->br_count);
	si_write_unlock(sb);
	au_nwt_done(&au_sbi(sb)->si_nowait);
	au_kfree_rcu(args);
}

/*
 * returns the index in the xi_file array whose corresponding file is necessary
 * to truncate, or -1 which means no need to truncate.
 */
static int xino_trunc_test(struct super_block *sb, struct au_branch *br)
{
	int err;
	unsigned int u;
	struct kstatfs st;
	struct au_sbinfo *sbinfo;
	struct au_xino *xi;
	struct file *file;

	/* todo: si_xino_expire and the ratio should be customizable */
	sbinfo = au_sbi(sb);
	if (time_before(jiffies,
			sbinfo->si_xino_jiffy + sbinfo->si_xino_expire))
		return -1;

	/* truncation border */
	xi = br->br_xino;
	for (u = 0; u < xi->xi_nfile; u++) {
		file = au_xino_file(xi, u);
		if (!file)
			continue;

		err = vfs_statfs(&file->f_path, &st);
		if (unlikely(err)) {
			AuErr1("statfs err %d, ignored\n", err);
			return -1;
		}
		if (div64_u64(st.f_bfree * 100, st.f_blocks)
		    >= AUFS_XINO_DEF_TRUNC)
			return u;
	}

	return -1;
}

static void xino_try_trunc(struct super_block *sb, struct au_branch *br)
{
	int idx;
	struct xino_do_trunc_args *args;
	int wkq_err;

	idx = xino_trunc_test(sb, br);
	if (idx < 0)
		return;

	if (atomic_inc_return(&br->br_xino->xi_truncating) > 1)
		goto out;

	/* lock and kfree() will be called in trunc_xino() */
	args = kmalloc(sizeof(*args), GFP_NOFS);
	if (unlikely(!args)) {
		AuErr1("no memory\n");
		goto out;
	}

	au_lcnt_inc(&br->br_count);
	args->sb = sb;
	args->br = br;
	args->idx = idx;
	wkq_err = au_wkq_nowait(xino_do_trunc, args, sb, /*flags*/0);
	if (!wkq_err)
		return; /* success */

	pr_err("wkq %d\n", wkq_err);
	au_lcnt_dec(&br->br_count);
	au_kfree_rcu(args);

out:
	atomic_dec(&br->br_xino->xi_truncating);
}

/* ---------------------------------------------------------------------- */

struct au_xi_calc {
	int idx;
	loff_t pos;
};

static void au_xi_calc(struct super_block *sb, ino_t h_ino,
		       struct au_xi_calc *calc)
{
	loff_t maxent;

	maxent = au_xi_maxent(sb);
	calc->idx = div64_u64_rem(h_ino, maxent, &calc->pos);
	calc->pos *= sizeof(ino_t);
}

static int au_xino_do_new_async(struct super_block *sb, struct au_branch *br,
				struct au_xi_calc *calc)
{
	int err;
	struct file *file;
	struct au_xino *xi = br->br_xino;
	struct au_xi_new xinew = {
		.xi = xi
	};

	SiMustAnyLock(sb);

	err = 0;
	if (!xi)
		goto out;

	mutex_lock(&xi->xi_mtx);
	file = au_xino_file(xi, calc->idx);
	if (file)
		goto out_mtx;

	file = au_xino_file(xi, /*idx*/-1);
	AuDebugOn(!file);
	xinew.idx = calc->idx;
	xinew.base = &file->f_path;
	/* xinew.copy_src = NULL; */
	file = au_xi_new(sb, &xinew);
	if (IS_ERR(file))
		err = PTR_ERR(file);

out_mtx:
	mutex_unlock(&xi->xi_mtx);
out:
	return err;
}

struct au_xino_do_new_async_args {
	struct super_block *sb;
	struct au_branch *br;
	struct au_xi_calc calc;
	ino_t ino;
};

struct au_xi_writing {
	struct hlist_bl_node node;
	ino_t h_ino, ino;
};

static int au_xino_do_write(vfs_writef_t write, struct file *file,
			    struct au_xi_calc *calc, ino_t ino);

static void au_xino_call_do_new_async(void *args)
{
	struct au_xino_do_new_async_args *a = args;
	struct au_branch *br;
	struct super_block *sb;
	struct au_sbinfo *sbi;
	struct inode *root;
	struct file *file;
	struct au_xi_writing *del, *p;
	struct hlist_bl_head *hbl;
	struct hlist_bl_node *pos;
	int err;

	br = a->br;
	sb = a->sb;
	sbi = au_sbi(sb);
	si_noflush_read_lock(sb);
	root = d_inode(sb->s_root);
	ii_read_lock_child(root);
	err = au_xino_do_new_async(sb, br, &a->calc);
	if (unlikely(err)) {
		AuIOErr("err %d\n", err);
		goto out;
	}

	file = au_xino_file(br->br_xino, a->calc.idx);
	AuDebugOn(!file);
	err = au_xino_do_write(sbi->si_xwrite, file, &a->calc, a->ino);
	if (unlikely(err)) {
		AuIOErr("err %d\n", err);
		goto out;
	}

	del = NULL;
	hbl = &br->br_xino->xi_writing;
	hlist_bl_lock(hbl);
	au_hbl_for_each(pos, hbl) {
		p = container_of(pos, struct au_xi_writing, node);
		if (p->ino == a->ino) {
			del = p;
			hlist_bl_del(&p->node);
			break;
		}
	}
	hlist_bl_unlock(hbl);
	au_kfree_rcu(del);

out:
	au_lcnt_dec(&br->br_count);
	ii_read_unlock(root);
	si_read_unlock(sb);
	au_nwt_done(&sbi->si_nowait);
	au_kfree_rcu(a);
}

/*
 * create a new xino file asynchronously
 */
static int au_xino_new_async(struct super_block *sb, struct au_branch *br,
			     struct au_xi_calc *calc, ino_t ino)
{
	int err;
	struct au_xino_do_new_async_args *arg;

	err = -ENOMEM;
	arg = kmalloc(sizeof(*arg), GFP_NOFS);
	if (unlikely(!arg))
		goto out;

	arg->sb = sb;
	arg->br = br;
	arg->calc = *calc;
	arg->ino = ino;
	au_lcnt_inc(&br->br_count);
	err = au_wkq_nowait(au_xino_call_do_new_async, arg, sb, AuWkq_NEST);
	if (unlikely(err)) {
		pr_err("wkq %d\n", err);
		au_lcnt_dec(&br->br_count);
		au_kfree_rcu(arg);
	}

out:
	return err;
}

/*
 * read @ino from xinofile for the specified branch{@sb, @bindex}
 * at the position of @h_ino.
 */
int au_xino_read(struct super_block *sb, aufs_bindex_t bindex, ino_t h_ino,
		 ino_t *ino)
{
	int err;
	ssize_t sz;
	struct au_xi_calc calc;
	struct au_sbinfo *sbinfo;
	struct file *file;
	struct au_xino *xi;
	struct hlist_bl_head *hbl;
	struct hlist_bl_node *pos;
	struct au_xi_writing *p;

	*ino = 0;
	if (!au_opt_test(au_mntflags(sb), XINO))
		return 0; /* no xino */

	err = 0;
	au_xi_calc(sb, h_ino, &calc);
	xi = au_sbr(sb, bindex)->br_xino;
	file = au_xino_file(xi, calc.idx);
	if (!file) {
		hbl = &xi->xi_writing;
		hlist_bl_lock(hbl);
		au_hbl_for_each(pos, hbl) {
			p = container_of(pos, struct au_xi_writing, node);
			if (p->h_ino == h_ino) {
				AuDbg("hi%llu, i%llu, found\n",
				      (u64)p->h_ino, (u64)p->ino);
				*ino = p->ino;
				break;
			}
		}
		hlist_bl_unlock(hbl);
		return 0;
	} else if (vfsub_f_size_read(file) < calc.pos + sizeof(*ino))
		return 0; /* no xino */

	sbinfo = au_sbi(sb);
	sz = xino_fread(sbinfo->si_xread, file, ino, sizeof(*ino), &calc.pos);
	if (sz == sizeof(*ino))
		return 0; /* success */

	err = sz;
	if (unlikely(sz >= 0)) {
		err = -EIO;
		AuIOErr("xino read error (%zd)\n", sz);
	}
	return err;
}

static int au_xino_do_write(vfs_writef_t write, struct file *file,
			    struct au_xi_calc *calc, ino_t ino)
{
	ssize_t sz;

	sz = xino_fwrite(write, file, &ino, sizeof(ino), &calc->pos);
	if (sz == sizeof(ino))
		return 0; /* success */

	AuIOErr("write failed (%zd)\n", sz);
	return -EIO;
}

/*
 * write @ino to the xinofile for the specified branch{@sb, @bindex}
 * at the position of @h_ino.
 * even if @ino is zero, it is written to the xinofile and means no entry.
 * if the size of the xino file on a specific filesystem exceeds the watermark,
 * try truncating it.
 */
int au_xino_write(struct super_block *sb, aufs_bindex_t bindex, ino_t h_ino,
		  ino_t ino)
{
	int err;
	unsigned int mnt_flags;
	struct au_xi_calc calc;
	struct file *file;
	struct au_branch *br;
	struct au_xino *xi;
	struct au_xi_writing *p;

	SiMustAnyLock(sb);

	mnt_flags = au_mntflags(sb);
	if (!au_opt_test(mnt_flags, XINO))
		return 0;

	au_xi_calc(sb, h_ino, &calc);
	br = au_sbr(sb, bindex);
	xi = br->br_xino;
	file = au_xino_file(xi, calc.idx);
	if (!file) {
		/* store the inum pair into the list */
		p = kmalloc(sizeof(*p), GFP_NOFS | __GFP_NOFAIL);
		p->h_ino = h_ino;
		p->ino = ino;
		au_hbl_add(&p->node, &xi->xi_writing);

		/* create and write a new xino file asynchronously */
		err = au_xino_new_async(sb, br, &calc, ino);
		if (!err)
			return 0; /* success */
		goto out;
	}

	err = au_xino_do_write(au_sbi(sb)->si_xwrite, file, &calc, ino);
	if (!err) {
		br = au_sbr(sb, bindex);
		if (au_opt_test(mnt_flags, TRUNC_XINO)
		    && au_test_fs_trunc_xino(au_br_sb(br)))
			xino_try_trunc(sb, br);
		return 0; /* success */
	}

out:
	AuIOErr("write failed (%d)\n", err);
	return -EIO;
}

static ssize_t xino_fread_wkq(vfs_readf_t func, struct file *file, void *buf,
			      size_t size, loff_t *pos);

/* todo: unnecessary to support mmap_sem since kernel-space? */
ssize_t xino_fread(vfs_readf_t func, struct file *file, void *kbuf, size_t size,
		   loff_t *pos)
{
	ssize_t err;
	mm_segment_t oldfs;
	union {
		void *k;
		char __user *u;
	} buf;
	int i;
	const int prevent_endless = 10;

	i = 0;
	buf.k = kbuf;
	oldfs = get_fs();
	set_fs(KERNEL_DS);
	do {
		err = func(file, buf.u, size, pos);
		if (err == -EINTR
		    && !au_wkq_test()
		    && fatal_signal_pending(current)) {
			set_fs(oldfs);
			err = xino_fread_wkq(func, file, kbuf, size, pos);
			BUG_ON(err == -EINTR);
			oldfs = get_fs();
			set_fs(KERNEL_DS);
		}
	} while (i++ < prevent_endless
		 && (err == -EAGAIN || err == -EINTR));
	set_fs(oldfs);

#if 0 /* reserved for future use */
	if (err > 0)
		fsnotify_access(file->f_path.dentry);
#endif

	return err;
}

struct xino_fread_args {
	ssize_t *errp;
	vfs_readf_t func;
	struct file *file;
	void *buf;
	size_t size;
	loff_t *pos;
};

static void call_xino_fread(void *args)
{
	struct xino_fread_args *a = args;
	*a->errp = xino_fread(a->func, a->file, a->buf, a->size, a->pos);
}

static ssize_t xino_fread_wkq(vfs_readf_t func, struct file *file, void *buf,
			      size_t size, loff_t *pos)
{
	ssize_t err;
	int wkq_err;
	struct xino_fread_args args = {
		.errp	= &err,
		.func	= func,
		.file	= file,
		.buf	= buf,
		.size	= size,
		.pos	= pos
	};

	wkq_err = au_wkq_wait(call_xino_fread, &args);
	if (unlikely(wkq_err))
		err = wkq_err;

	return err;
}

static ssize_t xino_fwrite_wkq(vfs_writef_t func, struct file *file, void *buf,
			       size_t size, loff_t *pos);

static ssize_t do_xino_fwrite(vfs_writef_t func, struct file *file, void *kbuf,
			      size_t size, loff_t *pos)
{
	ssize_t err;
	mm_segment_t oldfs;
	union {
		void *k;
		const char __user *u;
	} buf;
	int i;
	const int prevent_endless = 10;

	i = 0;
	buf.k = kbuf;
	oldfs = get_fs();
	set_fs(KERNEL_DS);
	do {
		err = func(file, buf.u, size, pos);
		if (err == -EINTR
		    && !au_wkq_test()
		    && fatal_signal_pending(current)) {
			set_fs(oldfs);
			err = xino_fwrite_wkq(func, file, kbuf, size, pos);
			BUG_ON(err == -EINTR);
			oldfs = get_fs();
			set_fs(KERNEL_DS);
		}
	} while (i++ < prevent_endless
		 && (err == -EAGAIN || err == -EINTR));
	set_fs(oldfs);

#if 0 /* reserved for future use */
	if (err > 0)
		fsnotify_modify(file->f_path.dentry);
#endif

	return err;
}

struct do_xino_fwrite_args {
	ssize_t *errp;
	vfs_writef_t func;
	struct file *file;
	void *buf;
	size_t size;
	loff_t *pos;
};

static void call_do_xino_fwrite(void *args)
{
	struct do_xino_fwrite_args *a = args;
	*a->errp = do_xino_fwrite(a->func, a->file, a->buf, a->size, a->pos);
}

static ssize_t xino_fwrite_wkq(vfs_writef_t func, struct file *file, void *buf,
			       size_t size, loff_t *pos)
{
	ssize_t err;
	int wkq_err;
	struct do_xino_fwrite_args args = {
		.errp	= &err,
		.func	= func,
		.file	= file,
		.buf	= buf,
		.size	= size,
		.pos	= pos
	};

	/*
	 * it breaks RLIMIT_FSIZE and normal user's limit,
	 * users should care about quota and real 'filesystem full.'
	 */
	wkq_err = au_wkq_wait(call_do_xino_fwrite, &args);
	if (unlikely(wkq_err))
		err = wkq_err;

	return err;
}

ssize_t xino_fwrite(vfs_writef_t func, struct file *file, void *buf,
		    size_t size, loff_t *pos)
{
	ssize_t err;

	if (rlimit(RLIMIT_FSIZE) == RLIM_INFINITY) {
		lockdep_off();
		err = do_xino_fwrite(func, file, buf, size, pos);
		lockdep_on();
	} else {
		lockdep_off();
		err = xino_fwrite_wkq(func, file, buf, size, pos);
		lockdep_on();
	}

	return err;
}

/* ---------------------------------------------------------------------- */

/*
 * inode number bitmap
 */
static const int page_bits = (int)PAGE_SIZE * BITS_PER_BYTE;
static ino_t xib_calc_ino(unsigned long pindex, int bit)
{
	ino_t ino;

	AuDebugOn(bit < 0 || page_bits <= bit);
	ino = AUFS_FIRST_INO + pindex * page_bits + bit;
	return ino;
}

static void xib_calc_bit(ino_t ino, unsigned long *pindex, int *bit)
{
	AuDebugOn(ino < AUFS_FIRST_INO);
	ino -= AUFS_FIRST_INO;
	*pindex = ino / page_bits;
	*bit = ino % page_bits;
}

static int xib_pindex(struct super_block *sb, unsigned long pindex)
{
	int err;
	loff_t pos;
	ssize_t sz;
	struct au_sbinfo *sbinfo;
	struct file *xib;
	unsigned long *p;

	sbinfo = au_sbi(sb);
	MtxMustLock(&sbinfo->si_xib_mtx);
	AuDebugOn(pindex > ULONG_MAX / PAGE_SIZE
		  || !au_opt_test(sbinfo->si_mntflags, XINO));

	if (pindex == sbinfo->si_xib_last_pindex)
		return 0;

	xib = sbinfo->si_xib;
	p = sbinfo->si_xib_buf;
	pos = sbinfo->si_xib_last_pindex;
	pos *= PAGE_SIZE;
	sz = xino_fwrite(sbinfo->si_xwrite, xib, p, PAGE_SIZE, &pos);
	if (unlikely(sz != PAGE_SIZE))
		goto out;

	pos = pindex;
	pos *= PAGE_SIZE;
	if (vfsub_f_size_read(xib) >= pos + PAGE_SIZE)
		sz = xino_fread(sbinfo->si_xread, xib, p, PAGE_SIZE, &pos);
	else {
		memset(p, 0, PAGE_SIZE);
		sz = xino_fwrite(sbinfo->si_xwrite, xib, p, PAGE_SIZE, &pos);
	}
	if (sz == PAGE_SIZE) {
		sbinfo->si_xib_last_pindex = pindex;
		return 0; /* success */
	}

out:
	AuIOErr1("write failed (%zd)\n", sz);
	err = sz;
	if (sz >= 0)
		err = -EIO;
	return err;
}

static void au_xib_clear_bit(struct inode *inode)
{
	int err, bit;
	unsigned long pindex;
	struct super_block *sb;
	struct au_sbinfo *sbinfo;

	AuDebugOn(inode->i_nlink);

	sb = inode->i_sb;
	xib_calc_bit(inode->i_ino, &pindex, &bit);
	AuDebugOn(page_bits <= bit);
	sbinfo = au_sbi(sb);
	mutex_lock(&sbinfo->si_xib_mtx);
	err = xib_pindex(sb, pindex);
	if (!err) {
		clear_bit(bit, sbinfo->si_xib_buf);
		sbinfo->si_xib_next_bit = bit;
	}
	mutex_unlock(&sbinfo->si_xib_mtx);
}

/* ---------------------------------------------------------------------- */

/*
 * truncate a xino bitmap file
 */

/* todo: slow */
static int do_xib_restore(struct super_block *sb, struct file *file, void *page)
{
	int err, bit;
	ssize_t sz;
	unsigned long pindex;
	loff_t pos, pend;
	struct au_sbinfo *sbinfo;
	vfs_readf_t func;
	ino_t *ino;
	unsigned long *p;

	err = 0;
	sbinfo = au_sbi(sb);
	MtxMustLock(&sbinfo->si_xib_mtx);
	p = sbinfo->si_xib_buf;
	func = sbinfo->si_xread;
	pend = vfsub_f_size_read(file);
	pos = 0;
	while (pos < pend) {
		sz = xino_fread(func, file, page, PAGE_SIZE, &pos);
		err = sz;
		if (unlikely(sz <= 0))
			goto out;

		err = 0;
		for (ino = page; sz > 0; ino++, sz -= sizeof(ino)) {
			if (unlikely(*ino < AUFS_FIRST_INO))
				continue;

			xib_calc_bit(*ino, &pindex, &bit);
			AuDebugOn(page_bits <= bit);
			err = xib_pindex(sb, pindex);
			if (!err)
				set_bit(bit, p);
			else
				goto out;
		}
	}

out:
	return err;
}

static int xib_restore(struct super_block *sb)
{
	int err, i;
	unsigned int nfile;
	aufs_bindex_t bindex, bbot;
	void *page;
	struct au_branch *br;
	struct au_xino *xi;
	struct file *file;

	err = -ENOMEM;
	page = (void *)__get_free_page(GFP_NOFS);
	if (unlikely(!page))
		goto out;

	err = 0;
	bbot = au_sbbot(sb);
	for (bindex = 0; !err && bindex <= bbot; bindex++)
		if (!bindex || is_sb_shared(sb, bindex, bindex - 1) < 0) {
			br = au_sbr(sb, bindex);
			xi = br->br_xino;
			nfile = xi->xi_nfile;
			for (i = 0; i < nfile; i++) {
				file = au_xino_file(xi, i);
				if (file)
					err = do_xib_restore(sb, file, page);
			}
		} else
			AuDbg("skip shared b%d\n", bindex);
	free_page((unsigned long)page);

out:
	return err;
}

int au_xib_trunc(struct super_block *sb)
{
	int err;
	ssize_t sz;
	loff_t pos;
	struct au_sbinfo *sbinfo;
	unsigned long *p;
	struct file *file;

	SiMustWriteLock(sb);

	err = 0;
	sbinfo = au_sbi(sb);
	if (!au_opt_test(sbinfo->si_mntflags, XINO))
		goto out;

	file = sbinfo->si_xib;
	if (vfsub_f_size_read(file) <= PAGE_SIZE)
		goto out;

	file = au_xino_create2(sb, &sbinfo->si_xib->f_path, NULL);
	err = PTR_ERR(file);
	if (IS_ERR(file))
		goto out;
	fput(sbinfo->si_xib);
	sbinfo->si_xib = file;

	p = sbinfo->si_xib_buf;
	memset(p, 0, PAGE_SIZE);
	pos = 0;
	sz = xino_fwrite(sbinfo->si_xwrite, sbinfo->si_xib, p, PAGE_SIZE, &pos);
	if (unlikely(sz != PAGE_SIZE)) {
		err = sz;
		AuIOErr("err %d\n", err);
		if (sz >= 0)
			err = -EIO;
		goto out;
	}

	mutex_lock(&sbinfo->si_xib_mtx);
	/* mnt_want_write() is unnecessary here */
	err = xib_restore(sb);
	mutex_unlock(&sbinfo->si_xib_mtx);

out:
	return err;
}

/* ---------------------------------------------------------------------- */

struct au_xino *au_xino_alloc(unsigned int nfile)
{
	struct au_xino *xi;

	xi = kzalloc(sizeof(*xi), GFP_NOFS);
	if (unlikely(!xi))
		goto out;
	xi->xi_nfile = nfile;
	xi->xi_file = kcalloc(nfile, sizeof(*xi->xi_file), GFP_NOFS);
	if (unlikely(!xi->xi_file))
		goto out_free;

	xi->xi_nondir.total = 8; /* initial size */
	xi->xi_nondir.array = kcalloc(xi->xi_nondir.total, sizeof(ino_t),
				      GFP_NOFS);
	if (unlikely(!xi->xi_nondir.array))
		goto out_file;

	spin_lock_init(&xi->xi_nondir.spin);
	init_waitqueue_head(&xi->xi_nondir.wqh);
	mutex_init(&xi->xi_mtx);
	INIT_HLIST_BL_HEAD(&xi->xi_writing);
	atomic_set(&xi->xi_truncating, 0);
	kref_init(&xi->xi_kref);
	goto out; /* success */

out_file:
	au_kfree_try_rcu(xi->xi_file);
out_free:
	au_kfree_rcu(xi);
	xi = NULL;
out:
	return xi;
}

static int au_xino_init(struct au_branch *br, int idx, struct file *file)
{
	int err;
	struct au_xino *xi;

	err = 0;
	xi = au_xino_alloc(idx + 1);
	if (unlikely(!xi)) {
		err = -ENOMEM;
		goto out;
	}

	if (file)
		get_file(file);
	xi->xi_file[idx] = file;
	AuDebugOn(br->br_xino);
	br->br_xino = xi;

out:
	return err;
}

static void au_xino_release(struct kref *kref)
{
	struct au_xino *xi;
	int i;
	unsigned long ul;
	struct hlist_bl_head *hbl;
	struct hlist_bl_node *pos, *n;
	struct au_xi_writing *p;

	xi = container_of(kref, struct au_xino, xi_kref);
	for (i = 0; i < xi->xi_nfile; i++)
		if (xi->xi_file[i])
			fput(xi->xi_file[i]);
	for (i = xi->xi_nondir.total - 1; i >= 0; i--)
		AuDebugOn(xi->xi_nondir.array[i]);
	mutex_destroy(&xi->xi_mtx);
	hbl = &xi->xi_writing;
	ul = au_hbl_count(hbl);
	if (unlikely(ul)) {
		pr_warn("xi_writing %lu\n", ul);
		hlist_bl_lock(hbl);
		hlist_bl_for_each_entry_safe (p, pos, n, hbl, node) {
			hlist_bl_del(&p->node);
			au_kfree_rcu(p);
		}
		hlist_bl_unlock(hbl);
	}
	au_kfree_try_rcu(xi->xi_file);
	au_kfree_try_rcu(xi->xi_nondir.array);
	au_kfree_rcu(xi);
}

int au_xino_put(struct au_branch *br)
{
	int ret;
	struct au_xino *xi;

	ret = 0;
	xi = br->br_xino;
	if (xi) {
		br->br_xino = NULL;
		ret = kref_put(&xi->xi_kref, au_xino_release);
	}

	return ret;
}

/* ---------------------------------------------------------------------- */

/*
 * xino mount option handlers
 */

/* xino bitmap */
static void xino_clear_xib(struct super_block *sb)
{
	struct au_sbinfo *sbinfo;

	SiMustWriteLock(sb);

	sbinfo = au_sbi(sb);
	/* unnecessary to clear sbinfo->si_xread and ->si_xwrite */
	if (sbinfo->si_xib)
		fput(sbinfo->si_xib);
	sbinfo->si_xib = NULL;
	if (sbinfo->si_xib_buf)
		free_page((unsigned long)sbinfo->si_xib_buf);
	sbinfo->si_xib_buf = NULL;
}

static int au_xino_set_xib(struct super_block *sb, struct path *path)
{
	int err;
	loff_t pos;
	struct au_sbinfo *sbinfo;
	struct file *file;
	struct super_block *xi_sb;

	SiMustWriteLock(sb);

	sbinfo = au_sbi(sb);
	file = au_xino_create2(sb, path, sbinfo->si_xib);
	err = PTR_ERR(file);
	if (IS_ERR(file))
		goto out;
	if (sbinfo->si_xib)
		fput(sbinfo->si_xib);
	sbinfo->si_xib = file;
	sbinfo->si_xread = vfs_readf(file);
	sbinfo->si_xwrite = vfs_writef(file);
	xi_sb = file_inode(file)->i_sb;
	sbinfo->si_ximaxent = xi_sb->s_maxbytes;
	if (unlikely(sbinfo->si_ximaxent < PAGE_SIZE)) {
		err = -EIO;
		pr_err("s_maxbytes(%llu) on %s is too small\n",
		       (u64)sbinfo->si_ximaxent, au_sbtype(xi_sb));
		goto out_unset;
	}
	sbinfo->si_ximaxent /= sizeof(ino_t);

	err = -ENOMEM;
	if (!sbinfo->si_xib_buf)
		sbinfo->si_xib_buf = (void *)get_zeroed_page(GFP_NOFS);
	if (unlikely(!sbinfo->si_xib_buf))
		goto out_unset;

	sbinfo->si_xib_last_pindex = 0;
	sbinfo->si_xib_next_bit = 0;
	if (vfsub_f_size_read(file) < PAGE_SIZE) {
		pos = 0;
		err = xino_fwrite(sbinfo->si_xwrite, file, sbinfo->si_xib_buf,
				  PAGE_SIZE, &pos);
		if (unlikely(err != PAGE_SIZE))
			goto out_free;
	}
	err = 0;
	goto out; /* success */

out_free:
	if (sbinfo->si_xib_buf)
		free_page((unsigned long)sbinfo->si_xib_buf);
	sbinfo->si_xib_buf = NULL;
	if (err >= 0)
		err = -EIO;
out_unset:
	fput(sbinfo->si_xib);
	sbinfo->si_xib = NULL;
out:
	AuTraceErr(err);
	return err;
}

/* xino for each branch */
static void xino_clear_br(struct super_block *sb)
{
	aufs_bindex_t bindex, bbot;
	struct au_branch *br;

	bbot = au_sbbot(sb);
	for (bindex = 0; bindex <= bbot; bindex++) {
		br = au_sbr(sb, bindex);
		AuDebugOn(!br);
		au_xino_put(br);
	}
}

static void au_xino_set_br_shared(struct super_block *sb, struct au_branch *br,
				  aufs_bindex_t bshared)
{
	struct au_branch *brshared;

	brshared = au_sbr(sb, bshared);
	AuDebugOn(!brshared->br_xino);
	AuDebugOn(!brshared->br_xino->xi_file);
	if (br->br_xino != brshared->br_xino) {
		au_xino_get(brshared);
		au_xino_put(br);
		br->br_xino = brshared->br_xino;
	}
}

struct au_xino_do_set_br {
	vfs_writef_t writef;
	struct au_branch *br;
	ino_t h_ino;
	aufs_bindex_t bshared;
};

static int au_xino_do_set_br(struct super_block *sb, struct path *path,
			     struct au_xino_do_set_br *args)
{
	int err;
	struct au_xi_calc calc;
	struct file *file;
	struct au_branch *br;
	struct au_xi_new xinew = {
		.base = path
	};

	br = args->br;
	xinew.xi = br->br_xino;
	au_xi_calc(sb, args->h_ino, &calc);
	xinew.copy_src = au_xino_file(xinew.xi, calc.idx);
	if (args->bshared >= 0)
		/* shared xino */
		au_xino_set_br_shared(sb, br, args->bshared);
	else if (!xinew.xi) {
		/* new xino */
		err = au_xino_init(br, calc.idx, xinew.copy_src);
		if (unlikely(err))
			goto out;
	}

	/* force re-creating */
	xinew.xi = br->br_xino;
	xinew.idx = calc.idx;
	mutex_lock(&xinew.xi->xi_mtx);
	file = au_xi_new(sb, &xinew);
	mutex_unlock(&xinew.xi->xi_mtx);
	err = PTR_ERR(file);
	if (IS_ERR(file))
		goto out;
	AuDebugOn(!file);

	err = au_xino_do_write(args->writef, file, &calc, AUFS_ROOT_INO);
	if (unlikely(err))
		au_xino_put(br);

out:
	AuTraceErr(err);
	return err;
}

static int au_xino_set_br(struct super_block *sb, struct path *path)
{
	int err;
	aufs_bindex_t bindex, bbot;
	struct au_xino_do_set_br args;
	struct inode *inode;

	SiMustWriteLock(sb);

	bbot = au_sbbot(sb);
	inode = d_inode(sb->s_root);
	args.writef = au_sbi(sb)->si_xwrite;
	for (bindex = 0; bindex <= bbot; bindex++) {
		args.h_ino = au_h_iptr(inode, bindex)->i_ino;
		args.br = au_sbr(sb, bindex);
		args.bshared = is_sb_shared(sb, bindex, bindex - 1);
		err = au_xino_do_set_br(sb, path, &args);
		if (unlikely(err))
			break;
	}

	AuTraceErr(err);
	return err;
}

void au_xino_clr(struct super_block *sb)
{
	struct au_sbinfo *sbinfo;

	au_xigen_clr(sb);
	xino_clear_xib(sb);
	xino_clear_br(sb);
	dbgaufs_brs_del(sb, 0);
	sbinfo = au_sbi(sb);
	/* lvalue, do not call au_mntflags() */
	au_opt_clr(sbinfo->si_mntflags, XINO);
}

int au_xino_set(struct super_block *sb, struct au_opt_xino *xiopt, int remount)
{
	int err, skip;
	struct dentry *dentry, *parent, *cur_dentry, *cur_parent;
	struct qstr *dname, *cur_name;
	struct file *cur_xino;
	struct au_sbinfo *sbinfo;
	struct path *path, *cur_path;

	SiMustWriteLock(sb);

	err = 0;
	sbinfo = au_sbi(sb);
	path = &xiopt->file->f_path;
	dentry = path->dentry;
	parent = dget_parent(dentry);
	if (remount) {
		skip = 0;
		cur_xino = sbinfo->si_xib;
		if (cur_xino) {
			cur_path = &cur_xino->f_path;
			cur_dentry = cur_path->dentry;
			cur_parent = dget_parent(cur_dentry);
			cur_name = &cur_dentry->d_name;
			dname = &dentry->d_name;
			skip = (cur_parent == parent
				&& au_qstreq(dname, cur_name));
			dput(cur_parent);
		}
		if (skip)
			goto out;
	}

	au_opt_set(sbinfo->si_mntflags, XINO);
	err = au_xino_set_xib(sb, path);
	/* si_x{read,write} are set */
	if (!err)
		err = au_xigen_set(sb, path);
	if (!err)
		err = au_xino_set_br(sb, path);
	if (!err) {
		dbgaufs_brs_add(sb, 0, /*topdown*/1);
		goto out; /* success */
	}

	/* reset all */
	AuIOErr("failed setting xino(%d).\n", err);
	au_xino_clr(sb);

out:
	dput(parent);
	return err;
}

/*
 * create a xinofile at the default place/path.
 */
struct file *au_xino_def(struct super_block *sb)
{
	struct file *file;
	char *page, *p;
	struct au_branch *br;
	struct super_block *h_sb;
	struct path path;
	aufs_bindex_t bbot, bindex, bwr;

	br = NULL;
	bbot = au_sbbot(sb);
	bwr = -1;
	for (bindex = 0; bindex <= bbot; bindex++) {
		br = au_sbr(sb, bindex);
		if (au_br_writable(br->br_perm)
		    && !au_test_fs_bad_xino(au_br_sb(br))) {
			bwr = bindex;
			break;
		}
	}

	if (bwr >= 0) {
		file = ERR_PTR(-ENOMEM);
		page = (void *)__get_free_page(GFP_NOFS);
		if (unlikely(!page))
			goto out;
		path.mnt = au_br_mnt(br);
		path.dentry = au_h_dptr(sb->s_root, bwr);
		p = d_path(&path, page, PATH_MAX - sizeof(AUFS_XINO_FNAME));
		file = (void *)p;
		if (!IS_ERR(p)) {
			strcat(p, "/" AUFS_XINO_FNAME);
			AuDbg("%s\n", p);
			file = au_xino_create(sb, p, /*silent*/0);
		}
		free_page((unsigned long)page);
	} else {
		file = au_xino_create(sb, AUFS_XINO_DEFPATH, /*silent*/0);
		if (IS_ERR(file))
			goto out;
		h_sb = file->f_path.dentry->d_sb;
		if (unlikely(au_test_fs_bad_xino(h_sb))) {
			pr_err("xino doesn't support %s(%s)\n",
			       AUFS_XINO_DEFPATH, au_sbtype(h_sb));
			fput(file);
			file = ERR_PTR(-EINVAL);
		}
	}

out:
	return file;
}

/* ---------------------------------------------------------------------- */

/*
 * initialize the xinofile for the specified branch @br
 * at the place/path where @base_file indicates.
 * test whether another branch is on the same filesystem or not,
 * if found then share the xinofile with another branch.
 */
int au_xino_init_br(struct super_block *sb, struct au_branch *br, ino_t h_ino,
		    struct path *base)
{
	int err;
	struct au_xino_do_set_br args = {
		.h_ino	= h_ino,
		.br	= br
	};

	args.writef = au_sbi(sb)->si_xwrite;
	args.bshared = sbr_find_shared(sb, /*btop*/0, au_sbbot(sb),
				       au_br_sb(br));
	err = au_xino_do_set_br(sb, base, &args);
	if (unlikely(err))
		au_xino_put(br);

	return err;
}

/* ---------------------------------------------------------------------- */

/*
 * get an unused inode number from bitmap
 */
ino_t au_xino_new_ino(struct super_block *sb)
{
	ino_t ino;
	unsigned long *p, pindex, ul, pend;
	struct au_sbinfo *sbinfo;
	struct file *file;
	int free_bit, err;

	if (!au_opt_test(au_mntflags(sb), XINO))
		return iunique(sb, AUFS_FIRST_INO);

	sbinfo = au_sbi(sb);
	mutex_lock(&sbinfo->si_xib_mtx);
	p = sbinfo->si_xib_buf;
	free_bit = sbinfo->si_xib_next_bit;
	if (free_bit < page_bits && !test_bit(free_bit, p))
		goto out; /* success */
	free_bit = find_first_zero_bit(p, page_bits);
	if (free_bit < page_bits)
		goto out; /* success */

	pindex = sbinfo->si_xib_last_pindex;
	for (ul = pindex - 1; ul < ULONG_MAX; ul--) {
		err = xib_pindex(sb, ul);
		if (unlikely(err))
			goto out_err;
		free_bit = find_first_zero_bit(p, page_bits);
		if (free_bit < page_bits)
			goto out; /* success */
	}

	file = sbinfo->si_xib;
	pend = vfsub_f_size_read(file) / PAGE_SIZE;
	for (ul = pindex + 1; ul <= pend; ul++) {
		err = xib_pindex(sb, ul);
		if (unlikely(err))
			goto out_err;
		free_bit = find_first_zero_bit(p, page_bits);
		if (free_bit < page_bits)
			goto out; /* success */
	}
	BUG();

out:
	set_bit(free_bit, p);
	sbinfo->si_xib_next_bit = free_bit + 1;
	pindex = sbinfo->si_xib_last_pindex;
	mutex_unlock(&sbinfo->si_xib_mtx);
	ino = xib_calc_ino(pindex, free_bit);
	AuDbg("i%lu\n", (unsigned long)ino);
	return ino;
out_err:
	mutex_unlock(&sbinfo->si_xib_mtx);
	AuDbg("i0\n");
	return 0;
}

/* for s_op->delete_inode() */
void au_xino_delete_inode(struct inode *inode, const int unlinked)
{
	int err;
	unsigned int mnt_flags;
	aufs_bindex_t bindex, bbot, bi;
	unsigned char try_trunc;
	struct au_iinfo *iinfo;
	struct super_block *sb;
	struct au_hinode *hi;
	struct inode *h_inode;
	struct au_branch *br;
	vfs_writef_t xwrite;
	struct au_xi_calc calc;
	struct file *file;

	AuDebugOn(au_is_bad_inode(inode));

	sb = inode->i_sb;
	mnt_flags = au_mntflags(sb);
	if (!au_opt_test(mnt_flags, XINO)
	    || inode->i_ino == AUFS_ROOT_INO)
		return;

	if (unlinked) {
		au_xigen_inc(inode);
		au_xib_clear_bit(inode);
	}

	iinfo = au_ii(inode);
	bindex = iinfo->ii_btop;
	if (bindex < 0)
		return;

	xwrite = au_sbi(sb)->si_xwrite;
	try_trunc = !!au_opt_test(mnt_flags, TRUNC_XINO);
	hi = au_hinode(iinfo, bindex);
	bbot = iinfo->ii_bbot;
	for (; bindex <= bbot; bindex++, hi++) {
		h_inode = hi->hi_inode;
		if (!h_inode
		    || (!unlinked && h_inode->i_nlink))
			continue;

		/* inode may not be revalidated */
		bi = au_br_index(sb, hi->hi_id);
		if (bi < 0)
			continue;

		br = au_sbr(sb, bi);
		au_xi_calc(sb, h_inode->i_ino, &calc);
		file = au_xino_file(br->br_xino, calc.idx);
		if (IS_ERR_OR_NULL(file))
			continue;

		err = au_xino_do_write(xwrite, file, &calc, /*ino*/0);
		if (!err && try_trunc
		    && au_test_fs_trunc_xino(au_br_sb(br)))
			xino_try_trunc(sb, br);
	}
}

/* ---------------------------------------------------------------------- */

static int au_xinondir_find(struct au_xino *xi, ino_t h_ino)
{
	int found, total, i;

	found = -1;
	total = xi->xi_nondir.total;
	for (i = 0; i < total; i++) {
		if (xi->xi_nondir.array[i] != h_ino)
			continue;
		found = i;
		break;
	}

	return found;
}

static int au_xinondir_expand(struct au_xino *xi)
{
	int err, sz;
	ino_t *p;

	BUILD_BUG_ON(KMALLOC_MAX_SIZE > INT_MAX);

	err = -ENOMEM;
	sz = xi->xi_nondir.total * sizeof(ino_t);
	if (unlikely(sz > KMALLOC_MAX_SIZE / 2))
		goto out;
	p = au_kzrealloc(xi->xi_nondir.array, sz, sz << 1, GFP_ATOMIC,
			 /*may_shrink*/0);
	if (p) {
		xi->xi_nondir.array = p;
		xi->xi_nondir.total <<= 1;
		AuDbg("xi_nondir.total %d\n", xi->xi_nondir.total);
		err = 0;
	}

out:
	return err;
}

void au_xinondir_leave(struct super_block *sb, aufs_bindex_t bindex,
		       ino_t h_ino, int idx)
{
	struct au_xino *xi;

	AuDebugOn(!au_opt_test(au_mntflags(sb), XINO));
	xi = au_sbr(sb, bindex)->br_xino;
	AuDebugOn(idx < 0 || xi->xi_nondir.total <= idx);

	spin_lock(&xi->xi_nondir.spin);
	AuDebugOn(xi->xi_nondir.array[idx] != h_ino);
	xi->xi_nondir.array[idx] = 0;
	spin_unlock(&xi->xi_nondir.spin);
	wake_up_all(&xi->xi_nondir.wqh);
}

int au_xinondir_enter(struct super_block *sb, aufs_bindex_t bindex, ino_t h_ino,
		      int *idx)
{
	int err, found, empty;
	struct au_xino *xi;

	err = 0;
	*idx = -1;
	if (!au_opt_test(au_mntflags(sb), XINO))
		goto out; /* no xino */

	xi = au_sbr(sb, bindex)->br_xino;

again:
	spin_lock(&xi->xi_nondir.spin);
	found = au_xinondir_find(xi, h_ino);
	if (found == -1) {
		empty = au_xinondir_find(xi, /*h_ino*/0);
		if (empty == -1) {
			empty = xi->xi_nondir.total;
			err = au_xinondir_expand(xi);
			if (unlikely(err))
				goto out_unlock;
		}
		xi->xi_nondir.array[empty] = h_ino;
		*idx = empty;
	} else {
		spin_unlock(&xi->xi_nondir.spin);
		wait_event(xi->xi_nondir.wqh,
			   xi->xi_nondir.array[found] != h_ino);
		goto again;
	}

out_unlock:
	spin_unlock(&xi->xi_nondir.spin);
out:
	return err;
}

/* ---------------------------------------------------------------------- */

int au_xino_path(struct seq_file *seq, struct file *file)
{
	int err;

	err = au_seq_path(seq, &file->f_path);
	if (unlikely(err))
		goto out;

#define Deleted "\\040(deleted)"
	seq->count -= sizeof(Deleted) - 1;
	AuDebugOn(memcmp(seq->buf + seq->count, Deleted,
			 sizeof(Deleted) - 1));
#undef Deleted

out:
	return err;
}
