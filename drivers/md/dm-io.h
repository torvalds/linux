/*
 * Copyright (C) 2003 Sistina Software
 *
 * This file is released under the GPL.
 */

#ifndef _DM_IO_H
#define _DM_IO_H

#include "dm.h"

struct io_region {
	struct block_device *bdev;
	sector_t sector;
	sector_t count;
};

struct page_list {
	struct page_list *next;
	struct page *page;
};

/*
 * 'error' is a bitset, with each bit indicating whether an error
 * occurred doing io to the corresponding region.
 */
typedef void (*io_notify_fn)(unsigned long error, void *context);

enum dm_io_mem_type {
	DM_IO_PAGE_LIST,/* Page list */
	DM_IO_BVEC,	/* Bio vector */
	DM_IO_VMA,	/* Virtual memory area */
	DM_IO_KMEM,	/* Kernel memory */
};

struct dm_io_memory {
	enum dm_io_mem_type type;

	union {
		struct page_list *pl;
		struct bio_vec *bvec;
		void *vma;
		void *addr;
	} ptr;

	unsigned offset;
};

struct dm_io_notify {
	io_notify_fn fn;	/* Callback for asynchronous requests */
	void *context;		/* Passed to callback */
};

/*
 * IO request structure
 */
struct dm_io_client;
struct dm_io_request {
	int bi_rw;			/* READ|WRITE - not READA */
	struct dm_io_memory mem;	/* Memory to use for io */
	struct dm_io_notify notify;	/* Synchronous if notify.fn is NULL */
	struct dm_io_client *client;	/* Client memory handler */
};

/*
 * Before anyone uses the IO interface they should call
 * dm_io_get(), specifying roughly how many pages they are
 * expecting to perform io on concurrently.
 *
 * This function may block.
 */
int dm_io_get(unsigned int num_pages);
void dm_io_put(unsigned int num_pages);

/*
 * For async io calls, users can alternatively use the dm_io() function below
 * and dm_io_client_create() to create private mempools for the client.
 *
 * Create/destroy may block.
 */
struct dm_io_client *dm_io_client_create(unsigned num_pages);
int dm_io_client_resize(unsigned num_pages, struct dm_io_client *client);
void dm_io_client_destroy(struct dm_io_client *client);

/*
 * Synchronous IO.
 *
 * Please ensure that the rw flag in the next two functions is
 * either READ or WRITE, ie. we don't take READA.  Any
 * regions with a zero count field will be ignored.
 */
int dm_io_sync(unsigned int num_regions, struct io_region *where, int rw,
	       struct page_list *pl, unsigned int offset,
	       unsigned long *error_bits);

int dm_io_sync_bvec(unsigned int num_regions, struct io_region *where, int rw,
		    struct bio_vec *bvec, unsigned long *error_bits);

int dm_io_sync_vm(unsigned int num_regions, struct io_region *where, int rw,
		  void *data, unsigned long *error_bits);

/*
 * Aynchronous IO.
 *
 * The 'where' array may be safely allocated on the stack since
 * the function takes a copy.
 */
int dm_io_async(unsigned int num_regions, struct io_region *where, int rw,
		struct page_list *pl, unsigned int offset,
		io_notify_fn fn, void *context);

int dm_io_async_bvec(unsigned int num_regions, struct io_region *where, int rw,
		     struct bio_vec *bvec, io_notify_fn fn, void *context);

int dm_io_async_vm(unsigned int num_regions, struct io_region *where, int rw,
		   void *data, io_notify_fn fn, void *context);

/*
 * IO interface using private per-client pools.
 */
int dm_io(struct dm_io_request *io_req, unsigned num_regions,
	  struct io_region *region, unsigned long *sync_error_bits);

#endif
