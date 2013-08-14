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
 * file operations for special files.
 * while they exist in aufs virtually,
 * their file I/O is handled out of aufs.
 */

#include <linux/aio.h>
#include "aufs.h"

/*
 * I don't think the size of this list grows much.
 * so here is a very simple list implemented in order to find finfo matching a
 * given file.
 */
static struct au_sphlhead au_finfo_sp = {
	.spin	= __SPIN_LOCK_INITIALIZER(au_finfo_sp.spin),
	.head	= HLIST_HEAD_INIT
};

struct au_finfo_sp {
	struct hlist_node	hlist;
	struct file		*file;
	struct au_finfo		*finfo;
};

struct au_finfo *au_fi_sp(struct file *file)
{
	struct au_finfo *finfo;
	struct au_finfo_sp *sp;

	finfo = NULL;
	spin_lock(&au_finfo_sp.spin);
	hlist_for_each_entry(sp, &au_finfo_sp.head, hlist) {
		if (sp->file != file)
			continue;
		finfo = sp->finfo;
		break;
	}
	spin_unlock(&au_finfo_sp.spin);

	return finfo;
}

static int au_fi_sp_add(struct file *file)
{
	int err;
	struct au_finfo_sp *sp;

	err = -ENOMEM;
	sp = kmalloc(sizeof(*sp), GFP_NOFS);
	if (sp) {
		err = 0;
		sp->file = file;
		sp->finfo = file->private_data;
		spin_lock(&au_finfo_sp.spin);
		hlist_add_head(&sp->hlist, &au_finfo_sp.head);
		spin_unlock(&au_finfo_sp.spin);
	}
	return err;
}

static void au_fi_sp_del(struct file *file)
{
	struct au_finfo_sp *sp, *do_free;

	do_free = NULL;
	spin_lock(&au_finfo_sp.spin);
	hlist_for_each_entry(sp, &au_finfo_sp.head, hlist) {
		if (sp->file != file)
			continue;
		hlist_del(&sp->hlist);
		do_free = sp;
		break;
	}
	spin_unlock(&au_finfo_sp.spin);
	kfree(do_free);
}

/* ---------------------------------------------------------------------- */

static ssize_t aufs_aio_read_sp(struct kiocb *kio, const struct iovec *iov,
				unsigned long nv, loff_t pos)
{
	ssize_t err;
	aufs_bindex_t bstart;
	unsigned char wbr;
	struct file *file, *h_file;
	struct super_block *sb;

	file = kio->ki_filp;
	sb = file->f_dentry->d_sb;
	si_read_lock(sb, AuLock_FLUSH);
	fi_read_lock(file);
	bstart = au_fbstart(file);
	h_file = au_hf_top(file);
	fi_read_unlock(file);
	wbr = !!au_br_writable(au_sbr(sb, bstart)->br_perm);
	si_read_unlock(sb);

	/* do not change the file in kio */
	AuDebugOn(!h_file->f_op || !h_file->f_op->aio_read);
	err = h_file->f_op->aio_read(kio, iov, nv, pos);
	if (err > 0 && wbr)
		file_accessed(h_file);

	return err;
}

static ssize_t aufs_aio_write_sp(struct kiocb *kio, const struct iovec *iov,
				 unsigned long nv, loff_t pos)
{
	ssize_t err;
	aufs_bindex_t bstart;
	unsigned char wbr;
	struct super_block *sb;
	struct file *file, *h_file;

	file = kio->ki_filp;
	sb = file->f_dentry->d_sb;
	si_read_lock(sb, AuLock_FLUSH);
	fi_read_lock(file);
	bstart = au_fbstart(file);
	h_file = au_hf_top(file);
	fi_read_unlock(file);
	wbr = !!au_br_writable(au_sbr(sb, bstart)->br_perm);
	si_read_unlock(sb);

	/* do not change the file in kio */
	AuDebugOn(!h_file->f_op || !h_file->f_op->aio_write);
	err = h_file->f_op->aio_write(kio, iov, nv, pos);
	return err;
}

/* ---------------------------------------------------------------------- */

static int aufs_release_sp(struct inode *inode, struct file *file)
{
	int err;
	struct file *h_file;

	fi_read_lock(file);
	h_file = au_hf_top(file);
	fi_read_unlock(file);
	/* close this fifo in aufs */
	err = h_file->f_op->release(inode, file); /* ignore */
	aufs_release_nondir(inode, file); /* ignore */
	au_fi_sp_del(file);
	return err;
}

/* ---------------------------------------------------------------------- */

/* currently, support only FIFO */
enum {
	AuSp_FIFO, AuSp_FIFO_R, AuSp_FIFO_W, AuSp_FIFO_RW,
	/* AuSp_SOCK, AuSp_CHR, AuSp_BLK, */
	AuSp_Last
};
static int aufs_open_sp(struct inode *inode, struct file *file);
static struct au_sp_fop {
	int			done;
	struct file_operations	fop;	/* not 'const' */
	spinlock_t		spin;
} au_sp_fop[AuSp_Last] = {
	[AuSp_FIFO] = {
		.fop	= {
			.owner	= THIS_MODULE,
			.open	= aufs_open_sp
		}
	}
};

