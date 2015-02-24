/*
 * Copyright (C) 2005-2014 Junjiro R. Okajima
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
 * file and vm operations
 */

#include <linux/aio.h>
#include <linux/fs_stack.h>
#include <linux/mman.h>
#include <linux/security.h>
#include "aufs.h"

int au_do_open_nondir(struct file *file, int flags)
{
	int err;
	aufs_bindex_t bindex;
	struct file *h_file;
	struct dentry *dentry;
	struct au_finfo *finfo;

	FiMustWriteLock(file);

	err = 0;
	dentry = file->f_dentry;
	finfo = au_fi(file);
	memset(&finfo->fi_htop, 0, sizeof(finfo->fi_htop));
	atomic_set(&finfo->fi_mmapped, 0);
	bindex = au_dbstart(dentry);
	h_file = au_h_open(dentry, bindex, flags, file, /*force_wr*/0);
	if (IS_ERR(h_file))
		err = PTR_ERR(h_file);
	else {
		au_set_fbstart(file, bindex);
		au_set_h_fptr(file, bindex, h_file);
		au_update_figen(file);
		/* todo: necessary? */
		/* file->f_ra = h_file->f_ra; */
	}

	return err;
}

static int aufs_open_nondir(struct inode *inode __maybe_unused,
			    struct file *file)
{
	int err;
	struct super_block *sb;

	AuDbg("%.*s, f_flags 0x%x, f_mode 0x%x\n",
	      AuDLNPair(file->f_dentry), vfsub_file_flags(file),
	      file->f_mode);

	sb = file->f_dentry->d_sb;
	si_read_lock(sb, AuLock_FLUSH);
	err = au_do_open(file, au_do_open_nondir, /*fidir*/NULL);
	si_read_unlock(sb);
	return err;
}

int aufs_release_nondir(struct inode *inode __maybe_unused, struct file *file)
{
	struct au_finfo *finfo;
	aufs_bindex_t bindex;

	finfo = au_fi(file);
	bindex = finfo->fi_btop;
	if (bindex >= 0)
		au_set_h_fptr(file, bindex, NULL);

	au_finfo_fin(file);
	return 0;
}

/* ---------------------------------------------------------------------- */

static int au_do_flush_nondir(struct file *file, fl_owner_t id)
{
	int err;
	struct file *h_file;

	err = 0;
	h_file = au_hf_top(file);
	if (h_file)
		err = vfsub_flush(h_file, id);
	return err;
}

static int aufs_flush_nondir(struct file *file, fl_owner_t id)
{
	return au_do_flush(file, id, au_do_flush_nondir);
}

/* ---------------------------------------------------------------------- */
/*
 * read and write functions acquire [fdi]_rwsem once, but release before
 * mmap_sem. This is because to stop a race condition between mmap(2).
 * Releasing these aufs-rwsem should be safe, no branch-mamagement (by keeping
 * si_rwsem), no harmful copy-up should happen. Actually copy-up may happen in
 * read functions after [fdi]_rwsem are released, but it should be harmless.
 */

static ssize_t aufs_read(struct file *file, char __user *buf, size_t count,
			 loff_t *ppos)
{
	ssize_t err;
	struct dentry *dentry;
	struct file *h_file;
	struct super_block *sb;

	dentry = file->f_dentry;
	sb = dentry->d_sb;
	si_read_lock(sb, AuLock_FLUSH | AuLock_NOPLMW);
	err = au_reval_and_lock_fdi(file, au_reopen_nondir, /*wlock*/0);
	if (unlikely(err))
		goto out;

	h_file = au_hf_top(file);
	get_file(h_file);
	di_read_unlock(dentry, AuLock_IR);
	fi_read_unlock(file);

	/* filedata may be obsoleted by concurrent copyup, but no problem */
	err = vfsub_read_u(h_file, buf, count, ppos);
	/* todo: necessary? */
	/* file->f_ra = h_file->f_ra; */
	/* update without lock, I don't think it a problem */
	fsstack_copy_attr_atime(dentry->d_inode, file_inode(h_file));
	fput(h_file);

out:
	si_read_unlock(sb);
	return err;
}

/*
 * todo: very ugly
 * it locks both of i_mutex and si_rwsem for read in safe.
 * if the plink maintenance mode continues forever (that is the problem),
 * may loop forever.
 */
