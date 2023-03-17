// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 1991, 1992  Linus Torvalds
 * Copyright (C) 2001  Andrea Arcangeli <andrea@suse.de> SuSE
 * Copyright (C) 2016 - 2020 Christoph Hellwig
 */
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/uio.h>
#include <linux/namei.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/falloc.h>
#include <linux/suspend.h>
#include <linux/fs.h>
#include <linux/module.h>
#include "blk.h"

static inline struct inode *bdev_file_inode(struct file *file)
{
	return file->f_mapping->host;
}

static int blkdev_get_block(struct inode *inode, sector_t iblock,
		struct buffer_head *bh, int create)
{
	bh->b_bdev = I_BDEV(inode);
	bh->b_blocknr = iblock;
	set_buffer_mapped(bh);
	return 0;
}

static blk_opf_t dio_bio_write_op(struct kiocb *iocb)
{
	blk_opf_t opf = REQ_OP_WRITE | REQ_SYNC | REQ_IDLE;

	/* avoid the need for a I/O completion work item */
	if (iocb_is_dsync(iocb))
		opf |= REQ_FUA;
	return opf;
}

static bool blkdev_dio_unaligned(struct block_device *bdev, loff_t pos,
			      struct iov_iter *iter)
{
	return pos & (bdev_logical_block_size(bdev) - 1) ||
		!bdev_iter_is_aligned(bdev, iter);
}

#define DIO_INLINE_BIO_VECS 4

static ssize_t __blkdev_direct_IO_simple(struct kiocb *iocb,
		struct iov_iter *iter, unsigned int nr_pages)
{
	struct block_device *bdev = iocb->ki_filp->private_data;
	struct bio_vec inline_vecs[DIO_INLINE_BIO_VECS], *vecs;
	loff_t pos = iocb->ki_pos;
	bool should_dirty = false;
	struct bio bio;
	ssize_t ret;

	if (blkdev_dio_unaligned(bdev, pos, iter))
		return -EINVAL;

	if (nr_pages <= DIO_INLINE_BIO_VECS)
		vecs = inline_vecs;
	else {
		vecs = kmalloc_array(nr_pages, sizeof(struct bio_vec),
				     GFP_KERNEL);
		if (!vecs)
			return -ENOMEM;
	}

	if (iov_iter_rw(iter) == READ) {
		bio_init(&bio, bdev, vecs, nr_pages, REQ_OP_READ);
		if (user_backed_iter(iter))
			should_dirty = true;
	} else {
		bio_init(&bio, bdev, vecs, nr_pages, dio_bio_write_op(iocb));
	}
	bio.bi_iter.bi_sector = pos >> SECTOR_SHIFT;
	bio.bi_ioprio = iocb->ki_ioprio;

	ret = bio_iov_iter_get_pages(&bio, iter);
	if (unlikely(ret))
		goto out;
	ret = bio.bi_iter.bi_size;

	if (iov_iter_rw(iter) == WRITE)
		task_io_account_write(ret);

	if (iocb->ki_flags & IOCB_NOWAIT)
		bio.bi_opf |= REQ_NOWAIT;

	submit_bio_wait(&bio);

	bio_release_pages(&bio, should_dirty);
	if (unlikely(bio.bi_status))
		ret = blk_status_to_errno(bio.bi_status);

out:
	if (vecs != inline_vecs)
		kfree(vecs);

	bio_uninit(&bio);

	return ret;
}

enum {
	DIO_SHOULD_DIRTY	= 1,
	DIO_IS_SYNC		= 2,
};

struct blkdev_dio {
	union {
		struct kiocb		*iocb;
		struct task_struct	*waiter;
	};
	size_t			size;
	atomic_t		ref;
	unsigned int		flags;
	struct bio		bio ____cacheline_aligned_in_smp;
};

static struct bio_set blkdev_dio_pool;

static void blkdev_bio_end_io(struct bio *bio)
{
	struct blkdev_dio *dio = bio->bi_private;
	bool should_dirty = dio->flags & DIO_SHOULD_DIRTY;

	if (bio->bi_status && !dio->bio.bi_status)
		dio->bio.bi_status = bio->bi_status;

	if (atomic_dec_and_test(&dio->ref)) {
		if (!(dio->flags & DIO_IS_SYNC)) {
			struct kiocb *iocb = dio->iocb;
			ssize_t ret;

			WRITE_ONCE(iocb->private, NULL);

			if (likely(!dio->bio.bi_status)) {
				ret = dio->size;
				iocb->ki_pos += ret;
			} else {
				ret = blk_status_to_errno(dio->bio.bi_status);
			}

			dio->iocb->ki_complete(iocb, ret);
			bio_put(&dio->bio);
		} else {
			struct task_struct *waiter = dio->waiter;

			WRITE_ONCE(dio->waiter, NULL);
			blk_wake_io_task(waiter);
		}
	}

	if (should_dirty) {
		bio_check_pages_dirty(bio);
	} else {
		bio_release_pages(bio, false);
		bio_put(bio);
	}
}

