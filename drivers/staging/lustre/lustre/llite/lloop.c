/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

/*
 *  linux/drivers/block/loop.c
 *
 *  Written by Theodore Ts'o, 3/29/93
 *
 * Copyright 1993 by Theodore Ts'o.  Redistribution of this file is
 * permitted under the GNU General Public License.
 *
 * Modularized and updated for 1.1.16 kernel - Mitch Dsouza 28th May 1994
 * Adapted for 1.3.59 kernel - Andries Brouwer, 1 Feb 1996
 *
 * Fixed do_loop_request() re-entrancy - Vincent.Renardias@waw.com Mar 20, 1997
 *
 * Added devfs support - Richard Gooch <rgooch@atnf.csiro.au> 16-Jan-1998
 *
 * Handle sparse backing files correctly - Kenn Humborg, Jun 28, 1998
 *
 * Loadable modules and other fixes by AK, 1998
 *
 * Maximum number of loop devices now dynamic via max_loop module parameter.
 * Russell Kroll <rkroll@exploits.org> 19990701
 *
 * Maximum number of loop devices when compiled-in now selectable by passing
 * max_loop=<1-255> to the kernel on boot.
 * Erik I. Bols?, <eriki@himolde.no>, Oct 31, 1999
 *
 * Completely rewrite request handling to be make_request_fn style and
 * non blocking, pushing work to a helper thread. Lots of fixes from
 * Al Viro too.
 * Jens Axboe <axboe@suse.de>, Nov 2000
 *
 * Support up to 256 loop devices
 * Heinz Mauelshagen <mge@sistina.com>, Feb 2002
 *
 * Support for falling back on the write file operation when the address space
 * operations prepare_write and/or commit_write are not available on the
 * backing filesystem.
 * Anton Altaparmakov, 16 Feb 2005
 *
 * Still To Fix:
 * - Advisory locking is ignored here.
 * - Should use an own CAP_* category instead of CAP_SYS_ADMIN
 *
 */

#include <linux/module.h>

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/wait.h>
#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/init.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/writeback.h>
#include <linux/buffer_head.h>		/* for invalidate_bdev() */
#include <linux/completion.h>
#include <linux/highmem.h>
#include <linux/gfp.h>
#include <linux/swap.h>
#include <linux/pagevec.h>

#include <asm/uaccess.h>

#include <lustre_lib.h>
#include <lustre_lite.h>
#include "llite_internal.h"

#define LLOOP_MAX_SEGMENTS	LNET_MAX_IOV

/* Possible states of device */
enum {
	LLOOP_UNBOUND,
	LLOOP_BOUND,
	LLOOP_RUNDOWN,
};

struct lloop_device {
	int		  lo_number;
	int		  lo_refcnt;
	loff_t	       lo_offset;
	loff_t	       lo_sizelimit;
	int		  lo_flags;
	int		(*ioctl)(struct lloop_device *, int cmd,
				    unsigned long arg);

	struct file	 *lo_backing_file;
	struct block_device *lo_device;
	unsigned	     lo_blocksize;

	int		  old_gfp_mask;

	spinlock_t		lo_lock;
	struct bio		*lo_bio;
	struct bio		*lo_biotail;
	int			lo_state;
	struct semaphore	lo_sem;
	struct mutex		lo_ctl_mutex;
	atomic_t	 lo_pending;
	wait_queue_head_t	  lo_bh_wait;

	struct request_queue *lo_queue;

	const struct lu_env *lo_env;
	struct cl_io	 lo_io;
	struct ll_dio_pages  lo_pvec;

	/* data to handle bio for lustre. */
	struct lo_request_data {
		struct page *lrd_pages[LLOOP_MAX_SEGMENTS];
		loff_t       lrd_offsets[LLOOP_MAX_SEGMENTS];
	} lo_requests[1];
};

/*
 * Loop flags
 */
enum {
	LO_FLAGS_READ_ONLY       = 1,
};

