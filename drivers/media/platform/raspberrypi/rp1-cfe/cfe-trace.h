/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Raspberry Pi Ltd.
 * Copyright (c) 2024 Ideas on Board Oy
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM cfe

#if !defined(_CFE_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _CFE_TRACE_H

#include <linux/tracepoint.h>
#include <media/videobuf2-v4l2.h>

TRACE_EVENT(cfe_return_buffer,
	TP_PROTO(u32 node_id, u32 buf_idx, u32 queue_id),
	TP_ARGS(node_id, buf_idx, queue_id),
	TP_STRUCT__entry(
		__field(u32, node_id)
		__field(u32, buf_idx)
		__field(u32, queue_id)
	),
	TP_fast_assign(
		__entry->node_id = node_id;
		__entry->buf_idx = buf_idx;
		__entry->queue_id = queue_id;
	),
	TP_printk("node=%u buf=%u, queue=%u", __entry->node_id,
		  __entry->buf_idx, __entry->queue_id)
);

DECLARE_EVENT_CLASS(cfe_buffer_template,
	TP_PROTO(u32 node_id, struct vb2_buffer *buf),
	TP_ARGS(node_id, buf),
	TP_STRUCT__entry(
		__field(u32, node_id)
		__field(u32, buf_idx)
	),
	TP_fast_assign(
		__entry->node_id = node_id;
		__entry->buf_idx = buf->index;
	),
	TP_printk("node=%u buf=%u", __entry->node_id, __entry->buf_idx)
);

DEFINE_EVENT(cfe_buffer_template, cfe_buffer_prepare,
	TP_PROTO(u32 node_id, struct vb2_buffer *buf),
	TP_ARGS(node_id, buf));

TRACE_EVENT(cfe_buffer_queue,
	TP_PROTO(u32 node_id, struct vb2_buffer *buf, bool schedule_now),
	TP_ARGS(node_id, buf, schedule_now),
	TP_STRUCT__entry(
		__field(u32, node_id)
		__field(u32, buf_idx)
		__field(bool, schedule_now)
	),
	TP_fast_assign(
		__entry->node_id = node_id;
		__entry->buf_idx = buf->index;
		__entry->schedule_now = schedule_now;
	),
	TP_printk("node=%u buf=%u%s", __entry->node_id, __entry->buf_idx,
		  __entry->schedule_now ? " schedule immediately" : "")
);

DEFINE_EVENT(cfe_buffer_template, cfe_csi2_schedule,
	TP_PROTO(u32 node_id, struct vb2_buffer *buf),
	TP_ARGS(node_id, buf));

DEFINE_EVENT(cfe_buffer_template, cfe_fe_schedule,
	TP_PROTO(u32 node_id, struct vb2_buffer *buf),
	TP_ARGS(node_id, buf));

TRACE_EVENT(cfe_buffer_complete,
	TP_PROTO(u32 node_id, struct vb2_v4l2_buffer *buf),
	TP_ARGS(node_id, buf),
	TP_STRUCT__entry(
		__field(u32, node_id)
		__field(u32, buf_idx)
		__field(u32, seq)
		__field(u64, ts)
	),
	TP_fast_assign(
		__entry->node_id = node_id;
		__entry->buf_idx = buf->vb2_buf.index;
		__entry->seq = buf->sequence;
		__entry->ts = buf->vb2_buf.timestamp;
	),
	TP_printk("node=%u buf=%u seq=%u ts=%llu", __entry->node_id,
		  __entry->buf_idx, __entry->seq, __entry->ts)
);

TRACE_EVENT(cfe_frame_start,
	TP_PROTO(u32 node_id, u32 fs_count),
	TP_ARGS(node_id, fs_count),
	TP_STRUCT__entry(
		__field(u32, node_id)
		__field(u32, fs_count)
	),
	TP_fast_assign(
		__entry->node_id = node_id;
		__entry->fs_count = fs_count;
	),
	TP_printk("node=%u fs_count=%u", __entry->node_id, __entry->fs_count)
);

TRACE_EVENT(cfe_frame_end,
	TP_PROTO(u32 node_id, u32 fs_count),
	TP_ARGS(node_id, fs_count),
	TP_STRUCT__entry(
		__field(u32, node_id)
		__field(u32, fs_count)
	),
	TP_fast_assign(
		__entry->node_id = node_id;
		__entry->fs_count = fs_count;
	),
	TP_printk("node=%u fs_count=%u", __entry->node_id, __entry->fs_count)
);

TRACE_EVENT(cfe_prepare_next_job,
	TP_PROTO(bool fe_enabled),
	TP_ARGS(fe_enabled),
	TP_STRUCT__entry(
		__field(bool, fe_enabled)
	),
	TP_fast_assign(
		__entry->fe_enabled = fe_enabled;
	),
	TP_printk("fe_enabled=%u", __entry->fe_enabled)
);

/* These are copied from csi2.c */
#define CSI2_STATUS_IRQ_FS(x)			(BIT(0) << (x))
#define CSI2_STATUS_IRQ_FE(x)			(BIT(4) << (x))
#define CSI2_STATUS_IRQ_FE_ACK(x)		(BIT(8) << (x))
#define CSI2_STATUS_IRQ_LE(x)			(BIT(12) << (x))
#define CSI2_STATUS_IRQ_LE_ACK(x)		(BIT(16) << (x))

TRACE_EVENT(csi2_irq,
	TP_PROTO(u32 channel, u32 status, u32 dbg),
	TP_ARGS(channel, status, dbg),
	TP_STRUCT__entry(
		__field(u32, channel)
		__field(u32, status)
		__field(u32, dbg)
	),
	TP_fast_assign(
		__entry->channel = channel;
		__entry->status = status;
		__entry->dbg = dbg;
	),
	TP_printk("ch=%u flags=[ %s%s%s%s%s] frame=%u line=%u\n",
		  __entry->channel,
		  (__entry->status & CSI2_STATUS_IRQ_FS(__entry->channel)) ?
			"FS " : "",
		  (__entry->status & CSI2_STATUS_IRQ_FE(__entry->channel)) ?
			"FE " : "",
		  (__entry->status & CSI2_STATUS_IRQ_FE_ACK(__entry->channel)) ?
			"FE_ACK " : "",
		  (__entry->status & CSI2_STATUS_IRQ_LE(__entry->channel)) ?
			"LE " : "",
		  (__entry->status & CSI2_STATUS_IRQ_LE_ACK(__entry->channel)) ?
			"LE_ACK " : "",
		  __entry->dbg >> 16, __entry->dbg & 0xffff)
);

TRACE_EVENT(fe_irq,
	TP_PROTO(u32 status, u32 output_status, u32 frame_status,
		 u32 error_status, u32 int_status),
	TP_ARGS(status, output_status, frame_status, error_status, int_status),
	TP_STRUCT__entry(
		__field(u32, status)
		__field(u32, output_status)
		__field(u32, frame_status)
		__field(u32, error_status)
		__field(u32, int_status)
	),
	TP_fast_assign(
		__entry->status = status;
		__entry->output_status = output_status;
		__entry->frame_status = frame_status;
		__entry->error_status = error_status;
		__entry->int_status = int_status;
	),
	TP_printk("status 0x%x out_status 0x%x frame_status 0x%x error_status 0x%x int_status 0x%x",
		  __entry->status,
		  __entry->output_status,
		  __entry->frame_status,
		  __entry->error_status,
		  __entry->int_status)
);

#endif /* _CFE_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE ../../drivers/media/platform/raspberrypi/rp1-cfe/cfe-trace
#include <trace/define_trace.h>
