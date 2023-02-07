// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2009-2011 Red Hat, Inc.
 *
 * Author: Mikulas Patocka <mpatocka@redhat.com>
 *
 * This file is released under the GPL.
 */

#include <linux/dm-bufio.h>

#include <linux/device-mapper.h>
#include <linux/dm-io.h>
#include <linux/slab.h>
#include <linux/sched/mm.h>
#include <linux/jiffies.h>
#include <linux/vmalloc.h>
#include <linux/shrinker.h>
#include <linux/module.h>
#include <linux/rbtree.h>
#include <linux/stacktrace.h>
#include <linux/jump_label.h>

#define DM_MSG_PREFIX "bufio"

/*
 * Memory management policy:
 *	Limit the number of buffers to DM_BUFIO_MEMORY_PERCENT of main memory
 *	or DM_BUFIO_VMALLOC_PERCENT of vmalloc memory (whichever is lower).
 *	Always allocate at least DM_BUFIO_MIN_BUFFERS buffers.
 *	Start background writeback when there are DM_BUFIO_WRITEBACK_PERCENT
 *	dirty buffers.
 */
#define DM_BUFIO_MIN_BUFFERS		8

#define DM_BUFIO_MEMORY_PERCENT		2
#define DM_BUFIO_VMALLOC_PERCENT	25
#define DM_BUFIO_WRITEBACK_RATIO	3
#define DM_BUFIO_LOW_WATERMARK_RATIO	16

/*
 * Check buffer ages in this interval (seconds)
 */
#define DM_BUFIO_WORK_TIMER_SECS	30

/*
 * Free buffers when they are older than this (seconds)
 */
#define DM_BUFIO_DEFAULT_AGE_SECS	300

/*
 * The nr of bytes of cached data to keep around.
 */
#define DM_BUFIO_DEFAULT_RETAIN_BYTES   (256 * 1024)

/*
 * Align buffer writes to this boundary.
 * Tests show that SSDs have the highest IOPS when using 4k writes.
 */
#define DM_BUFIO_WRITE_ALIGN		4096

/*
 * dm_buffer->list_mode
 */
#define LIST_CLEAN	0
#define LIST_DIRTY	1
#define LIST_SIZE	2

/*
 * Linking of buffers:
 *	All buffers are linked to buffer_tree with their node field.
 *
 *	Clean buffers that are not being written (B_WRITING not set)
 *	are linked to lru[LIST_CLEAN] with their lru_list field.
 *
 *	Dirty and clean buffers that are being written are linked to
 *	lru[LIST_DIRTY] with their lru_list field. When the write
 *	finishes, the buffer cannot be relinked immediately (because we
 *	are in an interrupt context and relinking requires process
 *	context), so some clean-not-writing buffers can be held on
 *	dirty_lru too.  They are later added to lru in the process
 *	context.
 */
struct dm_bufio_client {
	struct mutex lock;
	spinlock_t spinlock;
	bool no_sleep;

	struct list_head lru[LIST_SIZE];
	unsigned long n_buffers[LIST_SIZE];

	struct block_device *bdev;
	unsigned int block_size;
	s8 sectors_per_block_bits;
	void (*alloc_callback)(struct dm_buffer *buf);
	void (*write_callback)(struct dm_buffer *buf);
	struct kmem_cache *slab_buffer;
	struct kmem_cache *slab_cache;
	struct dm_io_client *dm_io;

	struct list_head reserved_buffers;
	unsigned int need_reserved_buffers;

	unsigned int minimum_buffers;

	struct rb_root buffer_tree;
	wait_queue_head_t free_buffer_wait;

	sector_t start;

	int async_write_error;

	struct list_head client_list;

	struct shrinker shrinker;
	struct work_struct shrink_work;
	atomic_long_t need_shrink;
};

/*
 * Buffer state bits.
 */
#define B_READING	0
#define B_WRITING	1
#define B_DIRTY		2

/*
 * Describes how the block was allocated:
 * kmem_cache_alloc(), __get_free_pages() or vmalloc().
 * See the comment at alloc_buffer_data.
 */
enum data_mode {
	DATA_MODE_SLAB = 0,
	DATA_MODE_GET_FREE_PAGES = 1,
	DATA_MODE_VMALLOC = 2,
	DATA_MODE_LIMIT = 3
};

struct dm_buffer {
	struct rb_node node;
	struct list_head lru_list;
	struct list_head global_list;
	sector_t block;
	void *data;
	unsigned char data_mode;		/* DATA_MODE_* */
	unsigned char list_mode;		/* LIST_* */
	blk_status_t read_error;
	blk_status_t write_error;
	unsigned int accessed;
	unsigned int hold_count;
	unsigned long state;
	unsigned long last_accessed;
	unsigned int dirty_start;
	unsigned int dirty_end;
	unsigned int write_start;
	unsigned int write_end;
	struct dm_bufio_client *c;
	struct list_head write_list;
	void (*end_io)(struct dm_buffer *buf, blk_status_t stat);
#ifdef CONFIG_DM_DEBUG_BLOCK_STACK_TRACING
#define MAX_STACK 10
	unsigned int stack_len;
	unsigned long stack_entries[MAX_STACK];
#endif
};

static DEFINE_STATIC_KEY_FALSE(no_sleep_enabled);

/*----------------------------------------------------------------*/

#define dm_bufio_in_request()	(!!current->bio_list)

static void dm_bufio_lock(struct dm_bufio_client *c)
{
	if (static_branch_unlikely(&no_sleep_enabled) && c->no_sleep)
		spin_lock_bh(&c->spinlock);
	else
		mutex_lock_nested(&c->lock, dm_bufio_in_request());
}

static int dm_bufio_trylock(struct dm_bufio_client *c)
{
	if (static_branch_unlikely(&no_sleep_enabled) && c->no_sleep)
		return spin_trylock_bh(&c->spinlock);
	else
		return mutex_trylock(&c->lock);
}

static void dm_bufio_unlock(struct dm_bufio_client *c)
{
	if (static_branch_unlikely(&no_sleep_enabled) && c->no_sleep)
		spin_unlock_bh(&c->spinlock);
	else
		mutex_unlock(&c->lock);
}

/*----------------------------------------------------------------*/

/*
 * Default cache size: available memory divided by the ratio.
 */
static unsigned long dm_bufio_default_cache_size;

/*
 * Total cache size set by the user.
 */
static unsigned long dm_bufio_cache_size;

/*
 * A copy of dm_bufio_cache_size because dm_bufio_cache_size can change
 * at any time.  If it disagrees, the user has changed cache size.
 */
static unsigned long dm_bufio_cache_size_latch;

static DEFINE_SPINLOCK(global_spinlock);

static LIST_HEAD(global_queue);

static unsigned long global_num;

/*
 * Buffers are freed after this timeout
 */
static unsigned int dm_bufio_max_age = DM_BUFIO_DEFAULT_AGE_SECS;
static unsigned long dm_bufio_retain_bytes = DM_BUFIO_DEFAULT_RETAIN_BYTES;

static unsigned long dm_bufio_peak_allocated;
static unsigned long dm_bufio_allocated_kmem_cache;
static unsigned long dm_bufio_allocated_get_free_pages;
static unsigned long dm_bufio_allocated_vmalloc;
static unsigned long dm_bufio_current_allocated;

/*----------------------------------------------------------------*/

/*
 * The current number of clients.
 */
static int dm_bufio_client_count;

/*
 * The list of all clients.
 */
static LIST_HEAD(dm_bufio_all_clients);

/*
 * This mutex protects dm_bufio_cache_size_latch and dm_bufio_client_count
 */
static DEFINE_MUTEX(dm_bufio_clients_lock);

static struct workqueue_struct *dm_bufio_wq;
static struct delayed_work dm_bufio_cleanup_old_work;
static struct work_struct dm_bufio_replacement_work;


#ifdef CONFIG_DM_DEBUG_BLOCK_STACK_TRACING
static void buffer_record_stack(struct dm_buffer *b)
{
	b->stack_len = stack_trace_save(b->stack_entries, MAX_STACK, 2);
}
#endif

/*
 *----------------------------------------------------------------
 * A red/black tree acts as an index for all the buffers.
 *----------------------------------------------------------------
 */
static struct dm_buffer *__find(struct dm_bufio_client *c, sector_t block)
{
	struct rb_node *n = c->buffer_tree.rb_node;
	struct dm_buffer *b;

	while (n) {
		b = container_of(n, struct dm_buffer, node);

		if (b->block == block)
			return b;

		n = block < b->block ? n->rb_left : n->rb_right;
	}

