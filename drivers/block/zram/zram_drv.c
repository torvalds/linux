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

#define pr_fmt(fmt) "zram: " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/bitops.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/device.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/backing-dev.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/err.h>
#include <linux/idr.h>
#include <linux/sysfs.h>
#include <linux/debugfs.h>
#include <linux/cpuhotplug.h>
#include <linux/part_stat.h>
#include <linux/kernel_read_file.h>

#include "zram_drv.h"

static DEFINE_IDR(zram_index_idr);
/* idr index must be protected */
static DEFINE_MUTEX(zram_index_mutex);

static int zram_major;
static const char *default_compressor = CONFIG_ZRAM_DEF_COMP;

#define ZRAM_MAX_ALGO_NAME_SZ	128

/* Module params (documentation at end) */
static unsigned int num_devices = 1;
/*
 * Pages that compress to sizes equals or greater than this are stored
 * uncompressed in memory.
 */
static size_t huge_class_size;

static const struct block_device_operations zram_devops;

static void slot_free(struct zram *zram, u32 index);
#define slot_dep_map(zram, index) (&(zram)->table[(index)].dep_map)

static void slot_lock_init(struct zram *zram, u32 index)
{
	static struct lock_class_key __key;

	lockdep_init_map(slot_dep_map(zram, index), "zram->table[index].lock",
			 &__key, 0);
}

/*
 * entry locking rules:
 *
 * 1) Lock is exclusive
 *
 * 2) lock() function can sleep waiting for the lock
 *
 * 3) Lock owner can sleep
 *
 * 4) Use TRY lock variant when in atomic context
 *    - must check return value and handle locking failers
 */
static __must_check bool slot_trylock(struct zram *zram, u32 index)
{
	unsigned long *lock = &zram->table[index].__lock;

	if (!test_and_set_bit_lock(ZRAM_ENTRY_LOCK, lock)) {
		mutex_acquire(slot_dep_map(zram, index), 0, 1, _RET_IP_);
		lock_acquired(slot_dep_map(zram, index), _RET_IP_);
		return true;
	}

	return false;
}

static void slot_lock(struct zram *zram, u32 index)
{
	unsigned long *lock = &zram->table[index].__lock;

	mutex_acquire(slot_dep_map(zram, index), 0, 0, _RET_IP_);
	wait_on_bit_lock(lock, ZRAM_ENTRY_LOCK, TASK_UNINTERRUPTIBLE);
	lock_acquired(slot_dep_map(zram, index), _RET_IP_);
}

static void slot_unlock(struct zram *zram, u32 index)
{
	unsigned long *lock = &zram->table[index].__lock;

	mutex_release(slot_dep_map(zram, index), _RET_IP_);
	clear_and_wake_up_bit(ZRAM_ENTRY_LOCK, lock);
}

static inline bool init_done(struct zram *zram)
{
	return zram->disksize;
}

static inline struct zram *dev_to_zram(struct device *dev)
{
	return (struct zram *)dev_to_disk(dev)->private_data;
}

static unsigned long get_slot_handle(struct zram *zram, u32 index)
{
	return zram->table[index].handle;
}

static void set_slot_handle(struct zram *zram, u32 index, unsigned long handle)
{
	zram->table[index].handle = handle;
}

static bool test_slot_flag(struct zram *zram, u32 index,
			   enum zram_pageflags flag)
{
	return zram->table[index].attr.flags & BIT(flag);
}

static void set_slot_flag(struct zram *zram, u32 index,
			  enum zram_pageflags flag)
{
	zram->table[index].attr.flags |= BIT(flag);
}

static void clear_slot_flag(struct zram *zram, u32 index,
			    enum zram_pageflags flag)
{
	zram->table[index].attr.flags &= ~BIT(flag);
}

static size_t get_slot_size(struct zram *zram, u32 index)
{
	return zram->table[index].attr.flags & (BIT(ZRAM_FLAG_SHIFT) - 1);
}

static void set_slot_size(struct zram *zram, u32 index, size_t size)
{
	unsigned long flags = zram->table[index].attr.flags >> ZRAM_FLAG_SHIFT;

	zram->table[index].attr.flags = (flags << ZRAM_FLAG_SHIFT) | size;
}

static inline bool slot_allocated(struct zram *zram, u32 index)
{
	return get_slot_size(zram, index) ||
		test_slot_flag(zram, index, ZRAM_SAME) ||
		test_slot_flag(zram, index, ZRAM_WB);
}

static inline void set_slot_comp_priority(struct zram *zram, u32 index,
					  u32 prio)
{
	prio &= ZRAM_COMP_PRIORITY_MASK;
	/*
	 * Clear previous priority value first, in case if we recompress
	 * further an already recompressed page
	 */
	zram->table[index].attr.flags &= ~(ZRAM_COMP_PRIORITY_MASK <<
					   ZRAM_COMP_PRIORITY_BIT1);
	zram->table[index].attr.flags |= (prio << ZRAM_COMP_PRIORITY_BIT1);
}

static inline u32 get_slot_comp_priority(struct zram *zram, u32 index)
{
	u32 prio = zram->table[index].attr.flags >> ZRAM_COMP_PRIORITY_BIT1;

	return prio & ZRAM_COMP_PRIORITY_MASK;
}

static void mark_slot_accessed(struct zram *zram, u32 index)
{
	clear_slot_flag(zram, index, ZRAM_IDLE);
	clear_slot_flag(zram, index, ZRAM_PP_SLOT);
#ifdef CONFIG_ZRAM_TRACK_ENTRY_ACTIME
	zram->table[index].attr.ac_time = (u32)ktime_get_boottime_seconds();
#endif
}

static inline void update_used_max(struct zram *zram, const unsigned long pages)
{
	unsigned long cur_max = atomic_long_read(&zram->stats.max_used_pages);

	do {
		if (cur_max >= pages)
			return;
	} while (!atomic_long_try_cmpxchg(&zram->stats.max_used_pages,
					  &cur_max, pages));
}

static bool zram_can_store_page(struct zram *zram)
{
	unsigned long alloced_pages;

	alloced_pages = zs_get_total_pages(zram->mem_pool);
	update_used_max(zram, alloced_pages);

	return !zram->limit_pages || alloced_pages <= zram->limit_pages;
}

#if PAGE_SIZE != 4096
static inline bool is_partial_io(struct bio_vec *bvec)
{
	return bvec->bv_len != PAGE_SIZE;
}
#define ZRAM_PARTIAL_IO		1
#else
static inline bool is_partial_io(struct bio_vec *bvec)
{
	return false;
}
#endif

#if defined CONFIG_ZRAM_WRITEBACK || defined CONFIG_ZRAM_MULTI_COMP
struct zram_pp_slot {
	unsigned long		index;
	struct list_head	entry;
};

/*
 * A post-processing bucket is, essentially, a size class, this defines
 * the range (in bytes) of pp-slots sizes in particular bucket.
 */
#define PP_BUCKET_SIZE_RANGE	64
#define NUM_PP_BUCKETS		((PAGE_SIZE / PP_BUCKET_SIZE_RANGE) + 1)

struct zram_pp_ctl {
	struct list_head	pp_buckets[NUM_PP_BUCKETS];
};

static struct zram_pp_ctl *init_pp_ctl(void)
{
	struct zram_pp_ctl *ctl;
	u32 idx;

	ctl = kmalloc(sizeof(*ctl), GFP_KERNEL);
	if (!ctl)
		return NULL;

	for (idx = 0; idx < NUM_PP_BUCKETS; idx++)
		INIT_LIST_HEAD(&ctl->pp_buckets[idx]);
	return ctl;
}

static void release_pp_slot(struct zram *zram, struct zram_pp_slot *pps)
{
	list_del_init(&pps->entry);

	slot_lock(zram, pps->index);
	clear_slot_flag(zram, pps->index, ZRAM_PP_SLOT);
	slot_unlock(zram, pps->index);

	kfree(pps);
}

static void release_pp_ctl(struct zram *zram, struct zram_pp_ctl *ctl)
{
	u32 idx;

	if (!ctl)
		return;

	for (idx = 0; idx < NUM_PP_BUCKETS; idx++) {
		while (!list_empty(&ctl->pp_buckets[idx])) {
			struct zram_pp_slot *pps;

			pps = list_first_entry(&ctl->pp_buckets[idx],
					       struct zram_pp_slot,
					       entry);
			release_pp_slot(zram, pps);
		}
	}

	kfree(ctl);
}

static bool place_pp_slot(struct zram *zram, struct zram_pp_ctl *ctl,
			  u32 index)
{
	struct zram_pp_slot *pps;
	u32 bid;

	pps = kmalloc(sizeof(*pps), GFP_NOIO | __GFP_NOWARN);
	if (!pps)
		return false;

	INIT_LIST_HEAD(&pps->entry);
	pps->index = index;

	bid = get_slot_size(zram, pps->index) / PP_BUCKET_SIZE_RANGE;
	list_add(&pps->entry, &ctl->pp_buckets[bid]);

	set_slot_flag(zram, pps->index, ZRAM_PP_SLOT);
	return true;
}

static struct zram_pp_slot *select_pp_slot(struct zram_pp_ctl *ctl)
{
	struct zram_pp_slot *pps = NULL;
	s32 idx = NUM_PP_BUCKETS - 1;

	/* The higher the bucket id the more optimal slot post-processing is */
	while (idx >= 0) {
		pps = list_first_entry_or_null(&ctl->pp_buckets[idx],
					       struct zram_pp_slot,
					       entry);
		if (pps)
			break;

		idx--;
	}
	return pps;
}
#endif

static inline void zram_fill_page(void *ptr, unsigned long len,
					unsigned long value)
{
	WARN_ON_ONCE(!IS_ALIGNED(len, sizeof(unsigned long)));
	memset_l(ptr, value, len / sizeof(unsigned long));
}

static bool page_same_filled(void *ptr, unsigned long *element)
{
	unsigned long *page;
	unsigned long val;
	unsigned int pos, last_pos = PAGE_SIZE / sizeof(*page) - 1;

	page = (unsigned long *)ptr;
	val = page[0];

	if (val != page[last_pos])
		return false;

	for (pos = 1; pos < last_pos; pos++) {
		if (val != page[pos])
			return false;
	}

	*element = val;

	return true;
}

static ssize_t initstate_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	u32 val;
	struct zram *zram = dev_to_zram(dev);

	guard(rwsem_read)(&zram->dev_lock);
	val = init_done(zram);

	return sysfs_emit(buf, "%u\n", val);
}

static ssize_t disksize_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct zram *zram = dev_to_zram(dev);

	return sysfs_emit(buf, "%llu\n", zram->disksize);
}

static ssize_t mem_limit_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t len)
{
	u64 limit;
	char *tmp;
	struct zram *zram = dev_to_zram(dev);

	limit = memparse(buf, &tmp);
	if (buf == tmp) /* no chars parsed, invalid input */
		return -EINVAL;

	guard(rwsem_write)(&zram->dev_lock);
	zram->limit_pages = PAGE_ALIGN(limit) >> PAGE_SHIFT;

	return len;
}

static ssize_t mem_used_max_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	int err;
	unsigned long val;
	struct zram *zram = dev_to_zram(dev);

	err = kstrtoul(buf, 10, &val);
	if (err || val != 0)
		return -EINVAL;

	guard(rwsem_read)(&zram->dev_lock);
	if (init_done(zram)) {
		atomic_long_set(&zram->stats.max_used_pages,
				zs_get_total_pages(zram->mem_pool));
	}

	return len;
}

/*
 * Mark all pages which are older than or equal to cutoff as IDLE.
 * Callers should hold the zram init lock in read mode
 */
