/*
 * ramdisk.c - Multiple RAM disk driver - gzip-loading version - v. 0.8 beta.
 *
 * (C) Chad Page, Theodore Ts'o, et. al, 1995.
 *
 * This RAM disk is designed to have filesystems created on it and mounted
 * just like a regular floppy disk.
 *
 * It also does something suggested by Linus: use the buffer cache as the
 * RAM disk data.  This makes it possible to dynamically allocate the RAM disk
 * buffer - with some consequences I have to deal with as I write this.
 *
 * This code is based on the original ramdisk.c, written mostly by
 * Theodore Ts'o (TYT) in 1991.  The code was largely rewritten by
 * Chad Page to use the buffer cache to store the RAM disk data in
 * 1995; Theodore then took over the driver again, and cleaned it up
 * for inclusion in the mainline kernel.
 *
 * The original CRAMDISK code was written by Richard Lyons, and
 * adapted by Chad Page to use the new RAM disk interface.  Theodore
 * Ts'o rewrote it so that both the compressed RAM disk loader and the
 * kernel decompressor uses the same inflate.c codebase.  The RAM disk
 * loader now also loads into a dynamic (buffer cache based) RAM disk,
 * not the old static RAM disk.  Support for the old static RAM disk has
 * been completely removed.
 *
 * Loadable module support added by Tom Dyas.
 *
 * Further cleanups by Chad Page (page0588@sundance.sjsu.edu):
 *	Cosmetic changes in #ifdef MODULE, code movement, etc.
 * 	When the RAM disk module is removed, free the protected buffers
 * 	Default RAM disk size changed to 2.88 MB
 *
 *  Added initrd: Werner Almesberger & Hans Lermen, Feb '96
 *
 * 4/25/96 : Made RAM disk size a parameter (default is now 4 MB)
 *		- Chad Page
 *
 * Add support for fs images split across >1 disk, Paul Gortmaker, Mar '98
 *
 * Make block size and block size shift for RAM disks a global macro
 * and set blk_size for -ENOSPC,     Werner Fink <werner@suse.de>, Apr '99
 */

#include <linux/config.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <asm/atomic.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/pagemap.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/buffer_head.h>		/* for invalidate_bdev() */
#include <linux/backing-dev.h>
#include <linux/blkpg.h>
#include <linux/writeback.h>

#include <asm/uaccess.h>

/* Various static variables go here.  Most are used only in the RAM disk code.
 */

static struct gendisk *rd_disks[CONFIG_BLK_DEV_RAM_COUNT];
static struct block_device *rd_bdev[CONFIG_BLK_DEV_RAM_COUNT];/* Protected device data */
static struct request_queue *rd_queue[CONFIG_BLK_DEV_RAM_COUNT];

/*
 * Parameters for the boot-loading of the RAM disk.  These are set by
 * init/main.c (from arguments to the kernel command line) or from the
 * architecture-specific setup routine (from the stored boot sector
 * information).
 */
int rd_size = CONFIG_BLK_DEV_RAM_SIZE;		/* Size of the RAM disks */
/*
 * It would be very desirable to have a soft-blocksize (that in the case
 * of the ramdisk driver is also the hardblocksize ;) of PAGE_SIZE because
 * doing that we'll achieve a far better MM footprint. Using a rd_blocksize of
 * BLOCK_SIZE in the worst case we'll make PAGE_SIZE/BLOCK_SIZE buffer-pages
 * unfreeable. With a rd_blocksize of PAGE_SIZE instead we are sure that only
 * 1 page will be protected. Depending on the size of the ramdisk you
 * may want to change the ramdisk blocksize to achieve a better or worse MM
 * behaviour. The default is still BLOCK_SIZE (needed by rd_load_image that
 * supposes the filesystem in the image uses a BLOCK_SIZE blocksize).
 */
static int rd_blocksize = BLOCK_SIZE;		/* blocksize of the RAM disks */

/*
 * Copyright (C) 2000 Linus Torvalds.
 *               2000 Transmeta Corp.
 * aops copied from ramfs.
 */

/*
 * If a ramdisk page has buffers, some may be uptodate and some may be not.
 * To bring the page uptodate we zero out the non-uptodate buffers.  The
 * page must be locked.
 */