	return NULL;
}

static struct dm_buffer *__find_next(struct dm_bufio_client *c, sector_t block)
{
	struct rb_node *n = c->buffer_tree.rb_node;
	struct dm_buffer *b;
	struct dm_buffer *best = NULL;

	while (n) {
		b = container_of(n, struct dm_buffer, node);

		if (b->block == block)
			return b;

		if (block <= b->block) {
			n = n->rb_left;
			best = b;
		} else {
			n = n->rb_right;
		}
	}

	return best;
}

static void __insert(struct dm_bufio_client *c, struct dm_buffer *b)
{
	struct rb_node **new = &c->buffer_tree.rb_node, *parent = NULL;
	struct dm_buffer *found;

	while (*new) {
		found = container_of(*new, struct dm_buffer, node);

		if (found->block == b->block) {
			BUG_ON(found != b);
			return;
		}

		parent = *new;
		new = b->block < found->block ?
			&found->node.rb_left : &found->node.rb_right;
	}

	rb_link_node(&b->node, parent, new);
	rb_insert_color(&b->node, &c->buffer_tree);
}

static void __remove(struct dm_bufio_client *c, struct dm_buffer *b)
{
	rb_erase(&b->node, &c->buffer_tree);
}

/*----------------------------------------------------------------*/

static void adjust_total_allocated(struct dm_buffer *b, bool unlink)
{
	unsigned char data_mode;
	long diff;

	static unsigned long * const class_ptr[DATA_MODE_LIMIT] = {
		&dm_bufio_allocated_kmem_cache,
		&dm_bufio_allocated_get_free_pages,
		&dm_bufio_allocated_vmalloc,
	};

	data_mode = b->data_mode;
	diff = (long)b->c->block_size;
	if (unlink)
		diff = -diff;

	spin_lock(&global_spinlock);

	*class_ptr[data_mode] += diff;

	dm_bufio_current_allocated += diff;

	if (dm_bufio_current_allocated > dm_bufio_peak_allocated)
		dm_bufio_peak_allocated = dm_bufio_current_allocated;

	b->accessed = 1;

	if (!unlink) {
		list_add(&b->global_list, &global_queue);
		global_num++;
		if (dm_bufio_current_allocated > dm_bufio_cache_size)
			queue_work(dm_bufio_wq, &dm_bufio_replacement_work);
	} else {
		list_del(&b->global_list);
		global_num--;
	}

	spin_unlock(&global_spinlock);
}

/*
 * Change the number of clients and recalculate per-client limit.
 */
static void __cache_size_refresh(void)
{
	BUG_ON(!mutex_is_locked(&dm_bufio_clients_lock));
	BUG_ON(dm_bufio_client_count < 0);

	dm_bufio_cache_size_latch = READ_ONCE(dm_bufio_cache_size);

	/*
	 * Use default if set to 0 and report the actual cache size used.
	 */
	if (!dm_bufio_cache_size_latch) {
		(void)cmpxchg(&dm_bufio_cache_size, 0,
			      dm_bufio_default_cache_size);
		dm_bufio_cache_size_latch = dm_bufio_default_cache_size;
	}
}

/*
 * Allocating buffer data.
 *
 * Small buffers are allocated with kmem_cache, to use space optimally.
 *
 * For large buffers, we choose between get_free_pages and vmalloc.
 * Each has advantages and disadvantages.
 *
 * __get_free_pages can randomly fail if the memory is fragmented.
 * __vmalloc won't randomly fail, but vmalloc space is limited (it may be
 * as low as 128M) so using it for caching is not appropriate.
 *
 * If the allocation may fail we use __get_free_pages. Memory fragmentation
 * won't have a fatal effect here, but it just causes flushes of some other
 * buffers and more I/O will be performed. Don't use __get_free_pages if it
 * always fails (i.e. order >= MAX_ORDER).
 *
 * If the allocation shouldn't fail we use __vmalloc. This is only for the
 * initial reserve allocation, so there's no risk of wasting all vmalloc
 * space.
 */
static void *alloc_buffer_data(struct dm_bufio_client *c, gfp_t gfp_mask,
			       unsigned char *data_mode)
{
	if (unlikely(c->slab_cache != NULL)) {
		*data_mode = DATA_MODE_SLAB;
		return kmem_cache_alloc(c->slab_cache, gfp_mask);
	}

	if (c->block_size <= KMALLOC_MAX_SIZE &&
	    gfp_mask & __GFP_NORETRY) {
		*data_mode = DATA_MODE_GET_FREE_PAGES;
		return (void *)__get_free_pages(gfp_mask,
						c->sectors_per_block_bits - (PAGE_SHIFT - SECTOR_SHIFT));
	}

	*data_mode = DATA_MODE_VMALLOC;

	/*
	 * __vmalloc allocates the data pages and auxiliary structures with
	 * gfp_flags that were specified, but pagetables are always allocated
	 * with GFP_KERNEL, no matter what was specified as gfp_mask.
	 *
	 * Consequently, we must set per-process flag PF_MEMALLOC_NOIO so that
	 * all allocations done by this process (including pagetables) are done
	 * as if GFP_NOIO was specified.
	 */
	if (gfp_mask & __GFP_NORETRY) {
		unsigned int noio_flag = memalloc_noio_save();
		void *ptr = __vmalloc(c->block_size, gfp_mask);

		memalloc_noio_restore(noio_flag);
		return ptr;
	}

	return __vmalloc(c->block_size, gfp_mask);
}

/*
 * Free buffer's data.
 */
static void free_buffer_data(struct dm_bufio_client *c,
			     void *data, unsigned char data_mode)
{
	switch (data_mode) {
	case DATA_MODE_SLAB:
		kmem_cache_free(c->slab_cache, data);
		break;

	case DATA_MODE_GET_FREE_PAGES:
		free_pages((unsigned long)data,
			   c->sectors_per_block_bits - (PAGE_SHIFT - SECTOR_SHIFT));
		break;

	case DATA_MODE_VMALLOC:
		vfree(data);
		break;

	default:
		DMCRIT("dm_bufio_free_buffer_data: bad data mode: %d",
		       data_mode);
		BUG();
	}
}

/*
 * Allocate buffer and its data.
 */
static struct dm_buffer *alloc_buffer(struct dm_bufio_client *c, gfp_t gfp_mask)
{
	struct dm_buffer *b = kmem_cache_alloc(c->slab_buffer, gfp_mask);

	if (!b)
		return NULL;

	b->c = c;

	b->data = alloc_buffer_data(c, gfp_mask, &b->data_mode);
	if (!b->data) {
		kmem_cache_free(c->slab_buffer, b);
		return NULL;
	}

#ifdef CONFIG_DM_DEBUG_BLOCK_STACK_TRACING
	b->stack_len = 0;
#endif
	return b;
}

/*
 * Free buffer and its data.
 */
static void free_buffer(struct dm_buffer *b)
{
	struct dm_bufio_client *c = b->c;

	free_buffer_data(c, b->data, b->data_mode);
	kmem_cache_free(c->slab_buffer, b);
}

/*
 * Link buffer to the buffer tree and clean or dirty queue.
 */
static void __link_buffer(struct dm_buffer *b, sector_t block, int dirty)
{
	struct dm_bufio_client *c = b->c;

	c->n_buffers[dirty]++;
	b->block = block;
	b->list_mode = dirty;
	list_add(&b->lru_list, &c->lru[dirty]);
	__insert(b->c, b);
	b->last_accessed = jiffies;

	adjust_total_allocated(b, false);
}

/*
 * Unlink buffer from the buffer tree and dirty or clean queue.
 */
static void __unlink_buffer(struct dm_buffer *b)
{
	struct dm_bufio_client *c = b->c;

	BUG_ON(!c->n_buffers[b->list_mode]);

	c->n_buffers[b->list_mode]--;
	__remove(b->c, b);
	list_del(&b->lru_list);

	adjust_total_allocated(b, true);
}

/*
 * Place the buffer to the head of dirty or clean LRU queue.
 */
static void __relink_lru(struct dm_buffer *b, int dirty)
{
	struct dm_bufio_client *c = b->c;

	b->accessed = 1;

	BUG_ON(!c->n_buffers[b->list_mode]);

	c->n_buffers[b->list_mode]--;
	c->n_buffers[dirty]++;
	b->list_mode = dirty;
	list_move(&b->lru_list, &c->lru[dirty]);
	b->last_accessed = jiffies;
}

