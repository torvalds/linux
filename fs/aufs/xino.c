/*
 * Copyright (C) 2005-2012 Junjiro R. Okajima
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
 * external inode number translation table and bitmap
 */

#include <linux/seq_file.h>
#include "aufs.h"

/* todo: unnecessary to support mmap_sem since kernel-space? */
ssize_t xino_fread(au_readf_t func, struct file *file, void *kbuf, size_t size,
		   loff_t *pos)
{
	ssize_t err;
	mm_segment_t oldfs;
	union {
		void *k;
		char __user *u;
	} buf;

	buf.k = kbuf;
	oldfs = get_fs();
	set_fs(KERNEL_DS);
	do {
		/* todo: signal_pending? */
		err = func(file, buf.u, size, pos);
	} while (err == -EAGAIN || err == -EINTR);
	set_fs(oldfs);

#if 0 /* reserved for future use */
	if (err > 0)
		fsnotify_access(file->f_dentry);
#endif

	return err;
}

/* ---------------------------------------------------------------------- */

static ssize_t do_xino_fwrite(au_writef_t func, struct file *file, void *kbuf,
			      size_t size, loff_t *pos)
{
	ssize_t err;
	mm_segment_t oldfs;
	union {
		void *k;
		const char __user *u;
	} buf;

	buf.k = kbuf;
	oldfs = get_fs();
	set_fs(KERNEL_DS);
	do {
		/* todo: signal_pending? */
		err = func(file, buf.u, size, pos);
	} while (err == -EAGAIN || err == -EINTR);
	set_fs(oldfs);

#if 0 /* reserved for future use */
	if (err > 0)
		fsnotify_modify(file->f_dentry);
#endif

	return err;
}

struct do_xino_fwrite_args {
	ssize_t *errp;
	au_writef_t func;
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

ssize_t xino_fwrite(au_writef_t func, struct file *file, void *buf, size_t size,
		    loff_t *pos)
{
	ssize_t err;

	/* todo: signal block and no wkq? */
	if (rlimit(RLIMIT_FSIZE) == RLIM_INFINITY) {
		lockdep_off();
		err = do_xino_fwrite(func, file, buf, size, pos);
		lockdep_on();
	} else {
		/*
		 * it breaks RLIMIT_FSIZE and normal user's limit,
		 * users should care about quota and real 'filesystem full.'
		 */
		int wkq_err;
		struct do_xino_fwrite_args args = {
			.errp	= &err,
			.func	= func,
			.file	= file,
			.buf	= buf,
			.size	= size,
			.pos	= pos
		};

		wkq_err = au_wkq_wait(call_do_xino_fwrite, &args);
		if (unlikely(wkq_err))
			err = wkq_err;
	}

	return err;
}

/* ---------------------------------------------------------------------- */

/*
 * create a new xinofile at the same place/path as @base_file.
 */
struct file *au_xino_create2(struct file *base_file, struct file *copy_src)
{
	struct file *file;
	struct dentry *base, *parent;
	struct inode *dir;
	struct qstr *name;
	struct path path;
	int err;

	base = base_file->f_dentry;
	parent = base->d_parent; /* dir inode is locked */
	dir = parent->d_inode;
	IMustLock(dir);

	file = ERR_PTR(-EINVAL);
	name = &base->d_name;
	path.dentry = vfsub_lookup_one_len(name->name, parent, name->len);
	if (IS_ERR(path.dentry)) {
		file = (void *)path.dentry;
		pr_err("%.*s lookup err %ld\n",
		       AuLNPair(name), PTR_ERR(path.dentry));
		goto out;
	}

	/* no need to mnt_want_write() since we call dentry_open() later */
	err = vfs_create(dir, path.dentry, S_IRUGO | S_IWUGO, NULL);
	if (unlikely(err)) {
		file = ERR_PTR(err);
		pr_err("%.*s create err %d\n", AuLNPair(name), err);
		goto out_dput;
	}

	path.mnt = base_file->f_vfsmnt;
	file = vfsub_dentry_open(&path,
				 O_RDWR | O_CREAT | O_EXCL | O_LARGEFILE
				 /* | __FMODE_NONOTIFY */);
	if (IS_ERR(file)) {
		pr_err("%.*s open err %ld\n", AuLNPair(name), PTR_ERR(file));
		goto out_dput;
	}

