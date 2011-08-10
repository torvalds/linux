/*
 * Copyright (C) 2005-2011 Junjiro R. Okajima
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
 * copy-up functions, see wbr_policy.c for copy-down
 */

#include <linux/file.h>
#include <linux/fs_stack.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include "aufs.h"

void au_cpup_attr_flags(struct inode *dst, struct inode *src)
{
	const unsigned int mask = S_DEAD | S_SWAPFILE | S_PRIVATE
		| S_NOATIME | S_NOCMTIME;

	dst->i_flags |= src->i_flags & ~mask;
	if (au_test_fs_notime(dst->i_sb))
		dst->i_flags |= S_NOATIME | S_NOCMTIME;
}

void au_cpup_attr_timesizes(struct inode *inode)
{
	struct inode *h_inode;

	h_inode = au_h_iptr(inode, au_ibstart(inode));
	fsstack_copy_attr_times(inode, h_inode);
	fsstack_copy_inode_size(inode, h_inode);
}

void au_cpup_attr_nlink(struct inode *inode, int force)
{
	struct inode *h_inode;
	struct super_block *sb;
	aufs_bindex_t bindex, bend;

	sb = inode->i_sb;
	bindex = au_ibstart(inode);
	h_inode = au_h_iptr(inode, bindex);
	if (!force
	    && !S_ISDIR(h_inode->i_mode)
	    && au_opt_test(au_mntflags(sb), PLINK)
	    && au_plink_test(inode))
		return;

	inode->i_nlink = h_inode->i_nlink;

	/*
	 * fewer nlink makes find(1) noisy, but larger nlink doesn't.
	 * it may includes whplink directory.
	 */
	if (S_ISDIR(h_inode->i_mode)) {
		bend = au_ibend(inode);
		for (bindex++; bindex <= bend; bindex++) {
			h_inode = au_h_iptr(inode, bindex);
			if (h_inode)
				au_add_nlink(inode, h_inode);
		}
	}
}

void au_cpup_attr_changeable(struct inode *inode)
{
	struct inode *h_inode;

	h_inode = au_h_iptr(inode, au_ibstart(inode));
	inode->i_mode = h_inode->i_mode;
	inode->i_uid = h_inode->i_uid;
	inode->i_gid = h_inode->i_gid;
	au_cpup_attr_timesizes(inode);
	au_cpup_attr_flags(inode, h_inode);
}

void au_cpup_igen(struct inode *inode, struct inode *h_inode)
{
	struct au_iinfo *iinfo = au_ii(inode);

	IiMustWriteLock(inode);

	iinfo->ii_higen = h_inode->i_generation;
	iinfo->ii_hsb1 = h_inode->i_sb;
}

void au_cpup_attr_all(struct inode *inode, int force)
{
	struct inode *h_inode;

	h_inode = au_h_iptr(inode, au_ibstart(inode));
	au_cpup_attr_changeable(inode);
	if (inode->i_nlink > 0)
		au_cpup_attr_nlink(inode, force);
	inode->i_rdev = h_inode->i_rdev;
	inode->i_blkbits = h_inode->i_blkbits;
	au_cpup_igen(inode, h_inode);
}

/* ---------------------------------------------------------------------- */

/* Note: dt_dentry and dt_h_dentry are not dget/dput-ed */

/* keep the timestamps of the parent dir when cpup */
void au_dtime_store(struct au_dtime *dt, struct dentry *dentry,
		    struct path *h_path)
{
	struct inode *h_inode;

	dt->dt_dentry = dentry;
	dt->dt_h_path = *h_path;
	h_inode = h_path->dentry->d_inode;
	dt->dt_atime = h_inode->i_atime;
	dt->dt_mtime = h_inode->i_mtime;
	/* smp_mb(); */
}

void au_dtime_revert(struct au_dtime *dt)
{
	struct iattr attr;
	int err;

	attr.ia_atime = dt->dt_atime;
	attr.ia_mtime = dt->dt_mtime;
	attr.ia_valid = ATTR_FORCE | ATTR_MTIME | ATTR_MTIME_SET
		| ATTR_ATIME | ATTR_ATIME_SET;

	err = vfsub_notify_change(&dt->dt_h_path, &attr);
	if (unlikely(err))
		pr_warning("restoring timestamps failed(%d). ignored\n", err);
}

/* ---------------------------------------------------------------------- */