static int lloop_major;
#define MAX_LOOP_DEFAULT  16
static int max_loop = MAX_LOOP_DEFAULT;
static struct lloop_device *loop_dev;
static struct gendisk **disks;
static struct mutex lloop_mutex;
static void *ll_iocontrol_magic = NULL;

static loff_t get_loop_size(struct lloop_device *lo, struct file *file)
{
	loff_t size, offset, loopsize;

	/* Compute loopsize in bytes */
	size = i_size_read(file->f_mapping->host);
	offset = lo->lo_offset;
	loopsize = size - offset;
	if (lo->lo_sizelimit > 0 && lo->lo_sizelimit < loopsize)
		loopsize = lo->lo_sizelimit;

	/*
	 * Unfortunately, if we want to do I/O on the device,
	 * the number of 512-byte sectors has to fit into a sector_t.
	 */
	return loopsize >> 9;
}

static int do_bio_lustrebacked(struct lloop_device *lo, struct bio *head)
{
	const struct lu_env  *env   = lo->lo_env;
	struct cl_io	 *io    = &lo->lo_io;
	struct inode	 *inode = lo->lo_backing_file->f_dentry->d_inode;
	struct cl_object     *obj = ll_i2info(inode)->lli_clob;
	pgoff_t	       offset;
	int		   ret;
	int		   i;
	int		   rw;
	obd_count	     page_count = 0;
	struct bio_vec       *bvec;
	struct bio	   *bio;
	ssize_t	       bytes;

	struct ll_dio_pages  *pvec = &lo->lo_pvec;
	struct page	 **pages = pvec->ldp_pages;
	loff_t	       *offsets = pvec->ldp_offsets;

	truncate_inode_pages(inode->i_mapping, 0);

	/* initialize the IO */
	memset(io, 0, sizeof(*io));
	io->ci_obj = obj;
	ret = cl_io_init(env, io, CIT_MISC, obj);
	if (ret)
		return io->ci_result;
	io->ci_lockreq = CILR_NEVER;

	LASSERT(head != NULL);
	rw = head->bi_rw;
	for (bio = head; bio != NULL; bio = bio->bi_next) {
		LASSERT(rw == bio->bi_rw);

		offset = (pgoff_t)(bio->bi_sector << 9) + lo->lo_offset;
		bio_for_each_segment(bvec, bio, i) {
			BUG_ON(bvec->bv_offset != 0);
			BUG_ON(bvec->bv_len != PAGE_CACHE_SIZE);

			pages[page_count] = bvec->bv_page;
			offsets[page_count] = offset;
			page_count++;
			offset += bvec->bv_len;
		}
		LASSERT(page_count <= LLOOP_MAX_SEGMENTS);
	}

	ll_stats_ops_tally(ll_i2sbi(inode),
			(rw == WRITE) ? LPROC_LL_BRW_WRITE : LPROC_LL_BRW_READ,
			page_count);

	pvec->ldp_size = page_count << PAGE_CACHE_SHIFT;
	pvec->ldp_nr = page_count;

	/* FIXME: in ll_direct_rw_pages, it has to allocate many cl_page{}s to
	 * write those pages into OST. Even worse case is that more pages
	 * would be asked to write out to swap space, and then finally get here
	 * again.
	 * Unfortunately this is NOT easy to fix.
	 * Thoughts on solution:
	 * 0. Define a reserved pool for cl_pages, which could be a list of
	 *    pre-allocated cl_pages;
	 * 1. Define a new operation in cl_object_operations{}, says clo_depth,
	 *    which measures how many layers for this lustre object. Generally
	 *    speaking, the depth would be 2, one for llite, and one for lovsub.
	 *    However, for SNS, there will be more since we need additional page
	 *    to store parity;
	 * 2. Reserve the # of (page_count * depth) cl_pages from the reserved
	 *    pool. Afterwards, the clio would allocate the pages from reserved
	 *    pool, this guarantees we neeedn't allocate the cl_pages from
	 *    generic cl_page slab cache.
	 *    Of course, if there is NOT enough pages in the pool, we might
	 *    be asked to write less pages once, this purely depends on
	 *    implementation. Anyway, we should be careful to avoid deadlocking.
	 */
	mutex_lock(&inode->i_mutex);
	bytes = ll_direct_rw_pages(env, io, rw, inode, pvec);
	mutex_unlock(&inode->i_mutex);
	cl_io_fini(env, io);
	return (bytes == pvec->ldp_size) ? 0 : (int)bytes;
}

