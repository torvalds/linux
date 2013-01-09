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
 * export via nfs
 */

#include <linux/exportfs.h>
#include <linux/mnt_namespace.h>
#include <linux/namei.h>
#include <linux/nsproxy.h>
#include <linux/random.h>
#include <linux/writeback.h>
#include "aufs.h"

union conv {
#ifdef CONFIG_AUFS_INO_T_64
	__u32 a[2];
#else
	__u32 a[1];
#endif
	ino_t ino;
};

static ino_t decode_ino(__u32 *a)
{
	union conv u;

	BUILD_BUG_ON(sizeof(u.ino) != sizeof(u.a));
	u.a[0] = a[0];
#ifdef CONFIG_AUFS_INO_T_64
	u.a[1] = a[1];
#endif
	return u.ino;
}

static void encode_ino(__u32 *a, ino_t ino)
{
	union conv u;

	u.ino = ino;
	a[0] = u.a[0];
#ifdef CONFIG_AUFS_INO_T_64
	a[1] = u.a[1];
#endif
}

/* NFS file handle */
enum {
	Fh_br_id,
	Fh_sigen,
#ifdef CONFIG_AUFS_INO_T_64
	/* support 64bit inode number */
	Fh_ino1,
	Fh_ino2,
	Fh_dir_ino1,
	Fh_dir_ino2,
#else
	Fh_ino1,
	Fh_dir_ino1,
#endif
	Fh_igen,
	Fh_h_type,
	Fh_tail,

	Fh_ino = Fh_ino1,
	Fh_dir_ino = Fh_dir_ino1
};

static int au_test_anon(struct dentry *dentry)
{
	/* note: read d_flags without d_lock */
	return !!(dentry->d_flags & DCACHE_DISCONNECTED);
}

/* ---------------------------------------------------------------------- */
/* inode generation external table */

void au_xigen_inc(struct inode *inode)
{
	loff_t pos;
	ssize_t sz;
	__u32 igen;
	struct super_block *sb;
	struct au_sbinfo *sbinfo;

	sb = inode->i_sb;
	AuDebugOn(!au_opt_test(au_mntflags(sb), XINO));

	sbinfo = au_sbi(sb);
	pos = inode->i_ino;
	pos *= sizeof(igen);
	igen = inode->i_generation + 1;
	sz = xino_fwrite(sbinfo->si_xwrite, sbinfo->si_xigen, &igen,
			 sizeof(igen), &pos);
	if (sz == sizeof(igen))
		return; /* success */

	if (unlikely(sz >= 0))
		AuIOErr("xigen error (%zd)\n", sz);
}

int au_xigen_new(struct inode *inode)
{
	int err;
	loff_t pos;
	ssize_t sz;
	struct super_block *sb;
	struct au_sbinfo *sbinfo;
	struct file *file;

	err = 0;
	/* todo: dirty, at mount time */
	if (inode->i_ino == AUFS_ROOT_INO)
		goto out;
	sb = inode->i_sb;
	SiMustAnyLock(sb);
	if (unlikely(!au_opt_test(au_mntflags(sb), XINO)))
		goto out;

	err = -EFBIG;
	pos = inode->i_ino;
	if (unlikely(au_loff_max / sizeof(inode->i_generation) - 1 < pos)) {
		AuIOErr1("too large i%lld\n", pos);
		goto out;
	}
	pos *= sizeof(inode->i_generation);

	err = 0;
	sbinfo = au_sbi(sb);
	file = sbinfo->si_xigen;
	BUG_ON(!file);

	if (i_size_read(file->f_dentry->d_inode)
	    < pos + sizeof(inode->i_generation)) {
		inode->i_generation = atomic_inc_return(&sbinfo->si_xigen_next);
		sz = xino_fwrite(sbinfo->si_xwrite, file, &inode->i_generation,
				 sizeof(inode->i_generation), &pos);
	} else
		sz = xino_fread(sbinfo->si_xread, file, &inode->i_generation,
				sizeof(inode->i_generation), &pos);
	if (sz == sizeof(inode->i_generation))
		goto out; /* success */

	err = sz;
	if (unlikely(sz >= 0)) {
		err = -EIO;
		AuIOErr("xigen error (%zd)\n", sz);
	}

out:
	return err;
}

