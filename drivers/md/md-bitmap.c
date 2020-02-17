// SPDX-License-Identifier: GPL-2.0-only
/*
 * bitmap.c two-level bitmap (C) Peter T. Breuer (ptb@ot.uc3m.es) 2003
 *
 * bitmap_create  - sets up the bitmap structure
 * bitmap_destroy - destroys the bitmap structure
 *
 * additions, Copyright (C) 2003-2004, Paul Clements, SteelEye Technology, Inc.:
 * - added disk storage for bitmap
 * - changes to allow various bitmap chunk sizes
 */

/*
 * Still to do:
 *
 * flush after percent set rather than just time based. (maybe both).
 */

#include <linux/blkdev.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/buffer_head.h>
#include <linux/seq_file.h>
#include <trace/events/block.h>
#include "md.h"
#include "md-bitmap.h"

static inline char *bmname(struct bitmap *bitmap)
{
	return bitmap->mddev ? mdname(bitmap->mddev) : "mdX";
}

/*
 * check a page and, if necessary, allocate it (or hijack it if the alloc fails)
 *
 * 1) check to see if this page is allocated, if it's not then try to alloc
 * 2) if the alloc fails, set the page's hijacked flag so we'll use the
 *    page pointer directly as a counter
 *
 * if we find our page, we increment the page's refcount so that it stays
 * allocated while we're using it
 */
static int md_bitmap_checkpage(struct bitmap_counts *bitmap,
			       unsigned long page, int create, int no_hijack)
__releases(bitmap->lock)
__acquires(bitmap->lock)
{
	unsigned char *mappage;

	if (page >= bitmap->pages) {
		/* This can happen if bitmap_start_sync goes beyond
		 * End-of-device while looking for a whole page.
		 * It is harmless.
		 */
		return -EINVAL;
	}

	if (bitmap->bp[page].hijacked) /* it's hijacked, don't try to alloc */
		return 0;

	if (bitmap->bp[page].map) /* page is already allocated, just return */
		return 0;

	if (!create)
		return -ENOENT;

	/* this page has not been allocated yet */

	spin_unlock_irq(&bitmap->lock);
	/* It is possible that this is being called inside a
	 * prepare_to_wait/finish_wait loop from raid5c:make_request().
	 * In general it is not permitted to sleep in that context as it
	 * can cause the loop to spin freely.
	 * That doesn't apply here as we can only reach this point
	 * once with any loop.
	 * When this function completes, either bp[page].map or
	 * bp[page].hijacked.  In either case, this function will
	 * abort before getting to this point again.  So there is
	 * no risk of a free-spin, and so it is safe to assert
	 * that sleeping here is allowed.
	 */
	sched_annotate_sleep();
	mappage = kzalloc(PAGE_SIZE, GFP_NOIO);
	spin_lock_irq(&bitmap->lock);

	if (mappage == NULL) {
		pr_debug("md/bitmap: map page allocation failed, hijacking\n");
		/* We don't support hijack for cluster raid */
		if (no_hijack)
			return -ENOMEM;
		/* failed - set the hijacked flag so that we can use the
		 * pointer as a counter */
		if (!bitmap->bp[page].map)
			bitmap->bp[page].hijacked = 1;
	} else if (bitmap->bp[page].map ||
		   bitmap->bp[page].hijacked) {
		/* somebody beat us to getting the page */
		kfree(mappage);
	} else {

		/* no page was in place and we have one, so install it */

		bitmap->bp[page].map = mappage;
		bitmap->missing_pages--;
	}
	return 0;
}

/* if page is completely empty, put it back on the free list, or dealloc it */
/* if page was hijacked, unmark the flag so it might get alloced next time */
/* Note: lock should be held when calling this */
static void md_bitmap_checkfree(struct bitmap_counts *bitmap, unsigned long page)
{
	char *ptr;

	if (bitmap->bp[page].count) /* page is still busy */
		return;

	/* page is no longer in use, it can be released */

	if (bitmap->bp[page].hijacked) { /* page was hijacked, undo this now */
		bitmap->bp[page].hijacked = 0;
		bitmap->bp[page].map = NULL;
	} else {
		/* normal case, free the page */
		ptr = bitmap->bp[page].map;
		bitmap->bp[page].map = NULL;
		bitmap->missing_pages++;
		kfree(ptr);
	}
}

/*
 * bitmap file handling - read and write the bitmap file and its superblock
 */

/*
 * basic page I/O operations
 */

/* IO operations when bitmap is stored near all superblocks */
static int read_sb_page(struct mddev *mddev, loff_t offset,
			struct page *page,
			unsigned long index, int size)
{
	/* choose a good rdev and read the page from there */

	struct md_rdev *rdev;
	sector_t target;

	rdev_for_each(rdev, mddev) {
		if (! test_bit(In_sync, &rdev->flags)
		    || test_bit(Faulty, &rdev->flags)
		    || test_bit(Bitmap_sync, &rdev->flags))
			continue;

		target = offset + index * (PAGE_SIZE/512);

		if (sync_page_io(rdev, target,
				 roundup(size, bdev_logical_block_size(rdev->bdev)),
				 page, REQ_OP_READ, 0, true)) {
			page->index = index;
			return 0;
		}
	}
	return -EIO;
}

static struct md_rdev *next_active_rdev(struct md_rdev *rdev, struct mddev *mddev)
{
	/* Iterate the disks of an mddev, using rcu to protect access to the
	 * linked list, and raising the refcount of devices we return to ensure
	 * they don't disappear while in use.
	 * As devices are only added or removed when raid_disk is < 0 and
	 * nr_pending is 0 and In_sync is clear, the entries we return will
	 * still be in the same position on the list when we re-enter
	 * list_for_each_entry_continue_rcu.
	 *
	 * Note that if entered with 'rdev == NULL' to start at the
	 * beginning, we temporarily assign 'rdev' to an address which
	 * isn't really an rdev, but which can be used by
	 * list_for_each_entry_continue_rcu() to find the first entry.
	 */
	rcu_read_lock();
	if (rdev == NULL)
		/* start at the beginning */
		rdev = list_entry(&mddev->disks, struct md_rdev, same_set);
	else {
		/* release the previous rdev and start from there. */
		rdev_dec_pending(rdev, mddev);
	}
	list_for_each_entry_continue_rcu(rdev, &mddev->disks, same_set) {
		if (rdev->raid_disk >= 0 &&
		    !test_bit(Faulty, &rdev->flags)) {
			/* this is a usable devices */
			atomic_inc(&rdev->nr_pending);
			rcu_read_unlock();
			return rdev;
		}
	}
	rcu_read_unlock();
	return NULL;
}

static int write_sb_page(struct bitmap *bitmap, struct page *page, int wait)
{
	struct md_rdev *rdev;
	struct block_device *bdev;
	struct mddev *mddev = bitmap->mddev;
	struct bitmap_storage *store = &bitmap->storage;

restart:
	rdev = NULL;
	while ((rdev = next_active_rdev(rdev, mddev)) != NULL) {
		int size = PAGE_SIZE;
		loff_t offset = mddev->bitmap_info.offset;

		bdev = (rdev->meta_bdev) ? rdev->meta_bdev : rdev->bdev;

		if (page->index == store->file_pages-1) {
			int last_page_size = store->bytes & (PAGE_SIZE-1);
			if (last_page_size == 0)
				last_page_size = PAGE_SIZE;
			size = roundup(last_page_size,
				       bdev_logical_block_size(bdev));
		}
		/* Just make sure we aren't corrupting data or
		 * metadata
		 */
		if (mddev->external) {
			/* Bitmap could be anywhere. */
			if (rdev->sb_start + offset + (page->index
						       * (PAGE_SIZE/512))
			    > rdev->data_offset
			    &&
			    rdev->sb_start + offset
			    < (rdev->data_offset + mddev->dev_sectors
			     + (PAGE_SIZE/512)))
				goto bad_alignment;
		} else if (offset < 0) {
			/* DATA  BITMAP METADATA  */
			if (offset
			    + (long)(page->index * (PAGE_SIZE/512))
			    + size/512 > 0)
				/* bitmap runs in to metadata */
				goto bad_alignment;
			if (rdev->data_offset + mddev->dev_sectors
			    > rdev->sb_start + offset)
				/* data runs in to bitmap */
				goto bad_alignment;
		} else if (rdev->sb_start < rdev->data_offset) {
			/* METADATA BITMAP DATA */
			if (rdev->sb_start
			    + offset
			    + page->index*(PAGE_SIZE/512) + size/512
			    > rdev->data_offset)
				/* bitmap runs in to data */
				goto bad_alignment;
		} else {
			/* DATA METADATA BITMAP - no problems */
		}
		md_super_write(mddev, rdev,
			       rdev->sb_start + offset
			       + page->index * (PAGE_SIZE/512),
			       size,
			       page);
	}

	if (wait && md_super_wait(mddev) < 0)
		goto restart;
	return 0;

 bad_alignment:
	return -EINVAL;
}

static void md_bitmap_file_kick(struct bitmap *bitmap);
/*
 * write out a page to a file
 */
static void write_page(struct bitmap *bitmap, struct page *page, int wait)
{
	struct buffer_head *bh;

	if (bitmap->storage.file == NULL) {
		switch (write_sb_page(bitmap, page, wait)) {
		case -EINVAL:
			set_bit(BITMAP_WRITE_ERROR, &bitmap->flags);
		}
	} else {

		bh = page_buffers(page);

		while (bh && bh->b_blocknr) {
			atomic_inc(&bitmap->pending_writes);
			set_buffer_locked(bh);
			set_buffer_mapped(bh);
			submit_bh(REQ_OP_WRITE, REQ_SYNC, bh);
			bh = bh->b_this_page;
		}

		if (wait)
			wait_event(bitmap->write_wait,
				   atomic_read(&bitmap->pending_writes)==0);
	}
	if (test_bit(BITMAP_WRITE_ERROR, &bitmap->flags))
		md_bitmap_file_kick(bitmap);
}

static void end_bitmap_write(struct buffer_head *bh, int uptodate)
{
	struct bitmap *bitmap = bh->b_private;

	if (!uptodate)
		set_bit(BITMAP_WRITE_ERROR, &bitmap->flags);
	if (atomic_dec_and_test(&bitmap->pending_writes))
		wake_up(&bitmap->write_wait);
}

/* copied from buffer.c */
static void
__clear_page_buffers(struct page *page)
{
	ClearPagePrivate(page);
	set_page_private(page, 0);
	put_page(page);
}
static void free_buffers(struct page *page)
{
	struct buffer_head *bh;

	if (!PagePrivate(page))
		return;

	bh = page_buffers(page);
	while (bh) {
		struct buffer_head *next = bh->b_this_page;
		free_buffer_head(bh);
		bh = next;
	}
	__clear_page_buffers(page);
	put_page(page);
}

/* read a page from a file.
 * We both read the page, and attach buffers to the page to record the
 * address of each block (using bmap).  These addresses will be used
 * to write the block later, completely bypassing the filesystem.
 * This usage is similar to how swap files are handled, and allows us
 * to write to a file with no concerns of memory allocation failing.
 */
static int read_page(struct file *file, unsigned long index,
		     struct bitmap *bitmap,
		     unsigned long count,
		     struct page *page)
{
	int ret = 0;
	struct inode *inode = file_inode(file);
	struct buffer_head *bh;
	sector_t block, blk_cur;

