/*
 * Compressed RAM block device
 *
 * Copyright (C) 2008, 2009, 2010  Nitin Gupta
 *               2012, 2013 Minchan Kim
 *
 * This code is released using a dual license strategy: BSD/GPL
 * You can choose the licence that better fits your requirements.
 *
 * Released under the terms of 3-clause BSD License
 * Released under the terms of GNU General Public License Version 2.0
 *
 */

#define KMSG_COMPONENT "zram"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/bitops.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/device.h>
#include <linux/genhd.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/backing-dev.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/err.h>
#include <linux/idr.h>
#include <linux/sysfs.h>
#include <linux/cpuhotplug.h>

#include "zram_drv.h"

static DEFINE_IDR(zram_index_idr);
/* idr index must be protected */
static DEFINE_MUTEX(zram_index_mutex);

static int zram_major;
static const char *default_compressor = "lzo";

/* Module params (documentation at end) */
static unsigned int num_devices = 1;

static void zram_free_page(struct zram *zram, size_t index);

static inline bool init_done(struct zram *zram)
{
	return zram->disksize;
}

static inline struct zram *dev_to_zram(struct device *dev)
{
	return (struct zram *)dev_to_disk(dev)->private_data;
}

static unsigned long zram_get_handle(struct zram *zram, u32 index)
{
	return zram->table[index].handle;
}

static void zram_set_handle(struct zram *zram, u32 index, unsigned long handle)
{
	zram->table[index].handle = handle;
}

/* flag operations require table entry bit_spin_lock() being held */
static int zram_test_flag(struct zram *zram, u32 index,
			enum zram_pageflags flag)
{
	return zram->table[index].value & BIT(flag);
}

static void zram_set_flag(struct zram *zram, u32 index,
			enum zram_pageflags flag)
{
	zram->table[index].value |= BIT(flag);
}

static void zram_clear_flag(struct zram *zram, u32 index,
			enum zram_pageflags flag)
{
	zram->table[index].value &= ~BIT(flag);
}

static inline void zram_set_element(struct zram *zram, u32 index,
			unsigned long element)
{
	zram->table[index].element = element;
}

static unsigned long zram_get_element(struct zram *zram, u32 index)
{
	return zram->table[index].element;
}

static size_t zram_get_obj_size(struct zram *zram, u32 index)
{
	return zram->table[index].value & (BIT(ZRAM_FLAG_SHIFT) - 1);
}

static void zram_set_obj_size(struct zram *zram,
					u32 index, size_t size)
{
	unsigned long flags = zram->table[index].value >> ZRAM_FLAG_SHIFT;

	zram->table[index].value = (flags << ZRAM_FLAG_SHIFT) | size;
}

#if PAGE_SIZE != 4096
static inline bool is_partial_io(struct bio_vec *bvec)
{
	return bvec->bv_len != PAGE_SIZE;
}
#else
static inline bool is_partial_io(struct bio_vec *bvec)
{
	return false;
}
#endif

static void zram_revalidate_disk(struct zram *zram)
{
	revalidate_disk(zram->disk);
	/* revalidate_disk reset the BDI_CAP_STABLE_WRITES so set again */
	zram->disk->queue->backing_dev_info->capabilities |=
		BDI_CAP_STABLE_WRITES;
}

/*
 * Check if request is within bounds and aligned on zram logical blocks.
 */
static inline bool valid_io_request(struct zram *zram,
		sector_t start, unsigned int size)
{
	u64 end, bound;

	/* unaligned request */
	if (unlikely(start & (ZRAM_SECTOR_PER_LOGICAL_BLOCK - 1)))
		return false;
	if (unlikely(size & (ZRAM_LOGICAL_BLOCK_SIZE - 1)))
		return false;

	end = start + (size >> SECTOR_SHIFT);
	bound = zram->disksize >> SECTOR_SHIFT;
	/* out of range range */
	if (unlikely(start >= bound || end > bound || start > end))
		return false;

	/* I/O request is valid */
	return true;
}

static void update_position(u32 *index, int *offset, struct bio_vec *bvec)
{
	*index  += (*offset + bvec->bv_len) / PAGE_SIZE;
	*offset = (*offset + bvec->bv_len) % PAGE_SIZE;
}

static inline void update_used_max(struct zram *zram,
					const unsigned long pages)
{
	unsigned long old_max, cur_max;

	old_max = atomic_long_read(&zram->stats.max_used_pages);

	do {
		cur_max = old_max;
		if (pages > cur_max)
			old_max = atomic_long_cmpxchg(
				&zram->stats.max_used_pages, cur_max, pages);
	} while (old_max != cur_max);
}

static inline void zram_fill_page(void *ptr, unsigned long len,
					unsigned long value)
{
	WARN_ON_ONCE(!IS_ALIGNED(len, sizeof(unsigned long)));
	memset_l(ptr, value, len / sizeof(unsigned long));
}

static bool page_same_filled(void *ptr, unsigned long *element)
{
	unsigned int pos;
	unsigned long *page;
	unsigned long val;

	page = (unsigned long *)ptr;
	val = page[0];

	for (pos = 1; pos < PAGE_SIZE / sizeof(*page); pos++) {
		if (val != page[pos])
			return false;
	}

	*element = val;

	return true;
}

static ssize_t initstate_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u32 val;
	struct zram *zram = dev_to_zram(dev);

	down_read(&zram->init_lock);
	val = init_done(zram);
	up_read(&zram->init_lock);

	return scnprintf(buf, PAGE_SIZE, "%u\n", val);
}

static ssize_t disksize_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct zram *zram = dev_to_zram(dev);

	return scnprintf(buf, PAGE_SIZE, "%llu\n", zram->disksize);
}

static ssize_t mem_limit_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	u64 limit;
	char *tmp;
	struct zram *zram = dev_to_zram(dev);

	limit = memparse(buf, &tmp);
	if (buf == tmp) /* no chars parsed, invalid input */
		return -EINVAL;

	down_write(&zram->init_lock);
	zram->limit_pages = PAGE_ALIGN(limit) >> PAGE_SHIFT;
	up_write(&zram->init_lock);

	return len;
}

static ssize_t mem_used_max_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int err;
	unsigned long val;
	struct zram *zram = dev_to_zram(dev);

	err = kstrtoul(buf, 10, &val);
	if (err || val != 0)
		return -EINVAL;

	down_read(&zram->init_lock);
	if (init_done(zram)) {
		atomic_long_set(&zram->stats.max_used_pages,
				zs_get_total_pages(zram->mem_pool));
	}
	up_read(&zram->init_lock);

	return len;
}