/*
 * Add bio to back of pending list
 */
static void loop_add_bio(struct lloop_device *lo, struct bio *bio)
{
	unsigned long flags;

	spin_lock_irqsave(&lo->lo_lock, flags);
	if (lo->lo_biotail) {
		lo->lo_biotail->bi_next = bio;
		lo->lo_biotail = bio;
	} else
		lo->lo_bio = lo->lo_biotail = bio;
	spin_unlock_irqrestore(&lo->lo_lock, flags);

	atomic_inc(&lo->lo_pending);
	if (waitqueue_active(&lo->lo_bh_wait))
		wake_up(&lo->lo_bh_wait);
}

/*
 * Grab first pending buffer
 */
static unsigned int loop_get_bio(struct lloop_device *lo, struct bio **req)
{
	struct bio *first;
	struct bio **bio;
	unsigned int count = 0;
	unsigned int page_count = 0;
	int rw;

	spin_lock_irq(&lo->lo_lock);
	first = lo->lo_bio;
	if (unlikely(first == NULL)) {
		spin_unlock_irq(&lo->lo_lock);
		return 0;
	}

	/* TODO: need to split the bio, too bad. */
	LASSERT(first->bi_vcnt <= LLOOP_MAX_SEGMENTS);

	rw = first->bi_rw;
	bio = &lo->lo_bio;
	while (*bio && (*bio)->bi_rw == rw) {
		CDEBUG(D_INFO, "bio sector %llu size %u count %u vcnt%u \n",
		       (unsigned long long)(*bio)->bi_sector, (*bio)->bi_size,
		       page_count, (*bio)->bi_vcnt);
		if (page_count + (*bio)->bi_vcnt > LLOOP_MAX_SEGMENTS)
			break;


		page_count += (*bio)->bi_vcnt;
		count++;
		bio = &(*bio)->bi_next;
	}
	if (*bio) {
		/* Some of bios can't be mergable. */
		lo->lo_bio = *bio;
		*bio = NULL;
	} else {
		/* Hit the end of queue */
		lo->lo_biotail = NULL;
		lo->lo_bio = NULL;
	}
	*req = first;
	spin_unlock_irq(&lo->lo_lock);
	return count;
}

static ll_mrf_ret
loop_make_request(struct request_queue *q, struct bio *old_bio)
{
	struct lloop_device *lo = q->queuedata;
	int rw = bio_rw(old_bio);
	int inactive;

	if (!lo)
		goto err;

	CDEBUG(D_INFO, "submit bio sector %llu size %u\n",
	       (unsigned long long)old_bio->bi_sector, old_bio->bi_size);

	spin_lock_irq(&lo->lo_lock);
	inactive = (lo->lo_state != LLOOP_BOUND);
	spin_unlock_irq(&lo->lo_lock);
	if (inactive)
		goto err;

	if (rw == WRITE) {
		if (lo->lo_flags & LO_FLAGS_READ_ONLY)
			goto err;
	} else if (rw == READA) {
		rw = READ;
	} else if (rw != READ) {
		CERROR("lloop: unknown command (%x)\n", rw);
		goto err;
	}
	loop_add_bio(lo, old_bio);
	LL_MRF_RETURN(0);
err:
	cfs_bio_io_error(old_bio, old_bio->bi_size);
	LL_MRF_RETURN(0);
}