static void make_page_uptodate(struct page *page)
{
	if (page_has_buffers(page)) {
		struct buffer_head *bh = page_buffers(page);
		struct buffer_head *head = bh;

		do {
			if (!buffer_uptodate(bh)) {
				memset(bh->b_data, 0, bh->b_size);
				/*
				 * akpm: I'm totally undecided about this.  The
				 * buffer has just been magically brought "up to
				 * date", but nobody should want to be reading
				 * it anyway, because it hasn't been used for
				 * anything yet.  It is still in a "not read
				 * from disk yet" state.
				 *
				 * But non-uptodate buffers against an uptodate
				 * page are against the rules.  So do it anyway.
				 */
				 set_buffer_uptodate(bh);
			}
		} while ((bh = bh->b_this_page) != head);
	} else {
		memset(page_address(page), 0, PAGE_CACHE_SIZE);
	}
	flush_dcache_page(page);
	SetPageUptodate(page);
}

static int ramdisk_readpage(struct file *file, struct page *page)
{
	if (!PageUptodate(page))
		make_page_uptodate(page);
	unlock_page(page);
	return 0;
}

static int ramdisk_prepare_write(struct file *file, struct page *page,
				unsigned offset, unsigned to)
{
	if (!PageUptodate(page))
		make_page_uptodate(page);
	return 0;
}

static int ramdisk_commit_write(struct file *file, struct page *page,
				unsigned offset, unsigned to)
{
	set_page_dirty(page);
	return 0;
}

/*
 * ->writepage to the the blockdev's mapping has to redirty the page so that the
 * VM doesn't go and steal it.  We return AOP_WRITEPAGE_ACTIVATE so that the VM
 * won't try to (pointlessly) write the page again for a while.
 *
 * Really, these pages should not be on the LRU at all.
 */
static int ramdisk_writepage(struct page *page, struct writeback_control *wbc)
{
	if (!PageUptodate(page))
		make_page_uptodate(page);
	SetPageDirty(page);
	if (wbc->for_reclaim)
		return AOP_WRITEPAGE_ACTIVATE;
	unlock_page(page);
	return 0;
}

/*
 * This is a little speedup thing: short-circuit attempts to write back the
 * ramdisk blockdev inode to its non-existent backing store.
 */
static int ramdisk_writepages(struct address_space *mapping,
				struct writeback_control *wbc)
{
	return 0;
}

/*
 * ramdisk blockdev pages have their own ->set_page_dirty() because we don't
 * want them to contribute to dirty memory accounting.
 */
static int ramdisk_set_page_dirty(struct page *page)
{
	SetPageDirty(page);
	return 0;
}

static struct address_space_operations ramdisk_aops = {
	.readpage	= ramdisk_readpage,
	.prepare_write	= ramdisk_prepare_write,
	.commit_write	= ramdisk_commit_write,
	.writepage	= ramdisk_writepage,
	.set_page_dirty	= ramdisk_set_page_dirty,
	.writepages	= ramdisk_writepages,
};

static int rd_blkdev_pagecache_IO(int rw, struct bio_vec *vec, sector_t sector,
				struct address_space *mapping)
{
	pgoff_t index = sector >> (PAGE_CACHE_SHIFT - 9);
	unsigned int vec_offset = vec->bv_offset;
	int offset = (sector << 9) & ~PAGE_CACHE_MASK;
	int size = vec->bv_len;
	int err = 0;

	do {
		int count;
		struct page *page;
		char *src;
		char *dst;

		count = PAGE_CACHE_SIZE - offset;
		if (count > size)
			count = size;
		size -= count;

		page = grab_cache_page(mapping, index);
		if (!page) {
			err = -ENOMEM;
			goto out;
		}

		if (!PageUptodate(page))
			make_page_uptodate(page);

		index++;

		if (rw == READ) {
			src = kmap_atomic(page, KM_USER0) + offset;
			dst = kmap_atomic(vec->bv_page, KM_USER1) + vec_offset;
		} else {
			src = kmap_atomic(vec->bv_page, KM_USER0) + vec_offset;
			dst = kmap_atomic(page, KM_USER1) + offset;
		}
		offset = 0;
		vec_offset += count;

		memcpy(dst, src, count);

		kunmap_atomic(src, KM_USER0);
		kunmap_atomic(dst, KM_USER1);

		if (rw == READ)
			flush_dcache_page(vec->bv_page);
		else
			set_page_dirty(page);
		unlock_page(page);
		put_page(page);
	} while (size);

 out:
	return err;
}