#ifdef CONFIG_ZRAM_WRITEBACK
static bool zram_wb_enabled(struct zram *zram)
{
	return zram->backing_dev;
}

static void reset_bdev(struct zram *zram)
{
	struct block_device *bdev;

	if (!zram_wb_enabled(zram))
		return;

	bdev = zram->bdev;
	if (zram->old_block_size)
		set_blocksize(bdev, zram->old_block_size);
	blkdev_put(bdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);
	/* hope filp_close flush all of IO */
	filp_close(zram->backing_dev, NULL);
	zram->backing_dev = NULL;
	zram->old_block_size = 0;
	zram->bdev = NULL;

	kvfree(zram->bitmap);
	zram->bitmap = NULL;
}

static ssize_t backing_dev_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct zram *zram = dev_to_zram(dev);
	struct file *file = zram->backing_dev;
	char *p;
	ssize_t ret;

	down_read(&zram->init_lock);
	if (!zram_wb_enabled(zram)) {
		memcpy(buf, "none\n", 5);
		up_read(&zram->init_lock);
		return 5;
	}

	p = file_path(file, buf, PAGE_SIZE - 1);
	if (IS_ERR(p)) {
		ret = PTR_ERR(p);
		goto out;
	}

	ret = strlen(p);
	memmove(buf, p, ret);
	buf[ret++] = '\n';
out:
	up_read(&zram->init_lock);
	return ret;
}

static ssize_t backing_dev_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	char *file_name;
	struct file *backing_dev = NULL;
	struct inode *inode;
	struct address_space *mapping;
	unsigned int bitmap_sz, old_block_size = 0;
	unsigned long nr_pages, *bitmap = NULL;
	struct block_device *bdev = NULL;
	int err;
	struct zram *zram = dev_to_zram(dev);

	file_name = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!file_name)
		return -ENOMEM;

	down_write(&zram->init_lock);
	if (init_done(zram)) {
		pr_info("Can't setup backing device for initialized device\n");
		err = -EBUSY;
		goto out;
	}

	strlcpy(file_name, buf, len);

	backing_dev = filp_open(file_name, O_RDWR|O_LARGEFILE, 0);
	if (IS_ERR(backing_dev)) {
		err = PTR_ERR(backing_dev);
		backing_dev = NULL;
		goto out;
	}

	mapping = backing_dev->f_mapping;
	inode = mapping->host;

	/* Support only block device in this moment */
	if (!S_ISBLK(inode->i_mode)) {
		err = -ENOTBLK;
		goto out;
	}

	bdev = bdgrab(I_BDEV(inode));
	err = blkdev_get(bdev, FMODE_READ | FMODE_WRITE | FMODE_EXCL, zram);
	if (err < 0)
		goto out;

	nr_pages = i_size_read(inode) >> PAGE_SHIFT;
	bitmap_sz = BITS_TO_LONGS(nr_pages) * sizeof(long);
	bitmap = kvzalloc(bitmap_sz, GFP_KERNEL);
	if (!bitmap) {
		err = -ENOMEM;
		goto out;
	}

	old_block_size = block_size(bdev);
	err = set_blocksize(bdev, PAGE_SIZE);
	if (err)
		goto out;

	reset_bdev(zram);
	spin_lock_init(&zram->bitmap_lock);

	zram->old_block_size = old_block_size;
	zram->bdev = bdev;
	zram->backing_dev = backing_dev;
	zram->bitmap = bitmap;
	zram->nr_pages = nr_pages;
	up_write(&zram->init_lock);

	pr_info("setup backing device %s\n", file_name);
	kfree(file_name);

	return len;
out:
	if (bitmap)
		kvfree(bitmap);

	if (bdev)
		blkdev_put(bdev, FMODE_READ | FMODE_WRITE | FMODE_EXCL);

	if (backing_dev)
		filp_close(backing_dev, NULL);

	up_write(&zram->init_lock);

	kfree(file_name);

	return err;
}

static unsigned long get_entry_bdev(struct zram *zram)
{
	unsigned long entry;

	spin_lock(&zram->bitmap_lock);
	/* skip 0 bit to confuse zram.handle = 0 */
	entry = find_next_zero_bit(zram->bitmap, zram->nr_pages, 1);
	if (entry == zram->nr_pages) {
		spin_unlock(&zram->bitmap_lock);
		return 0;
	}

	set_bit(entry, zram->bitmap);
	spin_unlock(&zram->bitmap_lock);

	return entry;
}

static void put_entry_bdev(struct zram *zram, unsigned long entry)
{
	int was_set;

	spin_lock(&zram->bitmap_lock);
	was_set = test_and_clear_bit(entry, zram->bitmap);
	spin_unlock(&zram->bitmap_lock);
	WARN_ON_ONCE(!was_set);
}

void zram_page_end_io(struct bio *bio)
{
	struct page *page = bio->bi_io_vec[0].bv_page;

	page_endio(page, op_is_write(bio_op(bio)),
			blk_status_to_errno(bio->bi_status));
	bio_put(bio);
}

/*
 * Returns 1 if the submission is successful.
 */
static int read_from_bdev_async(struct zram *zram, struct bio_vec *bvec,
			unsigned long entry, struct bio *parent)
{
	struct bio *bio;

	bio = bio_alloc(GFP_ATOMIC, 1);
	if (!bio)
		return -ENOMEM;

	bio->bi_iter.bi_sector = entry * (PAGE_SIZE >> 9);
	bio_set_dev(bio, zram->bdev);
	if (!bio_add_page(bio, bvec->bv_page, bvec->bv_len, bvec->bv_offset)) {
		bio_put(bio);
		return -EIO;
	}

	if (!parent) {
		bio->bi_opf = REQ_OP_READ;
		bio->bi_end_io = zram_page_end_io;
	} else {
		bio->bi_opf = parent->bi_opf;
		bio_chain(bio, parent);
	}

	submit_bio(bio);
	return 1;
}

struct zram_work {
	struct work_struct work;
	struct zram *zram;
	unsigned long entry;
	struct bio *bio;
};

