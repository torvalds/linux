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

#endif