/*
 *  Basically, my strategy here is to set up a buffer-head which can't be
 *  deleted, and make that my Ramdisk.  If the request is outside of the
 *  allocated size, we must get rid of it...
 *
 * 19-JAN-1998  Richard Gooch <rgooch@atnf.csiro.au>  Added devfs support
 *
 */
static int rd_make_request(request_queue_t *q, struct bio *bio)
{
	struct block_device *bdev = bio->bi_bdev;
	struct address_space * mapping = bdev->bd_inode->i_mapping;
	sector_t sector = bio->bi_sector;
	unsigned long len = bio->bi_size >> 9;
	int rw = bio_data_dir(bio);
	struct bio_vec *bvec;
	int ret = 0, i;

	if (sector + len > get_capacity(bdev->bd_disk))
		goto fail;

	if (rw==READA)
		rw=READ;

	bio_for_each_segment(bvec, bio, i) {
		ret |= rd_blkdev_pagecache_IO(rw, bvec, sector, mapping);
		sector += bvec->bv_len >> 9;
	}
	if (ret)
		goto fail;

	bio_endio(bio, bio->bi_size, 0);
	return 0;
fail:
	bio_io_error(bio, bio->bi_size);
	return 0;
} 

static int rd_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	int error;
	struct block_device *bdev = inode->i_bdev;

	if (cmd != BLKFLSBUF)
		return -ENOTTY;

	/*
	 * special: we want to release the ramdisk memory, it's not like with
	 * the other blockdevices where this ioctl only flushes away the buffer
	 * cache
	 */
	error = -EBUSY;
	down(&bdev->bd_sem);
	if (bdev->bd_openers <= 2) {
		truncate_inode_pages(bdev->bd_inode->i_mapping, 0);
		error = 0;
	}
	up(&bdev->bd_sem);
	return error;
}

/*
 * This is the backing_dev_info for the blockdev inode itself.  It doesn't need
 * writeback and it does not contribute to dirty memory accounting.
 */
static struct backing_dev_info rd_backing_dev_info = {
	.ra_pages	= 0,	/* No readahead */
	.capabilities	= BDI_CAP_NO_ACCT_DIRTY | BDI_CAP_NO_WRITEBACK | BDI_CAP_MAP_COPY,
	.unplug_io_fn	= default_unplug_io_fn,
};

/*
 * This is the backing_dev_info for the files which live atop the ramdisk
 * "device".  These files do need writeback and they do contribute to dirty
 * memory accounting.
 */
static struct backing_dev_info rd_file_backing_dev_info = {
	.ra_pages	= 0,	/* No readahead */
	.capabilities	= BDI_CAP_MAP_COPY,	/* Does contribute to dirty memory */
	.unplug_io_fn	= default_unplug_io_fn,
};

static int rd_open(struct inode *inode, struct file *filp)
{
	unsigned unit = iminor(inode);

	if (rd_bdev[unit] == NULL) {
		struct block_device *bdev = inode->i_bdev;
		struct address_space *mapping;
		unsigned bsize;
		gfp_t gfp_mask;

		inode = igrab(bdev->bd_inode);
		rd_bdev[unit] = bdev;
		bdev->bd_openers++;
		bsize = bdev_hardsect_size(bdev);
		bdev->bd_block_size = bsize;
		inode->i_blkbits = blksize_bits(bsize);
		inode->i_size = get_capacity(bdev->bd_disk)<<9;

		mapping = inode->i_mapping;
		mapping->a_ops = &ramdisk_aops;
		mapping->backing_dev_info = &rd_backing_dev_info;
		bdev->bd_inode_backing_dev_info = &rd_file_backing_dev_info;

		/*
		 * Deep badness.  rd_blkdev_pagecache_IO() needs to allocate
		 * pagecache pages within a request_fn.  We cannot recur back
		 * into the filesytem which is mounted atop the ramdisk, because
		 * that would deadlock on fs locks.  And we really don't want
		 * to reenter rd_blkdev_pagecache_IO when we're already within
		 * that function.
		 *
		 * So we turn off __GFP_FS and __GFP_IO.
		 *
		 * And to give this thing a hope of working, turn on __GFP_HIGH.
		 * Hopefully, there's enough regular memory allocation going on
		 * for the page allocator emergency pools to keep the ramdisk
		 * driver happy.
		 */
		gfp_mask = mapping_gfp_mask(mapping);
		gfp_mask &= ~(__GFP_FS|__GFP_IO);
		gfp_mask |= __GFP_HIGH;
		mapping_set_gfp_mask(mapping, gfp_mask);
	}

	return 0;
}