	pr_debug("read bitmap file (%dB @ %llu)\n", (int)PAGE_SIZE,
		 (unsigned long long)index << PAGE_SHIFT);

	bh = alloc_page_buffers(page, 1<<inode->i_blkbits, false);
	if (!bh) {
		ret = -ENOMEM;
		goto out;
	}
	attach_page_buffers(page, bh);
	blk_cur = index << (PAGE_SHIFT - inode->i_blkbits);
	while (bh) {
		block = blk_cur;

		if (count == 0)
			bh->b_blocknr = 0;
		else {
			ret = bmap(inode, &block);
			if (ret || !block) {
				ret = -EINVAL;
				bh->b_blocknr = 0;
				goto out;
			}

			bh->b_blocknr = block;
			bh->b_bdev = inode->i_sb->s_bdev;
			if (count < (1<<inode->i_blkbits))
				count = 0;
			else
				count -= (1<<inode->i_blkbits);

			bh->b_end_io = end_bitmap_write;
			bh->b_private = bitmap;
			atomic_inc(&bitmap->pending_writes);
			set_buffer_locked(bh);
			set_buffer_mapped(bh);
			submit_bh(REQ_OP_READ, 0, bh);
		}
		blk_cur++;
		bh = bh->b_this_page;
	}
	page->index = index;

	wait_event(bitmap->write_wait,
		   atomic_read(&bitmap->pending_writes)==0);
	if (test_bit(BITMAP_WRITE_ERROR, &bitmap->flags))
		ret = -EIO;
out:
	if (ret)
		pr_err("md: bitmap read error: (%dB @ %llu): %d\n",
		       (int)PAGE_SIZE,
		       (unsigned long long)index << PAGE_SHIFT,
		       ret);
	return ret;
}

/*
 * bitmap file superblock operations
 */

/*
 * md_bitmap_wait_writes() should be called before writing any bitmap
 * blocks, to ensure previous writes, particularly from
 * md_bitmap_daemon_work(), have completed.
 */
static void md_bitmap_wait_writes(struct bitmap *bitmap)
{
	if (bitmap->storage.file)
		wait_event(bitmap->write_wait,
			   atomic_read(&bitmap->pending_writes)==0);
	else
		/* Note that we ignore the return value.  The writes
		 * might have failed, but that would just mean that
		 * some bits which should be cleared haven't been,
		 * which is safe.  The relevant bitmap blocks will
		 * probably get written again, but there is no great
		 * loss if they aren't.
		 */
		md_super_wait(bitmap->mddev);
}


/* update the event counter and sync the superblock to disk */
void md_bitmap_update_sb(struct bitmap *bitmap)
{
	bitmap_super_t *sb;

	if (!bitmap || !bitmap->mddev) /* no bitmap for this array */
		return;
	if (bitmap->mddev->bitmap_info.external)
		return;
	if (!bitmap->storage.sb_page) /* no superblock */
		return;
	sb = kmap_atomic(bitmap->storage.sb_page);
	sb->events = cpu_to_le64(bitmap->mddev->events);
	if (bitmap->mddev->events < bitmap->events_cleared)
		/* rocking back to read-only */
		bitmap->events_cleared = bitmap->mddev->events;
	sb->events_cleared = cpu_to_le64(bitmap->events_cleared);
	/*
	 * clear BITMAP_WRITE_ERROR bit to protect against the case that
	 * a bitmap write error occurred but the later writes succeeded.
	 */
	sb->state = cpu_to_le32(bitmap->flags & ~BIT(BITMAP_WRITE_ERROR));
	/* Just in case these have been changed via sysfs: */
	sb->daemon_sleep = cpu_to_le32(bitmap->mddev->bitmap_info.daemon_sleep/HZ);
	sb->write_behind = cpu_to_le32(bitmap->mddev->bitmap_info.max_write_behind);
	/* This might have been changed by a reshape */
	sb->sync_size = cpu_to_le64(bitmap->mddev->resync_max_sectors);
	sb->chunksize = cpu_to_le32(bitmap->mddev->bitmap_info.chunksize);
	sb->nodes = cpu_to_le32(bitmap->mddev->bitmap_info.nodes);
	sb->sectors_reserved = cpu_to_le32(bitmap->mddev->
					   bitmap_info.space);
	kunmap_atomic(sb);
	write_page(bitmap, bitmap->storage.sb_page, 1);
}
EXPORT_SYMBOL(md_bitmap_update_sb);

/* print out the bitmap file superblock */
void md_bitmap_print_sb(struct bitmap *bitmap)
{
	bitmap_super_t *sb;

	if (!bitmap || !bitmap->storage.sb_page)
		return;
	sb = kmap_atomic(bitmap->storage.sb_page);
	pr_debug("%s: bitmap file superblock:\n", bmname(bitmap));
	pr_debug("         magic: %08x\n", le32_to_cpu(sb->magic));
	pr_debug("       version: %d\n", le32_to_cpu(sb->version));
	pr_debug("          uuid: %08x.%08x.%08x.%08x\n",
		 le32_to_cpu(*(__le32 *)(sb->uuid+0)),
		 le32_to_cpu(*(__le32 *)(sb->uuid+4)),
		 le32_to_cpu(*(__le32 *)(sb->uuid+8)),
		 le32_to_cpu(*(__le32 *)(sb->uuid+12)));
	pr_debug("        events: %llu\n",
		 (unsigned long long) le64_to_cpu(sb->events));
	pr_debug("events cleared: %llu\n",
		 (unsigned long long) le64_to_cpu(sb->events_cleared));
	pr_debug("         state: %08x\n", le32_to_cpu(sb->state));
	pr_debug("     chunksize: %d B\n", le32_to_cpu(sb->chunksize));
	pr_debug("  daemon sleep: %ds\n", le32_to_cpu(sb->daemon_sleep));
	pr_debug("     sync size: %llu KB\n",
		 (unsigned long long)le64_to_cpu(sb->sync_size)/2);
	pr_debug("max write behind: %d\n", le32_to_cpu(sb->write_behind));
	kunmap_atomic(sb);
}

/*
 * bitmap_new_disk_sb
 * @bitmap
 *
 * This function is somewhat the reverse of bitmap_read_sb.  bitmap_read_sb
 * reads and verifies the on-disk bitmap superblock and populates bitmap_info.
 * This function verifies 'bitmap_info' and populates the on-disk bitmap
 * structure, which is to be written to disk.
 *
 * Returns: 0 on success, -Exxx on error
 */
static int md_bitmap_new_disk_sb(struct bitmap *bitmap)
{
	bitmap_super_t *sb;
	unsigned long chunksize, daemon_sleep, write_behind;

	bitmap->storage.sb_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (bitmap->storage.sb_page == NULL)
		return -ENOMEM;
	bitmap->storage.sb_page->index = 0;

	sb = kmap_atomic(bitmap->storage.sb_page);

	sb->magic = cpu_to_le32(BITMAP_MAGIC);
	sb->version = cpu_to_le32(BITMAP_MAJOR_HI);

	chunksize = bitmap->mddev->bitmap_info.chunksize;
	BUG_ON(!chunksize);
	if (!is_power_of_2(chunksize)) {
		kunmap_atomic(sb);
		pr_warn("bitmap chunksize not a power of 2\n");
		return -EINVAL;
	}
	sb->chunksize = cpu_to_le32(chunksize);

	daemon_sleep = bitmap->mddev->bitmap_info.daemon_sleep;
	if (!daemon_sleep || (daemon_sleep > MAX_SCHEDULE_TIMEOUT)) {
		pr_debug("Choosing daemon_sleep default (5 sec)\n");
		daemon_sleep = 5 * HZ;
	}
	sb->daemon_sleep = cpu_to_le32(daemon_sleep);
	bitmap->mddev->bitmap_info.daemon_sleep = daemon_sleep;

	/*
	 * FIXME: write_behind for RAID1.  If not specified, what
	 * is a good choice?  We choose COUNTER_MAX / 2 arbitrarily.
	 */
	write_behind = bitmap->mddev->bitmap_info.max_write_behind;
	if (write_behind > COUNTER_MAX)
		write_behind = COUNTER_MAX / 2;
	sb->write_behind = cpu_to_le32(write_behind);
	bitmap->mddev->bitmap_info.max_write_behind = write_behind;

	/* keep the array size field of the bitmap superblock up to date */
	sb->sync_size = cpu_to_le64(bitmap->mddev->resync_max_sectors);

	memcpy(sb->uuid, bitmap->mddev->uuid, 16);

	set_bit(BITMAP_STALE, &bitmap->flags);
	sb->state = cpu_to_le32(bitmap->flags);
	bitmap->events_cleared = bitmap->mddev->events;
	sb->events_cleared = cpu_to_le64(bitmap->mddev->events);
	bitmap->mddev->bitmap_info.nodes = 0;

	kunmap_atomic(sb);

	return 0;
}