static void mark_idle(struct zram *zram, ktime_t cutoff)
{
	int is_idle = 1;
	unsigned long nr_pages = zram->disksize >> PAGE_SHIFT;
	int index;

	for (index = 0; index < nr_pages; index++) {
		/*
		 * Do not mark ZRAM_SAME slots as ZRAM_IDLE, because no
		 * post-processing (recompress, writeback) happens to the
		 * ZRAM_SAME slot.
		 *
		 * And ZRAM_WB slots simply cannot be ZRAM_IDLE.
		 */
		slot_lock(zram, index);
		if (!slot_allocated(zram, index) ||
		    test_slot_flag(zram, index, ZRAM_WB) ||
		    test_slot_flag(zram, index, ZRAM_SAME)) {
			slot_unlock(zram, index);
			continue;
		}

#ifdef CONFIG_ZRAM_TRACK_ENTRY_ACTIME
		is_idle = !cutoff ||
			ktime_after(cutoff, zram->table[index].attr.ac_time);
#endif
		if (is_idle)
			set_slot_flag(zram, index, ZRAM_IDLE);
		else
			clear_slot_flag(zram, index, ZRAM_IDLE);
		slot_unlock(zram, index);
	}
}

static ssize_t idle_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t len)
{
	struct zram *zram = dev_to_zram(dev);
	ktime_t cutoff = 0;

	if (!sysfs_streq(buf, "all")) {
		/*
		 * If it did not parse as 'all' try to treat it as an integer
		 * when we have memory tracking enabled.
		 */
		u32 age_sec;

		if (IS_ENABLED(CONFIG_ZRAM_TRACK_ENTRY_ACTIME) &&
		    !kstrtouint(buf, 0, &age_sec))
			cutoff = ktime_sub((u32)ktime_get_boottime_seconds(),
					   age_sec);
		else
			return -EINVAL;
	}

	guard(rwsem_read)(&zram->dev_lock);
	if (!init_done(zram))
		return -EINVAL;

	/*
	 * A cutoff of 0 marks everything as idle, this is the
	 * "all" behavior.
	 */
	mark_idle(zram, cutoff);
	return len;
}

#ifdef CONFIG_ZRAM_WRITEBACK
#define INVALID_BDEV_BLOCK		(~0UL)

static int read_from_zspool_raw(struct zram *zram, struct page *page,
				u32 index);
static int read_from_zspool(struct zram *zram, struct page *page, u32 index);

struct zram_wb_ctl {
	/* idle list is accessed only by the writeback task, no concurency */
	struct list_head idle_reqs;
	/* done list is accessed concurrently, protect by done_lock */
	struct list_head done_reqs;
	wait_queue_head_t done_wait;
	spinlock_t done_lock;
	atomic_t num_inflight;
};

struct zram_wb_req {
	unsigned long blk_idx;
	struct page *page;
	struct zram_pp_slot *pps;
	struct bio_vec bio_vec;
	struct bio bio;

	struct list_head entry;
};

struct zram_rb_req {
	struct work_struct work;
	struct zram *zram;
	struct page *page;
	/* The read bio for backing device */
	struct bio *bio;
	unsigned long blk_idx;
	union {
		/* The original bio to complete (async read) */
		struct bio *parent;
		/* error status (sync read) */
		int error;
	};
	u32 index;
};

#define FOUR_K(x) ((x) * (1 << (PAGE_SHIFT - 12)))
static ssize_t bd_stat_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct zram *zram = dev_to_zram(dev);
	ssize_t ret;

	guard(rwsem_read)(&zram->dev_lock);
	ret = sysfs_emit(buf,
			 "%8llu %8llu %8llu\n",
			 FOUR_K((u64)atomic64_read(&zram->stats.bd_count)),
			 FOUR_K((u64)atomic64_read(&zram->stats.bd_reads)),
			 FOUR_K((u64)atomic64_read(&zram->stats.bd_writes)));

	return ret;
}

static ssize_t writeback_compressed_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t len)
{
	struct zram *zram = dev_to_zram(dev);
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	guard(rwsem_write)(&zram->dev_lock);
	if (init_done(zram)) {
		return -EBUSY;
	}

	zram->wb_compressed = val;

	return len;
}

static ssize_t writeback_compressed_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	bool val;
	struct zram *zram = dev_to_zram(dev);

	guard(rwsem_read)(&zram->dev_lock);
	val = zram->wb_compressed;

	return sysfs_emit(buf, "%d\n", val);
}

static ssize_t writeback_limit_enable_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t len)
{
	struct zram *zram = dev_to_zram(dev);
	u64 val;

	if (kstrtoull(buf, 10, &val))
		return -EINVAL;

	guard(rwsem_write)(&zram->dev_lock);
	zram->wb_limit_enable = val;

	return len;
}

static ssize_t writeback_limit_enable_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	bool val;
	struct zram *zram = dev_to_zram(dev);

	guard(rwsem_read)(&zram->dev_lock);
	val = zram->wb_limit_enable;

	return sysfs_emit(buf, "%d\n", val);
}

static ssize_t writeback_limit_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t len)
{
	struct zram *zram = dev_to_zram(dev);
	u64 val;

	if (kstrtoull(buf, 10, &val))
		return -EINVAL;

	/*
	 * When the page size is greater than 4KB, if bd_wb_limit is set to
	 * a value that is not page - size aligned, it will cause value
	 * wrapping. For example, when the page size is set to 16KB and
	 * bd_wb_limit is set to 3, a single write - back operation will
	 * cause bd_wb_limit to become -1. Even more terrifying is that
	 * bd_wb_limit is an unsigned number.
	 */
	val = rounddown(val, PAGE_SIZE / 4096);

	guard(rwsem_write)(&zram->dev_lock);
	zram->bd_wb_limit = val;

	return len;
}

static ssize_t writeback_limit_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	u64 val;
	struct zram *zram = dev_to_zram(dev);

	guard(rwsem_read)(&zram->dev_lock);
	val = zram->bd_wb_limit;

	return sysfs_emit(buf, "%llu\n", val);
}

static ssize_t writeback_batch_size_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t len)
{
	struct zram *zram = dev_to_zram(dev);
	u32 val;

	if (kstrtouint(buf, 10, &val))
		return -EINVAL;

	if (!val)
		return -EINVAL;

	guard(rwsem_write)(&zram->dev_lock);
	zram->wb_batch_size = val;

	return len;
}

static ssize_t writeback_batch_size_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	u32 val;
	struct zram *zram = dev_to_zram(dev);

	guard(rwsem_read)(&zram->dev_lock);
	val = zram->wb_batch_size;

	return sysfs_emit(buf, "%u\n", val);
}

static void reset_bdev(struct zram *zram)
{
	if (!zram->backing_dev)
		return;

	/* hope filp_close flush all of IO */
	filp_close(zram->backing_dev, NULL);
	zram->backing_dev = NULL;
	zram->bdev = NULL;
	zram->disk->fops = &zram_devops;
	kvfree(zram->bitmap);
	zram->bitmap = NULL;
}

static ssize_t backing_dev_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct file *file;
	struct zram *zram = dev_to_zram(dev);
	char *p;
	ssize_t ret;

	guard(rwsem_read)(&zram->dev_lock);
	file = zram->backing_dev;
	if (!file) {
		memcpy(buf, "none\n", 5);
		return 5;
	}

	p = file_path(file, buf, PAGE_SIZE - 1);
	if (IS_ERR(p))
		return PTR_ERR(p);

	ret = strlen(p);
	memmove(buf, p, ret);
	buf[ret++] = '\n';
	return ret;
}

static ssize_t backing_dev_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t len)
{
	char *file_name;
	size_t sz;
	struct file *backing_dev = NULL;
	struct inode *inode;
	unsigned int bitmap_sz;
	unsigned long nr_pages, *bitmap = NULL;
	int err;
	struct zram *zram = dev_to_zram(dev);

	file_name = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!file_name)
		return -ENOMEM;

	guard(rwsem_write)(&zram->dev_lock);
	if (init_done(zram)) {
		pr_info("Can't setup backing device for initialized device\n");
		err = -EBUSY;
		goto out;
	}

	strscpy(file_name, buf, PATH_MAX);
	/* ignore trailing newline */
	sz = strlen(file_name);
	if (sz > 0 && file_name[sz - 1] == '\n')
		file_name[sz - 1] = 0x00;

	backing_dev = filp_open(file_name, O_RDWR | O_LARGEFILE | O_EXCL, 0);
	if (IS_ERR(backing_dev)) {
		err = PTR_ERR(backing_dev);
		backing_dev = NULL;
		goto out;
	}

	inode = backing_dev->f_mapping->host;

	/* Support only block device in this moment */
	if (!S_ISBLK(inode->i_mode)) {
		err = -ENOTBLK;
		goto out;
	}

	nr_pages = i_size_read(inode) >> PAGE_SHIFT;
	/* Refuse to use zero sized device (also prevents self reference) */
	if (!nr_pages) {
		err = -EINVAL;
		goto out;
	}

	bitmap_sz = BITS_TO_LONGS(nr_pages) * sizeof(long);
	bitmap = kvzalloc(bitmap_sz, GFP_KERNEL);
	if (!bitmap) {
		err = -ENOMEM;
		goto out;
	}

	reset_bdev(zram);

	zram->bdev = I_BDEV(inode);
	zram->backing_dev = backing_dev;
	zram->bitmap = bitmap;
	zram->nr_pages = nr_pages;

	pr_info("setup backing device %s\n", file_name);
	kfree(file_name);

	return len;
out:
	kvfree(bitmap);

	if (backing_dev)
		filp_close(backing_dev, NULL);

	kfree(file_name);

	return err;
}

static unsigned long zram_reserve_bdev_block(struct zram *zram)
{
	unsigned long blk_idx;

	blk_idx = find_next_zero_bit(zram->bitmap, zram->nr_pages, 0);
	if (blk_idx == zram->nr_pages)
		return INVALID_BDEV_BLOCK;

	set_bit(blk_idx, zram->bitmap);
	atomic64_inc(&zram->stats.bd_count);
	return blk_idx;
}

static void zram_release_bdev_block(struct zram *zram, unsigned long blk_idx)
{
	int was_set;

	was_set = test_and_clear_bit(blk_idx, zram->bitmap);
	WARN_ON_ONCE(!was_set);
	atomic64_dec(&zram->stats.bd_count);
}

static void release_wb_req(struct zram_wb_req *req)
{
	__free_page(req->page);
	kfree(req);
}

static void release_wb_ctl(struct zram_wb_ctl *wb_ctl)
{
	if (!wb_ctl)
		return;

	/* We should never have inflight requests at this point */
	WARN_ON(atomic_read(&wb_ctl->num_inflight));
	WARN_ON(!list_empty(&wb_ctl->done_reqs));

	while (!list_empty(&wb_ctl->idle_reqs)) {
		struct zram_wb_req *req;

		req = list_first_entry(&wb_ctl->idle_reqs,
				       struct zram_wb_req, entry);
		list_del(&req->entry);
		release_wb_req(req);
	}

	kfree(wb_ctl);
}

static struct zram_wb_ctl *init_wb_ctl(struct zram *zram)
{
	struct zram_wb_ctl *wb_ctl;
	int i;

	wb_ctl = kmalloc(sizeof(*wb_ctl), GFP_KERNEL);
	if (!wb_ctl)
		return NULL;

	INIT_LIST_HEAD(&wb_ctl->idle_reqs);
	INIT_LIST_HEAD(&wb_ctl->done_reqs);
	atomic_set(&wb_ctl->num_inflight, 0);
	init_waitqueue_head(&wb_ctl->done_wait);
	spin_lock_init(&wb_ctl->done_lock);

