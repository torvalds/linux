/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef UDS_INDEX_SESSION_H
#define UDS_INDEX_SESSION_H

#include <linux/atomic.h>
#include <linux/cache.h>

#include "thread-utils.h"

#include "config.h"
#include "indexer.h"

/*
 * The index session mediates all interactions with a UDS index. Once the index session is created,
 * it can be used to open, close, suspend, or recreate an index. It implements the majority of the
 * functions in the top-level UDS API.
 *
 * If any deduplication request fails due to an internal error, the index is marked disabled. It
 * will not accept any further requests and can only be closed. Closing the index will clear the
 * disabled flag, and the index can then be reopened and recovered using the same index session.
 */

struct __aligned(L1_CACHE_BYTES) session_stats {
	/* Post requests that found an entry */
	u64 posts_found;
	/* Post requests found in the open chapter */
	u64 posts_found_open_chapter;
	/* Post requests found in the dense index */
	u64 posts_found_dense;
	/* Post requests found in the sparse index */
	u64 posts_found_sparse;
	/* Post requests that did not find an entry */
	u64 posts_not_found;
	/* Update requests that found an entry */
	u64 updates_found;
	/* Update requests that did not find an entry */
	u64 updates_not_found;
	/* Delete requests that found an entry */
	u64 deletions_found;
	/* Delete requests that did not find an entry */
	u64 deletions_not_found;
	/* Query requests that found an entry */
	u64 queries_found;
	/* Query requests that did not find an entry */
	u64 queries_not_found;
	/* Total number of requests */
	u64 requests;
};

enum index_suspend_status {
	/* An index load has started but the index is not ready for use. */
	INDEX_OPENING = 0,
	/* The index is able to handle requests. */
	INDEX_READY,
	/* The index is attempting to suspend a rebuild. */
	INDEX_SUSPENDING,
	/* An index rebuild has been suspended. */
	INDEX_SUSPENDED,
	/* An index rebuild is being stopped in order to shut down. */
	INDEX_FREEING,
};

struct index_load_context {
	struct mutex mutex;
	struct cond_var cond;
	enum index_suspend_status status;
};

struct uds_index_session {
	unsigned int state;
	struct uds_index *index;
	struct uds_request_queue *callback_queue;
	struct uds_parameters parameters;
	struct index_load_context load_context;
	struct mutex request_mutex;
	struct cond_var request_cond;
	int request_count;
	struct session_stats stats;
};

#endif /* UDS_INDEX_SESSION_H */