/*
 *--------------------------------------------------------------------------
 * Submit I/O on the buffer.
 *
 * Bio interface is faster but it has some problems:
 *	the vector list is limited (increasing this limit increases
 *	memory-consumption per buffer, so it is not viable);
 *
 *	the memory must be direct-mapped, not vmalloced;
 *
 * If the buffer is small enough (up to DM_BUFIO_INLINE_VECS pages) and
 * it is not vmalloced, try using the bio interface.
 *
 * If the buffer is big, if it is vmalloced or if the underlying device
 * rejects the bio because it is too large, use dm-io layer to do the I/O.
 * The dm-io layer splits the I/O into multiple requests, avoiding the above
 * shortcomings.
 *--------------------------------------------------------------------------
 */

/*
 * dm-io completion routine. It just calls b->bio.bi_end_io, pretending
 * that the request was handled directly with bio interface.
 */
static void dmio_complete(unsigned long error, void *context)
{
	struct dm_buffer *b = context;

	b->end_io(b, unlikely(error != 0) ? BLK_STS_IOERR : 0);
}

static void use_dmio(struct dm_buffer *b, enum req_op op, sector_t sector,
		     unsigned int n_sectors, unsigned int offset)
{
	int r;
	struct dm_io_request io_req = {
		.bi_opf = op,
		.notify.fn = dmio_complete,
		.notify.context = b,
		.client = b->c->dm_io,
	};
	struct dm_io_region region = {
		.bdev = b->c->bdev,
		.sector = sector,
		.count = n_sectors,
	};

	if (b->data_mode != DATA_MODE_VMALLOC) {
		io_req.mem.type = DM_IO_KMEM;
		io_req.mem.ptr.addr = (char *)b->data + offset;
	} else {
		io_req.mem.type = DM_IO_VMA;
		io_req.mem.ptr.vma = (char *)b->data + offset;
	}

	r = dm_io(&io_req, 1, &region, NULL);
	if (unlikely(r))
		b->end_io(b, errno_to_blk_status(r));
}

static void bio_complete(struct bio *bio)
{
	struct dm_buffer *b = bio->bi_private;
	blk_status_t status = bio->bi_status;

	bio_uninit(bio);
	kfree(bio);
	b->end_io(b, status);
}

static void use_bio(struct dm_buffer *b, enum req_op op, sector_t sector,
		    unsigned int n_sectors, unsigned int offset)
{
	struct bio *bio;
	char *ptr;
	unsigned int vec_size, len;

	vec_size = b->c->block_size >> PAGE_SHIFT;
	if (unlikely(b->c->sectors_per_block_bits < PAGE_SHIFT - SECTOR_SHIFT))
		vec_size += 2;

	bio = bio_kmalloc(vec_size, GFP_NOWAIT | __GFP_NORETRY | __GFP_NOWARN);
	if (!bio) {
dmio:
		use_dmio(b, op, sector, n_sectors, offset);
		return;
	}
	bio_init(bio, b->c->bdev, bio->bi_inline_vecs, vec_size, op);
	bio->bi_iter.bi_sector = sector;
	bio->bi_end_io = bio_complete;
	bio->bi_private = b;

	ptr = (char *)b->data + offset;
	len = n_sectors << SECTOR_SHIFT;

	do {
		unsigned int this_step = min((unsigned int)(PAGE_SIZE - offset_in_page(ptr)), len);

		if (!bio_add_page(bio, virt_to_page(ptr), this_step,
				  offset_in_page(ptr))) {
			bio_put(bio);
			goto dmio;
		}

		len -= this_step;
		ptr += this_step;
	} while (len > 0);

	submit_bio(bio);
}

static inline sector_t block_to_sector(struct dm_bufio_client *c, sector_t block)
{
	sector_t sector;

	if (likely(c->sectors_per_block_bits >= 0))
		sector = block << c->sectors_per_block_bits;
	else
		sector = block * (c->block_size >> SECTOR_SHIFT);
	sector += c->start;

	return sector;
}

static void submit_io(struct dm_buffer *b, enum req_op op,
		      void (*end_io)(struct dm_buffer *, blk_status_t))
{
	unsigned int n_sectors;
	sector_t sector;
	unsigned int offset, end;

	b->end_io = end_io;

	sector = block_to_sector(b->c, b->block);

	if (op != REQ_OP_WRITE) {
		n_sectors = b->c->block_size >> SECTOR_SHIFT;
		offset = 0;
	} else {
		if (b->c->write_callback)
			b->c->write_callback(b);
		offset = b->write_start;
		end = b->write_end;
		offset &= -DM_BUFIO_WRITE_ALIGN;
		end += DM_BUFIO_WRITE_ALIGN - 1;
		end &= -DM_BUFIO_WRITE_ALIGN;
		if (unlikely(end > b->c->block_size))
			end = b->c->block_size;

		sector += offset >> SECTOR_SHIFT;
		n_sectors = (end - offset) >> SECTOR_SHIFT;
	}

	if (b->data_mode != DATA_MODE_VMALLOC)
		use_bio(b, op, sector, n_sectors, offset);
	else
		use_dmio(b, op, sector, n_sectors, offset);
}

/*
 *--------------------------------------------------------------
 * Writing dirty buffers
 *--------------------------------------------------------------
 */

/*
 * The endio routine for write.
 *
 * Set the error, clear B_WRITING bit and wake anyone who was waiting on
 * it.
 */
static void write_endio(struct dm_buffer *b, blk_status_t status)
{
	b->write_error = status;
	if (unlikely(status)) {
		struct dm_bufio_client *c = b->c;

		(void)cmpxchg(&c->async_write_error, 0,
				blk_status_to_errno(status));
	}

	BUG_ON(!test_bit(B_WRITING, &b->state));

	smp_mb__before_atomic();
	clear_bit(B_WRITING, &b->state);
	smp_mb__after_atomic();

	wake_up_bit(&b->state, B_WRITING);
}

/*
 * Initiate a write on a dirty buffer, but don't wait for it.
 *
 * - If the buffer is not dirty, exit.
 * - If there some previous write going on, wait for it to finish (we can't
 *   have two writes on the same buffer simultaneously).
 * - Submit our write and don't wait on it. We set B_WRITING indicating
 *   that there is a write in progress.
 */
static void __write_dirty_buffer(struct dm_buffer *b,
				 struct list_head *write_list)
{
	if (!test_bit(B_DIRTY, &b->state))
		return;

	clear_bit(B_DIRTY, &b->state);
	wait_on_bit_lock_io(&b->state, B_WRITING, TASK_UNINTERRUPTIBLE);

	b->write_start = b->dirty_start;
	b->write_end = b->dirty_end;

	if (!write_list)
		submit_io(b, REQ_OP_WRITE, write_endio);
	else
		list_add_tail(&b->write_list, write_list);
}

static void __flush_write_list(struct list_head *write_list)
{
	struct blk_plug plug;

	blk_start_plug(&plug);
	while (!list_empty(write_list)) {
		struct dm_buffer *b =
			list_entry(write_list->next, struct dm_buffer, write_list);
		list_del(&b->write_list);
		submit_io(b, REQ_OP_WRITE, write_endio);
		cond_resched();
	}
	blk_finish_plug(&plug);
}

/*
 * Wait until any activity on the buffer finishes.  Possibly write the
 * buffer if it is dirty.  When this function finishes, there is no I/O
 * running on the buffer and the buffer is not dirty.
 */
static void __make_buffer_clean(struct dm_buffer *b)
{
	BUG_ON(b->hold_count);

	/* smp_load_acquire() pairs with read_endio()'s smp_mb__before_atomic() */
	if (!smp_load_acquire(&b->state))	/* fast case */
		return;

	wait_on_bit_io(&b->state, B_READING, TASK_UNINTERRUPTIBLE);
	__write_dirty_buffer(b, NULL);
	wait_on_bit_io(&b->state, B_WRITING, TASK_UNINTERRUPTIBLE);
}

/*
 * Find some buffer that is not held by anybody, clean it, unlink it and
 * return it.
 */
static struct dm_buffer *__get_unclaimed_buffer(struct dm_bufio_client *c)
{
	struct dm_buffer *b;

	list_for_each_entry_reverse(b, &c->lru[LIST_CLEAN], lru_list) {
		BUG_ON(test_bit(B_WRITING, &b->state));
		BUG_ON(test_bit(B_DIRTY, &b->state));

		if (static_branch_unlikely(&no_sleep_enabled) && c->no_sleep &&
		    unlikely(test_bit_acquire(B_READING, &b->state)))
			continue;

		if (!b->hold_count) {
			__make_buffer_clean(b);
			__unlink_buffer(b);
			return b;
		}
		cond_resched();
	}