static void au_mtx_and_read_lock(struct inode *inode)
{
	int err;
	struct super_block *sb = inode->i_sb;

	while (1) {
		mutex_lock(&inode->i_mutex);
		err = si_read_lock(sb, AuLock_FLUSH | AuLock_NOPLM);
		if (!err)
			break;
		mutex_unlock(&inode->i_mutex);
		si_read_lock(sb, AuLock_NOPLMW);
		si_read_unlock(sb);
	}
}

static ssize_t aufs_write(struct file *file, const char __user *ubuf,
			  size_t count, loff_t *ppos)
{
	ssize_t err;
	blkcnt_t blks;
	aufs_bindex_t bstart;
	struct au_pin pin;
	struct dentry *dentry;
	struct inode *inode, *h_inode;
	struct super_block *sb;
	struct file *h_file;
	char __user *buf = (char __user *)ubuf;

	dentry = file->f_dentry;
	sb = dentry->d_sb;
	inode = dentry->d_inode;
	au_mtx_and_read_lock(inode);

	err = au_reval_and_lock_fdi(file, au_reopen_nondir, /*wlock*/1);
	if (unlikely(err))
		goto out;

	err = au_ready_to_write(file, -1, &pin);
	di_downgrade_lock(dentry, AuLock_IR);
	if (unlikely(err)) {
		di_read_unlock(dentry, AuLock_IR);
		fi_write_unlock(file);
		goto out;
	}

	bstart = au_fbstart(file);
	h_file = au_hf_top(file);
	get_file(h_file);
	h_inode = h_file->f_dentry->d_inode;
	blks = h_inode->i_blocks;
	au_unpin(&pin);
	di_read_unlock(dentry, AuLock_IR);
	fi_write_unlock(file);

	err = vfsub_write_u(h_file, buf, count, ppos);
	ii_write_lock_child(inode);
	au_cpup_attr_timesizes(inode);
	inode->i_mode = file_inode(h_file)->i_mode;
	AuDbg("blks %llu, %llu\n", (u64)blks, (u64)h_inode->i_blocks);
	if (err > 0)
		au_fhsm_wrote(sb, bstart, /*force*/h_inode->i_blocks > blks);
	ii_write_unlock(inode);
	fput(h_file);

out:
	si_read_unlock(sb);
	mutex_unlock(&inode->i_mutex);
	return err;
}

static ssize_t au_do_aio(struct file *h_file, int rw, struct kiocb *kio,
			 const struct iovec *iov, unsigned long nv, loff_t pos)
{
	ssize_t err;
	struct file *file;
	ssize_t (*func)(struct kiocb *, const struct iovec *, unsigned long,
			loff_t);

	err = security_file_permission(h_file, rw);
	if (unlikely(err))
		goto out;

	err = -ENOSYS;
	func = NULL;
	if (rw == MAY_READ)
		func = h_file->f_op->aio_read;
	else if (rw == MAY_WRITE)
		func = h_file->f_op->aio_write;
	if (func) {
		file = kio->ki_filp;
		kio->ki_filp = h_file;
		lockdep_off();
		err = func(kio, iov, nv, pos);
		lockdep_on();
		kio->ki_filp = file;
	} else
		/* currently there is no such fs */
		WARN_ON_ONCE(1);

out:
	return err;
}

static ssize_t aufs_aio_read(struct kiocb *kio, const struct iovec *iov,
			     unsigned long nv, loff_t pos)
{
	ssize_t err;
	struct file *file, *h_file;
	struct dentry *dentry;
	struct super_block *sb;

	file = kio->ki_filp;
	dentry = file->f_dentry;
	sb = dentry->d_sb;
	si_read_lock(sb, AuLock_FLUSH | AuLock_NOPLMW);
	err = au_reval_and_lock_fdi(file, au_reopen_nondir, /*wlock*/0);
	if (unlikely(err))
		goto out;

	h_file = au_hf_top(file);
	get_file(h_file);
	di_read_unlock(dentry, AuLock_IR);
	fi_read_unlock(file);

	err = au_do_aio(h_file, MAY_READ, kio, iov, nv, pos);
	/* todo: necessary? */
	/* file->f_ra = h_file->f_ra; */
	/* update without lock, I don't think it a problem */
	fsstack_copy_attr_atime(dentry->d_inode, file_inode(h_file));
	fput(h_file);

out:
	si_read_unlock(sb);
	return err;
}

