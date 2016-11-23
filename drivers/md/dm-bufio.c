/*
 * Copyright (C) 2009-2011 Red Hat, Inc.
 *
 * Author: Mikulas Patocka <mpatocka@redhat.com>
 *
 * This file is released under the GPL.
 */

#include "dm-bufio.h"

#include <linux/device-mapper.h>
#include <linux/dm-io.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/vmalloc.h>
#include <linux/shrinker.h>
#include <linux/module.h>
#include <linux/rbtree.h>
#include <linux/stacktrace.h>

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
#define DM_BUFIO_WRITEBACK_PERCENT	75

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
 * The number of bvec entries that are embedded directly in the buffer.
 * If the chunk size is larger, dm-io is used to do the io.
 */
#define DM_BUFIO_INLINE_VECS		16

/*
 * Don't try to use kmem_cache_alloc for blocks larger than this.
 * For explanation, see alloc_buffer_data below.
 */
#define DM_BUFIO_BLOCK_SIZE_SLAB_LIMIT	(PAGE_SIZE >> 1)
#define DM_BUFIO_BLOCK_SIZE_GFP_LIMIT	(PAGE_SIZE << (MAX_ORDER - 1))

/*
 * dm_buffer->list_mode
 */
#define LIST_CLEAN	0
#define LIST_DIRTY	1
#define LIST_SIZE	2

/*
 * Linking of buffers:
 *	All buffers are linked to cache_hash with their hash_list field.
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

	struct list_head lru[LIST_SIZE];
	unsigned long n_buffers[LIST_SIZE];

	struct block_device *bdev;
	unsigned block_size;
	unsigned char sectors_per_block_bits;
	unsigned char pages_per_block_bits;
	unsigned char blocks_per_page_bits;
	unsigned aux_size;
	void (*alloc_callback)(struct dm_buffer *);
	void (*write_callback)(struct dm_buffer *);

	struct dm_io_client *dm_io;

	struct list_head reserved_buffers;
	unsigned need_reserved_buffers;

	unsigned minimum_buffers;

	struct rb_root buffer_tree;
	wait_queue_head_t free_buffer_wait;

	int async_write_error;

	struct list_head client_list;
	struct shrinker shrinker;
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
	sector_t block;
	void *data;
	enum data_mode data_mode;
	unsigned char list_mode;		/* LIST_* */
	unsigned hold_count;
	int read_error;
	int write_error;
	unsigned long state;
	unsigned long last_accessed;
	struct dm_bufio_client *c;
	struct list_head write_list;
	struct bio bio;
	struct bio_vec bio_vec[DM_BUFIO_INLINE_VECS];
#ifdef CONFIG_DM_DEBUG_BLOCK_STACK_TRACING
#define MAX_STACK 10
	struct stack_trace stack_trace;
	unsigned long stack_entries[MAX_STACK];
#endif
};

/*----------------------------------------------------------------*/

static struct kmem_cache *dm_bufio_caches[PAGE_SHIFT - SECTOR_SHIFT];
static char *dm_bufio_cache_names[PAGE_SHIFT - SECTOR_SHIFT];

static inline int dm_bufio_cache_index(struct dm_bufio_client *c)
{
	unsigned ret = c->blocks_per_page_bits - 1;

	BUG_ON(ret >= ARRAY_SIZE(dm_bufio_caches));

	return ret;
}

#define DM_BUFIO_CACHE(c)	(dm_bufio_caches[dm_bufio_cache_index(c)])
#define DM_BUFIO_CACHE_NAME(c)	(dm_bufio_cache_names[dm_bufio_cache_index(c)])

#define dm_bufio_in_request()	(!!current->bio_list)

static void dm_bufio_lock(struct dm_bufio_client *c)
{
	mutex_lock_nested(&c->lock, dm_bufio_in_request());
}

static int dm_bufio_trylock(struct dm_bufio_client *c)
{
	return mutex_trylock(&c->lock);
}