static ssize_t __blkdev_direct_IO(struct kiocb *iocb, struct iov_iter *iter,
		unsigned int nr_pages)
{
	struct block_device *bdev = iocb->ki_filp->private_data;
	struct blk_plug plug;
	struct blkdev_dio *dio;
	struct bio *bio;
	bool is_read = (iov_iter_rw(iter) == READ), is_sync;
	blk_opf_t opf = is_read ? REQ_OP_READ : dio_bio_write_op(iocb);
	loff_t pos = iocb->ki_pos;
	int ret = 0;

	if (blkdev_dio_unaligned(bdev, pos, iter))
		return -EINVAL;

	if (iocb->ki_flags & IOCB_ALLOC_CACHE)
		opf |= REQ_ALLOC_CACHE;
	bio = bio_alloc_bioset(bdev, nr_pages, opf, GFP_KERNEL,
			       &blkdev_dio_pool);
	dio = container_of(bio, struct blkdev_dio, bio);
	atomic_set(&dio->ref, 1);
	/*
	 * Grab an extra reference to ensure the dio structure which is embedded
	 * into the first bio stays around.
	 */
	bio_get(bio);

	is_sync = is_sync_kiocb(iocb);
	if (is_sync) {
		dio->flags = DIO_IS_SYNC;
		dio->waiter = current;
	} else {
		dio->flags = 0;
		dio->iocb = iocb;
	}

	dio->size = 0;
	if (is_read && user_backed_iter(iter))
		dio->flags |= DIO_SHOULD_DIRTY;

	blk_start_plug(&plug);

	for (;;) {
		bio->bi_iter.bi_sector = pos >> SECTOR_SHIFT;
		bio->bi_private = dio;
		bio->bi_end_io = blkdev_bio_end_io;
		bio->bi_ioprio = iocb->ki_ioprio;

		ret = bio_iov_iter_get_pages(bio, iter);
		if (unlikely(ret)) {
			bio->bi_status = BLK_STS_IOERR;
			bio_endio(bio);
			break;
		}
		if (iocb->ki_flags & IOCB_NOWAIT) {
			/*
			 * This is nonblocking IO, and we need to allocate
			 * another bio if we have data left to map. As we
			 * cannot guarantee that one of the sub bios will not
			 * fail getting issued FOR NOWAIT and as error results
			 * are coalesced across all of them, be safe and ask for
			 * a retry of this from blocking context.
			 */
			if (unlikely(iov_iter_count(iter))) {
				bio_release_pages(bio, false);
				bio_clear_flag(bio, BIO_REFFED);
				bio_put(bio);
				blk_finish_plug(&plug);
				return -EAGAIN;
			}
			bio->bi_opf |= REQ_NOWAIT;
		}

		if (is_read) {
			if (dio->flags & DIO_SHOULD_DIRTY)
				bio_set_pages_dirty(bio);
		} else {
			task_io_account_write(bio->bi_iter.bi_size);
		}
		dio->size += bio->bi_iter.bi_size;
		pos += bio->bi_iter.bi_size;

		nr_pages = bio_iov_vecs_to_alloc(iter, BIO_MAX_VECS);
		if (!nr_pages) {
			submit_bio(bio);
			break;
		}
		atomic_inc(&dio->ref);
		submit_bio(bio);
		bio = bio_alloc(bdev, nr_pages, opf, GFP_KERNEL);
	}

	blk_finish_plug(&plug);

	if (!is_sync)
		return -EIOCBQUEUED;

	for (;;) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (!READ_ONCE(dio->waiter))
			break;
		blk_io_schedule();
	}
	__set_current_state(TASK_RUNNING);

	if (!ret)
		ret = blk_status_to_errno(dio->bio.bi_status);
	if (likely(!ret))
		ret = dio->size;

	bio_put(&dio->bio);
	return ret;
}