static ssize_t aufs_aio_write(struct kiocb *kio, const struct iovec *iov,
			      unsigned long nv, loff_t pos)
{
	ssize_t err;
	blkcnt_t blks;
	aufs_bindex_t bstart;
	struct au_pin pin;
	struct dentry *dentry;
	struct inode *inode, *h_inode;
	struct file *file, *h_file;
	struct super_block *sb;

	file = kio->ki_filp;
	dentry = file->f_dentry;
	sb = dentry->d_sb;
	inode = dentry->d_inode;
	au_mtx_and_read_lock(inode);

	err = au_reval_and_lock_fdi(file, au_reopen_nondir, /*wlock*/1);
	if (unlikely(err))
		goto out;

	err = au_ready_to_write(file, -1, &pin);
	di_downgrade_lock(dentry, AuLock_IR);
	if (unlikely(err)) {
		di_read_unlock(dentry, AuLock_IR);
		fi_write_unlock(file);
		goto out;
	}

	bstart = au_fbstart(file);
	h_file = au_hf_top(file);
	get_file(h_file);
	h_inode = h_file->f_dentry->d_inode;
	blks = h_inode->i_blocks;
	au_unpin(&pin);
	di_read_unlock(dentry, AuLock_IR);
	fi_write_unlock(file);

	err = au_do_aio(h_file, MAY_WRITE, kio, iov, nv, pos);
	ii_write_lock_child(inode);
	au_cpup_attr_timesizes(inode);
	inode->i_mode = file_inode(h_file)->i_mode;
	AuDbg("blks %llu, %llu\n", (u64)blks, (u64)h_inode->i_blocks);
	if (err > 0)
		au_fhsm_wrote(sb, bstart, /*force*/h_inode->i_blocks > blks);
	ii_write_unlock(inode);
	fput(h_file);

out:
	si_read_unlock(sb);
	mutex_unlock(&inode->i_mutex);
	return err;
}

static ssize_t aufs_splice_read(struct file *file, loff_t *ppos,
				struct pipe_inode_info *pipe, size_t len,
				unsigned int flags)
{
	ssize_t err;
	struct file *h_file;
	struct dentry *dentry;
	struct super_block *sb;

	dentry = file->f_dentry;
	sb = dentry->d_sb;
	si_read_lock(sb, AuLock_FLUSH | AuLock_NOPLMW);
	err = au_reval_and_lock_fdi(file, au_reopen_nondir, /*wlock*/0);
	if (unlikely(err))
		goto out;

	err = -EINVAL;
	h_file = au_hf_top(file);
	get_file(h_file);
	if (au_test_loopback_kthread()) {
		au_warn_loopback(h_file->f_dentry->d_sb);
		if (file->f_mapping != h_file->f_mapping) {
			file->f_mapping = h_file->f_mapping;
			smp_mb(); /* unnecessary? */
		}
	}
	di_read_unlock(dentry, AuLock_IR);
	fi_read_unlock(file);

	err = vfsub_splice_to(h_file, ppos, pipe, len, flags);
	/* todo: necessasry? */
	/* file->f_ra = h_file->f_ra; */
	/* update without lock, I don't think it a problem */
	fsstack_copy_attr_atime(dentry->d_inode, file_inode(h_file));
	fput(h_file);

out:
	si_read_unlock(sb);
	return err;
}