static struct block_device_operations rd_bd_op = {
	.owner =	THIS_MODULE,
	.open =		rd_open,
	.ioctl =	rd_ioctl,
};

/*
 * Before freeing the module, invalidate all of the protected buffers!
 */
static void __exit rd_cleanup(void)
{
	int i;

	for (i = 0; i < CONFIG_BLK_DEV_RAM_COUNT; i++) {
		struct block_device *bdev = rd_bdev[i];
		rd_bdev[i] = NULL;
		if (bdev) {
			invalidate_bdev(bdev, 1);
			blkdev_put(bdev);
		}
		del_gendisk(rd_disks[i]);
		put_disk(rd_disks[i]);
		blk_cleanup_queue(rd_queue[i]);
	}
	devfs_remove("rd");
	unregister_blkdev(RAMDISK_MAJOR, "ramdisk");
}

/*
 * This is the registration and initialization section of the RAM disk driver
 */
static int __init rd_init(void)
{
	int i;
	int err = -ENOMEM;

	if (rd_blocksize > PAGE_SIZE || rd_blocksize < 512 ||
			(rd_blocksize & (rd_blocksize-1))) {
		printk("RAMDISK: wrong blocksize %d, reverting to defaults\n",
		       rd_blocksize);
		rd_blocksize = BLOCK_SIZE;
	}

	for (i = 0; i < CONFIG_BLK_DEV_RAM_COUNT; i++) {
		rd_disks[i] = alloc_disk(1);
		if (!rd_disks[i])
			goto out;
	}

	if (register_blkdev(RAMDISK_MAJOR, "ramdisk")) {
		err = -EIO;
		goto out;
	}

	devfs_mk_dir("rd");

	for (i = 0; i < CONFIG_BLK_DEV_RAM_COUNT; i++) {
		struct gendisk *disk = rd_disks[i];

		rd_queue[i] = blk_alloc_queue(GFP_KERNEL);
		if (!rd_queue[i])
			goto out_queue;

		blk_queue_make_request(rd_queue[i], &rd_make_request);
		blk_queue_hardsect_size(rd_queue[i], rd_blocksize);

		/* rd_size is given in kB */
		disk->major = RAMDISK_MAJOR;
		disk->first_minor = i;
		disk->fops = &rd_bd_op;
		disk->queue = rd_queue[i];
		disk->flags |= GENHD_FL_SUPPRESS_PARTITION_INFO;
		sprintf(disk->disk_name, "ram%d", i);
		sprintf(disk->devfs_name, "rd/%d", i);
		set_capacity(disk, rd_size * 2);
		add_disk(rd_disks[i]);
	}

	/* rd_size is given in kB */
	printk("RAMDISK driver initialized: "
		"%d RAM disks of %dK size %d blocksize\n",
		CONFIG_BLK_DEV_RAM_COUNT, rd_size, rd_blocksize);

	return 0;
out_queue:
	unregister_blkdev(RAMDISK_MAJOR, "ramdisk");
out:
	while (i--) {
		put_disk(rd_disks[i]);
		blk_cleanup_queue(rd_queue[i]);
	}
	return err;
}

module_init(rd_init);
module_exit(rd_cleanup);

/* options - nonmodular */
#ifndef MODULE
static int __init ramdisk_size(char *str)
{
	rd_size = simple_strtol(str,NULL,0);
	return 1;
}
static int __init ramdisk_size2(char *str)	/* kludge */
{
	return ramdisk_size(str);
}
static int __init ramdisk_blocksize(char *str)
{
	rd_blocksize = simple_strtol(str,NULL,0);
	return 1;
}
__setup("ramdisk=", ramdisk_size);
__setup("ramdisk_size=", ramdisk_size2);
__setup("ramdisk_blocksize=", ramdisk_blocksize);
#endif

/* options - modular */
module_param(rd_size, int, 0);
MODULE_PARM_DESC(rd_size, "Size of each RAM disk in kbytes.");
module_param(rd_blocksize, int, 0);
MODULE_PARM_DESC(rd_blocksize, "Blocksize of each RAM disk in bytes.");
MODULE_ALIAS_BLOCKDEV_MAJOR(RAMDISK_MAJOR);

MODULE_LICENSE("GPL");
