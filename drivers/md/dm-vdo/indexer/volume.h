/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef UDS_VOLUME_H
#define UDS_VOLUME_H

#include <linux/atomic.h>
#include <linux/cache.h>
#include <linux/dm-bufio.h>
#include <linux/limits.h>

#include "permassert.h"
#include "thread-utils.h"

#include "chapter-index.h"
#include "config.h"
#include "geometry.h"
#include "indexer.h"
#include "index-layout.h"
#include "index-page-map.h"
#include "radix-sort.h"
#include "sparse-cache.h"

/*
 * The volume manages deduplication records on permanent storage. The term "volume" can also refer
 * to the region of permanent storage where the records (and the chapters containing them) are
 * stored. The volume handles all I/O to this region by reading, caching, and writing chapter pages
 * as necessary.
 */

enum index_lookup_mode {
	/* Always do lookups in all chapters normally */
	LOOKUP_NORMAL,
	/* Only do a subset of lookups needed when rebuilding an index */
	LOOKUP_FOR_REBUILD,
};

struct queued_read {
	bool invalid;
	bool reserved;
	u32 physical_page;
	struct uds_request *first_request;
	struct uds_request *last_request;
};

struct __aligned(L1_CACHE_BYTES) search_pending_counter {
	u64 atomic_value;
};

struct cached_page {
	/* Whether this page is currently being read asynchronously */
	bool read_pending;
	/* The physical page stored in this cache entry */
	u32 physical_page;
	/* The value of the volume clock when this page was last used */
	s64 last_used;
	/* The cached page buffer */
	struct dm_buffer *buffer;
	/* The chapter index page, meaningless for record pages */
	struct delta_index_page index_page;
};

struct page_cache {
	/* The number of zones */
	unsigned int zone_count;
	/* The number of volume pages that can be cached */
	u32 indexable_pages;
	/* The maximum number of simultaneously cached pages */
	u16 cache_slots;
	/* An index for each physical page noting where it is in the cache */
	u16 *index;
	/* The array of cached pages */
	struct cached_page *cache;
	/* A counter for each zone tracking if a search is occurring there */
	struct search_pending_counter *search_pending_counters;
	/* The read queue entries as a circular array */
	struct queued_read *read_queue;

	/* All entries above this point are constant after initialization. */

	/*
	 * These values are all indexes into the array of read queue entries. New entries in the
	 * read queue are enqueued at read_queue_last. To dequeue entries, a reader thread gets the
	 * lock and then claims the entry pointed to by read_queue_next_read and increments that
	 * value. After the read is completed, the reader thread calls release_read_queue_entry(),
	 * which increments read_queue_first until it points to a pending read, or is equal to
	 * read_queue_next_read. This means that if multiple reads are outstanding,
	 * read_queue_first might not advance until the last of the reads finishes.
	 */
	u16 read_queue_first;
	u16 read_queue_next_read;
	u16 read_queue_last;

	atomic64_t clock;
};

struct volume {
	struct index_geometry *geometry;
	struct dm_bufio_client *client;
	u64 nonce;
	size_t cache_size;

	/* A single page worth of records, for sorting */
	const struct uds_volume_record **record_pointers;
	/* Sorter for sorting records within each page */
	struct radix_sorter *radix_sorter;

	struct sparse_cache *sparse_cache;
	struct page_cache page_cache;
	struct index_page_map *index_page_map;

	struct mutex read_threads_mutex;
	struct cond_var read_threads_cond;
	struct cond_var read_threads_read_done_cond;
	struct thread **reader_threads;
	unsigned int read_thread_count;
	bool read_threads_exiting;

	enum index_lookup_mode lookup_mode;
	unsigned int reserved_buffers;
};

int __must_check uds_make_volume(const struct uds_configuration *config,
				 struct index_layout *layout,
				 struct volume **new_volume);

void uds_free_volume(struct volume *volume);

int __must_check uds_replace_volume_storage(struct volume *volume,
					    struct index_layout *layout,
					    struct block_device *bdev);

int __must_check uds_find_volume_chapter_boundaries(struct volume *volume,
						    u64 *lowest_vcn, u64 *highest_vcn,
						    bool *is_empty);

int __must_check uds_search_volume_page_cache(struct volume *volume,
					      struct uds_request *request,
					      bool *found);

int __must_check uds_search_volume_page_cache_for_rebuild(struct volume *volume,
							  const struct uds_record_name *name,
							  u64 virtual_chapter,
							  bool *found);

int __must_check uds_search_cached_record_page(struct volume *volume,
					       struct uds_request *request, u32 chapter,
					       u16 record_page_number, bool *found);

void uds_forget_chapter(struct volume *volume, u64 chapter);

int __must_check uds_write_chapter(struct volume *volume,
				   struct open_chapter_index *chapter_index,
				   const struct uds_volume_record records[]);

void uds_prefetch_volume_chapter(const struct volume *volume, u32 chapter);

int __must_check uds_read_chapter_index_from_volume(const struct volume *volume,
						    u64 virtual_chapter,
						    struct dm_buffer *volume_buffers[],
						    struct delta_index_page index_pages[]);

int __must_check uds_get_volume_record_page(struct volume *volume, u32 chapter,
					    u32 page_number, u8 **data_ptr);

int __must_check uds_get_volume_index_page(struct volume *volume, u32 chapter,
					   u32 page_number,
					   struct delta_index_page **page_ptr);

#endif /* UDS_VOLUME_H */