static noinline_for_stack
int cpup_iattr(struct dentry *dst, aufs_bindex_t bindex, struct dentry *h_src)
{
	int err, sbits;
	struct iattr ia;
	struct path h_path;
	struct inode *h_isrc, *h_idst;

	h_path.dentry = au_h_dptr(dst, bindex);
	h_idst = h_path.dentry->d_inode;
	h_path.mnt = au_sbr_mnt(dst->d_sb, bindex);
	h_isrc = h_src->d_inode;
	ia.ia_valid = ATTR_FORCE | ATTR_UID | ATTR_GID
		| ATTR_ATIME | ATTR_MTIME
		| ATTR_ATIME_SET | ATTR_MTIME_SET;
	ia.ia_uid = h_isrc->i_uid;
	ia.ia_gid = h_isrc->i_gid;
	ia.ia_atime = h_isrc->i_atime;
	ia.ia_mtime = h_isrc->i_mtime;
	if (h_idst->i_mode != h_isrc->i_mode
	    && !S_ISLNK(h_idst->i_mode)) {
		ia.ia_valid |= ATTR_MODE;
		ia.ia_mode = h_isrc->i_mode;
	}
	sbits = !!(h_isrc->i_mode & (S_ISUID | S_ISGID));
	au_cpup_attr_flags(h_idst, h_isrc);
	err = vfsub_notify_change(&h_path, &ia);

	/* is this nfs only? */
	if (!err && sbits && au_test_nfs(h_path.dentry->d_sb)) {
		ia.ia_valid = ATTR_FORCE | ATTR_MODE;
		ia.ia_mode = h_isrc->i_mode;
		err = vfsub_notify_change(&h_path, &ia);
	}

	return err;
}

/* ---------------------------------------------------------------------- */

static int au_do_copy_file(struct file *dst, struct file *src, loff_t len,
			   char *buf, unsigned long blksize)
{
	int err;
	size_t sz, rbytes, wbytes;
	unsigned char all_zero;
	char *p, *zp;
	struct mutex *h_mtx;
	/* reduce stack usage */
	struct iattr *ia;

	zp = page_address(ZERO_PAGE(0));
	if (unlikely(!zp))
		return -ENOMEM; /* possible? */

	err = 0;
	all_zero = 0;
	while (len) {
		AuDbg("len %lld\n", len);
		sz = blksize;
		if (len < blksize)
			sz = len;

		rbytes = 0;
		/* todo: signal_pending? */
		while (!rbytes || err == -EAGAIN || err == -EINTR) {
			rbytes = vfsub_read_k(src, buf, sz, &src->f_pos);
			err = rbytes;
		}
		if (unlikely(err < 0))
			break;

		all_zero = 0;
		if (len >= rbytes && rbytes == blksize)
			all_zero = !memcmp(buf, zp, rbytes);
		if (!all_zero) {
			wbytes = rbytes;
			p = buf;
			while (wbytes) {
				size_t b;

				b = vfsub_write_k(dst, p, wbytes, &dst->f_pos);
				err = b;
				/* todo: signal_pending? */
				if (unlikely(err == -EAGAIN || err == -EINTR))
					continue;
				if (unlikely(err < 0))
					break;
				wbytes -= b;
				p += b;
			}
		} else {
			loff_t res;

			AuLabel(hole);
			res = vfsub_llseek(dst, rbytes, SEEK_CUR);
			err = res;
			if (unlikely(res < 0))
				break;
		}
		len -= rbytes;
		err = 0;
	}

	/* the last block may be a hole */
	if (!err && all_zero) {
		AuLabel(last hole);

		err = 1;
		if (au_test_nfs(dst->f_dentry->d_sb)) {
			/* nfs requires this step to make last hole */
			/* is this only nfs? */
			do {
				/* todo: signal_pending? */
				err = vfsub_write_k(dst, "\0", 1, &dst->f_pos);
			} while (err == -EAGAIN || err == -EINTR);
			if (err == 1)
				dst->f_pos--;
		}

		if (err == 1) {
			ia = (void *)buf;
			ia->ia_size = dst->f_pos;
			ia->ia_valid = ATTR_SIZE | ATTR_FILE;
			ia->ia_file = dst;
			h_mtx = &dst->f_dentry->d_inode->i_mutex;
			mutex_lock_nested(h_mtx, AuLsc_I_CHILD2);
			err = vfsub_notify_change(&dst->f_path, ia);
			mutex_unlock(h_mtx);
		}
	}

	return err;
}

