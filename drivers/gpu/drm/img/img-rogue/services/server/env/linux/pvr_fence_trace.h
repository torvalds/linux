/*
 * @Codingstyle LinuxKernel
 * @Copyright   Copyright (c) Imagination Technologies Ltd. All Rights Reserved
 * @License     Dual MIT/GPLv2
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU General Public License Version 2 ("GPL") in which case the provisions
 * of GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms of
 * GPL, and not to allow others to use your version of this file under the terms
 * of the MIT license, indicate your decision by deleting the provisions above
 * and replace them with the notice and other provisions required by GPL as set
 * out in the file called "GPL-COPYING" included in this distribution. If you do
 * not delete the provisions above, a recipient may use your version of this file
 * under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT-COPYING".
 *
 * EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
 * PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM pvr_fence

#if !defined(_TRACE_PVR_FENCE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PVR_FENCE_H

#include <linux/tracepoint.h>

struct pvr_fence;
struct pvr_fence_context;

DECLARE_EVENT_CLASS(pvr_fence_context,

	TP_PROTO(struct pvr_fence_context *fctx),
	TP_ARGS(fctx),

	TP_STRUCT__entry(
		__string(name, fctx->name)
		__array(char, val, 128)
	),

	TP_fast_assign(
		__assign_str(name, fctx->name)
		pvr_context_value_str(fctx, __entry->val,
			sizeof(__entry->val));
	),

	TP_printk("name=%s val=%s",
		  __get_str(name),
		  __entry->val
	)
);

DEFINE_EVENT(pvr_fence_context, pvr_fence_context_create,
	TP_PROTO(struct pvr_fence_context *fctx),
	TP_ARGS(fctx)
);

DEFINE_EVENT(pvr_fence_context, pvr_fence_context_destroy,
	TP_PROTO(struct pvr_fence_context *fctx),
	TP_ARGS(fctx)
);

DEFINE_EVENT(pvr_fence_context, pvr_fence_context_destroy_kref,
	TP_PROTO(struct pvr_fence_context *fctx),
	TP_ARGS(fctx)
);

DEFINE_EVENT(pvr_fence_context, pvr_fence_context_signal_fences,
	TP_PROTO(struct pvr_fence_context *fctx),
	TP_ARGS(fctx)
);

DECLARE_EVENT_CLASS(pvr_fence,
	TP_PROTO(struct pvr_fence *fence),
	TP_ARGS(fence),

	TP_STRUCT__entry(
		__string(driver,
			fence->base.ops->get_driver_name(&fence->base))
		__string(timeline,
			fence->base.ops->get_timeline_name(&fence->base))
		__array(char, val, 128)
		__field(u64, context)
	),

	TP_fast_assign(
		__assign_str(driver,
			fence->base.ops->get_driver_name(&fence->base))
		__assign_str(timeline,
			fence->base.ops->get_timeline_name(&fence->base))
		fence->base.ops->fence_value_str(&fence->base,
			__entry->val, sizeof(__entry->val));
		__entry->context = fence->base.context;
	),

	TP_printk("driver=%s timeline=%s ctx=%llu val=%s",
		  __get_str(driver), __get_str(timeline),
		  __entry->context, __entry->val
	)
);

DEFINE_EVENT(pvr_fence, pvr_fence_create,
	TP_PROTO(struct pvr_fence *fence),
	TP_ARGS(fence)
);

DEFINE_EVENT(pvr_fence, pvr_fence_release,
	TP_PROTO(struct pvr_fence *fence),
	TP_ARGS(fence)
);

DEFINE_EVENT(pvr_fence, pvr_fence_enable_signaling,
	TP_PROTO(struct pvr_fence *fence),
	TP_ARGS(fence)
);

DEFINE_EVENT(pvr_fence, pvr_fence_signal_fence,
	TP_PROTO(struct pvr_fence *fence),
	TP_ARGS(fence)
);

DECLARE_EVENT_CLASS(pvr_fence_foreign,
	TP_PROTO(struct pvr_fence *fence),
	TP_ARGS(fence),

	TP_STRUCT__entry(
		__string(driver,
			fence->base.ops->get_driver_name(&fence->base))
		__string(timeline,
			fence->base.ops->get_timeline_name(&fence->base))
		__array(char, val, 128)
		__field(u64, context)
		__string(foreign_driver,
			fence->fence->ops->get_driver_name ?
			fence->fence->ops->get_driver_name(fence->fence) :
			"unknown")
		__string(foreign_timeline,
			fence->fence->ops->get_timeline_name ?
			fence->fence->ops->get_timeline_name(fence->fence) :
			"unknown")
		__array(char, foreign_val, 128)
		__field(u64, foreign_context)
	),

	TP_fast_assign(
		__assign_str(driver,
			fence->base.ops->get_driver_name(&fence->base))
		__assign_str(timeline,
			fence->base.ops->get_timeline_name(&fence->base))
		fence->base.ops->fence_value_str(&fence->base, __entry->val,
			sizeof(__entry->val));
		__entry->context = fence->base.context;
		__assign_str(foreign_driver,
			fence->fence->ops->get_driver_name ?
			fence->fence->ops->get_driver_name(fence->fence) :
			"unknown")
		__assign_str(foreign_timeline,
			fence->fence->ops->get_timeline_name ?
			fence->fence->ops->get_timeline_name(fence->fence) :
			"unknown")
		fence->fence->ops->fence_value_str ?
			fence->fence->ops->fence_value_str(
				fence->fence, __entry->foreign_val,
				sizeof(__entry->foreign_val)) :
			(void) strlcpy(__entry->foreign_val,
				"unknown", sizeof(__entry->foreign_val));
		__entry->foreign_context = fence->fence->context;
	),

	TP_printk("driver=%s timeline=%s ctx=%llu val=%s foreign: driver=%s timeline=%s ctx=%llu val=%s",
		  __get_str(driver), __get_str(timeline), __entry->context,
		  __entry->val, __get_str(foreign_driver),
		  __get_str(foreign_timeline), __entry->foreign_context,
		  __entry->foreign_val
	)
);

DEFINE_EVENT(pvr_fence_foreign, pvr_fence_foreign_create,
	TP_PROTO(struct pvr_fence *fence),
	TP_ARGS(fence)
);

DEFINE_EVENT(pvr_fence_foreign, pvr_fence_foreign_release,
	TP_PROTO(struct pvr_fence *fence),
	TP_ARGS(fence)
);

DEFINE_EVENT(pvr_fence_foreign, pvr_fence_foreign_signal,
	TP_PROTO(struct pvr_fence *fence),
	TP_ARGS(fence)
);

#endif /* _TRACE_PVR_FENCE_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .

/* This is needed because the name of this file doesn't match TRACE_SYSTEM. */
#define TRACE_INCLUDE_FILE pvr_fence_trace

/* This part must be outside protection */
#include <trace/define_trace.h>