static ssize_t
aufs_splice_write(struct pipe_inode_info *pipe, struct file *file, loff_t *ppos,
		  size_t len, unsigned int flags)
{
	ssize_t err;
	blkcnt_t blks;
	aufs_bindex_t bstart;
	struct au_pin pin;
	struct dentry *dentry;
	struct inode *inode, *h_inode;
	struct super_block *sb;
	struct file *h_file;

	dentry = file->f_dentry;
	sb = dentry->d_sb;
	inode = dentry->d_inode;
	au_mtx_and_read_lock(inode);

	err = au_reval_and_lock_fdi(file, au_reopen_nondir, /*wlock*/1);
	if (unlikely(err))
		goto out;

	err = au_ready_to_write(file, -1, &pin);
	di_downgrade_lock(dentry, AuLock_IR);
	if (unlikely(err)) {
		di_read_unlock(dentry, AuLock_IR);
		fi_write_unlock(file);
		goto out;
	}

	bstart = au_fbstart(file);
	h_file = au_hf_top(file);
	get_file(h_file);
	h_inode = h_file->f_dentry->d_inode;
	blks = h_inode->i_blocks;
	au_unpin(&pin);
	di_read_unlock(dentry, AuLock_IR);
	fi_write_unlock(file);

	err = vfsub_splice_from(pipe, h_file, ppos, len, flags);
	ii_write_lock_child(inode);
	au_cpup_attr_timesizes(inode);
	inode->i_mode = file_inode(h_file)->i_mode;
	AuDbg("blks %llu, %llu\n", (u64)blks, (u64)h_inode->i_blocks);
	if (err > 0)
		au_fhsm_wrote(sb, bstart, /*force*/h_inode->i_blocks > blks);
	ii_write_unlock(inode);
	fput(h_file);

out:
	si_read_unlock(sb);
	mutex_unlock(&inode->i_mutex);
	return err;
}

static long aufs_fallocate(struct file *file, int mode, loff_t offset,
			   loff_t len)
{
	long err;
	struct au_pin pin;
	struct dentry *dentry;
	struct super_block *sb;
	struct inode *inode;
	struct file *h_file;

	dentry = file->f_dentry;
	sb = dentry->d_sb;
	inode = dentry->d_inode;
	au_mtx_and_read_lock(inode);

	err = au_reval_and_lock_fdi(file, au_reopen_nondir, /*wlock*/1);
	if (unlikely(err))
		goto out;

	err = au_ready_to_write(file, -1, &pin);
	di_downgrade_lock(dentry, AuLock_IR);
	if (unlikely(err)) {
		di_read_unlock(dentry, AuLock_IR);
		fi_write_unlock(file);
		goto out;
	}

	h_file = au_hf_top(file);
	get_file(h_file);
	au_unpin(&pin);
	di_read_unlock(dentry, AuLock_IR);
	fi_write_unlock(file);

	lockdep_off();
	err = do_fallocate(h_file, mode, offset, len);
	lockdep_on();
	ii_write_lock_child(inode);
	au_cpup_attr_timesizes(inode);
	inode->i_mode = file_inode(h_file)->i_mode;
	ii_write_unlock(inode);
	fput(h_file);

out:
	si_read_unlock(sb);
	mutex_unlock(&inode->i_mutex);
	return err;
}

/* ---------------------------------------------------------------------- */

/*
 * The locking order around current->mmap_sem.
 * - in most and regular cases
 *   file I/O syscall -- aufs_read() or something
 *	-- si_rwsem for read -- mmap_sem
 *	(Note that [fdi]i_rwsem are released before mmap_sem).
 * - in mmap case
 *   mmap(2) -- mmap_sem -- aufs_mmap() -- si_rwsem for read -- [fdi]i_rwsem
 * This AB-BA order is definitly bad, but is not a problem since "si_rwsem for
 * read" allows muliple processes to acquire it and [fdi]i_rwsem are not held in
 * file I/O. Aufs needs to stop lockdep in aufs_mmap() though.
 * It means that when aufs acquires si_rwsem for write, the process should never
 * acquire mmap_sem.
 *
 * Actually aufs_readdir() holds [fdi]i_rwsem before mmap_sem, but this is not a
 * problem either since any directory is not able to be mmap-ed.
 * The similar scenario is applied to aufs_readlink() too.
 */

#if 0 /* stop calling security_file_mmap() */
/* cf. linux/include/linux/mman.h: calc_vm_prot_bits() */
#define AuConv_VM_PROT(f, b)	_calc_vm_trans(f, VM_##b, PROT_##b)

static unsigned long au_arch_prot_conv(unsigned long flags)
{
	/* currently ppc64 only */
#ifdef CONFIG_PPC64
	/* cf. linux/arch/powerpc/include/asm/mman.h */
	AuDebugOn(arch_calc_vm_prot_bits(-1) != VM_SAO);
	return AuConv_VM_PROT(flags, SAO);
#else
	AuDebugOn(arch_calc_vm_prot_bits(-1));
	return 0;
#endif
}