int au_copy_file(struct file *dst, struct file *src, loff_t len)
{
	int err;
	unsigned long blksize;
	unsigned char do_kfree;
	char *buf;

	err = -ENOMEM;
	blksize = dst->f_dentry->d_sb->s_blocksize;
	if (!blksize || PAGE_SIZE < blksize)
		blksize = PAGE_SIZE;
	AuDbg("blksize %lu\n", blksize);
	do_kfree = (blksize != PAGE_SIZE && blksize >= sizeof(struct iattr *));
	if (do_kfree)
		buf = kmalloc(blksize, GFP_NOFS);
	else
		buf = (void *)__get_free_page(GFP_NOFS);
	if (unlikely(!buf))
		goto out;

	if (len > (1 << 22))
		AuDbg("copying a large file %lld\n", (long long)len);

	src->f_pos = 0;
	dst->f_pos = 0;
	err = au_do_copy_file(dst, src, len, buf, blksize);
	if (do_kfree)
		kfree(buf);
	else
		free_page((unsigned long)buf);

out:
	return err;
}

/*
 * to support a sparse file which is opened with O_APPEND,
 * we need to close the file.
 */
static int au_cp_regular(struct dentry *dentry, aufs_bindex_t bdst,
			 aufs_bindex_t bsrc, loff_t len)
{
	int err, i;
	enum { SRC, DST };
	struct {
		aufs_bindex_t bindex;
		unsigned int flags;
		struct dentry *dentry;
		struct file *file;
		void *label, *label_file;
	} *f, file[] = {
		{
			.bindex = bsrc,
			.flags = O_RDONLY | O_NOATIME | O_LARGEFILE,
			.file = NULL,
			.label = &&out,
			.label_file = &&out_src
		},
		{
			.bindex = bdst,
			.flags = O_WRONLY | O_NOATIME | O_LARGEFILE,
			.file = NULL,
			.label = &&out_src,
			.label_file = &&out_dst
		}
	};
	struct super_block *sb;

	/* bsrc branch can be ro/rw. */
	sb = dentry->d_sb;
	f = file;
	for (i = 0; i < 2; i++, f++) {
		f->dentry = au_h_dptr(dentry, f->bindex);
		f->file = au_h_open(dentry, f->bindex, f->flags, /*file*/NULL);
		err = PTR_ERR(f->file);
		if (IS_ERR(f->file))
			goto *f->label;
		err = -EINVAL;
		if (unlikely(!f->file->f_op))
			goto *f->label_file;
	}

	/* try stopping to update while we copyup */
	IMustLock(file[SRC].dentry->d_inode);
	err = au_copy_file(file[DST].file, file[SRC].file, len);

out_dst:
	fput(file[DST].file);
	au_sbr_put(sb, file[DST].bindex);
out_src:
	fput(file[SRC].file);
	au_sbr_put(sb, file[SRC].bindex);
out:
	return err;
}

static int au_do_cpup_regular(struct dentry *dentry, aufs_bindex_t bdst,
			      aufs_bindex_t bsrc, loff_t len,
			      struct inode *h_dir, struct path *h_path)
{
	int err, rerr;
	loff_t l;

	err = 0;
	l = i_size_read(au_h_iptr(dentry->d_inode, bsrc));
	if (len == -1 || l < len)
		len = l;
	if (len)
		err = au_cp_regular(dentry, bdst, bsrc, len);
	if (!err)
		goto out; /* success */

	rerr = vfsub_unlink(h_dir, h_path, /*force*/0);
	if (rerr) {
		AuIOErr("failed unlinking cpup-ed %.*s(%d, %d)\n",
			AuDLNPair(h_path->dentry), err, rerr);
		err = -EIO;
	}

out:
	return err;
}

static int au_do_cpup_symlink(struct path *h_path, struct dentry *h_src,
			      struct inode *h_dir)
{
	int err, symlen;
	mm_segment_t old_fs;
	union {
		char *k;
		char __user *u;
	} sym;

	err = -ENOSYS;
	if (unlikely(!h_src->d_inode->i_op->readlink))
		goto out;

	err = -ENOMEM;
	sym.k = __getname_gfp(GFP_NOFS);
	if (unlikely(!sym.k))
		goto out;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	symlen = h_src->d_inode->i_op->readlink(h_src, sym.u, PATH_MAX);
	err = symlen;
	set_fs(old_fs);

