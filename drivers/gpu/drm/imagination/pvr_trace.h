/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/* Copyright (c) 2026 Imagination Technologies Ltd. */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM pvr

#if !defined(PVR_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define PVR_TRACE_H

#include "pvr_context.h"
#include "pvr_device.h"
#include "pvr_fw.h"
#include "pvr_hwrt.h"
#include "pvr_job.h"
#include "pvr_sync.h"

#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

/*
 * NOTE:
 * When adding trace points, or extra data to existing ones - you must capture
 * all the data in the TP_fast_assign section that you wish to use in the
 * TP_printk section. This is because the printk is performed on demand from the
 * captured data when you `cat /sys/kernel/tracing/trace` and by the time this
 * happens any pointers you captured will likely no longer point to valid data.
 */

/* Job submit */

TRACE_EVENT(pvr_job_submit_ioctl,
	    TP_PROTO(struct pvr_device *pvr_dev, u32 count),
	    TP_ARGS(pvr_dev, count),
	    TP_STRUCT__entry(__field(struct pvr_device *, pvr_dev)
			     __field(u32, count)),
	    TP_fast_assign(__entry->pvr_dev = pvr_dev;
			   __entry->count = count;),
	    TP_printk("pvr_dev=%p count=%u",
		      __entry->pvr_dev,
		      __entry->count)
);

#define PVR_JOB_TYPE_TO_STR(val)                                         \
	__print_symbolic(val,                                            \
			 { DRM_PVR_JOB_TYPE_GEOMETRY, "geometry" },      \
			 { DRM_PVR_JOB_TYPE_FRAGMENT, "fragment" },      \
			 { DRM_PVR_JOB_TYPE_COMPUTE, "compute" },        \
			 { DRM_PVR_JOB_TYPE_TRANSFER_FRAG, "transfer" })

TRACE_EVENT(pvr_job_create,
	    TP_PROTO(struct pvr_device *pvr_dev, struct pvr_job *job,
		     u32 sync_op_count),
	    TP_ARGS(pvr_dev, job, sync_op_count),
	    TP_STRUCT__entry(__field(struct pvr_device *, pvr_dev)
			     __field(struct pvr_context *, ctx)
			     __field(struct pvr_fw_object *, fw_obj)
			     __field(u32, fw_addr)
			     __field(u32, hwrt_addr)
			     __field(struct pvr_job *, job)
			     __field(enum drm_pvr_job_type, job_type)
			     __field(u32, sync_op_count)),
	    TP_fast_assign(__entry->pvr_dev = pvr_dev;
			   __entry->ctx = job->ctx;
			   __entry->fw_obj = job->ctx->fw_obj;
			   pvr_fw_object_get_fw_addr(job->ctx->fw_obj, &__entry->fw_addr);
			   __entry->hwrt_addr = job->hwrt ?
						job->hwrt->fw_obj->fw_addr_offset :
						0;
			   __entry->job = job;
			   __entry->job_type = job->type;
			   __entry->sync_op_count = sync_op_count;),
	    TP_printk("pvr_dev=%p ctx=%p fw_obj=%p fw_addr=0x%x hwrt_addr=0x%x job=%p job_type=%s sync_op_count=%u",
		      __entry->pvr_dev,
		      __entry->ctx,
		      __entry->fw_obj,
		      __entry->fw_addr,
		      __entry->hwrt_addr,
		      __entry->job,
		      PVR_JOB_TYPE_TO_STR(__entry->job_type),
		      __entry->sync_op_count)
);

#undef PVR_JOB_TYPE_TO_STR

TRACE_EVENT(pvr_job_submit_fw,
	    TP_PROTO(struct pvr_job *job),
	    TP_ARGS(job),
	    TP_STRUCT__entry(__field(struct pvr_job *, job)
			     __field(u32, done_seqno)),
	    TP_fast_assign(__entry->job = job;
			   __entry->done_seqno = job->done_fence->seqno;),
	    TP_printk("job=%p done_seqno=%u",
		      __entry->job,
		      __entry->done_seqno)
);

TRACE_EVENT(pvr_job_done,
	    TP_PROTO(struct pvr_job *job),
	    TP_ARGS(job),
	    TP_STRUCT__entry(__field(struct pvr_job *, job)),
	    TP_fast_assign(__entry->job = job;),
	    TP_printk("job=%p", __entry->job)
);

#endif /* PVR_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH ../../drivers/gpu/drm/imagination
#define TRACE_INCLUDE_FILE pvr_trace
#include <trace/define_trace.h>
