/*
 * Copyright (C) 2003 Sistina Software
 * Copyright (C) 2006 Red Hat GmbH
 *
 * This file is released under the GPL.
 */

#include "dm-io.h"

#include <linux/bio.h>
#include <linux/mempool.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>

struct dm_io_client {
	mempool_t *pool;
	struct bio_set *bios;
};

/* FIXME: can we shrink this ? */
struct io {
	unsigned long error;
	atomic_t count;
	struct task_struct *sleeper;
	struct dm_io_client *client;
	io_notify_fn callback;
	void *context;
};

/*
 * io contexts are only dynamically allocated for asynchronous
 * io.  Since async io is likely to be the majority of io we'll
 * have the same number of io contexts as bios! (FIXME: must reduce this).
 */

static unsigned int pages_to_ios(unsigned int pages)
{
	return 4 * pages;	/* too many ? */
}

/*
 * Create a client with mempool and bioset.
 */
struct dm_io_client *dm_io_client_create(unsigned num_pages)
{
	unsigned ios = pages_to_ios(num_pages);
	struct dm_io_client *client;

	client = kmalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return ERR_PTR(-ENOMEM);

	client->pool = mempool_create_kmalloc_pool(ios, sizeof(struct io));
	if (!client->pool)
		goto bad;

	client->bios = bioset_create(16, 16);
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

int dm_io_client_resize(unsigned num_pages, struct dm_io_client *client)
{
	return mempool_resize(client->pool, pages_to_ios(num_pages),
			      GFP_KERNEL);
}
EXPORT_SYMBOL(dm_io_client_resize);

void dm_io_client_destroy(struct dm_io_client *client)
{
	mempool_destroy(client->pool);
	bioset_free(client->bios);
	kfree(client);
}
EXPORT_SYMBOL(dm_io_client_destroy);

/*-----------------------------------------------------------------
 * We need to keep track of which region a bio is doing io for.
 * In order to save a memory allocation we store this the last
 * bvec which we know is unused (blech).
 * XXX This is ugly and can OOPS with some configs... find another way.
 *---------------------------------------------------------------*/
static inline void bio_set_region(struct bio *bio, unsigned region)
{
	bio->bi_io_vec[bio->bi_max_vecs].bv_len = region;
}

static inline unsigned bio_get_region(struct bio *bio)
{
	return bio->bi_io_vec[bio->bi_max_vecs].bv_len;
}

/*-----------------------------------------------------------------
 * We need an io object to keep track of the number of bios that
 * have been dispatched for a particular io.
 *---------------------------------------------------------------*/
static void dec_count(struct io *io, unsigned int region, int error)
{
	if (error)
		set_bit(region, &io->error);

	if (atomic_dec_and_test(&io->count)) {
		if (io->sleeper)
			wake_up_process(io->sleeper);

		else {
			int r = io->error;
			io_notify_fn fn = io->callback;
			void *context = io->context;

			mempool_free(io, io->client->pool);
			fn(r, context);
		}
	}
}

static int endio(struct bio *bio, unsigned int done, int error)
{
	struct io *io;
	unsigned region;

	/* keep going until we've finished */
	if (bio->bi_size)
		return 1;

	if (error && bio_data_dir(bio) == READ)
		zero_fill_bio(bio);

	/*
	 * The bio destructor in bio_put() may use the io object.
	 */
	io = bio->bi_private;
	region = bio_get_region(bio);

	bio->bi_max_vecs++;
	bio_put(bio);

	dec_count(io, region, error);

	return 0;
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
static void bvec_get_page(struct dpages *dp,
		  struct page **p, unsigned long *len, unsigned *offset)
{
	struct bio_vec *bvec = (struct bio_vec *) dp->context_ptr;
	*p = bvec->bv_page;
	*len = bvec->bv_len;
	*offset = bvec->bv_offset;
}

static void bvec_next_page(struct dpages *dp)
{
	struct bio_vec *bvec = (struct bio_vec *) dp->context_ptr;
	dp->context_ptr = bvec + 1;
}

static void bvec_dp_init(struct dpages *dp, struct bio_vec *bvec)
{
	dp->get_page = bvec_get_page;
	dp->next_page = bvec_next_page;
	dp->context_ptr = bvec;
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

static void dm_bio_destructor(struct bio *bio)
{
	struct io *io = bio->bi_private;

	bio_free(bio, io->client->bios);
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
static void do_region(int rw, unsigned int region, struct io_region *where,
		      struct dpages *dp, struct io *io)
{
	struct bio *bio;
	struct page *page;
	unsigned long len;
	unsigned offset;
	unsigned num_bvecs;
	sector_t remaining = where->count;

	while (remaining) {
		/*
		 * Allocate a suitably sized-bio: we add an extra
		 * bvec for bio_get/set_region() and decrement bi_max_vecs
		 * to hide it from bio_add_page().
		 */
		num_bvecs = (remaining / (PAGE_SIZE >> SECTOR_SHIFT)) + 2;
		bio = bio_alloc_bioset(GFP_NOIO, num_bvecs, io->client->bios);
		bio->bi_sector = where->sector + (where->count - remaining);
		bio->bi_bdev = where->bdev;
		bio->bi_end_io = endio;
		bio->bi_private = io;
		bio->bi_destructor = dm_bio_destructor;
		bio->bi_max_vecs--;
		bio_set_region(bio, region);

		/*
		 * Try and add as many pages as possible.
		 */
		while (remaining) {
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
	}
}

static void dispatch_io(int rw, unsigned int num_regions,
			struct io_region *where, struct dpages *dp,
			struct io *io, int sync)
{
	int i;
	struct dpages old_pages = *dp;

	if (sync)
		rw |= (1 << BIO_RW_SYNC);

	/*
	 * For multiple regions we need to be careful to rewind
	 * the dp object for each call to do_region.
	 */
	for (i = 0; i < num_regions; i++) {
		*dp = old_pages;
		if (where[i].count)
			do_region(rw, i, where + i, dp, io);
	}

	/*
	 * Drop the extra reference that we were holding to avoid
	 * the io being completed too early.
	 */
	dec_count(io, 0, 0);
}

static int sync_io(struct dm_io_client *client, unsigned int num_regions,
		   struct io_region *where, int rw, struct dpages *dp,
		   unsigned long *error_bits)
{
	struct io io;

	if (num_regions > 1 && rw != WRITE) {
		WARN_ON(1);
		return -EIO;
	}

	io.error = 0;
	atomic_set(&io.count, 1); /* see dispatch_io() */
	io.sleeper = current;
	io.client = client;

	dispatch_io(rw, num_regions, where, dp, &io, 1);

	while (1) {
		set_current_state(TASK_UNINTERRUPTIBLE);

		if (!atomic_read(&io.count) || signal_pending(current))
			break;

		io_schedule();
	}
	set_current_state(TASK_RUNNING);

	if (atomic_read(&io.count))
		return -EINTR;

	if (error_bits)
		*error_bits = io.error;

	return io.error ? -EIO : 0;
}

static int async_io(struct dm_io_client *client, unsigned int num_regions,
		    struct io_region *where, int rw, struct dpages *dp,
		    io_notify_fn fn, void *context)
{
	struct io *io;

	if (num_regions > 1 && rw != WRITE) {
		WARN_ON(1);
		fn(1, context);
		return -EIO;
	}

	io = mempool_alloc(client->pool, GFP_NOIO);
	io->error = 0;
	atomic_set(&io->count, 1); /* see dispatch_io() */
	io->sleeper = NULL;
	io->client = client;
	io->callback = fn;
	io->context = context;

	dispatch_io(rw, num_regions, where, dp, io, 0);
	return 0;
}

static int dp_init(struct dm_io_request *io_req, struct dpages *dp)
{
	/* Set up dpages based on memory type */
	switch (io_req->mem.type) {
	case DM_IO_PAGE_LIST:
		list_dp_init(dp, io_req->mem.ptr.pl, io_req->mem.offset);
		break;

	case DM_IO_BVEC:
		bvec_dp_init(dp, io_req->mem.ptr.bvec);
		break;

	case DM_IO_VMA:
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
 * New collapsed (a)synchronous interface
 */
int dm_io(struct dm_io_request *io_req, unsigned num_regions,
	  struct io_region *where, unsigned long *sync_error_bits)
{
	int r;
	struct dpages dp;

	r = dp_init(io_req, &dp);
	if (r)
		return r;

	if (!io_req->notify.fn)
		return sync_io(io_req->client, num_regions, where,
			       io_req->bi_rw, &dp, sync_error_bits);

	return async_io(io_req->client, num_regions, where, io_req->bi_rw,
			&dp, io_req->notify.fn, io_req->notify.context);
}
EXPORT_SYMBOL(dm_io);
