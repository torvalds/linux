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
#include "md.h"
#include "bitmap.h"

#include <linux/dm-dirty-log.h>
/* debug macros */

#define DEBUG 0

#if DEBUG
/* these are for debugging purposes only! */

/* define one and only one of these */
#define INJECT_FAULTS_1 0 /* cause bitmap_alloc_page to fail always */
#define INJECT_FAULTS_2 0 /* cause bitmap file to be kicked when first bit set*/
#define INJECT_FAULTS_3 0 /* treat bitmap file as kicked at init time */
#define INJECT_FAULTS_4 0 /* undef */
#define INJECT_FAULTS_5 0 /* undef */
#define INJECT_FAULTS_6 0

/* if these are defined, the driver will fail! debug only */
#define INJECT_FATAL_FAULT_1 0 /* fail kmalloc, causing bitmap_create to fail */
#define INJECT_FATAL_FAULT_2 0 /* undef */
#define INJECT_FATAL_FAULT_3 0 /* undef */
#endif

#ifndef PRINTK
#  if DEBUG > 0
#    define PRINTK(x...) printk(KERN_DEBUG x)
#  else
#    define PRINTK(x...)
#  endif
#endif

static inline char *bmname(struct bitmap *bitmap)
{
	return bitmap->mddev ? mdname(bitmap->mddev) : "mdX";
}

/*
 * just a placeholder - calls kmalloc for bitmap pages
 */
static unsigned char *bitmap_alloc_page(struct bitmap *bitmap)
{
	unsigned char *page;

#ifdef INJECT_FAULTS_1
	page = NULL;
#else
	page = kzalloc(PAGE_SIZE, GFP_NOIO);
#endif
	if (!page)
		printk("%s: bitmap_alloc_page FAILED\n", bmname(bitmap));
	else
		PRINTK("%s: bitmap_alloc_page: allocated page at %p\n",
			bmname(bitmap), page);
	return page;
}

/*
 * for now just a placeholder -- just calls kfree for bitmap pages
 */
static void bitmap_free_page(struct bitmap *bitmap, unsigned char *page)
{
	PRINTK("%s: bitmap_free_page: free page %p\n", bmname(bitmap), page);
	kfree(page);
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
static int bitmap_checkpage(struct bitmap *bitmap,
			    unsigned long page, int create)
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
	mappage = bitmap_alloc_page(bitmap);
	spin_lock_irq(&bitmap->lock);

	if (mappage == NULL) {
		PRINTK("%s: bitmap map page allocation failed, hijacking\n",
			bmname(bitmap));
		/* failed - set the hijacked flag so that we can use the
		 * pointer as a counter */
		if (!bitmap->bp[page].map)
			bitmap->bp[page].hijacked = 1;
	} else if (bitmap->bp[page].map ||
		   bitmap->bp[page].hijacked) {
		/* somebody beat us to getting the page */
		bitmap_free_page(bitmap, mappage);
		return 0;
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
static void bitmap_checkfree(struct bitmap *bitmap, unsigned long page)
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
		bitmap_free_page(bitmap, ptr);
	}
}

/*
 * bitmap file handling - read and write the bitmap file and its superblock
 */

/*
 * basic page I/O operations
 */

/* IO operations when bitmap is stored near all superblocks */
static struct page *read_sb_page(mddev_t *mddev, loff_t offset,
				 struct page *page,
				 unsigned long index, int size)
{
	/* choose a good rdev and read the page from there */

	mdk_rdev_t *rdev;
	sector_t target;
	int did_alloc = 0;

	if (!page) {
		page = alloc_page(GFP_KERNEL);
		if (!page)
			return ERR_PTR(-ENOMEM);
		did_alloc = 1;
	}

	list_for_each_entry(rdev, &mddev->disks, same_set) {
		if (! test_bit(In_sync, &rdev->flags)
		    || test_bit(Faulty, &rdev->flags))
			continue;

		target = rdev->sb_start + offset + index * (PAGE_SIZE/512);

		if (sync_page_io(rdev, target,
				 roundup(size, bdev_logical_block_size(rdev->bdev)),
				 page, READ)) {
			page->index = index;
			attach_page_buffers(page, NULL); /* so that free_buffer will
							  * quietly no-op */
			return page;
		}
	}
	if (did_alloc)
		put_page(page);
	return ERR_PTR(-EIO);

}