	for (i = 0; i < zram->wb_batch_size; i++) {
		struct zram_wb_req *req;

		/*
		 * This is fatal condition only if we couldn't allocate
		 * any requests at all.  Otherwise we just work with the
		 * requests that we have successfully allocated, so that
		 * writeback can still proceed, even if there is only one
		 * request on the idle list.
		 */
		req = kzalloc(sizeof(*req), GFP_KERNEL | __GFP_NOWARN);
		if (!req)
			break;

		req->page = alloc_page(GFP_KERNEL | __GFP_NOWARN);
		if (!req->page) {
			kfree(req);
			break;
		}

		list_add(&req->entry, &wb_ctl->idle_reqs);
	}

	/* We couldn't allocate any requests, so writeabck is not possible */
	if (list_empty(&wb_ctl->idle_reqs))
		goto release_wb_ctl;

	return wb_ctl;

release_wb_ctl:
	release_wb_ctl(wb_ctl);
	return NULL;
}

static void zram_account_writeback_rollback(struct zram *zram)
{
	lockdep_assert_held_write(&zram->dev_lock);

	if (zram->wb_limit_enable)
		zram->bd_wb_limit +=  1UL << (PAGE_SHIFT - 12);
}

static void zram_account_writeback_submit(struct zram *zram)
{
	lockdep_assert_held_write(&zram->dev_lock);

	if (zram->wb_limit_enable && zram->bd_wb_limit > 0)
		zram->bd_wb_limit -=  1UL << (PAGE_SHIFT - 12);
}

static int zram_writeback_complete(struct zram *zram, struct zram_wb_req *req)
{
	u32 size, index = req->pps->index;
	int err, prio;
	bool huge;

	err = blk_status_to_errno(req->bio.bi_status);
	if (err) {
		/*
		 * Failed wb requests should not be accounted in wb_limit
		 * (if enabled).
		 */
		zram_account_writeback_rollback(zram);
		zram_release_bdev_block(zram, req->blk_idx);
		return err;
	}

	atomic64_inc(&zram->stats.bd_writes);
	slot_lock(zram, index);
	/*
	 * We release slot lock during writeback so slot can change under us:
	 * slot_free() or slot_free() and zram_write_page(). In both cases
	 * slot loses ZRAM_PP_SLOT flag. No concurrent post-processing can
	 * set ZRAM_PP_SLOT on such slots until current post-processing
	 * finishes.
	 */
	if (!test_slot_flag(zram, index, ZRAM_PP_SLOT)) {
		zram_release_bdev_block(zram, req->blk_idx);
		goto out;
	}

	if (zram->wb_compressed) {
		/*
		 * ZRAM_WB slots get freed, we need to preserve data required
		 * for read decompression.
		 */
		size = get_slot_size(zram, index);
		prio = get_slot_comp_priority(zram, index);
		huge = test_slot_flag(zram, index, ZRAM_HUGE);
	}

	slot_free(zram, index);
	set_slot_flag(zram, index, ZRAM_WB);
	set_slot_handle(zram, index, req->blk_idx);

	if (zram->wb_compressed) {
		if (huge)
			set_slot_flag(zram, index, ZRAM_HUGE);
		set_slot_size(zram, index, size);
		set_slot_comp_priority(zram, index, prio);
	}

	atomic64_inc(&zram->stats.pages_stored);

out:
	slot_unlock(zram, index);
	return 0;
}

static void zram_writeback_endio(struct bio *bio)
{
	struct zram_wb_req *req = container_of(bio, struct zram_wb_req, bio);
	struct zram_wb_ctl *wb_ctl = bio->bi_private;
	unsigned long flags;

	spin_lock_irqsave(&wb_ctl->done_lock, flags);
	list_add(&req->entry, &wb_ctl->done_reqs);
	spin_unlock_irqrestore(&wb_ctl->done_lock, flags);

	wake_up(&wb_ctl->done_wait);
}

static void zram_submit_wb_request(struct zram *zram,
				   struct zram_wb_ctl *wb_ctl,
				   struct zram_wb_req *req)
{
	/*
	 * wb_limit (if enabled) should be adjusted before submission,
	 * so that we don't over-submit.
	 */
	zram_account_writeback_submit(zram);
	atomic_inc(&wb_ctl->num_inflight);
	req->bio.bi_private = wb_ctl;
	submit_bio(&req->bio);
}

static int zram_complete_done_reqs(struct zram *zram,
				   struct zram_wb_ctl *wb_ctl)
{
	struct zram_wb_req *req;
	unsigned long flags;
	int ret = 0, err;

	while (atomic_read(&wb_ctl->num_inflight) > 0) {
		spin_lock_irqsave(&wb_ctl->done_lock, flags);
		req = list_first_entry_or_null(&wb_ctl->done_reqs,
					       struct zram_wb_req, entry);
		if (req)
			list_del(&req->entry);
		spin_unlock_irqrestore(&wb_ctl->done_lock, flags);

		/* ->num_inflight > 0 doesn't mean we have done requests */
		if (!req)
			break;

		err = zram_writeback_complete(zram, req);
		if (err)
			ret = err;

		atomic_dec(&wb_ctl->num_inflight);
		release_pp_slot(zram, req->pps);
		req->pps = NULL;

		list_add(&req->entry, &wb_ctl->idle_reqs);
	}

	return ret;
}

static struct zram_wb_req *zram_select_idle_req(struct zram_wb_ctl *wb_ctl)
{
	struct zram_wb_req *req;

	req = list_first_entry_or_null(&wb_ctl->idle_reqs,
				       struct zram_wb_req, entry);
	if (req)
		list_del(&req->entry);
	return req;
}

static int zram_writeback_slots(struct zram *zram,
				struct zram_pp_ctl *ctl,
				struct zram_wb_ctl *wb_ctl)
{
	unsigned long blk_idx = INVALID_BDEV_BLOCK;
	struct zram_wb_req *req = NULL;
	struct zram_pp_slot *pps;
	int ret = 0, err = 0;
	u32 index = 0;

	while ((pps = select_pp_slot(ctl))) {
		if (zram->wb_limit_enable && !zram->bd_wb_limit) {
			ret = -EIO;
			break;
		}

		while (!req) {
			req = zram_select_idle_req(wb_ctl);
			if (req)
				break;

			wait_event(wb_ctl->done_wait,
				   !list_empty(&wb_ctl->done_reqs));

			err = zram_complete_done_reqs(zram, wb_ctl);
			/*
			 * BIO errors are not fatal, we continue and simply
			 * attempt to writeback the remaining objects (pages).
			 * At the same time we need to signal user-space that
			 * some writes (at least one, but also could be all of
			 * them) were not successful and we do so by returning
			 * the most recent BIO error.
			 */
			if (err)
				ret = err;
		}

		if (blk_idx == INVALID_BDEV_BLOCK) {
			blk_idx = zram_reserve_bdev_block(zram);
			if (blk_idx == INVALID_BDEV_BLOCK) {
				ret = -ENOSPC;
				break;
			}
		}

		index = pps->index;
		slot_lock(zram, index);
		/*
		 * scan_slots() sets ZRAM_PP_SLOT and releases slot lock, so
		 * slots can change in the meantime. If slots are accessed or
		 * freed they lose ZRAM_PP_SLOT flag and hence we don't
		 * post-process them.
		 */
		if (!test_slot_flag(zram, index, ZRAM_PP_SLOT))
			goto next;
		if (zram->wb_compressed)
			err = read_from_zspool_raw(zram, req->page, index);
		else
			err = read_from_zspool(zram, req->page, index);
		if (err)
			goto next;
		slot_unlock(zram, index);

		/*
		 * From now on pp-slot is owned by the req, remove it from
		 * its pp bucket.
		 */
		list_del_init(&pps->entry);

		req->blk_idx = blk_idx;
		req->pps = pps;
		bio_init(&req->bio, zram->bdev, &req->bio_vec, 1, REQ_OP_WRITE);
		req->bio.bi_iter.bi_sector = req->blk_idx * (PAGE_SIZE >> 9);
		req->bio.bi_end_io = zram_writeback_endio;
		__bio_add_page(&req->bio, req->page, PAGE_SIZE, 0);

		zram_submit_wb_request(zram, wb_ctl, req);
		blk_idx = INVALID_BDEV_BLOCK;
		req = NULL;
		cond_resched();
		continue;

next:
		slot_unlock(zram, index);
		release_pp_slot(zram, pps);
	}

	/*
	 * Selected idle req, but never submitted it due to some error or
	 * wb limit.
	 */
	if (req)
		release_wb_req(req);

	while (atomic_read(&wb_ctl->num_inflight) > 0) {
		wait_event(wb_ctl->done_wait, !list_empty(&wb_ctl->done_reqs));
		err = zram_complete_done_reqs(zram, wb_ctl);
		if (err)
			ret = err;
	}

	return ret;
}

#define PAGE_WRITEBACK			0
#define HUGE_WRITEBACK			(1 << 0)
#define IDLE_WRITEBACK			(1 << 1)
#define INCOMPRESSIBLE_WRITEBACK	(1 << 2)

static int parse_page_index(char *val, unsigned long nr_pages,
			    unsigned long *lo, unsigned long *hi)
{
	int ret;

	ret = kstrtoul(val, 10, lo);
	if (ret)
		return ret;
	if (*lo >= nr_pages)
		return -ERANGE;
	*hi = *lo + 1;
	return 0;
}

static int parse_page_indexes(char *val, unsigned long nr_pages,
			      unsigned long *lo, unsigned long *hi)
{
	char *delim;
	int ret;

	delim = strchr(val, '-');
	if (!delim)
		return -EINVAL;

	*delim = 0x00;
	ret = kstrtoul(val, 10, lo);
	if (ret)
		return ret;
	if (*lo >= nr_pages)
		return -ERANGE;

	ret = kstrtoul(delim + 1, 10, hi);
	if (ret)
		return ret;
	if (*hi >= nr_pages || *lo > *hi)
		return -ERANGE;
	*hi += 1;
	return 0;
}

static int parse_mode(char *val, u32 *mode)
{
	*mode = 0;

	if (!strcmp(val, "idle"))
		*mode = IDLE_WRITEBACK;
	if (!strcmp(val, "huge"))
		*mode = HUGE_WRITEBACK;
	if (!strcmp(val, "huge_idle"))
		*mode = IDLE_WRITEBACK | HUGE_WRITEBACK;
	if (!strcmp(val, "incompressible"))
		*mode = INCOMPRESSIBLE_WRITEBACK;

	if (*mode == 0)
		return -EINVAL;
	return 0;
}

static int scan_slots_for_writeback(struct zram *zram, u32 mode,
				    unsigned long lo, unsigned long hi,
				    struct zram_pp_ctl *ctl)
{
	u32 index = lo;

	while (index < hi) {
		bool ok = true;

		slot_lock(zram, index);
		if (!slot_allocated(zram, index))
			goto next;

		if (test_slot_flag(zram, index, ZRAM_WB) ||
		    test_slot_flag(zram, index, ZRAM_SAME))
			goto next;

		if (mode & IDLE_WRITEBACK &&
		    !test_slot_flag(zram, index, ZRAM_IDLE))
			goto next;
		if (mode & HUGE_WRITEBACK &&
		    !test_slot_flag(zram, index, ZRAM_HUGE))
			goto next;
		if (mode & INCOMPRESSIBLE_WRITEBACK &&
		    !test_slot_flag(zram, index, ZRAM_INCOMPRESSIBLE))
			goto next;

		ok = place_pp_slot(zram, ctl, index);
next:
		slot_unlock(zram, index);
		if (!ok)
			break;
		index++;
	}