static inline void loop_handle_bio(struct lloop_device *lo, struct bio *bio)
{
	int ret;
	ret = do_bio_lustrebacked(lo, bio);
	while (bio) {
		struct bio *tmp = bio->bi_next;
		bio->bi_next = NULL;
		cfs_bio_endio(bio, bio->bi_size, ret);
		bio = tmp;
	}
}

static inline int loop_active(struct lloop_device *lo)
{
	return atomic_read(&lo->lo_pending) ||
		(lo->lo_state == LLOOP_RUNDOWN);
}

/*
 * worker thread that handles reads/writes to file backed loop devices,
 * to avoid blocking in our make_request_fn.
 */
static int loop_thread(void *data)
{
	struct lloop_device *lo = data;
	struct bio *bio;
	unsigned int count;
	unsigned long times = 0;
	unsigned long total_count = 0;

	struct lu_env *env;
	int refcheck;
	int ret = 0;

	set_user_nice(current, -20);

	lo->lo_state = LLOOP_BOUND;

	env = cl_env_get(&refcheck);
	if (IS_ERR(env))
		GOTO(out, ret = PTR_ERR(env));

	lo->lo_env = env;
	memset(&lo->lo_pvec, 0, sizeof(lo->lo_pvec));
	lo->lo_pvec.ldp_pages   = lo->lo_requests[0].lrd_pages;
	lo->lo_pvec.ldp_offsets = lo->lo_requests[0].lrd_offsets;

	/*
	 * up sem, we are running
	 */
	up(&lo->lo_sem);

	for (;;) {
		wait_event(lo->lo_bh_wait, loop_active(lo));
		if (!atomic_read(&lo->lo_pending)) {
			int exiting = 0;
			spin_lock_irq(&lo->lo_lock);
			exiting = (lo->lo_state == LLOOP_RUNDOWN);
			spin_unlock_irq(&lo->lo_lock);
			if (exiting)
				break;
		}

		bio = NULL;
		count = loop_get_bio(lo, &bio);
		if (!count) {
			CWARN("lloop(minor: %d): missing bio\n", lo->lo_number);
			continue;
		}

		total_count += count;
		if (total_count < count) {     /* overflow */
			total_count = count;
			times = 1;
		} else {
			times++;
		}
		if ((times & 127) == 0) {
			CDEBUG(D_INFO, "total: %lu, count: %lu, avg: %lu\n",
			       total_count, times, total_count / times);
		}

		LASSERT(bio != NULL);
		LASSERT(count <= atomic_read(&lo->lo_pending));
		loop_handle_bio(lo, bio);
		atomic_sub(count, &lo->lo_pending);
	}
	cl_env_put(env, &refcheck);

out:
	up(&lo->lo_sem);
	return ret;
}

static int loop_set_fd(struct lloop_device *lo, struct file *unused,
		       struct block_device *bdev, struct file *file)
{
	struct inode	 *inode;
	struct address_space *mapping;
	int		   lo_flags = 0;
	int		   error;
	loff_t		size;

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	error = -EBUSY;
	if (lo->lo_state != LLOOP_UNBOUND)
		goto out;

	mapping = file->f_mapping;
	inode = mapping->host;

	error = -EINVAL;
	if (!S_ISREG(inode->i_mode) || inode->i_sb->s_magic != LL_SUPER_MAGIC)
		goto out;

	if (!(file->f_mode & FMODE_WRITE))
		lo_flags |= LO_FLAGS_READ_ONLY;

	size = get_loop_size(lo, file);

	if ((loff_t)(sector_t)size != size) {
		error = -EFBIG;
		goto out;
	}

	/* remove all pages in cache so as dirty pages not to be existent. */
	truncate_inode_pages(mapping, 0);

	set_device_ro(bdev, (lo_flags & LO_FLAGS_READ_ONLY) != 0);