int au_xigen_set(struct super_block *sb, struct file *base)
{
	int err;
	struct au_sbinfo *sbinfo;
	struct file *file;

	SiMustWriteLock(sb);

	sbinfo = au_sbi(sb);
	file = au_xino_create2(base, sbinfo->si_xigen);
	err = PTR_ERR(file);
	if (IS_ERR(file))
		goto out;
	err = 0;
	if (sbinfo->si_xigen)
		fput(sbinfo->si_xigen);
	sbinfo->si_xigen = file;

out:
	return err;
}

void au_xigen_clr(struct super_block *sb)
{
	struct au_sbinfo *sbinfo;

	SiMustWriteLock(sb);

	sbinfo = au_sbi(sb);
	if (sbinfo->si_xigen) {
		fput(sbinfo->si_xigen);
		sbinfo->si_xigen = NULL;
	}
}

/* ---------------------------------------------------------------------- */

static struct dentry *decode_by_ino(struct super_block *sb, ino_t ino,
				    ino_t dir_ino)
{
	struct dentry *dentry, *d;
	struct inode *inode;
	unsigned int sigen;

	dentry = NULL;
	inode = ilookup(sb, ino);
	if (!inode)
		goto out;

	dentry = ERR_PTR(-ESTALE);
	sigen = au_sigen(sb);
	if (unlikely(is_bad_inode(inode)
		     || IS_DEADDIR(inode)
		     || sigen != au_iigen(inode, NULL)))
		goto out_iput;

	dentry = NULL;
	if (!dir_ino || S_ISDIR(inode->i_mode))
		dentry = d_find_alias(inode);
	else {
		spin_lock(&inode->i_lock);
		list_for_each_entry(d, &inode->i_dentry, d_alias) {
			spin_lock(&d->d_lock);
			if (!au_test_anon(d)
			    && d->d_parent->d_inode->i_ino == dir_ino) {
				dentry = dget_dlock(d);
				spin_unlock(&d->d_lock);
				break;
			}
			spin_unlock(&d->d_lock);
		}
		spin_unlock(&inode->i_lock);
	}
	if (unlikely(dentry && au_digen_test(dentry, sigen))) {
		/* need to refresh */
		dput(dentry);
		dentry = NULL;
	}

out_iput:
	iput(inode);
out:
	AuTraceErrPtr(dentry);
	return dentry;
}

/* ---------------------------------------------------------------------- */

/* todo: dirty? */
/* if exportfs_decode_fh() passed vfsmount*, we could be happy */

struct au_compare_mnt_args {
	/* input */
	struct super_block *sb;

	/* output */
	struct vfsmount *mnt;
};

static int au_compare_mnt(struct vfsmount *mnt, void *arg)
{
	struct au_compare_mnt_args *a = arg;

	if (mnt->mnt_sb != a->sb)
		return 0;
	a->mnt = mntget(mnt);
	return 1;
}

static struct vfsmount *au_mnt_get(struct super_block *sb)
{
	int err;
	struct au_compare_mnt_args args = {
		.sb = sb
	};
	struct mnt_namespace *ns;

	br_read_lock(vfsmount_lock);
	/* no get/put ?? */
	AuDebugOn(!current->nsproxy);
	ns = current->nsproxy->mnt_ns;
	AuDebugOn(!ns);
	err = iterate_mounts(au_compare_mnt, &args, ns->root);
	br_read_unlock(vfsmount_lock);
	AuDebugOn(!err);
	AuDebugOn(!args.mnt);
	return args.mnt;
}

struct au_nfsd_si_lock {
	unsigned int sigen;
	aufs_bindex_t bindex, br_id;
	unsigned char force_lock;
};