	if (symlen > 0) {
		sym.k[symlen] = 0;
		err = vfsub_symlink(h_dir, h_path, sym.k);
	}
	__putname(sym.k);

out:
	return err;
}

/* return with the lower dst inode is locked */
static noinline_for_stack
int cpup_entry(struct dentry *dentry, aufs_bindex_t bdst,
	       aufs_bindex_t bsrc, loff_t len, unsigned int flags,
	       struct dentry *dst_parent)
{
	int err;
	umode_t mode;
	unsigned int mnt_flags;
	unsigned char isdir;
	const unsigned char do_dt = !!au_ftest_cpup(flags, DTIME);
	struct au_dtime dt;
	struct path h_path;
	struct dentry *h_src, *h_dst, *h_parent;
	struct inode *h_inode, *h_dir;
	struct super_block *sb;

	/* bsrc branch can be ro/rw. */
	h_src = au_h_dptr(dentry, bsrc);
	h_inode = h_src->d_inode;
	AuDebugOn(h_inode != au_h_iptr(dentry->d_inode, bsrc));

	/* try stopping to be referenced while we are creating */
	h_dst = au_h_dptr(dentry, bdst);
	h_parent = h_dst->d_parent; /* dir inode is locked */
	h_dir = h_parent->d_inode;
	IMustLock(h_dir);
	AuDebugOn(h_parent != h_dst->d_parent);

	sb = dentry->d_sb;
	h_path.mnt = au_sbr_mnt(sb, bdst);
	if (do_dt) {
		h_path.dentry = h_parent;
		au_dtime_store(&dt, dst_parent, &h_path);
	}
	h_path.dentry = h_dst;

	isdir = 0;
	mode = h_inode->i_mode;
	switch (mode & S_IFMT) {
	case S_IFREG:
		/* try stopping to update while we are referencing */
		IMustLock(h_inode);
		err = vfsub_create(h_dir, &h_path, mode | S_IWUSR);
		if (!err)
			err = au_do_cpup_regular
				(dentry, bdst, bsrc, len,
				 au_h_iptr(dst_parent->d_inode, bdst), &h_path);
		break;
	case S_IFDIR:
		isdir = 1;
		err = vfsub_mkdir(h_dir, &h_path, mode);
		if (!err) {
			/*
			 * strange behaviour from the users view,
			 * particularry setattr case
			 */
			if (au_ibstart(dst_parent->d_inode) == bdst)
				au_cpup_attr_nlink(dst_parent->d_inode,
						   /*force*/1);
			au_cpup_attr_nlink(dentry->d_inode, /*force*/1);
		}
		break;
	case S_IFLNK:
		err = au_do_cpup_symlink(&h_path, h_src, h_dir);
		break;
	case S_IFCHR:
	case S_IFBLK:
		AuDebugOn(!capable(CAP_MKNOD));
		/*FALLTHROUGH*/
	case S_IFIFO:
	case S_IFSOCK:
		err = vfsub_mknod(h_dir, &h_path, mode, h_inode->i_rdev);
		break;
	default:
		AuIOErr("Unknown inode type 0%o\n", mode);
		err = -EIO;
	}

	mnt_flags = au_mntflags(sb);
	if (!au_opt_test(mnt_flags, UDBA_NONE)
	    && !isdir
	    && au_opt_test(mnt_flags, XINO)
	    && h_inode->i_nlink == 1
	    /* todo: unnecessary? */
	    /* && dentry->d_inode->i_nlink == 1 */
	    && bdst < bsrc
	    && !au_ftest_cpup(flags, KEEPLINO))
		au_xino_write(sb, bsrc, h_inode->i_ino, /*ino*/0);
		/* ignore this error */

	if (do_dt)
		au_dtime_revert(&dt);
	return err;
}

/*
 * copyup the @dentry from @bsrc to @bdst.
 * the caller must set the both of lower dentries.
 * @len is for truncating when it is -1 copyup the entire file.
 * in link/rename cases, @dst_parent may be different from the real one.
 */
static int au_cpup_single(struct dentry *dentry, aufs_bindex_t bdst,
			  aufs_bindex_t bsrc, loff_t len, unsigned int flags,
			  struct dentry *dst_parent)
{
	int err, rerr;
	aufs_bindex_t old_ibstart;
	unsigned char isdir, plink;
	struct au_dtime dt;
	struct path h_path;
	struct dentry *h_src, *h_dst, *h_parent;
	struct inode *dst_inode, *h_dir, *inode;
	struct super_block *sb;