#if PAGE_SIZE != 4096
static void zram_sync_read(struct work_struct *work)
{
	struct bio_vec bvec;
	struct zram_work *zw = container_of(work, struct zram_work, work);
	struct zram *zram = zw->zram;
	unsigned long entry = zw->entry;
	struct bio *bio = zw->bio;

	read_from_bdev_async(zram, &bvec, entry, bio);
}

/*
 * Block layer want one ->make_request_fn to be active at a time
 * so if we use chained IO with parent IO in same context,
 * it's a deadlock. To avoid, it, it uses worker thread context.
 */
static int read_from_bdev_sync(struct zram *zram, struct bio_vec *bvec,
				unsigned long entry, struct bio *bio)
{
	struct zram_work work;

	work.zram = zram;
	work.entry = entry;
	work.bio = bio;

	INIT_WORK_ONSTACK(&work.work, zram_sync_read);
	queue_work(system_unbound_wq, &work.work);
	flush_work(&work.work);
	destroy_work_on_stack(&work.work);

	return 1;
}
#else
static int read_from_bdev_sync(struct zram *zram, struct bio_vec *bvec,
				unsigned long entry, struct bio *bio)
{
	WARN_ON(1);
	return -EIO;
}
#endif

static int read_from_bdev(struct zram *zram, struct bio_vec *bvec,
			unsigned long entry, struct bio *parent, bool sync)
{
	if (sync)
		return read_from_bdev_sync(zram, bvec, entry, parent);
	else
		return read_from_bdev_async(zram, bvec, entry, parent);
}

static int write_to_bdev(struct zram *zram, struct bio_vec *bvec,
					u32 index, struct bio *parent,
					unsigned long *pentry)
{
	struct bio *bio;
	unsigned long entry;

	bio = bio_alloc(GFP_ATOMIC, 1);
	if (!bio)
		return -ENOMEM;

	entry = get_entry_bdev(zram);
	if (!entry) {
		bio_put(bio);
		return -ENOSPC;
	}

	bio->bi_iter.bi_sector = entry * (PAGE_SIZE >> 9);
	bio_set_dev(bio, zram->bdev);
	if (!bio_add_page(bio, bvec->bv_page, bvec->bv_len,
					bvec->bv_offset)) {
		bio_put(bio);
		put_entry_bdev(zram, entry);
		return -EIO;
	}

	if (!parent) {
		bio->bi_opf = REQ_OP_WRITE | REQ_SYNC;
		bio->bi_end_io = zram_page_end_io;
	} else {
		bio->bi_opf = parent->bi_opf;
		bio_chain(bio, parent);
	}

	submit_bio(bio);
	*pentry = entry;

	return 0;
}

static void zram_wb_clear(struct zram *zram, u32 index)
{
	unsigned long entry;

	zram_clear_flag(zram, index, ZRAM_WB);
	entry = zram_get_element(zram, index);
	zram_set_element(zram, index, 0);
	put_entry_bdev(zram, entry);
}

#else
static bool zram_wb_enabled(struct zram *zram) { return false; }
static inline void reset_bdev(struct zram *zram) {};
static int write_to_bdev(struct zram *zram, struct bio_vec *bvec,
					u32 index, struct bio *parent,
					unsigned long *pentry)

{
	return -EIO;
}

static int read_from_bdev(struct zram *zram, struct bio_vec *bvec,
			unsigned long entry, struct bio *parent, bool sync)
{
	return -EIO;
}
static void zram_wb_clear(struct zram *zram, u32 index) {}
#endif


/*
 * We switched to per-cpu streams and this attr is not needed anymore.
 * However, we will keep it around for some time, because:
 * a) we may revert per-cpu streams in the future
 * b) it's visible to user space and we need to follow our 2 years
 *    retirement rule; but we already have a number of 'soon to be
 *    altered' attrs, so max_comp_streams need to wait for the next
 *    layoff cycle.
 */
static ssize_t max_comp_streams_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", num_online_cpus());
}

static ssize_t max_comp_streams_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	return len;
}

static ssize_t comp_algorithm_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t sz;
	struct zram *zram = dev_to_zram(dev);

	down_read(&zram->init_lock);
	sz = zcomp_available_show(zram->compressor, buf);
	up_read(&zram->init_lock);

	return sz;
}

static ssize_t comp_algorithm_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct zram *zram = dev_to_zram(dev);
	char compressor[ARRAY_SIZE(zram->compressor)];
	size_t sz;

	strlcpy(compressor, buf, sizeof(compressor));
	/* ignore trailing newline */
	sz = strlen(compressor);
	if (sz > 0 && compressor[sz - 1] == '\n')
		compressor[sz - 1] = 0x00;

	if (!zcomp_available_algorithm(compressor))
		return -EINVAL;

	down_write(&zram->init_lock);
	if (init_done(zram)) {
		up_write(&zram->init_lock);
		pr_info("Can't change algorithm for initialized device\n");
		return -EBUSY;
	}

	strcpy(zram->compressor, compressor);
	up_write(&zram->init_lock);
	return len;
}

static ssize_t compact_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct zram *zram = dev_to_zram(dev);

	down_read(&zram->init_lock);
	if (!init_done(zram)) {
		up_read(&zram->init_lock);
		return -EINVAL;
	}

	zs_compact(zram->mem_pool);
	up_read(&zram->init_lock);

	return len;
}

static ssize_t io_stat_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct zram *zram = dev_to_zram(dev);
	ssize_t ret;

	down_read(&zram->init_lock);
	ret = scnprintf(buf, PAGE_SIZE,
			"%8llu %8llu %8llu %8llu\n",
			(u64)atomic64_read(&zram->stats.failed_reads),
			(u64)atomic64_read(&zram->stats.failed_writes),
			(u64)atomic64_read(&zram->stats.invalid_io),
			(u64)atomic64_read(&zram->stats.notify_free));
	up_read(&zram->init_lock);

	return ret;
}