	err = vfsub_unlink(dir, &file->f_path, /*force*/0);
	if (unlikely(err)) {
		pr_err("%.*s unlink err %d\n", AuLNPair(name), err);
		goto out_fput;
	}

	if (copy_src) {
		/* no one can touch copy_src xino */
		err = au_copy_file(file, copy_src,
				   i_size_read(copy_src->f_dentry->d_inode));
		if (unlikely(err)) {
			pr_err("%.*s copy err %d\n", AuLNPair(name), err);
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
	return file;
}

struct au_xino_lock_dir {
	struct au_hinode *hdir;
	struct dentry *parent;
	struct mutex *mtx;
};

static void au_xino_lock_dir(struct super_block *sb, struct file *xino,
			     struct au_xino_lock_dir *ldir)
{
	aufs_bindex_t brid, bindex;

	ldir->hdir = NULL;
	bindex = -1;
	brid = au_xino_brid(sb);
	if (brid >= 0)
		bindex = au_br_index(sb, brid);
	if (bindex >= 0) {
		ldir->hdir = au_hi(sb->s_root->d_inode, bindex);
		au_hn_imtx_lock_nested(ldir->hdir, AuLsc_I_PARENT);
	} else {
		ldir->parent = dget_parent(xino->f_dentry);
		ldir->mtx = &ldir->parent->d_inode->i_mutex;
		mutex_lock_nested(ldir->mtx, AuLsc_I_PARENT);
	}
}

static void au_xino_unlock_dir(struct au_xino_lock_dir *ldir)
{
	if (ldir->hdir)
		au_hn_imtx_unlock(ldir->hdir);
	else {
		mutex_unlock(ldir->mtx);
		dput(ldir->parent);
	}
}

/* ---------------------------------------------------------------------- */

/* trucate xino files asynchronously */

int au_xino_trunc(struct super_block *sb, aufs_bindex_t bindex)
{
	int err;
	aufs_bindex_t bi, bend;
	struct au_branch *br;
	struct file *new_xino, *file;
	struct super_block *h_sb;
	struct au_xino_lock_dir ldir;

	err = -EINVAL;
	bend = au_sbend(sb);
	if (unlikely(bindex < 0 || bend < bindex))
		goto out;
	br = au_sbr(sb, bindex);
	file = br->br_xino.xi_file;
	if (!file)
		goto out;

	au_xino_lock_dir(sb, file, &ldir);
	/* mnt_want_write() is unnecessary here */
	new_xino = au_xino_create2(file, file);
	au_xino_unlock_dir(&ldir);
	err = PTR_ERR(new_xino);
	if (IS_ERR(new_xino))
		goto out;
	err = 0;
	fput(file);
	br->br_xino.xi_file = new_xino;

	h_sb = br->br_mnt->mnt_sb;
	for (bi = 0; bi <= bend; bi++) {
		if (unlikely(bi == bindex))
			continue;
		br = au_sbr(sb, bi);
		if (br->br_mnt->mnt_sb != h_sb)
			continue;

		fput(br->br_xino.xi_file);
		br->br_xino.xi_file = new_xino;
		get_file(new_xino);
	}

out:
	return err;
}

struct xino_do_trunc_args {
	struct super_block *sb;
	struct au_branch *br;
};

static void xino_do_trunc(void *_args)
{
	struct xino_do_trunc_args *args = _args;
	struct super_block *sb;
	struct au_branch *br;
	struct inode *dir;
	int err;
	aufs_bindex_t bindex;

	err = 0;
	sb = args->sb;
	dir = sb->s_root->d_inode;
	br = args->br;

	si_noflush_write_lock(sb);
	ii_read_lock_parent(dir);
	bindex = au_br_index(sb, br->br_id);
	err = au_xino_trunc(sb, bindex);
	if (!err
	    && br->br_xino.xi_file->f_dentry->d_inode->i_blocks
	    >= br->br_xino_upper)
		br->br_xino_upper += AUFS_XINO_TRUNC_STEP;

	ii_read_unlock(dir);
	if (unlikely(err))
		pr_warning("err b%d, (%d)\n", bindex, err);
	atomic_dec(&br->br_xino_running);
	atomic_dec(&br->br_count);
	si_write_unlock(sb);
	au_nwt_done(&au_sbi(sb)->si_nowait);
	kfree(args);
}

static void xino_try_trunc(struct super_block *sb, struct au_branch *br)
{
	struct xino_do_trunc_args *args;
	int wkq_err;

	if (br->br_xino.xi_file->f_dentry->d_inode->i_blocks
	    < br->br_xino_upper)
		return;

	if (atomic_inc_return(&br->br_xino_running) > 1)
		goto out;

	/* lock and kfree() will be called in trunc_xino() */
	args = kmalloc(sizeof(*args), GFP_NOFS);
	if (unlikely(!args)) {
		AuErr1("no memory\n");
		goto out_args;
	}

	atomic_inc(&br->br_count);
	args->sb = sb;
	args->br = br;
	wkq_err = au_wkq_nowait(xino_do_trunc, args, sb, /*flags*/0);
	if (!wkq_err)
		return; /* success */

	pr_err("wkq %d\n", wkq_err);
	atomic_dec(&br->br_count);

out_args:
	kfree(args);
out:
	atomic_dec(&br->br_xino_running);
}

/* ---------------------------------------------------------------------- */

static int au_xino_do_write(au_writef_t write, struct file *file,
			    ino_t h_ino, ino_t ino)
{
	loff_t pos;
	ssize_t sz;

	pos = h_ino;
	if (unlikely(au_loff_max / sizeof(ino) - 1 < pos)) {
		AuIOErr1("too large hi%lu\n", (unsigned long)h_ino);
		return -EFBIG;
	}
	pos *= sizeof(ino);
	sz = xino_fwrite(write, file, &ino, sizeof(ino), &pos);
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
	struct au_branch *br;

	BUILD_BUG_ON(sizeof(long long) != sizeof(au_loff_max)
		     || ((loff_t)-1) > 0);
	SiMustAnyLock(sb);

	mnt_flags = au_mntflags(sb);
	if (!au_opt_test(mnt_flags, XINO))
		return 0;

	br = au_sbr(sb, bindex);
	err = au_xino_do_write(au_sbi(sb)->si_xwrite, br->br_xino.xi_file,
			       h_ino, ino);
	if (!err) {
		if (au_opt_test(mnt_flags, TRUNC_XINO)
		    && au_test_fs_trunc_xino(br->br_mnt->mnt_sb))
			xino_try_trunc(sb, br);
		return 0; /* success */
	}

	AuIOErr("write failed (%d)\n", err);
	return -EIO;
}

/* ---------------------------------------------------------------------- */

/* aufs inode number bitmap */

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
	if (i_size_read(xib->f_dentry->d_inode) >= pos + PAGE_SIZE)
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

/* ---------------------------------------------------------------------- */

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

/* for s_op->delete_inode() */
void au_xino_delete_inode(struct inode *inode, const int unlinked)
{
	int err;
	unsigned int mnt_flags;
	aufs_bindex_t bindex, bend, bi;
	unsigned char try_trunc;
	struct au_iinfo *iinfo;
	struct super_block *sb;
	struct au_hinode *hi;
	struct inode *h_inode;
	struct au_branch *br;
	au_writef_t xwrite;

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
	if (!iinfo)
		return;

	bindex = iinfo->ii_bstart;
	if (bindex < 0)
		return;

	xwrite = au_sbi(sb)->si_xwrite;
	try_trunc = !!au_opt_test(mnt_flags, TRUNC_XINO);
	hi = iinfo->ii_hinode + bindex;
	bend = iinfo->ii_bend;
	for (; bindex <= bend; bindex++, hi++) {
		h_inode = hi->hi_inode;
		if (!h_inode
		    || (!unlinked && h_inode->i_nlink))
			continue;

		/* inode may not be revalidated */
		bi = au_br_index(sb, hi->hi_id);
		if (bi < 0)
			continue;

		br = au_sbr(sb, bi);
		err = au_xino_do_write(xwrite, br->br_xino.xi_file,
				       h_inode->i_ino, /*ino*/0);
		if (!err && try_trunc
		    && au_test_fs_trunc_xino(br->br_mnt->mnt_sb))
			xino_try_trunc(sb, br);
	}
}

/* get an unused inode number from bitmap */
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
	pend = i_size_read(file->f_dentry->d_inode) / PAGE_SIZE;
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

/*
 * read @ino from xinofile for the specified branch{@sb, @bindex}
 * at the position of @h_ino.
 * if @ino does not exist and @do_new is true, get new one.
 */
int au_xino_read(struct super_block *sb, aufs_bindex_t bindex, ino_t h_ino,
		 ino_t *ino)
{
	int err;
	ssize_t sz;
	loff_t pos;
	struct file *file;
	struct au_sbinfo *sbinfo;

	*ino = 0;
	if (!au_opt_test(au_mntflags(sb), XINO))
		return 0; /* no xino */

	err = 0;
	sbinfo = au_sbi(sb);
	pos = h_ino;
	if (unlikely(au_loff_max / sizeof(*ino) - 1 < pos)) {
		AuIOErr1("too large hi%lu\n", (unsigned long)h_ino);
		return -EFBIG;
	}
	pos *= sizeof(*ino);

	file = au_sbr(sb, bindex)->br_xino.xi_file;
	if (i_size_read(file->f_dentry->d_inode) < pos + sizeof(*ino))
		return 0; /* no ino */

	sz = xino_fread(sbinfo->si_xread, file, ino, sizeof(*ino), &pos);
	if (sz == sizeof(*ino))
		return 0; /* success */

	err = sz;
	if (unlikely(sz >= 0)) {
		err = -EIO;
		AuIOErr("xino read error (%zd)\n", sz);
	}

	return err;
}

/* ---------------------------------------------------------------------- */

/* create and set a new xino file */

struct file *au_xino_create(struct super_block *sb, char *fname, int silent)
{
	struct file *file;
	struct dentry *h_parent, *d;
	struct inode *h_dir;
	int err;

	/*
	 * at mount-time, and the xino file is the default path,
	 * hnotify is disabled so we have no notify events to ignore.
	 * when a user specified the xino, we cannot get au_hdir to be ignored.
	 */
	file = vfsub_filp_open(fname, O_RDWR | O_CREAT | O_EXCL | O_LARGEFILE
			       /* | __FMODE_NONOTIFY */,
			       S_IRUGO | S_IWUGO);
	if (IS_ERR(file)) {
		if (!silent)
			pr_err("open %s(%ld)\n", fname, PTR_ERR(file));
		return file;
	}

	/* keep file count */
	h_parent = dget_parent(file->f_dentry);
	h_dir = h_parent->d_inode;
	mutex_lock_nested(&h_dir->i_mutex, AuLsc_I_PARENT);
	/* mnt_want_write() is unnecessary here */
	err = vfsub_unlink(h_dir, &file->f_path, /*force*/0);
	mutex_unlock(&h_dir->i_mutex);
	dput(h_parent);
	if (unlikely(err)) {
		if (!silent)
			pr_err("unlink %s(%d)\n", fname, err);
		goto out;
	}

	err = -EINVAL;
	d = file->f_dentry;
	if (unlikely(sb == d->d_sb)) {
		if (!silent)
			pr_err("%s must be outside\n", fname);
		goto out;
	}
	if (unlikely(au_test_fs_bad_xino(d->d_sb))) {
		if (!silent)
			pr_err("xino doesn't support %s(%s)\n",
			       fname, au_sbtype(d->d_sb));
		goto out;
	}
	return file; /* success */

out:
	fput(file);
	file = ERR_PTR(err);
	return file;
}

/*
 * find another branch who is on the same filesystem of the specified
 * branch{@btgt}. search until @bend.
 */
static int is_sb_shared(struct super_block *sb, aufs_bindex_t btgt,
			aufs_bindex_t bend)
{
	aufs_bindex_t bindex;
	struct super_block *tgt_sb = au_sbr_sb(sb, btgt);

	for (bindex = 0; bindex < btgt; bindex++)
		if (unlikely(tgt_sb == au_sbr_sb(sb, bindex)))
			return bindex;
	for (bindex++; bindex <= bend; bindex++)
		if (unlikely(tgt_sb == au_sbr_sb(sb, bindex)))
			return bindex;
	return -1;
}

/* ---------------------------------------------------------------------- */

/*
 * initialize the xinofile for the specified branch @br
 * at the place/path where @base_file indicates.
 * test whether another branch is on the same filesystem or not,
 * if @do_test is true.
 */
int au_xino_br(struct super_block *sb, struct au_branch *br, ino_t h_ino,
	       struct file *base_file, int do_test)
{
	int err;
	ino_t ino;
	aufs_bindex_t bend, bindex;
	struct au_branch *shared_br, *b;
	struct file *file;
	struct super_block *tgt_sb;

	shared_br = NULL;
	bend = au_sbend(sb);
	if (do_test) {
		tgt_sb = br->br_mnt->mnt_sb;
		for (bindex = 0; bindex <= bend; bindex++) {
			b = au_sbr(sb, bindex);
			if (tgt_sb == b->br_mnt->mnt_sb) {
				shared_br = b;
				break;
			}
		}
	}

	if (!shared_br || !shared_br->br_xino.xi_file) {
		struct au_xino_lock_dir ldir;

		au_xino_lock_dir(sb, base_file, &ldir);
		/* mnt_want_write() is unnecessary here */
		file = au_xino_create2(base_file, NULL);
		au_xino_unlock_dir(&ldir);
		err = PTR_ERR(file);
		if (IS_ERR(file))
			goto out;
		br->br_xino.xi_file = file;
	} else {
		br->br_xino.xi_file = shared_br->br_xino.xi_file;
		get_file(br->br_xino.xi_file);
	}

	ino = AUFS_ROOT_INO;
	err = au_xino_do_write(au_sbi(sb)->si_xwrite, br->br_xino.xi_file,
			       h_ino, ino);
	if (unlikely(err)) {
		fput(br->br_xino.xi_file);
		br->br_xino.xi_file = NULL;
	}

out:
	return err;
}

/* ---------------------------------------------------------------------- */

/* trucate a xino bitmap file */

/* todo: slow */
static int do_xib_restore(struct super_block *sb, struct file *file, void *page)
{
	int err, bit;
	ssize_t sz;
	unsigned long pindex;
	loff_t pos, pend;
	struct au_sbinfo *sbinfo;
	au_readf_t func;
	ino_t *ino;
	unsigned long *p;

	err = 0;
	sbinfo = au_sbi(sb);
	MtxMustLock(&sbinfo->si_xib_mtx);
	p = sbinfo->si_xib_buf;
	func = sbinfo->si_xread;
	pend = i_size_read(file->f_dentry->d_inode);
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
	int err;
	aufs_bindex_t bindex, bend;
	void *page;

	err = -ENOMEM;
	page = (void *)__get_free_page(GFP_NOFS);
	if (unlikely(!page))
		goto out;

	err = 0;
	bend = au_sbend(sb);
	for (bindex = 0; !err && bindex <= bend; bindex++)
		if (!bindex || is_sb_shared(sb, bindex, bindex - 1) < 0)
			err = do_xib_restore
				(sb, au_sbr(sb, bindex)->br_xino.xi_file, page);
		else
			AuDbg("b%d\n", bindex);
	free_page((unsigned long)page);

out:
	return err;
}

int au_xib_trunc(struct super_block *sb)
{
	int err;
	ssize_t sz;
	loff_t pos;
	struct au_xino_lock_dir ldir;
	struct au_sbinfo *sbinfo;
	unsigned long *p;
	struct file *file;

	SiMustWriteLock(sb);

	err = 0;
	sbinfo = au_sbi(sb);
	if (!au_opt_test(sbinfo->si_mntflags, XINO))
		goto out;

	file = sbinfo->si_xib;
	if (i_size_read(file->f_dentry->d_inode) <= PAGE_SIZE)
		goto out;

	au_xino_lock_dir(sb, file, &ldir);
	/* mnt_want_write() is unnecessary here */
	file = au_xino_create2(sbinfo->si_xib, NULL);
	au_xino_unlock_dir(&ldir);
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

/*
 * xino mount option handlers
 */
static au_readf_t find_readf(struct file *h_file)
{
	const struct file_operations *fop = h_file->f_op;

	if (fop) {
		if (fop->read)
			return fop->read;
		if (fop->aio_read)
			return do_sync_read;
	}
	return ERR_PTR(-ENOSYS);
}

static au_writef_t find_writef(struct file *h_file)
{
	const struct file_operations *fop = h_file->f_op;

	if (fop) {
		if (fop->write)
			return fop->write;
		if (fop->aio_write)
			return do_sync_write;
	}
	return ERR_PTR(-ENOSYS);
}

/* xino bitmap */
static void xino_clear_xib(struct super_block *sb)
{
	struct au_sbinfo *sbinfo;

	SiMustWriteLock(sb);

	sbinfo = au_sbi(sb);
	sbinfo->si_xread = NULL;
	sbinfo->si_xwrite = NULL;
	if (sbinfo->si_xib)
		fput(sbinfo->si_xib);
	sbinfo->si_xib = NULL;
	free_page((unsigned long)sbinfo->si_xib_buf);
	sbinfo->si_xib_buf = NULL;
}

static int au_xino_set_xib(struct super_block *sb, struct file *base)
{
	int err;
	loff_t pos;
	struct au_sbinfo *sbinfo;
	struct file *file;

	SiMustWriteLock(sb);

	sbinfo = au_sbi(sb);
	file = au_xino_create2(base, sbinfo->si_xib);
	err = PTR_ERR(file);
	if (IS_ERR(file))
		goto out;
	if (sbinfo->si_xib)
		fput(sbinfo->si_xib);
	sbinfo->si_xib = file;
	sbinfo->si_xread = find_readf(file);
	sbinfo->si_xwrite = find_writef(file);

	err = -ENOMEM;
	if (!sbinfo->si_xib_buf)
		sbinfo->si_xib_buf = (void *)get_zeroed_page(GFP_NOFS);
	if (unlikely(!sbinfo->si_xib_buf))
		goto out_unset;

	sbinfo->si_xib_last_pindex = 0;
	sbinfo->si_xib_next_bit = 0;
	if (i_size_read(file->f_dentry->d_inode) < PAGE_SIZE) {
		pos = 0;
		err = xino_fwrite(sbinfo->si_xwrite, file, sbinfo->si_xib_buf,
				  PAGE_SIZE, &pos);
		if (unlikely(err != PAGE_SIZE))
			goto out_free;
	}
	err = 0;
	goto out; /* success */

out_free:
	free_page((unsigned long)sbinfo->si_xib_buf);
	sbinfo->si_xib_buf = NULL;
	if (err >= 0)
		err = -EIO;
out_unset:
	fput(sbinfo->si_xib);
	sbinfo->si_xib = NULL;
	sbinfo->si_xread = NULL;
	sbinfo->si_xwrite = NULL;
out:
	return err;
}

/* xino for each branch */
static void xino_clear_br(struct super_block *sb)
{
	aufs_bindex_t bindex, bend;
	struct au_branch *br;

	bend = au_sbend(sb);
	for (bindex = 0; bindex <= bend; bindex++) {
		br = au_sbr(sb, bindex);
		if (!br || !br->br_xino.xi_file)
			continue;

		fput(br->br_xino.xi_file);
		br->br_xino.xi_file = NULL;
	}
}

static int au_xino_set_br(struct super_block *sb, struct file *base)
{
	int err;
	ino_t ino;
	aufs_bindex_t bindex, bend, bshared;
	struct {
		struct file *old, *new;
	} *fpair, *p;
	struct au_branch *br;
	struct inode *inode;
	au_writef_t writef;

	SiMustWriteLock(sb);

	err = -ENOMEM;
	bend = au_sbend(sb);
	fpair = kcalloc(bend + 1, sizeof(*fpair), GFP_NOFS);
	if (unlikely(!fpair))
		goto out;

	inode = sb->s_root->d_inode;
	ino = AUFS_ROOT_INO;
	writef = au_sbi(sb)->si_xwrite;
	for (bindex = 0, p = fpair; bindex <= bend; bindex++, p++) {
		br = au_sbr(sb, bindex);
		bshared = is_sb_shared(sb, bindex, bindex - 1);
		if (bshared >= 0) {
			/* shared xino */
			*p = fpair[bshared];
			get_file(p->new);
		}

		if (!p->new) {
			/* new xino */
			p->old = br->br_xino.xi_file;
			p->new = au_xino_create2(base, br->br_xino.xi_file);
			err = PTR_ERR(p->new);
			if (IS_ERR(p->new)) {
				p->new = NULL;
				goto out_pair;
			}
		}

		err = au_xino_do_write(writef, p->new,
				       au_h_iptr(inode, bindex)->i_ino, ino);
		if (unlikely(err))
			goto out_pair;
	}

	for (bindex = 0, p = fpair; bindex <= bend; bindex++, p++) {
		br = au_sbr(sb, bindex);
		if (br->br_xino.xi_file)
			fput(br->br_xino.xi_file);
		get_file(p->new);
		br->br_xino.xi_file = p->new;
	}

out_pair:
	for (bindex = 0, p = fpair; bindex <= bend; bindex++, p++)
		if (p->new)
			fput(p->new);
		else
			break;
	kfree(fpair);
out:
	return err;
}

void au_xino_clr(struct super_block *sb)
{
	struct au_sbinfo *sbinfo;

	au_xigen_clr(sb);
	xino_clear_xib(sb);
	xino_clear_br(sb);
	sbinfo = au_sbi(sb);
	/* lvalue, do not call au_mntflags() */
	au_opt_clr(sbinfo->si_mntflags, XINO);
}

int au_xino_set(struct super_block *sb, struct au_opt_xino *xino, int remount)
{
	int err, skip;
	struct dentry *parent, *cur_parent;
	struct qstr *dname, *cur_name;
	struct file *cur_xino;
	struct inode *dir;
	struct au_sbinfo *sbinfo;

	SiMustWriteLock(sb);

	err = 0;
	sbinfo = au_sbi(sb);
	parent = dget_parent(xino->file->f_dentry);
	if (remount) {
		skip = 0;
		dname = &xino->file->f_dentry->d_name;
		cur_xino = sbinfo->si_xib;
		if (cur_xino) {
			cur_parent = dget_parent(cur_xino->f_dentry);
			cur_name = &cur_xino->f_dentry->d_name;
			skip = (cur_parent == parent
				&& dname->len == cur_name->len
				&& !memcmp(dname->name, cur_name->name,
					   dname->len));
			dput(cur_parent);
		}
		if (skip)
			goto out;
	}

	au_opt_set(sbinfo->si_mntflags, XINO);
	dir = parent->d_inode;
	mutex_lock_nested(&dir->i_mutex, AuLsc_I_PARENT);
	/* mnt_want_write() is unnecessary here */
	err = au_xino_set_xib(sb, xino->file);
	if (!err)
		err = au_xigen_set(sb, xino->file);
	if (!err)
		err = au_xino_set_br(sb, xino->file);
	mutex_unlock(&dir->i_mutex);
	if (!err)
		goto out; /* success */

	/* reset all */
	AuIOErr("failed creating xino(%d).\n", err);

out:
	dput(parent);
	return err;
}

/* ---------------------------------------------------------------------- */

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
	aufs_bindex_t bend, bindex, bwr;

	br = NULL;
	bend = au_sbend(sb);
	bwr = -1;
	for (bindex = 0; bindex <= bend; bindex++) {
		br = au_sbr(sb, bindex);
		if (au_br_writable(br->br_perm)
		    && !au_test_fs_bad_xino(br->br_mnt->mnt_sb)) {
			bwr = bindex;
			break;
		}
	}

	if (bwr >= 0) {
		file = ERR_PTR(-ENOMEM);
		page = __getname_gfp(GFP_NOFS);
		if (unlikely(!page))
			goto out;
		path.mnt = br->br_mnt;
		path.dentry = au_h_dptr(sb->s_root, bwr);
		p = d_path(&path, page, PATH_MAX - sizeof(AUFS_XINO_FNAME));
		file = (void *)p;
		if (!IS_ERR(p)) {
			strcat(p, "/" AUFS_XINO_FNAME);
			AuDbg("%s\n", p);
			file = au_xino_create(sb, p, /*silent*/0);
			if (!IS_ERR(file))
				au_xino_brid_set(sb, br->br_id);
		}
		__putname(page);
	} else {
		file = au_xino_create(sb, AUFS_XINO_DEFPATH, /*silent*/0);
		if (IS_ERR(file))
			goto out;
		h_sb = file->f_dentry->d_sb;
		if (unlikely(au_test_fs_bad_xino(h_sb))) {
			pr_err("xino doesn't support %s(%s)\n",
			       AUFS_XINO_DEFPATH, au_sbtype(h_sb));
			fput(file);
			file = ERR_PTR(-EINVAL);
		}
		if (!IS_ERR(file))
			au_xino_brid_set(sb, -1);
	}

out:
	return file;
}

/* ---------------------------------------------------------------------- */

int au_xino_path(struct seq_file *seq, struct file *file)
{
	int err;

	err = au_seq_path(seq, &file->f_path);
	if (unlikely(err < 0))
		goto out;

	err = 0;
#define Deleted "\\040(deleted)"
	seq->count -= sizeof(Deleted) - 1;
	AuDebugOn(memcmp(seq->buf + seq->count, Deleted,
			 sizeof(Deleted) - 1));
#undef Deleted

out:
	return err;
}