static unsigned long au_prot_conv(unsigned long flags)
{
	return AuConv_VM_PROT(flags, READ)
		| AuConv_VM_PROT(flags, WRITE)
		| AuConv_VM_PROT(flags, EXEC)
		| au_arch_prot_conv(flags);
}

/* cf. linux/include/linux/mman.h: calc_vm_flag_bits() */
#define AuConv_VM_MAP(f, b)	_calc_vm_trans(f, VM_##b, MAP_##b)

static unsigned long au_flag_conv(unsigned long flags)
{
	return AuConv_VM_MAP(flags, GROWSDOWN)
		| AuConv_VM_MAP(flags, DENYWRITE)
		| AuConv_VM_MAP(flags, LOCKED);
}
#endif

static int aufs_mmap(struct file *file, struct vm_area_struct *vma)
{
	int err;
	aufs_bindex_t bstart;
	const unsigned char wlock
		= (file->f_mode & FMODE_WRITE) && (vma->vm_flags & VM_SHARED);
	struct dentry *dentry;
	struct super_block *sb;
	struct file *h_file;
	struct au_branch *br;
	struct au_pin pin;

	AuDbgVmRegion(file, vma);

	dentry = file->f_dentry;
	sb = dentry->d_sb;
	lockdep_off();
	si_read_lock(sb, AuLock_NOPLMW);
	err = au_reval_and_lock_fdi(file, au_reopen_nondir, /*wlock*/1);
	if (unlikely(err))
		goto out;

	if (wlock) {
		err = au_ready_to_write(file, -1, &pin);
		di_write_unlock(dentry);
		if (unlikely(err)) {
			fi_write_unlock(file);
			goto out;
		}
		au_unpin(&pin);
	} else
		di_write_unlock(dentry);

	bstart = au_fbstart(file);
	br = au_sbr(sb, bstart);
	h_file = au_hf_top(file);
	get_file(h_file);
	au_set_mmapped(file);
	fi_write_unlock(file);
	lockdep_on();

	au_vm_file_reset(vma, h_file);
	/*
	 * we cannot call security_mmap_file() here since it may acquire
	 * mmap_sem or i_mutex.
	 *
	 * err = security_mmap_file(h_file, au_prot_conv(vma->vm_flags),
	 *			 au_flag_conv(vma->vm_flags));
	 */
	if (!err)
		err = h_file->f_op->mmap(h_file, vma);
	if (unlikely(err))
		goto out_reset;

	au_vm_prfile_set(vma, file);
	/* update without lock, I don't think it a problem */
	fsstack_copy_attr_atime(file_inode(file), file_inode(h_file));
	goto out_fput; /* success */

out_reset:
	au_unset_mmapped(file);
	au_vm_file_reset(vma, file);
out_fput:
	fput(h_file);
	lockdep_off();
out:
	si_read_unlock(sb);
	lockdep_on();
	AuTraceErr(err);
	return err;
}

/* ---------------------------------------------------------------------- */

static int aufs_fsync_nondir(struct file *file, loff_t start, loff_t end,
			     int datasync)
{
	int err;
	struct au_pin pin;
	struct dentry *dentry;
	struct inode *inode;
	struct file *h_file;
	struct super_block *sb;

	dentry = file->f_dentry;
	inode = dentry->d_inode;
	sb = dentry->d_sb;
	mutex_lock(&inode->i_mutex);
	err = si_read_lock(sb, AuLock_FLUSH | AuLock_NOPLM);
	if (unlikely(err))
		goto out;

	err = 0; /* -EBADF; */ /* posix? */
	if (unlikely(!(file->f_mode & FMODE_WRITE)))
		goto out_si;
	err = au_reval_and_lock_fdi(file, au_reopen_nondir, /*wlock*/1);
	if (unlikely(err))
		goto out_si;

	err = au_ready_to_write(file, -1, &pin);
	di_downgrade_lock(dentry, AuLock_IR);
	if (unlikely(err))
		goto out_unlock;
	au_unpin(&pin);

	err = -EINVAL;
	h_file = au_hf_top(file);
	err = vfsub_fsync(h_file, &h_file->f_path, datasync);
	au_cpup_attr_timesizes(inode);

out_unlock:
	di_read_unlock(dentry, AuLock_IR);
	fi_write_unlock(file);
out_si:
	si_read_unlock(sb);
out:
	mutex_unlock(&inode->i_mutex);
	return err;
}

