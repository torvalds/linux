#include "bcache.h"
#include "btree.h"
#include "request.h"

#include <linux/blktrace_api.h>
#include <linux/module.h>

#define CREATE_TRACE_POINTS
#include <trace/events/bcache.h>

EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_request_start);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_request_end);

EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_bypass_sequential);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_bypass_congested);

EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_read);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_write);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_read_retry);

EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_cache_insert);

EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_journal_replay_key);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_journal_write);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_journal_full);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_journal_entry_full);

EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_btree_cache_cannibalize);

EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_btree_read);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_btree_write);

EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_btree_node_alloc);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_btree_node_alloc_fail);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_btree_node_free);

EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_btree_gc_coalesce);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_gc_start);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_gc_end);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_gc_copy);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_gc_copy_collision);

EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_btree_node_split);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_btree_node_compact);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_btree_set_root);

EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_alloc_invalidate);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_alloc_fail);

EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_writeback);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_writeback_collision);
