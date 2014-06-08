/*
 * Copyright (C) 2003 Sistina Software
 * Copyright (C) 2006 Red Hat GmbH
 *
 * This file is released under the GPL.
 */

#include "dm.h"

#include <linux/device-mapper.h>

#include <linux/bio.h>
#include <linux/mempool.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/dm-io.h>

#define DM_MSG_PREFIX "io"

#define DM_IO_MAX_REGIONS	BITS_PER_LONG

struct dm_io_client {
	mempool_t *pool;
	struct bio_set *bios;
};

/*
 * Aligning 'struct io' reduces the number of bits required to store
 * its address.  Refer to store_io_and_region_in_bio() below.
 */
struct io {
	unsigned long error_bits;
	atomic_t count;
	struct task_struct *sleeper;
	struct dm_io_client *client;
	io_notify_fn callback;
	void *context;
	void *vma_invalidate_address;
	unsigned long vma_invalidate_size;
} __attribute__((aligned(DM_IO_MAX_REGIONS)));

static struct kmem_cache *_dm_io_cache;

/*
 * Create a client with mempool and bioset.
 */
struct dm_io_client *dm_io_client_create(void)
{
	struct dm_io_client *client;
	unsigned min_ios = dm_get_reserved_bio_based_ios();

	client = kmalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return ERR_PTR(-ENOMEM);

	client->pool = mempool_create_slab_pool(min_ios, _dm_io_cache);
	if (!client->pool)
		goto bad;

	client->bios = bioset_create(min_ios, 0);
	if (!client->bios)
		goto bad;

	return client;

   bad:
	if (client->pool)
		mempool_destroy(client->pool);
	kfree(client);
	return ERR_PTR(-ENOMEM);
}
EXPORT_SYMBOL(dm_io_client_create);

void dm_io_client_destroy(struct dm_io_client *client)
{
	mempool_destroy(client->pool);
	bioset_free(client->bios);
	kfree(client);
}
EXPORT_SYMBOL(dm_io_client_destroy);

/*-----------------------------------------------------------------
 * We need to keep track of which region a bio is doing io for.
 * To avoid a memory allocation to store just 5 or 6 bits, we
 * ensure the 'struct io' pointer is aligned so enough low bits are
 * always zero and then combine it with the region number directly in
 * bi_private.
 *---------------------------------------------------------------*/
static void store_io_and_region_in_bio(struct bio *bio, struct io *io,
				       unsigned region)
{
	if (unlikely(!IS_ALIGNED((unsigned long)io, DM_IO_MAX_REGIONS))) {
		DMCRIT("Unaligned struct io pointer %p", io);
		BUG();
	}

	bio->bi_private = (void *)((unsigned long)io | region);
}

static void retrieve_io_and_region_from_bio(struct bio *bio, struct io **io,
				       unsigned *region)
{
	unsigned long val = (unsigned long)bio->bi_private;

	*io = (void *)(val & -(unsigned long)DM_IO_MAX_REGIONS);
	*region = val & (DM_IO_MAX_REGIONS - 1);
}

/*-----------------------------------------------------------------
 * We need an io object to keep track of the number of bios that
 * have been dispatched for a particular io.
 *---------------------------------------------------------------*/
static void dec_count(struct io *io, unsigned int region, int error)
{
	if (error)
		set_bit(region, &io->error_bits);

	if (atomic_dec_and_test(&io->count)) {
		if (io->vma_invalidate_size)
			invalidate_kernel_vmap_range(io->vma_invalidate_address,
						     io->vma_invalidate_size);

		if (io->sleeper)
			wake_up_process(io->sleeper);

		else {
			unsigned long r = io->error_bits;
			io_notify_fn fn = io->callback;
			void *context = io->context;

			mempool_free(io, io->client->pool);
			fn(r, context);
		}
	}
}