	return 0;
}

static ssize_t writeback_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t len)
{
	struct zram *zram = dev_to_zram(dev);
	u64 nr_pages = zram->disksize >> PAGE_SHIFT;
	unsigned long lo = 0, hi = nr_pages;
	struct zram_pp_ctl *pp_ctl = NULL;
	struct zram_wb_ctl *wb_ctl = NULL;
	char *args, *param, *val;
	ssize_t ret = len;
	int err, mode = 0;

	guard(rwsem_write)(&zram->dev_lock);
	if (!init_done(zram))
		return -EINVAL;

	if (!zram->backing_dev)
		return -ENODEV;

	pp_ctl = init_pp_ctl();
	if (!pp_ctl)
		return -ENOMEM;

	wb_ctl = init_wb_ctl(zram);
	if (!wb_ctl) {
		ret = -ENOMEM;
		goto out;
	}

	args = skip_spaces(buf);
	while (*args) {
		args = next_arg(args, &param, &val);

		/*
		 * Workaround to support the old writeback interface.
		 *
		 * The old writeback interface has a minor inconsistency and
		 * requires key=value only for page_index parameter, while the
		 * writeback mode is a valueless parameter.
		 *
		 * This is not the case anymore and now all parameters are
		 * required to have values, however, we need to support the
		 * legacy writeback interface format so we check if we can
		 * recognize a valueless parameter as the (legacy) writeback
		 * mode.
		 */
		if (!val || !*val) {
			err = parse_mode(param, &mode);
			if (err) {
				ret = err;
				goto out;
			}

			scan_slots_for_writeback(zram, mode, lo, hi, pp_ctl);
			break;
		}

		if (!strcmp(param, "type")) {
			err = parse_mode(val, &mode);
			if (err) {
				ret = err;
				goto out;
			}

			scan_slots_for_writeback(zram, mode, lo, hi, pp_ctl);
			break;
		}

		if (!strcmp(param, "page_index")) {
			err = parse_page_index(val, nr_pages, &lo, &hi);
			if (err) {
				ret = err;
				goto out;
			}

			scan_slots_for_writeback(zram, mode, lo, hi, pp_ctl);
			continue;
		}

		if (!strcmp(param, "page_indexes")) {
			err = parse_page_indexes(val, nr_pages, &lo, &hi);
			if (err) {
				ret = err;
				goto out;
			}

			scan_slots_for_writeback(zram, mode, lo, hi, pp_ctl);
			continue;
		}
	}

	err = zram_writeback_slots(zram, pp_ctl, wb_ctl);
	if (err)
		ret = err;

out:
	release_pp_ctl(zram, pp_ctl);
	release_wb_ctl(wb_ctl);

	return ret;
}

static int decompress_bdev_page(struct zram *zram, struct page *page, u32 index)
{
	struct zcomp_strm *zstrm;
	unsigned int size;
	int ret, prio;
	void *src;

	slot_lock(zram, index);
	/* Since slot was unlocked we need to make sure it's still ZRAM_WB */
	if (!test_slot_flag(zram, index, ZRAM_WB)) {
		slot_unlock(zram, index);
		/* We read some stale data, zero it out */
		memset_page(page, 0, 0, PAGE_SIZE);
		return -EIO;
	}

	if (test_slot_flag(zram, index, ZRAM_HUGE)) {
		slot_unlock(zram, index);
		return 0;
	}

	size = get_slot_size(zram, index);
	prio = get_slot_comp_priority(zram, index);

	zstrm = zcomp_stream_get(zram->comps[prio]);
	src = kmap_local_page(page);
	ret = zcomp_decompress(zram->comps[prio], zstrm, src, size,
			       zstrm->local_copy);
	if (!ret)
		copy_page(src, zstrm->local_copy);
	kunmap_local(src);
	zcomp_stream_put(zstrm);
	slot_unlock(zram, index);

	return ret;
}

static void zram_deferred_decompress(struct work_struct *w)
{
	struct zram_rb_req *req = container_of(w, struct zram_rb_req, work);
	struct page *page = bio_first_page_all(req->bio);
	struct zram *zram = req->zram;
	u32 index = req->index;
	int ret;

	ret = decompress_bdev_page(zram, page, index);
	if (ret)
		req->parent->bi_status = BLK_STS_IOERR;

	/* Decrement parent's ->remaining */
	bio_endio(req->parent);
	bio_put(req->bio);
	kfree(req);
}

static void zram_async_read_endio(struct bio *bio)
{
	struct zram_rb_req *req = bio->bi_private;
	struct zram *zram = req->zram;

	if (bio->bi_status) {
		req->parent->bi_status = bio->bi_status;
		bio_endio(req->parent);
		bio_put(bio);
		kfree(req);
		return;
	}

	/*
	 * NOTE: zram_async_read_endio() is not exactly right place for this.
	 * Ideally, we need to do it after ZRAM_WB check, but this requires
	 * us to use wq path even on systems that don't enable compressed
	 * writeback, because we cannot take slot-lock in the current context.
	 *
	 * Keep the existing behavior for now.
	 */
	if (zram->wb_compressed == false) {
		/* No decompression needed, complete the parent IO */
		bio_endio(req->parent);
		bio_put(bio);
		kfree(req);
		return;
	}

	/*
	 * zram decompression is sleepable, so we need to deffer it to
	 * a preemptible context.
	 */
	INIT_WORK(&req->work, zram_deferred_decompress);
	queue_work(system_highpri_wq, &req->work);
}

static void read_from_bdev_async(struct zram *zram, struct page *page,
				 u32 index, unsigned long blk_idx,
				 struct bio *parent)
{
	struct zram_rb_req *req;
	struct bio *bio;

	req = kmalloc(sizeof(*req), GFP_NOIO);
	if (!req)
		return;

	bio = bio_alloc(zram->bdev, 1, parent->bi_opf, GFP_NOIO);
	if (!bio) {
		kfree(req);
		return;
	}

	req->zram = zram;
	req->index = index;
	req->blk_idx = blk_idx;
	req->bio = bio;
	req->parent = parent;

	bio->bi_iter.bi_sector = blk_idx * (PAGE_SIZE >> 9);
	bio->bi_private = req;
	bio->bi_end_io = zram_async_read_endio;

	__bio_add_page(bio, page, PAGE_SIZE, 0);
	bio_inc_remaining(parent);
	submit_bio(bio);
}

static void zram_sync_read(struct work_struct *w)
{
	struct zram_rb_req *req = container_of(w, struct zram_rb_req, work);
	struct bio_vec bv;
	struct bio bio;

	bio_init(&bio, req->zram->bdev, &bv, 1, REQ_OP_READ);
	bio.bi_iter.bi_sector = req->blk_idx * (PAGE_SIZE >> 9);
	__bio_add_page(&bio, req->page, PAGE_SIZE, 0);
	req->error = submit_bio_wait(&bio);
}

/*
 * Block layer want one ->submit_bio to be active at a time, so if we use
 * chained IO with parent IO in same context, it's a deadlock. To avoid that,
 * use a worker thread context.
 */
static int read_from_bdev_sync(struct zram *zram, struct page *page, u32 index,
			       unsigned long blk_idx)
{
	struct zram_rb_req req;

	req.page = page;
	req.zram = zram;
	req.blk_idx = blk_idx;

	INIT_WORK_ONSTACK(&req.work, zram_sync_read);
	queue_work(system_dfl_wq, &req.work);
	flush_work(&req.work);
	destroy_work_on_stack(&req.work);

	if (req.error || zram->wb_compressed == false)
		return req.error;

	return decompress_bdev_page(zram, page, index);
}

static int read_from_bdev(struct zram *zram, struct page *page, u32 index,
			  unsigned long blk_idx, struct bio *parent)
{
	atomic64_inc(&zram->stats.bd_reads);
	if (!parent) {
		if (WARN_ON_ONCE(!IS_ENABLED(ZRAM_PARTIAL_IO)))
			return -EIO;
		return read_from_bdev_sync(zram, page, index, blk_idx);
	}
	read_from_bdev_async(zram, page, index, blk_idx, parent);
	return 0;
}
#else
static inline void reset_bdev(struct zram *zram) {};
static int read_from_bdev(struct zram *zram, struct page *page, u32 index,
			  unsigned long blk_idx, struct bio *parent)
{
	return -EIO;
}

static void zram_release_bdev_block(struct zram *zram, unsigned long blk_idx)
{
}
#endif

#ifdef CONFIG_ZRAM_MEMORY_TRACKING

static struct dentry *zram_debugfs_root;

static void zram_debugfs_create(void)
{
	zram_debugfs_root = debugfs_create_dir("zram", NULL);
}

static void zram_debugfs_destroy(void)
{
	debugfs_remove_recursive(zram_debugfs_root);
}

