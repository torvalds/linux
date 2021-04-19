// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Red Hat. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include <linux/device-mapper.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/kthread.h>
#include <linux/dm-io.h>
#include <linux/dm-kcopyd.h>
#include <linux/dax.h>
#include <linux/pfn_t.h>
#include <linux/libnvdimm.h>

#define DM_MSG_PREFIX "writecache"

#define HIGH_WATERMARK			50
#define LOW_WATERMARK			45
#define MAX_WRITEBACK_JOBS		0
#define ENDIO_LATENCY			16
#define WRITEBACK_LATENCY		64
#define AUTOCOMMIT_BLOCKS_SSD		65536
#define AUTOCOMMIT_BLOCKS_PMEM		64
#define AUTOCOMMIT_MSEC			1000
#define MAX_AGE_DIV			16
#define MAX_AGE_UNSPECIFIED		-1UL

#define BITMAP_GRANULARITY	65536
#if BITMAP_GRANULARITY < PAGE_SIZE
#undef BITMAP_GRANULARITY
#define BITMAP_GRANULARITY	PAGE_SIZE
#endif

#if IS_ENABLED(CONFIG_ARCH_HAS_PMEM_API) && IS_ENABLED(CONFIG_DAX_DRIVER)
#define DM_WRITECACHE_HAS_PMEM
#endif

#ifdef DM_WRITECACHE_HAS_PMEM
#define pmem_assign(dest, src)					\
do {								\
	typeof(dest) uniq = (src);				\
	memcpy_flushcache(&(dest), &uniq, sizeof(dest));	\
} while (0)
#else
#define pmem_assign(dest, src)	((dest) = (src))
#endif

#if IS_ENABLED(CONFIG_ARCH_HAS_COPY_MC) && defined(DM_WRITECACHE_HAS_PMEM)
#define DM_WRITECACHE_HANDLE_HARDWARE_ERRORS
#endif

#define MEMORY_SUPERBLOCK_MAGIC		0x23489321
#define MEMORY_SUPERBLOCK_VERSION	1

struct wc_memory_entry {
	__le64 original_sector;
	__le64 seq_count;
};

struct wc_memory_superblock {
	union {
		struct {
			__le32 magic;
			__le32 version;
			__le32 block_size;
			__le32 pad;
			__le64 n_blocks;
			__le64 seq_count;
		};
		__le64 padding[8];
	};
	struct wc_memory_entry entries[0];
};

struct wc_entry {
	struct rb_node rb_node;
	struct list_head lru;
	unsigned short wc_list_contiguous;
	bool write_in_progress
#if BITS_PER_LONG == 64
		:1
#endif
	;
	unsigned long index
#if BITS_PER_LONG == 64
		:47
#endif
	;
	unsigned long age;
#ifdef DM_WRITECACHE_HANDLE_HARDWARE_ERRORS
	uint64_t original_sector;
	uint64_t seq_count;
#endif
};

#ifdef DM_WRITECACHE_HAS_PMEM
#define WC_MODE_PMEM(wc)			((wc)->pmem_mode)
#define WC_MODE_FUA(wc)				((wc)->writeback_fua)
#else
#define WC_MODE_PMEM(wc)			false
#define WC_MODE_FUA(wc)				false
#endif
#define WC_MODE_SORT_FREELIST(wc)		(!WC_MODE_PMEM(wc))

struct dm_writecache {
	struct mutex lock;
	struct list_head lru;
	union {
		struct list_head freelist;
		struct {
			struct rb_root freetree;
			struct wc_entry *current_free;
		};
	};
	struct rb_root tree;

	size_t freelist_size;
	size_t writeback_size;
	size_t freelist_high_watermark;
	size_t freelist_low_watermark;
	unsigned long max_age;

	unsigned uncommitted_blocks;
	unsigned autocommit_blocks;
	unsigned max_writeback_jobs;

	int error;

	unsigned long autocommit_jiffies;
	struct timer_list autocommit_timer;
	struct wait_queue_head freelist_wait;

	struct timer_list max_age_timer;

	atomic_t bio_in_progress[2];
	struct wait_queue_head bio_in_progress_wait[2];

	struct dm_target *ti;
	struct dm_dev *dev;
	struct dm_dev *ssd_dev;
	sector_t start_sector;
	void *memory_map;
	uint64_t memory_map_size;
	size_t metadata_sectors;
	size_t n_blocks;
	uint64_t seq_count;
	sector_t data_device_sectors;
	void *block_start;
	struct wc_entry *entries;
	unsigned block_size;
	unsigned char block_size_bits;

	bool pmem_mode:1;
	bool writeback_fua:1;

	bool overwrote_committed:1;
	bool memory_vmapped:1;

	bool start_sector_set:1;
	bool high_wm_percent_set:1;
	bool low_wm_percent_set:1;
	bool max_writeback_jobs_set:1;
	bool autocommit_blocks_set:1;
	bool autocommit_time_set:1;
	bool max_age_set:1;
	bool writeback_fua_set:1;
	bool flush_on_suspend:1;
	bool cleaner:1;
	bool cleaner_set:1;

	unsigned high_wm_percent_value;
	unsigned low_wm_percent_value;
	unsigned autocommit_time_value;
	unsigned max_age_value;

	unsigned writeback_all;
	struct workqueue_struct *writeback_wq;
	struct work_struct writeback_work;
	struct work_struct flush_work;

	struct dm_io_client *dm_io;

	raw_spinlock_t endio_list_lock;
	struct list_head endio_list;
	struct task_struct *endio_thread;

	struct task_struct *flush_thread;
	struct bio_list flush_list;

	struct dm_kcopyd_client *dm_kcopyd;
	unsigned long *dirty_bitmap;
	unsigned dirty_bitmap_size;

	struct bio_set bio_set;
	mempool_t copy_pool;
};

#define WB_LIST_INLINE		16

struct writeback_struct {
	struct list_head endio_entry;
	struct dm_writecache *wc;
	struct wc_entry **wc_list;
	unsigned wc_list_n;
	struct wc_entry *wc_list_inline[WB_LIST_INLINE];
	struct bio bio;
};

struct copy_struct {
	struct list_head endio_entry;
	struct dm_writecache *wc;
	struct wc_entry *e;
	unsigned n_entries;
	int error;
};

DECLARE_DM_KCOPYD_THROTTLE_WITH_MODULE_PARM(dm_writecache_throttle,
					    "A percentage of time allocated for data copying");

static void wc_lock(struct dm_writecache *wc)
{
	mutex_lock(&wc->lock);
}

static void wc_unlock(struct dm_writecache *wc)
{
	mutex_unlock(&wc->lock);
}

#ifdef DM_WRITECACHE_HAS_PMEM
static int persistent_memory_claim(struct dm_writecache *wc)
{
	int r;
	loff_t s;
	long p, da;
	pfn_t pfn;
	int id;
	struct page **pages;
	sector_t offset;

	wc->memory_vmapped = false;

	s = wc->memory_map_size;
	p = s >> PAGE_SHIFT;
	if (!p) {
		r = -EINVAL;
		goto err1;
	}
	if (p != s >> PAGE_SHIFT) {
		r = -EOVERFLOW;
		goto err1;
	}

	offset = get_start_sect(wc->ssd_dev->bdev);
	if (offset & (PAGE_SIZE / 512 - 1)) {
		r = -EINVAL;
		goto err1;
	}
	offset >>= PAGE_SHIFT - 9;

	id = dax_read_lock();

	da = dax_direct_access(wc->ssd_dev->dax_dev, offset, p, &wc->memory_map, &pfn);
	if (da < 0) {
		wc->memory_map = NULL;
		r = da;
		goto err2;
	}
	if (!pfn_t_has_page(pfn)) {
		wc->memory_map = NULL;
		r = -EOPNOTSUPP;
		goto err2;
	}
	if (da != p) {
		long i;
		wc->memory_map = NULL;
		pages = kvmalloc_array(p, sizeof(struct page *), GFP_KERNEL);
		if (!pages) {
			r = -ENOMEM;
			goto err2;
		}
		i = 0;
		do {
			long daa;
			daa = dax_direct_access(wc->ssd_dev->dax_dev, offset + i, p - i,
						NULL, &pfn);
			if (daa <= 0) {
				r = daa ? daa : -EINVAL;
				goto err3;
			}
			if (!pfn_t_has_page(pfn)) {
				r = -EOPNOTSUPP;
				goto err3;
			}
			while (daa-- && i < p) {
				pages[i++] = pfn_t_to_page(pfn);
				pfn.val++;
				if (!(i & 15))
					cond_resched();
			}
		} while (i < p);
		wc->memory_map = vmap(pages, p, VM_MAP, PAGE_KERNEL);
		if (!wc->memory_map) {
			r = -ENOMEM;
			goto err3;
		}
		kvfree(pages);
		wc->memory_vmapped = true;
	}

	dax_read_unlock(id);

	wc->memory_map += (size_t)wc->start_sector << SECTOR_SHIFT;
	wc->memory_map_size -= (size_t)wc->start_sector << SECTOR_SHIFT;

	return 0;
err3:
	kvfree(pages);
err2:
	dax_read_unlock(id);
err1:
	return r;
}
#else
static int persistent_memory_claim(struct dm_writecache *wc)
{
	return -EOPNOTSUPP;
}
#endif

static void persistent_memory_release(struct dm_writecache *wc)
{
	if (wc->memory_vmapped)
		vunmap(wc->memory_map - ((size_t)wc->start_sector << SECTOR_SHIFT));
}

static struct page *persistent_memory_page(void *addr)
{
	if (is_vmalloc_addr(addr))
		return vmalloc_to_page(addr);
	else
		return virt_to_page(addr);
}

static unsigned persistent_memory_page_offset(void *addr)
{
	return (unsigned long)addr & (PAGE_SIZE - 1);
}

static void persistent_memory_flush_cache(void *ptr, size_t size)
{
	if (is_vmalloc_addr(ptr))
		flush_kernel_vmap_range(ptr, size);
}

static void persistent_memory_invalidate_cache(void *ptr, size_t size)
{
	if (is_vmalloc_addr(ptr))
		invalidate_kernel_vmap_range(ptr, size);
}

static struct wc_memory_superblock *sb(struct dm_writecache *wc)
{
	return wc->memory_map;
}

static struct wc_memory_entry *memory_entry(struct dm_writecache *wc, struct wc_entry *e)
{
	return &sb(wc)->entries[e->index];
}

static void *memory_data(struct dm_writecache *wc, struct wc_entry *e)
{
	return (char *)wc->block_start + (e->index << wc->block_size_bits);
}