static ssize_t mm_stat_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct zram *zram = dev_to_zram(dev);
	struct zs_pool_stats pool_stats;
	u64 orig_size, mem_used = 0;
	long max_used;
	ssize_t ret;

	memset(&pool_stats, 0x00, sizeof(struct zs_pool_stats));

	down_read(&zram->init_lock);
	if (init_done(zram)) {
		mem_used = zs_get_total_pages(zram->mem_pool);
		zs_pool_stats(zram->mem_pool, &pool_stats);
	}

	orig_size = atomic64_read(&zram->stats.pages_stored);
	max_used = atomic_long_read(&zram->stats.max_used_pages);

	ret = scnprintf(buf, PAGE_SIZE,
			"%8llu %8llu %8llu %8lu %8ld %8llu %8lu\n",
			orig_size << PAGE_SHIFT,
			(u64)atomic64_read(&zram->stats.compr_data_size),
			mem_used << PAGE_SHIFT,
			zram->limit_pages << PAGE_SHIFT,
			max_used << PAGE_SHIFT,
			(u64)atomic64_read(&zram->stats.same_pages),
			pool_stats.pages_compacted);
	up_read(&zram->init_lock);

	return ret;
}

static ssize_t debug_stat_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int version = 1;
	struct zram *zram = dev_to_zram(dev);
	ssize_t ret;

	down_read(&zram->init_lock);
	ret = scnprintf(buf, PAGE_SIZE,
			"version: %d\n%8llu\n",
			version,
			(u64)atomic64_read(&zram->stats.writestall));
	up_read(&zram->init_lock);

	return ret;
}

static DEVICE_ATTR_RO(io_stat);
static DEVICE_ATTR_RO(mm_stat);
static DEVICE_ATTR_RO(debug_stat);

static void zram_slot_lock(struct zram *zram, u32 index)
{
	bit_spin_lock(ZRAM_ACCESS, &zram->table[index].value);
}

static void zram_slot_unlock(struct zram *zram, u32 index)
{
	bit_spin_unlock(ZRAM_ACCESS, &zram->table[index].value);
}

static void zram_meta_free(struct zram *zram, u64 disksize)
{
	size_t num_pages = disksize >> PAGE_SHIFT;
	size_t index;

	/* Free all pages that are still in this zram device */
	for (index = 0; index < num_pages; index++)
		zram_free_page(zram, index);

	zs_destroy_pool(zram->mem_pool);
	vfree(zram->table);
}

static bool zram_meta_alloc(struct zram *zram, u64 disksize)
{
	size_t num_pages;

	num_pages = disksize >> PAGE_SHIFT;
	zram->table = vzalloc(num_pages * sizeof(*zram->table));
	if (!zram->table)
		return false;

	zram->mem_pool = zs_create_pool(zram->disk->disk_name);
	if (!zram->mem_pool) {
		vfree(zram->table);
		return false;
	}

	return true;
}

/*
 * To protect concurrent access to the same index entry,
 * caller should hold this table index entry's bit_spinlock to
 * indicate this index entry is accessing.
 */
static void zram_free_page(struct zram *zram, size_t index)
{
	unsigned long handle;

	if (zram_wb_enabled(zram) && zram_test_flag(zram, index, ZRAM_WB)) {
		zram_wb_clear(zram, index);
		atomic64_dec(&zram->stats.pages_stored);
		return;
	}

	/*
	 * No memory is allocated for same element filled pages.
	 * Simply clear same page flag.
	 */
	if (zram_test_flag(zram, index, ZRAM_SAME)) {
		zram_clear_flag(zram, index, ZRAM_SAME);
		zram_set_element(zram, index, 0);
		atomic64_dec(&zram->stats.same_pages);
		atomic64_dec(&zram->stats.pages_stored);
		return;
	}

	handle = zram_get_handle(zram, index);
	if (!handle)
		return;

	zs_free(zram->mem_pool, handle);

	atomic64_sub(zram_get_obj_size(zram, index),
			&zram->stats.compr_data_size);
	atomic64_dec(&zram->stats.pages_stored);

	zram_set_handle(zram, index, 0);
	zram_set_obj_size(zram, index, 0);
}

static int __zram_bvec_read(struct zram *zram, struct page *page, u32 index,
				struct bio *bio, bool partial_io)
{
	int ret;
	unsigned long handle;
	unsigned int size;
	void *src, *dst;

	if (zram_wb_enabled(zram)) {
		zram_slot_lock(zram, index);
		if (zram_test_flag(zram, index, ZRAM_WB)) {
			struct bio_vec bvec;

			zram_slot_unlock(zram, index);

			bvec.bv_page = page;
			bvec.bv_len = PAGE_SIZE;
			bvec.bv_offset = 0;
			return read_from_bdev(zram, &bvec,
					zram_get_element(zram, index),
					bio, partial_io);
		}
		zram_slot_unlock(zram, index);
	}

	zram_slot_lock(zram, index);
	handle = zram_get_handle(zram, index);
	if (!handle || zram_test_flag(zram, index, ZRAM_SAME)) {
		unsigned long value;
		void *mem;

		value = handle ? zram_get_element(zram, index) : 0;
		mem = kmap_atomic(page);
		zram_fill_page(mem, PAGE_SIZE, value);
		kunmap_atomic(mem);
		zram_slot_unlock(zram, index);
		return 0;
	}

	size = zram_get_obj_size(zram, index);

	src = zs_map_object(zram->mem_pool, handle, ZS_MM_RO);
	if (size == PAGE_SIZE) {
		dst = kmap_atomic(page);
		memcpy(dst, src, PAGE_SIZE);
		kunmap_atomic(dst);
		ret = 0;
	} else {
		struct zcomp_strm *zstrm = zcomp_stream_get(zram->comp);

		dst = kmap_atomic(page);
		ret = zcomp_decompress(zstrm, src, size, dst);
		kunmap_atomic(dst);
		zcomp_stream_put(zram->comp);
	}
	zs_unmap_object(zram->mem_pool, handle);
	zram_slot_unlock(zram, index);

	/* Should NEVER happen. Return bio error if it does. */
	if (unlikely(ret))
		pr_err("Decompression failed! err=%d, page=%u\n", ret, index);

	return ret;
}

static int zram_bvec_read(struct zram *zram, struct bio_vec *bvec,
				u32 index, int offset, struct bio *bio)
{
	int ret;
	struct page *page;

	page = bvec->bv_page;
	if (is_partial_io(bvec)) {
		/* Use a temporary buffer to decompress the page */
		page = alloc_page(GFP_NOIO|__GFP_HIGHMEM);
		if (!page)
			return -ENOMEM;
	}

	ret = __zram_bvec_read(zram, page, index, bio, is_partial_io(bvec));
	if (unlikely(ret))
		goto out;

	if (is_partial_io(bvec)) {
		void *dst = kmap_atomic(bvec->bv_page);
		void *src = kmap_atomic(page);

		memcpy(dst + bvec->bv_offset, src + offset, bvec->bv_len);
		kunmap_atomic(src);
		kunmap_atomic(dst);
	}
out:
	if (is_partial_io(bvec))
		__free_page(page);

	return ret;
}