static void endio(struct bio *bio, int error)
{
	struct io *io;
	unsigned region;

	if (error && bio_data_dir(bio) == READ)
		zero_fill_bio(bio);

	/*
	 * The bio destructor in bio_put() may use the io object.
	 */
	retrieve_io_and_region_from_bio(bio, &io, &region);

	bio_put(bio);

	dec_count(io, region, error);
}

/*-----------------------------------------------------------------
 * These little objects provide an abstraction for getting a new
 * destination page for io.
 *---------------------------------------------------------------*/
struct dpages {
	void (*get_page)(struct dpages *dp,
			 struct page **p, unsigned long *len, unsigned *offset);
	void (*next_page)(struct dpages *dp);

	unsigned context_u;
	void *context_ptr;

	void *vma_invalidate_address;
	unsigned long vma_invalidate_size;
};

/*
 * Functions for getting the pages from a list.
 */
static void list_get_page(struct dpages *dp,
		  struct page **p, unsigned long *len, unsigned *offset)
{
	unsigned o = dp->context_u;
	struct page_list *pl = (struct page_list *) dp->context_ptr;

	*p = pl->page;
	*len = PAGE_SIZE - o;
	*offset = o;
}

static void list_next_page(struct dpages *dp)
{
	struct page_list *pl = (struct page_list *) dp->context_ptr;
	dp->context_ptr = pl->next;
	dp->context_u = 0;
}

static void list_dp_init(struct dpages *dp, struct page_list *pl, unsigned offset)
{
	dp->get_page = list_get_page;
	dp->next_page = list_next_page;
	dp->context_u = offset;
	dp->context_ptr = pl;
}

/*
 * Functions for getting the pages from a bvec.
 */
static void bio_get_page(struct dpages *dp, struct page **p,
			 unsigned long *len, unsigned *offset)
{
	struct bio_vec *bvec = dp->context_ptr;
	*p = bvec->bv_page;
	*len = bvec->bv_len - dp->context_u;
	*offset = bvec->bv_offset + dp->context_u;
}

static void bio_next_page(struct dpages *dp)
{
	struct bio_vec *bvec = dp->context_ptr;
	dp->context_ptr = bvec + 1;
	dp->context_u = 0;
}

static void bio_dp_init(struct dpages *dp, struct bio *bio)
{
	dp->get_page = bio_get_page;
	dp->next_page = bio_next_page;
	dp->context_ptr = __bvec_iter_bvec(bio->bi_io_vec, bio->bi_iter);
	dp->context_u = bio->bi_iter.bi_bvec_done;
}

/*
 * Functions for getting the pages from a VMA.
 */
static void vm_get_page(struct dpages *dp,
		 struct page **p, unsigned long *len, unsigned *offset)
{
	*p = vmalloc_to_page(dp->context_ptr);
	*offset = dp->context_u;
	*len = PAGE_SIZE - dp->context_u;
}

static void vm_next_page(struct dpages *dp)
{
	dp->context_ptr += PAGE_SIZE - dp->context_u;
	dp->context_u = 0;
}

static void vm_dp_init(struct dpages *dp, void *data)
{
	dp->get_page = vm_get_page;
	dp->next_page = vm_next_page;
	dp->context_u = ((unsigned long) data) & (PAGE_SIZE - 1);
	dp->context_ptr = data;
}

/*
 * Functions for getting the pages from kernel memory.
 */
static void km_get_page(struct dpages *dp, struct page **p, unsigned long *len,
			unsigned *offset)
{
	*p = virt_to_page(dp->context_ptr);
	*offset = dp->context_u;
	*len = PAGE_SIZE - dp->context_u;
}

static void km_next_page(struct dpages *dp)
{
	dp->context_ptr += PAGE_SIZE - dp->context_u;
	dp->context_u = 0;
}

static void km_dp_init(struct dpages *dp, void *data)
{
	dp->get_page = km_get_page;
	dp->next_page = km_next_page;
	dp->context_u = ((unsigned long) data) & (PAGE_SIZE - 1);
	dp->context_ptr = data;
}