static void dm_bufio_unlock(struct dm_bufio_client *c)
{
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

static DEFINE_SPINLOCK(param_spinlock);

/*
 * Buffers are freed after this timeout
 */
static unsigned dm_bufio_max_age = DM_BUFIO_DEFAULT_AGE_SECS;
static unsigned dm_bufio_retain_bytes = DM_BUFIO_DEFAULT_RETAIN_BYTES;

static unsigned long dm_bufio_peak_allocated;
static unsigned long dm_bufio_allocated_kmem_cache;
static unsigned long dm_bufio_allocated_get_free_pages;
static unsigned long dm_bufio_allocated_vmalloc;
static unsigned long dm_bufio_current_allocated;

/*----------------------------------------------------------------*/

/*
 * Per-client cache: dm_bufio_cache_size / dm_bufio_client_count
 */
static unsigned long dm_bufio_cache_size_per_client;

/*
 * The current number of clients.
 */
static int dm_bufio_client_count;

/*
 * The list of all clients.
 */
static LIST_HEAD(dm_bufio_all_clients);

/*
 * This mutex protects dm_bufio_cache_size_latch,
 * dm_bufio_cache_size_per_client and dm_bufio_client_count
 */
static DEFINE_MUTEX(dm_bufio_clients_lock);

#ifdef CONFIG_DM_DEBUG_BLOCK_STACK_TRACING
static void buffer_record_stack(struct dm_buffer *b)
{
	b->stack_trace.nr_entries = 0;
	b->stack_trace.max_entries = MAX_STACK;
	b->stack_trace.entries = b->stack_entries;
	b->stack_trace.skip = 2;
	save_stack_trace(&b->stack_trace);
}
#endif

/*----------------------------------------------------------------
 * A red/black tree acts as an index for all the buffers.
 *--------------------------------------------------------------*/
static struct dm_buffer *__find(struct dm_bufio_client *c, sector_t block)
{
	struct rb_node *n = c->buffer_tree.rb_node;
	struct dm_buffer *b;

	while (n) {
		b = container_of(n, struct dm_buffer, node);

		if (b->block == block)
			return b;

		n = (b->block < block) ? n->rb_left : n->rb_right;
	}

	return NULL;
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
		new = (found->block < b->block) ?
			&((*new)->rb_left) : &((*new)->rb_right);
	}

	rb_link_node(&b->node, parent, new);
	rb_insert_color(&b->node, &c->buffer_tree);
}

static void __remove(struct dm_bufio_client *c, struct dm_buffer *b)
{
	rb_erase(&b->node, &c->buffer_tree);
}

/*----------------------------------------------------------------*/

static void adjust_total_allocated(enum data_mode data_mode, long diff)
{
	static unsigned long * const class_ptr[DATA_MODE_LIMIT] = {
		&dm_bufio_allocated_kmem_cache,
		&dm_bufio_allocated_get_free_pages,
		&dm_bufio_allocated_vmalloc,
	};

	spin_lock(&param_spinlock);

	*class_ptr[data_mode] += diff;

	dm_bufio_current_allocated += diff;

	if (dm_bufio_current_allocated > dm_bufio_peak_allocated)
		dm_bufio_peak_allocated = dm_bufio_current_allocated;

	spin_unlock(&param_spinlock);
}

/*
 * Change the number of clients and recalculate per-client limit.
 */
static void __cache_size_refresh(void)
{
	BUG_ON(!mutex_is_locked(&dm_bufio_clients_lock));
	BUG_ON(dm_bufio_client_count < 0);

	dm_bufio_cache_size_latch = ACCESS_ONCE(dm_bufio_cache_size);

	/*
	 * Use default if set to 0 and report the actual cache size used.
	 */
	if (!dm_bufio_cache_size_latch) {
		(void)cmpxchg(&dm_bufio_cache_size, 0,
			      dm_bufio_default_cache_size);
		dm_bufio_cache_size_latch = dm_bufio_default_cache_size;
	}

	dm_bufio_cache_size_per_client = dm_bufio_cache_size_latch /
					 (dm_bufio_client_count ? : 1);
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
			       enum data_mode *data_mode)
{
	unsigned noio_flag;
	void *ptr;

	if (c->block_size <= DM_BUFIO_BLOCK_SIZE_SLAB_LIMIT) {
		*data_mode = DATA_MODE_SLAB;
		return kmem_cache_alloc(DM_BUFIO_CACHE(c), gfp_mask);
	}