static mdk_rdev_t *next_active_rdev(mdk_rdev_t *rdev, mddev_t *mddev)
{
	/* Iterate the disks of an mddev, using rcu to protect access to the
	 * linked list, and raising the refcount of devices we return to ensure
	 * they don't disappear while in use.
	 * As devices are only added or removed when raid_disk is < 0 and
	 * nr_pending is 0 and In_sync is clear, the entries we return will
	 * still be in the same position on the list when we re-enter
	 * list_for_each_continue_rcu.
	 */
	struct list_head *pos;
	rcu_read_lock();
	if (rdev == NULL)
		/* start at the beginning */
		pos = &mddev->disks;
	else {
		/* release the previous rdev and start from there. */
		rdev_dec_pending(rdev, mddev);
		pos = &rdev->same_set;
	}
	list_for_each_continue_rcu(pos, &mddev->disks) {
		rdev = list_entry(pos, mdk_rdev_t, same_set);
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
	mdk_rdev_t *rdev = NULL;
	mddev_t *mddev = bitmap->mddev;

	while ((rdev = next_active_rdev(rdev, mddev)) != NULL) {
		int size = PAGE_SIZE;
		loff_t offset = mddev->bitmap_info.offset;
		if (page->index == bitmap->file_pages-1)
			size = roundup(bitmap->last_page_size,
				       bdev_logical_block_size(rdev->bdev));
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

	if (wait)
		md_super_wait(mddev);
	return 0;

 bad_alignment:
	return -EINVAL;
}

static void bitmap_file_kick(struct bitmap *bitmap);
/*
 * write out a page to a file
 */
static void write_page(struct bitmap *bitmap, struct page *page, int wait)
{
	struct buffer_head *bh;

	if (bitmap->file == NULL) {
		switch (write_sb_page(bitmap, page, wait)) {
		case -EINVAL:
			bitmap->flags |= BITMAP_WRITE_ERROR;
		}
	} else {

		bh = page_buffers(page);

		while (bh && bh->b_blocknr) {
			atomic_inc(&bitmap->pending_writes);
			set_buffer_locked(bh);
			set_buffer_mapped(bh);
			submit_bh(WRITE | REQ_UNPLUG | REQ_SYNC, bh);
			bh = bh->b_this_page;
		}

		if (wait)
			wait_event(bitmap->write_wait,
				   atomic_read(&bitmap->pending_writes)==0);
	}
	if (bitmap->flags & BITMAP_WRITE_ERROR)
		bitmap_file_kick(bitmap);
}

static void end_bitmap_write(struct buffer_head *bh, int uptodate)
{
	struct bitmap *bitmap = bh->b_private;
	unsigned long flags;

	if (!uptodate) {
		spin_lock_irqsave(&bitmap->lock, flags);
		bitmap->flags |= BITMAP_WRITE_ERROR;
		spin_unlock_irqrestore(&bitmap->lock, flags);
	}
	if (atomic_dec_and_test(&bitmap->pending_writes))
		wake_up(&bitmap->write_wait);
}

/* copied from buffer.c */
static void
__clear_page_buffers(struct page *page)
{
	ClearPagePrivate(page);
	set_page_private(page, 0);
	page_cache_release(page);
}
static void free_buffers(struct page *page)
{
	struct buffer_head *bh = page_buffers(page);

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
static struct page *read_page(struct file *file, unsigned long index,
			      struct bitmap *bitmap,
			      unsigned long count)
{
	struct page *page = NULL;
	struct inode *inode = file->f_path.dentry->d_inode;
	struct buffer_head *bh;
	sector_t block;

	PRINTK("read bitmap file (%dB @ %llu)\n", (int)PAGE_SIZE,
			(unsigned long long)index << PAGE_SHIFT);

	page = alloc_page(GFP_KERNEL);
	if (!page)
		page = ERR_PTR(-ENOMEM);
	if (IS_ERR(page))
		goto out;

	bh = alloc_page_buffers(page, 1<<inode->i_blkbits, 0);
	if (!bh) {
		put_page(page);
		page = ERR_PTR(-ENOMEM);
		goto out;
	}
	attach_page_buffers(page, bh);
	block = index << (PAGE_SHIFT - inode->i_blkbits);
	while (bh) {
		if (count == 0)
			bh->b_blocknr = 0;
		else {
			bh->b_blocknr = bmap(inode, block);
			if (bh->b_blocknr == 0) {
				/* Cannot use this file! */
				free_buffers(page);
				page = ERR_PTR(-EINVAL);
				goto out;
			}
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
			submit_bh(READ, bh);
		}
		block++;
		bh = bh->b_this_page;
	}
	page->index = index;

	wait_event(bitmap->write_wait,
		   atomic_read(&bitmap->pending_writes)==0);
	if (bitmap->flags & BITMAP_WRITE_ERROR) {
		free_buffers(page);
		page = ERR_PTR(-EIO);
	}
out:
	if (IS_ERR(page))
		printk(KERN_ALERT "md: bitmap read error: (%dB @ %llu): %ld\n",
			(int)PAGE_SIZE,
			(unsigned long long)index << PAGE_SHIFT,
			PTR_ERR(page));
	return page;
}

/*
 * bitmap file superblock operations
 */

/* update the event counter and sync the superblock to disk */
void bitmap_update_sb(struct bitmap *bitmap)
{
	bitmap_super_t *sb;
	unsigned long flags;

	if (!bitmap || !bitmap->mddev) /* no bitmap for this array */
		return;
	if (bitmap->mddev->bitmap_info.external)
		return;
	spin_lock_irqsave(&bitmap->lock, flags);
	if (!bitmap->sb_page) { /* no superblock */
		spin_unlock_irqrestore(&bitmap->lock, flags);
		return;
	}
	spin_unlock_irqrestore(&bitmap->lock, flags);
	sb = kmap_atomic(bitmap->sb_page, KM_USER0);
	sb->events = cpu_to_le64(bitmap->mddev->events);
	if (bitmap->mddev->events < bitmap->events_cleared) {
		/* rocking back to read-only */
		bitmap->events_cleared = bitmap->mddev->events;
		sb->events_cleared = cpu_to_le64(bitmap->events_cleared);
	}
	/* Just in case these have been changed via sysfs: */
	sb->daemon_sleep = cpu_to_le32(bitmap->mddev->bitmap_info.daemon_sleep/HZ);
	sb->write_behind = cpu_to_le32(bitmap->mddev->bitmap_info.max_write_behind);
	kunmap_atomic(sb, KM_USER0);
	write_page(bitmap, bitmap->sb_page, 1);
}

/* print out the bitmap file superblock */
void bitmap_print_sb(struct bitmap *bitmap)
{
	bitmap_super_t *sb;

	if (!bitmap || !bitmap->sb_page)
		return;
	sb = kmap_atomic(bitmap->sb_page, KM_USER0);
	printk(KERN_DEBUG "%s: bitmap file superblock:\n", bmname(bitmap));
	printk(KERN_DEBUG "         magic: %08x\n", le32_to_cpu(sb->magic));
	printk(KERN_DEBUG "       version: %d\n", le32_to_cpu(sb->version));
	printk(KERN_DEBUG "          uuid: %08x.%08x.%08x.%08x\n",
					*(__u32 *)(sb->uuid+0),
					*(__u32 *)(sb->uuid+4),
					*(__u32 *)(sb->uuid+8),
					*(__u32 *)(sb->uuid+12));
	printk(KERN_DEBUG "        events: %llu\n",
			(unsigned long long) le64_to_cpu(sb->events));
	printk(KERN_DEBUG "events cleared: %llu\n",
			(unsigned long long) le64_to_cpu(sb->events_cleared));
	printk(KERN_DEBUG "         state: %08x\n", le32_to_cpu(sb->state));
	printk(KERN_DEBUG "     chunksize: %d B\n", le32_to_cpu(sb->chunksize));
	printk(KERN_DEBUG "  daemon sleep: %ds\n", le32_to_cpu(sb->daemon_sleep));
	printk(KERN_DEBUG "     sync size: %llu KB\n",
			(unsigned long long)le64_to_cpu(sb->sync_size)/2);
	printk(KERN_DEBUG "max write behind: %d\n", le32_to_cpu(sb->write_behind));
	kunmap_atomic(sb, KM_USER0);
}

/* read the superblock from the bitmap file and initialize some bitmap fields */
static int bitmap_read_sb(struct bitmap *bitmap)
{
	char *reason = NULL;
	bitmap_super_t *sb;
	unsigned long chunksize, daemon_sleep, write_behind;
	unsigned long long events;
	int err = -EINVAL;

	/* page 0 is the superblock, read it... */
	if (bitmap->file) {
		loff_t isize = i_size_read(bitmap->file->f_mapping->host);
		int bytes = isize > PAGE_SIZE ? PAGE_SIZE : isize;

		bitmap->sb_page = read_page(bitmap->file, 0, bitmap, bytes);
	} else {
		bitmap->sb_page = read_sb_page(bitmap->mddev,
					       bitmap->mddev->bitmap_info.offset,
					       NULL,
					       0, sizeof(bitmap_super_t));
	}
	if (IS_ERR(bitmap->sb_page)) {
		err = PTR_ERR(bitmap->sb_page);
		bitmap->sb_page = NULL;
		return err;
	}

	sb = kmap_atomic(bitmap->sb_page, KM_USER0);

	chunksize = le32_to_cpu(sb->chunksize);
	daemon_sleep = le32_to_cpu(sb->daemon_sleep) * HZ;
	write_behind = le32_to_cpu(sb->write_behind);

	/* verify that the bitmap-specific fields are valid */
	if (sb->magic != cpu_to_le32(BITMAP_MAGIC))
		reason = "bad magic";
	else if (le32_to_cpu(sb->version) < BITMAP_MAJOR_LO ||
		 le32_to_cpu(sb->version) > BITMAP_MAJOR_HI)
		reason = "unrecognized superblock version";
	else if (chunksize < 512)
		reason = "bitmap chunksize too small";
	else if ((1 << ffz(~chunksize)) != chunksize)
		reason = "bitmap chunksize not a power of 2";
	else if (daemon_sleep < 1 || daemon_sleep > MAX_SCHEDULE_TIMEOUT)
		reason = "daemon sleep period out of range";
	else if (write_behind > COUNTER_MAX)
		reason = "write-behind limit out of range (0 - 16383)";
	if (reason) {
		printk(KERN_INFO "%s: invalid bitmap file superblock: %s\n",
			bmname(bitmap), reason);
		goto out;
	}

	/* keep the array size field of the bitmap superblock up to date */
	sb->sync_size = cpu_to_le64(bitmap->mddev->resync_max_sectors);

	if (!bitmap->mddev->persistent)
		goto success;

	/*
	 * if we have a persistent array superblock, compare the
	 * bitmap's UUID and event counter to the mddev's
	 */
	if (memcmp(sb->uuid, bitmap->mddev->uuid, 16)) {
		printk(KERN_INFO "%s: bitmap superblock UUID mismatch\n",
			bmname(bitmap));
		goto out;
	}
	events = le64_to_cpu(sb->events);
	if (events < bitmap->mddev->events) {
		printk(KERN_INFO "%s: bitmap file is out of date (%llu < %llu) "
			"-- forcing full recovery\n", bmname(bitmap), events,
			(unsigned long long) bitmap->mddev->events);
		sb->state |= cpu_to_le32(BITMAP_STALE);
	}
success:
	/* assign fields using values from superblock */
	bitmap->mddev->bitmap_info.chunksize = chunksize;
	bitmap->mddev->bitmap_info.daemon_sleep = daemon_sleep;
	bitmap->mddev->bitmap_info.max_write_behind = write_behind;
	bitmap->flags |= le32_to_cpu(sb->state);
	if (le32_to_cpu(sb->version) == BITMAP_MAJOR_HOSTENDIAN)
		bitmap->flags |= BITMAP_HOSTENDIAN;
	bitmap->events_cleared = le64_to_cpu(sb->events_cleared);
	if (sb->state & cpu_to_le32(BITMAP_STALE))
		bitmap->events_cleared = bitmap->mddev->events;
	err = 0;
out:
	kunmap_atomic(sb, KM_USER0);
	if (err)
		bitmap_print_sb(bitmap);
	return err;
}

enum bitmap_mask_op {
	MASK_SET,
	MASK_UNSET
};

/* record the state of the bitmap in the superblock.  Return the old value */
static int bitmap_mask_state(struct bitmap *bitmap, enum bitmap_state bits,
			     enum bitmap_mask_op op)
{
	bitmap_super_t *sb;
	unsigned long flags;
	int old;

	spin_lock_irqsave(&bitmap->lock, flags);
	if (!bitmap->sb_page) { /* can't set the state */
		spin_unlock_irqrestore(&bitmap->lock, flags);
		return 0;
	}
	spin_unlock_irqrestore(&bitmap->lock, flags);
	sb = kmap_atomic(bitmap->sb_page, KM_USER0);
	old = le32_to_cpu(sb->state) & bits;
	switch (op) {
	case MASK_SET:
		sb->state |= cpu_to_le32(bits);
		break;
	case MASK_UNSET:
		sb->state &= cpu_to_le32(~bits);
		break;
	default:
		BUG();
	}
	kunmap_atomic(sb, KM_USER0);
	return old;
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
static inline unsigned long file_page_index(struct bitmap *bitmap, unsigned long chunk)
{
	if (!bitmap->mddev->bitmap_info.external)
		chunk += sizeof(bitmap_super_t) << 3;
	return chunk >> PAGE_BIT_SHIFT;
}

/* calculate the (bit) offset of this bit within a page */
static inline unsigned long file_page_offset(struct bitmap *bitmap, unsigned long chunk)
{
	if (!bitmap->mddev->bitmap_info.external)
		chunk += sizeof(bitmap_super_t) << 3;
	return chunk & (PAGE_BITS - 1);
}

/*
 * return a pointer to the page in the filemap that contains the given bit
 *
 * this lookup is complicated by the fact that the bitmap sb might be exactly
 * 1 page (e.g., x86) or less than 1 page -- so the bitmap might start on page
 * 0 or page 1
 */
static inline struct page *filemap_get_page(struct bitmap *bitmap,
					unsigned long chunk)
{
	if (bitmap->filemap == NULL)
		return NULL;
	if (file_page_index(bitmap, chunk) >= bitmap->file_pages)
		return NULL;
	return bitmap->filemap[file_page_index(bitmap, chunk)
			       - file_page_index(bitmap, 0)];
}

static void bitmap_file_unmap(struct bitmap *bitmap)
{
	struct page **map, *sb_page;
	unsigned long *attr;
	int pages;
	unsigned long flags;

	spin_lock_irqsave(&bitmap->lock, flags);
	map = bitmap->filemap;
	bitmap->filemap = NULL;
	attr = bitmap->filemap_attr;
	bitmap->filemap_attr = NULL;
	pages = bitmap->file_pages;
	bitmap->file_pages = 0;
	sb_page = bitmap->sb_page;
	bitmap->sb_page = NULL;
	spin_unlock_irqrestore(&bitmap->lock, flags);

	while (pages--)
		if (map[pages] != sb_page) /* 0 is sb_page, release it below */
			free_buffers(map[pages]);
	kfree(map);
	kfree(attr);

	if (sb_page)
		free_buffers(sb_page);
}

static void bitmap_file_put(struct bitmap *bitmap)
{
	struct file *file;
	unsigned long flags;

	spin_lock_irqsave(&bitmap->lock, flags);
	file = bitmap->file;
	bitmap->file = NULL;
	spin_unlock_irqrestore(&bitmap->lock, flags);

	if (file)
		wait_event(bitmap->write_wait,
			   atomic_read(&bitmap->pending_writes)==0);
	bitmap_file_unmap(bitmap);

	if (file) {
		struct inode *inode = file->f_path.dentry->d_inode;
		invalidate_mapping_pages(inode->i_mapping, 0, -1);
		fput(file);
	}
}

/*
 * bitmap_file_kick - if an error occurs while manipulating the bitmap file
 * then it is no longer reliable, so we stop using it and we mark the file
 * as failed in the superblock
 */
static void bitmap_file_kick(struct bitmap *bitmap)
{
	char *path, *ptr = NULL;

	if (bitmap_mask_state(bitmap, BITMAP_STALE, MASK_SET) == 0) {
		bitmap_update_sb(bitmap);

		if (bitmap->file) {
			path = kmalloc(PAGE_SIZE, GFP_KERNEL);
			if (path)
				ptr = d_path(&bitmap->file->f_path, path,
					     PAGE_SIZE);

			printk(KERN_ALERT
			      "%s: kicking failed bitmap file %s from array!\n",
			      bmname(bitmap), IS_ERR(ptr) ? "" : ptr);

			kfree(path);
		} else
			printk(KERN_ALERT
			       "%s: disabling internal bitmap due to errors\n",
			       bmname(bitmap));
	}

	bitmap_file_put(bitmap);

	return;
}

enum bitmap_page_attr {
	BITMAP_PAGE_DIRTY = 0,     /* there are set bits that need to be synced */
	BITMAP_PAGE_CLEAN = 1,     /* there are bits that might need to be cleared */
	BITMAP_PAGE_NEEDWRITE = 2, /* there are cleared bits that need to be synced */
};

static inline void set_page_attr(struct bitmap *bitmap, struct page *page,
				enum bitmap_page_attr attr)
{
	if (page)
		__set_bit((page->index<<2) + attr, bitmap->filemap_attr);
	else
		__set_bit(attr, &bitmap->logattrs);
}

static inline void clear_page_attr(struct bitmap *bitmap, struct page *page,
				enum bitmap_page_attr attr)
{
	if (page)
		__clear_bit((page->index<<2) + attr, bitmap->filemap_attr);
	else
		__clear_bit(attr, &bitmap->logattrs);
}

static inline unsigned long test_page_attr(struct bitmap *bitmap, struct page *page,
					   enum bitmap_page_attr attr)
{
	if (page)
		return test_bit((page->index<<2) + attr, bitmap->filemap_attr);
	else
		return test_bit(attr, &bitmap->logattrs);
}

/*
 * bitmap_file_set_bit -- called before performing a write to the md device
 * to set (and eventually sync) a particular bit in the bitmap file
 *
 * we set the bit immediately, then we record the page number so that
 * when an unplug occurs, we can flush the dirty pages out to disk
 */
static void bitmap_file_set_bit(struct bitmap *bitmap, sector_t block)
{
	unsigned long bit;
	struct page *page = NULL;
	void *kaddr;
	unsigned long chunk = block >> CHUNK_BLOCK_SHIFT(bitmap);

	if (!bitmap->filemap) {
		struct dm_dirty_log *log = bitmap->mddev->bitmap_info.log;
		if (log)
			log->type->mark_region(log, chunk);
	} else {

		page = filemap_get_page(bitmap, chunk);
		if (!page)
			return;
		bit = file_page_offset(bitmap, chunk);

		/* set the bit */
		kaddr = kmap_atomic(page, KM_USER0);
		if (bitmap->flags & BITMAP_HOSTENDIAN)
			set_bit(bit, kaddr);
		else
			ext2_set_bit(bit, kaddr);
		kunmap_atomic(kaddr, KM_USER0);
		PRINTK("set file bit %lu page %lu\n", bit, page->index);
	}
	/* record page number so it gets flushed to disk when unplug occurs */
	set_page_attr(bitmap, page, BITMAP_PAGE_DIRTY);
}

/* this gets called when the md device is ready to unplug its underlying
 * (slave) device queues -- before we let any writes go down, we need to
 * sync the dirty pages of the bitmap file to disk */
void bitmap_unplug(struct bitmap *bitmap)
{
	unsigned long i, flags;
	int dirty, need_write;
	struct page *page;
	int wait = 0;

	if (!bitmap)
		return;
	if (!bitmap->filemap) {
		/* Must be using a dirty_log */
		struct dm_dirty_log *log = bitmap->mddev->bitmap_info.log;
		dirty = test_and_clear_bit(BITMAP_PAGE_DIRTY, &bitmap->logattrs);
		need_write = test_and_clear_bit(BITMAP_PAGE_NEEDWRITE, &bitmap->logattrs);
		if (dirty || need_write)
			if (log->type->flush(log))
				bitmap->flags |= BITMAP_WRITE_ERROR;
		goto out;
	}

	/* look at each page to see if there are any set bits that need to be
	 * flushed out to disk */
	for (i = 0; i < bitmap->file_pages; i++) {
		spin_lock_irqsave(&bitmap->lock, flags);
		if (!bitmap->filemap) {
			spin_unlock_irqrestore(&bitmap->lock, flags);
			return;
		}
		page = bitmap->filemap[i];
		dirty = test_page_attr(bitmap, page, BITMAP_PAGE_DIRTY);
		need_write = test_page_attr(bitmap, page, BITMAP_PAGE_NEEDWRITE);
		clear_page_attr(bitmap, page, BITMAP_PAGE_DIRTY);
		clear_page_attr(bitmap, page, BITMAP_PAGE_NEEDWRITE);
		if (dirty)
			wait = 1;
		spin_unlock_irqrestore(&bitmap->lock, flags);

		if (dirty || need_write)
			write_page(bitmap, page, 0);
	}
	if (wait) { /* if any writes were performed, we need to wait on them */
		if (bitmap->file)
			wait_event(bitmap->write_wait,
				   atomic_read(&bitmap->pending_writes)==0);
		else
			md_super_wait(bitmap->mddev);
	}
out:
	if (bitmap->flags & BITMAP_WRITE_ERROR)
		bitmap_file_kick(bitmap);
}
EXPORT_SYMBOL(bitmap_unplug);

static void bitmap_set_memory_bits(struct bitmap *bitmap, sector_t offset, int needed);
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
static int bitmap_init_from_disk(struct bitmap *bitmap, sector_t start)
{
	unsigned long i, chunks, index, oldindex, bit;
	struct page *page = NULL, *oldpage = NULL;
	unsigned long num_pages, bit_cnt = 0;
	struct file *file;
	unsigned long bytes, offset;
	int outofdate;
	int ret = -ENOSPC;
	void *paddr;

	chunks = bitmap->chunks;
	file = bitmap->file;

	BUG_ON(!file && !bitmap->mddev->bitmap_info.offset);

#ifdef INJECT_FAULTS_3
	outofdate = 1;
#else
	outofdate = bitmap->flags & BITMAP_STALE;
#endif
	if (outofdate)
		printk(KERN_INFO "%s: bitmap file is out of date, doing full "
			"recovery\n", bmname(bitmap));

	bytes = DIV_ROUND_UP(bitmap->chunks, 8);
	if (!bitmap->mddev->bitmap_info.external)
		bytes += sizeof(bitmap_super_t);

	num_pages = DIV_ROUND_UP(bytes, PAGE_SIZE);

	if (file && i_size_read(file->f_mapping->host) < bytes) {
		printk(KERN_INFO "%s: bitmap file too short %lu < %lu\n",
			bmname(bitmap),
			(unsigned long) i_size_read(file->f_mapping->host),
			bytes);
		goto err;
	}

	ret = -ENOMEM;

	bitmap->filemap = kmalloc(sizeof(struct page *) * num_pages, GFP_KERNEL);
	if (!bitmap->filemap)
		goto err;

	/* We need 4 bits per page, rounded up to a multiple of sizeof(unsigned long) */
	bitmap->filemap_attr = kzalloc(
		roundup(DIV_ROUND_UP(num_pages*4, 8), sizeof(unsigned long)),
		GFP_KERNEL);
	if (!bitmap->filemap_attr)
		goto err;

	oldindex = ~0L;

	for (i = 0; i < chunks; i++) {
		int b;
		index = file_page_index(bitmap, i);
		bit = file_page_offset(bitmap, i);
		if (index != oldindex) { /* this is a new page, read it in */
			int count;
			/* unmap the old page, we're done with it */
			if (index == num_pages-1)
				count = bytes - index * PAGE_SIZE;
			else
				count = PAGE_SIZE;
			if (index == 0 && bitmap->sb_page) {
				/*
				 * if we're here then the superblock page
				 * contains some bits (PAGE_SIZE != sizeof sb)
				 * we've already read it in, so just use it
				 */
				page = bitmap->sb_page;
				offset = sizeof(bitmap_super_t);
				if (!file)
					page = read_sb_page(
						bitmap->mddev,
						bitmap->mddev->bitmap_info.offset,
						page,
						index, count);
			} else if (file) {
				page = read_page(file, index, bitmap, count);
				offset = 0;
			} else {
				page = read_sb_page(bitmap->mddev,
						    bitmap->mddev->bitmap_info.offset,
						    NULL,
						    index, count);
				offset = 0;
			}
			if (IS_ERR(page)) { /* read error */
				ret = PTR_ERR(page);
				goto err;
			}

			oldindex = index;
			oldpage = page;

			bitmap->filemap[bitmap->file_pages++] = page;
			bitmap->last_page_size = count;

			if (outofdate) {
				/*
				 * if bitmap is out of date, dirty the
				 * whole page and write it out
				 */
				paddr = kmap_atomic(page, KM_USER0);
				memset(paddr + offset, 0xff,
				       PAGE_SIZE - offset);
				kunmap_atomic(paddr, KM_USER0);
				write_page(bitmap, page, 1);

				ret = -EIO;
				if (bitmap->flags & BITMAP_WRITE_ERROR)
					goto err;
			}
		}
		paddr = kmap_atomic(page, KM_USER0);
		if (bitmap->flags & BITMAP_HOSTENDIAN)
			b = test_bit(bit, paddr);
		else
			b = ext2_test_bit(bit, paddr);
		kunmap_atomic(paddr, KM_USER0);
		if (b) {
			/* if the disk bit is set, set the memory bit */
			int needed = ((sector_t)(i+1) << (CHUNK_BLOCK_SHIFT(bitmap))
				      >= start);
			bitmap_set_memory_bits(bitmap,
					       (sector_t)i << CHUNK_BLOCK_SHIFT(bitmap),
					       needed);
			bit_cnt++;
			set_page_attr(bitmap, page, BITMAP_PAGE_CLEAN);
		}
	}

	/* everything went OK */
	ret = 0;
	bitmap_mask_state(bitmap, BITMAP_STALE, MASK_UNSET);

	if (bit_cnt) { /* Kick recovery if any bits were set */
		set_bit(MD_RECOVERY_NEEDED, &bitmap->mddev->recovery);
		md_wakeup_thread(bitmap->mddev->thread);
	}

	printk(KERN_INFO "%s: bitmap initialized from disk: "
		"read %lu/%lu pages, set %lu bits\n",
		bmname(bitmap), bitmap->file_pages, num_pages, bit_cnt);

	return 0;

 err:
	printk(KERN_INFO "%s: bitmap initialisation failed: %d\n",
	       bmname(bitmap), ret);
	return ret;
}

void bitmap_write_all(struct bitmap *bitmap)
{
	/* We don't actually write all bitmap blocks here,
	 * just flag them as needing to be written
	 */
	int i;

	for (i = 0; i < bitmap->file_pages; i++)
		set_page_attr(bitmap, bitmap->filemap[i],
			      BITMAP_PAGE_NEEDWRITE);
}

static void bitmap_count_page(struct bitmap *bitmap, sector_t offset, int inc)
{
	sector_t chunk = offset >> CHUNK_BLOCK_SHIFT(bitmap);
	unsigned long page = chunk >> PAGE_COUNTER_SHIFT;
	bitmap->bp[page].count += inc;
	bitmap_checkfree(bitmap, page);
}
static bitmap_counter_t *bitmap_get_counter(struct bitmap *bitmap,
					    sector_t offset, sector_t *blocks,
					    int create);

/*
 * bitmap daemon -- periodically wakes up to clean bits and flush pages
 *			out to disk
 */

void bitmap_daemon_work(mddev_t *mddev)
{
	struct bitmap *bitmap;
	unsigned long j;
	unsigned long flags;
	struct page *page = NULL, *lastpage = NULL;
	sector_t blocks;
	void *paddr;
	struct dm_dirty_log *log = mddev->bitmap_info.log;

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
			+ bitmap->mddev->bitmap_info.daemon_sleep))
		goto done;

	bitmap->daemon_lastrun = jiffies;
	if (bitmap->allclean) {
		bitmap->mddev->thread->timeout = MAX_SCHEDULE_TIMEOUT;
		goto done;
	}
	bitmap->allclean = 1;

	spin_lock_irqsave(&bitmap->lock, flags);
	for (j = 0; j < bitmap->chunks; j++) {
		bitmap_counter_t *bmc;
		if (!bitmap->filemap) {
			if (!log)
				/* error or shutdown */
				break;
		} else
			page = filemap_get_page(bitmap, j);

		if (page != lastpage) {
			/* skip this page unless it's marked as needing cleaning */
			if (!test_page_attr(bitmap, page, BITMAP_PAGE_CLEAN)) {
				int need_write = test_page_attr(bitmap, page,
								BITMAP_PAGE_NEEDWRITE);
				if (need_write)
					clear_page_attr(bitmap, page, BITMAP_PAGE_NEEDWRITE);

				spin_unlock_irqrestore(&bitmap->lock, flags);
				if (need_write) {
					write_page(bitmap, page, 0);
					bitmap->allclean = 0;
				}
				spin_lock_irqsave(&bitmap->lock, flags);
				j |= (PAGE_BITS - 1);
				continue;
			}

			/* grab the new page, sync and release the old */
			if (lastpage != NULL) {
				if (test_page_attr(bitmap, lastpage, BITMAP_PAGE_NEEDWRITE)) {
					clear_page_attr(bitmap, lastpage, BITMAP_PAGE_NEEDWRITE);
					spin_unlock_irqrestore(&bitmap->lock, flags);
					write_page(bitmap, lastpage, 0);
				} else {
					set_page_attr(bitmap, lastpage, BITMAP_PAGE_NEEDWRITE);
					spin_unlock_irqrestore(&bitmap->lock, flags);
				}
			} else
				spin_unlock_irqrestore(&bitmap->lock, flags);
			lastpage = page;

			/* We are possibly going to clear some bits, so make
			 * sure that events_cleared is up-to-date.
			 */
			if (bitmap->need_sync &&
			    bitmap->mddev->bitmap_info.external == 0) {
				bitmap_super_t *sb;
				bitmap->need_sync = 0;
				sb = kmap_atomic(bitmap->sb_page, KM_USER0);
				sb->events_cleared =
					cpu_to_le64(bitmap->events_cleared);
				kunmap_atomic(sb, KM_USER0);
				write_page(bitmap, bitmap->sb_page, 1);
			}
			spin_lock_irqsave(&bitmap->lock, flags);
			if (!bitmap->need_sync)
				clear_page_attr(bitmap, page, BITMAP_PAGE_CLEAN);
		}
		bmc = bitmap_get_counter(bitmap,
					 (sector_t)j << CHUNK_BLOCK_SHIFT(bitmap),
					 &blocks, 0);
		if (bmc) {
			if (*bmc)
				bitmap->allclean = 0;

			if (*bmc == 2) {
				*bmc = 1; /* maybe clear the bit next time */
				set_page_attr(bitmap, page, BITMAP_PAGE_CLEAN);
			} else if (*bmc == 1 && !bitmap->need_sync) {
				/* we can clear the bit */
				*bmc = 0;
				bitmap_count_page(bitmap,
						  (sector_t)j << CHUNK_BLOCK_SHIFT(bitmap),
						  -1);

				/* clear the bit */
				if (page) {
					paddr = kmap_atomic(page, KM_USER0);
					if (bitmap->flags & BITMAP_HOSTENDIAN)
						clear_bit(file_page_offset(bitmap, j),
							  paddr);
					else
						ext2_clear_bit(file_page_offset(bitmap, j),
							       paddr);
					kunmap_atomic(paddr, KM_USER0);
				} else
					log->type->clear_region(log, j);
			}
		} else
			j |= PAGE_COUNTER_MASK;
	}
	spin_unlock_irqrestore(&bitmap->lock, flags);

	/* now sync the final page */
	if (lastpage != NULL || log != NULL) {
		spin_lock_irqsave(&bitmap->lock, flags);
		if (test_page_attr(bitmap, lastpage, BITMAP_PAGE_NEEDWRITE)) {
			clear_page_attr(bitmap, lastpage, BITMAP_PAGE_NEEDWRITE);
			spin_unlock_irqrestore(&bitmap->lock, flags);
			if (lastpage)
				write_page(bitmap, lastpage, 0);
			else
				if (log->type->flush(log))
					bitmap->flags |= BITMAP_WRITE_ERROR;
		} else {
			set_page_attr(bitmap, lastpage, BITMAP_PAGE_NEEDWRITE);
			spin_unlock_irqrestore(&bitmap->lock, flags);
		}
	}

 done:
	if (bitmap->allclean == 0)
		bitmap->mddev->thread->timeout =
			bitmap->mddev->bitmap_info.daemon_sleep;
	mutex_unlock(&mddev->bitmap_info.mutex);
}

static bitmap_counter_t *bitmap_get_counter(struct bitmap *bitmap,
					    sector_t offset, sector_t *blocks,
					    int create)
__releases(bitmap->lock)
__acquires(bitmap->lock)
{
	/* If 'create', we might release the lock and reclaim it.
	 * The lock must have been taken with interrupts enabled.
	 * If !create, we don't release the lock.
	 */
	sector_t chunk = offset >> CHUNK_BLOCK_SHIFT(bitmap);
	unsigned long page = chunk >> PAGE_COUNTER_SHIFT;
	unsigned long pageoff = (chunk & PAGE_COUNTER_MASK) << COUNTER_BYTE_SHIFT;
	sector_t csize;
	int err;

	err = bitmap_checkpage(bitmap, page, create);

	if (bitmap->bp[page].hijacked ||
	    bitmap->bp[page].map == NULL)
		csize = ((sector_t)1) << (CHUNK_BLOCK_SHIFT(bitmap) +
					  PAGE_COUNTER_SHIFT - 1);
	else
		csize = ((sector_t)1) << (CHUNK_BLOCK_SHIFT(bitmap));
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

int bitmap_startwrite(struct bitmap *bitmap, sector_t offset, unsigned long sectors, int behind)
{
	if (!bitmap)
		return 0;

	if (behind) {
		int bw;
		atomic_inc(&bitmap->behind_writes);
		bw = atomic_read(&bitmap->behind_writes);
		if (bw > bitmap->behind_writes_used)
			bitmap->behind_writes_used = bw;

		PRINTK(KERN_DEBUG "inc write-behind count %d/%d\n",
		       bw, bitmap->max_write_behind);
	}

	while (sectors) {
		sector_t blocks;
		bitmap_counter_t *bmc;

		spin_lock_irq(&bitmap->lock);
		bmc = bitmap_get_counter(bitmap, offset, &blocks, 1);
		if (!bmc) {
			spin_unlock_irq(&bitmap->lock);
			return 0;
		}

		if (unlikely((*bmc & COUNTER_MAX) == COUNTER_MAX)) {
			DEFINE_WAIT(__wait);
			/* note that it is safe to do the prepare_to_wait
			 * after the test as long as we do it before dropping
			 * the spinlock.
			 */
			prepare_to_wait(&bitmap->overflow_wait, &__wait,
					TASK_UNINTERRUPTIBLE);
			spin_unlock_irq(&bitmap->lock);
			md_unplug(bitmap->mddev);
			schedule();
			finish_wait(&bitmap->overflow_wait, &__wait);
			continue;
		}

		switch (*bmc) {
		case 0:
			bitmap_file_set_bit(bitmap, offset);
			bitmap_count_page(bitmap, offset, 1);
			/* fall through */
		case 1:
			*bmc = 2;
		}

		(*bmc)++;

		spin_unlock_irq(&bitmap->lock);

		offset += blocks;
		if (sectors > blocks)
			sectors -= blocks;
		else
			sectors = 0;
	}
	bitmap->allclean = 0;
	return 0;
}
EXPORT_SYMBOL(bitmap_startwrite);

void bitmap_endwrite(struct bitmap *bitmap, sector_t offset, unsigned long sectors,
		     int success, int behind)
{
	if (!bitmap)
		return;
	if (behind) {
		if (atomic_dec_and_test(&bitmap->behind_writes))
			wake_up(&bitmap->behind_wait);
		PRINTK(KERN_DEBUG "dec write-behind count %d/%d\n",
		  atomic_read(&bitmap->behind_writes), bitmap->max_write_behind);
	}
	if (bitmap->mddev->degraded)
		/* Never clear bits or update events_cleared when degraded */
		success = 0;

	while (sectors) {
		sector_t blocks;
		unsigned long flags;
		bitmap_counter_t *bmc;

		spin_lock_irqsave(&bitmap->lock, flags);
		bmc = bitmap_get_counter(bitmap, offset, &blocks, 0);
		if (!bmc) {
			spin_unlock_irqrestore(&bitmap->lock, flags);
			return;
		}

		if (success &&
		    bitmap->events_cleared < bitmap->mddev->events) {
			bitmap->events_cleared = bitmap->mddev->events;
			bitmap->need_sync = 1;
			sysfs_notify_dirent_safe(bitmap->sysfs_can_clear);
		}

		if (!success && ! (*bmc & NEEDED_MASK))
			*bmc |= NEEDED_MASK;

		if ((*bmc & COUNTER_MAX) == COUNTER_MAX)
			wake_up(&bitmap->overflow_wait);

		(*bmc)--;
		if (*bmc <= 2)
			set_page_attr(bitmap,
				      filemap_get_page(
					      bitmap,
					      offset >> CHUNK_BLOCK_SHIFT(bitmap)),
				      BITMAP_PAGE_CLEAN);

		spin_unlock_irqrestore(&bitmap->lock, flags);
		offset += blocks;
		if (sectors > blocks)
			sectors -= blocks;
		else
			sectors = 0;
	}
}
EXPORT_SYMBOL(bitmap_endwrite);

static int __bitmap_start_sync(struct bitmap *bitmap, sector_t offset, sector_t *blocks,
			       int degraded)
{
	bitmap_counter_t *bmc;
	int rv;
	if (bitmap == NULL) {/* FIXME or bitmap set as 'failed' */
		*blocks = 1024;
		return 1; /* always resync if no bitmap */
	}
	spin_lock_irq(&bitmap->lock);
	bmc = bitmap_get_counter(bitmap, offset, blocks, 0);
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
	spin_unlock_irq(&bitmap->lock);
	bitmap->allclean = 0;
	return rv;
}

int bitmap_start_sync(struct bitmap *bitmap, sector_t offset, sector_t *blocks,
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
EXPORT_SYMBOL(bitmap_start_sync);

void bitmap_end_sync(struct bitmap *bitmap, sector_t offset, sector_t *blocks, int aborted)
{
	bitmap_counter_t *bmc;
	unsigned long flags;

	if (bitmap == NULL) {
		*blocks = 1024;
		return;
	}
	spin_lock_irqsave(&bitmap->lock, flags);
	bmc = bitmap_get_counter(bitmap, offset, blocks, 0);
	if (bmc == NULL)
		goto unlock;
	/* locked */
	if (RESYNC(*bmc)) {
		*bmc &= ~RESYNC_MASK;

		if (!NEEDED(*bmc) && aborted)
			*bmc |= NEEDED_MASK;
		else {
			if (*bmc <= 2)
				set_page_attr(bitmap,
					      filemap_get_page(bitmap, offset >> CHUNK_BLOCK_SHIFT(bitmap)),
					      BITMAP_PAGE_CLEAN);
		}
	}
 unlock:
	spin_unlock_irqrestore(&bitmap->lock, flags);
	bitmap->allclean = 0;
}
EXPORT_SYMBOL(bitmap_end_sync);

void bitmap_close_sync(struct bitmap *bitmap)
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
		bitmap_end_sync(bitmap, sector, &blocks, 0);
		sector += blocks;
	}
}
EXPORT_SYMBOL(bitmap_close_sync);

void bitmap_cond_end_sync(struct bitmap *bitmap, sector_t sector)
{
	sector_t s = 0;
	sector_t blocks;

	if (!bitmap)
		return;
	if (sector == 0) {
		bitmap->last_end_sync = jiffies;
		return;
	}
	if (time_before(jiffies, (bitmap->last_end_sync
				  + bitmap->mddev->bitmap_info.daemon_sleep)))
		return;
	wait_event(bitmap->mddev->recovery_wait,
		   atomic_read(&bitmap->mddev->recovery_active) == 0);

	bitmap->mddev->curr_resync_completed = bitmap->mddev->curr_resync;
	set_bit(MD_CHANGE_CLEAN, &bitmap->mddev->flags);
	sector &= ~((1ULL << CHUNK_BLOCK_SHIFT(bitmap)) - 1);
	s = 0;
	while (s < sector && s < bitmap->mddev->resync_max_sectors) {
		bitmap_end_sync(bitmap, s, &blocks, 0);
		s += blocks;
	}
	bitmap->last_end_sync = jiffies;
	sysfs_notify(&bitmap->mddev->kobj, NULL, "sync_completed");
}
EXPORT_SYMBOL(bitmap_cond_end_sync);

static void bitmap_set_memory_bits(struct bitmap *bitmap, sector_t offset, int needed)
{
	/* For each chunk covered by any of these sectors, set the
	 * counter to 1 and set resync_needed.  They should all
	 * be 0 at this point
	 */

	sector_t secs;
	bitmap_counter_t *bmc;
	spin_lock_irq(&bitmap->lock);
	bmc = bitmap_get_counter(bitmap, offset, &secs, 1);
	if (!bmc) {
		spin_unlock_irq(&bitmap->lock);
		return;
	}
	if (!*bmc) {
		struct page *page;
		*bmc = 1 | (needed ? NEEDED_MASK : 0);
		bitmap_count_page(bitmap, offset, 1);
		page = filemap_get_page(bitmap, offset >> CHUNK_BLOCK_SHIFT(bitmap));
		set_page_attr(bitmap, page, BITMAP_PAGE_CLEAN);
	}
	spin_unlock_irq(&bitmap->lock);
	bitmap->allclean = 0;
}

/* dirty the memory and file bits for bitmap chunks "s" to "e" */
void bitmap_dirty_bits(struct bitmap *bitmap, unsigned long s, unsigned long e)
{
	unsigned long chunk;

	for (chunk = s; chunk <= e; chunk++) {
		sector_t sec = (sector_t)chunk << CHUNK_BLOCK_SHIFT(bitmap);
		bitmap_set_memory_bits(bitmap, sec, 1);
		bitmap_file_set_bit(bitmap, sec);
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
void bitmap_flush(mddev_t *mddev)
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
	bitmap_daemon_work(mddev);
	bitmap->daemon_lastrun -= sleep;
	bitmap_daemon_work(mddev);
	bitmap->daemon_lastrun -= sleep;
	bitmap_daemon_work(mddev);
	bitmap_update_sb(bitmap);
}

/*
 * free memory that was allocated
 */
static void bitmap_free(struct bitmap *bitmap)
{
	unsigned long k, pages;
	struct bitmap_page *bp;

	if (!bitmap) /* there was no bitmap */
		return;

	/* release the bitmap file and kill the daemon */
	bitmap_file_put(bitmap);

	bp = bitmap->bp;
	pages = bitmap->pages;

	/* free all allocated memory */

	if (bp) /* deallocate the page memory */
		for (k = 0; k < pages; k++)
			if (bp[k].map && !bp[k].hijacked)
				kfree(bp[k].map);
	kfree(bp);
	kfree(bitmap);
}

void bitmap_destroy(mddev_t *mddev)
{
	struct bitmap *bitmap = mddev->bitmap;

	if (!bitmap) /* there was no bitmap */
		return;

	mutex_lock(&mddev->bitmap_info.mutex);
	mddev->bitmap = NULL; /* disconnect from the md device */
	mutex_unlock(&mddev->bitmap_info.mutex);
	if (mddev->thread)
		mddev->thread->timeout = MAX_SCHEDULE_TIMEOUT;

	if (bitmap->sysfs_can_clear)
		sysfs_put(bitmap->sysfs_can_clear);

	bitmap_free(bitmap);
}

/*
 * initialize the bitmap structure
 * if this returns an error, bitmap_destroy must be called to do clean up
 */
int bitmap_create(mddev_t *mddev)
{
	struct bitmap *bitmap;
	sector_t blocks = mddev->resync_max_sectors;
	unsigned long chunks;
	unsigned long pages;
	struct file *file = mddev->bitmap_info.file;
	int err;
	struct sysfs_dirent *bm = NULL;

	BUILD_BUG_ON(sizeof(bitmap_super_t) != 256);

	if (!file
	    && !mddev->bitmap_info.offset
	    && !mddev->bitmap_info.log) /* bitmap disabled, nothing to do */
		return 0;

	BUG_ON(file && mddev->bitmap_info.offset);
	BUG_ON(mddev->bitmap_info.offset && mddev->bitmap_info.log);

	bitmap = kzalloc(sizeof(*bitmap), GFP_KERNEL);
	if (!bitmap)
		return -ENOMEM;

	spin_lock_init(&bitmap->lock);
	atomic_set(&bitmap->pending_writes, 0);
	init_waitqueue_head(&bitmap->write_wait);
	init_waitqueue_head(&bitmap->overflow_wait);
	init_waitqueue_head(&bitmap->behind_wait);

	bitmap->mddev = mddev;

	if (mddev->kobj.sd)
		bm = sysfs_get_dirent(mddev->kobj.sd, NULL, "bitmap");
	if (bm) {
		bitmap->sysfs_can_clear = sysfs_get_dirent(bm, NULL, "can_clear");
		sysfs_put(bm);
	} else
		bitmap->sysfs_can_clear = NULL;

	bitmap->file = file;
	if (file) {
		get_file(file);
		/* As future accesses to this file will use bmap,
		 * and bypass the page cache, we must sync the file
		 * first.
		 */
		vfs_fsync(file, 1);
	}
	/* read superblock from bitmap file (this sets mddev->bitmap_info.chunksize) */
	if (!mddev->bitmap_info.external)
		err = bitmap_read_sb(bitmap);
	else {
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
	bitmap->chunkshift = ffz(~mddev->bitmap_info.chunksize);

	/* now that chunksize and chunkshift are set, we can use these macros */
	chunks = (blocks + CHUNK_BLOCK_RATIO(bitmap) - 1) >>
			CHUNK_BLOCK_SHIFT(bitmap);
	pages = (chunks + PAGE_COUNTER_RATIO - 1) / PAGE_COUNTER_RATIO;

	BUG_ON(!pages);

	bitmap->chunks = chunks;
	bitmap->pages = pages;
	bitmap->missing_pages = pages;
	bitmap->counter_bits = COUNTER_BITS;

	bitmap->syncchunk = ~0UL;

#ifdef INJECT_FATAL_FAULT_1
	bitmap->bp = NULL;
#else
	bitmap->bp = kzalloc(pages * sizeof(*bitmap->bp), GFP_KERNEL);
#endif
	err = -ENOMEM;
	if (!bitmap->bp)
		goto error;

	printk(KERN_INFO "created bitmap (%lu pages) for device %s\n",
		pages, bmname(bitmap));

	mddev->bitmap = bitmap;


	return (bitmap->flags & BITMAP_WRITE_ERROR) ? -EIO : 0;

 error:
	bitmap_free(bitmap);
	return err;
}

int bitmap_load(mddev_t *mddev)
{
	int err = 0;
	sector_t sector = 0;
	struct bitmap *bitmap = mddev->bitmap;

	if (!bitmap)
		goto out;

	/* Clear out old bitmap info first:  Either there is none, or we
	 * are resuming after someone else has possibly changed things,
	 * so we should forget old cached info.
	 * All chunks should be clean, but some might need_sync.
	 */
	while (sector < mddev->resync_max_sectors) {
		sector_t blocks;
		bitmap_start_sync(bitmap, sector, &blocks, 0);
		sector += blocks;
	}
	bitmap_close_sync(bitmap);

	if (mddev->bitmap_info.log) {
		unsigned long i;
		struct dm_dirty_log *log = mddev->bitmap_info.log;
		for (i = 0; i < bitmap->chunks; i++)
			if (!log->type->in_sync(log, i, 1))
				bitmap_set_memory_bits(bitmap,
						       (sector_t)i << CHUNK_BLOCK_SHIFT(bitmap),
						       1);
	} else {
		sector_t start = 0;
		if (mddev->degraded == 0
		    || bitmap->events_cleared == mddev->events)
			/* no need to keep dirty bits to optimise a
			 * re-add of a missing device */
			start = mddev->recovery_cp;

		err = bitmap_init_from_disk(bitmap, start);
	}
	if (err)
		goto out;

	mddev->thread->timeout = mddev->bitmap_info.daemon_sleep;
	md_wakeup_thread(mddev->thread);

	bitmap_update_sb(bitmap);

	if (bitmap->flags & BITMAP_WRITE_ERROR)
		err = -EIO;
out:
	return err;
}
EXPORT_SYMBOL_GPL(bitmap_load);

static ssize_t
location_show(mddev_t *mddev, char *page)
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
location_store(mddev_t *mddev, const char *buf, size_t len)
{

	if (mddev->pers) {
		if (!mddev->pers->quiesce)
			return -EBUSY;
		if (mddev->recovery || mddev->sync_thread)
			return -EBUSY;
	}

	if (mddev->bitmap || mddev->bitmap_info.file ||
	    mddev->bitmap_info.offset) {
		/* bitmap already configured.  Only option is to clear it */
		if (strncmp(buf, "none", 4) != 0)
			return -EBUSY;
		if (mddev->pers) {
			mddev->pers->quiesce(mddev, 1);
			bitmap_destroy(mddev);
			mddev->pers->quiesce(mddev, 0);
		}
		mddev->bitmap_info.offset = 0;
		if (mddev->bitmap_info.file) {
			struct file *f = mddev->bitmap_info.file;
			mddev->bitmap_info.file = NULL;
			restore_bitmap_write_access(f);
			fput(f);
		}
	} else {
		/* No bitmap, OK to set a location */
		long long offset;
		if (strncmp(buf, "none", 4) == 0)
			/* nothing to be done */;
		else if (strncmp(buf, "file:", 5) == 0) {
			/* Not supported yet */
			return -EINVAL;
		} else {
			int rv;
			if (buf[0] == '+')
				rv = strict_strtoll(buf+1, 10, &offset);
			else
				rv = strict_strtoll(buf, 10, &offset);
			if (rv)
				return rv;
			if (offset == 0)
				return -EINVAL;
			if (mddev->bitmap_info.external == 0 &&
			    mddev->major_version == 0 &&
			    offset != mddev->bitmap_info.default_offset)
				return -EINVAL;
			mddev->bitmap_info.offset = offset;
			if (mddev->pers) {
				mddev->pers->quiesce(mddev, 1);
				rv = bitmap_create(mddev);
				if (rv) {
					bitmap_destroy(mddev);
					mddev->bitmap_info.offset = 0;
				}
				mddev->pers->quiesce(mddev, 0);
				if (rv)
					return rv;
			}
		}
	}
	if (!mddev->external) {
		/* Ensure new bitmap info is stored in
		 * metadata promptly.
		 */
		set_bit(MD_CHANGE_DEVS, &mddev->flags);
		md_wakeup_thread(mddev->thread);
	}
	return len;
}

static struct md_sysfs_entry bitmap_location =
__ATTR(location, S_IRUGO|S_IWUSR, location_show, location_store);

static ssize_t
timeout_show(mddev_t *mddev, char *page)
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
timeout_store(mddev_t *mddev, const char *buf, size_t len)
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
backlog_show(mddev_t *mddev, char *page)
{
	return sprintf(page, "%lu\n", mddev->bitmap_info.max_write_behind);
}

static ssize_t
backlog_store(mddev_t *mddev, const char *buf, size_t len)
{
	unsigned long backlog;
	int rv = strict_strtoul(buf, 10, &backlog);
	if (rv)
		return rv;
	if (backlog > COUNTER_MAX)
		return -EINVAL;
	mddev->bitmap_info.max_write_behind = backlog;
	return len;
}

static struct md_sysfs_entry bitmap_backlog =
__ATTR(backlog, S_IRUGO|S_IWUSR, backlog_show, backlog_store);

static ssize_t
chunksize_show(mddev_t *mddev, char *page)
{
	return sprintf(page, "%lu\n", mddev->bitmap_info.chunksize);
}

static ssize_t
chunksize_store(mddev_t *mddev, const char *buf, size_t len)
{
	/* Can only be changed when no bitmap is active */
	int rv;
	unsigned long csize;
	if (mddev->bitmap)
		return -EBUSY;
	rv = strict_strtoul(buf, 10, &csize);
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

static ssize_t metadata_show(mddev_t *mddev, char *page)
{
	return sprintf(page, "%s\n", (mddev->bitmap_info.external
				      ? "external" : "internal"));
}

static ssize_t metadata_store(mddev_t *mddev, const char *buf, size_t len)
{
	if (mddev->bitmap ||
	    mddev->bitmap_info.file ||
	    mddev->bitmap_info.offset)
		return -EBUSY;
	if (strncmp(buf, "external", 8) == 0)
		mddev->bitmap_info.external = 1;
	else if (strncmp(buf, "internal", 8) == 0)
		mddev->bitmap_info.external = 0;
	else
		return -EINVAL;
	return len;
}

static struct md_sysfs_entry bitmap_metadata =
__ATTR(metadata, S_IRUGO|S_IWUSR, metadata_show, metadata_store);

static ssize_t can_clear_show(mddev_t *mddev, char *page)
{
	int len;
	if (mddev->bitmap)
		len = sprintf(page, "%s\n", (mddev->bitmap->need_sync ?
					     "false" : "true"));
	else
		len = sprintf(page, "\n");
	return len;
}

static ssize_t can_clear_store(mddev_t *mddev, const char *buf, size_t len)
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
behind_writes_used_show(mddev_t *mddev, char *page)
{
	if (mddev->bitmap == NULL)
		return sprintf(page, "0\n");
	return sprintf(page, "%lu\n",
		       mddev->bitmap->behind_writes_used);
}

static ssize_t
behind_writes_used_reset(mddev_t *mddev, const char *buf, size_t len)
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