static int si_nfsd_read_lock(struct super_block *sb,
			     struct au_nfsd_si_lock *nsi_lock)
{
	int err;
	aufs_bindex_t bindex;

	si_read_lock(sb, AuLock_FLUSH);

	/* branch id may be wrapped around */
	err = 0;
	bindex = au_br_index(sb, nsi_lock->br_id);
	if (bindex >= 0 && nsi_lock->sigen + AUFS_BRANCH_MAX > au_sigen(sb))
		goto out; /* success */

	err = -ESTALE;
	bindex = -1;
	if (!nsi_lock->force_lock)
		si_read_unlock(sb);

out:
	nsi_lock->bindex = bindex;
	return err;
}

struct find_name_by_ino {
	int called, found;
	ino_t ino;
	char *name;
	int namelen;
};

static int
find_name_by_ino(void *arg, const char *name, int namelen, loff_t offset,
		 u64 ino, unsigned int d_type)
{
	struct find_name_by_ino *a = arg;

	a->called++;
	if (a->ino != ino)
		return 0;

	memcpy(a->name, name, namelen);
	a->namelen = namelen;
	a->found = 1;
	return 1;
}

static struct dentry *au_lkup_by_ino(struct path *path, ino_t ino,
				     struct au_nfsd_si_lock *nsi_lock)
{
	struct dentry *dentry, *parent;
	struct file *file;
	struct inode *dir;
	struct find_name_by_ino arg;
	int err;

	parent = path->dentry;
	if (nsi_lock)
		si_read_unlock(parent->d_sb);
	file = vfsub_dentry_open(path, au_dir_roflags);
	dentry = (void *)file;
	if (IS_ERR(file))
		goto out;

	dentry = ERR_PTR(-ENOMEM);
	arg.name = __getname_gfp(GFP_NOFS);
	if (unlikely(!arg.name))
		goto out_file;
	arg.ino = ino;
	arg.found = 0;
	do {
		arg.called = 0;
		/* smp_mb(); */
		err = vfsub_readdir(file, find_name_by_ino, &arg);
	} while (!err && !arg.found && arg.called);
	dentry = ERR_PTR(err);
	if (unlikely(err))
		goto out_name;
	/* instead of ENOENT */
	dentry = ERR_PTR(-ESTALE);
	if (!arg.found)
		goto out_name;

	/* do not call au_lkup_one() */
	dir = parent->d_inode;
	mutex_lock(&dir->i_mutex);
	dentry = vfsub_lookup_one_len(arg.name, parent, arg.namelen);
	mutex_unlock(&dir->i_mutex);
	AuTraceErrPtr(dentry);
	if (IS_ERR(dentry))
		goto out_name;
	AuDebugOn(au_test_anon(dentry));
	if (unlikely(!dentry->d_inode)) {
		dput(dentry);
		dentry = ERR_PTR(-ENOENT);
	}

out_name:
	__putname(arg.name);
out_file:
	fput(file);
out:
	if (unlikely(nsi_lock
		     && si_nfsd_read_lock(parent->d_sb, nsi_lock) < 0))
		if (!IS_ERR(dentry)) {
			dput(dentry);
			dentry = ERR_PTR(-ESTALE);
		}
	AuTraceErrPtr(dentry);
	return dentry;
}

static struct dentry *decode_by_dir_ino(struct super_block *sb, ino_t ino,
					ino_t dir_ino,
					struct au_nfsd_si_lock *nsi_lock)
{
	struct dentry *dentry;
	struct path path;

	if (dir_ino != AUFS_ROOT_INO) {
		path.dentry = decode_by_ino(sb, dir_ino, 0);
		dentry = path.dentry;
		if (!path.dentry || IS_ERR(path.dentry))
			goto out;
		AuDebugOn(au_test_anon(path.dentry));
	} else
		path.dentry = dget(sb->s_root);

	path.mnt = au_mnt_get(sb);
	dentry = au_lkup_by_ino(&path, ino, nsi_lock);
	path_put(&path);

out:
	AuTraceErrPtr(dentry);
	return dentry;
}