static int __zram_bvec_write(struct zram *zram, struct bio_vec *bvec,
				u32 index, struct bio *bio)
{
	int ret = 0;
	unsigned long alloced_pages;
	unsigned long handle = 0;
	unsigned int comp_len = 0;
	void *src, *dst, *mem;
	struct zcomp_strm *zstrm;
	struct page *page = bvec->bv_page;
	unsigned long element = 0;
	enum zram_pageflags flags = 0;
	bool allow_wb = true;

	mem = kmap_atomic(page);
	if (page_same_filled(mem, &element)) {
		kunmap_atomic(mem);
		/* Free memory associated with this sector now. */
		flags = ZRAM_SAME;
		atomic64_inc(&zram->stats.same_pages);
		goto out;
	}
	kunmap_atomic(mem);

compress_again:
	zstrm = zcomp_stream_get(zram->comp);
	src = kmap_atomic(page);
	ret = zcomp_compress(zstrm, src, &comp_len);
	kunmap_atomic(src);

	if (unlikely(ret)) {
		zcomp_stream_put(zram->comp);
		pr_err("Compression failed! err=%d\n", ret);
		zs_free(zram->mem_pool, handle);
		return ret;
	}

	if (unlikely(comp_len > max_zpage_size)) {
		if (zram_wb_enabled(zram) && allow_wb) {
			zcomp_stream_put(zram->comp);
			ret = write_to_bdev(zram, bvec, index, bio, &element);
			if (!ret) {
				flags = ZRAM_WB;
				ret = 1;
				goto out;
			}
			allow_wb = false;
			goto compress_again;
		}
		comp_len = PAGE_SIZE;
	}

	/*
	 * handle allocation has 2 paths:
	 * a) fast path is executed with preemption disabled (for
	 *  per-cpu streams) and has __GFP_DIRECT_RECLAIM bit clear,
	 *  since we can't sleep;
	 * b) slow path enables preemption and attempts to allocate
	 *  the page with __GFP_DIRECT_RECLAIM bit set. we have to
	 *  put per-cpu compression stream and, thus, to re-do
	 *  the compression once handle is allocated.
	 *
	 * if we have a 'non-null' handle here then we are coming
	 * from the slow path and handle has already been allocated.
	 */
	if (!handle)
		handle = zs_malloc(zram->mem_pool, comp_len,
				__GFP_KSWAPD_RECLAIM |
				__GFP_NOWARN |
				__GFP_HIGHMEM |
				__GFP_MOVABLE);
	if (!handle) {
		zcomp_stream_put(zram->comp);
		atomic64_inc(&zram->stats.writestall);
		handle = zs_malloc(zram->mem_pool, comp_len,
				GFP_NOIO | __GFP_HIGHMEM |
				__GFP_MOVABLE);
		if (handle)
			goto compress_again;
		return -ENOMEM;
	}

	alloced_pages = zs_get_total_pages(zram->mem_pool);
	update_used_max(zram, alloced_pages);

	if (zram->limit_pages && alloced_pages > zram->limit_pages) {
		zcomp_stream_put(zram->comp);
		zs_free(zram->mem_pool, handle);
		return -ENOMEM;
	}

	dst = zs_map_object(zram->mem_pool, handle, ZS_MM_WO);

	src = zstrm->buffer;
	if (comp_len == PAGE_SIZE)
		src = kmap_atomic(page);
	memcpy(dst, src, comp_len);
	if (comp_len == PAGE_SIZE)
		kunmap_atomic(src);

	zcomp_stream_put(zram->comp);
	zs_unmap_object(zram->mem_pool, handle);
	atomic64_add(comp_len, &zram->stats.compr_data_size);
out:
	/*
	 * Free memory associated with this sector
	 * before overwriting unused sectors.
	 */
	zram_slot_lock(zram, index);
	zram_free_page(zram, index);

	if (flags) {
		zram_set_flag(zram, index, flags);
		zram_set_element(zram, index, element);
	}  else {
		zram_set_handle(zram, index, handle);
		zram_set_obj_size(zram, index, comp_len);
	}
	zram_slot_unlock(zram, index);

	/* Update stats */
	atomic64_inc(&zram->stats.pages_stored);
	return ret;
}

static int zram_bvec_write(struct zram *zram, struct bio_vec *bvec,
				u32 index, int offset, struct bio *bio)
{
	int ret;
	struct page *page = NULL;
	void *src;
	struct bio_vec vec;

	vec = *bvec;
	if (is_partial_io(bvec)) {
		void *dst;
		/*
		 * This is a partial IO. We need to read the full page
		 * before to write the changes.
		 */
		page = alloc_page(GFP_NOIO|__GFP_HIGHMEM);
		if (!page)
			return -ENOMEM;

		ret = __zram_bvec_read(zram, page, index, bio, true);
		if (ret)
			goto out;

		src = kmap_atomic(bvec->bv_page);
		dst = kmap_atomic(page);
		memcpy(dst + offset, src + bvec->bv_offset, bvec->bv_len);
		kunmap_atomic(dst);
		kunmap_atomic(src);

		vec.bv_page = page;
		vec.bv_len = PAGE_SIZE;
		vec.bv_offset = 0;
	}

	ret = __zram_bvec_write(zram, &vec, index, bio);
out:
	if (is_partial_io(bvec))
		__free_page(page);
	return ret;
}

/*
 * zram_bio_discard - handler on discard request
 * @index: physical block index in PAGE_SIZE units
 * @offset: byte offset within physical block
 */
static void zram_bio_discard(struct zram *zram, u32 index,
			     int offset, struct bio *bio)
{
	size_t n = bio->bi_iter.bi_size;

	/*
	 * zram manages data in physical block size units. Because logical block
	 * size isn't identical with physical block size on some arch, we
	 * could get a discard request pointing to a specific offset within a
	 * certain physical block.  Although we can handle this request by
	 * reading that physiclal block and decompressing and partially zeroing
	 * and re-compressing and then re-storing it, this isn't reasonable
	 * because our intent with a discard request is to save memory.  So
	 * skipping this logical block is appropriate here.
	 */
	if (offset) {
		if (n <= (PAGE_SIZE - offset))
			return;

		n -= (PAGE_SIZE - offset);
		index++;
	}

	while (n >= PAGE_SIZE) {
		zram_slot_lock(zram, index);
		zram_free_page(zram, index);
		zram_slot_unlock(zram, index);
		atomic64_inc(&zram->stats.notify_free);
		index++;
		n -= PAGE_SIZE;
	}
}