	lo->lo_blocksize = PAGE_CACHE_SIZE;
	lo->lo_device = bdev;
	lo->lo_flags = lo_flags;
	lo->lo_backing_file = file;
	lo->ioctl = NULL;
	lo->lo_sizelimit = 0;
	lo->old_gfp_mask = mapping_gfp_mask(mapping);
	mapping_set_gfp_mask(mapping, lo->old_gfp_mask & ~(__GFP_IO|__GFP_FS));

	lo->lo_bio = lo->lo_biotail = NULL;

	/*
	 * set queue make_request_fn, and add limits based on lower level
	 * device
	 */
	blk_queue_make_request(lo->lo_queue, loop_make_request);
	lo->lo_queue->queuedata = lo;

	/* queue parameters */
	CLASSERT(PAGE_CACHE_SIZE < (1 << (sizeof(unsigned short) * 8)));
	blk_queue_logical_block_size(lo->lo_queue,
				     (unsigned short)PAGE_CACHE_SIZE);
	blk_queue_max_hw_sectors(lo->lo_queue,
				 LLOOP_MAX_SEGMENTS << (PAGE_CACHE_SHIFT - 9));
	blk_queue_max_segments(lo->lo_queue, LLOOP_MAX_SEGMENTS);

	set_capacity(disks[lo->lo_number], size);
	bd_set_size(bdev, size << 9);

	set_blocksize(bdev, lo->lo_blocksize);

	kthread_run(loop_thread, lo, "lloop%d", lo->lo_number);
	down(&lo->lo_sem);
	return 0;

out:
	/* This is safe: open() is still holding a reference. */
	module_put(THIS_MODULE);
	return error;
}

static int loop_clr_fd(struct lloop_device *lo, struct block_device *bdev,
		       int count)
{
	struct file *filp = lo->lo_backing_file;
	int gfp = lo->old_gfp_mask;

	if (lo->lo_state != LLOOP_BOUND)
		return -ENXIO;

	if (lo->lo_refcnt > count)	/* we needed one fd for the ioctl */
		return -EBUSY;

	if (filp == NULL)
		return -EINVAL;

	spin_lock_irq(&lo->lo_lock);
	lo->lo_state = LLOOP_RUNDOWN;
	spin_unlock_irq(&lo->lo_lock);
	wake_up(&lo->lo_bh_wait);

	down(&lo->lo_sem);
	lo->lo_backing_file = NULL;
	lo->ioctl = NULL;
	lo->lo_device = NULL;
	lo->lo_offset = 0;
	lo->lo_sizelimit = 0;
	lo->lo_flags = 0;
	ll_invalidate_bdev(bdev, 0);
	set_capacity(disks[lo->lo_number], 0);
	bd_set_size(bdev, 0);
	mapping_set_gfp_mask(filp->f_mapping, gfp);
	lo->lo_state = LLOOP_UNBOUND;
	fput(filp);
	/* This is safe: open() is still holding a reference. */
	module_put(THIS_MODULE);
	return 0;
}

static int lo_open(struct block_device *bdev, fmode_t mode)
{
	struct lloop_device *lo = bdev->bd_disk->private_data;

	mutex_lock(&lo->lo_ctl_mutex);
	lo->lo_refcnt++;
	mutex_unlock(&lo->lo_ctl_mutex);

	return 0;
}

static void lo_release(struct gendisk *disk, fmode_t mode)
{
	struct lloop_device *lo = disk->private_data;

	mutex_lock(&lo->lo_ctl_mutex);
	--lo->lo_refcnt;
	mutex_unlock(&lo->lo_ctl_mutex);
}