	if (c->block_size <= DM_BUFIO_BLOCK_SIZE_GFP_LIMIT &&
	    gfp_mask & __GFP_NORETRY) {
		*data_mode = DATA_MODE_GET_FREE_PAGES;
		return (void *)__get_free_pages(gfp_mask,
						c->pages_per_block_bits);
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

	if (gfp_mask & __GFP_NORETRY)
		noio_flag = memalloc_noio_save();

	ptr = __vmalloc(c->block_size, gfp_mask | __GFP_HIGHMEM, PAGE_KERNEL);

	if (gfp_mask & __GFP_NORETRY)
		memalloc_noio_restore(noio_flag);

	return ptr;
}

/*
 * Free buffer's data.
 */
static void free_buffer_data(struct dm_bufio_client *c,
			     void *data, enum data_mode data_mode)
{
	switch (data_mode) {
	case DATA_MODE_SLAB:
		kmem_cache_free(DM_BUFIO_CACHE(c), data);
		break;

	case DATA_MODE_GET_FREE_PAGES:
		free_pages((unsigned long)data, c->pages_per_block_bits);
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
	struct dm_buffer *b = kmalloc(sizeof(struct dm_buffer) + c->aux_size,
				      gfp_mask);

	if (!b)
		return NULL;

	b->c = c;

	b->data = alloc_buffer_data(c, gfp_mask, &b->data_mode);
	if (!b->data) {
		kfree(b);
		return NULL;
	}

	adjust_total_allocated(b->data_mode, (long)c->block_size);

#ifdef CONFIG_DM_DEBUG_BLOCK_STACK_TRACING
	memset(&b->stack_trace, 0, sizeof(b->stack_trace));
#endif
	return b;
}

/*
 * Free buffer and its data.
 */
static void free_buffer(struct dm_buffer *b)
{
	struct dm_bufio_client *c = b->c;

	adjust_total_allocated(b->data_mode, -(long)c->block_size);

	free_buffer_data(c, b->data, b->data_mode);
	kfree(b);
}

/*
 * Link buffer to the hash list and clean or dirty queue.
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
}

/*
 * Unlink buffer from the hash list and dirty or clean queue.
 */
static void __unlink_buffer(struct dm_buffer *b)
{
	struct dm_bufio_client *c = b->c;

	BUG_ON(!c->n_buffers[b->list_mode]);

	c->n_buffers[b->list_mode]--;
	__remove(b->c, b);
	list_del(&b->lru_list);
}

/*
 * Place the buffer to the head of dirty or clean LRU queue.
 */
static void __relink_lru(struct dm_buffer *b, int dirty)
{
	struct dm_bufio_client *c = b->c;

	BUG_ON(!c->n_buffers[b->list_mode]);

	c->n_buffers[b->list_mode]--;
	c->n_buffers[dirty]++;
	b->list_mode = dirty;
	list_move(&b->lru_list, &c->lru[dirty]);
	b->last_accessed = jiffies;
}

/*----------------------------------------------------------------
 * Submit I/O on the buffer.
 *
 * Bio interface is faster but it has some problems:
 *	the vector list is limited (increasing this limit increases
 *	memory-consumption per buffer, so it is not viable);
 *
 *	the memory must be direct-mapped, not vmalloced;
 *
 *	the I/O driver can reject requests spuriously if it thinks that
 *	the requests are too big for the device or if they cross a
 *	controller-defined memory boundary.
 *
 * If the buffer is small enough (up to DM_BUFIO_INLINE_VECS pages) and
 * it is not vmalloced, try using the bio interface.
 *
 * If the buffer is big, if it is vmalloced or if the underlying device
 * rejects the bio because it is too large, use dm-io layer to do the I/O.
 * The dm-io layer splits the I/O into multiple requests, avoiding the above
 * shortcomings.
 *--------------------------------------------------------------*/

/*
 * dm-io completion routine. It just calls b->bio.bi_end_io, pretending
 * that the request was handled directly with bio interface.
 */
static void dmio_complete(unsigned long error, void *context)
{
	struct dm_buffer *b = context;

	b->bio.bi_error = error ? -EIO : 0;
	b->bio.bi_end_io(&b->bio);
}

static void use_dmio(struct dm_buffer *b, int rw, sector_t block,
		     bio_end_io_t *end_io)
{
	int r;
	struct dm_io_request io_req = {
		.bi_op = rw,
		.bi_op_flags = 0,
		.notify.fn = dmio_complete,
		.notify.context = b,
		.client = b->c->dm_io,
	};
	struct dm_io_region region = {
		.bdev = b->c->bdev,
		.sector = block << b->c->sectors_per_block_bits,
		.count = b->c->block_size >> SECTOR_SHIFT,
	};

	if (b->data_mode != DATA_MODE_VMALLOC) {
		io_req.mem.type = DM_IO_KMEM;
		io_req.mem.ptr.addr = b->data;
	} else {
		io_req.mem.type = DM_IO_VMA;
		io_req.mem.ptr.vma = b->data;
	}

	b->bio.bi_end_io = end_io;

	r = dm_io(&io_req, 1, &region, NULL);
	if (r) {
		b->bio.bi_error = r;
		end_io(&b->bio);
	}
}

static void inline_endio(struct bio *bio)
{
	bio_end_io_t *end_fn = bio->bi_private;
	int error = bio->bi_error;

	/*
	 * Reset the bio to free any attached resources
	 * (e.g. bio integrity profiles).
	 */
	bio_reset(bio);

	bio->bi_error = error;
	end_fn(bio);
}

static void use_inline_bio(struct dm_buffer *b, int rw, sector_t block,
			   bio_end_io_t *end_io)
{
	char *ptr;
	int len;

	bio_init(&b->bio);
	b->bio.bi_io_vec = b->bio_vec;
	b->bio.bi_max_vecs = DM_BUFIO_INLINE_VECS;
	b->bio.bi_iter.bi_sector = block << b->c->sectors_per_block_bits;
	b->bio.bi_bdev = b->c->bdev;
	b->bio.bi_end_io = inline_endio;
	/*
	 * Use of .bi_private isn't a problem here because
	 * the dm_buffer's inline bio is local to bufio.
	 */
	b->bio.bi_private = end_io;
	bio_set_op_attrs(&b->bio, rw, 0);

	/*
	 * We assume that if len >= PAGE_SIZE ptr is page-aligned.
	 * If len < PAGE_SIZE the buffer doesn't cross page boundary.
	 */
	ptr = b->data;
	len = b->c->block_size;

	if (len >= PAGE_SIZE)
		BUG_ON((unsigned long)ptr & (PAGE_SIZE - 1));
	else
		BUG_ON((unsigned long)ptr & (len - 1));

	do {
		if (!bio_add_page(&b->bio, virt_to_page(ptr),
				  len < PAGE_SIZE ? len : PAGE_SIZE,
				  offset_in_page(ptr))) {
			BUG_ON(b->c->block_size <= PAGE_SIZE);
			use_dmio(b, rw, block, end_io);
			return;
		}

		len -= PAGE_SIZE;
		ptr += PAGE_SIZE;
	} while (len > 0);

	submit_bio(&b->bio);
}

static void submit_io(struct dm_buffer *b, int rw, sector_t block,
		      bio_end_io_t *end_io)
{
	if (rw == WRITE && b->c->write_callback)
		b->c->write_callback(b);

	if (b->c->block_size <= DM_BUFIO_INLINE_VECS * PAGE_SIZE &&
	    b->data_mode != DATA_MODE_VMALLOC)
		use_inline_bio(b, rw, block, end_io);
	else
		use_dmio(b, rw, block, end_io);
}

/*----------------------------------------------------------------
 * Writing dirty buffers
 *--------------------------------------------------------------*/

/*
 * The endio routine for write.
 *
 * Set the error, clear B_WRITING bit and wake anyone who was waiting on
 * it.
 */
static void write_endio(struct bio *bio)
{
	struct dm_buffer *b = container_of(bio, struct dm_buffer, bio);

	b->write_error = bio->bi_error;
	if (unlikely(bio->bi_error)) {
		struct dm_bufio_client *c = b->c;
		int error = bio->bi_error;
		(void)cmpxchg(&c->async_write_error, 0, error);
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

	if (!write_list)
		submit_io(b, WRITE, b->block, write_endio);
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
		submit_io(b, WRITE, b->block, write_endio);
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

	if (!b->state)	/* fast case */
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

		if (!b->hold_count) {
			__make_buffer_clean(b);
			__unlink_buffer(b);
			return b;
		}
		cond_resched();
	}

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
	set_task_state(current, TASK_UNINTERRUPTIBLE);
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
 * Get writeback threshold and buffer limit for a given client.
 */
static void __get_memory_limit(struct dm_bufio_client *c,
			       unsigned long *threshold_buffers,
			       unsigned long *limit_buffers)
{
	unsigned long buffers;

	if (ACCESS_ONCE(dm_bufio_cache_size) != dm_bufio_cache_size_latch) {
		mutex_lock(&dm_bufio_clients_lock);
		__cache_size_refresh();
		mutex_unlock(&dm_bufio_clients_lock);
	}

	buffers = dm_bufio_cache_size_per_client >>
		  (c->sectors_per_block_bits + SECTOR_SHIFT);

	if (buffers < c->minimum_buffers)
		buffers = c->minimum_buffers;

	*limit_buffers = buffers;
	*threshold_buffers = buffers * DM_BUFIO_WRITEBACK_PERCENT / 100;
}

/*
 * Check if we're over watermark.
 * If we are over threshold_buffers, start freeing buffers.
 * If we're over "limit_buffers", block until we get under the limit.
 */
static void __check_watermark(struct dm_bufio_client *c,
			      struct list_head *write_list)
{
	unsigned long threshold_buffers, limit_buffers;

	__get_memory_limit(c, &threshold_buffers, &limit_buffers);

	while (c->n_buffers[LIST_CLEAN] + c->n_buffers[LIST_DIRTY] >
	       limit_buffers) {

		struct dm_buffer *b = __get_unclaimed_buffer(c);

		if (!b)
			return;

		__free_buffer_wake(b);
		cond_resched();
	}

	if (c->n_buffers[LIST_DIRTY] > threshold_buffers)
		__write_dirty_buffers_async(c, 1, write_list);
}

/*----------------------------------------------------------------
 * Getting a buffer
 *--------------------------------------------------------------*/

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
	 * recheck the hash table.
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
	if (nf == NF_GET && unlikely(test_bit(B_READING, &b->state)))
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
static void read_endio(struct bio *bio)
{
	struct dm_buffer *b = container_of(bio, struct dm_buffer, bio);

	b->read_error = bio->bi_error;

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
		submit_io(b, READ, b->block, read_endio);

	wait_on_bit_io(&b->state, B_READING, TASK_UNINTERRUPTIBLE);

	if (b->read_error) {
		int error = b->read_error;

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
		       sector_t block, unsigned n_blocks)
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
				submit_io(b, READ, b->block, read_endio);
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
		    !test_bit(B_READING, &b->state) &&
		    !test_bit(B_WRITING, &b->state) &&
		    !test_bit(B_DIRTY, &b->state)) {
			__unlink_buffer(b);
			__free_buffer_wake(b);
		}
	}

	dm_bufio_unlock(c);
}
EXPORT_SYMBOL_GPL(dm_bufio_release);

void dm_bufio_mark_buffer_dirty(struct dm_buffer *b)
{
	struct dm_bufio_client *c = b->c;

	dm_bufio_lock(c);

	BUG_ON(test_bit(B_READING, &b->state));

	if (!test_and_set_bit(B_DIRTY, &b->state))
		__relink_lru(b, LIST_DIRTY);

	dm_bufio_unlock(c);
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
 * Use dm-io to send and empty barrier flush the device.
 */
int dm_bufio_issue_flush(struct dm_bufio_client *c)
{
	struct dm_io_request io_req = {
		.bi_op = REQ_OP_WRITE,
		.bi_op_flags = WRITE_FLUSH,
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
 * We first delete any other buffer that may be at that new location.
 *
 * Then, we write the buffer to the original location if it was dirty.
 *
 * Then, if we are the only one who is holding the buffer, relink the buffer
 * in the hash queue for the new location.
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
		submit_io(b, WRITE, new_block, write_endio);
		wait_on_bit_io(&b->state, B_WRITING,
			       TASK_UNINTERRUPTIBLE);
		__unlink_buffer(b);
		__link_buffer(b, old_block, b->list_mode);
	}

	dm_bufio_unlock(c);
	dm_bufio_release(b);
}
EXPORT_SYMBOL_GPL(dm_bufio_release_move);

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
	if (b && likely(!b->hold_count) && likely(!b->state)) {
		__unlink_buffer(b);
		__free_buffer_wake(b);
	}

	dm_bufio_unlock(c);
}
EXPORT_SYMBOL(dm_bufio_forget);

void dm_bufio_set_minimum_buffers(struct dm_bufio_client *c, unsigned n)
{
	c->minimum_buffers = n;
}
EXPORT_SYMBOL(dm_bufio_set_minimum_buffers);

unsigned dm_bufio_get_block_size(struct dm_bufio_client *c)
{
	return c->block_size;
}
EXPORT_SYMBOL_GPL(dm_bufio_get_block_size);

sector_t dm_bufio_get_device_size(struct dm_bufio_client *c)
{
	return i_size_read(c->bdev->bd_inode) >>
			   (SECTOR_SHIFT + c->sectors_per_block_bits);
}
EXPORT_SYMBOL_GPL(dm_bufio_get_device_size);

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
			print_stack_trace(&b->stack_trace, 1);
			b->hold_count = 0; /* mark unclaimed to avoid BUG_ON below */
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
	if (!(gfp & __GFP_FS)) {
		if (test_bit(B_READING, &b->state) ||
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

static unsigned get_retain_buffers(struct dm_bufio_client *c)
{
        unsigned retain_bytes = ACCESS_ONCE(dm_bufio_retain_bytes);
        return retain_bytes / c->block_size;
}

static unsigned long __scan(struct dm_bufio_client *c, unsigned long nr_to_scan,
			    gfp_t gfp_mask)
{
	int l;
	struct dm_buffer *b, *tmp;
	unsigned long freed = 0;
	unsigned long count = nr_to_scan;
	unsigned retain_target = get_retain_buffers(c);

	for (l = 0; l < LIST_SIZE; l++) {
		list_for_each_entry_safe_reverse(b, tmp, &c->lru[l], lru_list) {
			if (__try_evict_buffer(b, gfp_mask))
				freed++;
			if (!--nr_to_scan || ((count - freed) <= retain_target))
				return freed;
			cond_resched();
		}
	}
	return freed;
}

static unsigned long
dm_bufio_shrink_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	struct dm_bufio_client *c;
	unsigned long freed;

	c = container_of(shrink, struct dm_bufio_client, shrinker);
	if (sc->gfp_mask & __GFP_FS)
		dm_bufio_lock(c);
	else if (!dm_bufio_trylock(c))
		return SHRINK_STOP;

	freed  = __scan(c, sc->nr_to_scan, sc->gfp_mask);
	dm_bufio_unlock(c);
	return freed;
}

static unsigned long
dm_bufio_shrink_count(struct shrinker *shrink, struct shrink_control *sc)
{
	struct dm_bufio_client *c = container_of(shrink, struct dm_bufio_client, shrinker);

	return ACCESS_ONCE(c->n_buffers[LIST_CLEAN]) + ACCESS_ONCE(c->n_buffers[LIST_DIRTY]);
}

/*
 * Create the buffering interface
 */
struct dm_bufio_client *dm_bufio_client_create(struct block_device *bdev, unsigned block_size,
					       unsigned reserved_buffers, unsigned aux_size,
					       void (*alloc_callback)(struct dm_buffer *),
					       void (*write_callback)(struct dm_buffer *))
{
	int r;
	struct dm_bufio_client *c;
	unsigned i;

	BUG_ON(block_size < 1 << SECTOR_SHIFT ||
	       (block_size & (block_size - 1)));

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c) {
		r = -ENOMEM;
		goto bad_client;
	}
	c->buffer_tree = RB_ROOT;

	c->bdev = bdev;
	c->block_size = block_size;
	c->sectors_per_block_bits = __ffs(block_size) - SECTOR_SHIFT;
	c->pages_per_block_bits = (__ffs(block_size) >= PAGE_SHIFT) ?
				  __ffs(block_size) - PAGE_SHIFT : 0;
	c->blocks_per_page_bits = (__ffs(block_size) < PAGE_SHIFT ?
				  PAGE_SHIFT - __ffs(block_size) : 0);

	c->aux_size = aux_size;
	c->alloc_callback = alloc_callback;
	c->write_callback = write_callback;

	for (i = 0; i < LIST_SIZE; i++) {
		INIT_LIST_HEAD(&c->lru[i]);
		c->n_buffers[i] = 0;
	}

	mutex_init(&c->lock);
	INIT_LIST_HEAD(&c->reserved_buffers);
	c->need_reserved_buffers = reserved_buffers;

	c->minimum_buffers = DM_BUFIO_MIN_BUFFERS;

	init_waitqueue_head(&c->free_buffer_wait);
	c->async_write_error = 0;

	c->dm_io = dm_io_client_create();
	if (IS_ERR(c->dm_io)) {
		r = PTR_ERR(c->dm_io);
		goto bad_dm_io;
	}

	mutex_lock(&dm_bufio_clients_lock);
	if (c->blocks_per_page_bits) {
		if (!DM_BUFIO_CACHE_NAME(c)) {
			DM_BUFIO_CACHE_NAME(c) = kasprintf(GFP_KERNEL, "dm_bufio_cache-%u", c->block_size);
			if (!DM_BUFIO_CACHE_NAME(c)) {
				r = -ENOMEM;
				mutex_unlock(&dm_bufio_clients_lock);
				goto bad_cache;
			}
		}

		if (!DM_BUFIO_CACHE(c)) {
			DM_BUFIO_CACHE(c) = kmem_cache_create(DM_BUFIO_CACHE_NAME(c),
							      c->block_size,
							      c->block_size, 0, NULL);
			if (!DM_BUFIO_CACHE(c)) {
				r = -ENOMEM;
				mutex_unlock(&dm_bufio_clients_lock);
				goto bad_cache;
			}
		}
	}
	mutex_unlock(&dm_bufio_clients_lock);

	while (c->need_reserved_buffers) {
		struct dm_buffer *b = alloc_buffer(c, GFP_KERNEL);

		if (!b) {
			r = -ENOMEM;
			goto bad_buffer;
		}
		__free_buffer_wake(b);
	}

	mutex_lock(&dm_bufio_clients_lock);
	dm_bufio_client_count++;
	list_add(&c->client_list, &dm_bufio_all_clients);
	__cache_size_refresh();
	mutex_unlock(&dm_bufio_clients_lock);

	c->shrinker.count_objects = dm_bufio_shrink_count;
	c->shrinker.scan_objects = dm_bufio_shrink_scan;
	c->shrinker.seeks = 1;
	c->shrinker.batch = 0;
	register_shrinker(&c->shrinker);

	return c;

bad_buffer:
bad_cache:
	while (!list_empty(&c->reserved_buffers)) {
		struct dm_buffer *b = list_entry(c->reserved_buffers.next,
						 struct dm_buffer, lru_list);
		list_del(&b->lru_list);
		free_buffer(b);
	}
	dm_io_client_destroy(c->dm_io);
bad_dm_io:
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
	unsigned i;

	drop_buffers(c);

	unregister_shrinker(&c->shrinker);

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

	dm_io_client_destroy(c->dm_io);
	kfree(c);
}
EXPORT_SYMBOL_GPL(dm_bufio_client_destroy);

static unsigned get_max_age_hz(void)
{
	unsigned max_age = ACCESS_ONCE(dm_bufio_max_age);

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
	unsigned retain_target = get_retain_buffers(c);
	unsigned count;

	dm_bufio_lock(c);

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

static void cleanup_old_buffers(void)
{
	unsigned long max_age_hz = get_max_age_hz();
	struct dm_bufio_client *c;

	mutex_lock(&dm_bufio_clients_lock);

	list_for_each_entry(c, &dm_bufio_all_clients, client_list)
		__evict_old_buffers(c, max_age_hz);

	mutex_unlock(&dm_bufio_clients_lock);
}

static struct workqueue_struct *dm_bufio_wq;
static struct delayed_work dm_bufio_work;

static void work_fn(struct work_struct *w)
{
	cleanup_old_buffers();

	queue_delayed_work(dm_bufio_wq, &dm_bufio_work,
			   DM_BUFIO_WORK_TIMER_SECS * HZ);
}

/*----------------------------------------------------------------
 * Module setup
 *--------------------------------------------------------------*/

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

	memset(&dm_bufio_caches, 0, sizeof dm_bufio_caches);
	memset(&dm_bufio_cache_names, 0, sizeof dm_bufio_cache_names);

	mem = (__u64)((totalram_pages - totalhigh_pages) *
		      DM_BUFIO_MEMORY_PERCENT / 100) << PAGE_SHIFT;

	if (mem > ULONG_MAX)
		mem = ULONG_MAX;

#ifdef CONFIG_MMU
	/*
	 * Get the size of vmalloc space the same way as VMALLOC_TOTAL
	 * in fs/proc/internal.h
	 */
	if (mem > (VMALLOC_END - VMALLOC_START) * DM_BUFIO_VMALLOC_PERCENT / 100)
		mem = (VMALLOC_END - VMALLOC_START) * DM_BUFIO_VMALLOC_PERCENT / 100;
#endif

	dm_bufio_default_cache_size = mem;

	mutex_lock(&dm_bufio_clients_lock);
	__cache_size_refresh();
	mutex_unlock(&dm_bufio_clients_lock);

	dm_bufio_wq = alloc_workqueue("dm_bufio_cache", WQ_MEM_RECLAIM, 0);
	if (!dm_bufio_wq)
		return -ENOMEM;

	INIT_DELAYED_WORK(&dm_bufio_work, work_fn);
	queue_delayed_work(dm_bufio_wq, &dm_bufio_work,
			   DM_BUFIO_WORK_TIMER_SECS * HZ);

	return 0;
}

/*
 * This is called once when unloading the dm_bufio module.
 */
static void __exit dm_bufio_exit(void)
{
	int bug = 0;
	int i;

	cancel_delayed_work_sync(&dm_bufio_work);
	destroy_workqueue(dm_bufio_wq);

	for (i = 0; i < ARRAY_SIZE(dm_bufio_caches); i++)
		kmem_cache_destroy(dm_bufio_caches[i]);

	for (i = 0; i < ARRAY_SIZE(dm_bufio_cache_names); i++)
		kfree(dm_bufio_cache_names[i]);

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

module_param_named(max_cache_size_bytes, dm_bufio_cache_size, ulong, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(max_cache_size_bytes, "Size of metadata cache");

module_param_named(max_age_seconds, dm_bufio_max_age, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(max_age_seconds, "Max age of a buffer in seconds");

module_param_named(retain_bytes, dm_bufio_retain_bytes, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(retain_bytes, "Try to keep at least this many bytes cached in memory");

module_param_named(peak_allocated_bytes, dm_bufio_peak_allocated, ulong, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(peak_allocated_bytes, "Tracks the maximum allocated memory");

module_param_named(allocated_kmem_cache_bytes, dm_bufio_allocated_kmem_cache, ulong, S_IRUGO);
MODULE_PARM_DESC(allocated_kmem_cache_bytes, "Memory allocated with kmem_cache_alloc");

module_param_named(allocated_get_free_pages_bytes, dm_bufio_allocated_get_free_pages, ulong, S_IRUGO);
MODULE_PARM_DESC(allocated_get_free_pages_bytes, "Memory allocated with get_free_pages");

module_param_named(allocated_vmalloc_bytes, dm_bufio_allocated_vmalloc, ulong, S_IRUGO);
MODULE_PARM_DESC(allocated_vmalloc_bytes, "Memory allocated with vmalloc");

module_param_named(current_allocated_bytes, dm_bufio_current_allocated, ulong, S_IRUGO);
MODULE_PARM_DESC(current_allocated_bytes, "Memory currently used by the cache");

MODULE_AUTHOR("Mikulas Patocka <dm-devel@redhat.com>");
MODULE_DESCRIPTION(DM_NAME " buffered I/O library");
MODULE_LICENSE("GPL");