	if (static_branch_unlikely(&no_sleep_enabled) && c->no_sleep)
		return NULL;

	list_for_each_entry_reverse(b, &c->lru[LIST_DIRTY], lru_list) {
		BUG_ON(test_bit(B_READING, &b->state));

		if (!b->hold_count) {
			__make_buffer_clean(b);
			__unlink_buffer(b);
			return b;
		}
		cond_resched();
	}

	return NULL;
}

/*
 * Wait until some other threads free some buffer or release hold count on
 * some buffer.
 *
 * This function is entered with c->lock held, drops it and regains it
 * before exiting.
 */
static void __wait_for_free_buffer(struct dm_bufio_client *c)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&c->free_buffer_wait, &wait);
	set_current_state(TASK_UNINTERRUPTIBLE);
	dm_bufio_unlock(c);

	io_schedule();

	remove_wait_queue(&c->free_buffer_wait, &wait);

	dm_bufio_lock(c);
}

enum new_flag {
	NF_FRESH = 0,
	NF_READ = 1,
	NF_GET = 2,
	NF_PREFETCH = 3
};

/*
 * Allocate a new buffer. If the allocation is not possible, wait until
 * some other thread frees a buffer.
 *
 * May drop the lock and regain it.
 */
static struct dm_buffer *__alloc_buffer_wait_no_callback(struct dm_bufio_client *c, enum new_flag nf)
{
	struct dm_buffer *b;
	bool tried_noio_alloc = false;

	/*
	 * dm-bufio is resistant to allocation failures (it just keeps
	 * one buffer reserved in cases all the allocations fail).
	 * So set flags to not try too hard:
	 *	GFP_NOWAIT: don't wait; if we need to sleep we'll release our
	 *		    mutex and wait ourselves.
	 *	__GFP_NORETRY: don't retry and rather return failure
	 *	__GFP_NOMEMALLOC: don't use emergency reserves
	 *	__GFP_NOWARN: don't print a warning in case of failure
	 *
	 * For debugging, if we set the cache size to 1, no new buffers will
	 * be allocated.
	 */
	while (1) {
		if (dm_bufio_cache_size_latch != 1) {
			b = alloc_buffer(c, GFP_NOWAIT | __GFP_NORETRY | __GFP_NOMEMALLOC | __GFP_NOWARN);
			if (b)
				return b;
		}

		if (nf == NF_PREFETCH)
			return NULL;

		if (dm_bufio_cache_size_latch != 1 && !tried_noio_alloc) {
			dm_bufio_unlock(c);
			b = alloc_buffer(c, GFP_NOIO | __GFP_NORETRY | __GFP_NOMEMALLOC | __GFP_NOWARN);
			dm_bufio_lock(c);
			if (b)
				return b;
			tried_noio_alloc = true;
		}

		if (!list_empty(&c->reserved_buffers)) {
			b = list_entry(c->reserved_buffers.next,
				       struct dm_buffer, lru_list);
			list_del(&b->lru_list);
			c->need_reserved_buffers++;

			return b;
		}

		b = __get_unclaimed_buffer(c);
		if (b)
			return b;

		__wait_for_free_buffer(c);
	}
}

static struct dm_buffer *__alloc_buffer_wait(struct dm_bufio_client *c, enum new_flag nf)
{
	struct dm_buffer *b = __alloc_buffer_wait_no_callback(c, nf);

	if (!b)
		return NULL;

	if (c->alloc_callback)
		c->alloc_callback(b);

	return b;
}

/*
 * Free a buffer and wake other threads waiting for free buffers.
 */
static void __free_buffer_wake(struct dm_buffer *b)
{
	struct dm_bufio_client *c = b->c;

	if (!c->need_reserved_buffers)
		free_buffer(b);
	else {
		list_add(&b->lru_list, &c->reserved_buffers);
		c->need_reserved_buffers--;
	}

	wake_up(&c->free_buffer_wait);
}

static void __write_dirty_buffers_async(struct dm_bufio_client *c, int no_wait,
					struct list_head *write_list)
{
	struct dm_buffer *b, *tmp;

	list_for_each_entry_safe_reverse(b, tmp, &c->lru[LIST_DIRTY], lru_list) {
		BUG_ON(test_bit(B_READING, &b->state));

		if (!test_bit(B_DIRTY, &b->state) &&
		    !test_bit(B_WRITING, &b->state)) {
			__relink_lru(b, LIST_CLEAN);
			continue;
		}

		if (no_wait && test_bit(B_WRITING, &b->state))
			return;

		__write_dirty_buffer(b, write_list);
		cond_resched();
	}
}

/*
 * Check if we're over watermark.
 * If we are over threshold_buffers, start freeing buffers.
 * If we're over "limit_buffers", block until we get under the limit.
 */
static void __check_watermark(struct dm_bufio_client *c,
			      struct list_head *write_list)
{
	if (c->n_buffers[LIST_DIRTY] > c->n_buffers[LIST_CLEAN] * DM_BUFIO_WRITEBACK_RATIO)
		__write_dirty_buffers_async(c, 1, write_list);
}

/*
 *--------------------------------------------------------------
 * Getting a buffer
 *--------------------------------------------------------------
 */

static struct dm_buffer *__bufio_new(struct dm_bufio_client *c, sector_t block,
				     enum new_flag nf, int *need_submit,
				     struct list_head *write_list)
{
	struct dm_buffer *b, *new_b = NULL;

	*need_submit = 0;

	b = __find(c, block);
	if (b)
		goto found_buffer;

	if (nf == NF_GET)
		return NULL;

	new_b = __alloc_buffer_wait(c, nf);
	if (!new_b)
		return NULL;

	/*
	 * We've had a period where the mutex was unlocked, so need to
	 * recheck the buffer tree.
	 */
	b = __find(c, block);
	if (b) {
		__free_buffer_wake(new_b);
		goto found_buffer;
	}

	__check_watermark(c, write_list);

	b = new_b;
	b->hold_count = 1;
	b->read_error = 0;
	b->write_error = 0;
	__link_buffer(b, block, LIST_CLEAN);

	if (nf == NF_FRESH) {
		b->state = 0;
		return b;
	}

	b->state = 1 << B_READING;
	*need_submit = 1;

	return b;

found_buffer:
	if (nf == NF_PREFETCH)
		return NULL;
	/*
	 * Note: it is essential that we don't wait for the buffer to be
	 * read if dm_bufio_get function is used. Both dm_bufio_get and
	 * dm_bufio_prefetch can be used in the driver request routine.
	 * If the user called both dm_bufio_prefetch and dm_bufio_get on
	 * the same buffer, it would deadlock if we waited.
	 */
	if (nf == NF_GET && unlikely(test_bit_acquire(B_READING, &b->state)))
		return NULL;

	b->hold_count++;
	__relink_lru(b, test_bit(B_DIRTY, &b->state) ||
		     test_bit(B_WRITING, &b->state));
	return b;
}

/*
 * The endio routine for reading: set the error, clear the bit and wake up
 * anyone waiting on the buffer.
 */
static void read_endio(struct dm_buffer *b, blk_status_t status)
{
	b->read_error = status;

	BUG_ON(!test_bit(B_READING, &b->state));

	smp_mb__before_atomic();
	clear_bit(B_READING, &b->state);
	smp_mb__after_atomic();

	wake_up_bit(&b->state, B_READING);
}

/*
 * A common routine for dm_bufio_new and dm_bufio_read.  Operation of these
 * functions is similar except that dm_bufio_new doesn't read the
 * buffer from the disk (assuming that the caller overwrites all the data
 * and uses dm_bufio_mark_buffer_dirty to write new data back).
 */
static void *new_read(struct dm_bufio_client *c, sector_t block,
		      enum new_flag nf, struct dm_buffer **bp)
{
	int need_submit;
	struct dm_buffer *b;

	LIST_HEAD(write_list);

	dm_bufio_lock(c);
	b = __bufio_new(c, block, nf, &need_submit, &write_list);
#ifdef CONFIG_DM_DEBUG_BLOCK_STACK_TRACING
	if (b && b->hold_count == 1)
		buffer_record_stack(b);
#endif
	dm_bufio_unlock(c);

	__flush_write_list(&write_list);

	if (!b)
		return NULL;

	if (need_submit)
		submit_io(b, REQ_OP_READ, read_endio);

	wait_on_bit_io(&b->state, B_READING, TASK_UNINTERRUPTIBLE);

