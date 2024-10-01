/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 Broadcom
 */

#if !defined(_VC4_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _VC4_TRACE_H_

#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM vc4
#define TRACE_INCLUDE_FILE vc4_trace

TRACE_EVENT(vc4_wait_for_seqno_begin,
	    TP_PROTO(struct drm_device *dev, uint64_t seqno, uint64_t timeout),
	    TP_ARGS(dev, seqno, timeout),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u64, seqno)
			     __field(u64, timeout)
			     ),

	    TP_fast_assign(
			   __entry->dev = dev->primary->index;
			   __entry->seqno = seqno;
			   __entry->timeout = timeout;
			   ),

	    TP_printk("dev=%u, seqno=%llu, timeout=%llu",
		      __entry->dev, __entry->seqno, __entry->timeout)
);

TRACE_EVENT(vc4_wait_for_seqno_end,
	    TP_PROTO(struct drm_device *dev, uint64_t seqno),
	    TP_ARGS(dev, seqno),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u64, seqno)
			     ),

	    TP_fast_assign(
			   __entry->dev = dev->primary->index;
			   __entry->seqno = seqno;
			   ),

	    TP_printk("dev=%u, seqno=%llu",
		      __entry->dev, __entry->seqno)
);

TRACE_EVENT(vc4_submit_cl_ioctl,
	    TP_PROTO(struct drm_device *dev, u32 bin_cl_size, u32 shader_rec_size, u32 bo_count),
	    TP_ARGS(dev, bin_cl_size, shader_rec_size, bo_count),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u32, bin_cl_size)
			     __field(u32, shader_rec_size)
			     __field(u32, bo_count)
			     ),

	    TP_fast_assign(
			   __entry->dev = dev->primary->index;
			   __entry->bin_cl_size = bin_cl_size;
			   __entry->shader_rec_size = shader_rec_size;
			   __entry->bo_count = bo_count;
			   ),

	    TP_printk("dev=%u, bin_cl_size=%u, shader_rec_size=%u, bo_count=%u",
		      __entry->dev,
		      __entry->bin_cl_size,
		      __entry->shader_rec_size,
		      __entry->bo_count)
);

TRACE_EVENT(vc4_submit_cl,
	    TP_PROTO(struct drm_device *dev, bool is_render,
		     uint64_t seqno,
		     u32 ctnqba, u32 ctnqea),
	    TP_ARGS(dev, is_render, seqno, ctnqba, ctnqea),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(bool, is_render)
			     __field(u64, seqno)
			     __field(u32, ctnqba)
			     __field(u32, ctnqea)
			     ),

	    TP_fast_assign(
			   __entry->dev = dev->primary->index;
			   __entry->is_render = is_render;
			   __entry->seqno = seqno;
			   __entry->ctnqba = ctnqba;
			   __entry->ctnqea = ctnqea;
			   ),

	    TP_printk("dev=%u, %s, seqno=%llu, 0x%08x..0x%08x",
		      __entry->dev,
		      __entry->is_render ? "RCL" : "BCL",
		      __entry->seqno,
		      __entry->ctnqba,
		      __entry->ctnqea)
);

TRACE_EVENT(vc4_bcl_end_irq,
	    TP_PROTO(struct drm_device *dev,
		     uint64_t seqno),
	    TP_ARGS(dev, seqno),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u64, seqno)
			     ),

	    TP_fast_assign(
			   __entry->dev = dev->primary->index;
			   __entry->seqno = seqno;
			   ),

	    TP_printk("dev=%u, seqno=%llu",
		      __entry->dev,
		      __entry->seqno)
);

TRACE_EVENT(vc4_rcl_end_irq,
	    TP_PROTO(struct drm_device *dev,
		     uint64_t seqno),
	    TP_ARGS(dev, seqno),

	    TP_STRUCT__entry(
			     __field(u32, dev)
			     __field(u64, seqno)
			     ),

	    TP_fast_assign(
			   __entry->dev = dev->primary->index;
			   __entry->seqno = seqno;
			   ),

	    TP_printk("dev=%u, seqno=%llu",
		      __entry->dev,
		      __entry->seqno)
);

#endif /* _VC4_TRACE_H_ */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/gpu/drm/vc4
#include <trace/define_trace.h>