/*
 * Returns errno if it has some problem. Otherwise return 0 or 1.
 * Returns 0 if IO request was done synchronously
 * Returns 1 if IO request was successfully submitted.
 */
static int zram_bvec_rw(struct zram *zram, struct bio_vec *bvec, u32 index,
			int offset, bool is_write, struct bio *bio)
{
	unsigned long start_time = jiffies;
	int rw_acct = is_write ? REQ_OP_WRITE : REQ_OP_READ;
	struct request_queue *q = zram->disk->queue;
	int ret;

	generic_start_io_acct(q, rw_acct, bvec->bv_len >> SECTOR_SHIFT,
			&zram->disk->part0);

	if (!is_write) {
		atomic64_inc(&zram->stats.num_reads);
		ret = zram_bvec_read(zram, bvec, index, offset, bio);
		flush_dcache_page(bvec->bv_page);
	} else {
		atomic64_inc(&zram->stats.num_writes);
		ret = zram_bvec_write(zram, bvec, index, offset, bio);
	}

	generic_end_io_acct(q, rw_acct, &zram->disk->part0, start_time);

	if (unlikely(ret < 0)) {
		if (!is_write)
			atomic64_inc(&zram->stats.failed_reads);
		else
			atomic64_inc(&zram->stats.failed_writes);
	}

	return ret;
}

static void __zram_make_request(struct zram *zram, struct bio *bio)
{
	int offset;
	u32 index;
	struct bio_vec bvec;
	struct bvec_iter iter;

	index = bio->bi_iter.bi_sector >> SECTORS_PER_PAGE_SHIFT;
	offset = (bio->bi_iter.bi_sector &
		  (SECTORS_PER_PAGE - 1)) << SECTOR_SHIFT;

	switch (bio_op(bio)) {
	case REQ_OP_DISCARD:
	case REQ_OP_WRITE_ZEROES:
		zram_bio_discard(zram, index, offset, bio);
		bio_endio(bio);
		return;
	default:
		break;
	}

	bio_for_each_segment(bvec, bio, iter) {
		struct bio_vec bv = bvec;
		unsigned int unwritten = bvec.bv_len;

		do {
			bv.bv_len = min_t(unsigned int, PAGE_SIZE - offset,
							unwritten);
			if (zram_bvec_rw(zram, &bv, index, offset,
					op_is_write(bio_op(bio)), bio) < 0)
				goto out;

			bv.bv_offset += bv.bv_len;
			unwritten -= bv.bv_len;

			update_position(&index, &offset, &bv);
		} while (unwritten);
	}

	bio_endio(bio);
	return;

out:
	bio_io_error(bio);
}

/*
 * Handler function for all zram I/O requests.
 */
static blk_qc_t zram_make_request(struct request_queue *queue, struct bio *bio)
{
	struct zram *zram = queue->queuedata;

	if (!valid_io_request(zram, bio->bi_iter.bi_sector,
					bio->bi_iter.bi_size)) {
		atomic64_inc(&zram->stats.invalid_io);
		goto error;
	}

	__zram_make_request(zram, bio);
	return BLK_QC_T_NONE;

error:
	bio_io_error(bio);
	return BLK_QC_T_NONE;
}

static void zram_slot_free_notify(struct block_device *bdev,
				unsigned long index)
{
	struct zram *zram;

	zram = bdev->bd_disk->private_data;

	zram_slot_lock(zram, index);
	zram_free_page(zram, index);
	zram_slot_unlock(zram, index);
	atomic64_inc(&zram->stats.notify_free);
}

static int zram_rw_page(struct block_device *bdev, sector_t sector,
		       struct page *page, bool is_write)
{
	int offset, ret;
	u32 index;
	struct zram *zram;
	struct bio_vec bv;

	if (PageTransHuge(page))
		return -ENOTSUPP;
	zram = bdev->bd_disk->private_data;

	if (!valid_io_request(zram, sector, PAGE_SIZE)) {
		atomic64_inc(&zram->stats.invalid_io);
		ret = -EINVAL;
		goto out;
	}

	index = sector >> SECTORS_PER_PAGE_SHIFT;
	offset = (sector & (SECTORS_PER_PAGE - 1)) << SECTOR_SHIFT;

	bv.bv_page = page;
	bv.bv_len = PAGE_SIZE;
	bv.bv_offset = 0;

	ret = zram_bvec_rw(zram, &bv, index, offset, is_write, NULL);
out:
	/*
	 * If I/O fails, just return error(ie, non-zero) without
	 * calling page_endio.
	 * It causes resubmit the I/O with bio request by upper functions
	 * of rw_page(e.g., swap_readpage, __swap_writepage) and
	 * bio->bi_end_io does things to handle the error
	 * (e.g., SetPageError, set_page_dirty and extra works).
	 */
	if (unlikely(ret < 0))
		return ret;

	switch (ret) {
	case 0:
		page_endio(page, is_write, 0);
		break;
	case 1:
		ret = 0;
		break;
	default:
		WARN_ON(1);
	}
	return ret;
}

static void zram_reset_device(struct zram *zram)
{
	struct zcomp *comp;
	u64 disksize;

	down_write(&zram->init_lock);

	zram->limit_pages = 0;

	if (!init_done(zram)) {
		up_write(&zram->init_lock);
		return;
	}

	comp = zram->comp;
	disksize = zram->disksize;
	zram->disksize = 0;

	set_capacity(zram->disk, 0);
	part_stat_set_all(&zram->disk->part0, 0);

	up_write(&zram->init_lock);
	/* I/O operation under all of CPU are done so let's free */
	zram_meta_free(zram, disksize);
	memset(&zram->stats, 0, sizeof(zram->stats));
	zcomp_destroy(comp);
	reset_bdev(zram);
}