	if (b->read_error) {
		int error = blk_status_to_errno(b->read_error);

		dm_bufio_release(b);

		return ERR_PTR(error);
	}

	*bp = b;

	return b->data;
}

void *dm_bufio_get(struct dm_bufio_client *c, sector_t block,
		   struct dm_buffer **bp)
{
	return new_read(c, block, NF_GET, bp);
}
EXPORT_SYMBOL_GPL(dm_bufio_get);

void *dm_bufio_read(struct dm_bufio_client *c, sector_t block,
		    struct dm_buffer **bp)
{
	BUG_ON(dm_bufio_in_request());

	return new_read(c, block, NF_READ, bp);
}
EXPORT_SYMBOL_GPL(dm_bufio_read);

void *dm_bufio_new(struct dm_bufio_client *c, sector_t block,
		   struct dm_buffer **bp)
{
	BUG_ON(dm_bufio_in_request());

	return new_read(c, block, NF_FRESH, bp);
}
EXPORT_SYMBOL_GPL(dm_bufio_new);

void dm_bufio_prefetch(struct dm_bufio_client *c,
		       sector_t block, unsigned int n_blocks)
{
	struct blk_plug plug;

	LIST_HEAD(write_list);

	BUG_ON(dm_bufio_in_request());

	blk_start_plug(&plug);
	dm_bufio_lock(c);

	for (; n_blocks--; block++) {
		int need_submit;
		struct dm_buffer *b;

		b = __bufio_new(c, block, NF_PREFETCH, &need_submit,
				&write_list);
		if (unlikely(!list_empty(&write_list))) {
			dm_bufio_unlock(c);
			blk_finish_plug(&plug);
			__flush_write_list(&write_list);
			blk_start_plug(&plug);
			dm_bufio_lock(c);
		}
		if (unlikely(b != NULL)) {
			dm_bufio_unlock(c);

			if (need_submit)
				submit_io(b, REQ_OP_READ, read_endio);
			dm_bufio_release(b);

			cond_resched();

			if (!n_blocks)
				goto flush_plug;
			dm_bufio_lock(c);
		}
	}

	dm_bufio_unlock(c);

flush_plug:
	blk_finish_plug(&plug);
}
EXPORT_SYMBOL_GPL(dm_bufio_prefetch);

void dm_bufio_release(struct dm_buffer *b)
{
	struct dm_bufio_client *c = b->c;

	dm_bufio_lock(c);

	BUG_ON(!b->hold_count);

	b->hold_count--;
	if (!b->hold_count) {
		wake_up(&c->free_buffer_wait);

		/*
		 * If there were errors on the buffer, and the buffer is not
		 * to be written, free the buffer. There is no point in caching
		 * invalid buffer.
		 */
		if ((b->read_error || b->write_error) &&
		    !test_bit_acquire(B_READING, &b->state) &&
		    !test_bit(B_WRITING, &b->state) &&
		    !test_bit(B_DIRTY, &b->state)) {
			__unlink_buffer(b);
			__free_buffer_wake(b);
		}
	}

	dm_bufio_unlock(c);
}
EXPORT_SYMBOL_GPL(dm_bufio_release);

void dm_bufio_mark_partial_buffer_dirty(struct dm_buffer *b,
					unsigned int start, unsigned int end)
{
	struct dm_bufio_client *c = b->c;

	BUG_ON(start >= end);
	BUG_ON(end > b->c->block_size);

	dm_bufio_lock(c);

	BUG_ON(test_bit(B_READING, &b->state));

	if (!test_and_set_bit(B_DIRTY, &b->state)) {
		b->dirty_start = start;
		b->dirty_end = end;
		__relink_lru(b, LIST_DIRTY);
	} else {
		if (start < b->dirty_start)
			b->dirty_start = start;
		if (end > b->dirty_end)
			b->dirty_end = end;
	}

	dm_bufio_unlock(c);
}
EXPORT_SYMBOL_GPL(dm_bufio_mark_partial_buffer_dirty);

void dm_bufio_mark_buffer_dirty(struct dm_buffer *b)
{
	dm_bufio_mark_partial_buffer_dirty(b, 0, b->c->block_size);
}
EXPORT_SYMBOL_GPL(dm_bufio_mark_buffer_dirty);

void dm_bufio_write_dirty_buffers_async(struct dm_bufio_client *c)
{
	LIST_HEAD(write_list);

	BUG_ON(dm_bufio_in_request());

	dm_bufio_lock(c);
	__write_dirty_buffers_async(c, 0, &write_list);
	dm_bufio_unlock(c);
	__flush_write_list(&write_list);
}
EXPORT_SYMBOL_GPL(dm_bufio_write_dirty_buffers_async);

/*
 * For performance, it is essential that the buffers are written asynchronously
 * and simultaneously (so that the block layer can merge the writes) and then
 * waited upon.
 *
 * Finally, we flush hardware disk cache.
 */
int dm_bufio_write_dirty_buffers(struct dm_bufio_client *c)
{
	int a, f;
	unsigned long buffers_processed = 0;
	struct dm_buffer *b, *tmp;

	LIST_HEAD(write_list);

	dm_bufio_lock(c);
	__write_dirty_buffers_async(c, 0, &write_list);
	dm_bufio_unlock(c);
	__flush_write_list(&write_list);
	dm_bufio_lock(c);

again:
	list_for_each_entry_safe_reverse(b, tmp, &c->lru[LIST_DIRTY], lru_list) {
		int dropped_lock = 0;

		if (buffers_processed < c->n_buffers[LIST_DIRTY])
			buffers_processed++;

		BUG_ON(test_bit(B_READING, &b->state));

		if (test_bit(B_WRITING, &b->state)) {
			if (buffers_processed < c->n_buffers[LIST_DIRTY]) {
				dropped_lock = 1;
				b->hold_count++;
				dm_bufio_unlock(c);
				wait_on_bit_io(&b->state, B_WRITING,
					       TASK_UNINTERRUPTIBLE);
				dm_bufio_lock(c);
				b->hold_count--;
			} else
				wait_on_bit_io(&b->state, B_WRITING,
					       TASK_UNINTERRUPTIBLE);
		}

		if (!test_bit(B_DIRTY, &b->state) &&
		    !test_bit(B_WRITING, &b->state))
			__relink_lru(b, LIST_CLEAN);

		cond_resched();

		/*
		 * If we dropped the lock, the list is no longer consistent,
		 * so we must restart the search.
		 *
		 * In the most common case, the buffer just processed is
		 * relinked to the clean list, so we won't loop scanning the
		 * same buffer again and again.
		 *
		 * This may livelock if there is another thread simultaneously
		 * dirtying buffers, so we count the number of buffers walked
		 * and if it exceeds the total number of buffers, it means that
		 * someone is doing some writes simultaneously with us.  In
		 * this case, stop, dropping the lock.
		 */
		if (dropped_lock)
			goto again;
	}
	wake_up(&c->free_buffer_wait);
	dm_bufio_unlock(c);

	a = xchg(&c->async_write_error, 0);
	f = dm_bufio_issue_flush(c);
	if (a)
		return a;

	return f;
}
EXPORT_SYMBOL_GPL(dm_bufio_write_dirty_buffers);

/*
 * Use dm-io to send an empty barrier to flush the device.
 */
int dm_bufio_issue_flush(struct dm_bufio_client *c)
{
	struct dm_io_request io_req = {
		.bi_opf = REQ_OP_WRITE | REQ_PREFLUSH | REQ_SYNC,
		.mem.type = DM_IO_KMEM,
		.mem.ptr.addr = NULL,
		.client = c->dm_io,
	};
	struct dm_io_region io_reg = {
		.bdev = c->bdev,
		.sector = 0,
		.count = 0,
	};

	BUG_ON(dm_bufio_in_request());

	return dm_io(&io_req, 1, &io_reg, NULL);
}
EXPORT_SYMBOL_GPL(dm_bufio_issue_flush);

/*
 * Use dm-io to send a discard request to flush the device.
 */
int dm_bufio_issue_discard(struct dm_bufio_client *c, sector_t block, sector_t count)
{
	struct dm_io_request io_req = {
		.bi_opf = REQ_OP_DISCARD | REQ_SYNC,
		.mem.type = DM_IO_KMEM,
		.mem.ptr.addr = NULL,
		.client = c->dm_io,
	};
	struct dm_io_region io_reg = {
		.bdev = c->bdev,
		.sector = block_to_sector(c, block),
		.count = block_to_sector(c, count),
	};

	BUG_ON(dm_bufio_in_request());

	return dm_io(&io_req, 1, &io_reg, NULL);
}
EXPORT_SYMBOL_GPL(dm_bufio_issue_discard);