/*-----------------------------------------------------------------
 * IO routines that accept a list of pages.
 *---------------------------------------------------------------*/
static void do_region(int rw, unsigned region, struct dm_io_region *where,
		      struct dpages *dp, struct io *io)
{
	struct bio *bio;
	struct page *page;
	unsigned long len;
	unsigned offset;
	unsigned num_bvecs;
	sector_t remaining = where->count;
	struct request_queue *q = bdev_get_queue(where->bdev);
	unsigned short logical_block_size = queue_logical_block_size(q);
	sector_t num_sectors;

	/*
	 * where->count may be zero if rw holds a flush and we need to
	 * send a zero-sized flush.
	 */
	do {
		/*
		 * Allocate a suitably sized-bio.
		 */
		if ((rw & REQ_DISCARD) || (rw & REQ_WRITE_SAME))
			num_bvecs = 1;
		else
			num_bvecs = min_t(int, bio_get_nr_vecs(where->bdev),
					  dm_sector_div_up(remaining, (PAGE_SIZE >> SECTOR_SHIFT)));

		bio = bio_alloc_bioset(GFP_NOIO, num_bvecs, io->client->bios);
		bio->bi_iter.bi_sector = where->sector + (where->count - remaining);
		bio->bi_bdev = where->bdev;
		bio->bi_end_io = endio;
		store_io_and_region_in_bio(bio, io, region);

		if (rw & REQ_DISCARD) {
			num_sectors = min_t(sector_t, q->limits.max_discard_sectors, remaining);
			bio->bi_iter.bi_size = num_sectors << SECTOR_SHIFT;
			remaining -= num_sectors;
		} else if (rw & REQ_WRITE_SAME) {
			/*
			 * WRITE SAME only uses a single page.
			 */
			dp->get_page(dp, &page, &len, &offset);
			bio_add_page(bio, page, logical_block_size, offset);
			num_sectors = min_t(sector_t, q->limits.max_write_same_sectors, remaining);
			bio->bi_iter.bi_size = num_sectors << SECTOR_SHIFT;

			offset = 0;
			remaining -= num_sectors;
			dp->next_page(dp);
		} else while (remaining) {
			/*
			 * Try and add as many pages as possible.
			 */
			dp->get_page(dp, &page, &len, &offset);
			len = min(len, to_bytes(remaining));
			if (!bio_add_page(bio, page, len, offset))
				break;

			offset = 0;
			remaining -= to_sector(len);
			dp->next_page(dp);
		}

		atomic_inc(&io->count);
		submit_bio(rw, bio);
	} while (remaining);
}

static void dispatch_io(int rw, unsigned int num_regions,
			struct dm_io_region *where, struct dpages *dp,
			struct io *io, int sync)
{
	int i;
	struct dpages old_pages = *dp;

	BUG_ON(num_regions > DM_IO_MAX_REGIONS);

	if (sync)
		rw |= REQ_SYNC;

	/*
	 * For multiple regions we need to be careful to rewind
	 * the dp object for each call to do_region.
	 */
	for (i = 0; i < num_regions; i++) {
		*dp = old_pages;
		if (where[i].count || (rw & REQ_FLUSH))
			do_region(rw, i, where + i, dp, io);
	}

	/*
	 * Drop the extra reference that we were holding to avoid
	 * the io being completed too early.
	 */
	dec_count(io, 0, 0);
}