static sector_t cache_sector(struct dm_writecache *wc, struct wc_entry *e)
{
	return wc->start_sector + wc->metadata_sectors +
		((sector_t)e->index << (wc->block_size_bits - SECTOR_SHIFT));
}

static uint64_t read_original_sector(struct dm_writecache *wc, struct wc_entry *e)
{
#ifdef DM_WRITECACHE_HANDLE_HARDWARE_ERRORS
	return e->original_sector;
#else
	return le64_to_cpu(memory_entry(wc, e)->original_sector);
#endif
}

static uint64_t read_seq_count(struct dm_writecache *wc, struct wc_entry *e)
{
#ifdef DM_WRITECACHE_HANDLE_HARDWARE_ERRORS
	return e->seq_count;
#else
	return le64_to_cpu(memory_entry(wc, e)->seq_count);
#endif
}

static void clear_seq_count(struct dm_writecache *wc, struct wc_entry *e)
{
#ifdef DM_WRITECACHE_HANDLE_HARDWARE_ERRORS
	e->seq_count = -1;
#endif
	pmem_assign(memory_entry(wc, e)->seq_count, cpu_to_le64(-1));
}

static void write_original_sector_seq_count(struct dm_writecache *wc, struct wc_entry *e,
					    uint64_t original_sector, uint64_t seq_count)
{
	struct wc_memory_entry me;
#ifdef DM_WRITECACHE_HANDLE_HARDWARE_ERRORS
	e->original_sector = original_sector;
	e->seq_count = seq_count;
#endif
	me.original_sector = cpu_to_le64(original_sector);
	me.seq_count = cpu_to_le64(seq_count);
	pmem_assign(*memory_entry(wc, e), me);
}