/* ---------------------------------------------------------------------- */

static int h_acceptable(void *expv, struct dentry *dentry)
{
	return 1;
}

static char *au_build_path(struct dentry *h_parent, struct path *h_rootpath,
			   char *buf, int len, struct super_block *sb)
{
	char *p;
	int n;
	struct path path;

	p = d_path(h_rootpath, buf, len);
	if (IS_ERR(p))
		goto out;
	n = strlen(p);

	path.mnt = h_rootpath->mnt;
	path.dentry = h_parent;
	p = d_path(&path, buf, len);
	if (IS_ERR(p))
		goto out;
	if (n != 1)
		p += n;

	path.mnt = au_mnt_get(sb);
	path.dentry = sb->s_root;
	p = d_path(&path, buf, len - strlen(p));
	mntput(path.mnt);
	if (IS_ERR(p))
		goto out;
	if (n != 1)
		p[strlen(p)] = '/';

out:
	AuTraceErrPtr(p);
	return p;
}

static
struct dentry *decode_by_path(struct super_block *sb, ino_t ino, __u32 *fh,
			      int fh_len, struct au_nfsd_si_lock *nsi_lock)
{
	struct dentry *dentry, *h_parent, *root;
	struct super_block *h_sb;
	char *pathname, *p;
	struct vfsmount *h_mnt;
	struct au_branch *br;
	int err;
	struct path path;

	br = au_sbr(sb, nsi_lock->bindex);
	h_mnt = br->br_mnt;
	h_sb = h_mnt->mnt_sb;
	/* todo: call lower fh_to_dentry()? fh_to_parent()? */
	h_parent = exportfs_decode_fh(h_mnt, (void *)(fh + Fh_tail),
				      fh_len - Fh_tail, fh[Fh_h_type],
				      h_acceptable, /*context*/NULL);
	dentry = h_parent;
	if (unlikely(!h_parent || IS_ERR(h_parent))) {
		AuWarn1("%s decode_fh failed, %ld\n",
			au_sbtype(h_sb), PTR_ERR(h_parent));
		goto out;
	}
	dentry = NULL;
	if (unlikely(au_test_anon(h_parent))) {
		AuWarn1("%s decode_fh returned a disconnected dentry\n",
			au_sbtype(h_sb));
		goto out_h_parent;
	}

	dentry = ERR_PTR(-ENOMEM);
	pathname = (void *)__get_free_page(GFP_NOFS);
	if (unlikely(!pathname))
		goto out_h_parent;

	root = sb->s_root;
	path.mnt = h_mnt;
	di_read_lock_parent(root, !AuLock_IR);
	path.dentry = au_h_dptr(root, nsi_lock->bindex);
	di_read_unlock(root, !AuLock_IR);
	p = au_build_path(h_parent, &path, pathname, PAGE_SIZE, sb);
	dentry = (void *)p;
	if (IS_ERR(p))
		goto out_pathname;

	si_read_unlock(sb);
	err = vfsub_kern_path(p, LOOKUP_FOLLOW | LOOKUP_DIRECTORY, &path);
	dentry = ERR_PTR(err);
	if (unlikely(err))
		goto out_relock;

	dentry = ERR_PTR(-ENOENT);
	AuDebugOn(au_test_anon(path.dentry));
	if (unlikely(!path.dentry->d_inode))
		goto out_path;

	if (ino != path.dentry->d_inode->i_ino)
		dentry = au_lkup_by_ino(&path, ino, /*nsi_lock*/NULL);
	else
		dentry = dget(path.dentry);

out_path:
	path_put(&path);
out_relock:
	if (unlikely(si_nfsd_read_lock(sb, nsi_lock) < 0))
		if (!IS_ERR(dentry)) {
			dput(dentry);
			dentry = ERR_PTR(-ESTALE);
		}
out_pathname:
	free_page((unsigned long)pathname);
out_h_parent:
	dput(h_parent);
out:
	AuTraceErrPtr(dentry);
	return dentry;
}