static int sync_io(struct dm_io_client *client, unsigned int num_regions,
		   struct dm_io_region *where, int rw, struct dpages *dp,
		   unsigned long *error_bits)
{
	/*
	 * gcc <= 4.3 can't do the alignment for stack variables, so we must
	 * align it on our own.
	 * volatile prevents the optimizer from removing or reusing
	 * "io_" field from the stack frame (allowed in ANSI C).
	 */
	volatile char io_[sizeof(struct io) + __alignof__(struct io) - 1];
	struct io *io = (struct io *)PTR_ALIGN(&io_, __alignof__(struct io));

	if (num_regions > 1 && (rw & RW_MASK) != WRITE) {
		WARN_ON(1);
		return -EIO;
	}

	io->error_bits = 0;
	atomic_set(&io->count, 1); /* see dispatch_io() */
	io->sleeper = current;
	io->client = client;

	io->vma_invalidate_address = dp->vma_invalidate_address;
	io->vma_invalidate_size = dp->vma_invalidate_size;

	dispatch_io(rw, num_regions, where, dp, io, 1);

	while (1) {
		set_current_state(TASK_UNINTERRUPTIBLE);

		if (!atomic_read(&io->count))
			break;

		io_schedule();
	}
	set_current_state(TASK_RUNNING);

	if (error_bits)
		*error_bits = io->error_bits;

	return io->error_bits ? -EIO : 0;
}

static int async_io(struct dm_io_client *client, unsigned int num_regions,
		    struct dm_io_region *where, int rw, struct dpages *dp,
		    io_notify_fn fn, void *context)
{
	struct io *io;

	if (num_regions > 1 && (rw & RW_MASK) != WRITE) {
		WARN_ON(1);
		fn(1, context);
		return -EIO;
	}

	io = mempool_alloc(client->pool, GFP_NOIO);
	io->error_bits = 0;
	atomic_set(&io->count, 1); /* see dispatch_io() */
	io->sleeper = NULL;
	io->client = client;
	io->callback = fn;
	io->context = context;

	io->vma_invalidate_address = dp->vma_invalidate_address;
	io->vma_invalidate_size = dp->vma_invalidate_size;

	dispatch_io(rw, num_regions, where, dp, io, 0);
	return 0;
}

static int dp_init(struct dm_io_request *io_req, struct dpages *dp,
		   unsigned long size)
{
	/* Set up dpages based on memory type */

	dp->vma_invalidate_address = NULL;
	dp->vma_invalidate_size = 0;

	switch (io_req->mem.type) {
	case DM_IO_PAGE_LIST:
		list_dp_init(dp, io_req->mem.ptr.pl, io_req->mem.offset);
		break;

	case DM_IO_BIO:
		bio_dp_init(dp, io_req->mem.ptr.bio);
		break;

	case DM_IO_VMA:
		flush_kernel_vmap_range(io_req->mem.ptr.vma, size);
		if ((io_req->bi_rw & RW_MASK) == READ) {
			dp->vma_invalidate_address = io_req->mem.ptr.vma;
			dp->vma_invalidate_size = size;
		}
		vm_dp_init(dp, io_req->mem.ptr.vma);
		break;

	case DM_IO_KMEM:
		km_dp_init(dp, io_req->mem.ptr.addr);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * New collapsed (a)synchronous interface.
 *
 * If the IO is asynchronous (i.e. it has notify.fn), you must either unplug
 * the queue with blk_unplug() some time later or set REQ_SYNC in
io_req->bi_rw. If you fail to do one of these, the IO will be submitted to
 * the disk after q->unplug_delay, which defaults to 3ms in blk-settings.c.
 */
int dm_io(struct dm_io_request *io_req, unsigned num_regions,
	  struct dm_io_region *where, unsigned long *sync_error_bits)
{
	int r;
	struct dpages dp;

	r = dp_init(io_req, &dp, (unsigned long)where->count << SECTOR_SHIFT);
	if (r)
		return r;

	if (!io_req->notify.fn)
		return sync_io(io_req->client, num_regions, where,
			       io_req->bi_rw, &dp, sync_error_bits);

	return async_io(io_req->client, num_regions, where, io_req->bi_rw,
			&dp, io_req->notify.fn, io_req->notify.context);
}
EXPORT_SYMBOL(dm_io);

int __init dm_io_init(void)
{
	_dm_io_cache = KMEM_CACHE(io, 0);
	if (!_dm_io_cache)
		return -ENOMEM;

	return 0;
}

void dm_io_exit(void)
{
	kmem_cache_destroy(_dm_io_cache);
	_dm_io_cache = NULL;
}