static ssize_t read_block_state(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	char *kbuf;
	ssize_t index, written = 0;
	struct zram *zram = file->private_data;
	unsigned long nr_pages = zram->disksize >> PAGE_SHIFT;

	kbuf = kvmalloc(count, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	guard(rwsem_read)(&zram->dev_lock);
	if (!init_done(zram)) {
		kvfree(kbuf);
		return -EINVAL;
	}

	for (index = *ppos; index < nr_pages; index++) {
		int copied;

		slot_lock(zram, index);
		if (!slot_allocated(zram, index))
			goto next;

		copied = snprintf(kbuf + written, count,
			"%12zd %12u.%06d %c%c%c%c%c%c\n",
			index, zram->table[index].attr.ac_time, 0,
			test_slot_flag(zram, index, ZRAM_SAME) ? 's' : '.',
			test_slot_flag(zram, index, ZRAM_WB) ? 'w' : '.',
			test_slot_flag(zram, index, ZRAM_HUGE) ? 'h' : '.',
			test_slot_flag(zram, index, ZRAM_IDLE) ? 'i' : '.',
			get_slot_comp_priority(zram, index) ? 'r' : '.',
			test_slot_flag(zram, index,
				       ZRAM_INCOMPRESSIBLE) ? 'n' : '.');

		if (count <= copied) {
			slot_unlock(zram, index);
			break;
		}
		written += copied;
		count -= copied;
next:
		slot_unlock(zram, index);
		*ppos += 1;
	}

	if (copy_to_user(buf, kbuf, written))
		written = -EFAULT;
	kvfree(kbuf);

	return written;
}

static const struct file_operations proc_zram_block_state_op = {
	.open = simple_open,
	.read = read_block_state,
	.llseek = default_llseek,
};

static void zram_debugfs_register(struct zram *zram)
{
	if (!zram_debugfs_root)
		return;

	zram->debugfs_dir = debugfs_create_dir(zram->disk->disk_name,
						zram_debugfs_root);
	debugfs_create_file("block_state", 0400, zram->debugfs_dir,
				zram, &proc_zram_block_state_op);
}

static void zram_debugfs_unregister(struct zram *zram)
{
	debugfs_remove_recursive(zram->debugfs_dir);
}
#else
static void zram_debugfs_create(void) {};
static void zram_debugfs_destroy(void) {};
static void zram_debugfs_register(struct zram *zram) {};
static void zram_debugfs_unregister(struct zram *zram) {};
#endif

static void comp_algorithm_set(struct zram *zram, u32 prio, const char *alg)
{
	/* Do not free statically defined compression algorithms */
	if (zram->comp_algs[prio] != default_compressor)
		kfree(zram->comp_algs[prio]);

	zram->comp_algs[prio] = alg;
}

static int __comp_algorithm_store(struct zram *zram, u32 prio, const char *buf)
{
	char *compressor;
	size_t sz;

	sz = strlen(buf);
	if (sz >= ZRAM_MAX_ALGO_NAME_SZ)
		return -E2BIG;

	compressor = kstrdup(buf, GFP_KERNEL);
	if (!compressor)
		return -ENOMEM;

	/* ignore trailing newline */
	if (sz > 0 && compressor[sz - 1] == '\n')
		compressor[sz - 1] = 0x00;

	if (!zcomp_available_algorithm(compressor)) {
		kfree(compressor);
		return -EINVAL;
	}

	guard(rwsem_write)(&zram->dev_lock);
	if (init_done(zram)) {
		kfree(compressor);
		pr_info("Can't change algorithm for initialized device\n");
		return -EBUSY;
	}

	comp_algorithm_set(zram, prio, compressor);
	return 0;
}

static void comp_params_reset(struct zram *zram, u32 prio)
{
	struct zcomp_params *params = &zram->params[prio];

	vfree(params->dict);
	params->level = ZCOMP_PARAM_NOT_SET;
	params->deflate.winbits = ZCOMP_PARAM_NOT_SET;
	params->dict_sz = 0;
	params->dict = NULL;
}

static int comp_params_store(struct zram *zram, u32 prio, s32 level,
			     const char *dict_path,
			     struct deflate_params *deflate_params)
{
	ssize_t sz = 0;

	comp_params_reset(zram, prio);

	if (dict_path) {
		sz = kernel_read_file_from_path(dict_path, 0,
						&zram->params[prio].dict,
						INT_MAX,
						NULL,
						READING_POLICY);
		if (sz < 0)
			return -EINVAL;
	}

	zram->params[prio].dict_sz = sz;
	zram->params[prio].level = level;
	zram->params[prio].deflate.winbits = deflate_params->winbits;
	return 0;
}

static ssize_t algorithm_params_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t len)
{
	s32 prio = ZRAM_PRIMARY_COMP, level = ZCOMP_PARAM_NOT_SET;
	char *args, *param, *val, *algo = NULL, *dict_path = NULL;
	struct deflate_params deflate_params;
	struct zram *zram = dev_to_zram(dev);
	int ret;

	deflate_params.winbits = ZCOMP_PARAM_NOT_SET;

	args = skip_spaces(buf);
	while (*args) {
		args = next_arg(args, &param, &val);

		if (!val || !*val)
			return -EINVAL;

		if (!strcmp(param, "priority")) {
			ret = kstrtoint(val, 10, &prio);
			if (ret)
				return ret;
			continue;
		}

		if (!strcmp(param, "level")) {
			ret = kstrtoint(val, 10, &level);
			if (ret)
				return ret;
			continue;
		}

		if (!strcmp(param, "algo")) {
			algo = val;
			continue;
		}

		if (!strcmp(param, "dict")) {
			dict_path = val;
			continue;
		}

		if (!strcmp(param, "deflate.winbits")) {
			ret = kstrtoint(val, 10, &deflate_params.winbits);
			if (ret)
				return ret;
			continue;
		}
	}

	/* Lookup priority by algorithm name */
	if (algo) {
		s32 p;

		prio = -EINVAL;
		for (p = ZRAM_PRIMARY_COMP; p < ZRAM_MAX_COMPS; p++) {
			if (!zram->comp_algs[p])
				continue;

			if (!strcmp(zram->comp_algs[p], algo)) {
				prio = p;
				break;
			}
		}
	}

	if (prio < ZRAM_PRIMARY_COMP || prio >= ZRAM_MAX_COMPS)
		return -EINVAL;

	ret = comp_params_store(zram, prio, level, dict_path, &deflate_params);
	return ret ? ret : len;
}

static ssize_t comp_algorithm_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct zram *zram = dev_to_zram(dev);
	ssize_t sz;

	guard(rwsem_read)(&zram->dev_lock);
	sz = zcomp_available_show(zram->comp_algs[ZRAM_PRIMARY_COMP], buf, 0);
	return sz;
}

static ssize_t comp_algorithm_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t len)
{
	struct zram *zram = dev_to_zram(dev);
	int ret;

	ret = __comp_algorithm_store(zram, ZRAM_PRIMARY_COMP, buf);
	return ret ? ret : len;
}

#ifdef CONFIG_ZRAM_MULTI_COMP
static ssize_t recomp_algorithm_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct zram *zram = dev_to_zram(dev);
	ssize_t sz = 0;
	u32 prio;

	guard(rwsem_read)(&zram->dev_lock);
	for (prio = ZRAM_SECONDARY_COMP; prio < ZRAM_MAX_COMPS; prio++) {
		if (!zram->comp_algs[prio])
			continue;

		sz += sysfs_emit_at(buf, sz, "#%d: ", prio);
		sz += zcomp_available_show(zram->comp_algs[prio], buf, sz);
	}
	return sz;
}

static ssize_t recomp_algorithm_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t len)
{
	struct zram *zram = dev_to_zram(dev);
	int prio = ZRAM_SECONDARY_COMP;
	char *args, *param, *val;
	char *alg = NULL;
	int ret;

	args = skip_spaces(buf);
	while (*args) {
		args = next_arg(args, &param, &val);

		if (!val || !*val)
			return -EINVAL;

		if (!strcmp(param, "algo")) {
			alg = val;
			continue;
		}

		if (!strcmp(param, "priority")) {
			ret = kstrtoint(val, 10, &prio);
			if (ret)
				return ret;
			continue;
		}
	}

	if (!alg)
		return -EINVAL;

	if (prio < ZRAM_SECONDARY_COMP || prio >= ZRAM_MAX_COMPS)
		return -EINVAL;

	ret = __comp_algorithm_store(zram, prio, alg);
	return ret ? ret : len;
}
#endif

static ssize_t compact_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct zram *zram = dev_to_zram(dev);

	guard(rwsem_read)(&zram->dev_lock);
	if (!init_done(zram))
		return -EINVAL;

	zs_compact(zram->mem_pool);

	return len;
}

static ssize_t io_stat_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct zram *zram = dev_to_zram(dev);
	ssize_t ret;

	guard(rwsem_read)(&zram->dev_lock);
	ret = sysfs_emit(buf,
			"%8llu %8llu 0 %8llu\n",
			(u64)atomic64_read(&zram->stats.failed_reads),
			(u64)atomic64_read(&zram->stats.failed_writes),
			(u64)atomic64_read(&zram->stats.notify_free));

	return ret;
}

static ssize_t mm_stat_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct zram *zram = dev_to_zram(dev);
	struct zs_pool_stats pool_stats;
	u64 orig_size, mem_used = 0;
	long max_used;
	ssize_t ret;

	memset(&pool_stats, 0x00, sizeof(struct zs_pool_stats));

	guard(rwsem_read)(&zram->dev_lock);
	if (init_done(zram)) {
		mem_used = zs_get_total_pages(zram->mem_pool);
		zs_pool_stats(zram->mem_pool, &pool_stats);
	}

	orig_size = atomic64_read(&zram->stats.pages_stored);
	max_used = atomic_long_read(&zram->stats.max_used_pages);

	ret = sysfs_emit(buf,
			"%8llu %8llu %8llu %8lu %8ld %8llu %8lu %8llu %8llu\n",
			orig_size << PAGE_SHIFT,
			(u64)atomic64_read(&zram->stats.compr_data_size),
			mem_used << PAGE_SHIFT,
			zram->limit_pages << PAGE_SHIFT,
			max_used << PAGE_SHIFT,
			(u64)atomic64_read(&zram->stats.same_pages),
			atomic_long_read(&pool_stats.pages_compacted),
			(u64)atomic64_read(&zram->stats.huge_pages),
			(u64)atomic64_read(&zram->stats.huge_pages_since));

	return ret;
}

static ssize_t debug_stat_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int version = 1;
	struct zram *zram = dev_to_zram(dev);
	ssize_t ret;

	guard(rwsem_read)(&zram->dev_lock);
	ret = sysfs_emit(buf,
			"version: %d\n0 %8llu\n",
			version,
			(u64)atomic64_read(&zram->stats.miss_free));

	return ret;
}

static void zram_meta_free(struct zram *zram, u64 disksize)
{
	size_t num_pages = disksize >> PAGE_SHIFT;
	size_t index;

	if (!zram->table)
		return;

	/* Free all pages that are still in this zram device */
	for (index = 0; index < num_pages; index++)
		slot_free(zram, index);

	zs_destroy_pool(zram->mem_pool);
	vfree(zram->table);
	zram->table = NULL;
}

static bool zram_meta_alloc(struct zram *zram, u64 disksize)
{
	size_t num_pages, index;

	num_pages = disksize >> PAGE_SHIFT;
	zram->table = vzalloc(array_size(num_pages, sizeof(*zram->table)));
	if (!zram->table)
		return false;

	zram->mem_pool = zs_create_pool(zram->disk->disk_name);
	if (!zram->mem_pool) {
		vfree(zram->table);
		zram->table = NULL;
		return false;
	}

	if (!huge_class_size)
		huge_class_size = zs_huge_class_size(zram->mem_pool);

	for (index = 0; index < num_pages; index++)
		slot_lock_init(zram, index);

	return true;
}

static void slot_free(struct zram *zram, u32 index)
{
	unsigned long handle;

#ifdef CONFIG_ZRAM_TRACK_ENTRY_ACTIME
	zram->table[index].attr.ac_time = 0;
#endif

	clear_slot_flag(zram, index, ZRAM_IDLE);
	clear_slot_flag(zram, index, ZRAM_INCOMPRESSIBLE);
	clear_slot_flag(zram, index, ZRAM_PP_SLOT);
	set_slot_comp_priority(zram, index, 0);

	if (test_slot_flag(zram, index, ZRAM_HUGE)) {
		clear_slot_flag(zram, index, ZRAM_HUGE);
		atomic64_dec(&zram->stats.huge_pages);
	}

	if (test_slot_flag(zram, index, ZRAM_WB)) {
		clear_slot_flag(zram, index, ZRAM_WB);
		zram_release_bdev_block(zram, get_slot_handle(zram, index));
		goto out;
	}

	/*
	 * No memory is allocated for same element filled pages.
	 * Simply clear same page flag.
	 */
	if (test_slot_flag(zram, index, ZRAM_SAME)) {
		clear_slot_flag(zram, index, ZRAM_SAME);
		atomic64_dec(&zram->stats.same_pages);
		goto out;
	}

	handle = get_slot_handle(zram, index);
	if (!handle)
		return;

	zs_free(zram->mem_pool, handle);

	atomic64_sub(get_slot_size(zram, index),
		     &zram->stats.compr_data_size);
out:
	atomic64_dec(&zram->stats.pages_stored);
	set_slot_handle(zram, index, 0);
	set_slot_size(zram, index, 0);
}

static int read_same_filled_page(struct zram *zram, struct page *page,
				 u32 index)
{
	void *mem;

	mem = kmap_local_page(page);
	zram_fill_page(mem, PAGE_SIZE, get_slot_handle(zram, index));
	kunmap_local(mem);
	return 0;
}

static int read_incompressible_page(struct zram *zram, struct page *page,
				    u32 index)
{
	unsigned long handle;
	void *src, *dst;

	handle = get_slot_handle(zram, index);
	src = zs_obj_read_begin(zram->mem_pool, handle, PAGE_SIZE, NULL);
	dst = kmap_local_page(page);
	copy_page(dst, src);
	kunmap_local(dst);
	zs_obj_read_end(zram->mem_pool, handle, PAGE_SIZE, src);

	return 0;
}