/* lloop device node's ioctl function. */
static int lo_ioctl(struct block_device *bdev, fmode_t mode,
		    unsigned int cmd, unsigned long arg)
{
	struct lloop_device *lo = bdev->bd_disk->private_data;
	struct inode *inode = NULL;
	int err = 0;

	mutex_lock(&lloop_mutex);
	switch (cmd) {
	case LL_IOC_LLOOP_DETACH: {
		err = loop_clr_fd(lo, bdev, 2);
		if (err == 0)
			ll_blkdev_put(bdev, 0); /* grabbed in LLOOP_ATTACH */
		break;
	}

	case LL_IOC_LLOOP_INFO: {
		struct lu_fid fid;

		LASSERT(lo->lo_backing_file != NULL);
		if (inode == NULL)
			inode = lo->lo_backing_file->f_dentry->d_inode;
		if (lo->lo_state == LLOOP_BOUND)
			fid = ll_i2info(inode)->lli_fid;
		else
			fid_zero(&fid);

		if (copy_to_user((struct lu_fid *)arg, &fid, sizeof(fid)))
			err = -EFAULT;
		break;
	}

	default:
		err = -EINVAL;
		break;
	}
	mutex_unlock(&lloop_mutex);

	return err;
}

static struct block_device_operations lo_fops = {
	.owner =	THIS_MODULE,
	.open =	 lo_open,
	.release =      lo_release,
	.ioctl =	lo_ioctl,
};

/* dynamic iocontrol callback.
 * This callback is registered in lloop_init and will be called by
 * ll_iocontrol_call.
 *
 * This is a llite regular file ioctl function. It takes the responsibility
 * of attaching or detaching a file by a lloop's device numner.
 */
static enum llioc_iter lloop_ioctl(struct inode *unused, struct file *file,
				   unsigned int cmd, unsigned long arg,
				   void *magic, int *rcp)
{
	struct lloop_device *lo = NULL;
	struct block_device *bdev = NULL;
	int err = 0;
	dev_t dev;

	if (magic != ll_iocontrol_magic)
		return LLIOC_CONT;

	if (disks == NULL)
		GOTO(out1, err = -ENODEV);

	CWARN("Enter llop_ioctl\n");

	mutex_lock(&lloop_mutex);
	switch (cmd) {
	case LL_IOC_LLOOP_ATTACH: {
		struct lloop_device *lo_free = NULL;
		int i;

		for (i = 0; i < max_loop; i++, lo = NULL) {
			lo = &loop_dev[i];
			if (lo->lo_state == LLOOP_UNBOUND) {
				if (!lo_free)
					lo_free = lo;
				continue;
			}
			if (lo->lo_backing_file->f_dentry->d_inode ==
			    file->f_dentry->d_inode)
				break;
		}
		if (lo || !lo_free)
			GOTO(out, err = -EBUSY);

		lo = lo_free;
		dev = MKDEV(lloop_major, lo->lo_number);

		/* quit if the used pointer is writable */
		if (put_user((long)old_encode_dev(dev), (long*)arg))
			GOTO(out, err = -EFAULT);

		bdev = blkdev_get_by_dev(dev, file->f_mode, NULL);
		if (IS_ERR(bdev))
			GOTO(out, err = PTR_ERR(bdev));

		get_file(file);
		err = loop_set_fd(lo, NULL, bdev, file);
		if (err) {
			fput(file);
			ll_blkdev_put(bdev, 0);
		}

		break;
	}

	case LL_IOC_LLOOP_DETACH_BYDEV: {
		int minor;

		dev = old_decode_dev(arg);
		if (MAJOR(dev) != lloop_major)
			GOTO(out, err = -EINVAL);

		minor = MINOR(dev);
		if (minor > max_loop - 1)
			GOTO(out, err = -EINVAL);

		lo = &loop_dev[minor];
		if (lo->lo_state != LLOOP_BOUND)
			GOTO(out, err = -EINVAL);

		bdev = lo->lo_device;
		err = loop_clr_fd(lo, bdev, 1);
		if (err == 0)
			ll_blkdev_put(bdev, 0); /* grabbed in LLOOP_ATTACH */

		break;
	}

	default:
		err = -EINVAL;
		break;
	}

out:
	mutex_unlock(&lloop_mutex);
out1:
	if (rcp)
		*rcp = err;
	return LLIOC_STOP;
}