static void blkdev_bio_end_io_async(struct bio *bio)
{
	struct blkdev_dio *dio = container_of(bio, struct blkdev_dio, bio);
	struct kiocb *iocb = dio->iocb;
	ssize_t ret;

	WRITE_ONCE(iocb->private, NULL);

	if (likely(!bio->bi_status)) {
		ret = dio->size;
		iocb->ki_pos += ret;
	} else {
		ret = blk_status_to_errno(bio->bi_status);
	}

	iocb->ki_complete(iocb, ret);

	if (dio->flags & DIO_SHOULD_DIRTY) {
		bio_check_pages_dirty(bio);
	} else {
		bio_release_pages(bio, false);
		bio_put(bio);
	}
}

static ssize_t __blkdev_direct_IO_async(struct kiocb *iocb,
					struct iov_iter *iter,
					unsigned int nr_pages)
{
	struct block_device *bdev = iocb->ki_filp->private_data;
	bool is_read = iov_iter_rw(iter) == READ;
	blk_opf_t opf = is_read ? REQ_OP_READ : dio_bio_write_op(iocb);
	struct blkdev_dio *dio;
	struct bio *bio;
	loff_t pos = iocb->ki_pos;
	int ret = 0;

	if (blkdev_dio_unaligned(bdev, pos, iter))
		return -EINVAL;

	if (iocb->ki_flags & IOCB_ALLOC_CACHE)
		opf |= REQ_ALLOC_CACHE;
	bio = bio_alloc_bioset(bdev, nr_pages, opf, GFP_KERNEL,
			       &blkdev_dio_pool);
	dio = container_of(bio, struct blkdev_dio, bio);
	dio->flags = 0;
	dio->iocb = iocb;
	bio->bi_iter.bi_sector = pos >> SECTOR_SHIFT;
	bio->bi_end_io = blkdev_bio_end_io_async;
	bio->bi_ioprio = iocb->ki_ioprio;

	if (iov_iter_is_bvec(iter)) {
		/*
		 * Users don't rely on the iterator being in any particular
		 * state for async I/O returning -EIOCBQUEUED, hence we can
		 * avoid expensive iov_iter_advance(). Bypass
		 * bio_iov_iter_get_pages() and set the bvec directly.
		 */
		bio_iov_bvec_set(bio, iter);
	} else {
		ret = bio_iov_iter_get_pages(bio, iter);
		if (unlikely(ret)) {
			bio_put(bio);
			return ret;
		}
	}
	dio->size = bio->bi_iter.bi_size;

	if (is_read) {
		if (user_backed_iter(iter)) {
			dio->flags |= DIO_SHOULD_DIRTY;
			bio_set_pages_dirty(bio);
		}
	} else {
		task_io_account_write(bio->bi_iter.bi_size);
	}

	if (iocb->ki_flags & IOCB_HIPRI) {
		bio->bi_opf |= REQ_POLLED | REQ_NOWAIT;
		submit_bio(bio);
		WRITE_ONCE(iocb->private, bio);
	} else {
		if (iocb->ki_flags & IOCB_NOWAIT)
			bio->bi_opf |= REQ_NOWAIT;
		submit_bio(bio);
	}
	return -EIOCBQUEUED;
}

static ssize_t blkdev_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	unsigned int nr_pages;

	if (!iov_iter_count(iter))
		return 0;

	nr_pages = bio_iov_vecs_to_alloc(iter, BIO_MAX_VECS + 1);
	if (likely(nr_pages <= BIO_MAX_VECS)) {
		if (is_sync_kiocb(iocb))
			return __blkdev_direct_IO_simple(iocb, iter, nr_pages);
		return __blkdev_direct_IO_async(iocb, iter, nr_pages);
	}
	return __blkdev_direct_IO(iocb, iter, bio_max_segs(nr_pages));
}

static int blkdev_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, blkdev_get_block, wbc);
}

static int blkdev_read_folio(struct file *file, struct folio *folio)
{
	return block_read_full_folio(folio, blkdev_get_block);
}

static void blkdev_readahead(struct readahead_control *rac)
{
	mpage_readahead(rac, blkdev_get_block);
}

static int blkdev_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, struct page **pagep, void **fsdata)
{
	return block_write_begin(mapping, pos, len, pagep, blkdev_get_block);
}

static int blkdev_write_end(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned copied, struct page *page,
		void *fsdata)
{
	int ret;
	ret = block_write_end(file, mapping, pos, len, copied, page, fsdata);

	unlock_page(page);
	put_page(page);

	return ret;
}