static int read_compressed_page(struct zram *zram, struct page *page, u32 index)
{
	struct zcomp_strm *zstrm;
	unsigned long handle;
	unsigned int size;
	void *src, *dst;
	int ret, prio;

	handle = get_slot_handle(zram, index);
	size = get_slot_size(zram, index);
	prio = get_slot_comp_priority(zram, index);

	zstrm = zcomp_stream_get(zram->comps[prio]);
	src = zs_obj_read_begin(zram->mem_pool, handle, size,
				zstrm->local_copy);
	dst = kmap_local_page(page);
	ret = zcomp_decompress(zram->comps[prio], zstrm, src, size, dst);
	kunmap_local(dst);
	zs_obj_read_end(zram->mem_pool, handle, size, src);
	zcomp_stream_put(zstrm);

	return ret;
}

#if defined CONFIG_ZRAM_WRITEBACK
static int read_from_zspool_raw(struct zram *zram, struct page *page, u32 index)
{
	struct zcomp_strm *zstrm;
	unsigned long handle;
	unsigned int size;
	void *src;

	handle = get_slot_handle(zram, index);
	size = get_slot_size(zram, index);

	/*
	 * We need to get stream just for ->local_copy buffer, in
	 * case if object spans two physical pages. No decompression
	 * takes place here, as we read raw compressed data.
	 */
	zstrm = zcomp_stream_get(zram->comps[ZRAM_PRIMARY_COMP]);
	src = zs_obj_read_begin(zram->mem_pool, handle, size,
				zstrm->local_copy);
	memcpy_to_page(page, 0, src, size);
	zs_obj_read_end(zram->mem_pool, handle, size, src);
	zcomp_stream_put(zstrm);

	return 0;
}
#endif

/*
 * Reads (decompresses if needed) a page from zspool (zsmalloc).
 * Corresponding ZRAM slot should be locked.
 */
static int read_from_zspool(struct zram *zram, struct page *page, u32 index)
{
	if (test_slot_flag(zram, index, ZRAM_SAME) ||
	    !get_slot_handle(zram, index))
		return read_same_filled_page(zram, page, index);

	if (!test_slot_flag(zram, index, ZRAM_HUGE))
		return read_compressed_page(zram, page, index);
	else
		return read_incompressible_page(zram, page, index);
}

static int zram_read_page(struct zram *zram, struct page *page, u32 index,
			  struct bio *parent)
{
	int ret;

	slot_lock(zram, index);
	if (!test_slot_flag(zram, index, ZRAM_WB)) {
		/* Slot should be locked through out the function call */
		ret = read_from_zspool(zram, page, index);
		slot_unlock(zram, index);
	} else {
		unsigned long blk_idx = get_slot_handle(zram, index);

		/*
		 * The slot should be unlocked before reading from the backing
		 * device.
		 */
		slot_unlock(zram, index);
		ret = read_from_bdev(zram, page, index, blk_idx, parent);
	}

	/* Should NEVER happen. Return bio error if it does. */
	if (WARN_ON(ret < 0))
		pr_err("Decompression failed! err=%d, page=%u\n", ret, index);

	return ret;
}

/*
 * Use a temporary buffer to decompress the page, as the decompressor
 * always expects a full page for the output.
 */
static int zram_bvec_read_partial(struct zram *zram, struct bio_vec *bvec,
				  u32 index, int offset)
{
	struct page *page = alloc_page(GFP_NOIO);
	int ret;

	if (!page)
		return -ENOMEM;
	ret = zram_read_page(zram, page, index, NULL);
	if (likely(!ret))
		memcpy_to_bvec(bvec, page_address(page) + offset);
	__free_page(page);
	return ret;
}

static int zram_bvec_read(struct zram *zram, struct bio_vec *bvec,
			  u32 index, int offset, struct bio *bio)
{
	if (is_partial_io(bvec))
		return zram_bvec_read_partial(zram, bvec, index, offset);
	return zram_read_page(zram, bvec->bv_page, index, bio);
}

static int write_same_filled_page(struct zram *zram, unsigned long fill,
				  u32 index)
{
	slot_lock(zram, index);
	slot_free(zram, index);
	set_slot_flag(zram, index, ZRAM_SAME);
	set_slot_handle(zram, index, fill);
	slot_unlock(zram, index);

	atomic64_inc(&zram->stats.same_pages);
	atomic64_inc(&zram->stats.pages_stored);

	return 0;
}

static int write_incompressible_page(struct zram *zram, struct page *page,
				     u32 index)
{
	unsigned long handle;
	void *src;

	/*
	 * This function is called from preemptible context so we don't need
	 * to do optimistic and fallback to pessimistic handle allocation,
	 * like we do for compressible pages.
	 */
	handle = zs_malloc(zram->mem_pool, PAGE_SIZE,
			   GFP_NOIO | __GFP_NOWARN |
			   __GFP_HIGHMEM | __GFP_MOVABLE, page_to_nid(page));
	if (IS_ERR_VALUE(handle))
		return PTR_ERR((void *)handle);

	if (!zram_can_store_page(zram)) {
		zs_free(zram->mem_pool, handle);
		return -ENOMEM;
	}

	src = kmap_local_page(page);
	zs_obj_write(zram->mem_pool, handle, src, PAGE_SIZE);
	kunmap_local(src);

	slot_lock(zram, index);
	slot_free(zram, index);
	set_slot_flag(zram, index, ZRAM_HUGE);
	set_slot_handle(zram, index, handle);
	set_slot_size(zram, index, PAGE_SIZE);
	slot_unlock(zram, index);

	atomic64_add(PAGE_SIZE, &zram->stats.compr_data_size);
	atomic64_inc(&zram->stats.huge_pages);
	atomic64_inc(&zram->stats.huge_pages_since);
	atomic64_inc(&zram->stats.pages_stored);

	return 0;
}

static int zram_write_page(struct zram *zram, struct page *page, u32 index)
{
	int ret = 0;
	unsigned long handle;
	unsigned int comp_len;
	void *mem;
	struct zcomp_strm *zstrm;
	unsigned long element;
	bool same_filled;

	mem = kmap_local_page(page);
	same_filled = page_same_filled(mem, &element);
	kunmap_local(mem);
	if (same_filled)
		return write_same_filled_page(zram, element, index);

	zstrm = zcomp_stream_get(zram->comps[ZRAM_PRIMARY_COMP]);
	mem = kmap_local_page(page);
	ret = zcomp_compress(zram->comps[ZRAM_PRIMARY_COMP], zstrm,
			     mem, &comp_len);
	kunmap_local(mem);

	if (unlikely(ret)) {
		zcomp_stream_put(zstrm);
		pr_err("Compression failed! err=%d\n", ret);
		return ret;
	}

	if (comp_len >= huge_class_size) {
		zcomp_stream_put(zstrm);
		return write_incompressible_page(zram, page, index);
	}

	handle = zs_malloc(zram->mem_pool, comp_len,
			   GFP_NOIO | __GFP_NOWARN |
			   __GFP_HIGHMEM | __GFP_MOVABLE, page_to_nid(page));
	if (IS_ERR_VALUE(handle)) {
		zcomp_stream_put(zstrm);
		return PTR_ERR((void *)handle);
	}

	if (!zram_can_store_page(zram)) {
		zcomp_stream_put(zstrm);
		zs_free(zram->mem_pool, handle);
		return -ENOMEM;
	}

	zs_obj_write(zram->mem_pool, handle, zstrm->buffer, comp_len);
	zcomp_stream_put(zstrm);

	slot_lock(zram, index);
	slot_free(zram, index);
	set_slot_handle(zram, index, handle);
	set_slot_size(zram, index, comp_len);
	slot_unlock(zram, index);

	/* Update stats */
	atomic64_inc(&zram->stats.pages_stored);
	atomic64_add(comp_len, &zram->stats.compr_data_size);

	return ret;
}

/*
 * This is a partial IO. Read the full page before writing the changes.
 */
static int zram_bvec_write_partial(struct zram *zram, struct bio_vec *bvec,
				   u32 index, int offset, struct bio *bio)
{
	struct page *page = alloc_page(GFP_NOIO);
	int ret;

	if (!page)
		return -ENOMEM;

	ret = zram_read_page(zram, page, index, bio);
	if (!ret) {
		memcpy_from_bvec(page_address(page) + offset, bvec);
		ret = zram_write_page(zram, page, index);
	}
	__free_page(page);
	return ret;
}

static int zram_bvec_write(struct zram *zram, struct bio_vec *bvec,
			   u32 index, int offset, struct bio *bio)
{
	if (is_partial_io(bvec))
		return zram_bvec_write_partial(zram, bvec, index, offset, bio);
	return zram_write_page(zram, bvec->bv_page, index);
}

#ifdef CONFIG_ZRAM_MULTI_COMP
#define RECOMPRESS_IDLE		(1 << 0)
#define RECOMPRESS_HUGE		(1 << 1)

static int scan_slots_for_recompress(struct zram *zram, u32 mode, u32 prio_max,
				     struct zram_pp_ctl *ctl)
{
	unsigned long nr_pages = zram->disksize >> PAGE_SHIFT;
	unsigned long index;

	for (index = 0; index < nr_pages; index++) {
		bool ok = true;

		slot_lock(zram, index);
		if (!slot_allocated(zram, index))
			goto next;

		if (mode & RECOMPRESS_IDLE &&
		    !test_slot_flag(zram, index, ZRAM_IDLE))
			goto next;

		if (mode & RECOMPRESS_HUGE &&
		    !test_slot_flag(zram, index, ZRAM_HUGE))
			goto next;

		if (test_slot_flag(zram, index, ZRAM_WB) ||
		    test_slot_flag(zram, index, ZRAM_SAME) ||
		    test_slot_flag(zram, index, ZRAM_INCOMPRESSIBLE))
			goto next;

		/* Already compressed with same of higher priority */
		if (get_slot_comp_priority(zram, index) + 1 >= prio_max)
			goto next;

		ok = place_pp_slot(zram, ctl, index);
next:
		slot_unlock(zram, index);
		if (!ok)
			break;
	}

	return 0;
}

/*
 * This function will decompress (unless it's ZRAM_HUGE) the page and then
 * attempt to compress it using provided compression algorithm priority
 * (which is potentially more effective).
 *
 * Corresponding ZRAM slot should be locked.
 */
