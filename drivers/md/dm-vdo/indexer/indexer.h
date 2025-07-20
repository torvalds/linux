/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef INDEXER_H
#define INDEXER_H

#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/wait.h>

#include "funnel-queue.h"

/*
 * UDS public API
 *
 * The Universal Deduplication System (UDS) is an efficient name-value store. When used for
 * deduplicating storage, the names are generally hashes of data blocks and the associated data is
 * where that block is located on the underlying storage medium. The stored names are expected to
 * be randomly distributed among the space of possible names. If this assumption is violated, the
 * UDS index will store fewer names than normal but will otherwise continue to work. The data
 * associated with each name can be any 16-byte value.
 *
 * A client must first create an index session to interact with an index. Once created, the session
 * can be shared among multiple threads or users. When a session is destroyed, it will also close
 * and save any associated index.
 *
 * To make a request, a client must allocate a uds_request structure and set the required fields
 * before launching it. UDS will invoke the provided callback to complete the request. After the
 * callback has been called, the uds_request structure can be freed or reused for a new request.
 * There are five types of requests:
 *
 * A UDS_UPDATE request will associate the provided name with the provided data. Any previous data
 * associated with that name will be discarded.
 *
 * A UDS_QUERY request will return the data associated with the provided name, if any. The entry
 * for the name will also be marked as most recent, as if the data had been updated.
 *
 * A UDS_POST request is a combination of UDS_QUERY and UDS_UPDATE. If there is already data
 * associated with the provided name, that data is returned. If there is no existing association,
 * the name is associated with the newly provided data. This request is equivalent to a UDS_QUERY
 * request followed by a UDS_UPDATE request if no data is found, but it is much more efficient.
 *
 * A UDS_QUERY_NO_UPDATE request will return the data associated with the provided name, but will
 * not change the recency of the entry for the name. This request is primarily useful for testing,
 * to determine whether an entry exists without changing the internal state of the index.
 *
 * A UDS_DELETE request removes any data associated with the provided name. This operation is
 * generally not necessary, because the index will automatically discard its oldest entries once it
 * becomes full.
 */

/* General UDS constants and structures */

enum uds_request_type {
	/* Create or update the mapping for a name, and make the name most recent. */
	UDS_UPDATE,

	/* Return any mapped data for a name, and make the name most recent. */
	UDS_QUERY,

	/*
	 * Return any mapped data for a name, or map the provided data to the name if there is no
	 * current data, and make the name most recent.
	 */
	UDS_POST,

	/* Return any mapped data for a name without updating its recency. */
	UDS_QUERY_NO_UPDATE,

	/* Remove any mapping for a name. */
	UDS_DELETE,

} __packed;

enum uds_open_index_type {
	/* Create a new index. */
	UDS_CREATE,

	/* Load an existing index and try to recover if necessary. */
	UDS_LOAD,

	/* Load an existing index, but only if it was saved cleanly. */
	UDS_NO_REBUILD,
};

enum {
	/* The record name size in bytes */
	UDS_RECORD_NAME_SIZE = 16,
	/* The maximum record data size in bytes */
	UDS_RECORD_DATA_SIZE = 16,
};

/*
 * A type representing a UDS memory configuration which is either a positive integer number of
 * gigabytes or one of the six special constants for configurations smaller than one gigabyte.
 */
typedef int uds_memory_config_size_t;

enum {
	/* The maximum configurable amount of memory */
	UDS_MEMORY_CONFIG_MAX = 1024,
	/* Flag indicating that the index has one less chapter than usual */
	UDS_MEMORY_CONFIG_REDUCED = 0x1000,
	UDS_MEMORY_CONFIG_REDUCED_MAX = 1024 + UDS_MEMORY_CONFIG_REDUCED,
	/* Special values indicating sizes less than 1 GB */
	UDS_MEMORY_CONFIG_256MB = -256,
	UDS_MEMORY_CONFIG_512MB = -512,
	UDS_MEMORY_CONFIG_768MB = -768,
	UDS_MEMORY_CONFIG_REDUCED_256MB = -1280,
	UDS_MEMORY_CONFIG_REDUCED_512MB = -1536,
	UDS_MEMORY_CONFIG_REDUCED_768MB = -1792,
};