/* no one supports this operation, currently */
#if 0
static int aufs_aio_fsync_nondir(struct kiocb *kio, int datasync)
{
	int err;
	struct au_pin pin;
	struct dentry *dentry;
	struct inode *inode;
	struct file *file, *h_file;

	file = kio->ki_filp;
	dentry = file->f_dentry;
	inode = dentry->d_inode;
	au_mtx_and_read_lock(inode);

	err = 0; /* -EBADF; */ /* posix? */
	if (unlikely(!(file->f_mode & FMODE_WRITE)))
		goto out;
	err = au_reval_and_lock_fdi(file, au_reopen_nondir, /*wlock*/1);
	if (unlikely(err))
		goto out;

	err = au_ready_to_write(file, -1, &pin);
	di_downgrade_lock(dentry, AuLock_IR);
	if (unlikely(err))
		goto out_unlock;
	au_unpin(&pin);

	err = -ENOSYS;
	h_file = au_hf_top(file);
	if (h_file->f_op && h_file->f_op->aio_fsync) {
		struct mutex *h_mtx;

		h_mtx = &file_inode(h_file)->i_mutex;
		if (!is_sync_kiocb(kio)) {
			get_file(h_file);
			fput(file);
		}
		kio->ki_filp = h_file;
		err = h_file->f_op->aio_fsync(kio, datasync);
		mutex_lock_nested(h_mtx, AuLsc_I_CHILD);
		if (!err)
			vfsub_update_h_iattr(&h_file->f_path, /*did*/NULL);
		/*ignore*/
		au_cpup_attr_timesizes(inode);
		mutex_unlock(h_mtx);
	}

out_unlock:
	di_read_unlock(dentry, AuLock_IR);
	fi_write_unlock(file);
out:
	si_read_unlock(inode->sb);
	mutex_unlock(&inode->i_mutex);
	return err;
}
#endif

static int aufs_fasync(int fd, struct file *file, int flag)
{
	int err;
	struct file *h_file;
	struct dentry *dentry;
	struct super_block *sb;

	dentry = file->f_dentry;
	sb = dentry->d_sb;
	si_read_lock(sb, AuLock_FLUSH | AuLock_NOPLMW);
	err = au_reval_and_lock_fdi(file, au_reopen_nondir, /*wlock*/0);
	if (unlikely(err))
		goto out;

	h_file = au_hf_top(file);
	if (h_file->f_op && h_file->f_op->fasync)
		err = h_file->f_op->fasync(fd, h_file, flag);

	di_read_unlock(dentry, AuLock_IR);
	fi_read_unlock(file);

out:
	si_read_unlock(sb);
	return err;
}

/* ---------------------------------------------------------------------- */

/* no one supports this operation, currently */
#if 0
static ssize_t aufs_sendpage(struct file *file, struct page *page, int offset,
			     size_t len, loff_t *pos , int more)
{
}
#endif

/* ---------------------------------------------------------------------- */

const struct file_operations aufs_file_fop = {
	.owner		= THIS_MODULE,

	.llseek		= default_llseek,

	.read		= aufs_read,
	.write		= aufs_write,
	.aio_read	= aufs_aio_read,
	.aio_write	= aufs_aio_write,
#ifdef CONFIG_AUFS_POLL
	.poll		= aufs_poll,
#endif
	.unlocked_ioctl	= aufs_ioctl_nondir,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= aufs_compat_ioctl_nondir,
#endif
	.mmap		= aufs_mmap,
	.open		= aufs_open_nondir,
	.flush		= aufs_flush_nondir,
	.release	= aufs_release_nondir,
	.fsync		= aufs_fsync_nondir,
	/* .aio_fsync	= aufs_aio_fsync_nondir, */
	.fasync		= aufs_fasync,
	/* .sendpage	= aufs_sendpage, */
	.splice_write	= aufs_splice_write,
	.splice_read	= aufs_splice_read,
#if 0
	.aio_splice_write = aufs_aio_splice_write,
	.aio_splice_read  = aufs_aio_splice_read,
#endif
	.fallocate	= aufs_fallocate
};