static ssize_t disksize_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	u64 disksize;
	struct zcomp *comp;
	struct zram *zram = dev_to_zram(dev);
	int err;

	disksize = memparse(buf, NULL);
	if (!disksize)
		return -EINVAL;

	down_write(&zram->init_lock);
	if (init_done(zram)) {
		pr_info("Cannot change disksize for initialized device\n");
		err = -EBUSY;
		goto out_unlock;
	}

	disksize = PAGE_ALIGN(disksize);
	if (!zram_meta_alloc(zram, disksize)) {
		err = -ENOMEM;
		goto out_unlock;
	}

	comp = zcomp_create(zram->compressor);
	if (IS_ERR(comp)) {
		pr_err("Cannot initialise %s compressing backend\n",
				zram->compressor);
		err = PTR_ERR(comp);
		goto out_free_meta;
	}

	zram->comp = comp;
	zram->disksize = disksize;
	set_capacity(zram->disk, zram->disksize >> SECTOR_SHIFT);
	zram_revalidate_disk(zram);
	up_write(&zram->init_lock);

	return len;

out_free_meta:
	zram_meta_free(zram, disksize);
out_unlock:
	up_write(&zram->init_lock);
	return err;
}

static ssize_t reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret;
	unsigned short do_reset;
	struct zram *zram;
	struct block_device *bdev;

	ret = kstrtou16(buf, 10, &do_reset);
	if (ret)
		return ret;

	if (!do_reset)
		return -EINVAL;

	zram = dev_to_zram(dev);
	bdev = bdget_disk(zram->disk, 0);
	if (!bdev)
		return -ENOMEM;

	mutex_lock(&bdev->bd_mutex);
	/* Do not reset an active device or claimed device */
	if (bdev->bd_openers || zram->claim) {
		mutex_unlock(&bdev->bd_mutex);
		bdput(bdev);
		return -EBUSY;
	}

	/* From now on, anyone can't open /dev/zram[0-9] */
	zram->claim = true;
	mutex_unlock(&bdev->bd_mutex);

	/* Make sure all the pending I/O are finished */
	fsync_bdev(bdev);
	zram_reset_device(zram);
	zram_revalidate_disk(zram);
	bdput(bdev);

	mutex_lock(&bdev->bd_mutex);
	zram->claim = false;
	mutex_unlock(&bdev->bd_mutex);

	return len;
}

static int zram_open(struct block_device *bdev, fmode_t mode)
{
	int ret = 0;
	struct zram *zram;

	WARN_ON(!mutex_is_locked(&bdev->bd_mutex));

	zram = bdev->bd_disk->private_data;
	/* zram was claimed to reset so open request fails */
	if (zram->claim)
		ret = -EBUSY;

	return ret;
}

static const struct block_device_operations zram_devops = {
	.open = zram_open,
	.swap_slot_free_notify = zram_slot_free_notify,
	.rw_page = zram_rw_page,
	.owner = THIS_MODULE
};

static DEVICE_ATTR_WO(compact);
static DEVICE_ATTR_RW(disksize);
static DEVICE_ATTR_RO(initstate);
static DEVICE_ATTR_WO(reset);
static DEVICE_ATTR_WO(mem_limit);
static DEVICE_ATTR_WO(mem_used_max);
static DEVICE_ATTR_RW(max_comp_streams);
static DEVICE_ATTR_RW(comp_algorithm);
#ifdef CONFIG_ZRAM_WRITEBACK
static DEVICE_ATTR_RW(backing_dev);
#endif

static struct attribute *zram_disk_attrs[] = {
	&dev_attr_disksize.attr,
	&dev_attr_initstate.attr,
	&dev_attr_reset.attr,
	&dev_attr_compact.attr,
	&dev_attr_mem_limit.attr,
	&dev_attr_mem_used_max.attr,
	&dev_attr_max_comp_streams.attr,
	&dev_attr_comp_algorithm.attr,
#ifdef CONFIG_ZRAM_WRITEBACK
	&dev_attr_backing_dev.attr,
#endif
	&dev_attr_io_stat.attr,
	&dev_attr_mm_stat.attr,
	&dev_attr_debug_stat.attr,
	NULL,
};

static const struct attribute_group zram_disk_attr_group = {
	.attrs = zram_disk_attrs,
};

/*
 * Allocate and initialize new zram device. the function returns
 * '>= 0' device_id upon success, and negative value otherwise.
 */
static int zram_add(void)
{
	struct zram *zram;
	struct request_queue *queue;
	int ret, device_id;

	zram = kzalloc(sizeof(struct zram), GFP_KERNEL);
	if (!zram)
		return -ENOMEM;

	ret = idr_alloc(&zram_index_idr, zram, 0, 0, GFP_KERNEL);
	if (ret < 0)
		goto out_free_dev;
	device_id = ret;

	init_rwsem(&zram->init_lock);

	queue = blk_alloc_queue(GFP_KERNEL);
	if (!queue) {
		pr_err("Error allocating disk queue for device %d\n",
			device_id);
		ret = -ENOMEM;
		goto out_free_idr;
	}

	blk_queue_make_request(queue, zram_make_request);

	/* gendisk structure */
	zram->disk = alloc_disk(1);
	if (!zram->disk) {
		pr_err("Error allocating disk structure for device %d\n",
			device_id);
		ret = -ENOMEM;
		goto out_free_queue;
	}

	zram->disk->major = zram_major;
	zram->disk->first_minor = device_id;
	zram->disk->fops = &zram_devops;
	zram->disk->queue = queue;
	zram->disk->queue->queuedata = zram;
	zram->disk->private_data = zram;
	snprintf(zram->disk->disk_name, 16, "zram%d", device_id);

	/* Actual capacity set using syfs (/sys/block/zram<id>/disksize */
	set_capacity(zram->disk, 0);
	/* zram devices sort of resembles non-rotational disks */
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, zram->disk->queue);
	queue_flag_clear_unlocked(QUEUE_FLAG_ADD_RANDOM, zram->disk->queue);
	/*
	 * To ensure that we always get PAGE_SIZE aligned
	 * and n*PAGE_SIZED sized I/O requests.
	 */
	blk_queue_physical_block_size(zram->disk->queue, PAGE_SIZE);
	blk_queue_logical_block_size(zram->disk->queue,
					ZRAM_LOGICAL_BLOCK_SIZE);
	blk_queue_io_min(zram->disk->queue, PAGE_SIZE);
	blk_queue_io_opt(zram->disk->queue, PAGE_SIZE);
	zram->disk->queue->limits.discard_granularity = PAGE_SIZE;
	blk_queue_max_discard_sectors(zram->disk->queue, UINT_MAX);
	queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, zram->disk->queue);

	/*
	 * zram_bio_discard() will clear all logical blocks if logical block
	 * size is identical with physical block size(PAGE_SIZE). But if it is
	 * different, we will skip discarding some parts of logical blocks in
	 * the part of the request range which isn't aligned to physical block
	 * size.  So we can't ensure that all discarded logical blocks are
	 * zeroed.
	 */
	if (ZRAM_LOGICAL_BLOCK_SIZE == PAGE_SIZE)
		blk_queue_max_write_zeroes_sectors(zram->disk->queue, UINT_MAX);

	add_disk(zram->disk);

	ret = sysfs_create_group(&disk_to_dev(zram->disk)->kobj,
				&zram_disk_attr_group);
	if (ret < 0) {
		pr_err("Error creating sysfs group for device %d\n",
				device_id);
		goto out_free_disk;
	}
	strlcpy(zram->compressor, default_compressor, sizeof(zram->compressor));

	pr_info("Added device: %s\n", zram->disk->disk_name);
	return device_id;