struct uds_record_name {
	unsigned char name[UDS_RECORD_NAME_SIZE];
};

struct uds_record_data {
	unsigned char data[UDS_RECORD_DATA_SIZE];
};

struct uds_volume_record {
	struct uds_record_name name;
	struct uds_record_data data;
};

struct uds_parameters {
	/* The block_device used for storage */
	struct block_device *bdev;
	/* The maximum allowable size of the index on storage */
	size_t size;
	/* The offset where the index should start */
	off_t offset;
	/* The maximum memory allocation, in GB */
	uds_memory_config_size_t memory_size;
	/* Whether the index should include sparse chapters */
	bool sparse;
	/* A 64-bit nonce to validate the index */
	u64 nonce;
	/* The number of threads used to process index requests */
	unsigned int zone_count;
	/* The number of threads used to read volume pages */
	unsigned int read_threads;
};

/*
 * These statistics capture characteristics of the current index, including resource usage and
 * requests processed since the index was opened.
 */
struct uds_index_stats {
	/* The total number of records stored in the index */
	u64 entries_indexed;
	/* An estimate of the index's memory usage, in bytes */
	u64 memory_used;
	/* The number of collisions recorded in the volume index */
	u64 collisions;
	/* The number of entries discarded from the index since startup */
	u64 entries_discarded;
	/* The time at which these statistics were fetched */
	s64 current_time;
	/* The number of post calls that found an existing entry */
	u64 posts_found;
	/* The number of post calls that added an entry */
	u64 posts_not_found;
	/*
	 * The number of post calls that found an existing entry that is current enough to only
	 * exist in memory and not have been committed to disk yet
	 */
	u64 in_memory_posts_found;
	/*
	 * The number of post calls that found an existing entry in the dense portion of the index
	 */
	u64 dense_posts_found;
	/*
	 * The number of post calls that found an existing entry in the sparse portion of the index
	 */
	u64 sparse_posts_found;
	/* The number of update calls that updated an existing entry */
	u64 updates_found;
	/* The number of update calls that added a new entry */
	u64 updates_not_found;
	/* The number of delete requests that deleted an existing entry */
	u64 deletions_found;
	/* The number of delete requests that did nothing */
	u64 deletions_not_found;
	/* The number of query calls that found existing entry */
	u64 queries_found;
	/* The number of query calls that did not find an entry */
	u64 queries_not_found;
	/* The total number of requests processed */
	u64 requests;
};

enum uds_index_region {
	/* No location information has been determined */
	UDS_LOCATION_UNKNOWN = 0,
	/* The index page entry has been found */
	UDS_LOCATION_INDEX_PAGE_LOOKUP,
	/* The record page entry has been found */
	UDS_LOCATION_RECORD_PAGE_LOOKUP,
	/* The record is not in the index */
	UDS_LOCATION_UNAVAILABLE,
	/* The record was found in the open chapter */
	UDS_LOCATION_IN_OPEN_CHAPTER,
	/* The record was found in the dense part of the index */
	UDS_LOCATION_IN_DENSE,
	/* The record was found in the sparse part of the index */
	UDS_LOCATION_IN_SPARSE,
} __packed;

/* Zone message requests are used to communicate between index zones. */
enum uds_zone_message_type {
	/* A standard request with no message */
	UDS_MESSAGE_NONE = 0,
	/* Add a chapter to the sparse chapter index cache */
	UDS_MESSAGE_SPARSE_CACHE_BARRIER,
	/* Close a chapter to keep the zone from falling behind */
	UDS_MESSAGE_ANNOUNCE_CHAPTER_CLOSED,
} __packed;