	AuDebugOn(bsrc <= bdst);

	sb = dentry->d_sb;
	h_path.mnt = au_sbr_mnt(sb, bdst);
	h_dst = au_h_dptr(dentry, bdst);
	h_parent = h_dst->d_parent; /* dir inode is locked */
	h_dir = h_parent->d_inode;
	IMustLock(h_dir);

	h_src = au_h_dptr(dentry, bsrc);
	inode = dentry->d_inode;

	if (!dst_parent)
		dst_parent = dget_parent(dentry);
	else
		dget(dst_parent);

	plink = !!au_opt_test(au_mntflags(sb), PLINK);
	dst_inode = au_h_iptr(inode, bdst);
	if (dst_inode) {
		if (unlikely(!plink)) {
			err = -EIO;
			AuIOErr("hi%lu(i%lu) exists on b%d "
				"but plink is disabled\n",
				dst_inode->i_ino, inode->i_ino, bdst);
			goto out;
		}

		if (dst_inode->i_nlink) {
			const int do_dt = au_ftest_cpup(flags, DTIME);

			h_src = au_plink_lkup(inode, bdst);
			err = PTR_ERR(h_src);
			if (IS_ERR(h_src))
				goto out;
			if (unlikely(!h_src->d_inode)) {
				err = -EIO;
				AuIOErr("i%lu exists on a upper branch "
					"but not pseudo-linked\n",
					inode->i_ino);
				dput(h_src);
				goto out;
			}

			if (do_dt) {
				h_path.dentry = h_parent;
				au_dtime_store(&dt, dst_parent, &h_path);
			}
			h_path.dentry = h_dst;
			err = vfsub_link(h_src, h_dir, &h_path);
			if (do_dt)
				au_dtime_revert(&dt);
			dput(h_src);
			goto out;
		} else
			/* todo: cpup_wh_file? */
			/* udba work */
			au_update_ibrange(inode, /*do_put_zero*/1);
	}

	old_ibstart = au_ibstart(inode);
	err = cpup_entry(dentry, bdst, bsrc, len, flags, dst_parent);
	if (unlikely(err))
		goto out;
	dst_inode = h_dst->d_inode;
	mutex_lock_nested(&dst_inode->i_mutex, AuLsc_I_CHILD2);

	err = cpup_iattr(dentry, bdst, h_src);
	isdir = S_ISDIR(dst_inode->i_mode);
	if (!err) {
		if (bdst < old_ibstart) {
			if (S_ISREG(inode->i_mode)) {
				err = au_dy_iaop(inode, bdst, dst_inode);
				if (unlikely(err))
					goto out_rev;
			}
			au_set_ibstart(inode, bdst);
		}
		au_set_h_iptr(inode, bdst, au_igrab(dst_inode),
			      au_hi_flags(inode, isdir));
		mutex_unlock(&dst_inode->i_mutex);
		if (!isdir
		    && h_src->d_inode->i_nlink > 1
		    && plink)
			au_plink_append(inode, bdst, h_dst);
		goto out; /* success */
	}

	/* revert */
out_rev:
	h_path.dentry = h_parent;
	mutex_unlock(&dst_inode->i_mutex);
	au_dtime_store(&dt, dst_parent, &h_path);
	h_path.dentry = h_dst;
	if (!isdir)
		rerr = vfsub_unlink(h_dir, &h_path, /*force*/0);
	else
		rerr = vfsub_rmdir(h_dir, &h_path);
	au_dtime_revert(&dt);
	if (rerr) {
		AuIOErr("failed removing broken entry(%d, %d)\n", err, rerr);
		err = -EIO;
	}

out:
	dput(dst_parent);
	return err;
}

struct au_cpup_single_args {
	int *errp;
	struct dentry *dentry;
	aufs_bindex_t bdst, bsrc;
	loff_t len;
	unsigned int flags;
	struct dentry *dst_parent;
};

static void au_call_cpup_single(void *args)
{
	struct au_cpup_single_args *a = args;
	*a->errp = au_cpup_single(a->dentry, a->bdst, a->bsrc, a->len,
				  a->flags, a->dst_parent);
}