/* read the superblock from the bitmap file and initialize some bitmap fields */
static int md_bitmap_read_sb(struct bitmap *bitmap)
{
	char *reason = NULL;
	bitmap_super_t *sb;
	unsigned long chunksize, daemon_sleep, write_behind;
	unsigned long long events;
	int nodes = 0;
	unsigned long sectors_reserved = 0;
	int err = -EINVAL;
	struct page *sb_page;
	loff_t offset = bitmap->mddev->bitmap_info.offset;

	if (!bitmap->storage.file && !bitmap->mddev->bitmap_info.offset) {
		chunksize = 128 * 1024 * 1024;
		daemon_sleep = 5 * HZ;
		write_behind = 0;
		set_bit(BITMAP_STALE, &bitmap->flags);
		err = 0;
		goto out_no_sb;
	}
	/* page 0 is the superblock, read it... */
	sb_page = alloc_page(GFP_KERNEL);
	if (!sb_page)
		return -ENOMEM;
	bitmap->storage.sb_page = sb_page;

re_read:
	/* If cluster_slot is set, the cluster is setup */
	if (bitmap->cluster_slot >= 0) {
		sector_t bm_blocks = bitmap->mddev->resync_max_sectors;

		sector_div(bm_blocks,
			   bitmap->mddev->bitmap_info.chunksize >> 9);
		/* bits to bytes */
		bm_blocks = ((bm_blocks+7) >> 3) + sizeof(bitmap_super_t);
		/* to 4k blocks */
		bm_blocks = DIV_ROUND_UP_SECTOR_T(bm_blocks, 4096);
		offset = bitmap->mddev->bitmap_info.offset + (bitmap->cluster_slot * (bm_blocks << 3));
		pr_debug("%s:%d bm slot: %d offset: %llu\n", __func__, __LINE__,
			bitmap->cluster_slot, offset);
	}

	if (bitmap->storage.file) {
		loff_t isize = i_size_read(bitmap->storage.file->f_mapping->host);
		int bytes = isize > PAGE_SIZE ? PAGE_SIZE : isize;

		err = read_page(bitmap->storage.file, 0,
				bitmap, bytes, sb_page);
	} else {
		err = read_sb_page(bitmap->mddev,
				   offset,
				   sb_page,
				   0, sizeof(bitmap_super_t));
	}
	if (err)
		return err;

	err = -EINVAL;
	sb = kmap_atomic(sb_page);

	chunksize = le32_to_cpu(sb->chunksize);
	daemon_sleep = le32_to_cpu(sb->daemon_sleep) * HZ;
	write_behind = le32_to_cpu(sb->write_behind);
	sectors_reserved = le32_to_cpu(sb->sectors_reserved);
	/* Setup nodes/clustername only if bitmap version is
	 * cluster-compatible
	 */
	if (sb->version == cpu_to_le32(BITMAP_MAJOR_CLUSTERED)) {
		nodes = le32_to_cpu(sb->nodes);
		strlcpy(bitmap->mddev->bitmap_info.cluster_name,
				sb->cluster_name, 64);
	}

	/* verify that the bitmap-specific fields are valid */
	if (sb->magic != cpu_to_le32(BITMAP_MAGIC))
		reason = "bad magic";
	else if (le32_to_cpu(sb->version) < BITMAP_MAJOR_LO ||
		 le32_to_cpu(sb->version) > BITMAP_MAJOR_CLUSTERED)
		reason = "unrecognized superblock version";
	else if (chunksize < 512)
		reason = "bitmap chunksize too small";
	else if (!is_power_of_2(chunksize))
		reason = "bitmap chunksize not a power of 2";
	else if (daemon_sleep < 1 || daemon_sleep > MAX_SCHEDULE_TIMEOUT)
		reason = "daemon sleep period out of range";
	else if (write_behind > COUNTER_MAX)
		reason = "write-behind limit out of range (0 - 16383)";
	if (reason) {
		pr_warn("%s: invalid bitmap file superblock: %s\n",
			bmname(bitmap), reason);
		goto out;
	}

	/* keep the array size field of the bitmap superblock up to date */
	sb->sync_size = cpu_to_le64(bitmap->mddev->resync_max_sectors);

	if (bitmap->mddev->persistent) {
		/*
		 * We have a persistent array superblock, so compare the
		 * bitmap's UUID and event counter to the mddev's
		 */
		if (memcmp(sb->uuid, bitmap->mddev->uuid, 16)) {
			pr_warn("%s: bitmap superblock UUID mismatch\n",
				bmname(bitmap));
			goto out;
		}
		events = le64_to_cpu(sb->events);
		if (!nodes && (events < bitmap->mddev->events)) {
			pr_warn("%s: bitmap file is out of date (%llu < %llu) -- forcing full recovery\n",
				bmname(bitmap), events,
				(unsigned long long) bitmap->mddev->events);
			set_bit(BITMAP_STALE, &bitmap->flags);
		}
	}

	/* assign fields using values from superblock */
	bitmap->flags |= le32_to_cpu(sb->state);
	if (le32_to_cpu(sb->version) == BITMAP_MAJOR_HOSTENDIAN)
		set_bit(BITMAP_HOSTENDIAN, &bitmap->flags);
	bitmap->events_cleared = le64_to_cpu(sb->events_cleared);
	strlcpy(bitmap->mddev->bitmap_info.cluster_name, sb->cluster_name, 64);
	err = 0;

out:
	kunmap_atomic(sb);
	/* Assigning chunksize is required for "re_read" */
	bitmap->mddev->bitmap_info.chunksize = chunksize;
	if (err == 0 && nodes && (bitmap->cluster_slot < 0)) {
		err = md_setup_cluster(bitmap->mddev, nodes);
		if (err) {
			pr_warn("%s: Could not setup cluster service (%d)\n",
				bmname(bitmap), err);
			goto out_no_sb;
		}
		bitmap->cluster_slot = md_cluster_ops->slot_number(bitmap->mddev);
		goto re_read;
	}


out_no_sb:
	if (test_bit(BITMAP_STALE, &bitmap->flags))
		bitmap->events_cleared = bitmap->mddev->events;
	bitmap->mddev->bitmap_info.chunksize = chunksize;
	bitmap->mddev->bitmap_info.daemon_sleep = daemon_sleep;
	bitmap->mddev->bitmap_info.max_write_behind = write_behind;
	bitmap->mddev->bitmap_info.nodes = nodes;
	if (bitmap->mddev->bitmap_info.space == 0 ||
	    bitmap->mddev->bitmap_info.space > sectors_reserved)
		bitmap->mddev->bitmap_info.space = sectors_reserved;
	if (err) {
		md_bitmap_print_sb(bitmap);
		if (bitmap->cluster_slot < 0)
			md_cluster_stop(bitmap->mddev);
	}
	return err;
}

/*
 * general bitmap file operations
 */

/*
 * on-disk bitmap:
 *
 * Use one bit per "chunk" (block set). We do the disk I/O on the bitmap
 * file a page at a time. There's a superblock at the start of the file.
 */
/* calculate the index of the page that contains this bit */
static inline unsigned long file_page_index(struct bitmap_storage *store,
					    unsigned long chunk)
{
	if (store->sb_page)
		chunk += sizeof(bitmap_super_t) << 3;
	return chunk >> PAGE_BIT_SHIFT;
}

/* calculate the (bit) offset of this bit within a page */
static inline unsigned long file_page_offset(struct bitmap_storage *store,
					     unsigned long chunk)
{
	if (store->sb_page)
		chunk += sizeof(bitmap_super_t) << 3;
	return chunk & (PAGE_BITS - 1);
}

/*
 * return a pointer to the page in the filemap that contains the given bit
 *
 */
static inline struct page *filemap_get_page(struct bitmap_storage *store,
					    unsigned long chunk)
{
	if (file_page_index(store, chunk) >= store->file_pages)
		return NULL;
	return store->filemap[file_page_index(store, chunk)];
}

static int md_bitmap_storage_alloc(struct bitmap_storage *store,
				   unsigned long chunks, int with_super,
				   int slot_number)
{
	int pnum, offset = 0;
	unsigned long num_pages;
	unsigned long bytes;

	bytes = DIV_ROUND_UP(chunks, 8);
	if (with_super)
		bytes += sizeof(bitmap_super_t);

	num_pages = DIV_ROUND_UP(bytes, PAGE_SIZE);
	offset = slot_number * num_pages;

	store->filemap = kmalloc_array(num_pages, sizeof(struct page *),
				       GFP_KERNEL);
	if (!store->filemap)
		return -ENOMEM;

	if (with_super && !store->sb_page) {
		store->sb_page = alloc_page(GFP_KERNEL|__GFP_ZERO);
		if (store->sb_page == NULL)
			return -ENOMEM;
	}

	pnum = 0;
	if (store->sb_page) {
		store->filemap[0] = store->sb_page;
		pnum = 1;
		store->sb_page->index = offset;
	}

	for ( ; pnum < num_pages; pnum++) {
		store->filemap[pnum] = alloc_page(GFP_KERNEL|__GFP_ZERO);
		if (!store->filemap[pnum]) {
			store->file_pages = pnum;
			return -ENOMEM;
		}
		store->filemap[pnum]->index = pnum + offset;
	}
	store->file_pages = pnum;

	/* We need 4 bits per page, rounded up to a multiple
	 * of sizeof(unsigned long) */
	store->filemap_attr = kzalloc(
		roundup(DIV_ROUND_UP(num_pages*4, 8), sizeof(unsigned long)),
		GFP_KERNEL);
	if (!store->filemap_attr)
		return -ENOMEM;

	store->bytes = bytes;

	return 0;
}

static void md_bitmap_file_unmap(struct bitmap_storage *store)
{
	struct page **map, *sb_page;
	int pages;
	struct file *file;

	file = store->file;
	map = store->filemap;
	pages = store->file_pages;
	sb_page = store->sb_page;

	while (pages--)
		if (map[pages] != sb_page) /* 0 is sb_page, release it below */
			free_buffers(map[pages]);
	kfree(map);
	kfree(store->filemap_attr);

	if (sb_page)
		free_buffers(sb_page);

	if (file) {
		struct inode *inode = file_inode(file);
		invalidate_mapping_pages(inode->i_mapping, 0, -1);
		fput(file);
	}
}

/*
 * bitmap_file_kick - if an error occurs while manipulating the bitmap file
 * then it is no longer reliable, so we stop using it and we mark the file
 * as failed in the superblock
 */
static void md_bitmap_file_kick(struct bitmap *bitmap)
{
	char *path, *ptr = NULL;

	if (!test_and_set_bit(BITMAP_STALE, &bitmap->flags)) {
		md_bitmap_update_sb(bitmap);

		if (bitmap->storage.file) {
			path = kmalloc(PAGE_SIZE, GFP_KERNEL);
			if (path)
				ptr = file_path(bitmap->storage.file,
					     path, PAGE_SIZE);

			pr_warn("%s: kicking failed bitmap file %s from array!\n",
				bmname(bitmap), IS_ERR(ptr) ? "" : ptr);

			kfree(path);
		} else
			pr_warn("%s: disabling internal bitmap due to errors\n",
				bmname(bitmap));
	}
}

enum bitmap_page_attr {
	BITMAP_PAGE_DIRTY = 0,     /* there are set bits that need to be synced */
	BITMAP_PAGE_PENDING = 1,   /* there are bits that are being cleaned.
				    * i.e. counter is 1 or 2. */
	BITMAP_PAGE_NEEDWRITE = 2, /* there are cleared bits that need to be synced */
};

static inline void set_page_attr(struct bitmap *bitmap, int pnum,
				 enum bitmap_page_attr attr)
{
	set_bit((pnum<<2) + attr, bitmap->storage.filemap_attr);
}

static inline void clear_page_attr(struct bitmap *bitmap, int pnum,
				   enum bitmap_page_attr attr)
{
	clear_bit((pnum<<2) + attr, bitmap->storage.filemap_attr);
}

static inline int test_page_attr(struct bitmap *bitmap, int pnum,
				 enum bitmap_page_attr attr)
{
	return test_bit((pnum<<2) + attr, bitmap->storage.filemap_attr);
}

static inline int test_and_clear_page_attr(struct bitmap *bitmap, int pnum,
					   enum bitmap_page_attr attr)
{
	return test_and_clear_bit((pnum<<2) + attr,
				  bitmap->storage.filemap_attr);
}
/*
 * bitmap_file_set_bit -- called before performing a write to the md device
 * to set (and eventually sync) a particular bit in the bitmap file
 *
 * we set the bit immediately, then we record the page number so that
 * when an unplug occurs, we can flush the dirty pages out to disk
 */
static void md_bitmap_file_set_bit(struct bitmap *bitmap, sector_t block)
{
	unsigned long bit;
	struct page *page;
	void *kaddr;
	unsigned long chunk = block >> bitmap->counts.chunkshift;
	struct bitmap_storage *store = &bitmap->storage;
	unsigned long node_offset = 0;

	if (mddev_is_clustered(bitmap->mddev))
		node_offset = bitmap->cluster_slot * store->file_pages;

	page = filemap_get_page(&bitmap->storage, chunk);
	if (!page)
		return;
	bit = file_page_offset(&bitmap->storage, chunk);

	/* set the bit */
	kaddr = kmap_atomic(page);
	if (test_bit(BITMAP_HOSTENDIAN, &bitmap->flags))
		set_bit(bit, kaddr);
	else
		set_bit_le(bit, kaddr);
	kunmap_atomic(kaddr);
	pr_debug("set file bit %lu page %lu\n", bit, page->index);
	/* record page number so it gets flushed to disk when unplug occurs */
	set_page_attr(bitmap, page->index - node_offset, BITMAP_PAGE_DIRTY);
}