struct uds_zone_message {
	/* The type of message, determining how it will be processed */
	enum uds_zone_message_type type;
	/* The virtual chapter number to which the message applies */
	u64 virtual_chapter;
} __packed;

struct uds_index_session;
struct uds_index;
struct uds_request;

/* Once this callback has been invoked, the uds_request structure can be reused or freed. */
typedef void (*uds_request_callback_fn)(struct uds_request *request);

struct uds_request {
	/* These input fields must be set before launching a request. */

	/* The name of the record to look up or create */
	struct uds_record_name record_name;
	/* New data to associate with the record name, if applicable */
	struct uds_record_data new_metadata;
	/* A callback to invoke when the request is complete */
	uds_request_callback_fn callback;
	/* The index session that will manage this request */
	struct uds_index_session *session;
	/* The type of operation to perform, as describe above */
	enum uds_request_type type;

	/* These output fields are set when a request is complete. */

	/* The existing data associated with the request name, if any */
	struct uds_record_data old_metadata;
	/* True if the record name had an existing entry in the index */
	bool found;
	/* Either UDS_SUCCESS or an error code for the request */
	int status;

	/* The remaining fields are used internally and should not be altered by clients. */
	struct_group(internal,
		     /* The virtual chapter containing the record name, if known */
		     u64 virtual_chapter;
		     /* The region of the index containing the record name */
		     enum uds_index_region location;
		     /* If true, process request immediately by waking the worker thread */
		     bool unbatched;
		     /* If true, continue this request before processing newer requests */
		     bool requeued;
		     /* Control message for coordinating between zones */
		     struct uds_zone_message zone_message;
		     /* The number of the zone which will process this request*/
		     unsigned int zone_number;
		     /* A link for adding a request to a lock-free queue */
		     struct funnel_queue_entry queue_link;
		     /* A link for adding a request to a standard linked list */
		     struct uds_request *next_request;
		     /* A pointer to the index processing this request */
		     struct uds_index *index;
		     );
};

/* A session is required for most index operations. */
int __must_check uds_create_index_session(struct uds_index_session **session);

/* Destroying an index session also closes and saves the associated index. */
int uds_destroy_index_session(struct uds_index_session *session);

/*
 * Create or open an index with an existing session. This operation fails if the index session is
 * suspended, or if there is already an open index.
 */
int __must_check uds_open_index(enum uds_open_index_type open_type,
				const struct uds_parameters *parameters,
				struct uds_index_session *session);

/*
 * Wait until all callbacks for index operations are complete, and prevent new index operations
 * from starting. New index operations will fail with EBUSY until the session is resumed. Also
 * optionally saves the index.
 */
int __must_check uds_suspend_index_session(struct uds_index_session *session, bool save);

/*
 * Allow new index operations for an index, whether it was suspended or not. If the index is
 * suspended and the supplied block device differs from the current backing store, the index will
 * start using the new backing store instead.
 */
int __must_check uds_resume_index_session(struct uds_index_session *session,
					  struct block_device *bdev);

/* Wait until all outstanding index operations are complete. */
int __must_check uds_flush_index_session(struct uds_index_session *session);

/* Close an index. This operation fails if the index session is suspended. */
int __must_check uds_close_index(struct uds_index_session *session);

/* Get index statistics since the last time the index was opened. */
int __must_check uds_get_index_session_stats(struct uds_index_session *session,
					     struct uds_index_stats *stats);

/* This function will fail if any required field of the request is not set. */
int __must_check uds_launch_request(struct uds_request *request);

struct cond_var {
	wait_queue_head_t wait_queue;
};

static inline void uds_init_cond(struct cond_var *cv)
{
	init_waitqueue_head(&cv->wait_queue);
}

static inline void uds_signal_cond(struct cond_var *cv)
{
	wake_up(&cv->wait_queue);
}

static inline void uds_broadcast_cond(struct cond_var *cv)
{
	wake_up_all(&cv->wait_queue);
}

void uds_wait_cond(struct cond_var *cv, struct mutex *mutex);

#endif /* INDEXER_H */