#define writecache_error(wc, err, msg, arg...)				\
do {									\
	if (!cmpxchg(&(wc)->error, 0, err))				\
		DMERR(msg, ##arg);					\
	wake_up(&(wc)->freelist_wait);					\
} while (0)

#define writecache_has_error(wc)	(unlikely(READ_ONCE((wc)->error)))

static void writecache_flush_all_metadata(struct dm_writecache *wc)
{
	if (!WC_MODE_PMEM(wc))
		memset(wc->dirty_bitmap, -1, wc->dirty_bitmap_size);
}

static void writecache_flush_region(struct dm_writecache *wc, void *ptr, size_t size)
{
	if (!WC_MODE_PMEM(wc))
		__set_bit(((char *)ptr - (char *)wc->memory_map) / BITMAP_GRANULARITY,
			  wc->dirty_bitmap);
}

static void writecache_disk_flush(struct dm_writecache *wc, struct dm_dev *dev);

struct io_notify {
	struct dm_writecache *wc;
	struct completion c;
	atomic_t count;
};

static void writecache_notify_io(unsigned long error, void *context)
{
	struct io_notify *endio = context;

	if (unlikely(error != 0))
		writecache_error(endio->wc, -EIO, "error writing metadata");
	BUG_ON(atomic_read(&endio->count) <= 0);
	if (atomic_dec_and_test(&endio->count))
		complete(&endio->c);
}

static void writecache_wait_for_ios(struct dm_writecache *wc, int direction)
{
	wait_event(wc->bio_in_progress_wait[direction],
		   !atomic_read(&wc->bio_in_progress[direction]));
}

static void ssd_commit_flushed(struct dm_writecache *wc, bool wait_for_ios)
{
	struct dm_io_region region;
	struct dm_io_request req;
	struct io_notify endio = {
		wc,
		COMPLETION_INITIALIZER_ONSTACK(endio.c),
		ATOMIC_INIT(1),
	};
	unsigned bitmap_bits = wc->dirty_bitmap_size * 8;
	unsigned i = 0;

	while (1) {
		unsigned j;
		i = find_next_bit(wc->dirty_bitmap, bitmap_bits, i);
		if (unlikely(i == bitmap_bits))
			break;
		j = find_next_zero_bit(wc->dirty_bitmap, bitmap_bits, i);

		region.bdev = wc->ssd_dev->bdev;
		region.sector = (sector_t)i * (BITMAP_GRANULARITY >> SECTOR_SHIFT);
		region.count = (sector_t)(j - i) * (BITMAP_GRANULARITY >> SECTOR_SHIFT);

		if (unlikely(region.sector >= wc->metadata_sectors))
			break;
		if (unlikely(region.sector + region.count > wc->metadata_sectors))
			region.count = wc->metadata_sectors - region.sector;

		region.sector += wc->start_sector;
		atomic_inc(&endio.count);
		req.bi_op = REQ_OP_WRITE;
		req.bi_op_flags = REQ_SYNC;
		req.mem.type = DM_IO_VMA;
		req.mem.ptr.vma = (char *)wc->memory_map + (size_t)i * BITMAP_GRANULARITY;
		req.client = wc->dm_io;
		req.notify.fn = writecache_notify_io;
		req.notify.context = &endio;

		/* writing via async dm-io (implied by notify.fn above) won't return an error */
	        (void) dm_io(&req, 1, &region, NULL);
		i = j;
	}

	writecache_notify_io(0, &endio);
	wait_for_completion_io(&endio.c);

	if (wait_for_ios)
		writecache_wait_for_ios(wc, WRITE);

	writecache_disk_flush(wc, wc->ssd_dev);

	memset(wc->dirty_bitmap, 0, wc->dirty_bitmap_size);
}

static void ssd_commit_superblock(struct dm_writecache *wc)
{
	int r;
	struct dm_io_region region;
	struct dm_io_request req;

	region.bdev = wc->ssd_dev->bdev;
	region.sector = 0;
	region.count = PAGE_SIZE >> SECTOR_SHIFT;

	if (unlikely(region.sector + region.count > wc->metadata_sectors))
		region.count = wc->metadata_sectors - region.sector;

	region.sector += wc->start_sector;

	req.bi_op = REQ_OP_WRITE;
	req.bi_op_flags = REQ_SYNC | REQ_FUA;
	req.mem.type = DM_IO_VMA;
	req.mem.ptr.vma = (char *)wc->memory_map;
	req.client = wc->dm_io;
	req.notify.fn = NULL;
	req.notify.context = NULL;

	r = dm_io(&req, 1, &region, NULL);
	if (unlikely(r))
		writecache_error(wc, r, "error writing superblock");
}

static void writecache_commit_flushed(struct dm_writecache *wc, bool wait_for_ios)
{
	if (WC_MODE_PMEM(wc))
		pmem_wmb();
	else
		ssd_commit_flushed(wc, wait_for_ios);
}

static void writecache_disk_flush(struct dm_writecache *wc, struct dm_dev *dev)
{
	int r;
	struct dm_io_region region;
	struct dm_io_request req;

	region.bdev = dev->bdev;
	region.sector = 0;
	region.count = 0;
	req.bi_op = REQ_OP_WRITE;
	req.bi_op_flags = REQ_PREFLUSH;
	req.mem.type = DM_IO_KMEM;
	req.mem.ptr.addr = NULL;
	req.client = wc->dm_io;
	req.notify.fn = NULL;

	r = dm_io(&req, 1, &region, NULL);
	if (unlikely(r))
		writecache_error(wc, r, "error flushing metadata: %d", r);
}

#define WFE_RETURN_FOLLOWING	1
#define WFE_LOWEST_SEQ		2

static struct wc_entry *writecache_find_entry(struct dm_writecache *wc,
					      uint64_t block, int flags)
{
	struct wc_entry *e;
	struct rb_node *node = wc->tree.rb_node;

	if (unlikely(!node))
		return NULL;

	while (1) {
		e = container_of(node, struct wc_entry, rb_node);
		if (read_original_sector(wc, e) == block)
			break;

		node = (read_original_sector(wc, e) >= block ?
			e->rb_node.rb_left : e->rb_node.rb_right);
		if (unlikely(!node)) {
			if (!(flags & WFE_RETURN_FOLLOWING))
				return NULL;
			if (read_original_sector(wc, e) >= block) {
				return e;
			} else {
				node = rb_next(&e->rb_node);
				if (unlikely(!node))
					return NULL;
				e = container_of(node, struct wc_entry, rb_node);
				return e;
			}
		}
	}

	while (1) {
		struct wc_entry *e2;
		if (flags & WFE_LOWEST_SEQ)
			node = rb_prev(&e->rb_node);
		else
			node = rb_next(&e->rb_node);
		if (unlikely(!node))
			return e;
		e2 = container_of(node, struct wc_entry, rb_node);
		if (read_original_sector(wc, e2) != block)
			return e;
		e = e2;
	}
}

static void writecache_insert_entry(struct dm_writecache *wc, struct wc_entry *ins)
{
	struct wc_entry *e;
	struct rb_node **node = &wc->tree.rb_node, *parent = NULL;

	while (*node) {
		e = container_of(*node, struct wc_entry, rb_node);
		parent = &e->rb_node;
		if (read_original_sector(wc, e) > read_original_sector(wc, ins))
			node = &parent->rb_left;
		else
			node = &parent->rb_right;
	}
	rb_link_node(&ins->rb_node, parent, node);
	rb_insert_color(&ins->rb_node, &wc->tree);
	list_add(&ins->lru, &wc->lru);
	ins->age = jiffies;
}

static void writecache_unlink(struct dm_writecache *wc, struct wc_entry *e)
{
	list_del(&e->lru);
	rb_erase(&e->rb_node, &wc->tree);
}

static void writecache_add_to_freelist(struct dm_writecache *wc, struct wc_entry *e)
{
	if (WC_MODE_SORT_FREELIST(wc)) {
		struct rb_node **node = &wc->freetree.rb_node, *parent = NULL;
		if (unlikely(!*node))
			wc->current_free = e;
		while (*node) {
			parent = *node;
			if (&e->rb_node < *node)
				node = &parent->rb_left;
			else
				node = &parent->rb_right;
		}
		rb_link_node(&e->rb_node, parent, node);
		rb_insert_color(&e->rb_node, &wc->freetree);
	} else {
		list_add_tail(&e->lru, &wc->freelist);
	}
	wc->freelist_size++;
}

static inline void writecache_verify_watermark(struct dm_writecache *wc)
{
	if (unlikely(wc->freelist_size + wc->writeback_size <= wc->freelist_high_watermark))
		queue_work(wc->writeback_wq, &wc->writeback_work);
}

static void writecache_max_age_timer(struct timer_list *t)
{
	struct dm_writecache *wc = from_timer(wc, t, max_age_timer);

	if (!dm_suspended(wc->ti) && !writecache_has_error(wc)) {
		queue_work(wc->writeback_wq, &wc->writeback_work);
		mod_timer(&wc->max_age_timer, jiffies + wc->max_age / MAX_AGE_DIV);
	}
}

static struct wc_entry *writecache_pop_from_freelist(struct dm_writecache *wc, sector_t expected_sector)
{
	struct wc_entry *e;

	if (WC_MODE_SORT_FREELIST(wc)) {
		struct rb_node *next;
		if (unlikely(!wc->current_free))
			return NULL;
		e = wc->current_free;
		if (expected_sector != (sector_t)-1 && unlikely(cache_sector(wc, e) != expected_sector))
			return NULL;
		next = rb_next(&e->rb_node);
		rb_erase(&e->rb_node, &wc->freetree);
		if (unlikely(!next))
			next = rb_first(&wc->freetree);
		wc->current_free = next ? container_of(next, struct wc_entry, rb_node) : NULL;
	} else {
		if (unlikely(list_empty(&wc->freelist)))
			return NULL;
		e = container_of(wc->freelist.next, struct wc_entry, lru);
		if (expected_sector != (sector_t)-1 && unlikely(cache_sector(wc, e) != expected_sector))
			return NULL;
		list_del(&e->lru);
	}
	wc->freelist_size--;

	writecache_verify_watermark(wc);

	return e;
}

static void writecache_free_entry(struct dm_writecache *wc, struct wc_entry *e)
{
	writecache_unlink(wc, e);
	writecache_add_to_freelist(wc, e);
	clear_seq_count(wc, e);
	writecache_flush_region(wc, memory_entry(wc, e), sizeof(struct wc_memory_entry));
	if (unlikely(waitqueue_active(&wc->freelist_wait)))
		wake_up(&wc->freelist_wait);
}

static void writecache_wait_on_freelist(struct dm_writecache *wc)
{
	DEFINE_WAIT(wait);

	prepare_to_wait(&wc->freelist_wait, &wait, TASK_UNINTERRUPTIBLE);
	wc_unlock(wc);
	io_schedule();
	finish_wait(&wc->freelist_wait, &wait);
	wc_lock(wc);
}

static void writecache_poison_lists(struct dm_writecache *wc)
{
	/*
	 * Catch incorrect access to these values while the device is suspended.
	 */
	memset(&wc->tree, -1, sizeof wc->tree);
	wc->lru.next = LIST_POISON1;
	wc->lru.prev = LIST_POISON2;
	wc->freelist.next = LIST_POISON1;
	wc->freelist.prev = LIST_POISON2;
}

static void writecache_flush_entry(struct dm_writecache *wc, struct wc_entry *e)
{
	writecache_flush_region(wc, memory_entry(wc, e), sizeof(struct wc_memory_entry));
	if (WC_MODE_PMEM(wc))
		writecache_flush_region(wc, memory_data(wc, e), wc->block_size);
}

static bool writecache_entry_is_committed(struct dm_writecache *wc, struct wc_entry *e)
{
	return read_seq_count(wc, e) < wc->seq_count;
}

static void writecache_flush(struct dm_writecache *wc)
{
	struct wc_entry *e, *e2;
	bool need_flush_after_free;

	wc->uncommitted_blocks = 0;
	del_timer(&wc->autocommit_timer);

	if (list_empty(&wc->lru))
		return;

	e = container_of(wc->lru.next, struct wc_entry, lru);
	if (writecache_entry_is_committed(wc, e)) {
		if (wc->overwrote_committed) {
			writecache_wait_for_ios(wc, WRITE);
			writecache_disk_flush(wc, wc->ssd_dev);
			wc->overwrote_committed = false;
		}
		return;
	}
	while (1) {
		writecache_flush_entry(wc, e);
		if (unlikely(e->lru.next == &wc->lru))
			break;
		e2 = container_of(e->lru.next, struct wc_entry, lru);
		if (writecache_entry_is_committed(wc, e2))
			break;
		e = e2;
		cond_resched();
	}
	writecache_commit_flushed(wc, true);

	wc->seq_count++;
	pmem_assign(sb(wc)->seq_count, cpu_to_le64(wc->seq_count));
	if (WC_MODE_PMEM(wc))
		writecache_commit_flushed(wc, false);
	else
		ssd_commit_superblock(wc);

	wc->overwrote_committed = false;

	need_flush_after_free = false;
	while (1) {
		/* Free another committed entry with lower seq-count */
		struct rb_node *rb_node = rb_prev(&e->rb_node);

		if (rb_node) {
			e2 = container_of(rb_node, struct wc_entry, rb_node);
			if (read_original_sector(wc, e2) == read_original_sector(wc, e) &&
			    likely(!e2->write_in_progress)) {
				writecache_free_entry(wc, e2);
				need_flush_after_free = true;
			}
		}
		if (unlikely(e->lru.prev == &wc->lru))
			break;
		e = container_of(e->lru.prev, struct wc_entry, lru);
		cond_resched();
	}

	if (need_flush_after_free)
		writecache_commit_flushed(wc, false);
}

static void writecache_flush_work(struct work_struct *work)
{
	struct dm_writecache *wc = container_of(work, struct dm_writecache, flush_work);

	wc_lock(wc);
	writecache_flush(wc);
	wc_unlock(wc);
}

static void writecache_autocommit_timer(struct timer_list *t)
{
	struct dm_writecache *wc = from_timer(wc, t, autocommit_timer);
	if (!writecache_has_error(wc))
		queue_work(wc->writeback_wq, &wc->flush_work);
}

static void writecache_schedule_autocommit(struct dm_writecache *wc)
{
	if (!timer_pending(&wc->autocommit_timer))
		mod_timer(&wc->autocommit_timer, jiffies + wc->autocommit_jiffies);
}

static void writecache_discard(struct dm_writecache *wc, sector_t start, sector_t end)
{
	struct wc_entry *e;
	bool discarded_something = false;

	e = writecache_find_entry(wc, start, WFE_RETURN_FOLLOWING | WFE_LOWEST_SEQ);
	if (unlikely(!e))
		return;

	while (read_original_sector(wc, e) < end) {
		struct rb_node *node = rb_next(&e->rb_node);

		if (likely(!e->write_in_progress)) {
			if (!discarded_something) {
				if (!WC_MODE_PMEM(wc)) {
					writecache_wait_for_ios(wc, READ);
					writecache_wait_for_ios(wc, WRITE);
				}
				discarded_something = true;
			}
			if (!writecache_entry_is_committed(wc, e))
				wc->uncommitted_blocks--;
			writecache_free_entry(wc, e);
		}

		if (unlikely(!node))
			break;

		e = container_of(node, struct wc_entry, rb_node);
	}

	if (discarded_something)
		writecache_commit_flushed(wc, false);
}

static bool writecache_wait_for_writeback(struct dm_writecache *wc)
{
	if (wc->writeback_size) {
		writecache_wait_on_freelist(wc);
		return true;
	}
	return false;
}

static void writecache_suspend(struct dm_target *ti)
{
	struct dm_writecache *wc = ti->private;
	bool flush_on_suspend;

	del_timer_sync(&wc->autocommit_timer);
	del_timer_sync(&wc->max_age_timer);

	wc_lock(wc);
	writecache_flush(wc);
	flush_on_suspend = wc->flush_on_suspend;
	if (flush_on_suspend) {
		wc->flush_on_suspend = false;
		wc->writeback_all++;
		queue_work(wc->writeback_wq, &wc->writeback_work);
	}
	wc_unlock(wc);

	drain_workqueue(wc->writeback_wq);

	wc_lock(wc);
	if (flush_on_suspend)
		wc->writeback_all--;
	while (writecache_wait_for_writeback(wc));

	if (WC_MODE_PMEM(wc))
		persistent_memory_flush_cache(wc->memory_map, wc->memory_map_size);

	writecache_poison_lists(wc);

	wc_unlock(wc);
}

static int writecache_alloc_entries(struct dm_writecache *wc)
{
	size_t b;

	if (wc->entries)
		return 0;
	wc->entries = vmalloc(array_size(sizeof(struct wc_entry), wc->n_blocks));
	if (!wc->entries)
		return -ENOMEM;
	for (b = 0; b < wc->n_blocks; b++) {
		struct wc_entry *e = &wc->entries[b];
		e->index = b;
		e->write_in_progress = false;
		cond_resched();
	}

	return 0;
}

static int writecache_read_metadata(struct dm_writecache *wc, sector_t n_sectors)
{
	struct dm_io_region region;
	struct dm_io_request req;

	region.bdev = wc->ssd_dev->bdev;
	region.sector = wc->start_sector;
	region.count = n_sectors;
	req.bi_op = REQ_OP_READ;
	req.bi_op_flags = REQ_SYNC;
	req.mem.type = DM_IO_VMA;
	req.mem.ptr.vma = (char *)wc->memory_map;
	req.client = wc->dm_io;
	req.notify.fn = NULL;

	return dm_io(&req, 1, &region, NULL);
}

static void writecache_resume(struct dm_target *ti)
{
	struct dm_writecache *wc = ti->private;
	size_t b;
	bool need_flush = false;
	__le64 sb_seq_count;
	int r;

	wc_lock(wc);

	wc->data_device_sectors = bdev_nr_sectors(wc->dev->bdev);

	if (WC_MODE_PMEM(wc)) {
		persistent_memory_invalidate_cache(wc->memory_map, wc->memory_map_size);
	} else {
		r = writecache_read_metadata(wc, wc->metadata_sectors);
		if (r) {
			size_t sb_entries_offset;
			writecache_error(wc, r, "unable to read metadata: %d", r);
			sb_entries_offset = offsetof(struct wc_memory_superblock, entries);
			memset((char *)wc->memory_map + sb_entries_offset, -1,
			       (wc->metadata_sectors << SECTOR_SHIFT) - sb_entries_offset);
		}
	}

	wc->tree = RB_ROOT;
	INIT_LIST_HEAD(&wc->lru);
	if (WC_MODE_SORT_FREELIST(wc)) {
		wc->freetree = RB_ROOT;
		wc->current_free = NULL;
	} else {
		INIT_LIST_HEAD(&wc->freelist);
	}
	wc->freelist_size = 0;

	r = copy_mc_to_kernel(&sb_seq_count, &sb(wc)->seq_count,
			      sizeof(uint64_t));
	if (r) {
		writecache_error(wc, r, "hardware memory error when reading superblock: %d", r);
		sb_seq_count = cpu_to_le64(0);
	}
	wc->seq_count = le64_to_cpu(sb_seq_count);

#ifdef DM_WRITECACHE_HANDLE_HARDWARE_ERRORS
	for (b = 0; b < wc->n_blocks; b++) {
		struct wc_entry *e = &wc->entries[b];
		struct wc_memory_entry wme;
		if (writecache_has_error(wc)) {
			e->original_sector = -1;
			e->seq_count = -1;
			continue;
		}
		r = copy_mc_to_kernel(&wme, memory_entry(wc, e),
				      sizeof(struct wc_memory_entry));
		if (r) {
			writecache_error(wc, r, "hardware memory error when reading metadata entry %lu: %d",
					 (unsigned long)b, r);
			e->original_sector = -1;
			e->seq_count = -1;
		} else {
			e->original_sector = le64_to_cpu(wme.original_sector);
			e->seq_count = le64_to_cpu(wme.seq_count);
		}
		cond_resched();
	}
#endif
	for (b = 0; b < wc->n_blocks; b++) {
		struct wc_entry *e = &wc->entries[b];
		if (!writecache_entry_is_committed(wc, e)) {
			if (read_seq_count(wc, e) != -1) {
erase_this:
				clear_seq_count(wc, e);
				need_flush = true;
			}
			writecache_add_to_freelist(wc, e);
		} else {
			struct wc_entry *old;

			old = writecache_find_entry(wc, read_original_sector(wc, e), 0);
			if (!old) {
				writecache_insert_entry(wc, e);
			} else {
				if (read_seq_count(wc, old) == read_seq_count(wc, e)) {
					writecache_error(wc, -EINVAL,
						 "two identical entries, position %llu, sector %llu, sequence %llu",
						 (unsigned long long)b, (unsigned long long)read_original_sector(wc, e),
						 (unsigned long long)read_seq_count(wc, e));
				}
				if (read_seq_count(wc, old) > read_seq_count(wc, e)) {
					goto erase_this;
				} else {
					writecache_free_entry(wc, old);
					writecache_insert_entry(wc, e);
					need_flush = true;
				}
			}
		}
		cond_resched();
	}

	if (need_flush) {
		writecache_flush_all_metadata(wc);
		writecache_commit_flushed(wc, false);
	}

	writecache_verify_watermark(wc);

	if (wc->max_age != MAX_AGE_UNSPECIFIED)
		mod_timer(&wc->max_age_timer, jiffies + wc->max_age / MAX_AGE_DIV);

	wc_unlock(wc);
}

static int process_flush_mesg(unsigned argc, char **argv, struct dm_writecache *wc)
{
	if (argc != 1)
		return -EINVAL;

	wc_lock(wc);
	if (dm_suspended(wc->ti)) {
		wc_unlock(wc);
		return -EBUSY;
	}
	if (writecache_has_error(wc)) {
		wc_unlock(wc);
		return -EIO;
	}

	writecache_flush(wc);
	wc->writeback_all++;
	queue_work(wc->writeback_wq, &wc->writeback_work);
	wc_unlock(wc);

	flush_workqueue(wc->writeback_wq);

	wc_lock(wc);
	wc->writeback_all--;
	if (writecache_has_error(wc)) {
		wc_unlock(wc);
		return -EIO;
	}
	wc_unlock(wc);

	return 0;
}

static int process_flush_on_suspend_mesg(unsigned argc, char **argv, struct dm_writecache *wc)
{
	if (argc != 1)
		return -EINVAL;

	wc_lock(wc);
	wc->flush_on_suspend = true;
	wc_unlock(wc);

	return 0;
}

static void activate_cleaner(struct dm_writecache *wc)
{
	wc->flush_on_suspend = true;
	wc->cleaner = true;
	wc->freelist_high_watermark = wc->n_blocks;
	wc->freelist_low_watermark = wc->n_blocks;
}

static int process_cleaner_mesg(unsigned argc, char **argv, struct dm_writecache *wc)
{
	if (argc != 1)
		return -EINVAL;

	wc_lock(wc);
	activate_cleaner(wc);
	if (!dm_suspended(wc->ti))
		writecache_verify_watermark(wc);
	wc_unlock(wc);

	return 0;
}

static int writecache_message(struct dm_target *ti, unsigned argc, char **argv,
			      char *result, unsigned maxlen)
{
	int r = -EINVAL;
	struct dm_writecache *wc = ti->private;

	if (!strcasecmp(argv[0], "flush"))
		r = process_flush_mesg(argc, argv, wc);
	else if (!strcasecmp(argv[0], "flush_on_suspend"))
		r = process_flush_on_suspend_mesg(argc, argv, wc);
	else if (!strcasecmp(argv[0], "cleaner"))
		r = process_cleaner_mesg(argc, argv, wc);
	else
		DMERR("unrecognised message received: %s", argv[0]);

	return r;
}

static void memcpy_flushcache_optimized(void *dest, void *source, size_t size)
{
	/*
	 * clflushopt performs better with block size 1024, 2048, 4096
	 * non-temporal stores perform better with block size 512
	 *
	 * block size   512             1024            2048            4096
	 * movnti       496 MB/s        642 MB/s        725 MB/s        744 MB/s
	 * clflushopt   373 MB/s        688 MB/s        1.1 GB/s        1.2 GB/s
	 *
	 * We see that movnti performs better for 512-byte blocks, and
	 * clflushopt performs better for 1024-byte and larger blocks. So, we
	 * prefer clflushopt for sizes >= 768.
	 *
	 * NOTE: this happens to be the case now (with dm-writecache's single
	 * threaded model) but re-evaluate this once memcpy_flushcache() is
	 * enabled to use movdir64b which might invalidate this performance
	 * advantage seen with cache-allocating-writes plus flushing.
	 */
#ifdef CONFIG_X86
	if (static_cpu_has(X86_FEATURE_CLFLUSHOPT) &&
	    likely(boot_cpu_data.x86_clflush_size == 64) &&
	    likely(size >= 768)) {
		do {
			memcpy((void *)dest, (void *)source, 64);
			clflushopt((void *)dest);
			dest += 64;
			source += 64;
			size -= 64;
		} while (size >= 64);
		return;
	}
#endif
	memcpy_flushcache(dest, source, size);
}

static void bio_copy_block(struct dm_writecache *wc, struct bio *bio, void *data)
{
	void *buf;
	unsigned long flags;
	unsigned size;
	int rw = bio_data_dir(bio);
	unsigned remaining_size = wc->block_size;

	do {
		struct bio_vec bv = bio_iter_iovec(bio, bio->bi_iter);
		buf = bvec_kmap_irq(&bv, &flags);
		size = bv.bv_len;
		if (unlikely(size > remaining_size))
			size = remaining_size;

		if (rw == READ) {
			int r;
			r = copy_mc_to_kernel(buf, data, size);
			flush_dcache_page(bio_page(bio));
			if (unlikely(r)) {
				writecache_error(wc, r, "hardware memory error when reading data: %d", r);
				bio->bi_status = BLK_STS_IOERR;
			}
		} else {
			flush_dcache_page(bio_page(bio));
			memcpy_flushcache_optimized(data, buf, size);
		}

		bvec_kunmap_irq(buf, &flags);

		data = (char *)data + size;
		remaining_size -= size;
		bio_advance(bio, size);
	} while (unlikely(remaining_size));
}

static int writecache_flush_thread(void *data)
{
	struct dm_writecache *wc = data;

	while (1) {
		struct bio *bio;

		wc_lock(wc);
		bio = bio_list_pop(&wc->flush_list);
		if (!bio) {
			set_current_state(TASK_INTERRUPTIBLE);
			wc_unlock(wc);

			if (unlikely(kthread_should_stop())) {
				set_current_state(TASK_RUNNING);
				break;
			}

			schedule();
			continue;
		}

		if (bio_op(bio) == REQ_OP_DISCARD) {
			writecache_discard(wc, bio->bi_iter.bi_sector,
					   bio_end_sector(bio));
			wc_unlock(wc);
			bio_set_dev(bio, wc->dev->bdev);
			submit_bio_noacct(bio);
		} else {
			writecache_flush(wc);
			wc_unlock(wc);
			if (writecache_has_error(wc))
				bio->bi_status = BLK_STS_IOERR;
			bio_endio(bio);
		}
	}

	return 0;
}

static void writecache_offload_bio(struct dm_writecache *wc, struct bio *bio)
{
	if (bio_list_empty(&wc->flush_list))
		wake_up_process(wc->flush_thread);
	bio_list_add(&wc->flush_list, bio);
}

static int writecache_map(struct dm_target *ti, struct bio *bio)
{
	struct wc_entry *e;
	struct dm_writecache *wc = ti->private;

	bio->bi_private = NULL;

	wc_lock(wc);

	if (unlikely(bio->bi_opf & REQ_PREFLUSH)) {
		if (writecache_has_error(wc))
			goto unlock_error;
		if (WC_MODE_PMEM(wc)) {
			writecache_flush(wc);
			if (writecache_has_error(wc))
				goto unlock_error;
			goto unlock_submit;
		} else {
			writecache_offload_bio(wc, bio);
			goto unlock_return;
		}
	}

	bio->bi_iter.bi_sector = dm_target_offset(ti, bio->bi_iter.bi_sector);

	if (unlikely((((unsigned)bio->bi_iter.bi_sector | bio_sectors(bio)) &
				(wc->block_size / 512 - 1)) != 0)) {
		DMERR("I/O is not aligned, sector %llu, size %u, block size %u",
		      (unsigned long long)bio->bi_iter.bi_sector,
		      bio->bi_iter.bi_size, wc->block_size);
		goto unlock_error;
	}

	if (unlikely(bio_op(bio) == REQ_OP_DISCARD)) {
		if (writecache_has_error(wc))
			goto unlock_error;
		if (WC_MODE_PMEM(wc)) {
			writecache_discard(wc, bio->bi_iter.bi_sector, bio_end_sector(bio));
			goto unlock_remap_origin;
		} else {
			writecache_offload_bio(wc, bio);
			goto unlock_return;
		}
	}

	if (bio_data_dir(bio) == READ) {
read_next_block:
		e = writecache_find_entry(wc, bio->bi_iter.bi_sector, WFE_RETURN_FOLLOWING);
		if (e && read_original_sector(wc, e) == bio->bi_iter.bi_sector) {
			if (WC_MODE_PMEM(wc)) {
				bio_copy_block(wc, bio, memory_data(wc, e));
				if (bio->bi_iter.bi_size)
					goto read_next_block;
				goto unlock_submit;
			} else {
				dm_accept_partial_bio(bio, wc->block_size >> SECTOR_SHIFT);
				bio_set_dev(bio, wc->ssd_dev->bdev);
				bio->bi_iter.bi_sector = cache_sector(wc, e);
				if (!writecache_entry_is_committed(wc, e))
					writecache_wait_for_ios(wc, WRITE);
				goto unlock_remap;
			}
		} else {
			if (e) {
				sector_t next_boundary =
					read_original_sector(wc, e) - bio->bi_iter.bi_sector;
				if (next_boundary < bio->bi_iter.bi_size >> SECTOR_SHIFT) {
					dm_accept_partial_bio(bio, next_boundary);
				}
			}
			goto unlock_remap_origin;
		}
	} else {
		do {
			bool found_entry = false;
			if (writecache_has_error(wc))
				goto unlock_error;
			e = writecache_find_entry(wc, bio->bi_iter.bi_sector, 0);
			if (e) {
				if (!writecache_entry_is_committed(wc, e))
					goto bio_copy;
				if (!WC_MODE_PMEM(wc) && !e->write_in_progress) {
					wc->overwrote_committed = true;
					goto bio_copy;
				}
				found_entry = true;
			} else {
				if (unlikely(wc->cleaner))
					goto direct_write;
			}
			e = writecache_pop_from_freelist(wc, (sector_t)-1);
			if (unlikely(!e)) {
				if (!found_entry) {
direct_write:
					e = writecache_find_entry(wc, bio->bi_iter.bi_sector, WFE_RETURN_FOLLOWING);
					if (e) {
						sector_t next_boundary = read_original_sector(wc, e) - bio->bi_iter.bi_sector;
						BUG_ON(!next_boundary);
						if (next_boundary < bio->bi_iter.bi_size >> SECTOR_SHIFT) {
							dm_accept_partial_bio(bio, next_boundary);
						}
					}
					goto unlock_remap_origin;
				}
				writecache_wait_on_freelist(wc);
				continue;
			}
			write_original_sector_seq_count(wc, e, bio->bi_iter.bi_sector, wc->seq_count);
			writecache_insert_entry(wc, e);
			wc->uncommitted_blocks++;
bio_copy:
			if (WC_MODE_PMEM(wc)) {
				bio_copy_block(wc, bio, memory_data(wc, e));
			} else {
				unsigned bio_size = wc->block_size;
				sector_t start_cache_sec = cache_sector(wc, e);
				sector_t current_cache_sec = start_cache_sec + (bio_size >> SECTOR_SHIFT);

				while (bio_size < bio->bi_iter.bi_size) {
					struct wc_entry *f = writecache_pop_from_freelist(wc, current_cache_sec);
					if (!f)
						break;
					write_original_sector_seq_count(wc, f, bio->bi_iter.bi_sector +
									(bio_size >> SECTOR_SHIFT), wc->seq_count);
					writecache_insert_entry(wc, f);
					wc->uncommitted_blocks++;
					bio_size += wc->block_size;
					current_cache_sec += wc->block_size >> SECTOR_SHIFT;
				}

				bio_set_dev(bio, wc->ssd_dev->bdev);
				bio->bi_iter.bi_sector = start_cache_sec;
				dm_accept_partial_bio(bio, bio_size >> SECTOR_SHIFT);

				if (unlikely(wc->uncommitted_blocks >= wc->autocommit_blocks)) {
					wc->uncommitted_blocks = 0;
					queue_work(wc->writeback_wq, &wc->flush_work);
				} else {
					writecache_schedule_autocommit(wc);
				}
				goto unlock_remap;
			}
		} while (bio->bi_iter.bi_size);

		if (unlikely(bio->bi_opf & REQ_FUA ||
			     wc->uncommitted_blocks >= wc->autocommit_blocks))
			writecache_flush(wc);
		else
			writecache_schedule_autocommit(wc);
		goto unlock_submit;
	}

unlock_remap_origin:
	bio_set_dev(bio, wc->dev->bdev);
	wc_unlock(wc);
	return DM_MAPIO_REMAPPED;

unlock_remap:
	/* make sure that writecache_end_io decrements bio_in_progress: */
	bio->bi_private = (void *)1;
	atomic_inc(&wc->bio_in_progress[bio_data_dir(bio)]);
	wc_unlock(wc);
	return DM_MAPIO_REMAPPED;

unlock_submit:
	wc_unlock(wc);
	bio_endio(bio);
	return DM_MAPIO_SUBMITTED;

unlock_return:
	wc_unlock(wc);
	return DM_MAPIO_SUBMITTED;

unlock_error:
	wc_unlock(wc);
	bio_io_error(bio);
	return DM_MAPIO_SUBMITTED;
}

static int writecache_end_io(struct dm_target *ti, struct bio *bio, blk_status_t *status)
{
	struct dm_writecache *wc = ti->private;

	if (bio->bi_private != NULL) {
		int dir = bio_data_dir(bio);
		if (atomic_dec_and_test(&wc->bio_in_progress[dir]))
			if (unlikely(waitqueue_active(&wc->bio_in_progress_wait[dir])))
				wake_up(&wc->bio_in_progress_wait[dir]);
	}
	return 0;
}

static int writecache_iterate_devices(struct dm_target *ti,
				      iterate_devices_callout_fn fn, void *data)
{
	struct dm_writecache *wc = ti->private;

	return fn(ti, wc->dev, 0, ti->len, data);
}

static void writecache_io_hints(struct dm_target *ti, struct queue_limits *limits)
{
	struct dm_writecache *wc = ti->private;

	if (limits->logical_block_size < wc->block_size)
		limits->logical_block_size = wc->block_size;

	if (limits->physical_block_size < wc->block_size)
		limits->physical_block_size = wc->block_size;

	if (limits->io_min < wc->block_size)
		limits->io_min = wc->block_size;
}


static void writecache_writeback_endio(struct bio *bio)
{
	struct writeback_struct *wb = container_of(bio, struct writeback_struct, bio);
	struct dm_writecache *wc = wb->wc;
	unsigned long flags;

	raw_spin_lock_irqsave(&wc->endio_list_lock, flags);
	if (unlikely(list_empty(&wc->endio_list)))
		wake_up_process(wc->endio_thread);
	list_add_tail(&wb->endio_entry, &wc->endio_list);
	raw_spin_unlock_irqrestore(&wc->endio_list_lock, flags);
}

static void writecache_copy_endio(int read_err, unsigned long write_err, void *ptr)
{
	struct copy_struct *c = ptr;
	struct dm_writecache *wc = c->wc;

	c->error = likely(!(read_err | write_err)) ? 0 : -EIO;

	raw_spin_lock_irq(&wc->endio_list_lock);
	if (unlikely(list_empty(&wc->endio_list)))
		wake_up_process(wc->endio_thread);
	list_add_tail(&c->endio_entry, &wc->endio_list);
	raw_spin_unlock_irq(&wc->endio_list_lock);
}

static void __writecache_endio_pmem(struct dm_writecache *wc, struct list_head *list)
{
	unsigned i;
	struct writeback_struct *wb;
	struct wc_entry *e;
	unsigned long n_walked = 0;

	do {
		wb = list_entry(list->next, struct writeback_struct, endio_entry);
		list_del(&wb->endio_entry);

		if (unlikely(wb->bio.bi_status != BLK_STS_OK))
			writecache_error(wc, blk_status_to_errno(wb->bio.bi_status),
					"write error %d", wb->bio.bi_status);
		i = 0;
		do {
			e = wb->wc_list[i];
			BUG_ON(!e->write_in_progress);
			e->write_in_progress = false;
			INIT_LIST_HEAD(&e->lru);
			if (!writecache_has_error(wc))
				writecache_free_entry(wc, e);
			BUG_ON(!wc->writeback_size);
			wc->writeback_size--;
			n_walked++;
			if (unlikely(n_walked >= ENDIO_LATENCY)) {
				writecache_commit_flushed(wc, false);
				wc_unlock(wc);
				wc_lock(wc);
				n_walked = 0;
			}
		} while (++i < wb->wc_list_n);

		if (wb->wc_list != wb->wc_list_inline)
			kfree(wb->wc_list);
		bio_put(&wb->bio);
	} while (!list_empty(list));
}

static void __writecache_endio_ssd(struct dm_writecache *wc, struct list_head *list)
{
	struct copy_struct *c;
	struct wc_entry *e;

	do {
		c = list_entry(list->next, struct copy_struct, endio_entry);
		list_del(&c->endio_entry);

		if (unlikely(c->error))
			writecache_error(wc, c->error, "copy error");

		e = c->e;
		do {
			BUG_ON(!e->write_in_progress);
			e->write_in_progress = false;
			INIT_LIST_HEAD(&e->lru);
			if (!writecache_has_error(wc))
				writecache_free_entry(wc, e);

			BUG_ON(!wc->writeback_size);
			wc->writeback_size--;
			e++;
		} while (--c->n_entries);
		mempool_free(c, &wc->copy_pool);
	} while (!list_empty(list));
}

static int writecache_endio_thread(void *data)
{
	struct dm_writecache *wc = data;

	while (1) {
		struct list_head list;

		raw_spin_lock_irq(&wc->endio_list_lock);
		if (!list_empty(&wc->endio_list))
			goto pop_from_list;
		set_current_state(TASK_INTERRUPTIBLE);
		raw_spin_unlock_irq(&wc->endio_list_lock);

		if (unlikely(kthread_should_stop())) {
			set_current_state(TASK_RUNNING);
			break;
		}

		schedule();

		continue;

pop_from_list:
		list = wc->endio_list;
		list.next->prev = list.prev->next = &list;
		INIT_LIST_HEAD(&wc->endio_list);
		raw_spin_unlock_irq(&wc->endio_list_lock);

		if (!WC_MODE_FUA(wc))
			writecache_disk_flush(wc, wc->dev);

		wc_lock(wc);

		if (WC_MODE_PMEM(wc)) {
			__writecache_endio_pmem(wc, &list);
		} else {
			__writecache_endio_ssd(wc, &list);
			writecache_wait_for_ios(wc, READ);
		}

		writecache_commit_flushed(wc, false);

		wc_unlock(wc);
	}

	return 0;
}

static bool wc_add_block(struct writeback_struct *wb, struct wc_entry *e, gfp_t gfp)
{
	struct dm_writecache *wc = wb->wc;
	unsigned block_size = wc->block_size;
	void *address = memory_data(wc, e);

	persistent_memory_flush_cache(address, block_size);

	if (unlikely(bio_end_sector(&wb->bio) >= wc->data_device_sectors))
		return true;

	return bio_add_page(&wb->bio, persistent_memory_page(address),
			    block_size, persistent_memory_page_offset(address)) != 0;
}

struct writeback_list {
	struct list_head list;
	size_t size;
};

static void __writeback_throttle(struct dm_writecache *wc, struct writeback_list *wbl)
{
	if (unlikely(wc->max_writeback_jobs)) {
		if (READ_ONCE(wc->writeback_size) - wbl->size >= wc->max_writeback_jobs) {
			wc_lock(wc);
			while (wc->writeback_size - wbl->size >= wc->max_writeback_jobs)
				writecache_wait_on_freelist(wc);
			wc_unlock(wc);
		}
	}
	cond_resched();
}

static void __writecache_writeback_pmem(struct dm_writecache *wc, struct writeback_list *wbl)
{
	struct wc_entry *e, *f;
	struct bio *bio;
	struct writeback_struct *wb;
	unsigned max_pages;

	while (wbl->size) {
		wbl->size--;
		e = container_of(wbl->list.prev, struct wc_entry, lru);
		list_del(&e->lru);

		max_pages = e->wc_list_contiguous;

		bio = bio_alloc_bioset(GFP_NOIO, max_pages, &wc->bio_set);
		wb = container_of(bio, struct writeback_struct, bio);
		wb->wc = wc;
		bio->bi_end_io = writecache_writeback_endio;
		bio_set_dev(bio, wc->dev->bdev);
		bio->bi_iter.bi_sector = read_original_sector(wc, e);
		if (max_pages <= WB_LIST_INLINE ||
		    unlikely(!(wb->wc_list = kmalloc_array(max_pages, sizeof(struct wc_entry *),
							   GFP_NOIO | __GFP_NORETRY |
							   __GFP_NOMEMALLOC | __GFP_NOWARN)))) {
			wb->wc_list = wb->wc_list_inline;
			max_pages = WB_LIST_INLINE;
		}

		BUG_ON(!wc_add_block(wb, e, GFP_NOIO));

		wb->wc_list[0] = e;
		wb->wc_list_n = 1;

		while (wbl->size && wb->wc_list_n < max_pages) {
			f = container_of(wbl->list.prev, struct wc_entry, lru);
			if (read_original_sector(wc, f) !=
			    read_original_sector(wc, e) + (wc->block_size >> SECTOR_SHIFT))
				break;
			if (!wc_add_block(wb, f, GFP_NOWAIT | __GFP_NOWARN))
				break;
			wbl->size--;
			list_del(&f->lru);
			wb->wc_list[wb->wc_list_n++] = f;
			e = f;
		}
		bio_set_op_attrs(bio, REQ_OP_WRITE, WC_MODE_FUA(wc) * REQ_FUA);
		if (writecache_has_error(wc)) {
			bio->bi_status = BLK_STS_IOERR;
			bio_endio(bio);
		} else if (unlikely(!bio_sectors(bio))) {
			bio->bi_status = BLK_STS_OK;
			bio_endio(bio);
		} else {
			submit_bio(bio);
		}

		__writeback_throttle(wc, wbl);
	}
}

static void __writecache_writeback_ssd(struct dm_writecache *wc, struct writeback_list *wbl)
{
	struct wc_entry *e, *f;
	struct dm_io_region from, to;
	struct copy_struct *c;

	while (wbl->size) {
		unsigned n_sectors;

		wbl->size--;
		e = container_of(wbl->list.prev, struct wc_entry, lru);
		list_del(&e->lru);

		n_sectors = e->wc_list_contiguous << (wc->block_size_bits - SECTOR_SHIFT);

		from.bdev = wc->ssd_dev->bdev;
		from.sector = cache_sector(wc, e);
		from.count = n_sectors;
		to.bdev = wc->dev->bdev;
		to.sector = read_original_sector(wc, e);
		to.count = n_sectors;

		c = mempool_alloc(&wc->copy_pool, GFP_NOIO);
		c->wc = wc;
		c->e = e;
		c->n_entries = e->wc_list_contiguous;

		while ((n_sectors -= wc->block_size >> SECTOR_SHIFT)) {
			wbl->size--;
			f = container_of(wbl->list.prev, struct wc_entry, lru);
			BUG_ON(f != e + 1);
			list_del(&f->lru);
			e = f;
		}

		if (unlikely(to.sector + to.count > wc->data_device_sectors)) {
			if (to.sector >= wc->data_device_sectors) {
				writecache_copy_endio(0, 0, c);
				continue;
			}
			from.count = to.count = wc->data_device_sectors - to.sector;
		}

		dm_kcopyd_copy(wc->dm_kcopyd, &from, 1, &to, 0, writecache_copy_endio, c);

		__writeback_throttle(wc, wbl);
	}
}

static void writecache_writeback(struct work_struct *work)
{
	struct dm_writecache *wc = container_of(work, struct dm_writecache, writeback_work);
	struct blk_plug plug;
	struct wc_entry *f, *g, *e = NULL;
	struct rb_node *node, *next_node;
	struct list_head skipped;
	struct writeback_list wbl;
	unsigned long n_walked;

	wc_lock(wc);
restart:
	if (writecache_has_error(wc)) {
		wc_unlock(wc);
		return;
	}

	if (unlikely(wc->writeback_all)) {
		if (writecache_wait_for_writeback(wc))
			goto restart;
	}

	if (wc->overwrote_committed) {
		writecache_wait_for_ios(wc, WRITE);
	}

	n_walked = 0;
	INIT_LIST_HEAD(&skipped);
	INIT_LIST_HEAD(&wbl.list);
	wbl.size = 0;
	while (!list_empty(&wc->lru) &&
	       (wc->writeback_all ||
		wc->freelist_size + wc->writeback_size <= wc->freelist_low_watermark ||
		(jiffies - container_of(wc->lru.prev, struct wc_entry, lru)->age >=
		 wc->max_age - wc->max_age / MAX_AGE_DIV))) {

		n_walked++;
		if (unlikely(n_walked > WRITEBACK_LATENCY) &&
		    likely(!wc->writeback_all) && likely(!dm_suspended(wc->ti))) {
			queue_work(wc->writeback_wq, &wc->writeback_work);
			break;
		}

		if (unlikely(wc->writeback_all)) {
			if (unlikely(!e)) {
				writecache_flush(wc);
				e = container_of(rb_first(&wc->tree), struct wc_entry, rb_node);
			} else
				e = g;
		} else
			e = container_of(wc->lru.prev, struct wc_entry, lru);
		BUG_ON(e->write_in_progress);
		if (unlikely(!writecache_entry_is_committed(wc, e))) {
			writecache_flush(wc);
		}
		node = rb_prev(&e->rb_node);
		if (node) {
			f = container_of(node, struct wc_entry, rb_node);
			if (unlikely(read_original_sector(wc, f) ==
				     read_original_sector(wc, e))) {
				BUG_ON(!f->write_in_progress);
				list_del(&e->lru);
				list_add(&e->lru, &skipped);
				cond_resched();
				continue;
			}
		}
		wc->writeback_size++;
		list_del(&e->lru);
		list_add(&e->lru, &wbl.list);
		wbl.size++;
		e->write_in_progress = true;
		e->wc_list_contiguous = 1;

		f = e;

		while (1) {
			next_node = rb_next(&f->rb_node);
			if (unlikely(!next_node))
				break;
			g = container_of(next_node, struct wc_entry, rb_node);
			if (unlikely(read_original_sector(wc, g) ==
			    read_original_sector(wc, f))) {
				f = g;
				continue;
			}
			if (read_original_sector(wc, g) !=
			    read_original_sector(wc, f) + (wc->block_size >> SECTOR_SHIFT))
				break;
			if (unlikely(g->write_in_progress))
				break;
			if (unlikely(!writecache_entry_is_committed(wc, g)))
				break;

			if (!WC_MODE_PMEM(wc)) {
				if (g != f + 1)
					break;
			}

			n_walked++;
			//if (unlikely(n_walked > WRITEBACK_LATENCY) && likely(!wc->writeback_all))
			//	break;

			wc->writeback_size++;
			list_del(&g->lru);
			list_add(&g->lru, &wbl.list);
			wbl.size++;
			g->write_in_progress = true;
			g->wc_list_contiguous = BIO_MAX_VECS;
			f = g;
			e->wc_list_contiguous++;
			if (unlikely(e->wc_list_contiguous == BIO_MAX_VECS)) {
				if (unlikely(wc->writeback_all)) {
					next_node = rb_next(&f->rb_node);
					if (likely(next_node))
						g = container_of(next_node, struct wc_entry, rb_node);
				}
				break;
			}
		}
		cond_resched();
	}

	if (!list_empty(&skipped)) {
		list_splice_tail(&skipped, &wc->lru);
		/*
		 * If we didn't do any progress, we must wait until some
		 * writeback finishes to avoid burning CPU in a loop
		 */
		if (unlikely(!wbl.size))
			writecache_wait_for_writeback(wc);
	}

	wc_unlock(wc);

	blk_start_plug(&plug);

	if (WC_MODE_PMEM(wc))
		__writecache_writeback_pmem(wc, &wbl);
	else
		__writecache_writeback_ssd(wc, &wbl);

	blk_finish_plug(&plug);

	if (unlikely(wc->writeback_all)) {
		wc_lock(wc);
		while (writecache_wait_for_writeback(wc));
		wc_unlock(wc);
	}
}

static int calculate_memory_size(uint64_t device_size, unsigned block_size,
				 size_t *n_blocks_p, size_t *n_metadata_blocks_p)
{
	uint64_t n_blocks, offset;
	struct wc_entry e;

	n_blocks = device_size;
	do_div(n_blocks, block_size + sizeof(struct wc_memory_entry));

	while (1) {
		if (!n_blocks)
			return -ENOSPC;
		/* Verify the following entries[n_blocks] won't overflow */
		if (n_blocks >= ((size_t)-sizeof(struct wc_memory_superblock) /
				 sizeof(struct wc_memory_entry)))
			return -EFBIG;
		offset = offsetof(struct wc_memory_superblock, entries[n_blocks]);
		offset = (offset + block_size - 1) & ~(uint64_t)(block_size - 1);
		if (offset + n_blocks * block_size <= device_size)
			break;
		n_blocks--;
	}

	/* check if the bit field overflows */
	e.index = n_blocks;
	if (e.index != n_blocks)
		return -EFBIG;

	if (n_blocks_p)
		*n_blocks_p = n_blocks;
	if (n_metadata_blocks_p)
		*n_metadata_blocks_p = offset >> __ffs(block_size);
	return 0;
}

static int init_memory(struct dm_writecache *wc)
{
	size_t b;
	int r;

	r = calculate_memory_size(wc->memory_map_size, wc->block_size, &wc->n_blocks, NULL);
	if (r)
		return r;

	r = writecache_alloc_entries(wc);
	if (r)
		return r;

	for (b = 0; b < ARRAY_SIZE(sb(wc)->padding); b++)
		pmem_assign(sb(wc)->padding[b], cpu_to_le64(0));
	pmem_assign(sb(wc)->version, cpu_to_le32(MEMORY_SUPERBLOCK_VERSION));
	pmem_assign(sb(wc)->block_size, cpu_to_le32(wc->block_size));
	pmem_assign(sb(wc)->n_blocks, cpu_to_le64(wc->n_blocks));
	pmem_assign(sb(wc)->seq_count, cpu_to_le64(0));

	for (b = 0; b < wc->n_blocks; b++) {
		write_original_sector_seq_count(wc, &wc->entries[b], -1, -1);
		cond_resched();
	}

	writecache_flush_all_metadata(wc);
	writecache_commit_flushed(wc, false);
	pmem_assign(sb(wc)->magic, cpu_to_le32(MEMORY_SUPERBLOCK_MAGIC));
	writecache_flush_region(wc, &sb(wc)->magic, sizeof sb(wc)->magic);
	writecache_commit_flushed(wc, false);

	return 0;
}

static void writecache_dtr(struct dm_target *ti)
{
	struct dm_writecache *wc = ti->private;

	if (!wc)
		return;

	if (wc->endio_thread)
		kthread_stop(wc->endio_thread);

	if (wc->flush_thread)
		kthread_stop(wc->flush_thread);

	bioset_exit(&wc->bio_set);

	mempool_exit(&wc->copy_pool);

	if (wc->writeback_wq)
		destroy_workqueue(wc->writeback_wq);

	if (wc->dev)
		dm_put_device(ti, wc->dev);

	if (wc->ssd_dev)
		dm_put_device(ti, wc->ssd_dev);

	vfree(wc->entries);

	if (wc->memory_map) {
		if (WC_MODE_PMEM(wc))
			persistent_memory_release(wc);
		else
			vfree(wc->memory_map);
	}

	if (wc->dm_kcopyd)
		dm_kcopyd_client_destroy(wc->dm_kcopyd);

	if (wc->dm_io)
		dm_io_client_destroy(wc->dm_io);

	vfree(wc->dirty_bitmap);

	kfree(wc);
}

static int writecache_ctr(struct dm_target *ti, unsigned argc, char **argv)
{
	struct dm_writecache *wc;
	struct dm_arg_set as;
	const char *string;
	unsigned opt_params;
	size_t offset, data_size;
	int i, r;
	char dummy;
	int high_wm_percent = HIGH_WATERMARK;
	int low_wm_percent = LOW_WATERMARK;
	uint64_t x;
	struct wc_memory_superblock s;

	static struct dm_arg _args[] = {
		{0, 16, "Invalid number of feature args"},
	};

	as.argc = argc;
	as.argv = argv;

	wc = kzalloc(sizeof(struct dm_writecache), GFP_KERNEL);
	if (!wc) {
		ti->error = "Cannot allocate writecache structure";
		r = -ENOMEM;
		goto bad;
	}
	ti->private = wc;
	wc->ti = ti;

	mutex_init(&wc->lock);
	wc->max_age = MAX_AGE_UNSPECIFIED;
	writecache_poison_lists(wc);
	init_waitqueue_head(&wc->freelist_wait);
	timer_setup(&wc->autocommit_timer, writecache_autocommit_timer, 0);
	timer_setup(&wc->max_age_timer, writecache_max_age_timer, 0);

	for (i = 0; i < 2; i++) {
		atomic_set(&wc->bio_in_progress[i], 0);
		init_waitqueue_head(&wc->bio_in_progress_wait[i]);
	}

	wc->dm_io = dm_io_client_create();
	if (IS_ERR(wc->dm_io)) {
		r = PTR_ERR(wc->dm_io);
		ti->error = "Unable to allocate dm-io client";
		wc->dm_io = NULL;
		goto bad;
	}

	wc->writeback_wq = alloc_workqueue("writecache-writeback", WQ_MEM_RECLAIM, 1);
	if (!wc->writeback_wq) {
		r = -ENOMEM;
		ti->error = "Could not allocate writeback workqueue";
		goto bad;
	}
	INIT_WORK(&wc->writeback_work, writecache_writeback);
	INIT_WORK(&wc->flush_work, writecache_flush_work);

	raw_spin_lock_init(&wc->endio_list_lock);
	INIT_LIST_HEAD(&wc->endio_list);
	wc->endio_thread = kthread_create(writecache_endio_thread, wc, "writecache_endio");
	if (IS_ERR(wc->endio_thread)) {
		r = PTR_ERR(wc->endio_thread);
		wc->endio_thread = NULL;
		ti->error = "Couldn't spawn endio thread";
		goto bad;
	}
	wake_up_process(wc->endio_thread);

	/*
	 * Parse the mode (pmem or ssd)
	 */
	string = dm_shift_arg(&as);
	if (!string)
		goto bad_arguments;

	if (!strcasecmp(string, "s")) {
		wc->pmem_mode = false;
	} else if (!strcasecmp(string, "p")) {
#ifdef DM_WRITECACHE_HAS_PMEM
		wc->pmem_mode = true;
		wc->writeback_fua = true;
#else
		/*
		 * If the architecture doesn't support persistent memory or
		 * the kernel doesn't support any DAX drivers, this driver can
		 * only be used in SSD-only mode.
		 */
		r = -EOPNOTSUPP;
		ti->error = "Persistent memory or DAX not supported on this system";
		goto bad;
#endif
	} else {
		goto bad_arguments;
	}

	if (WC_MODE_PMEM(wc)) {
		r = bioset_init(&wc->bio_set, BIO_POOL_SIZE,
				offsetof(struct writeback_struct, bio),
				BIOSET_NEED_BVECS);
		if (r) {
			ti->error = "Could not allocate bio set";
			goto bad;
		}
	} else {
		r = mempool_init_kmalloc_pool(&wc->copy_pool, 1, sizeof(struct copy_struct));
		if (r) {
			ti->error = "Could not allocate mempool";
			goto bad;
		}
	}

	/*
	 * Parse the origin data device
	 */
	string = dm_shift_arg(&as);
	if (!string)
		goto bad_arguments;
	r = dm_get_device(ti, string, dm_table_get_mode(ti->table), &wc->dev);
	if (r) {
		ti->error = "Origin data device lookup failed";
		goto bad;
	}

	/*
	 * Parse cache data device (be it pmem or ssd)
	 */
	string = dm_shift_arg(&as);
	if (!string)
		goto bad_arguments;

	r = dm_get_device(ti, string, dm_table_get_mode(ti->table), &wc->ssd_dev);
	if (r) {
		ti->error = "Cache data device lookup failed";
		goto bad;
	}
	wc->memory_map_size = i_size_read(wc->ssd_dev->bdev->bd_inode);

	/*
	 * Parse the cache block size
	 */
	string = dm_shift_arg(&as);
	if (!string)
		goto bad_arguments;
	if (sscanf(string, "%u%c", &wc->block_size, &dummy) != 1 ||
	    wc->block_size < 512 || wc->block_size > PAGE_SIZE ||
	    (wc->block_size & (wc->block_size - 1))) {
		r = -EINVAL;
		ti->error = "Invalid block size";
		goto bad;
	}
	if (wc->block_size < bdev_logical_block_size(wc->dev->bdev) ||
	    wc->block_size < bdev_logical_block_size(wc->ssd_dev->bdev)) {
		r = -EINVAL;
		ti->error = "Block size is smaller than device logical block size";
		goto bad;
	}
	wc->block_size_bits = __ffs(wc->block_size);

	wc->max_writeback_jobs = MAX_WRITEBACK_JOBS;
	wc->autocommit_blocks = !WC_MODE_PMEM(wc) ? AUTOCOMMIT_BLOCKS_SSD : AUTOCOMMIT_BLOCKS_PMEM;
	wc->autocommit_jiffies = msecs_to_jiffies(AUTOCOMMIT_MSEC);

	/*
	 * Parse optional arguments
	 */
	r = dm_read_arg_group(_args, &as, &opt_params, &ti->error);
	if (r)
		goto bad;

	while (opt_params) {
		string = dm_shift_arg(&as), opt_params--;
		if (!strcasecmp(string, "start_sector") && opt_params >= 1) {
			unsigned long long start_sector;
			string = dm_shift_arg(&as), opt_params--;
			if (sscanf(string, "%llu%c", &start_sector, &dummy) != 1)
				goto invalid_optional;
			wc->start_sector = start_sector;
			wc->start_sector_set = true;
			if (wc->start_sector != start_sector ||
			    wc->start_sector >= wc->memory_map_size >> SECTOR_SHIFT)
				goto invalid_optional;
		} else if (!strcasecmp(string, "high_watermark") && opt_params >= 1) {
			string = dm_shift_arg(&as), opt_params--;
			if (sscanf(string, "%d%c", &high_wm_percent, &dummy) != 1)
				goto invalid_optional;
			if (high_wm_percent < 0 || high_wm_percent > 100)
				goto invalid_optional;
			wc->high_wm_percent_value = high_wm_percent;
			wc->high_wm_percent_set = true;
		} else if (!strcasecmp(string, "low_watermark") && opt_params >= 1) {
			string = dm_shift_arg(&as), opt_params--;
			if (sscanf(string, "%d%c", &low_wm_percent, &dummy) != 1)
				goto invalid_optional;
			if (low_wm_percent < 0 || low_wm_percent > 100)
				goto invalid_optional;
			wc->low_wm_percent_value = low_wm_percent;
			wc->low_wm_percent_set = true;
		} else if (!strcasecmp(string, "writeback_jobs") && opt_params >= 1) {
			string = dm_shift_arg(&as), opt_params--;
			if (sscanf(string, "%u%c", &wc->max_writeback_jobs, &dummy) != 1)
				goto invalid_optional;
			wc->max_writeback_jobs_set = true;
		} else if (!strcasecmp(string, "autocommit_blocks") && opt_params >= 1) {
			string = dm_shift_arg(&as), opt_params--;
			if (sscanf(string, "%u%c", &wc->autocommit_blocks, &dummy) != 1)
				goto invalid_optional;
			wc->autocommit_blocks_set = true;
		} else if (!strcasecmp(string, "autocommit_time") && opt_params >= 1) {
			unsigned autocommit_msecs;
			string = dm_shift_arg(&as), opt_params--;
			if (sscanf(string, "%u%c", &autocommit_msecs, &dummy) != 1)
				goto invalid_optional;
			if (autocommit_msecs > 3600000)
				goto invalid_optional;
			wc->autocommit_jiffies = msecs_to_jiffies(autocommit_msecs);
			wc->autocommit_time_value = autocommit_msecs;
			wc->autocommit_time_set = true;
		} else if (!strcasecmp(string, "max_age") && opt_params >= 1) {
			unsigned max_age_msecs;
			string = dm_shift_arg(&as), opt_params--;
			if (sscanf(string, "%u%c", &max_age_msecs, &dummy) != 1)
				goto invalid_optional;
			if (max_age_msecs > 86400000)
				goto invalid_optional;
			wc->max_age = msecs_to_jiffies(max_age_msecs);
			wc->max_age_set = true;
			wc->max_age_value = max_age_msecs;
		} else if (!strcasecmp(string, "cleaner")) {
			wc->cleaner_set = true;
			wc->cleaner = true;
		} else if (!strcasecmp(string, "fua")) {
			if (WC_MODE_PMEM(wc)) {
				wc->writeback_fua = true;
				wc->writeback_fua_set = true;
			} else goto invalid_optional;
		} else if (!strcasecmp(string, "nofua")) {
			if (WC_MODE_PMEM(wc)) {
				wc->writeback_fua = false;
				wc->writeback_fua_set = true;
			} else goto invalid_optional;
		} else {
invalid_optional:
			r = -EINVAL;
			ti->error = "Invalid optional argument";
			goto bad;
		}
	}

	if (high_wm_percent < low_wm_percent) {
		r = -EINVAL;
		ti->error = "High watermark must be greater than or equal to low watermark";
		goto bad;
	}

	if (WC_MODE_PMEM(wc)) {
		if (!dax_synchronous(wc->ssd_dev->dax_dev)) {
			r = -EOPNOTSUPP;
			ti->error = "Asynchronous persistent memory not supported as pmem cache";
			goto bad;
		}

		r = persistent_memory_claim(wc);
		if (r) {
			ti->error = "Unable to map persistent memory for cache";
			goto bad;
		}
	} else {
		size_t n_blocks, n_metadata_blocks;
		uint64_t n_bitmap_bits;

		wc->memory_map_size -= (uint64_t)wc->start_sector << SECTOR_SHIFT;

		bio_list_init(&wc->flush_list);
		wc->flush_thread = kthread_create(writecache_flush_thread, wc, "dm_writecache_flush");
		if (IS_ERR(wc->flush_thread)) {
			r = PTR_ERR(wc->flush_thread);
			wc->flush_thread = NULL;
			ti->error = "Couldn't spawn flush thread";
			goto bad;
		}
		wake_up_process(wc->flush_thread);

		r = calculate_memory_size(wc->memory_map_size, wc->block_size,
					  &n_blocks, &n_metadata_blocks);
		if (r) {
			ti->error = "Invalid device size";
			goto bad;
		}

		n_bitmap_bits = (((uint64_t)n_metadata_blocks << wc->block_size_bits) +
				 BITMAP_GRANULARITY - 1) / BITMAP_GRANULARITY;
		/* this is limitation of test_bit functions */
		if (n_bitmap_bits > 1U << 31) {
			r = -EFBIG;
			ti->error = "Invalid device size";
			goto bad;
		}

		wc->memory_map = vmalloc(n_metadata_blocks << wc->block_size_bits);
		if (!wc->memory_map) {
			r = -ENOMEM;
			ti->error = "Unable to allocate memory for metadata";
			goto bad;
		}

		wc->dm_kcopyd = dm_kcopyd_client_create(&dm_kcopyd_throttle);
		if (IS_ERR(wc->dm_kcopyd)) {
			r = PTR_ERR(wc->dm_kcopyd);
			ti->error = "Unable to allocate dm-kcopyd client";
			wc->dm_kcopyd = NULL;
			goto bad;
		}

		wc->metadata_sectors = n_metadata_blocks << (wc->block_size_bits - SECTOR_SHIFT);
		wc->dirty_bitmap_size = (n_bitmap_bits + BITS_PER_LONG - 1) /
			BITS_PER_LONG * sizeof(unsigned long);
		wc->dirty_bitmap = vzalloc(wc->dirty_bitmap_size);
		if (!wc->dirty_bitmap) {
			r = -ENOMEM;
			ti->error = "Unable to allocate dirty bitmap";
			goto bad;
		}

		r = writecache_read_metadata(wc, wc->block_size >> SECTOR_SHIFT);
		if (r) {
			ti->error = "Unable to read first block of metadata";
			goto bad;
		}
	}

	r = copy_mc_to_kernel(&s, sb(wc), sizeof(struct wc_memory_superblock));
	if (r) {
		ti->error = "Hardware memory error when reading superblock";
		goto bad;
	}
	if (!le32_to_cpu(s.magic) && !le32_to_cpu(s.version)) {
		r = init_memory(wc);
		if (r) {
			ti->error = "Unable to initialize device";
			goto bad;
		}
		r = copy_mc_to_kernel(&s, sb(wc),
				      sizeof(struct wc_memory_superblock));
		if (r) {
			ti->error = "Hardware memory error when reading superblock";
			goto bad;
		}
	}

	if (le32_to_cpu(s.magic) != MEMORY_SUPERBLOCK_MAGIC) {
		ti->error = "Invalid magic in the superblock";
		r = -EINVAL;
		goto bad;
	}

	if (le32_to_cpu(s.version) != MEMORY_SUPERBLOCK_VERSION) {
		ti->error = "Invalid version in the superblock";
		r = -EINVAL;
		goto bad;
	}

	if (le32_to_cpu(s.block_size) != wc->block_size) {
		ti->error = "Block size does not match superblock";
		r = -EINVAL;
		goto bad;
	}

	wc->n_blocks = le64_to_cpu(s.n_blocks);

	offset = wc->n_blocks * sizeof(struct wc_memory_entry);
	if (offset / sizeof(struct wc_memory_entry) != le64_to_cpu(sb(wc)->n_blocks)) {
overflow:
		ti->error = "Overflow in size calculation";
		r = -EINVAL;
		goto bad;
	}
	offset += sizeof(struct wc_memory_superblock);
	if (offset < sizeof(struct wc_memory_superblock))
		goto overflow;
	offset = (offset + wc->block_size - 1) & ~(size_t)(wc->block_size - 1);
	data_size = wc->n_blocks * (size_t)wc->block_size;
	if (!offset || (data_size / wc->block_size != wc->n_blocks) ||
	    (offset + data_size < offset))
		goto overflow;
	if (offset + data_size > wc->memory_map_size) {
		ti->error = "Memory area is too small";
		r = -EINVAL;
		goto bad;
	}

	wc->metadata_sectors = offset >> SECTOR_SHIFT;
	wc->block_start = (char *)sb(wc) + offset;

	x = (uint64_t)wc->n_blocks * (100 - high_wm_percent);
	x += 50;
	do_div(x, 100);
	wc->freelist_high_watermark = x;
	x = (uint64_t)wc->n_blocks * (100 - low_wm_percent);
	x += 50;
	do_div(x, 100);
	wc->freelist_low_watermark = x;

	if (wc->cleaner)
		activate_cleaner(wc);

	r = writecache_alloc_entries(wc);
	if (r) {
		ti->error = "Cannot allocate memory";
		goto bad;
	}

	ti->num_flush_bios = 1;
	ti->flush_supported = true;
	ti->num_discard_bios = 1;

	if (WC_MODE_PMEM(wc))
		persistent_memory_flush_cache(wc->memory_map, wc->memory_map_size);

	return 0;

bad_arguments:
	r = -EINVAL;
	ti->error = "Bad arguments";
bad:
	writecache_dtr(ti);
	return r;
}

static void writecache_status(struct dm_target *ti, status_type_t type,
			      unsigned status_flags, char *result, unsigned maxlen)
{
	struct dm_writecache *wc = ti->private;
	unsigned extra_args;
	unsigned sz = 0;

	switch (type) {
	case STATUSTYPE_INFO:
		DMEMIT("%ld %llu %llu %llu", writecache_has_error(wc),
		       (unsigned long long)wc->n_blocks, (unsigned long long)wc->freelist_size,
		       (unsigned long long)wc->writeback_size);
		break;
	case STATUSTYPE_TABLE:
		DMEMIT("%c %s %s %u ", WC_MODE_PMEM(wc) ? 'p' : 's',
				wc->dev->name, wc->ssd_dev->name, wc->block_size);
		extra_args = 0;
		if (wc->start_sector_set)
			extra_args += 2;
		if (wc->high_wm_percent_set)
			extra_args += 2;
		if (wc->low_wm_percent_set)
			extra_args += 2;
		if (wc->max_writeback_jobs_set)
			extra_args += 2;
		if (wc->autocommit_blocks_set)
			extra_args += 2;
		if (wc->autocommit_time_set)
			extra_args += 2;
		if (wc->max_age_set)
			extra_args += 2;
		if (wc->cleaner_set)
			extra_args++;
		if (wc->writeback_fua_set)
			extra_args++;

		DMEMIT("%u", extra_args);
		if (wc->start_sector_set)
			DMEMIT(" start_sector %llu", (unsigned long long)wc->start_sector);
		if (wc->high_wm_percent_set)
			DMEMIT(" high_watermark %u", wc->high_wm_percent_value);
		if (wc->low_wm_percent_set)
			DMEMIT(" low_watermark %u", wc->low_wm_percent_value);
		if (wc->max_writeback_jobs_set)
			DMEMIT(" writeback_jobs %u", wc->max_writeback_jobs);
		if (wc->autocommit_blocks_set)
			DMEMIT(" autocommit_blocks %u", wc->autocommit_blocks);
		if (wc->autocommit_time_set)
			DMEMIT(" autocommit_time %u", wc->autocommit_time_value);
		if (wc->max_age_set)
			DMEMIT(" max_age %u", wc->max_age_value);
		if (wc->cleaner_set)
			DMEMIT(" cleaner");
		if (wc->writeback_fua_set)
			DMEMIT(" %sfua", wc->writeback_fua ? "" : "no");
		break;
	}
}

static struct target_type writecache_target = {
	.name			= "writecache",
	.version		= {1, 4, 0},
	.module			= THIS_MODULE,
	.ctr			= writecache_ctr,
	.dtr			= writecache_dtr,
	.status			= writecache_status,
	.postsuspend		= writecache_suspend,
	.resume			= writecache_resume,
	.message		= writecache_message,
	.map			= writecache_map,
	.end_io			= writecache_end_io,
	.iterate_devices	= writecache_iterate_devices,
	.io_hints		= writecache_io_hints,
};

static int __init dm_writecache_init(void)
{
	int r;

	r = dm_register_target(&writecache_target);
	if (r < 0) {
		DMERR("register failed %d", r);
		return r;
	}

	return 0;
}

static void __exit dm_writecache_exit(void)
{
	dm_unregister_target(&writecache_target);
}

module_init(dm_writecache_init);
module_exit(dm_writecache_exit);

MODULE_DESCRIPTION(DM_NAME " writecache target");
MODULE_AUTHOR("Mikulas Patocka <dm-devel@redhat.com>");
MODULE_LICENSE("GPL");