static void au_init_fop_sp(struct file *file)
{
	struct au_sp_fop *p;
	int i;
	struct file *h_file;

	p = au_sp_fop;
	if (unlikely(!p->done)) {
		/* initialize first time only */
		static DEFINE_SPINLOCK(spin);

		spin_lock(&spin);
		if (!p->done) {
			BUILD_BUG_ON(sizeof(au_sp_fop)/sizeof(*au_sp_fop)
				     != AuSp_Last);
			for (i = 0; i < AuSp_Last; i++)
				spin_lock_init(&p[i].spin);
			p->done = 1;
		}
		spin_unlock(&spin);
	}

	switch (file->f_mode & (FMODE_READ | FMODE_WRITE)) {
	case FMODE_READ:
		i = AuSp_FIFO_R;
		break;
	case FMODE_WRITE:
		i = AuSp_FIFO_W;
		break;
	case FMODE_READ | FMODE_WRITE:
		i = AuSp_FIFO_RW;
		break;
	default:
		BUG();
	}

	p += i;
	if (unlikely(!p->done)) {
		/* initialize first time only */
		h_file = au_hf_top(file);
		spin_lock(&p->spin);
		if (!p->done) {
			p->fop = *h_file->f_op;
			p->fop.owner = THIS_MODULE;
			if (p->fop.aio_read)
				p->fop.aio_read = aufs_aio_read_sp;
			if (p->fop.aio_write)
				p->fop.aio_write = aufs_aio_write_sp;
			p->fop.release = aufs_release_sp;
			p->done = 1;
		}
		spin_unlock(&p->spin);
	}
	file->f_op = &p->fop;
}

static int au_cpup_sp(struct dentry *dentry)
{
	int err;
	struct au_pin pin;
	struct au_wr_dir_args wr_dir_args = {
		.force_btgt	= -1,
		.flags		= 0
	};
	struct au_cp_generic cpg = {
		.dentry	= dentry,
		.bdst	= -1,
		.bsrc	= -1,
		.len	= -1,
		.pin	= &pin,
		.flags	= AuCpup_DTIME
	};

	AuDbg("%.*s\n", AuDLNPair(dentry));

	di_read_unlock(dentry, AuLock_IR);
	di_write_lock_child(dentry);
	err = au_wr_dir(dentry, /*src_dentry*/NULL, &wr_dir_args);
	if (unlikely(err < 0))
		goto out;
	cpg.bdst = err;
	err = 0;
	if (cpg.bdst == au_dbstart(dentry))
		goto out; /* success */

	err = au_pin(&pin, dentry, cpg.bdst, au_opt_udba(dentry->d_sb),
		     AuPin_MNT_WRITE);
	if (!err) {
		err = au_sio_cpup_simple(&cpg);
		au_unpin(&pin);
	}

out:
	di_downgrade_lock(dentry, AuLock_IR);
	return err;
}

static int au_do_open_sp(struct file *file, int flags)
{
	int err;
	struct dentry *dentry;
	struct super_block *sb;
	struct file *h_file;
	struct inode *h_inode;

	err = au_fi_sp_add(file);
	if (unlikely(err))
		goto out;

	dentry = file->f_dentry;
	AuDbg("%.*s\n", AuDLNPair(dentry));

	/*
	 * try copying-up.
	 * operate on the ro branch is not an error.
	 */
	au_cpup_sp(dentry); /* ignore */

	/* prepare h_file */
	err = au_do_open_nondir(file, vfsub_file_flags(file));
	if (unlikely(err))
		goto out_del;

	sb = dentry->d_sb;
	h_file = au_hf_top(file);
	h_inode = file_inode(h_file);
	di_read_unlock(dentry, AuLock_IR);
	fi_write_unlock(file);
	si_read_unlock(sb);
	/* open this fifo in aufs */
	err = h_inode->i_fop->open(file_inode(file), file);
	si_noflush_read_lock(sb);
	fi_write_lock(file);
	di_read_lock_child(dentry, AuLock_IR);
	if (!err) {
		au_init_fop_sp(file);
		goto out; /* success */
	}

out_del:
	au_fi_sp_del(file);
out:
	return err;
}

static int aufs_open_sp(struct inode *inode, struct file *file)
{
	int err;
	struct super_block *sb;

	sb = file->f_dentry->d_sb;
	si_read_lock(sb, AuLock_FLUSH);
	err = au_do_open(file, au_do_open_sp, /*fidir*/NULL);
	si_read_unlock(sb);
	return err;
}

/* ---------------------------------------------------------------------- */

void au_init_special_fop(struct inode *inode, umode_t mode, dev_t rdev)
{
	init_special_inode(inode, mode, rdev);

	switch (mode & S_IFMT) {
	case S_IFIFO:
		inode->i_fop = &au_sp_fop[AuSp_FIFO].fop;
		/*FALLTHROUGH*/
	case S_IFCHR:
	case S_IFBLK:
	case S_IFSOCK:
		break;
	default:
		AuDebugOn(1);
	}
}

int au_special_file(umode_t mode)
{
	int ret;

	ret = 0;
	switch (mode & S_IFMT) {
	case S_IFIFO:
#if 0
	case S_IFCHR:
	case S_IFBLK:
	case S_IFSOCK:
#endif
		ret = 1;
	}

	return ret;
}