static int recompress_slot(struct zram *zram, u32 index, struct page *page,
			   u64 *num_recomp_pages, u32 threshold, u32 prio,
			   u32 prio_max)
{
	struct zcomp_strm *zstrm = NULL;
	unsigned long handle_old;
	unsigned long handle_new;
	unsigned int comp_len_old;
	unsigned int comp_len_new;
	unsigned int class_index_old;
	unsigned int class_index_new;
	void *src;
	int ret = 0;

	handle_old = get_slot_handle(zram, index);
	if (!handle_old)
		return -EINVAL;

	comp_len_old = get_slot_size(zram, index);
	/*
	 * Do not recompress objects that are already "small enough".
	 */
	if (comp_len_old < threshold)
		return 0;

	ret = read_from_zspool(zram, page, index);
	if (ret)
		return ret;

	/*
	 * We touched this entry so mark it as non-IDLE. This makes sure that
	 * we don't preserve IDLE flag and don't incorrectly pick this entry
	 * for different post-processing type (e.g. writeback).
	 */
	clear_slot_flag(zram, index, ZRAM_IDLE);

	class_index_old = zs_lookup_class_index(zram->mem_pool, comp_len_old);

	prio = max(prio, get_slot_comp_priority(zram, index) + 1);
	/*
	 * Recompression slots scan should not select slots that are
	 * already compressed with a higher priority algorithm, but
	 * just in case
	 */
	if (prio >= prio_max)
		return 0;

	/*
	 * Iterate the secondary comp algorithms list (in order of priority)
	 * and try to recompress the page.
	 */
	for (; prio < prio_max; prio++) {
		if (!zram->comps[prio])
			continue;

		zstrm = zcomp_stream_get(zram->comps[prio]);
		src = kmap_local_page(page);
		ret = zcomp_compress(zram->comps[prio], zstrm,
				     src, &comp_len_new);
		kunmap_local(src);

		if (ret) {
			zcomp_stream_put(zstrm);
			zstrm = NULL;
			break;
		}

		class_index_new = zs_lookup_class_index(zram->mem_pool,
							comp_len_new);

		/* Continue until we make progress */
		if (class_index_new >= class_index_old ||
		    (threshold && comp_len_new >= threshold)) {
			zcomp_stream_put(zstrm);
			zstrm = NULL;
			continue;
		}

		/* Recompression was successful so break out */
		break;
	}

	/*
	 * Decrement the limit (if set) on pages we can recompress, even
	 * when current recompression was unsuccessful or did not compress
	 * the page below the threshold, because we still spent resources
	 * on it.
	 */
	if (*num_recomp_pages)
		*num_recomp_pages -= 1;

	/* Compression error */
	if (ret)
		return ret;

	if (!zstrm) {
		/*
		 * Secondary algorithms failed to re-compress the page
		 * in a way that would save memory.
		 *
		 * Mark the object incompressible if the max-priority
		 * algorithm couldn't re-compress it.
		 */
		if (prio < zram->num_active_comps)
			return 0;
		set_slot_flag(zram, index, ZRAM_INCOMPRESSIBLE);
		return 0;
	}

	/*
	 * We are holding per-CPU stream mutex and entry lock so better
	 * avoid direct reclaim.  Allocation error is not fatal since
	 * we still have the old object in the mem_pool.
	 *
	 * XXX: technically, the node we really want here is the node that
	 * holds the original compressed data. But that would require us to
	 * modify zsmalloc API to return this information. For now, we will
	 * make do with the node of the page allocated for recompression.
	 */
	handle_new = zs_malloc(zram->mem_pool, comp_len_new,
			       GFP_NOIO | __GFP_NOWARN |
			       __GFP_HIGHMEM | __GFP_MOVABLE,
			       page_to_nid(page));
	if (IS_ERR_VALUE(handle_new)) {
		zcomp_stream_put(zstrm);
		return PTR_ERR((void *)handle_new);
	}

	zs_obj_write(zram->mem_pool, handle_new, zstrm->buffer, comp_len_new);
	zcomp_stream_put(zstrm);

	slot_free(zram, index);
	set_slot_handle(zram, index, handle_new);
	set_slot_size(zram, index, comp_len_new);
	set_slot_comp_priority(zram, index, prio);

	atomic64_add(comp_len_new, &zram->stats.compr_data_size);
	atomic64_inc(&zram->stats.pages_stored);

	return 0;
}

static ssize_t recompress_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t len)
{
	struct zram *zram = dev_to_zram(dev);
	char *args, *param, *val, *algo = NULL;
	u64 num_recomp_pages = ULLONG_MAX;
	struct zram_pp_ctl *ctl = NULL;
	struct zram_pp_slot *pps;
	u32 mode = 0, threshold = 0;
	u32 prio, prio_max;
	struct page *page = NULL;
	ssize_t ret;

	prio = ZRAM_SECONDARY_COMP;
	prio_max = zram->num_active_comps;

	args = skip_spaces(buf);
	while (*args) {
		args = next_arg(args, &param, &val);

		if (!val || !*val)
			return -EINVAL;

		if (!strcmp(param, "type")) {
			if (!strcmp(val, "idle"))
				mode = RECOMPRESS_IDLE;
			if (!strcmp(val, "huge"))
				mode = RECOMPRESS_HUGE;
			if (!strcmp(val, "huge_idle"))
				mode = RECOMPRESS_IDLE | RECOMPRESS_HUGE;
			continue;
		}

		if (!strcmp(param, "max_pages")) {
			/*
			 * Limit the number of entries (pages) we attempt to
			 * recompress.
			 */
			ret = kstrtoull(val, 10, &num_recomp_pages);
			if (ret)
				return ret;
			continue;
		}

		if (!strcmp(param, "threshold")) {
			/*
			 * We will re-compress only idle objects equal or
			 * greater in size than watermark.
			 */
			ret = kstrtouint(val, 10, &threshold);
			if (ret)
				return ret;
			continue;
		}

		if (!strcmp(param, "algo")) {
			algo = val;
			continue;
		}

		if (!strcmp(param, "priority")) {
			ret = kstrtouint(val, 10, &prio);
			if (ret)
				return ret;

			if (prio == ZRAM_PRIMARY_COMP)
				prio = ZRAM_SECONDARY_COMP;

			prio_max = prio + 1;
			continue;
		}
	}

	if (threshold >= huge_class_size)
		return -EINVAL;

	guard(rwsem_write)(&zram->dev_lock);
	if (!init_done(zram))
		return -EINVAL;

	if (algo) {
		bool found = false;

		for (; prio < ZRAM_MAX_COMPS; prio++) {
			if (!zram->comp_algs[prio])
				continue;

			if (!strcmp(zram->comp_algs[prio], algo)) {
				prio_max = prio + 1;
				found = true;
				break;
			}
		}

		if (!found) {
			ret = -EINVAL;
			goto out;
		}
	}

	prio_max = min(prio_max, (u32)zram->num_active_comps);
	if (prio >= prio_max) {
		ret = -EINVAL;
		goto out;
	}

	page = alloc_page(GFP_KERNEL);
	if (!page) {
		ret = -ENOMEM;
		goto out;
	}

	ctl = init_pp_ctl();
	if (!ctl) {
		ret = -ENOMEM;
		goto out;
	}

	scan_slots_for_recompress(zram, mode, prio_max, ctl);

	ret = len;
	while ((pps = select_pp_slot(ctl))) {
		int err = 0;

		if (!num_recomp_pages)
			break;

		slot_lock(zram, pps->index);
		if (!test_slot_flag(zram, pps->index, ZRAM_PP_SLOT))
			goto next;

		err = recompress_slot(zram, pps->index, page,
				      &num_recomp_pages, threshold,
				      prio, prio_max);
next:
		slot_unlock(zram, pps->index);
		release_pp_slot(zram, pps);

		if (err) {
			ret = err;
			break;
		}

		cond_resched();
	}

out:
	if (page)
		__free_page(page);
	release_pp_ctl(zram, ctl);
	return ret;
}
#endif

static void zram_bio_discard(struct zram *zram, struct bio *bio)
{
	size_t n = bio->bi_iter.bi_size;
	u32 index = bio->bi_iter.bi_sector >> SECTORS_PER_PAGE_SHIFT;
	u32 offset = (bio->bi_iter.bi_sector & (SECTORS_PER_PAGE - 1)) <<
			SECTOR_SHIFT;

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
		slot_lock(zram, index);
		slot_free(zram, index);
		slot_unlock(zram, index);
		atomic64_inc(&zram->stats.notify_free);
		index++;
		n -= PAGE_SIZE;
	}

	bio_endio(bio);
}

static void zram_bio_read(struct zram *zram, struct bio *bio)
{
	unsigned long start_time = bio_start_io_acct(bio);
	struct bvec_iter iter = bio->bi_iter;

	do {
		u32 index = iter.bi_sector >> SECTORS_PER_PAGE_SHIFT;
		u32 offset = (iter.bi_sector & (SECTORS_PER_PAGE - 1)) <<
				SECTOR_SHIFT;
		struct bio_vec bv = bio_iter_iovec(bio, iter);

		bv.bv_len = min_t(u32, bv.bv_len, PAGE_SIZE - offset);

		if (zram_bvec_read(zram, &bv, index, offset, bio) < 0) {
			atomic64_inc(&zram->stats.failed_reads);
			bio->bi_status = BLK_STS_IOERR;
			break;
		}
		flush_dcache_page(bv.bv_page);

		slot_lock(zram, index);
		mark_slot_accessed(zram, index);
		slot_unlock(zram, index);

		bio_advance_iter_single(bio, &iter, bv.bv_len);
	} while (iter.bi_size);

	bio_end_io_acct(bio, start_time);
	bio_endio(bio);
}

static void zram_bio_write(struct zram *zram, struct bio *bio)
{
	unsigned long start_time = bio_start_io_acct(bio);
	struct bvec_iter iter = bio->bi_iter;

	do {
		u32 index = iter.bi_sector >> SECTORS_PER_PAGE_SHIFT;
		u32 offset = (iter.bi_sector & (SECTORS_PER_PAGE - 1)) <<
				SECTOR_SHIFT;
		struct bio_vec bv = bio_iter_iovec(bio, iter);

		bv.bv_len = min_t(u32, bv.bv_len, PAGE_SIZE - offset);

		if (zram_bvec_write(zram, &bv, index, offset, bio) < 0) {
			atomic64_inc(&zram->stats.failed_writes);
			bio->bi_status = BLK_STS_IOERR;
			break;
		}

		slot_lock(zram, index);
		mark_slot_accessed(zram, index);
		slot_unlock(zram, index);

		bio_advance_iter_single(bio, &iter, bv.bv_len);
	} while (iter.bi_size);

	bio_end_io_acct(bio, start_time);
	bio_endio(bio);
}

/*
 * Handler function for all zram I/O requests.
 */
static void zram_submit_bio(struct bio *bio)
{
	struct zram *zram = bio->bi_bdev->bd_disk->private_data;

	switch (bio_op(bio)) {
	case REQ_OP_READ:
		zram_bio_read(zram, bio);
		break;
	case REQ_OP_WRITE:
		zram_bio_write(zram, bio);
		break;
	case REQ_OP_DISCARD:
	case REQ_OP_WRITE_ZEROES:
		zram_bio_discard(zram, bio);
		break;
	default:
		WARN_ON_ONCE(1);
		bio_endio(bio);
	}
}

static void zram_slot_free_notify(struct block_device *bdev,
				unsigned long index)
{
	struct zram *zram;

	zram = bdev->bd_disk->private_data;

	atomic64_inc(&zram->stats.notify_free);
	if (!slot_trylock(zram, index)) {
		atomic64_inc(&zram->stats.miss_free);
		return;
	}

	slot_free(zram, index);
	slot_unlock(zram, index);
}

static void zram_comp_params_reset(struct zram *zram)
{
	u32 prio;

	for (prio = ZRAM_PRIMARY_COMP; prio < ZRAM_MAX_COMPS; prio++) {
		comp_params_reset(zram, prio);
	}
}

static void zram_destroy_comps(struct zram *zram)
{
	u32 prio;

	for (prio = ZRAM_PRIMARY_COMP; prio < ZRAM_MAX_COMPS; prio++) {
		struct zcomp *comp = zram->comps[prio];

		zram->comps[prio] = NULL;
		if (!comp)
			continue;
		zcomp_destroy(comp);
		zram->num_active_comps--;
	}

	for (prio = ZRAM_PRIMARY_COMP; prio < ZRAM_MAX_COMPS; prio++) {
		/* Do not free statically defined compression algorithms */
		if (zram->comp_algs[prio] != default_compressor)
			kfree(zram->comp_algs[prio]);
		zram->comp_algs[prio] = NULL;
	}

	zram_comp_params_reset(zram);
}