/*
 * prevent SIGXFSZ in copy-up.
 * testing CAP_MKNOD is for generic fs,
 * but CAP_FSETID is for xfs only, currently.
 */
static int au_cpup_sio_test(struct super_block *sb, umode_t mode)
{
	int do_sio;

	do_sio = 0;
	if (!au_wkq_test()
	    && (!au_sbi(sb)->si_plink_maint_pid
		|| au_plink_maint(sb, AuLock_NOPLM))) {
		switch (mode & S_IFMT) {
		case S_IFREG:
			/* no condition about RLIMIT_FSIZE and the file size */
			do_sio = 1;
			break;
		case S_IFCHR:
		case S_IFBLK:
			do_sio = !capable(CAP_MKNOD);
			break;
		}
		if (!do_sio)
			do_sio = ((mode & (S_ISUID | S_ISGID))
				  && !capable(CAP_FSETID));
	}

	return do_sio;
}

int au_sio_cpup_single(struct dentry *dentry, aufs_bindex_t bdst,
		       aufs_bindex_t bsrc, loff_t len, unsigned int flags,
		       struct dentry *dst_parent)
{
	int err, wkq_err;
	struct dentry *h_dentry;

	h_dentry = au_h_dptr(dentry, bsrc);
	if (!au_cpup_sio_test(dentry->d_sb, h_dentry->d_inode->i_mode))
		err = au_cpup_single(dentry, bdst, bsrc, len, flags,
				     dst_parent);
	else {
		struct au_cpup_single_args args = {
			.errp		= &err,
			.dentry		= dentry,
			.bdst		= bdst,
			.bsrc		= bsrc,
			.len		= len,
			.flags		= flags,
			.dst_parent	= dst_parent
		};
		wkq_err = au_wkq_wait(au_call_cpup_single, &args);
		if (unlikely(wkq_err))
			err = wkq_err;
	}

	return err;
}

/*
 * copyup the @dentry from the first active lower branch to @bdst,
 * using au_cpup_single().
 */
static int au_cpup_simple(struct dentry *dentry, aufs_bindex_t bdst, loff_t len,
			  unsigned int flags)
{
	int err;
	aufs_bindex_t bsrc, bend;

	bend = au_dbend(dentry);
	for (bsrc = bdst + 1; bsrc <= bend; bsrc++)
		if (au_h_dptr(dentry, bsrc))
			break;

	err = au_lkup_neg(dentry, bdst);
	if (!err) {
		err = au_cpup_single(dentry, bdst, bsrc, len, flags, NULL);
		if (!err)
			return 0; /* success */

		/* revert */
		au_set_h_dptr(dentry, bdst, NULL);
		au_set_dbstart(dentry, bsrc);
	}

	return err;
}

struct au_cpup_simple_args {
	int *errp;
	struct dentry *dentry;
	aufs_bindex_t bdst;
	loff_t len;
	unsigned int flags;
};

static void au_call_cpup_simple(void *args)
{
	struct au_cpup_simple_args *a = args;
	*a->errp = au_cpup_simple(a->dentry, a->bdst, a->len, a->flags);
}

int au_sio_cpup_simple(struct dentry *dentry, aufs_bindex_t bdst, loff_t len,
		       unsigned int flags)
{
	int err, wkq_err;
	struct dentry *parent;
	struct inode *h_dir;

	parent = dget_parent(dentry);
	h_dir = au_h_iptr(parent->d_inode, bdst);
	if (!au_test_h_perm_sio(h_dir, MAY_EXEC | MAY_WRITE)
	    && !au_cpup_sio_test(dentry->d_sb, dentry->d_inode->i_mode))
		err = au_cpup_simple(dentry, bdst, len, flags);
	else {
		struct au_cpup_simple_args args = {
			.errp		= &err,
			.dentry		= dentry,
			.bdst		= bdst,
			.len		= len,
			.flags		= flags
		};
		wkq_err = au_wkq_wait(au_call_cpup_simple, &args);
		if (unlikely(wkq_err))
			err = wkq_err;
	}

	dput(parent);
	return err;
}

/* ---------------------------------------------------------------------- */

/*
 * copyup the deleted file for writing.
 */
