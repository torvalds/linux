/*************************************************************************/ /*!
@File
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rogue

#if !defined(_ROGUE_TRACE_EVENTS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _ROGUE_TRACE_EVENTS_H

#include <linux/tracepoint.h>
#include <linux/time.h>

#define show_secs_from_ns(ns) \
	({ \
		u64 t = ns + (NSEC_PER_USEC / 2); \
		do_div(t, NSEC_PER_SEC); \
		t; \
	})

#define show_usecs_from_ns(ns) \
	({ \
		u64 t = ns + (NSEC_PER_USEC / 2) ; \
		u32 rem; \
		do_div(t, NSEC_PER_USEC); \
		rem = do_div(t, USEC_PER_SEC); \
	})

void trace_fence_update_enabled_callback(void);
void trace_fence_update_disabled_callback(void);

TRACE_EVENT_FN(rogue_fence_update,

	TP_PROTO(const char *comm, const char *cmd, const char *dm, u32 ctx_id, u32 offset,
		u32 sync_fwaddr, u32 sync_value),

	TP_ARGS(comm, cmd, dm, ctx_id, offset, sync_fwaddr, sync_value),

	TP_STRUCT__entry(
		__string(       comm,           comm            )
		__string(       cmd,            cmd             )
		__string(       dm,             dm              )
		__field(        u32,            ctx_id          )
		__field(        u32,            offset          )
		__field(        u32,            sync_fwaddr     )
		__field(        u32,            sync_value      )
	),

	TP_fast_assign(
		__assign_str(comm, comm);
		__assign_str(cmd, cmd);
		__assign_str(dm, dm);
		__entry->ctx_id = ctx_id;
		__entry->offset = offset;
		__entry->sync_fwaddr = sync_fwaddr;
		__entry->sync_value = sync_value;
	),

	TP_printk("comm=%s cmd=%s dm=%s ctx_id=%lu offset=%lu sync_fwaddr=%#lx sync_value=%#lx",
		__get_str(comm),
		__get_str(cmd),
		__get_str(dm),
		(unsigned long)__entry->ctx_id,
		(unsigned long)__entry->offset,
		(unsigned long)__entry->sync_fwaddr,
		(unsigned long)__entry->sync_value),

	trace_fence_update_enabled_callback,
	trace_fence_update_disabled_callback
);

void trace_fence_check_enabled_callback(void);
void trace_fence_check_disabled_callback(void);

TRACE_EVENT_FN(rogue_fence_check,

	TP_PROTO(const char *comm, const char *cmd, const char *dm, u32 ctx_id, u32 offset,
		u32 sync_fwaddr, u32 sync_value),

	TP_ARGS(comm, cmd, dm, ctx_id, offset, sync_fwaddr, sync_value),

	TP_STRUCT__entry(
		__string(       comm,           comm            )
		__string(       cmd,            cmd             )
		__string(       dm,             dm              )
		__field(        u32,            ctx_id          )
		__field(        u32,            offset          )
		__field(        u32,            sync_fwaddr     )
		__field(        u32,            sync_value      )
	),

	TP_fast_assign(
		__assign_str(comm, comm);
		__assign_str(cmd, cmd);
		__assign_str(dm, dm);
		__entry->ctx_id = ctx_id;
		__entry->offset = offset;
		__entry->sync_fwaddr = sync_fwaddr;
		__entry->sync_value = sync_value;
	),

	TP_printk("comm=%s cmd=%s dm=%s ctx_id=%lu offset=%lu sync_fwaddr=%#lx sync_value=%#lx",
		__get_str(comm),
		__get_str(cmd),
		__get_str(dm),
		(unsigned long)__entry->ctx_id,
		(unsigned long)__entry->offset,
		(unsigned long)__entry->sync_fwaddr,
		(unsigned long)__entry->sync_value),

	trace_fence_check_enabled_callback,
	trace_fence_check_disabled_callback
);

TRACE_EVENT(rogue_create_fw_context,

	TP_PROTO(const char *comm, const char *dm, u32 ctx_id),

	TP_ARGS(comm, dm, ctx_id),

	TP_STRUCT__entry(
		__string(       comm,           comm            )
		__string(       dm,             dm              )
		__field(        u32,            ctx_id          )
	),

	TP_fast_assign(
		__assign_str(comm, comm);
		__assign_str(dm, dm);
		__entry->ctx_id = ctx_id;
	),

	TP_printk("comm=%s dm=%s ctx_id=%lu",
		__get_str(comm),
		__get_str(dm),
		(unsigned long)__entry->ctx_id)
);

#if defined(SUPPORT_GPUTRACE_EVENTS)

void PVRGpuTraceEnableUfoCallback(void);
void PVRGpuTraceDisableUfoCallback(void);

TRACE_EVENT_FN(rogue_ufo_update,

	TP_PROTO(u64 timestamp, u32 ctx_id, u32 job_id, u32 fwaddr,
		u32 old_value, u32 new_value),

	TP_ARGS(timestamp, ctx_id, job_id, fwaddr, old_value, new_value),

	TP_STRUCT__entry(
		__field(        u64,            timestamp   )
		__field(        u32,            ctx_id      )
		__field(        u32,            job_id      )
		__field(        u32,            fwaddr      )
		__field(        u32,            old_value   )
		__field(        u32,            new_value   )
	),

	TP_fast_assign(
		__entry->timestamp = timestamp;
		__entry->ctx_id = ctx_id;
		__entry->job_id = job_id;
		__entry->fwaddr = fwaddr;
		__entry->old_value = old_value;
		__entry->new_value = new_value;
	),

	TP_printk("ts=%llu.%06lu ctx_id=%lu job_id=%lu fwaddr=%#lx "
		"old_value=%#lx new_value=%#lx",
		(unsigned long long)show_secs_from_ns(__entry->timestamp),
		(unsigned long)show_usecs_from_ns(__entry->timestamp),
		(unsigned long)__entry->ctx_id,
		(unsigned long)__entry->job_id,
		(unsigned long)__entry->fwaddr,
		(unsigned long)__entry->old_value,
		(unsigned long)__entry->new_value),
	PVRGpuTraceEnableUfoCallback,
	PVRGpuTraceDisableUfoCallback
);

TRACE_EVENT_FN(rogue_ufo_check_fail,

	TP_PROTO(u64 timestamp, u32 ctx_id, u32 job_id, u32 fwaddr,
		u32 value, u32 required),

	TP_ARGS(timestamp, ctx_id, job_id, fwaddr, value, required),

	TP_STRUCT__entry(
		__field(        u64,            timestamp   )
		__field(        u32,            ctx_id      )
		__field(        u32,            job_id      )
		__field(        u32,            fwaddr      )
		__field(        u32,            value       )
		__field(        u32,            required    )
	),

	TP_fast_assign(
		__entry->timestamp = timestamp;
		__entry->ctx_id = ctx_id;
		__entry->job_id = job_id;
		__entry->fwaddr = fwaddr;
		__entry->value = value;
		__entry->required = required;
	),

	TP_printk("ts=%llu.%06lu ctx_id=%lu job_id=%lu fwaddr=%#lx "
		"value=%#lx required=%#lx",
		(unsigned long long)show_secs_from_ns(__entry->timestamp),
		(unsigned long)show_usecs_from_ns(__entry->timestamp),
		(unsigned long)__entry->ctx_id,
		(unsigned long)__entry->job_id,
		(unsigned long)__entry->fwaddr,
		(unsigned long)__entry->value,
		(unsigned long)__entry->required),
	PVRGpuTraceEnableUfoCallback,
	PVRGpuTraceDisableUfoCallback
);

TRACE_EVENT_FN(rogue_ufo_pr_check_fail,

	TP_PROTO(u64 timestamp, u32 ctx_id, u32 job_id, u32 fwaddr,
		u32 value, u32 required),

	TP_ARGS(timestamp, ctx_id, job_id, fwaddr, value, required),

	TP_STRUCT__entry(
		__field(        u64,            timestamp   )
		__field(        u32,            ctx_id      )
		__field(        u32,            job_id      )
		__field(        u32,            fwaddr      )
		__field(        u32,            value       )
		__field(        u32,            required    )
	),

	TP_fast_assign(
		__entry->timestamp = timestamp;
		__entry->ctx_id = ctx_id;
		__entry->job_id = job_id;
		__entry->fwaddr = fwaddr;
		__entry->value = value;
		__entry->required = required;
	),

	TP_printk("ts=%llu.%06lu ctx_id=%lu job_id=%lu fwaddr=%#lx "
		"value=%#lx required=%#lx",
		(unsigned long long)show_secs_from_ns(__entry->timestamp),
		(unsigned long)show_usecs_from_ns(__entry->timestamp),
		(unsigned long)__entry->ctx_id,
		(unsigned long)__entry->job_id,
		(unsigned long)__entry->fwaddr,
		(unsigned long)__entry->value,
		(unsigned long)__entry->required),
	PVRGpuTraceEnableUfoCallback,
	PVRGpuTraceDisableUfoCallback
);

TRACE_EVENT_FN(rogue_ufo_check_success,

	TP_PROTO(u64 timestamp, u32 ctx_id, u32 job_id, u32 fwaddr, u32 value),

	TP_ARGS(timestamp, ctx_id, job_id, fwaddr, value),

	TP_STRUCT__entry(
		__field(        u64,            timestamp   )
		__field(        u32,            ctx_id      )
		__field(        u32,            job_id      )
		__field(        u32,            fwaddr      )
		__field(        u32,            value       )
	),

	TP_fast_assign(
		__entry->timestamp = timestamp;
		__entry->ctx_id = ctx_id;
		__entry->job_id = job_id;
		__entry->fwaddr = fwaddr;
		__entry->value = value;
	),

	TP_printk("ts=%llu.%06lu ctx_id=%lu job_id=%lu fwaddr=%#lx value=%#lx",
		(unsigned long long)show_secs_from_ns(__entry->timestamp),
		(unsigned long)show_usecs_from_ns(__entry->timestamp),
		(unsigned long)__entry->ctx_id,
		(unsigned long)__entry->job_id,
		(unsigned long)__entry->fwaddr,
		(unsigned long)__entry->value),
	PVRGpuTraceEnableUfoCallback,
	PVRGpuTraceDisableUfoCallback
);

TRACE_EVENT_FN(rogue_ufo_pr_check_success,

	TP_PROTO(u64 timestamp, u32 ctx_id, u32 job_id, u32 fwaddr, u32 value),

	TP_ARGS(timestamp, ctx_id, job_id, fwaddr, value),

	TP_STRUCT__entry(
		__field(        u64,            timestamp   )
		__field(        u32,            ctx_id      )
		__field(        u32,            job_id      )
		__field(        u32,            fwaddr      )
		__field(        u32,            value       )
	),

	TP_fast_assign(
		__entry->timestamp = timestamp;
		__entry->ctx_id = ctx_id;
		__entry->job_id = job_id;
		__entry->fwaddr = fwaddr;
		__entry->value = value;
	),

	TP_printk("ts=%llu.%06lu ctx_id=%lu job_id=%lu fwaddr=%#lx value=%#lx",
		(unsigned long long)show_secs_from_ns(__entry->timestamp),
		(unsigned long)show_usecs_from_ns(__entry->timestamp),
		(unsigned long)__entry->ctx_id,
		(unsigned long)__entry->job_id,
		(unsigned long)__entry->fwaddr,
		(unsigned long)__entry->value),
	PVRGpuTraceEnableUfoCallback,
	PVRGpuTraceDisableUfoCallback
);

TRACE_EVENT(rogue_events_lost,

	TP_PROTO(u32 event_source, u32 last_ordinal, u32 curr_ordinal),

	TP_ARGS(event_source, last_ordinal, curr_ordinal),

	TP_STRUCT__entry(
		__field(        u32,            event_source     )
		__field(        u32,            last_ordinal     )
		__field(        u32,            curr_ordinal     )
	),

	TP_fast_assign(
		__entry->event_source = event_source;
		__entry->last_ordinal = last_ordinal;
		__entry->curr_ordinal = curr_ordinal;
	),

	TP_printk("event_source=%s last_ordinal=%u curr_ordinal=%u",
		__print_symbolic(__entry->event_source, {0, "GPU"}, {1, "Host"}),
		__entry->last_ordinal,
		__entry->curr_ordinal)
);

void PVRGpuTraceEnableFirmwareActivityCallback(void);
void PVRGpuTraceDisableFirmwareActivityCallback(void);

TRACE_EVENT_FN(rogue_firmware_activity,

	TP_PROTO(u64 timestamp, const char *task, u32 fw_event),

	TP_ARGS(timestamp, task, fw_event),

	TP_STRUCT__entry(
		__field(        u64,            timestamp       )
		__string(       task,           task            )
		__field(        u32,            fw_event        )
	),

	TP_fast_assign(
		__entry->timestamp = timestamp;
		__assign_str(task, task);
		__entry->fw_event = fw_event;
	),

	TP_printk("ts=%llu.%06lu task=%s event=%s",
		(unsigned long long)show_secs_from_ns(__entry->timestamp),
		(unsigned long)show_usecs_from_ns(__entry->timestamp),
		__get_str(task),
		__print_symbolic(__entry->fw_event,
			/* These values are from pvr_gputrace.h. */
			{ 1, "begin" },
			{ 2, "end" })),

	PVRGpuTraceEnableFirmwareActivityCallback,
	PVRGpuTraceDisableFirmwareActivityCallback
);

#endif /* defined(SUPPORT_GPUTRACE_EVENTS) */

#undef show_secs_from_ns
#undef show_usecs_from_ns

#endif /* _ROGUE_TRACE_EVENTS_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .

/* This is needed because the name of this file doesn't match TRACE_SYSTEM. */
#define TRACE_INCLUDE_FILE rogue_trace_events

/* This part must be outside protection */
#include <trace/define_trace.h>