static void md_bitmap_file_clear_bit(struct bitmap *bitmap, sector_t block)
{
	unsigned long bit;
	struct page *page;
	void *paddr;
	unsigned long chunk = block >> bitmap->counts.chunkshift;
	struct bitmap_storage *store = &bitmap->storage;
	unsigned long node_offset = 0;

	if (mddev_is_clustered(bitmap->mddev))
		node_offset = bitmap->cluster_slot * store->file_pages;

	page = filemap_get_page(&bitmap->storage, chunk);
	if (!page)
		return;
	bit = file_page_offset(&bitmap->storage, chunk);
	paddr = kmap_atomic(page);
	if (test_bit(BITMAP_HOSTENDIAN, &bitmap->flags))
		clear_bit(bit, paddr);
	else
		clear_bit_le(bit, paddr);
	kunmap_atomic(paddr);
	if (!test_page_attr(bitmap, page->index - node_offset, BITMAP_PAGE_NEEDWRITE)) {
		set_page_attr(bitmap, page->index - node_offset, BITMAP_PAGE_PENDING);
		bitmap->allclean = 0;
	}
}

static int md_bitmap_file_test_bit(struct bitmap *bitmap, sector_t block)
{
	unsigned long bit;
	struct page *page;
	void *paddr;
	unsigned long chunk = block >> bitmap->counts.chunkshift;
	int set = 0;

	page = filemap_get_page(&bitmap->storage, chunk);
	if (!page)
		return -EINVAL;
	bit = file_page_offset(&bitmap->storage, chunk);
	paddr = kmap_atomic(page);
	if (test_bit(BITMAP_HOSTENDIAN, &bitmap->flags))
		set = test_bit(bit, paddr);
	else
		set = test_bit_le(bit, paddr);
	kunmap_atomic(paddr);
	return set;
}


/* this gets called when the md device is ready to unplug its underlying
 * (slave) device queues -- before we let any writes go down, we need to
 * sync the dirty pages of the bitmap file to disk */
void md_bitmap_unplug(struct bitmap *bitmap)
{
	unsigned long i;
	int dirty, need_write;
	int writing = 0;

	if (!bitmap || !bitmap->storage.filemap ||
	    test_bit(BITMAP_STALE, &bitmap->flags))
		return;

	/* look at each page to see if there are any set bits that need to be
	 * flushed out to disk */
	for (i = 0; i < bitmap->storage.file_pages; i++) {
		dirty = test_and_clear_page_attr(bitmap, i, BITMAP_PAGE_DIRTY);
		need_write = test_and_clear_page_attr(bitmap, i,
						      BITMAP_PAGE_NEEDWRITE);
		if (dirty || need_write) {
			if (!writing) {
				md_bitmap_wait_writes(bitmap);
				if (bitmap->mddev->queue)
					blk_add_trace_msg(bitmap->mddev->queue,
							  "md bitmap_unplug");
			}
			clear_page_attr(bitmap, i, BITMAP_PAGE_PENDING);
			write_page(bitmap, bitmap->storage.filemap[i], 0);
			writing = 1;
		}
	}
	if (writing)
		md_bitmap_wait_writes(bitmap);

	if (test_bit(BITMAP_WRITE_ERROR, &bitmap->flags))
		md_bitmap_file_kick(bitmap);
}
EXPORT_SYMBOL(md_bitmap_unplug);

static void md_bitmap_set_memory_bits(struct bitmap *bitmap, sector_t offset, int needed);
/* * bitmap_init_from_disk -- called at bitmap_create time to initialize
 * the in-memory bitmap from the on-disk bitmap -- also, sets up the
 * memory mapping of the bitmap file
 * Special cases:
 *   if there's no bitmap file, or if the bitmap file had been
 *   previously kicked from the array, we mark all the bits as
 *   1's in order to cause a full resync.
 *
 * We ignore all bits for sectors that end earlier than 'start'.
 * This is used when reading an out-of-date bitmap...
 */
static int md_bitmap_init_from_disk(struct bitmap *bitmap, sector_t start)
{
	unsigned long i, chunks, index, oldindex, bit, node_offset = 0;
	struct page *page = NULL;
	unsigned long bit_cnt = 0;
	struct file *file;
	unsigned long offset;
	int outofdate;
	int ret = -ENOSPC;
	void *paddr;
	struct bitmap_storage *store = &bitmap->storage;

	chunks = bitmap->counts.chunks;
	file = store->file;

	if (!file && !bitmap->mddev->bitmap_info.offset) {
		/* No permanent bitmap - fill with '1s'. */
		store->filemap = NULL;
		store->file_pages = 0;
		for (i = 0; i < chunks ; i++) {
			/* if the disk bit is set, set the memory bit */
			int needed = ((sector_t)(i+1) << (bitmap->counts.chunkshift)
				      >= start);
			md_bitmap_set_memory_bits(bitmap,
						  (sector_t)i << bitmap->counts.chunkshift,
						  needed);
		}
		return 0;
	}

	outofdate = test_bit(BITMAP_STALE, &bitmap->flags);
	if (outofdate)
		pr_warn("%s: bitmap file is out of date, doing full recovery\n", bmname(bitmap));

	if (file && i_size_read(file->f_mapping->host) < store->bytes) {
		pr_warn("%s: bitmap file too short %lu < %lu\n",
			bmname(bitmap),
			(unsigned long) i_size_read(file->f_mapping->host),
			store->bytes);
		goto err;
	}

	oldindex = ~0L;
	offset = 0;
	if (!bitmap->mddev->bitmap_info.external)
		offset = sizeof(bitmap_super_t);

	if (mddev_is_clustered(bitmap->mddev))
		node_offset = bitmap->cluster_slot * (DIV_ROUND_UP(store->bytes, PAGE_SIZE));

	for (i = 0; i < chunks; i++) {
		int b;
		index = file_page_index(&bitmap->storage, i);
		bit = file_page_offset(&bitmap->storage, i);
		if (index != oldindex) { /* this is a new page, read it in */
			int count;
			/* unmap the old page, we're done with it */
			if (index == store->file_pages-1)
				count = store->bytes - index * PAGE_SIZE;
			else
				count = PAGE_SIZE;
			page = store->filemap[index];
			if (file)
				ret = read_page(file, index, bitmap,
						count, page);
			else
				ret = read_sb_page(
					bitmap->mddev,
					bitmap->mddev->bitmap_info.offset,
					page,
					index + node_offset, count);

			if (ret)
				goto err;

			oldindex = index;

			if (outofdate) {
				/*
				 * if bitmap is out of date, dirty the
				 * whole page and write it out
				 */
				paddr = kmap_atomic(page);
				memset(paddr + offset, 0xff,
				       PAGE_SIZE - offset);
				kunmap_atomic(paddr);
				write_page(bitmap, page, 1);

				ret = -EIO;
				if (test_bit(BITMAP_WRITE_ERROR,
					     &bitmap->flags))
					goto err;
			}
		}
		paddr = kmap_atomic(page);
		if (test_bit(BITMAP_HOSTENDIAN, &bitmap->flags))
			b = test_bit(bit, paddr);
		else
			b = test_bit_le(bit, paddr);
		kunmap_atomic(paddr);
		if (b) {
			/* if the disk bit is set, set the memory bit */
			int needed = ((sector_t)(i+1) << bitmap->counts.chunkshift
				      >= start);
			md_bitmap_set_memory_bits(bitmap,
						  (sector_t)i << bitmap->counts.chunkshift,
						  needed);
			bit_cnt++;
		}
		offset = 0;
	}

	pr_debug("%s: bitmap initialized from disk: read %lu pages, set %lu of %lu bits\n",
		 bmname(bitmap), store->file_pages,
		 bit_cnt, chunks);

	return 0;

 err:
	pr_warn("%s: bitmap initialisation failed: %d\n",
		bmname(bitmap), ret);
	return ret;
}

void md_bitmap_write_all(struct bitmap *bitmap)
{
	/* We don't actually write all bitmap blocks here,
	 * just flag them as needing to be written
	 */
	int i;

	if (!bitmap || !bitmap->storage.filemap)
		return;
	if (bitmap->storage.file)
		/* Only one copy, so nothing needed */
		return;

	for (i = 0; i < bitmap->storage.file_pages; i++)
		set_page_attr(bitmap, i,
			      BITMAP_PAGE_NEEDWRITE);
	bitmap->allclean = 0;
}

static void md_bitmap_count_page(struct bitmap_counts *bitmap,
				 sector_t offset, int inc)
{
	sector_t chunk = offset >> bitmap->chunkshift;
	unsigned long page = chunk >> PAGE_COUNTER_SHIFT;
	bitmap->bp[page].count += inc;
	md_bitmap_checkfree(bitmap, page);
}

static void md_bitmap_set_pending(struct bitmap_counts *bitmap, sector_t offset)
{
	sector_t chunk = offset >> bitmap->chunkshift;
	unsigned long page = chunk >> PAGE_COUNTER_SHIFT;
	struct bitmap_page *bp = &bitmap->bp[page];

	if (!bp->pending)
		bp->pending = 1;
}

static bitmap_counter_t *md_bitmap_get_counter(struct bitmap_counts *bitmap,
					       sector_t offset, sector_t *blocks,
					       int create);

/*
 * bitmap daemon -- periodically wakes up to clean bits and flush pages
 *			out to disk
 */