static int au_do_cpup_wh(struct dentry *dentry, aufs_bindex_t bdst,
			 struct dentry *wh_dentry, struct file *file,
			 loff_t len)
{
	int err;
	aufs_bindex_t bstart;
	struct au_dinfo *dinfo;
	struct dentry *h_d_dst, *h_d_start;
	struct au_hdentry *hdp;

	dinfo = au_di(dentry);
	AuRwMustWriteLock(&dinfo->di_rwsem);

	bstart = dinfo->di_bstart;
	hdp = dinfo->di_hdentry;
	h_d_dst = hdp[0 + bdst].hd_dentry;
	dinfo->di_bstart = bdst;
	hdp[0 + bdst].hd_dentry = wh_dentry;
	if (file) {
		h_d_start = hdp[0 + bstart].hd_dentry;
		hdp[0 + bstart].hd_dentry = au_hf_top(file)->f_dentry;
	}
	err = au_cpup_single(dentry, bdst, bstart, len, !AuCpup_DTIME,
			     /*h_parent*/NULL);
	if (file) {
		if (!err)
			err = au_reopen_nondir(file);
		hdp[0 + bstart].hd_dentry = h_d_start;
	}
	hdp[0 + bdst].hd_dentry = h_d_dst;
	dinfo->di_bstart = bstart;

	return err;
}

static int au_cpup_wh(struct dentry *dentry, aufs_bindex_t bdst, loff_t len,
		      struct file *file)
{
	int err;
	struct au_dtime dt;
	struct dentry *parent, *h_parent, *wh_dentry;
	struct au_branch *br;
	struct path h_path;

	br = au_sbr(dentry->d_sb, bdst);
	parent = dget_parent(dentry);
	h_parent = au_h_dptr(parent, bdst);
	wh_dentry = au_whtmp_lkup(h_parent, br, &dentry->d_name);
	err = PTR_ERR(wh_dentry);
	if (IS_ERR(wh_dentry))
		goto out;

	h_path.dentry = h_parent;
	h_path.mnt = br->br_mnt;
	au_dtime_store(&dt, parent, &h_path);
	err = au_do_cpup_wh(dentry, bdst, wh_dentry, file, len);
	if (unlikely(err))
		goto out_wh;

	dget(wh_dentry);
	h_path.dentry = wh_dentry;
	if (!S_ISDIR(wh_dentry->d_inode->i_mode))
		err = vfsub_unlink(h_parent->d_inode, &h_path, /*force*/0);
	else
		err = vfsub_rmdir(h_parent->d_inode, &h_path);
	if (unlikely(err)) {
		AuIOErr("failed remove copied-up tmp file %.*s(%d)\n",
			AuDLNPair(wh_dentry), err);
		err = -EIO;
	}
	au_dtime_revert(&dt);
	au_set_hi_wh(dentry->d_inode, bdst, wh_dentry);

out_wh:
	dput(wh_dentry);
out:
	dput(parent);
	return err;
}

struct au_cpup_wh_args {
	int *errp;
	struct dentry *dentry;
	aufs_bindex_t bdst;
	loff_t len;
	struct file *file;
};

static void au_call_cpup_wh(void *args)
{
	struct au_cpup_wh_args *a = args;
	*a->errp = au_cpup_wh(a->dentry, a->bdst, a->len, a->file);
}

int au_sio_cpup_wh(struct dentry *dentry, aufs_bindex_t bdst, loff_t len,
		   struct file *file)
{
	int err, wkq_err;
	struct dentry *parent, *h_orph, *h_parent, *h_dentry;
	struct inode *dir, *h_dir, *h_tmpdir, *h_inode;
	struct au_wbr *wbr;

	parent = dget_parent(dentry);
	dir = parent->d_inode;
	h_orph = NULL;
	h_parent = NULL;
	h_dir = au_igrab(au_h_iptr(dir, bdst));
	h_tmpdir = h_dir;
	if (!h_dir->i_nlink) {
		wbr = au_sbr(dentry->d_sb, bdst)->br_wbr;
		h_orph = wbr->wbr_orph;

		h_parent = dget(au_h_dptr(parent, bdst));
		au_set_h_dptr(parent, bdst, dget(h_orph));
		h_tmpdir = h_orph->d_inode;
		au_set_h_iptr(dir, bdst, au_igrab(h_tmpdir), /*flags*/0);

		/* this temporary unlock is safe */
		if (file)
			h_dentry = au_hf_top(file)->f_dentry;
		else
			h_dentry = au_h_dptr(dentry, au_dbstart(dentry));
		h_inode = h_dentry->d_inode;
		IMustLock(h_inode);
		mutex_unlock(&h_inode->i_mutex);
		mutex_lock_nested(&h_tmpdir->i_mutex, AuLsc_I_PARENT3);
		mutex_lock_nested(&h_inode->i_mutex, AuLsc_I_CHILD);
		/* todo: au_h_open_pre()? */
	}

