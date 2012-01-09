/*
 * Copyright (C) 2009-2011 Red Hat, Inc.
 *
 * Author: Mikulas Patocka <mpatocka@redhat.com>
 *
 * This file is released under the GPL.
 */

#ifndef DM_BUFIO_H
#define DM_BUFIO_H

#include <linux/blkdev.h>
#include <linux/types.h>

/*----------------------------------------------------------------*/

struct dm_bufio_client;
struct dm_buffer;

/*
 * Create a buffered IO cache on a given device
 */
struct dm_bufio_client *
dm_bufio_client_create(struct block_device *bdev, unsigned block_size,
		       unsigned reserved_buffers, unsigned aux_size,
		       void (*alloc_callback)(struct dm_buffer *),
		       void (*write_callback)(struct dm_buffer *));

/*
 * Release a buffered IO cache.
 */
void dm_bufio_client_destroy(struct dm_bufio_client *c);

/*
 * WARNING: to avoid deadlocks, these conditions are observed:
 *
 * - At most one thread can hold at most "reserved_buffers" simultaneously.
 * - Each other threads can hold at most one buffer.
 * - Threads which call only dm_bufio_get can hold unlimited number of
 *   buffers.
 */

/*
 * Read a given block from disk. Returns pointer to data.  Returns a
 * pointer to dm_buffer that can be used to release the buffer or to make
 * it dirty.
 */
void *dm_bufio_read(struct dm_bufio_client *c, sector_t block,
		    struct dm_buffer **bp);

/*
 * Like dm_bufio_read, but return buffer from cache, don't read
 * it. If the buffer is not in the cache, return NULL.
 */
void *dm_bufio_get(struct dm_bufio_client *c, sector_t block,
		   struct dm_buffer **bp);

/*
 * Like dm_bufio_read, but don't read anything from the disk.  It is
 * expected that the caller initializes the buffer and marks it dirty.
 */
void *dm_bufio_new(struct dm_bufio_client *c, sector_t block,
		   struct dm_buffer **bp);

/*
 * Release a reference obtained with dm_bufio_{read,get,new}. The data
 * pointer and dm_buffer pointer is no longer valid after this call.
 */
void dm_bufio_release(struct dm_buffer *b);

/*
 * Mark a buffer dirty. It should be called after the buffer is modified.
 *
 * In case of memory pressure, the buffer may be written after
 * dm_bufio_mark_buffer_dirty, but before dm_bufio_write_dirty_buffers.  So
 * dm_bufio_write_dirty_buffers guarantees that the buffer is on-disk but
 * the actual writing may occur earlier.
 */
void dm_bufio_mark_buffer_dirty(struct dm_buffer *b);

/*
 * Initiate writing of dirty buffers, without waiting for completion.
 */
void dm_bufio_write_dirty_buffers_async(struct dm_bufio_client *c);

/*
 * Write all dirty buffers. Guarantees that all dirty buffers created prior
 * to this call are on disk when this call exits.
 */
int dm_bufio_write_dirty_buffers(struct dm_bufio_client *c);

/*
 * Send an empty write barrier to the device to flush hardware disk cache.
 */
int dm_bufio_issue_flush(struct dm_bufio_client *c);

/*
 * Like dm_bufio_release but also move the buffer to the new
 * block. dm_bufio_write_dirty_buffers is needed to commit the new block.
 */
void dm_bufio_release_move(struct dm_buffer *b, sector_t new_block);

unsigned dm_bufio_get_block_size(struct dm_bufio_client *c);
sector_t dm_bufio_get_device_size(struct dm_bufio_client *c);
sector_t dm_bufio_get_block_number(struct dm_buffer *b);
void *dm_bufio_get_block_data(struct dm_buffer *b);
void *dm_bufio_get_aux_data(struct dm_buffer *b);
struct dm_bufio_client *dm_bufio_get_client(struct dm_buffer *b);

/*----------------------------------------------------------------*/

#endif
