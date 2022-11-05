/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_VISL_TRACE_FWHT_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _VISL_TRACE_FWHT_H_

#include <linux/tracepoint.h>
#include "visl.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM visl_fwht_controls

DECLARE_EVENT_CLASS(v4l2_ctrl_fwht_params_tmpl,
	TP_PROTO(const struct v4l2_ctrl_fwht_params *p),
	TP_ARGS(p),
	TP_STRUCT__entry(
			 __field(u64, backward_ref_ts)
			 __field(u32, version)
			 __field(u32, width)
			 __field(u32, height)
			 __field(u32, flags)
			 __field(u32, colorspace)
			 __field(u32, xfer_func)
			 __field(u32, ycbcr_enc)
			 __field(u32, quantization)
			 ),
	TP_fast_assign(
		       __entry->backward_ref_ts = p->backward_ref_ts;
		       __entry->version = p->version;
		       __entry->width = p->width;
		       __entry->height = p->height;
		       __entry->flags = p->flags;
		       __entry->colorspace = p->colorspace;
		       __entry->xfer_func = p->xfer_func;
		       __entry->ycbcr_enc = p->ycbcr_enc;
		       __entry->quantization = p->quantization;
		       ),
	TP_printk("backward_ref_ts %llu version %u width %u height %u flags %s colorspace %u xfer_func %u ycbcr_enc %u quantization %u",
		  __entry->backward_ref_ts, __entry->version, __entry->width, __entry->height,
		  __print_flags(__entry->flags, "|",
		  {V4L2_FWHT_FL_IS_INTERLACED, "IS_INTERLACED"},
		  {V4L2_FWHT_FL_IS_BOTTOM_FIRST, "IS_BOTTOM_FIRST"},
		  {V4L2_FWHT_FL_IS_ALTERNATE, "IS_ALTERNATE"},
		  {V4L2_FWHT_FL_IS_BOTTOM_FIELD, "IS_BOTTOM_FIELD"},
		  {V4L2_FWHT_FL_LUMA_IS_UNCOMPRESSED, "LUMA_IS_UNCOMPRESSED"},
		  {V4L2_FWHT_FL_CB_IS_UNCOMPRESSED, "CB_IS_UNCOMPRESSED"},
		  {V4L2_FWHT_FL_CR_IS_UNCOMPRESSED, "CR_IS_UNCOMPRESSED"},
		  {V4L2_FWHT_FL_ALPHA_IS_UNCOMPRESSED, "ALPHA_IS_UNCOMPRESSED"},
		  {V4L2_FWHT_FL_I_FRAME, "I_FRAME"},
		  {V4L2_FWHT_FL_PIXENC_HSV, "PIXENC_HSV"},
		  {V4L2_FWHT_FL_PIXENC_RGB, "PIXENC_RGB"},
		  {V4L2_FWHT_FL_PIXENC_YUV, "PIXENC_YUV"}),
		  __entry->colorspace, __entry->xfer_func, __entry->ycbcr_enc,
		  __entry->quantization)
);

DEFINE_EVENT(v4l2_ctrl_fwht_params_tmpl, v4l2_ctrl_fwht_params,
	TP_PROTO(const struct v4l2_ctrl_fwht_params *p),
	TP_ARGS(p)
);

#endif

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH ../../drivers/media/test-drivers/visl
#define TRACE_INCLUDE_FILE visl-trace-fwht
#include <trace/define_trace.h>