/* ---------------------------------------------------------------------- */

static struct dentry *
aufs_fh_to_dentry(struct super_block *sb, struct fid *fid, int fh_len,
		  int fh_type)
{
	struct dentry *dentry;
	__u32 *fh = fid->raw;
	struct au_branch *br;
	ino_t ino, dir_ino;
	struct au_nfsd_si_lock nsi_lock = {
		.force_lock	= 0
	};

	dentry = ERR_PTR(-ESTALE);
	/* it should never happen, but the file handle is unreliable */
	if (unlikely(fh_len < Fh_tail))
		goto out;
	nsi_lock.sigen = fh[Fh_sigen];
	nsi_lock.br_id = fh[Fh_br_id];

	/* branch id may be wrapped around */
	br = NULL;
	if (unlikely(si_nfsd_read_lock(sb, &nsi_lock)))
		goto out;
	nsi_lock.force_lock = 1;

	/* is this inode still cached? */
	ino = decode_ino(fh + Fh_ino);
	/* it should never happen */
	if (unlikely(ino == AUFS_ROOT_INO))
		goto out;

	dir_ino = decode_ino(fh + Fh_dir_ino);
	dentry = decode_by_ino(sb, ino, dir_ino);
	if (IS_ERR(dentry))
		goto out_unlock;
	if (dentry)
		goto accept;

	/* is the parent dir cached? */
	br = au_sbr(sb, nsi_lock.bindex);
	atomic_inc(&br->br_count);
	dentry = decode_by_dir_ino(sb, ino, dir_ino, &nsi_lock);
	if (IS_ERR(dentry))
		goto out_unlock;
	if (dentry)
		goto accept;

	/* lookup path */
	dentry = decode_by_path(sb, ino, fh, fh_len, &nsi_lock);
	if (IS_ERR(dentry))
		goto out_unlock;
	if (unlikely(!dentry))
		/* todo?: make it ESTALE */
		goto out_unlock;

accept:
	if (!au_digen_test(dentry, au_sigen(sb))
	    && dentry->d_inode->i_generation == fh[Fh_igen])
		goto out_unlock; /* success */

	dput(dentry);
	dentry = ERR_PTR(-ESTALE);
out_unlock:
	if (br)
		atomic_dec(&br->br_count);
	si_read_unlock(sb);
out:
	AuTraceErrPtr(dentry);
	return dentry;
}

#if 0 /* reserved for future use */
/* support subtreecheck option */
static struct dentry *aufs_fh_to_parent(struct super_block *sb, struct fid *fid,
					int fh_len, int fh_type)
{
	struct dentry *parent;
	__u32 *fh = fid->raw;
	ino_t dir_ino;

	dir_ino = decode_ino(fh + Fh_dir_ino);
	parent = decode_by_ino(sb, dir_ino, 0);
	if (IS_ERR(parent))
		goto out;
	if (!parent)
		parent = decode_by_path(sb, au_br_index(sb, fh[Fh_br_id]),
					dir_ino, fh, fh_len);

out:
	AuTraceErrPtr(parent);
	return parent;
}
#endif

/* ---------------------------------------------------------------------- */