const struct address_space_operations def_blk_aops = {
	.dirty_folio	= block_dirty_folio,
	.invalidate_folio = block_invalidate_folio,
	.read_folio	= blkdev_read_folio,
	.readahead	= blkdev_readahead,
	.writepage	= blkdev_writepage,
	.write_begin	= blkdev_write_begin,
	.write_end	= blkdev_write_end,
	.direct_IO	= blkdev_direct_IO,
	.migrate_folio	= buffer_migrate_folio_norefs,
	.is_dirty_writeback = buffer_check_dirty_writeback,
};

/*
 * for a block special file file_inode(file)->i_size is zero
 * so we compute the size by hand (just as in block_read/write above)
 */
static loff_t blkdev_llseek(struct file *file, loff_t offset, int whence)
{
	struct inode *bd_inode = bdev_file_inode(file);
	loff_t retval;

	inode_lock(bd_inode);
	retval = fixed_size_llseek(file, offset, whence, i_size_read(bd_inode));
	inode_unlock(bd_inode);
	return retval;
}

static int blkdev_fsync(struct file *filp, loff_t start, loff_t end,
		int datasync)
{
	struct block_device *bdev = filp->private_data;
	int error;

	error = file_write_and_wait_range(filp, start, end);
	if (error)
		return error;

	/*
	 * There is no need to serialise calls to blkdev_issue_flush with
	 * i_mutex and doing so causes performance issues with concurrent
	 * O_SYNC writers to a block device.
	 */
	error = blkdev_issue_flush(bdev);
	if (error == -EOPNOTSUPP)
		error = 0;

	return error;
}

static int blkdev_open(struct inode *inode, struct file *filp)
{
	struct block_device *bdev;

	/*
	 * Preserve backwards compatibility and allow large file access
	 * even if userspace doesn't ask for it explicitly. Some mkfs
	 * binary needs it. We might want to drop this workaround
	 * during an unstable branch.
	 */
	filp->f_flags |= O_LARGEFILE;
	filp->f_mode |= FMODE_NOWAIT | FMODE_BUF_RASYNC;

	if (filp->f_flags & O_NDELAY)
		filp->f_mode |= FMODE_NDELAY;
	if (filp->f_flags & O_EXCL)
		filp->f_mode |= FMODE_EXCL;
	if ((filp->f_flags & O_ACCMODE) == 3)
		filp->f_mode |= FMODE_WRITE_IOCTL;

	bdev = blkdev_get_by_dev(inode->i_rdev, filp->f_mode, filp);
	if (IS_ERR(bdev))
		return PTR_ERR(bdev);

	filp->private_data = bdev;
	filp->f_mapping = bdev->bd_inode->i_mapping;
	filp->f_wb_err = filemap_sample_wb_err(filp->f_mapping);
	return 0;
}

static int blkdev_close(struct inode *inode, struct file *filp)
{
	struct block_device *bdev = filp->private_data;

	blkdev_put(bdev, filp->f_mode);
	return 0;
}

/*
 * Write data to the block device.  Only intended for the block device itself
 * and the raw driver which basically is a fake block device.
 *
 * Does not take i_mutex for the write and thus is not for general purpose
 * use.
 */
static ssize_t blkdev_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct block_device *bdev = iocb->ki_filp->private_data;
	struct inode *bd_inode = bdev->bd_inode;
	loff_t size = bdev_nr_bytes(bdev);
	struct blk_plug plug;
	size_t shorted = 0;
	ssize_t ret;

	if (bdev_read_only(bdev))
		return -EPERM;

	if (IS_SWAPFILE(bd_inode) && !is_hibernate_resume_dev(bd_inode->i_rdev))
		return -ETXTBSY;

	if (!iov_iter_count(from))
		return 0;

	if (iocb->ki_pos >= size)
		return -ENOSPC;

	if ((iocb->ki_flags & (IOCB_NOWAIT | IOCB_DIRECT)) == IOCB_NOWAIT)
		return -EOPNOTSUPP;

	size -= iocb->ki_pos;
	if (iov_iter_count(from) > size) {
		shorted = iov_iter_count(from) - size;
		iov_iter_truncate(from, size);
	}

	blk_start_plug(&plug);
	ret = __generic_file_write_iter(iocb, from);
	if (ret > 0)
		ret = generic_write_sync(iocb, ret);
	iov_iter_reexpand(from, iov_iter_count(from) + shorted);
	blk_finish_plug(&plug);
	return ret;
}