/*
 * We first delete any other buffer that may be at that new location.
 *
 * Then, we write the buffer to the original location if it was dirty.
 *
 * Then, if we are the only one who is holding the buffer, relink the buffer
 * in the buffer tree for the new location.
 *
 * If there was someone else holding the buffer, we write it to the new
 * location but not relink it, because that other user needs to have the buffer
 * at the same place.
 */
void dm_bufio_release_move(struct dm_buffer *b, sector_t new_block)
{
	struct dm_bufio_client *c = b->c;
	struct dm_buffer *new;

	BUG_ON(dm_bufio_in_request());

	dm_bufio_lock(c);

retry:
	new = __find(c, new_block);
	if (new) {
		if (new->hold_count) {
			__wait_for_free_buffer(c);
			goto retry;
		}

		/*
		 * FIXME: Is there any point waiting for a write that's going
		 * to be overwritten in a bit?
		 */
		__make_buffer_clean(new);
		__unlink_buffer(new);
		__free_buffer_wake(new);
	}

	BUG_ON(!b->hold_count);
	BUG_ON(test_bit(B_READING, &b->state));

	__write_dirty_buffer(b, NULL);
	if (b->hold_count == 1) {
		wait_on_bit_io(&b->state, B_WRITING,
			       TASK_UNINTERRUPTIBLE);
		set_bit(B_DIRTY, &b->state);
		b->dirty_start = 0;
		b->dirty_end = c->block_size;
		__unlink_buffer(b);
		__link_buffer(b, new_block, LIST_DIRTY);
	} else {
		sector_t old_block;

		wait_on_bit_lock_io(&b->state, B_WRITING,
				    TASK_UNINTERRUPTIBLE);
		/*
		 * Relink buffer to "new_block" so that write_callback
		 * sees "new_block" as a block number.
		 * After the write, link the buffer back to old_block.
		 * All this must be done in bufio lock, so that block number
		 * change isn't visible to other threads.
		 */
		old_block = b->block;
		__unlink_buffer(b);
		__link_buffer(b, new_block, b->list_mode);
		submit_io(b, REQ_OP_WRITE, write_endio);
		wait_on_bit_io(&b->state, B_WRITING,
			       TASK_UNINTERRUPTIBLE);
		__unlink_buffer(b);
		__link_buffer(b, old_block, b->list_mode);
	}

	dm_bufio_unlock(c);
	dm_bufio_release(b);
}
EXPORT_SYMBOL_GPL(dm_bufio_release_move);

static void forget_buffer_locked(struct dm_buffer *b)
{
	if (likely(!b->hold_count) && likely(!smp_load_acquire(&b->state))) {
		__unlink_buffer(b);
		__free_buffer_wake(b);
	}
}

/*
 * Free the given buffer.
 *
 * This is just a hint, if the buffer is in use or dirty, this function
 * does nothing.
 */
void dm_bufio_forget(struct dm_bufio_client *c, sector_t block)
{
	struct dm_buffer *b;

	dm_bufio_lock(c);

	b = __find(c, block);
	if (b)
		forget_buffer_locked(b);

	dm_bufio_unlock(c);
}
EXPORT_SYMBOL_GPL(dm_bufio_forget);

void dm_bufio_forget_buffers(struct dm_bufio_client *c, sector_t block, sector_t n_blocks)
{
	struct dm_buffer *b;
	sector_t end_block = block + n_blocks;

	while (block < end_block) {
		dm_bufio_lock(c);

		b = __find_next(c, block);
		if (b) {
			block = b->block + 1;
			forget_buffer_locked(b);
		}

		dm_bufio_unlock(c);

		if (!b)
			break;
	}

}
EXPORT_SYMBOL_GPL(dm_bufio_forget_buffers);

void dm_bufio_set_minimum_buffers(struct dm_bufio_client *c, unsigned int n)
{
	c->minimum_buffers = n;
}
EXPORT_SYMBOL_GPL(dm_bufio_set_minimum_buffers);

unsigned int dm_bufio_get_block_size(struct dm_bufio_client *c)
{
	return c->block_size;
}
EXPORT_SYMBOL_GPL(dm_bufio_get_block_size);

sector_t dm_bufio_get_device_size(struct dm_bufio_client *c)
{
	sector_t s = bdev_nr_sectors(c->bdev);

	if (s >= c->start)
		s -= c->start;
	else
		s = 0;
	if (likely(c->sectors_per_block_bits >= 0))
		s >>= c->sectors_per_block_bits;
	else
		sector_div(s, c->block_size >> SECTOR_SHIFT);
	return s;
}
EXPORT_SYMBOL_GPL(dm_bufio_get_device_size);

struct dm_io_client *dm_bufio_get_dm_io_client(struct dm_bufio_client *c)
{
	return c->dm_io;
}
EXPORT_SYMBOL_GPL(dm_bufio_get_dm_io_client);

sector_t dm_bufio_get_block_number(struct dm_buffer *b)
{
	return b->block;
}
EXPORT_SYMBOL_GPL(dm_bufio_get_block_number);

void *dm_bufio_get_block_data(struct dm_buffer *b)
{
	return b->data;
}
EXPORT_SYMBOL_GPL(dm_bufio_get_block_data);

void *dm_bufio_get_aux_data(struct dm_buffer *b)
{
	return b + 1;
}
EXPORT_SYMBOL_GPL(dm_bufio_get_aux_data);

struct dm_bufio_client *dm_bufio_get_client(struct dm_buffer *b)
{
	return b->c;
}
EXPORT_SYMBOL_GPL(dm_bufio_get_client);

static void drop_buffers(struct dm_bufio_client *c)
{
	struct dm_buffer *b;
	int i;
	bool warned = false;

	BUG_ON(dm_bufio_in_request());

	/*
	 * An optimization so that the buffers are not written one-by-one.
	 */
	dm_bufio_write_dirty_buffers_async(c);

	dm_bufio_lock(c);

	while ((b = __get_unclaimed_buffer(c)))
		__free_buffer_wake(b);

	for (i = 0; i < LIST_SIZE; i++)
		list_for_each_entry(b, &c->lru[i], lru_list) {
			WARN_ON(!warned);
			warned = true;
			DMERR("leaked buffer %llx, hold count %u, list %d",
			      (unsigned long long)b->block, b->hold_count, i);
#ifdef CONFIG_DM_DEBUG_BLOCK_STACK_TRACING
			stack_trace_print(b->stack_entries, b->stack_len, 1);
			/* mark unclaimed to avoid BUG_ON below */
			b->hold_count = 0;
#endif
		}

#ifdef CONFIG_DM_DEBUG_BLOCK_STACK_TRACING
	while ((b = __get_unclaimed_buffer(c)))
		__free_buffer_wake(b);
#endif

	for (i = 0; i < LIST_SIZE; i++)
		BUG_ON(!list_empty(&c->lru[i]));

	dm_bufio_unlock(c);
}

/*
 * We may not be able to evict this buffer if IO pending or the client
 * is still using it.  Caller is expected to know buffer is too old.
 *
 * And if GFP_NOFS is used, we must not do any I/O because we hold
 * dm_bufio_clients_lock and we would risk deadlock if the I/O gets
 * rerouted to different bufio client.
 */
static bool __try_evict_buffer(struct dm_buffer *b, gfp_t gfp)
{
	if (!(gfp & __GFP_FS) ||
	    (static_branch_unlikely(&no_sleep_enabled) && b->c->no_sleep)) {
		if (test_bit_acquire(B_READING, &b->state) ||
		    test_bit(B_WRITING, &b->state) ||
		    test_bit(B_DIRTY, &b->state))
			return false;
	}

	if (b->hold_count)
		return false;

	__make_buffer_clean(b);
	__unlink_buffer(b);
	__free_buffer_wake(b);

	return true;
}

static unsigned long get_retain_buffers(struct dm_bufio_client *c)
{
	unsigned long retain_bytes = READ_ONCE(dm_bufio_retain_bytes);

	if (likely(c->sectors_per_block_bits >= 0))
		retain_bytes >>= c->sectors_per_block_bits + SECTOR_SHIFT;
	else
		retain_bytes /= c->block_size;

	return retain_bytes;
}