void md_bitmap_daemon_work(struct mddev *mddev)
{
	struct bitmap *bitmap;
	unsigned long j;
	unsigned long nextpage;
	sector_t blocks;
	struct bitmap_counts *counts;

	/* Use a mutex to guard daemon_work against
	 * bitmap_destroy.
	 */
	mutex_lock(&mddev->bitmap_info.mutex);
	bitmap = mddev->bitmap;
	if (bitmap == NULL) {
		mutex_unlock(&mddev->bitmap_info.mutex);
		return;
	}
	if (time_before(jiffies, bitmap->daemon_lastrun
			+ mddev->bitmap_info.daemon_sleep))
		goto done;

	bitmap->daemon_lastrun = jiffies;
	if (bitmap->allclean) {
		mddev->thread->timeout = MAX_SCHEDULE_TIMEOUT;
		goto done;
	}
	bitmap->allclean = 1;

	if (bitmap->mddev->queue)
		blk_add_trace_msg(bitmap->mddev->queue,
				  "md bitmap_daemon_work");

	/* Any file-page which is PENDING now needs to be written.
	 * So set NEEDWRITE now, then after we make any last-minute changes
	 * we will write it.
	 */
	for (j = 0; j < bitmap->storage.file_pages; j++)
		if (test_and_clear_page_attr(bitmap, j,
					     BITMAP_PAGE_PENDING))
			set_page_attr(bitmap, j,
				      BITMAP_PAGE_NEEDWRITE);

	if (bitmap->need_sync &&
	    mddev->bitmap_info.external == 0) {
		/* Arrange for superblock update as well as
		 * other changes */
		bitmap_super_t *sb;
		bitmap->need_sync = 0;
		if (bitmap->storage.filemap) {
			sb = kmap_atomic(bitmap->storage.sb_page);
			sb->events_cleared =
				cpu_to_le64(bitmap->events_cleared);
			kunmap_atomic(sb);
			set_page_attr(bitmap, 0,
				      BITMAP_PAGE_NEEDWRITE);
		}
	}
	/* Now look at the bitmap counters and if any are '2' or '1',
	 * decrement and handle accordingly.
	 */
	counts = &bitmap->counts;
	spin_lock_irq(&counts->lock);
	nextpage = 0;
	for (j = 0; j < counts->chunks; j++) {
		bitmap_counter_t *bmc;
		sector_t  block = (sector_t)j << counts->chunkshift;

		if (j == nextpage) {
			nextpage += PAGE_COUNTER_RATIO;
			if (!counts->bp[j >> PAGE_COUNTER_SHIFT].pending) {
				j |= PAGE_COUNTER_MASK;
				continue;
			}
			counts->bp[j >> PAGE_COUNTER_SHIFT].pending = 0;
		}

		bmc = md_bitmap_get_counter(counts, block, &blocks, 0);
		if (!bmc) {
			j |= PAGE_COUNTER_MASK;
			continue;
		}
		if (*bmc == 1 && !bitmap->need_sync) {
			/* We can clear the bit */
			*bmc = 0;
			md_bitmap_count_page(counts, block, -1);
			md_bitmap_file_clear_bit(bitmap, block);
		} else if (*bmc && *bmc <= 2) {
			*bmc = 1;
			md_bitmap_set_pending(counts, block);
			bitmap->allclean = 0;
		}
	}
	spin_unlock_irq(&counts->lock);

	md_bitmap_wait_writes(bitmap);
	/* Now start writeout on any page in NEEDWRITE that isn't DIRTY.
	 * DIRTY pages need to be written by bitmap_unplug so it can wait
	 * for them.
	 * If we find any DIRTY page we stop there and let bitmap_unplug
	 * handle all the rest.  This is important in the case where
	 * the first blocking holds the superblock and it has been updated.
	 * We mustn't write any other blocks before the superblock.
	 */
	for (j = 0;
	     j < bitmap->storage.file_pages
		     && !test_bit(BITMAP_STALE, &bitmap->flags);
	     j++) {
		if (test_page_attr(bitmap, j,
				   BITMAP_PAGE_DIRTY))
			/* bitmap_unplug will handle the rest */
			break;
		if (bitmap->storage.filemap &&
		    test_and_clear_page_attr(bitmap, j,
					     BITMAP_PAGE_NEEDWRITE)) {
			write_page(bitmap, bitmap->storage.filemap[j], 0);
		}
	}

 done:
	if (bitmap->allclean == 0)
		mddev->thread->timeout =
			mddev->bitmap_info.daemon_sleep;
	mutex_unlock(&mddev->bitmap_info.mutex);
}

static bitmap_counter_t *md_bitmap_get_counter(struct bitmap_counts *bitmap,
					       sector_t offset, sector_t *blocks,
					       int create)
__releases(bitmap->lock)
__acquires(bitmap->lock)
{
	/* If 'create', we might release the lock and reclaim it.
	 * The lock must have been taken with interrupts enabled.
	 * If !create, we don't release the lock.
	 */
	sector_t chunk = offset >> bitmap->chunkshift;
	unsigned long page = chunk >> PAGE_COUNTER_SHIFT;
	unsigned long pageoff = (chunk & PAGE_COUNTER_MASK) << COUNTER_BYTE_SHIFT;
	sector_t csize;
	int err;

	err = md_bitmap_checkpage(bitmap, page, create, 0);

	if (bitmap->bp[page].hijacked ||
	    bitmap->bp[page].map == NULL)
		csize = ((sector_t)1) << (bitmap->chunkshift +
					  PAGE_COUNTER_SHIFT - 1);
	else
		csize = ((sector_t)1) << bitmap->chunkshift;
	*blocks = csize - (offset & (csize - 1));

	if (err < 0)
		return NULL;

	/* now locked ... */

	if (bitmap->bp[page].hijacked) { /* hijacked pointer */
		/* should we use the first or second counter field
		 * of the hijacked pointer? */
		int hi = (pageoff > PAGE_COUNTER_MASK);
		return  &((bitmap_counter_t *)
			  &bitmap->bp[page].map)[hi];
	} else /* page is allocated */
		return (bitmap_counter_t *)
			&(bitmap->bp[page].map[pageoff]);
}

int md_bitmap_startwrite(struct bitmap *bitmap, sector_t offset, unsigned long sectors, int behind)
{
	if (!bitmap)
		return 0;

	if (behind) {
		int bw;
		atomic_inc(&bitmap->behind_writes);
		bw = atomic_read(&bitmap->behind_writes);
		if (bw > bitmap->behind_writes_used)
			bitmap->behind_writes_used = bw;

		pr_debug("inc write-behind count %d/%lu\n",
			 bw, bitmap->mddev->bitmap_info.max_write_behind);
	}

	while (sectors) {
		sector_t blocks;
		bitmap_counter_t *bmc;

		spin_lock_irq(&bitmap->counts.lock);
		bmc = md_bitmap_get_counter(&bitmap->counts, offset, &blocks, 1);
		if (!bmc) {
			spin_unlock_irq(&bitmap->counts.lock);
			return 0;
		}

		if (unlikely(COUNTER(*bmc) == COUNTER_MAX)) {
			DEFINE_WAIT(__wait);
			/* note that it is safe to do the prepare_to_wait
			 * after the test as long as we do it before dropping
			 * the spinlock.
			 */
			prepare_to_wait(&bitmap->overflow_wait, &__wait,
					TASK_UNINTERRUPTIBLE);
			spin_unlock_irq(&bitmap->counts.lock);
			schedule();
			finish_wait(&bitmap->overflow_wait, &__wait);
			continue;
		}

		switch (*bmc) {
		case 0:
			md_bitmap_file_set_bit(bitmap, offset);
			md_bitmap_count_page(&bitmap->counts, offset, 1);
			/* fall through */
		case 1:
			*bmc = 2;
		}

		(*bmc)++;

		spin_unlock_irq(&bitmap->counts.lock);

		offset += blocks;
		if (sectors > blocks)
			sectors -= blocks;
		else
			sectors = 0;
	}
	return 0;
}
EXPORT_SYMBOL(md_bitmap_startwrite);

void md_bitmap_endwrite(struct bitmap *bitmap, sector_t offset,
			unsigned long sectors, int success, int behind)
{
	if (!bitmap)
		return;
	if (behind) {
		if (atomic_dec_and_test(&bitmap->behind_writes))
			wake_up(&bitmap->behind_wait);
		pr_debug("dec write-behind count %d/%lu\n",
			 atomic_read(&bitmap->behind_writes),
			 bitmap->mddev->bitmap_info.max_write_behind);
	}

	while (sectors) {
		sector_t blocks;
		unsigned long flags;
		bitmap_counter_t *bmc;

		spin_lock_irqsave(&bitmap->counts.lock, flags);
		bmc = md_bitmap_get_counter(&bitmap->counts, offset, &blocks, 0);
		if (!bmc) {
			spin_unlock_irqrestore(&bitmap->counts.lock, flags);
			return;
		}

		if (success && !bitmap->mddev->degraded &&
		    bitmap->events_cleared < bitmap->mddev->events) {
			bitmap->events_cleared = bitmap->mddev->events;
			bitmap->need_sync = 1;
			sysfs_notify_dirent_safe(bitmap->sysfs_can_clear);
		}

		if (!success && !NEEDED(*bmc))
			*bmc |= NEEDED_MASK;

		if (COUNTER(*bmc) == COUNTER_MAX)
			wake_up(&bitmap->overflow_wait);

		(*bmc)--;
		if (*bmc <= 2) {
			md_bitmap_set_pending(&bitmap->counts, offset);
			bitmap->allclean = 0;
		}
		spin_unlock_irqrestore(&bitmap->counts.lock, flags);
		offset += blocks;
		if (sectors > blocks)
			sectors -= blocks;
		else
			sectors = 0;
	}
}
EXPORT_SYMBOL(md_bitmap_endwrite);

static int __bitmap_start_sync(struct bitmap *bitmap, sector_t offset, sector_t *blocks,
			       int degraded)
{
	bitmap_counter_t *bmc;
	int rv;
	if (bitmap == NULL) {/* FIXME or bitmap set as 'failed' */
		*blocks = 1024;
		return 1; /* always resync if no bitmap */
	}
	spin_lock_irq(&bitmap->counts.lock);
	bmc = md_bitmap_get_counter(&bitmap->counts, offset, blocks, 0);
	rv = 0;
	if (bmc) {
		/* locked */
		if (RESYNC(*bmc))
			rv = 1;
		else if (NEEDED(*bmc)) {
			rv = 1;
			if (!degraded) { /* don't set/clear bits if degraded */
				*bmc |= RESYNC_MASK;
				*bmc &= ~NEEDED_MASK;
			}
		}
	}
	spin_unlock_irq(&bitmap->counts.lock);
	return rv;
}

int md_bitmap_start_sync(struct bitmap *bitmap, sector_t offset, sector_t *blocks,
			 int degraded)
{
	/* bitmap_start_sync must always report on multiples of whole
	 * pages, otherwise resync (which is very PAGE_SIZE based) will
	 * get confused.
	 * So call __bitmap_start_sync repeatedly (if needed) until
	 * At least PAGE_SIZE>>9 blocks are covered.
	 * Return the 'or' of the result.
	 */
	int rv = 0;
	sector_t blocks1;

	*blocks = 0;
	while (*blocks < (PAGE_SIZE>>9)) {
		rv |= __bitmap_start_sync(bitmap, offset,
					  &blocks1, degraded);
		offset += blocks1;
		*blocks += blocks1;
	}
	return rv;
}
EXPORT_SYMBOL(md_bitmap_start_sync);

void md_bitmap_end_sync(struct bitmap *bitmap, sector_t offset, sector_t *blocks, int aborted)
{
	bitmap_counter_t *bmc;
	unsigned long flags;

	if (bitmap == NULL) {
		*blocks = 1024;
		return;
	}
	spin_lock_irqsave(&bitmap->counts.lock, flags);
	bmc = md_bitmap_get_counter(&bitmap->counts, offset, blocks, 0);
	if (bmc == NULL)
		goto unlock;
	/* locked */
	if (RESYNC(*bmc)) {
		*bmc &= ~RESYNC_MASK;

		if (!NEEDED(*bmc) && aborted)
			*bmc |= NEEDED_MASK;
		else {
			if (*bmc <= 2) {
				md_bitmap_set_pending(&bitmap->counts, offset);
				bitmap->allclean = 0;
			}
		}
	}
 unlock:
	spin_unlock_irqrestore(&bitmap->counts.lock, flags);
}
EXPORT_SYMBOL(md_bitmap_end_sync);

void md_bitmap_close_sync(struct bitmap *bitmap)
{
	/* Sync has finished, and any bitmap chunks that weren't synced
	 * properly have been aborted.  It remains to us to clear the
	 * RESYNC bit wherever it is still on
	 */
	sector_t sector = 0;
	sector_t blocks;
	if (!bitmap)
		return;
	while (sector < bitmap->mddev->resync_max_sectors) {
		md_bitmap_end_sync(bitmap, sector, &blocks, 0);
		sector += blocks;
	}
}
EXPORT_SYMBOL(md_bitmap_close_sync);