static ssize_t blkdev_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct block_device *bdev = iocb->ki_filp->private_data;
	loff_t size = bdev_nr_bytes(bdev);
	loff_t pos = iocb->ki_pos;
	size_t shorted = 0;
	ssize_t ret = 0;
	size_t count;

	if (unlikely(pos + iov_iter_count(to) > size)) {
		if (pos >= size)
			return 0;
		size -= pos;
		shorted = iov_iter_count(to) - size;
		iov_iter_truncate(to, size);
	}

	count = iov_iter_count(to);
	if (!count)
		goto reexpand; /* skip atime */

	if (iocb->ki_flags & IOCB_DIRECT) {
		struct address_space *mapping = iocb->ki_filp->f_mapping;

		if (iocb->ki_flags & IOCB_NOWAIT) {
			if (filemap_range_needs_writeback(mapping, pos,
							  pos + count - 1)) {
				ret = -EAGAIN;
				goto reexpand;
			}
		} else {
			ret = filemap_write_and_wait_range(mapping, pos,
							   pos + count - 1);
			if (ret < 0)
				goto reexpand;
		}

		file_accessed(iocb->ki_filp);

		ret = blkdev_direct_IO(iocb, to);
		if (ret >= 0) {
			iocb->ki_pos += ret;
			count -= ret;
		}
		iov_iter_revert(to, count - iov_iter_count(to));
		if (ret < 0 || !count)
			goto reexpand;
	}

	ret = filemap_read(iocb, to, ret);

reexpand:
	if (unlikely(shorted))
		iov_iter_reexpand(to, iov_iter_count(to) + shorted);
	return ret;
}

#define	BLKDEV_FALLOC_FL_SUPPORTED					\
		(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE |		\
		 FALLOC_FL_ZERO_RANGE | FALLOC_FL_NO_HIDE_STALE)

static long blkdev_fallocate(struct file *file, int mode, loff_t start,
			     loff_t len)
{
	struct inode *inode = bdev_file_inode(file);
	struct block_device *bdev = I_BDEV(inode);
	loff_t end = start + len - 1;
	loff_t isize;
	int error;

	/* Fail if we don't recognize the flags. */
	if (mode & ~BLKDEV_FALLOC_FL_SUPPORTED)
		return -EOPNOTSUPP;

	/* Don't go off the end of the device. */
	isize = bdev_nr_bytes(bdev);
	if (start >= isize)
		return -EINVAL;
	if (end >= isize) {
		if (mode & FALLOC_FL_KEEP_SIZE) {
			len = isize - start;
			end = start + len - 1;
		} else
			return -EINVAL;
	}

	/*
	 * Don't allow IO that isn't aligned to logical block size.
	 */
	if ((start | len) & (bdev_logical_block_size(bdev) - 1))
		return -EINVAL;

	filemap_invalidate_lock(inode->i_mapping);

	/* Invalidate the page cache, including dirty pages. */
	error = truncate_bdev_range(bdev, file->f_mode, start, end);
	if (error)
		goto fail;

	switch (mode) {
	case FALLOC_FL_ZERO_RANGE:
	case FALLOC_FL_ZERO_RANGE | FALLOC_FL_KEEP_SIZE:
		error = blkdev_issue_zeroout(bdev, start >> SECTOR_SHIFT,
					     len >> SECTOR_SHIFT, GFP_KERNEL,
					     BLKDEV_ZERO_NOUNMAP);
		break;
	case FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE:
		error = blkdev_issue_zeroout(bdev, start >> SECTOR_SHIFT,
					     len >> SECTOR_SHIFT, GFP_KERNEL,
					     BLKDEV_ZERO_NOFALLBACK);
		break;
	case FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE | FALLOC_FL_NO_HIDE_STALE:
		error = blkdev_issue_discard(bdev, start >> SECTOR_SHIFT,
					     len >> SECTOR_SHIFT, GFP_KERNEL);
		break;
	default:
		error = -EOPNOTSUPP;
	}

 fail:
	filemap_invalidate_unlock(inode->i_mapping);
	return error;
}

const struct file_operations def_blk_fops = {
	.open		= blkdev_open,
	.release	= blkdev_close,
	.llseek		= blkdev_llseek,
	.read_iter	= blkdev_read_iter,
	.write_iter	= blkdev_write_iter,
	.iopoll		= iocb_bio_iopoll,
	.mmap		= generic_file_mmap,
	.fsync		= blkdev_fsync,
	.unlocked_ioctl	= blkdev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= compat_blkdev_ioctl,
#endif
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.fallocate	= blkdev_fallocate,
};

static __init int blkdev_init(void)
{
	return bioset_init(&blkdev_dio_pool, 4,
				offsetof(struct blkdev_dio, bio),
				BIOSET_NEED_BVECS|BIOSET_PERCPU_CACHE);
}
module_init(blkdev_init);