static void zram_reset_device(struct zram *zram)
{
	guard(rwsem_write)(&zram->dev_lock);

	zram->limit_pages = 0;

	set_capacity_and_notify(zram->disk, 0);
	part_stat_set_all(zram->disk->part0, 0);

	/* I/O operation under all of CPU are done so let's free */
	zram_meta_free(zram, zram->disksize);
	zram->disksize = 0;
	zram_destroy_comps(zram);
	memset(&zram->stats, 0, sizeof(zram->stats));
	reset_bdev(zram);

	comp_algorithm_set(zram, ZRAM_PRIMARY_COMP, default_compressor);
}

static ssize_t disksize_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t len)
{
	u64 disksize;
	struct zcomp *comp;
	struct zram *zram = dev_to_zram(dev);
	int err;
	u32 prio;

	disksize = memparse(buf, NULL);
	if (!disksize)
		return -EINVAL;

	guard(rwsem_write)(&zram->dev_lock);
	if (init_done(zram)) {
		pr_info("Cannot change disksize for initialized device\n");
		return -EBUSY;
	}

	disksize = PAGE_ALIGN(disksize);
	if (!zram_meta_alloc(zram, disksize))
		return -ENOMEM;

	for (prio = ZRAM_PRIMARY_COMP; prio < ZRAM_MAX_COMPS; prio++) {
		if (!zram->comp_algs[prio])
			continue;

		comp = zcomp_create(zram->comp_algs[prio],
				    &zram->params[prio]);
		if (IS_ERR(comp)) {
			pr_err("Cannot initialise %s compressing backend\n",
			       zram->comp_algs[prio]);
			err = PTR_ERR(comp);
			goto out_free_comps;
		}

		zram->comps[prio] = comp;
		zram->num_active_comps++;
	}
	zram->disksize = disksize;
	set_capacity_and_notify(zram->disk, zram->disksize >> SECTOR_SHIFT);

	return len;

out_free_comps:
	zram_destroy_comps(zram);
	zram_meta_free(zram, disksize);
	return err;
}

static ssize_t reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret;
	unsigned short do_reset;
	struct zram *zram;
	struct gendisk *disk;

	ret = kstrtou16(buf, 10, &do_reset);
	if (ret)
		return ret;

	if (!do_reset)
		return -EINVAL;

	zram = dev_to_zram(dev);
	disk = zram->disk;

	mutex_lock(&disk->open_mutex);
	/* Do not reset an active device or claimed device */
	if (disk_openers(disk) || zram->claim) {
		mutex_unlock(&disk->open_mutex);
		return -EBUSY;
	}

	/* From now on, anyone can't open /dev/zram[0-9] */
	zram->claim = true;
	mutex_unlock(&disk->open_mutex);

	/* Make sure all the pending I/O are finished */
	sync_blockdev(disk->part0);
	zram_reset_device(zram);

	mutex_lock(&disk->open_mutex);
	zram->claim = false;
	mutex_unlock(&disk->open_mutex);

	return len;
}

static int zram_open(struct gendisk *disk, blk_mode_t mode)
{
	struct zram *zram = disk->private_data;

	WARN_ON(!mutex_is_locked(&disk->open_mutex));

	/* zram was claimed to reset so open request fails */
	if (zram->claim)
		return -EBUSY;
	return 0;
}

static const struct block_device_operations zram_devops = {
	.open = zram_open,
	.submit_bio = zram_submit_bio,
	.swap_slot_free_notify = zram_slot_free_notify,
	.owner = THIS_MODULE
};

static DEVICE_ATTR_RO(io_stat);
static DEVICE_ATTR_RO(mm_stat);
static DEVICE_ATTR_RO(debug_stat);
static DEVICE_ATTR_WO(compact);
static DEVICE_ATTR_RW(disksize);
static DEVICE_ATTR_RO(initstate);
static DEVICE_ATTR_WO(reset);
static DEVICE_ATTR_WO(mem_limit);
static DEVICE_ATTR_WO(mem_used_max);
static DEVICE_ATTR_WO(idle);
static DEVICE_ATTR_RW(comp_algorithm);
#ifdef CONFIG_ZRAM_WRITEBACK
static DEVICE_ATTR_RO(bd_stat);
static DEVICE_ATTR_RW(backing_dev);
static DEVICE_ATTR_WO(writeback);
static DEVICE_ATTR_RW(writeback_limit);
static DEVICE_ATTR_RW(writeback_limit_enable);
static DEVICE_ATTR_RW(writeback_batch_size);
static DEVICE_ATTR_RW(writeback_compressed);
#endif
#ifdef CONFIG_ZRAM_MULTI_COMP
static DEVICE_ATTR_RW(recomp_algorithm);
static DEVICE_ATTR_WO(recompress);
#endif
static DEVICE_ATTR_WO(algorithm_params);

static struct attribute *zram_disk_attrs[] = {
	&dev_attr_disksize.attr,
	&dev_attr_initstate.attr,
	&dev_attr_reset.attr,
	&dev_attr_compact.attr,
	&dev_attr_mem_limit.attr,
	&dev_attr_mem_used_max.attr,
	&dev_attr_idle.attr,
	&dev_attr_comp_algorithm.attr,
#ifdef CONFIG_ZRAM_WRITEBACK
	&dev_attr_bd_stat.attr,
	&dev_attr_backing_dev.attr,
	&dev_attr_writeback.attr,
	&dev_attr_writeback_limit.attr,
	&dev_attr_writeback_limit_enable.attr,
	&dev_attr_writeback_batch_size.attr,
	&dev_attr_writeback_compressed.attr,
#endif
	&dev_attr_io_stat.attr,
	&dev_attr_mm_stat.attr,
	&dev_attr_debug_stat.attr,
#ifdef CONFIG_ZRAM_MULTI_COMP
	&dev_attr_recomp_algorithm.attr,
	&dev_attr_recompress.attr,
#endif
	&dev_attr_algorithm_params.attr,
	NULL,
};

ATTRIBUTE_GROUPS(zram_disk);

/*
 * Allocate and initialize new zram device. the function returns
 * '>= 0' device_id upon success, and negative value otherwise.
 */
static int zram_add(void)
{
	struct queue_limits lim = {
		.logical_block_size		= ZRAM_LOGICAL_BLOCK_SIZE,
		/*
		 * To ensure that we always get PAGE_SIZE aligned and
		 * n*PAGE_SIZED sized I/O requests.
		 */
		.physical_block_size		= PAGE_SIZE,
		.io_min				= PAGE_SIZE,
		.io_opt				= PAGE_SIZE,
		.max_hw_discard_sectors		= UINT_MAX,
		/*
		 * zram_bio_discard() will clear all logical blocks if logical
		 * block size is identical with physical block size(PAGE_SIZE).
		 * But if it is different, we will skip discarding some parts of
		 * logical blocks in the part of the request range which isn't
		 * aligned to physical block size.  So we can't ensure that all
		 * discarded logical blocks are zeroed.
		 */
#if ZRAM_LOGICAL_BLOCK_SIZE == PAGE_SIZE
		.max_write_zeroes_sectors	= UINT_MAX,
#endif
		.features			= BLK_FEAT_STABLE_WRITES |
						  BLK_FEAT_SYNCHRONOUS,
	};
	struct zram *zram;
	int ret, device_id;

	zram = kzalloc(sizeof(struct zram), GFP_KERNEL);
	if (!zram)
		return -ENOMEM;

	ret = idr_alloc(&zram_index_idr, zram, 0, 0, GFP_KERNEL);
	if (ret < 0)
		goto out_free_dev;
	device_id = ret;

	init_rwsem(&zram->dev_lock);
#ifdef CONFIG_ZRAM_WRITEBACK
	zram->wb_batch_size = 32;
	zram->wb_compressed = false;
#endif

	/* gendisk structure */
	zram->disk = blk_alloc_disk(&lim, NUMA_NO_NODE);
	if (IS_ERR(zram->disk)) {
		pr_err("Error allocating disk structure for device %d\n",
			device_id);
		ret = PTR_ERR(zram->disk);
		goto out_free_idr;
	}

	zram->disk->major = zram_major;
	zram->disk->first_minor = device_id;
	zram->disk->minors = 1;
	zram->disk->flags |= GENHD_FL_NO_PART;
	zram->disk->fops = &zram_devops;
	zram->disk->private_data = zram;
	snprintf(zram->disk->disk_name, 16, "zram%d", device_id);
	zram_comp_params_reset(zram);
	comp_algorithm_set(zram, ZRAM_PRIMARY_COMP, default_compressor);

	/* Actual capacity set using sysfs (/sys/block/zram<id>/disksize */
	set_capacity(zram->disk, 0);
	ret = device_add_disk(NULL, zram->disk, zram_disk_groups);
	if (ret)
		goto out_cleanup_disk;

	zram_debugfs_register(zram);
	pr_info("Added device: %s\n", zram->disk->disk_name);
	return device_id;

out_cleanup_disk:
	put_disk(zram->disk);
out_free_idr:
	idr_remove(&zram_index_idr, device_id);
out_free_dev:
	kfree(zram);
	return ret;
}

static int zram_remove(struct zram *zram)
{
	bool claimed;

	mutex_lock(&zram->disk->open_mutex);
	if (disk_openers(zram->disk)) {
		mutex_unlock(&zram->disk->open_mutex);
		return -EBUSY;
	}

	claimed = zram->claim;
	if (!claimed)
		zram->claim = true;
	mutex_unlock(&zram->disk->open_mutex);

	zram_debugfs_unregister(zram);

	if (claimed) {
		/*
		 * If we were claimed by reset_store(), del_gendisk() will
		 * wait until reset_store() is done, so nothing need to do.
		 */
		;
	} else {
		/* Make sure all the pending I/O are finished */
		sync_blockdev(zram->disk->part0);
		zram_reset_device(zram);
	}

	pr_info("Removed device: %s\n", zram->disk->disk_name);

	del_gendisk(zram->disk);

	/* del_gendisk drains pending reset_store */
	WARN_ON_ONCE(claimed && zram->claim);

	/*
	 * disksize_store() may be called in between zram_reset_device()
	 * and del_gendisk(), so run the last reset to avoid leaking
	 * anything allocated with disksize_store()
	 */
	zram_reset_device(zram);

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
static ssize_t hot_add_show(const struct class *class,
			const struct class_attribute *attr,
			char *buf)
{
	int ret;

	mutex_lock(&zram_index_mutex);
	ret = zram_add();
	mutex_unlock(&zram_index_mutex);

	if (ret < 0)
		return ret;
	return sysfs_emit(buf, "%d\n", ret);
}
/* This attribute must be set to 0400, so CLASS_ATTR_RO() can not be used */
static struct class_attribute class_attr_hot_add =
	__ATTR(hot_add, 0400, hot_add_show, NULL);

static ssize_t hot_remove_store(const struct class *class,
			const struct class_attribute *attr,
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
	.class_groups	= zram_control_class_groups,
};

static int zram_remove_cb(int id, void *ptr, void *data)
{
	WARN_ON_ONCE(zram_remove(ptr));
	return 0;
}

static void destroy_devices(void)
{
	class_unregister(&zram_control_class);
	idr_for_each(&zram_index_idr, &zram_remove_cb, NULL);
	zram_debugfs_destroy();
	idr_destroy(&zram_index_idr);
	unregister_blkdev(zram_major, "zram");
	cpuhp_remove_multi_state(CPUHP_ZCOMP_PREPARE);
}

static int __init zram_init(void)
{
	struct zram_table_entry zram_te;
	int ret;

	BUILD_BUG_ON(__NR_ZRAM_PAGEFLAGS > sizeof(zram_te.attr.flags) * 8);

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

	zram_debugfs_create();
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