static void __scan(struct dm_bufio_client *c)
{
	int l;
	struct dm_buffer *b, *tmp;
	unsigned long freed = 0;
	unsigned long count = c->n_buffers[LIST_CLEAN] +
			      c->n_buffers[LIST_DIRTY];
	unsigned long retain_target = get_retain_buffers(c);

	for (l = 0; l < LIST_SIZE; l++) {
		list_for_each_entry_safe_reverse(b, tmp, &c->lru[l], lru_list) {
			if (count - freed <= retain_target)
				atomic_long_set(&c->need_shrink, 0);
			if (!atomic_long_read(&c->need_shrink))
				return;
			if (__try_evict_buffer(b, GFP_KERNEL)) {
				atomic_long_dec(&c->need_shrink);
				freed++;
			}
			cond_resched();
		}
	}
}

static void shrink_work(struct work_struct *w)
{
	struct dm_bufio_client *c = container_of(w, struct dm_bufio_client, shrink_work);

	dm_bufio_lock(c);
	__scan(c);
	dm_bufio_unlock(c);
}

static unsigned long dm_bufio_shrink_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	struct dm_bufio_client *c;

	c = container_of(shrink, struct dm_bufio_client, shrinker);
	atomic_long_add(sc->nr_to_scan, &c->need_shrink);
	queue_work(dm_bufio_wq, &c->shrink_work);

	return sc->nr_to_scan;
}

static unsigned long dm_bufio_shrink_count(struct shrinker *shrink, struct shrink_control *sc)
{
	struct dm_bufio_client *c = container_of(shrink, struct dm_bufio_client, shrinker);
	unsigned long count = READ_ONCE(c->n_buffers[LIST_CLEAN]) +
			      READ_ONCE(c->n_buffers[LIST_DIRTY]);
	unsigned long retain_target = get_retain_buffers(c);
	unsigned long queued_for_cleanup = atomic_long_read(&c->need_shrink);

	if (unlikely(count < retain_target))
		count = 0;
	else
		count -= retain_target;

	if (unlikely(count < queued_for_cleanup))
		count = 0;
	else
		count -= queued_for_cleanup;

	return count;
}

/*
 * Create the buffering interface
 */
struct dm_bufio_client *dm_bufio_client_create(struct block_device *bdev, unsigned int block_size,
					       unsigned int reserved_buffers, unsigned int aux_size,
					       void (*alloc_callback)(struct dm_buffer *),
					       void (*write_callback)(struct dm_buffer *),
					       unsigned int flags)
{
	int r;
	struct dm_bufio_client *c;
	unsigned int i;
	char slab_name[27];

	if (!block_size || block_size & ((1 << SECTOR_SHIFT) - 1)) {
		DMERR("%s: block size not specified or is not multiple of 512b", __func__);
		r = -EINVAL;
		goto bad_client;
	}

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c) {
		r = -ENOMEM;
		goto bad_client;
	}
	c->buffer_tree = RB_ROOT;

	c->bdev = bdev;
	c->block_size = block_size;
	if (is_power_of_2(block_size))
		c->sectors_per_block_bits = __ffs(block_size) - SECTOR_SHIFT;
	else
		c->sectors_per_block_bits = -1;

	c->alloc_callback = alloc_callback;
	c->write_callback = write_callback;

	if (flags & DM_BUFIO_CLIENT_NO_SLEEP) {
		c->no_sleep = true;
		static_branch_inc(&no_sleep_enabled);
	}

	for (i = 0; i < LIST_SIZE; i++) {
		INIT_LIST_HEAD(&c->lru[i]);
		c->n_buffers[i] = 0;
	}

	mutex_init(&c->lock);
	spin_lock_init(&c->spinlock);
	INIT_LIST_HEAD(&c->reserved_buffers);
	c->need_reserved_buffers = reserved_buffers;

	dm_bufio_set_minimum_buffers(c, DM_BUFIO_MIN_BUFFERS);

	init_waitqueue_head(&c->free_buffer_wait);
	c->async_write_error = 0;

	c->dm_io = dm_io_client_create();
	if (IS_ERR(c->dm_io)) {
		r = PTR_ERR(c->dm_io);
		goto bad_dm_io;
	}

	if (block_size <= KMALLOC_MAX_SIZE &&
	    (block_size < PAGE_SIZE || !is_power_of_2(block_size))) {
		unsigned int align = min(1U << __ffs(block_size), (unsigned int)PAGE_SIZE);

		snprintf(slab_name, sizeof(slab_name), "dm_bufio_cache-%u", block_size);
		c->slab_cache = kmem_cache_create(slab_name, block_size, align,
						  SLAB_RECLAIM_ACCOUNT, NULL);
		if (!c->slab_cache) {
			r = -ENOMEM;
			goto bad;
		}
	}
	if (aux_size)
		snprintf(slab_name, sizeof(slab_name), "dm_bufio_buffer-%u", aux_size);
	else
		snprintf(slab_name, sizeof(slab_name), "dm_bufio_buffer");
	c->slab_buffer = kmem_cache_create(slab_name, sizeof(struct dm_buffer) + aux_size,
					   0, SLAB_RECLAIM_ACCOUNT, NULL);
	if (!c->slab_buffer) {
		r = -ENOMEM;
		goto bad;
	}

	while (c->need_reserved_buffers) {
		struct dm_buffer *b = alloc_buffer(c, GFP_KERNEL);

		if (!b) {
			r = -ENOMEM;
			goto bad;
		}
		__free_buffer_wake(b);
	}

	INIT_WORK(&c->shrink_work, shrink_work);
	atomic_long_set(&c->need_shrink, 0);

	c->shrinker.count_objects = dm_bufio_shrink_count;
	c->shrinker.scan_objects = dm_bufio_shrink_scan;
	c->shrinker.seeks = 1;
	c->shrinker.batch = 0;
	r = register_shrinker(&c->shrinker, "dm-bufio:(%u:%u)",
			      MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev));
	if (r)
		goto bad;

	mutex_lock(&dm_bufio_clients_lock);
	dm_bufio_client_count++;
	list_add(&c->client_list, &dm_bufio_all_clients);
	__cache_size_refresh();
	mutex_unlock(&dm_bufio_clients_lock);

	return c;

bad:
	while (!list_empty(&c->reserved_buffers)) {
		struct dm_buffer *b = list_entry(c->reserved_buffers.next,
						 struct dm_buffer, lru_list);
		list_del(&b->lru_list);
		free_buffer(b);
	}
	kmem_cache_destroy(c->slab_cache);
	kmem_cache_destroy(c->slab_buffer);
	dm_io_client_destroy(c->dm_io);
bad_dm_io:
	mutex_destroy(&c->lock);
	if (c->no_sleep)
		static_branch_dec(&no_sleep_enabled);
	kfree(c);
bad_client:
	return ERR_PTR(r);
}
EXPORT_SYMBOL_GPL(dm_bufio_client_create);

/*
 * Free the buffering interface.
 * It is required that there are no references on any buffers.
 */
void dm_bufio_client_destroy(struct dm_bufio_client *c)
{
	unsigned int i;

	drop_buffers(c);

	unregister_shrinker(&c->shrinker);
	flush_work(&c->shrink_work);

	mutex_lock(&dm_bufio_clients_lock);

	list_del(&c->client_list);
	dm_bufio_client_count--;
	__cache_size_refresh();

	mutex_unlock(&dm_bufio_clients_lock);

	BUG_ON(!RB_EMPTY_ROOT(&c->buffer_tree));
	BUG_ON(c->need_reserved_buffers);

	while (!list_empty(&c->reserved_buffers)) {
		struct dm_buffer *b = list_entry(c->reserved_buffers.next,
						 struct dm_buffer, lru_list);
		list_del(&b->lru_list);
		free_buffer(b);
	}

	for (i = 0; i < LIST_SIZE; i++)
		if (c->n_buffers[i])
			DMERR("leaked buffer count %d: %ld", i, c->n_buffers[i]);

	for (i = 0; i < LIST_SIZE; i++)
		BUG_ON(c->n_buffers[i]);

	kmem_cache_destroy(c->slab_cache);
	kmem_cache_destroy(c->slab_buffer);
	dm_io_client_destroy(c->dm_io);
	mutex_destroy(&c->lock);
	if (c->no_sleep)
		static_branch_dec(&no_sleep_enabled);
	kfree(c);
}
EXPORT_SYMBOL_GPL(dm_bufio_client_destroy);

void dm_bufio_set_sector_offset(struct dm_bufio_client *c, sector_t start)
{
	c->start = start;
}
EXPORT_SYMBOL_GPL(dm_bufio_set_sector_offset);

static unsigned int get_max_age_hz(void)
{
	unsigned int max_age = READ_ONCE(dm_bufio_max_age);

	if (max_age > UINT_MAX / HZ)
		max_age = UINT_MAX / HZ;

	return max_age * HZ;
}