out_free_disk:
	del_gendisk(zram->disk);
	put_disk(zram->disk);
out_free_queue:
	blk_cleanup_queue(queue);
out_free_idr:
	idr_remove(&zram_index_idr, device_id);
out_free_dev:
	kfree(zram);
	return ret;
}

static int zram_remove(struct zram *zram)
{
	struct block_device *bdev;

	bdev = bdget_disk(zram->disk, 0);
	if (!bdev)
		return -ENOMEM;

	mutex_lock(&bdev->bd_mutex);
	if (bdev->bd_openers || zram->claim) {
		mutex_unlock(&bdev->bd_mutex);
		bdput(bdev);
		return -EBUSY;
	}

	zram->claim = true;
	mutex_unlock(&bdev->bd_mutex);

	/*
	 * Remove sysfs first, so no one will perform a disksize
	 * store while we destroy the devices. This also helps during
	 * hot_remove -- zram_reset_device() is the last holder of
	 * ->init_lock, no later/concurrent disksize_store() or any
	 * other sysfs handlers are possible.
	 */
	sysfs_remove_group(&disk_to_dev(zram->disk)->kobj,
			&zram_disk_attr_group);

	/* Make sure all the pending I/O are finished */
	fsync_bdev(bdev);
	zram_reset_device(zram);
	bdput(bdev);

	pr_info("Removed device: %s\n", zram->disk->disk_name);

	blk_cleanup_queue(zram->disk->queue);
	del_gendisk(zram->disk);
	put_disk(zram->disk);
	kfree(zram);
	return 0;
}

/* zram-control sysfs attributes */

/*
 * NOTE: hot_add attribute is not the usual read-only sysfs attribute. In a
 * sense that reading from this file does alter the state of your system -- it
 * creates a new un-initialized zram device and returns back this device's
 * device_id (or an error code if it fails to create a new device).
 */
static ssize_t hot_add_show(struct class *class,
			struct class_attribute *attr,
			char *buf)
{
	int ret;

	mutex_lock(&zram_index_mutex);
	ret = zram_add();
	mutex_unlock(&zram_index_mutex);

	if (ret < 0)
		return ret;
	return scnprintf(buf, PAGE_SIZE, "%d\n", ret);
}
static CLASS_ATTR_RO(hot_add);

static ssize_t hot_remove_store(struct class *class,
			struct class_attribute *attr,
			const char *buf,
			size_t count)
{
	struct zram *zram;
	int ret, dev_id;

	/* dev_id is gendisk->first_minor, which is `int' */
	ret = kstrtoint(buf, 10, &dev_id);
	if (ret)
		return ret;
	if (dev_id < 0)
		return -EINVAL;

	mutex_lock(&zram_index_mutex);

	zram = idr_find(&zram_index_idr, dev_id);
	if (zram) {
		ret = zram_remove(zram);
		if (!ret)
			idr_remove(&zram_index_idr, dev_id);
	} else {
		ret = -ENODEV;
	}

	mutex_unlock(&zram_index_mutex);
	return ret ? ret : count;
}
static CLASS_ATTR_WO(hot_remove);

static struct attribute *zram_control_class_attrs[] = {
	&class_attr_hot_add.attr,
	&class_attr_hot_remove.attr,
	NULL,
};
ATTRIBUTE_GROUPS(zram_control_class);

static struct class zram_control_class = {
	.name		= "zram-control",
	.owner		= THIS_MODULE,
	.class_groups	= zram_control_class_groups,
};

static int zram_remove_cb(int id, void *ptr, void *data)
{
	zram_remove(ptr);
	return 0;
}

static void destroy_devices(void)
{
	class_unregister(&zram_control_class);
	idr_for_each(&zram_index_idr, &zram_remove_cb, NULL);
	idr_destroy(&zram_index_idr);
	unregister_blkdev(zram_major, "zram");
	cpuhp_remove_multi_state(CPUHP_ZCOMP_PREPARE);
}

static int __init zram_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_ZCOMP_PREPARE, "block/zram:prepare",
				      zcomp_cpu_up_prepare, zcomp_cpu_dead);
	if (ret < 0)
		return ret;

	ret = class_register(&zram_control_class);
	if (ret) {
		pr_err("Unable to register zram-control class\n");
		cpuhp_remove_multi_state(CPUHP_ZCOMP_PREPARE);
		return ret;
	}

	zram_major = register_blkdev(0, "zram");
	if (zram_major <= 0) {
		pr_err("Unable to get major number\n");
		class_unregister(&zram_control_class);
		cpuhp_remove_multi_state(CPUHP_ZCOMP_PREPARE);
		return -EBUSY;
	}

	while (num_devices != 0) {
		mutex_lock(&zram_index_mutex);
		ret = zram_add();
		mutex_unlock(&zram_index_mutex);
		if (ret < 0)
			goto out_error;
		num_devices--;
	}

	return 0;

out_error:
	destroy_devices();
	return ret;
}

static void __exit zram_exit(void)
{
	destroy_devices();
}

module_init(zram_init);
module_exit(zram_exit);

module_param(num_devices, uint, 0);
MODULE_PARM_DESC(num_devices, "Number of pre-created zram devices");

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Nitin Gupta <ngupta@vflare.org>");
MODULE_DESCRIPTION("Compressed RAM Block Device");