void md_bitmap_cond_end_sync(struct bitmap *bitmap, sector_t sector, bool force)
{
	sector_t s = 0;
	sector_t blocks;

	if (!bitmap)
		return;
	if (sector == 0) {
		bitmap->last_end_sync = jiffies;
		return;
	}
	if (!force && time_before(jiffies, (bitmap->last_end_sync
				  + bitmap->mddev->bitmap_info.daemon_sleep)))
		return;
	wait_event(bitmap->mddev->recovery_wait,
		   atomic_read(&bitmap->mddev->recovery_active) == 0);

	bitmap->mddev->curr_resync_completed = sector;
	set_bit(MD_SB_CHANGE_CLEAN, &bitmap->mddev->sb_flags);
	sector &= ~((1ULL << bitmap->counts.chunkshift) - 1);
	s = 0;
	while (s < sector && s < bitmap->mddev->resync_max_sectors) {
		md_bitmap_end_sync(bitmap, s, &blocks, 0);
		s += blocks;
	}
	bitmap->last_end_sync = jiffies;
	sysfs_notify(&bitmap->mddev->kobj, NULL, "sync_completed");
}
EXPORT_SYMBOL(md_bitmap_cond_end_sync);

void md_bitmap_sync_with_cluster(struct mddev *mddev,
			      sector_t old_lo, sector_t old_hi,
			      sector_t new_lo, sector_t new_hi)
{
	struct bitmap *bitmap = mddev->bitmap;
	sector_t sector, blocks = 0;

	for (sector = old_lo; sector < new_lo; ) {
		md_bitmap_end_sync(bitmap, sector, &blocks, 0);
		sector += blocks;
	}
	WARN((blocks > new_lo) && old_lo, "alignment is not correct for lo\n");

	for (sector = old_hi; sector < new_hi; ) {
		md_bitmap_start_sync(bitmap, sector, &blocks, 0);
		sector += blocks;
	}
	WARN((blocks > new_hi) && old_hi, "alignment is not correct for hi\n");
}
EXPORT_SYMBOL(md_bitmap_sync_with_cluster);

static void md_bitmap_set_memory_bits(struct bitmap *bitmap, sector_t offset, int needed)
{
	/* For each chunk covered by any of these sectors, set the
	 * counter to 2 and possibly set resync_needed.  They should all
	 * be 0 at this point
	 */

	sector_t secs;
	bitmap_counter_t *bmc;
	spin_lock_irq(&bitmap->counts.lock);
	bmc = md_bitmap_get_counter(&bitmap->counts, offset, &secs, 1);
	if (!bmc) {
		spin_unlock_irq(&bitmap->counts.lock);
		return;
	}
	if (!*bmc) {
		*bmc = 2;
		md_bitmap_count_page(&bitmap->counts, offset, 1);
		md_bitmap_set_pending(&bitmap->counts, offset);
		bitmap->allclean = 0;
	}
	if (needed)
		*bmc |= NEEDED_MASK;
	spin_unlock_irq(&bitmap->counts.lock);
}

/* dirty the memory and file bits for bitmap chunks "s" to "e" */
void md_bitmap_dirty_bits(struct bitmap *bitmap, unsigned long s, unsigned long e)
{
	unsigned long chunk;

	for (chunk = s; chunk <= e; chunk++) {
		sector_t sec = (sector_t)chunk << bitmap->counts.chunkshift;
		md_bitmap_set_memory_bits(bitmap, sec, 1);
		md_bitmap_file_set_bit(bitmap, sec);
		if (sec < bitmap->mddev->recovery_cp)
			/* We are asserting that the array is dirty,
			 * so move the recovery_cp address back so
			 * that it is obvious that it is dirty
			 */
			bitmap->mddev->recovery_cp = sec;
	}
}

/*
 * flush out any pending updates
 */
void md_bitmap_flush(struct mddev *mddev)
{
	struct bitmap *bitmap = mddev->bitmap;
	long sleep;

	if (!bitmap) /* there was no bitmap */
		return;

	/* run the daemon_work three time to ensure everything is flushed
	 * that can be
	 */
	sleep = mddev->bitmap_info.daemon_sleep * 2;
	bitmap->daemon_lastrun -= sleep;
	md_bitmap_daemon_work(mddev);
	bitmap->daemon_lastrun -= sleep;
	md_bitmap_daemon_work(mddev);
	bitmap->daemon_lastrun -= sleep;
	md_bitmap_daemon_work(mddev);
	md_bitmap_update_sb(bitmap);
}

/*
 * free memory that was allocated
 */
void md_bitmap_free(struct bitmap *bitmap)
{
	unsigned long k, pages;
	struct bitmap_page *bp;

	if (!bitmap) /* there was no bitmap */
		return;

	if (bitmap->sysfs_can_clear)
		sysfs_put(bitmap->sysfs_can_clear);

	if (mddev_is_clustered(bitmap->mddev) && bitmap->mddev->cluster_info &&
		bitmap->cluster_slot == md_cluster_ops->slot_number(bitmap->mddev))
		md_cluster_stop(bitmap->mddev);

	/* Shouldn't be needed - but just in case.... */
	wait_event(bitmap->write_wait,
		   atomic_read(&bitmap->pending_writes) == 0);

	/* release the bitmap file  */
	md_bitmap_file_unmap(&bitmap->storage);

	bp = bitmap->counts.bp;
	pages = bitmap->counts.pages;

	/* free all allocated memory */

	if (bp) /* deallocate the page memory */
		for (k = 0; k < pages; k++)
			if (bp[k].map && !bp[k].hijacked)
				kfree(bp[k].map);
	kfree(bp);
	kfree(bitmap);
}
EXPORT_SYMBOL(md_bitmap_free);

void md_bitmap_wait_behind_writes(struct mddev *mddev)
{
	struct bitmap *bitmap = mddev->bitmap;

	/* wait for behind writes to complete */
	if (bitmap && atomic_read(&bitmap->behind_writes) > 0) {
		pr_debug("md:%s: behind writes in progress - waiting to stop.\n",
			 mdname(mddev));
		/* need to kick something here to make sure I/O goes? */
		wait_event(bitmap->behind_wait,
			   atomic_read(&bitmap->behind_writes) == 0);
	}
}

void md_bitmap_destroy(struct mddev *mddev)
{
	struct bitmap *bitmap = mddev->bitmap;

	if (!bitmap) /* there was no bitmap */
		return;

	md_bitmap_wait_behind_writes(mddev);
	if (!mddev->serialize_policy)
		mddev_destroy_serial_pool(mddev, NULL, true);

	mutex_lock(&mddev->bitmap_info.mutex);
	spin_lock(&mddev->lock);
	mddev->bitmap = NULL; /* disconnect from the md device */
	spin_unlock(&mddev->lock);
	mutex_unlock(&mddev->bitmap_info.mutex);
	if (mddev->thread)
		mddev->thread->timeout = MAX_SCHEDULE_TIMEOUT;

	md_bitmap_free(bitmap);
}

/*
 * initialize the bitmap structure
 * if this returns an error, bitmap_destroy must be called to do clean up
 * once mddev->bitmap is set
 */
struct bitmap *md_bitmap_create(struct mddev *mddev, int slot)
{
	struct bitmap *bitmap;
	sector_t blocks = mddev->resync_max_sectors;
	struct file *file = mddev->bitmap_info.file;
	int err;
	struct kernfs_node *bm = NULL;

	BUILD_BUG_ON(sizeof(bitmap_super_t) != 256);

	BUG_ON(file && mddev->bitmap_info.offset);

	if (test_bit(MD_HAS_JOURNAL, &mddev->flags)) {
		pr_notice("md/raid:%s: array with journal cannot have bitmap\n",
			  mdname(mddev));
		return ERR_PTR(-EBUSY);
	}

	bitmap = kzalloc(sizeof(*bitmap), GFP_KERNEL);
	if (!bitmap)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&bitmap->counts.lock);
	atomic_set(&bitmap->pending_writes, 0);
	init_waitqueue_head(&bitmap->write_wait);
	init_waitqueue_head(&bitmap->overflow_wait);
	init_waitqueue_head(&bitmap->behind_wait);

	bitmap->mddev = mddev;
	bitmap->cluster_slot = slot;

	if (mddev->kobj.sd)
		bm = sysfs_get_dirent(mddev->kobj.sd, "bitmap");
	if (bm) {
		bitmap->sysfs_can_clear = sysfs_get_dirent(bm, "can_clear");
		sysfs_put(bm);
	} else
		bitmap->sysfs_can_clear = NULL;

	bitmap->storage.file = file;
	if (file) {
		get_file(file);
		/* As future accesses to this file will use bmap,
		 * and bypass the page cache, we must sync the file
		 * first.
		 */
		vfs_fsync(file, 1);
	}
	/* read superblock from bitmap file (this sets mddev->bitmap_info.chunksize) */
	if (!mddev->bitmap_info.external) {
		/*
		 * If 'MD_ARRAY_FIRST_USE' is set, then device-mapper is
		 * instructing us to create a new on-disk bitmap instance.
		 */
		if (test_and_clear_bit(MD_ARRAY_FIRST_USE, &mddev->flags))
			err = md_bitmap_new_disk_sb(bitmap);
		else
			err = md_bitmap_read_sb(bitmap);
	} else {
		err = 0;
		if (mddev->bitmap_info.chunksize == 0 ||
		    mddev->bitmap_info.daemon_sleep == 0)
			/* chunksize and time_base need to be
			 * set first. */
			err = -EINVAL;
	}
	if (err)
		goto error;

	bitmap->daemon_lastrun = jiffies;
	err = md_bitmap_resize(bitmap, blocks, mddev->bitmap_info.chunksize, 1);
	if (err)
		goto error;

	pr_debug("created bitmap (%lu pages) for device %s\n",
		 bitmap->counts.pages, bmname(bitmap));

	err = test_bit(BITMAP_WRITE_ERROR, &bitmap->flags) ? -EIO : 0;
	if (err)
		goto error;

	return bitmap;
 error:
	md_bitmap_free(bitmap);
	return ERR_PTR(err);
}

int md_bitmap_load(struct mddev *mddev)
{
	int err = 0;
	sector_t start = 0;
	sector_t sector = 0;
	struct bitmap *bitmap = mddev->bitmap;
	struct md_rdev *rdev;

	if (!bitmap)
		goto out;

	rdev_for_each(rdev, mddev)
		mddev_create_serial_pool(mddev, rdev, true);

	if (mddev_is_clustered(mddev))
		md_cluster_ops->load_bitmaps(mddev, mddev->bitmap_info.nodes);

	/* Clear out old bitmap info first:  Either there is none, or we
	 * are resuming after someone else has possibly changed things,
	 * so we should forget old cached info.
	 * All chunks should be clean, but some might need_sync.
	 */
	while (sector < mddev->resync_max_sectors) {
		sector_t blocks;
		md_bitmap_start_sync(bitmap, sector, &blocks, 0);
		sector += blocks;
	}
	md_bitmap_close_sync(bitmap);

	if (mddev->degraded == 0
	    || bitmap->events_cleared == mddev->events)
		/* no need to keep dirty bits to optimise a
		 * re-add of a missing device */
		start = mddev->recovery_cp;

	mutex_lock(&mddev->bitmap_info.mutex);
	err = md_bitmap_init_from_disk(bitmap, start);
	mutex_unlock(&mddev->bitmap_info.mutex);

	if (err)
		goto out;
	clear_bit(BITMAP_STALE, &bitmap->flags);

	/* Kick recovery in case any bits were set */
	set_bit(MD_RECOVERY_NEEDED, &bitmap->mddev->recovery);

	mddev->thread->timeout = mddev->bitmap_info.daemon_sleep;
	md_wakeup_thread(mddev->thread);

	md_bitmap_update_sb(bitmap);

	if (test_bit(BITMAP_WRITE_ERROR, &bitmap->flags))
		err = -EIO;
out:
	return err;
}
EXPORT_SYMBOL_GPL(md_bitmap_load);