static bool older_than(struct dm_buffer *b, unsigned long age_hz)
{
	return time_after_eq(jiffies, b->last_accessed + age_hz);
}

static void __evict_old_buffers(struct dm_bufio_client *c, unsigned long age_hz)
{
	struct dm_buffer *b, *tmp;
	unsigned long retain_target = get_retain_buffers(c);
	unsigned long count;
	LIST_HEAD(write_list);

	dm_bufio_lock(c);

	__check_watermark(c, &write_list);
	if (unlikely(!list_empty(&write_list))) {
		dm_bufio_unlock(c);
		__flush_write_list(&write_list);
		dm_bufio_lock(c);
	}

	count = c->n_buffers[LIST_CLEAN] + c->n_buffers[LIST_DIRTY];
	list_for_each_entry_safe_reverse(b, tmp, &c->lru[LIST_CLEAN], lru_list) {
		if (count <= retain_target)
			break;

		if (!older_than(b, age_hz))
			break;

		if (__try_evict_buffer(b, 0))
			count--;

		cond_resched();
	}

	dm_bufio_unlock(c);
}

static void do_global_cleanup(struct work_struct *w)
{
	struct dm_bufio_client *locked_client = NULL;
	struct dm_bufio_client *current_client;
	struct dm_buffer *b;
	unsigned int spinlock_hold_count;
	unsigned long threshold = dm_bufio_cache_size -
		dm_bufio_cache_size / DM_BUFIO_LOW_WATERMARK_RATIO;
	unsigned long loops = global_num * 2;

	mutex_lock(&dm_bufio_clients_lock);

	while (1) {
		cond_resched();

		spin_lock(&global_spinlock);
		if (unlikely(dm_bufio_current_allocated <= threshold))
			break;

		spinlock_hold_count = 0;
get_next:
		if (!loops--)
			break;
		if (unlikely(list_empty(&global_queue)))
			break;
		b = list_entry(global_queue.prev, struct dm_buffer, global_list);

		if (b->accessed) {
			b->accessed = 0;
			list_move(&b->global_list, &global_queue);
			if (likely(++spinlock_hold_count < 16))
				goto get_next;
			spin_unlock(&global_spinlock);
			continue;
		}

		current_client = b->c;
		if (unlikely(current_client != locked_client)) {
			if (locked_client)
				dm_bufio_unlock(locked_client);

			if (!dm_bufio_trylock(current_client)) {
				spin_unlock(&global_spinlock);
				dm_bufio_lock(current_client);
				locked_client = current_client;
				continue;
			}

			locked_client = current_client;
		}

		spin_unlock(&global_spinlock);

		if (unlikely(!__try_evict_buffer(b, GFP_KERNEL))) {
			spin_lock(&global_spinlock);
			list_move(&b->global_list, &global_queue);
			spin_unlock(&global_spinlock);
		}
	}

	spin_unlock(&global_spinlock);

	if (locked_client)
		dm_bufio_unlock(locked_client);

	mutex_unlock(&dm_bufio_clients_lock);
}

static void cleanup_old_buffers(void)
{
	unsigned long max_age_hz = get_max_age_hz();
	struct dm_bufio_client *c;

	mutex_lock(&dm_bufio_clients_lock);

	__cache_size_refresh();

	list_for_each_entry(c, &dm_bufio_all_clients, client_list)
		__evict_old_buffers(c, max_age_hz);

	mutex_unlock(&dm_bufio_clients_lock);
}

static void work_fn(struct work_struct *w)
{
	cleanup_old_buffers();

	queue_delayed_work(dm_bufio_wq, &dm_bufio_cleanup_old_work,
			   DM_BUFIO_WORK_TIMER_SECS * HZ);
}

/*
 *--------------------------------------------------------------
 * Module setup
 *--------------------------------------------------------------
 */

/*
 * This is called only once for the whole dm_bufio module.
 * It initializes memory limit.
 */
static int __init dm_bufio_init(void)
{
	__u64 mem;

	dm_bufio_allocated_kmem_cache = 0;
	dm_bufio_allocated_get_free_pages = 0;
	dm_bufio_allocated_vmalloc = 0;
	dm_bufio_current_allocated = 0;

	mem = (__u64)mult_frac(totalram_pages() - totalhigh_pages(),
			       DM_BUFIO_MEMORY_PERCENT, 100) << PAGE_SHIFT;

	if (mem > ULONG_MAX)
		mem = ULONG_MAX;

#ifdef CONFIG_MMU
	if (mem > mult_frac(VMALLOC_TOTAL, DM_BUFIO_VMALLOC_PERCENT, 100))
		mem = mult_frac(VMALLOC_TOTAL, DM_BUFIO_VMALLOC_PERCENT, 100);
#endif

	dm_bufio_default_cache_size = mem;

	mutex_lock(&dm_bufio_clients_lock);
	__cache_size_refresh();
	mutex_unlock(&dm_bufio_clients_lock);

	dm_bufio_wq = alloc_workqueue("dm_bufio_cache", WQ_MEM_RECLAIM, 0);
	if (!dm_bufio_wq)
		return -ENOMEM;

	INIT_DELAYED_WORK(&dm_bufio_cleanup_old_work, work_fn);
	INIT_WORK(&dm_bufio_replacement_work, do_global_cleanup);
	queue_delayed_work(dm_bufio_wq, &dm_bufio_cleanup_old_work,
			   DM_BUFIO_WORK_TIMER_SECS * HZ);

	return 0;
}

/*
 * This is called once when unloading the dm_bufio module.
 */
static void __exit dm_bufio_exit(void)
{
	int bug = 0;

	cancel_delayed_work_sync(&dm_bufio_cleanup_old_work);
	destroy_workqueue(dm_bufio_wq);

	if (dm_bufio_client_count) {
		DMCRIT("%s: dm_bufio_client_count leaked: %d",
			__func__, dm_bufio_client_count);
		bug = 1;
	}

	if (dm_bufio_current_allocated) {
		DMCRIT("%s: dm_bufio_current_allocated leaked: %lu",
			__func__, dm_bufio_current_allocated);
		bug = 1;
	}

	if (dm_bufio_allocated_get_free_pages) {
		DMCRIT("%s: dm_bufio_allocated_get_free_pages leaked: %lu",
		       __func__, dm_bufio_allocated_get_free_pages);
		bug = 1;
	}

	if (dm_bufio_allocated_vmalloc) {
		DMCRIT("%s: dm_bufio_vmalloc leaked: %lu",
		       __func__, dm_bufio_allocated_vmalloc);
		bug = 1;
	}

	BUG_ON(bug);
}

module_init(dm_bufio_init)
module_exit(dm_bufio_exit)

module_param_named(max_cache_size_bytes, dm_bufio_cache_size, ulong, 0644);
MODULE_PARM_DESC(max_cache_size_bytes, "Size of metadata cache");

module_param_named(max_age_seconds, dm_bufio_max_age, uint, 0644);
MODULE_PARM_DESC(max_age_seconds, "Max age of a buffer in seconds");

module_param_named(retain_bytes, dm_bufio_retain_bytes, ulong, 0644);
MODULE_PARM_DESC(retain_bytes, "Try to keep at least this many bytes cached in memory");

module_param_named(peak_allocated_bytes, dm_bufio_peak_allocated, ulong, 0644);
MODULE_PARM_DESC(peak_allocated_bytes, "Tracks the maximum allocated memory");

module_param_named(allocated_kmem_cache_bytes, dm_bufio_allocated_kmem_cache, ulong, 0444);
MODULE_PARM_DESC(allocated_kmem_cache_bytes, "Memory allocated with kmem_cache_alloc");

module_param_named(allocated_get_free_pages_bytes, dm_bufio_allocated_get_free_pages, ulong, 0444);
MODULE_PARM_DESC(allocated_get_free_pages_bytes, "Memory allocated with get_free_pages");

module_param_named(allocated_vmalloc_bytes, dm_bufio_allocated_vmalloc, ulong, 0444);
MODULE_PARM_DESC(allocated_vmalloc_bytes, "Memory allocated with vmalloc");

module_param_named(current_allocated_bytes, dm_bufio_current_allocated, ulong, 0444);
MODULE_PARM_DESC(current_allocated_bytes, "Memory currently used by the cache");

MODULE_AUTHOR("Mikulas Patocka <dm-devel@redhat.com>");
MODULE_DESCRIPTION(DM_NAME " buffered I/O library");
MODULE_LICENSE("GPL");
