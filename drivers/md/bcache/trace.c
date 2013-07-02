#include "bcache.h"
#include "btree.h"
#include "request.h"

#include <linux/module.h>

#define CREATE_TRACE_POINTS
#include <trace/events/bcache.h>

EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_request_start);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_request_end);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_passthrough);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_cache_hit);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_cache_miss);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_read_retry);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_writethrough);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_writeback);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_write_skip);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_btree_read);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_btree_write);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_write_dirty);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_read_dirty);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_journal_write);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_cache_insert);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_gc_start);
EXPORT_TRACEPOINT_SYMBOL_GPL(bcache_gc_end);