struct bitmap *get_bitmap_from_slot(struct mddev *mddev, int slot)
{
	int rv = 0;
	struct bitmap *bitmap;

	bitmap = md_bitmap_create(mddev, slot);
	if (IS_ERR(bitmap)) {
		rv = PTR_ERR(bitmap);
		return ERR_PTR(rv);
	}

	rv = md_bitmap_init_from_disk(bitmap, 0);
	if (rv) {
		md_bitmap_free(bitmap);
		return ERR_PTR(rv);
	}

	return bitmap;
}
EXPORT_SYMBOL(get_bitmap_from_slot);

/* Loads the bitmap associated with slot and copies the resync information
 * to our bitmap
 */
int md_bitmap_copy_from_slot(struct mddev *mddev, int slot,
		sector_t *low, sector_t *high, bool clear_bits)
{
	int rv = 0, i, j;
	sector_t block, lo = 0, hi = 0;
	struct bitmap_counts *counts;
	struct bitmap *bitmap;

	bitmap = get_bitmap_from_slot(mddev, slot);
	if (IS_ERR(bitmap)) {
		pr_err("%s can't get bitmap from slot %d\n", __func__, slot);
		return -1;
	}

	counts = &bitmap->counts;
	for (j = 0; j < counts->chunks; j++) {
		block = (sector_t)j << counts->chunkshift;
		if (md_bitmap_file_test_bit(bitmap, block)) {
			if (!lo)
				lo = block;
			hi = block;
			md_bitmap_file_clear_bit(bitmap, block);
			md_bitmap_set_memory_bits(mddev->bitmap, block, 1);
			md_bitmap_file_set_bit(mddev->bitmap, block);
		}
	}

	if (clear_bits) {
		md_bitmap_update_sb(bitmap);
		/* BITMAP_PAGE_PENDING is set, but bitmap_unplug needs
		 * BITMAP_PAGE_DIRTY or _NEEDWRITE to write ... */
		for (i = 0; i < bitmap->storage.file_pages; i++)
			if (test_page_attr(bitmap, i, BITMAP_PAGE_PENDING))
				set_page_attr(bitmap, i, BITMAP_PAGE_NEEDWRITE);
		md_bitmap_unplug(bitmap);
	}
	md_bitmap_unplug(mddev->bitmap);
	*low = lo;
	*high = hi;

	return rv;
}
EXPORT_SYMBOL_GPL(md_bitmap_copy_from_slot);


void md_bitmap_status(struct seq_file *seq, struct bitmap *bitmap)
{
	unsigned long chunk_kb;
	struct bitmap_counts *counts;

	if (!bitmap)
		return;

	counts = &bitmap->counts;

	chunk_kb = bitmap->mddev->bitmap_info.chunksize >> 10;
	seq_printf(seq, "bitmap: %lu/%lu pages [%luKB], "
		   "%lu%s chunk",
		   counts->pages - counts->missing_pages,
		   counts->pages,
		   (counts->pages - counts->missing_pages)
		   << (PAGE_SHIFT - 10),
		   chunk_kb ? chunk_kb : bitmap->mddev->bitmap_info.chunksize,
		   chunk_kb ? "KB" : "B");
	if (bitmap->storage.file) {
		seq_printf(seq, ", file: ");
		seq_file_path(seq, bitmap->storage.file, " \t\n");
	}

	seq_printf(seq, "\n");
}

int md_bitmap_resize(struct bitmap *bitmap, sector_t blocks,
		  int chunksize, int init)
{
	/* If chunk_size is 0, choose an appropriate chunk size.
	 * Then possibly allocate new storage space.
	 * Then quiesce, copy bits, replace bitmap, and re-start
	 *
	 * This function is called both to set up the initial bitmap
	 * and to resize the bitmap while the array is active.
	 * If this happens as a result of the array being resized,
	 * chunksize will be zero, and we need to choose a suitable
	 * chunksize, otherwise we use what we are given.
	 */
	struct bitmap_storage store;
	struct bitmap_counts old_counts;
	unsigned long chunks;
	sector_t block;
	sector_t old_blocks, new_blocks;
	int chunkshift;
	int ret = 0;
	long pages;
	struct bitmap_page *new_bp;

	if (bitmap->storage.file && !init) {
		pr_info("md: cannot resize file-based bitmap\n");
		return -EINVAL;
	}

	if (chunksize == 0) {
		/* If there is enough space, leave the chunk size unchanged,
		 * else increase by factor of two until there is enough space.
		 */
		long bytes;
		long space = bitmap->mddev->bitmap_info.space;

		if (space == 0) {
			/* We don't know how much space there is, so limit
			 * to current size - in sectors.
			 */
			bytes = DIV_ROUND_UP(bitmap->counts.chunks, 8);
			if (!bitmap->mddev->bitmap_info.external)
				bytes += sizeof(bitmap_super_t);
			space = DIV_ROUND_UP(bytes, 512);
			bitmap->mddev->bitmap_info.space = space;
		}
		chunkshift = bitmap->counts.chunkshift;
		chunkshift--;
		do {
			/* 'chunkshift' is shift from block size to chunk size */
			chunkshift++;
			chunks = DIV_ROUND_UP_SECTOR_T(blocks, 1 << chunkshift);
			bytes = DIV_ROUND_UP(chunks, 8);
			if (!bitmap->mddev->bitmap_info.external)
				bytes += sizeof(bitmap_super_t);
		} while (bytes > (space << 9));
	} else
		chunkshift = ffz(~chunksize) - BITMAP_BLOCK_SHIFT;

	chunks = DIV_ROUND_UP_SECTOR_T(blocks, 1 << chunkshift);
	memset(&store, 0, sizeof(store));
	if (bitmap->mddev->bitmap_info.offset || bitmap->mddev->bitmap_info.file)
		ret = md_bitmap_storage_alloc(&store, chunks,
					      !bitmap->mddev->bitmap_info.external,
					      mddev_is_clustered(bitmap->mddev)
					      ? bitmap->cluster_slot : 0);
	if (ret) {
		md_bitmap_file_unmap(&store);
		goto err;
	}

	pages = DIV_ROUND_UP(chunks, PAGE_COUNTER_RATIO);

	new_bp = kcalloc(pages, sizeof(*new_bp), GFP_KERNEL);
	ret = -ENOMEM;
	if (!new_bp) {
		md_bitmap_file_unmap(&store);
		goto err;
	}

	if (!init)
		bitmap->mddev->pers->quiesce(bitmap->mddev, 1);

	store.file = bitmap->storage.file;
	bitmap->storage.file = NULL;

	if (store.sb_page && bitmap->storage.sb_page)
		memcpy(page_address(store.sb_page),
		       page_address(bitmap->storage.sb_page),
		       sizeof(bitmap_super_t));
	spin_lock_irq(&bitmap->counts.lock);
	md_bitmap_file_unmap(&bitmap->storage);
	bitmap->storage = store;

	old_counts = bitmap->counts;
	bitmap->counts.bp = new_bp;
	bitmap->counts.pages = pages;
	bitmap->counts.missing_pages = pages;
	bitmap->counts.chunkshift = chunkshift;
	bitmap->counts.chunks = chunks;
	bitmap->mddev->bitmap_info.chunksize = 1 << (chunkshift +
						     BITMAP_BLOCK_SHIFT);

	blocks = min(old_counts.chunks << old_counts.chunkshift,
		     chunks << chunkshift);

	/* For cluster raid, need to pre-allocate bitmap */
	if (mddev_is_clustered(bitmap->mddev)) {
		unsigned long page;
		for (page = 0; page < pages; page++) {
			ret = md_bitmap_checkpage(&bitmap->counts, page, 1, 1);
			if (ret) {
				unsigned long k;

				/* deallocate the page memory */
				for (k = 0; k < page; k++) {
					kfree(new_bp[k].map);
				}
				kfree(new_bp);

				/* restore some fields from old_counts */
				bitmap->counts.bp = old_counts.bp;
				bitmap->counts.pages = old_counts.pages;
				bitmap->counts.missing_pages = old_counts.pages;
				bitmap->counts.chunkshift = old_counts.chunkshift;
				bitmap->counts.chunks = old_counts.chunks;
				bitmap->mddev->bitmap_info.chunksize = 1 << (old_counts.chunkshift +
									     BITMAP_BLOCK_SHIFT);
				blocks = old_counts.chunks << old_counts.chunkshift;
				pr_warn("Could not pre-allocate in-memory bitmap for cluster raid\n");
				break;
			} else
				bitmap->counts.bp[page].count += 1;
		}
	}

	for (block = 0; block < blocks; ) {
		bitmap_counter_t *bmc_old, *bmc_new;
		int set;

		bmc_old = md_bitmap_get_counter(&old_counts, block, &old_blocks, 0);
		set = bmc_old && NEEDED(*bmc_old);

		if (set) {
			bmc_new = md_bitmap_get_counter(&bitmap->counts, block, &new_blocks, 1);
			if (*bmc_new == 0) {
				/* need to set on-disk bits too. */
				sector_t end = block + new_blocks;
				sector_t start = block >> chunkshift;
				start <<= chunkshift;
				while (start < end) {
					md_bitmap_file_set_bit(bitmap, block);
					start += 1 << chunkshift;
				}
				*bmc_new = 2;
				md_bitmap_count_page(&bitmap->counts, block, 1);
				md_bitmap_set_pending(&bitmap->counts, block);
			}
			*bmc_new |= NEEDED_MASK;
			if (new_blocks < old_blocks)
				old_blocks = new_blocks;
		}
		block += old_blocks;
	}

	if (bitmap->counts.bp != old_counts.bp) {
		unsigned long k;
		for (k = 0; k < old_counts.pages; k++)
			if (!old_counts.bp[k].hijacked)
				kfree(old_counts.bp[k].map);
		kfree(old_counts.bp);
	}

	if (!init) {
		int i;
		while (block < (chunks << chunkshift)) {
			bitmap_counter_t *bmc;
			bmc = md_bitmap_get_counter(&bitmap->counts, block, &new_blocks, 1);
			if (bmc) {
				/* new space.  It needs to be resynced, so
				 * we set NEEDED_MASK.
				 */
				if (*bmc == 0) {
					*bmc = NEEDED_MASK | 2;
					md_bitmap_count_page(&bitmap->counts, block, 1);
					md_bitmap_set_pending(&bitmap->counts, block);
				}
			}
			block += new_blocks;
		}
		for (i = 0; i < bitmap->storage.file_pages; i++)
			set_page_attr(bitmap, i, BITMAP_PAGE_DIRTY);
	}
	spin_unlock_irq(&bitmap->counts.lock);

	if (!init) {
		md_bitmap_unplug(bitmap);
		bitmap->mddev->pers->quiesce(bitmap->mddev, 0);
	}
	ret = 0;
err:
	return ret;
}
EXPORT_SYMBOL_GPL(md_bitmap_resize);

