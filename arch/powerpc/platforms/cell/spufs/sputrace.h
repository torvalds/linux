/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_TRACE_SPUFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SPUFS_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM spufs

TRACE_EVENT(spufs_context,
	TP_PROTO(struct spu_context *ctx, struct spu *spu, const char *name),
	TP_ARGS(ctx, spu, name),

	TP_STRUCT__entry(
		__field(const char *, name)
		__field(int, owner_tid)
		__field(int, number)
	),

	TP_fast_assign(
		__entry->name = name;
		__entry->owner_tid = ctx->tid;
		__entry->number = spu ? spu->number : -1;
	),

	TP_printk("%s (ctxthread = %d, spu = %d)",
		__entry->name, __entry->owner_tid, __entry->number)
);

#define spu_context_trace(name, ctx, spu) \
	trace_spufs_context(ctx, spu, __stringify(name))
#define spu_context_nospu_trace(name, ctx) \
	trace_spufs_context(ctx, NULL, __stringify(name))

#endif /* _TRACE_SPUFS_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE sputrace
#include <trace/define_trace.h>