static int __init lloop_init(void)
{
	int	i;
	unsigned int cmdlist[] = {
		LL_IOC_LLOOP_ATTACH,
		LL_IOC_LLOOP_DETACH_BYDEV,
	};

	if (max_loop < 1 || max_loop > 256) {
		max_loop = MAX_LOOP_DEFAULT;
		CWARN("lloop: invalid max_loop (must be between"
		      " 1 and 256), using default (%u)\n", max_loop);
	}

	lloop_major = register_blkdev(0, "lloop");
	if (lloop_major < 0)
		return -EIO;

	CDEBUG(D_CONFIG, "registered lloop major %d with %u minors\n",
	       lloop_major, max_loop);

	ll_iocontrol_magic = ll_iocontrol_register(lloop_ioctl, 2, cmdlist);
	if (ll_iocontrol_magic == NULL)
		goto out_mem1;

	OBD_ALLOC_WAIT(loop_dev, max_loop * sizeof(*loop_dev));
	if (!loop_dev)
		goto out_mem1;

	OBD_ALLOC_WAIT(disks, max_loop * sizeof(*disks));
	if (!disks)
		goto out_mem2;

	for (i = 0; i < max_loop; i++) {
		disks[i] = alloc_disk(1);
		if (!disks[i])
			goto out_mem3;
	}

	mutex_init(&lloop_mutex);

	for (i = 0; i < max_loop; i++) {
		struct lloop_device *lo = &loop_dev[i];
		struct gendisk *disk = disks[i];

		lo->lo_queue = blk_alloc_queue(GFP_KERNEL);
		if (!lo->lo_queue)
			goto out_mem4;

		mutex_init(&lo->lo_ctl_mutex);
		sema_init(&lo->lo_sem, 0);
		init_waitqueue_head(&lo->lo_bh_wait);
		lo->lo_number = i;
		spin_lock_init(&lo->lo_lock);
		disk->major = lloop_major;
		disk->first_minor = i;
		disk->fops = &lo_fops;
		sprintf(disk->disk_name, "lloop%d", i);
		disk->private_data = lo;
		disk->queue = lo->lo_queue;
	}

	/* We cannot fail after we call this, so another loop!*/
	for (i = 0; i < max_loop; i++)
		add_disk(disks[i]);
	return 0;

out_mem4:
	while (i--)
		blk_cleanup_queue(loop_dev[i].lo_queue);
	i = max_loop;
out_mem3:
	while (i--)
		put_disk(disks[i]);
	OBD_FREE(disks, max_loop * sizeof(*disks));
out_mem2:
	OBD_FREE(loop_dev, max_loop * sizeof(*loop_dev));
out_mem1:
	unregister_blkdev(lloop_major, "lloop");
	ll_iocontrol_unregister(ll_iocontrol_magic);
	CERROR("lloop: ran out of memory\n");
	return -ENOMEM;
}

static void lloop_exit(void)
{
	int i;

	ll_iocontrol_unregister(ll_iocontrol_magic);
	for (i = 0; i < max_loop; i++) {
		del_gendisk(disks[i]);
		blk_cleanup_queue(loop_dev[i].lo_queue);
		put_disk(disks[i]);
	}
	if (ll_unregister_blkdev(lloop_major, "lloop"))
		CWARN("lloop: cannot unregister blkdev\n");
	else
		CDEBUG(D_CONFIG, "unregistered lloop major %d\n", lloop_major);

	OBD_FREE(disks, max_loop * sizeof(*disks));
	OBD_FREE(loop_dev, max_loop * sizeof(*loop_dev));
}

module_init(lloop_init);
module_exit(lloop_exit);

CFS_MODULE_PARM(max_loop, "i", int, 0444, "maximum of lloop_device");
MODULE_AUTHOR("Sun Microsystems, Inc. <http://www.lustre.org/>");
MODULE_DESCRIPTION("Lustre virtual block device");
MODULE_LICENSE("GPL");