static int aufs_encode_fh(struct dentry *dentry, __u32 *fh, int *max_len,
			  int connectable)
{
	int err;
	aufs_bindex_t bindex, bend;
	struct super_block *sb, *h_sb;
	struct inode *inode;
	struct dentry *parent, *h_parent;
	struct au_branch *br;

	AuDebugOn(au_test_anon(dentry));

	parent = NULL;
	err = -ENOSPC;
	if (unlikely(*max_len <= Fh_tail)) {
		AuWarn1("NFSv2 client (max_len %d)?\n", *max_len);
		goto out;
	}

	err = FILEID_ROOT;
	if (IS_ROOT(dentry)) {
		AuDebugOn(dentry->d_inode->i_ino != AUFS_ROOT_INO);
		goto out;
	}

	h_parent = NULL;
	err = aufs_read_lock(dentry, AuLock_FLUSH | AuLock_IR | AuLock_GEN);
	if (unlikely(err))
		goto out;

	inode = dentry->d_inode;
	AuDebugOn(!inode);
	sb = dentry->d_sb;
#ifdef CONFIG_AUFS_DEBUG
	if (unlikely(!au_opt_test(au_mntflags(sb), XINO)))
		AuWarn1("NFS-exporting requires xino\n");
#endif
	err = -EIO;
	parent = dget_parent(dentry);
	di_read_lock_parent(parent, !AuLock_IR);
	bend = au_dbtaildir(parent);
	for (bindex = au_dbstart(parent); bindex <= bend; bindex++) {
		h_parent = au_h_dptr(parent, bindex);
		if (h_parent) {
			dget(h_parent);
			break;
		}
	}
	if (unlikely(!h_parent))
		goto out_unlock;

	err = -EPERM;
	br = au_sbr(sb, bindex);
	h_sb = br->br_mnt->mnt_sb;
	if (unlikely(!h_sb->s_export_op)) {
		AuErr1("%s branch is not exportable\n", au_sbtype(h_sb));
		goto out_dput;
	}

	fh[Fh_br_id] = br->br_id;
	fh[Fh_sigen] = au_sigen(sb);
	encode_ino(fh + Fh_ino, inode->i_ino);
	encode_ino(fh + Fh_dir_ino, parent->d_inode->i_ino);
	fh[Fh_igen] = inode->i_generation;

	*max_len -= Fh_tail;
	fh[Fh_h_type] = exportfs_encode_fh(h_parent, (void *)(fh + Fh_tail),
					   max_len,
					   /*connectable or subtreecheck*/0);
	err = fh[Fh_h_type];
	*max_len += Fh_tail;
	/* todo: macros? */
	if (err != 255)
		err = 99;
	else
		AuWarn1("%s encode_fh failed\n", au_sbtype(h_sb));

out_dput:
	dput(h_parent);
out_unlock:
	di_read_unlock(parent, !AuLock_IR);
	dput(parent);
	aufs_read_unlock(dentry, AuLock_IR);
out:
	if (unlikely(err < 0))
		err = 255;
	return err;
}

/* ---------------------------------------------------------------------- */

static int aufs_commit_metadata(struct inode *inode)
{
	int err;
	aufs_bindex_t bindex;
	struct super_block *sb;
	struct inode *h_inode;
	int (*f)(struct inode *inode);

	sb = inode->i_sb;
	si_read_lock(sb, AuLock_FLUSH | AuLock_NOPLMW);
	ii_write_lock_child(inode);
	bindex = au_ibstart(inode);
	AuDebugOn(bindex < 0);
	h_inode = au_h_iptr(inode, bindex);

	f = h_inode->i_sb->s_export_op->commit_metadata;
	if (f)
		err = f(h_inode);
	else {
		struct writeback_control wbc = {
			.sync_mode	= WB_SYNC_ALL,
			.nr_to_write	= 0 /* metadata only */
		};

		err = sync_inode(h_inode, &wbc);
	}

	au_cpup_attr_timesizes(inode);
	ii_write_unlock(inode);
	si_read_unlock(sb);
	return err;
}

/* ---------------------------------------------------------------------- */

static struct export_operations aufs_export_op = {
	.fh_to_dentry		= aufs_fh_to_dentry,
	/* .fh_to_parent	= aufs_fh_to_parent, */
	.encode_fh		= aufs_encode_fh,
	.commit_metadata	= aufs_commit_metadata
};

void au_export_init(struct super_block *sb)
{
	struct au_sbinfo *sbinfo;
	__u32 u;

	sb->s_export_op = &aufs_export_op;
	sbinfo = au_sbi(sb);
	sbinfo->si_xigen = NULL;
	get_random_bytes(&u, sizeof(u));
	BUILD_BUG_ON(sizeof(u) != sizeof(int));
	atomic_set(&sbinfo->si_xigen_next, u);
}