static ssize_t
location_show(struct mddev *mddev, char *page)
{
	ssize_t len;
	if (mddev->bitmap_info.file)
		len = sprintf(page, "file");
	else if (mddev->bitmap_info.offset)
		len = sprintf(page, "%+lld", (long long)mddev->bitmap_info.offset);
	else
		len = sprintf(page, "none");
	len += sprintf(page+len, "\n");
	return len;
}

static ssize_t
location_store(struct mddev *mddev, const char *buf, size_t len)
{
	int rv;

	rv = mddev_lock(mddev);
	if (rv)
		return rv;
	if (mddev->pers) {
		if (!mddev->pers->quiesce) {
			rv = -EBUSY;
			goto out;
		}
		if (mddev->recovery || mddev->sync_thread) {
			rv = -EBUSY;
			goto out;
		}
	}

	if (mddev->bitmap || mddev->bitmap_info.file ||
	    mddev->bitmap_info.offset) {
		/* bitmap already configured.  Only option is to clear it */
		if (strncmp(buf, "none", 4) != 0) {
			rv = -EBUSY;
			goto out;
		}
		if (mddev->pers) {
			mddev_suspend(mddev);
			md_bitmap_destroy(mddev);
			mddev_resume(mddev);
		}
		mddev->bitmap_info.offset = 0;
		if (mddev->bitmap_info.file) {
			struct file *f = mddev->bitmap_info.file;
			mddev->bitmap_info.file = NULL;
			fput(f);
		}
	} else {
		/* No bitmap, OK to set a location */
		long long offset;
		if (strncmp(buf, "none", 4) == 0)
			/* nothing to be done */;
		else if (strncmp(buf, "file:", 5) == 0) {
			/* Not supported yet */
			rv = -EINVAL;
			goto out;
		} else {
			if (buf[0] == '+')
				rv = kstrtoll(buf+1, 10, &offset);
			else
				rv = kstrtoll(buf, 10, &offset);
			if (rv)
				goto out;
			if (offset == 0) {
				rv = -EINVAL;
				goto out;
			}
			if (mddev->bitmap_info.external == 0 &&
			    mddev->major_version == 0 &&
			    offset != mddev->bitmap_info.default_offset) {
				rv = -EINVAL;
				goto out;
			}
			mddev->bitmap_info.offset = offset;
			if (mddev->pers) {
				struct bitmap *bitmap;
				bitmap = md_bitmap_create(mddev, -1);
				mddev_suspend(mddev);
				if (IS_ERR(bitmap))
					rv = PTR_ERR(bitmap);
				else {
					mddev->bitmap = bitmap;
					rv = md_bitmap_load(mddev);
					if (rv)
						mddev->bitmap_info.offset = 0;
				}
				if (rv) {
					md_bitmap_destroy(mddev);
					mddev_resume(mddev);
					goto out;
				}
				mddev_resume(mddev);
			}
		}
	}
	if (!mddev->external) {
		/* Ensure new bitmap info is stored in
		 * metadata promptly.
		 */
		set_bit(MD_SB_CHANGE_DEVS, &mddev->sb_flags);
		md_wakeup_thread(mddev->thread);
	}
	rv = 0;
out:
	mddev_unlock(mddev);
	if (rv)
		return rv;
	return len;
}

static struct md_sysfs_entry bitmap_location =
__ATTR(location, S_IRUGO|S_IWUSR, location_show, location_store);

/* 'bitmap/space' is the space available at 'location' for the
 * bitmap.  This allows the kernel to know when it is safe to
 * resize the bitmap to match a resized array.
 */
static ssize_t
space_show(struct mddev *mddev, char *page)
{
	return sprintf(page, "%lu\n", mddev->bitmap_info.space);
}

static ssize_t
space_store(struct mddev *mddev, const char *buf, size_t len)
{
	unsigned long sectors;
	int rv;

	rv = kstrtoul(buf, 10, &sectors);
	if (rv)
		return rv;

	if (sectors == 0)
		return -EINVAL;

	if (mddev->bitmap &&
	    sectors < (mddev->bitmap->storage.bytes + 511) >> 9)
		return -EFBIG; /* Bitmap is too big for this small space */

	/* could make sure it isn't too big, but that isn't really
	 * needed - user-space should be careful.
	 */
	mddev->bitmap_info.space = sectors;
	return len;
}

static struct md_sysfs_entry bitmap_space =
__ATTR(space, S_IRUGO|S_IWUSR, space_show, space_store);

static ssize_t
timeout_show(struct mddev *mddev, char *page)
{
	ssize_t len;
	unsigned long secs = mddev->bitmap_info.daemon_sleep / HZ;
	unsigned long jifs = mddev->bitmap_info.daemon_sleep % HZ;

	len = sprintf(page, "%lu", secs);
	if (jifs)
		len += sprintf(page+len, ".%03u", jiffies_to_msecs(jifs));
	len += sprintf(page+len, "\n");
	return len;
}

static ssize_t
timeout_store(struct mddev *mddev, const char *buf, size_t len)
{
	/* timeout can be set at any time */
	unsigned long timeout;
	int rv = strict_strtoul_scaled(buf, &timeout, 4);
	if (rv)
		return rv;

	/* just to make sure we don't overflow... */
	if (timeout >= LONG_MAX / HZ)
		return -EINVAL;

	timeout = timeout * HZ / 10000;

	if (timeout >= MAX_SCHEDULE_TIMEOUT)
		timeout = MAX_SCHEDULE_TIMEOUT-1;
	if (timeout < 1)
		timeout = 1;
	mddev->bitmap_info.daemon_sleep = timeout;
	if (mddev->thread) {
		/* if thread->timeout is MAX_SCHEDULE_TIMEOUT, then
		 * the bitmap is all clean and we don't need to
		 * adjust the timeout right now
		 */
		if (mddev->thread->timeout < MAX_SCHEDULE_TIMEOUT) {
			mddev->thread->timeout = timeout;
			md_wakeup_thread(mddev->thread);
		}
	}
	return len;
}

static struct md_sysfs_entry bitmap_timeout =
__ATTR(time_base, S_IRUGO|S_IWUSR, timeout_show, timeout_store);

static ssize_t
backlog_show(struct mddev *mddev, char *page)
{
	return sprintf(page, "%lu\n", mddev->bitmap_info.max_write_behind);
}

static ssize_t
backlog_store(struct mddev *mddev, const char *buf, size_t len)
{
	unsigned long backlog;
	unsigned long old_mwb = mddev->bitmap_info.max_write_behind;
	int rv = kstrtoul(buf, 10, &backlog);
	if (rv)
		return rv;
	if (backlog > COUNTER_MAX)
		return -EINVAL;
	mddev->bitmap_info.max_write_behind = backlog;
	if (!backlog && mddev->serial_info_pool) {
		/* serial_info_pool is not needed if backlog is zero */
		if (!mddev->serialize_policy)
			mddev_destroy_serial_pool(mddev, NULL, false);
	} else if (backlog && !mddev->serial_info_pool) {
		/* serial_info_pool is needed since backlog is not zero */
		struct md_rdev *rdev;

		rdev_for_each(rdev, mddev)
			mddev_create_serial_pool(mddev, rdev, false);
	}
	if (old_mwb != backlog)
		md_bitmap_update_sb(mddev->bitmap);
	return len;
}

static struct md_sysfs_entry bitmap_backlog =
__ATTR(backlog, S_IRUGO|S_IWUSR, backlog_show, backlog_store);

static ssize_t
chunksize_show(struct mddev *mddev, char *page)
{
	return sprintf(page, "%lu\n", mddev->bitmap_info.chunksize);
}

static ssize_t
chunksize_store(struct mddev *mddev, const char *buf, size_t len)
{
	/* Can only be changed when no bitmap is active */
	int rv;
	unsigned long csize;
	if (mddev->bitmap)
		return -EBUSY;
	rv = kstrtoul(buf, 10, &csize);
	if (rv)
		return rv;
	if (csize < 512 ||
	    !is_power_of_2(csize))
		return -EINVAL;
	mddev->bitmap_info.chunksize = csize;
	return len;
}

static struct md_sysfs_entry bitmap_chunksize =
__ATTR(chunksize, S_IRUGO|S_IWUSR, chunksize_show, chunksize_store);

static ssize_t metadata_show(struct mddev *mddev, char *page)
{
	if (mddev_is_clustered(mddev))
		return sprintf(page, "clustered\n");
	return sprintf(page, "%s\n", (mddev->bitmap_info.external
				      ? "external" : "internal"));
}

static ssize_t metadata_store(struct mddev *mddev, const char *buf, size_t len)
{
	if (mddev->bitmap ||
	    mddev->bitmap_info.file ||
	    mddev->bitmap_info.offset)
		return -EBUSY;
	if (strncmp(buf, "external", 8) == 0)
		mddev->bitmap_info.external = 1;
	else if ((strncmp(buf, "internal", 8) == 0) ||
			(strncmp(buf, "clustered", 9) == 0))
		mddev->bitmap_info.external = 0;
	else
		return -EINVAL;
	return len;
}

static struct md_sysfs_entry bitmap_metadata =
__ATTR(metadata, S_IRUGO|S_IWUSR, metadata_show, metadata_store);

static ssize_t can_clear_show(struct mddev *mddev, char *page)
{
	int len;
	spin_lock(&mddev->lock);
	if (mddev->bitmap)
		len = sprintf(page, "%s\n", (mddev->bitmap->need_sync ?
					     "false" : "true"));
	else
		len = sprintf(page, "\n");
	spin_unlock(&mddev->lock);
	return len;
}

static ssize_t can_clear_store(struct mddev *mddev, const char *buf, size_t len)
{
	if (mddev->bitmap == NULL)
		return -ENOENT;
	if (strncmp(buf, "false", 5) == 0)
		mddev->bitmap->need_sync = 1;
	else if (strncmp(buf, "true", 4) == 0) {
		if (mddev->degraded)
			return -EBUSY;
		mddev->bitmap->need_sync = 0;
	} else
		return -EINVAL;
	return len;
}

static struct md_sysfs_entry bitmap_can_clear =
__ATTR(can_clear, S_IRUGO|S_IWUSR, can_clear_show, can_clear_store);

static ssize_t
behind_writes_used_show(struct mddev *mddev, char *page)
{
	ssize_t ret;
	spin_lock(&mddev->lock);
	if (mddev->bitmap == NULL)
		ret = sprintf(page, "0\n");
	else
		ret = sprintf(page, "%lu\n",
			      mddev->bitmap->behind_writes_used);
	spin_unlock(&mddev->lock);
	return ret;
}

static ssize_t
behind_writes_used_reset(struct mddev *mddev, const char *buf, size_t len)
{
	if (mddev->bitmap)
		mddev->bitmap->behind_writes_used = 0;
	return len;
}

static struct md_sysfs_entry max_backlog_used =
__ATTR(max_backlog_used, S_IRUGO | S_IWUSR,
       behind_writes_used_show, behind_writes_used_reset);

static struct attribute *md_bitmap_attrs[] = {
	&bitmap_location.attr,
	&bitmap_space.attr,
	&bitmap_timeout.attr,
	&bitmap_backlog.attr,
	&bitmap_chunksize.attr,
	&bitmap_metadata.attr,
	&bitmap_can_clear.attr,
	&max_backlog_used.attr,
	NULL
};
struct attribute_group md_bitmap_group = {
	.name = "bitmap",
	.attrs = md_bitmap_attrs,
};