	if (!au_test_h_perm_sio(h_tmpdir, MAY_EXEC | MAY_WRITE)
	    && !au_cpup_sio_test(dentry->d_sb, dentry->d_inode->i_mode))
		err = au_cpup_wh(dentry, bdst, len, file);
	else {
		struct au_cpup_wh_args args = {
			.errp	= &err,
			.dentry	= dentry,
			.bdst	= bdst,
			.len	= len,
			.file	= file
		};
		wkq_err = au_wkq_wait(au_call_cpup_wh, &args);
		if (unlikely(wkq_err))
			err = wkq_err;
	}

	if (h_orph) {
		mutex_unlock(&h_tmpdir->i_mutex);
		/* todo: au_h_open_post()? */
		au_set_h_iptr(dir, bdst, au_igrab(h_dir), /*flags*/0);
		au_set_h_dptr(parent, bdst, h_parent);
	}
	iput(h_dir);
	dput(parent);

	return err;
}

/* ---------------------------------------------------------------------- */

/*
 * generic routine for both of copy-up and copy-down.
 */
/* cf. revalidate function in file.c */
int au_cp_dirs(struct dentry *dentry, aufs_bindex_t bdst,
	       int (*cp)(struct dentry *dentry, aufs_bindex_t bdst,
			 struct dentry *h_parent, void *arg),
	       void *arg)
{
	int err;
	struct au_pin pin;
	struct dentry *d, *parent, *h_parent, *real_parent;

	err = 0;
	parent = dget_parent(dentry);
	if (IS_ROOT(parent))
		goto out;

	au_pin_init(&pin, dentry, bdst, AuLsc_DI_PARENT2, AuLsc_I_PARENT2,
		    au_opt_udba(dentry->d_sb), AuPin_MNT_WRITE);

	/* do not use au_dpage */
	real_parent = parent;
	while (1) {
		dput(parent);
		parent = dget_parent(dentry);
		h_parent = au_h_dptr(parent, bdst);
		if (h_parent)
			goto out; /* success */

		/* find top dir which is necessary to cpup */
		do {
			d = parent;
			dput(parent);
			parent = dget_parent(d);
			di_read_lock_parent3(parent, !AuLock_IR);
			h_parent = au_h_dptr(parent, bdst);
			di_read_unlock(parent, !AuLock_IR);
		} while (!h_parent);

		if (d != real_parent)
			di_write_lock_child3(d);

		/* somebody else might create while we were sleeping */
		if (!au_h_dptr(d, bdst) || !au_h_dptr(d, bdst)->d_inode) {
			if (au_h_dptr(d, bdst))
				au_update_dbstart(d);

			au_pin_set_dentry(&pin, d);
			err = au_do_pin(&pin);
			if (!err) {
				err = cp(d, bdst, h_parent, arg);
				au_unpin(&pin);
			}
		}

		if (d != real_parent)
			di_write_unlock(d);
		if (unlikely(err))
			break;
	}

out:
	dput(parent);
	return err;
}

static int au_cpup_dir(struct dentry *dentry, aufs_bindex_t bdst,
		       struct dentry *h_parent __maybe_unused ,
		       void *arg __maybe_unused)
{
	return au_sio_cpup_simple(dentry, bdst, -1, AuCpup_DTIME);
}

int au_cpup_dirs(struct dentry *dentry, aufs_bindex_t bdst)
{
	return au_cp_dirs(dentry, bdst, au_cpup_dir, NULL);
}

int au_test_and_cpup_dirs(struct dentry *dentry, aufs_bindex_t bdst)
{
	int err;
	struct dentry *parent;
	struct inode *dir;

	parent = dget_parent(dentry);
	dir = parent->d_inode;
	err = 0;
	if (au_h_iptr(dir, bdst))
		goto out;

	di_read_unlock(parent, AuLock_IR);
	di_write_lock_parent(parent);
	/* someone else might change our inode while we were sleeping */
	if (!au_h_iptr(dir, bdst))
		err = au_cpup_dirs(dentry, bdst);
	di_downgrade_lock(parent, AuLock_IR);

out:
	dput(parent);
	return err;
}
