/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM coda

#if !defined(__CODA_TRACE_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __CODA_TRACE_H__

#include <linux/tracepoint.h>
#include <media/videobuf2-v4l2.h>

#include "coda.h"

TRACE_EVENT(coda_bit_run,
	TP_PROTO(struct coda_ctx *ctx, int cmd),

	TP_ARGS(ctx, cmd),

	TP_STRUCT__entry(
		__field(int, minor)
		__field(int, ctx)
		__field(int, cmd)
	),

	TP_fast_assign(
		__entry->minor = ctx->fh.vdev->minor;
		__entry->ctx = ctx->idx;
		__entry->cmd = cmd;
	),

	TP_printk("minor = %d, ctx = %d, cmd = %d",
		  __entry->minor, __entry->ctx, __entry->cmd)
);

TRACE_EVENT(coda_bit_done,
	TP_PROTO(struct coda_ctx *ctx),

	TP_ARGS(ctx),

	TP_STRUCT__entry(
		__field(int, minor)
		__field(int, ctx)
	),

	TP_fast_assign(
		__entry->minor = ctx->fh.vdev->minor;
		__entry->ctx = ctx->idx;
	),

	TP_printk("minor = %d, ctx = %d", __entry->minor, __entry->ctx)
);

DECLARE_EVENT_CLASS(coda_buf_class,
	TP_PROTO(struct coda_ctx *ctx, struct vb2_v4l2_buffer *buf),

	TP_ARGS(ctx, buf),

	TP_STRUCT__entry(
		__field(int, minor)
		__field(int, index)
		__field(int, ctx)
	),

	TP_fast_assign(
		__entry->minor = ctx->fh.vdev->minor;
		__entry->index = buf->vb2_buf.index;
		__entry->ctx = ctx->idx;
	),

	TP_printk("minor = %d, index = %d, ctx = %d",
		  __entry->minor, __entry->index, __entry->ctx)
);

DEFINE_EVENT(coda_buf_class, coda_enc_pic_run,
	TP_PROTO(struct coda_ctx *ctx, struct vb2_v4l2_buffer *buf),
	TP_ARGS(ctx, buf)
);

DEFINE_EVENT(coda_buf_class, coda_enc_pic_done,
	TP_PROTO(struct coda_ctx *ctx, struct vb2_v4l2_buffer *buf),
	TP_ARGS(ctx, buf)
);

DECLARE_EVENT_CLASS(coda_buf_meta_class,
	TP_PROTO(struct coda_ctx *ctx, struct vb2_v4l2_buffer *buf,
		 struct coda_buffer_meta *meta),

	TP_ARGS(ctx, buf, meta),

	TP_STRUCT__entry(
		__field(int, minor)
		__field(int, index)
		__field(int, start)
		__field(int, end)
		__field(int, ctx)
	),

	TP_fast_assign(
		__entry->minor = ctx->fh.vdev->minor;
		__entry->index = buf->vb2_buf.index;
		__entry->start = meta->start;
		__entry->end = meta->end;
		__entry->ctx = ctx->idx;
	),

	TP_printk("minor = %d, index = %d, start = 0x%x, end = 0x%x, ctx = %d",
		  __entry->minor, __entry->index, __entry->start, __entry->end,
		  __entry->ctx)
);

DEFINE_EVENT(coda_buf_meta_class, coda_bit_queue,
	TP_PROTO(struct coda_ctx *ctx, struct vb2_v4l2_buffer *buf,
		 struct coda_buffer_meta *meta),
	TP_ARGS(ctx, buf, meta)
);

DECLARE_EVENT_CLASS(coda_meta_class,
	TP_PROTO(struct coda_ctx *ctx, struct coda_buffer_meta *meta),

	TP_ARGS(ctx, meta),

	TP_STRUCT__entry(
		__field(int, minor)
		__field(int, start)
		__field(int, end)
		__field(int, ctx)
	),

	TP_fast_assign(
		__entry->minor = ctx->fh.vdev->minor;
		__entry->start = meta ? meta->start : 0;
		__entry->end = meta ? meta->end : 0;
		__entry->ctx = ctx->idx;
	),

	TP_printk("minor = %d, start = 0x%x, end = 0x%x, ctx = %d",
		  __entry->minor, __entry->start, __entry->end, __entry->ctx)
);

DEFINE_EVENT(coda_meta_class, coda_dec_pic_run,
	TP_PROTO(struct coda_ctx *ctx, struct coda_buffer_meta *meta),
	TP_ARGS(ctx, meta)
);

DEFINE_EVENT(coda_meta_class, coda_dec_pic_done,
	TP_PROTO(struct coda_ctx *ctx, struct coda_buffer_meta *meta),
	TP_ARGS(ctx, meta)
);

DEFINE_EVENT(coda_buf_meta_class, coda_dec_rot_done,
	TP_PROTO(struct coda_ctx *ctx, struct vb2_v4l2_buffer *buf,
		 struct coda_buffer_meta *meta),
	TP_ARGS(ctx, buf, meta)
);

#endif /* __CODA_TRACE_H__ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>
