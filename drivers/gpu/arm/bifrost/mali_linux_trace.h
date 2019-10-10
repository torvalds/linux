/*
 *
 * (C) COPYRIGHT 2011-2019 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#if !defined(_TRACE_MALI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MALI_H

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mali
#define TRACE_INCLUDE_FILE mali_linux_trace

#include <linux/tracepoint.h>

#define MALI_JOB_SLOTS_EVENT_CHANGED

/**
 * mali_job_slots_event - Reports change of job slot status.
 * @gpu_id:   Kbase device id
 * @event_id: ORed together bitfields representing a type of event,
 *            made with the GATOR_MAKE_EVENT() macro.
 */
TRACE_EVENT(mali_job_slots_event,
	TP_PROTO(u32 gpu_id, u32 event_id, u32 tgid, u32 pid,
		u8 job_id),
	TP_ARGS(gpu_id, event_id, tgid, pid, job_id),
	TP_STRUCT__entry(
		__field(u32, gpu_id)
		__field(u32, event_id)
		__field(u32, tgid)
		__field(u32, pid)
		__field(u8,  job_id)
	),
	TP_fast_assign(
		__entry->gpu_id   = gpu_id;
		__entry->event_id = event_id;
		__entry->tgid     = tgid;
		__entry->pid      = pid;
		__entry->job_id   = job_id;
	),
	TP_printk("gpu=%u event=%u tgid=%u pid=%u job_id=%u",
		__entry->gpu_id, __entry->event_id,
		__entry->tgid, __entry->pid, __entry->job_id)
);

/**
 * mali_pm_status - Reports change of power management status.
 * @gpu_id:   Kbase device id
 * @event_id: Core type (shader, tiler, L2 cache)
 * @value:    64bits bitmask reporting either power status of
 *            the cores (1-ON, 0-OFF)
 */
TRACE_EVENT(mali_pm_status,
	TP_PROTO(u32 gpu_id, u32 event_id, u64 value),
	TP_ARGS(gpu_id, event_id, value),
	TP_STRUCT__entry(
		__field(u32, gpu_id)
		__field(u32, event_id)
		__field(u64, value)
	),
	TP_fast_assign(
		__entry->gpu_id   = gpu_id;
		__entry->event_id = event_id;
		__entry->value    = value;
	),
	TP_printk("gpu=%u event %u = %llu",
		__entry->gpu_id, __entry->event_id, __entry->value)
);

/**
 * mali_page_fault_insert_pages - Reports an MMU page fault
 * resulting in new pages being mapped.
 * @gpu_id:   Kbase device id
 * @event_id: MMU address space number
 * @value:    Number of newly allocated pages
 */
TRACE_EVENT(mali_page_fault_insert_pages,
	TP_PROTO(u32 gpu_id, s32 event_id, u64 value),
	TP_ARGS(gpu_id, event_id, value),
	TP_STRUCT__entry(
		__field(u32, gpu_id)
		__field(s32, event_id)
		__field(u64, value)
	),
	TP_fast_assign(
		__entry->gpu_id   = gpu_id;
		__entry->event_id = event_id;
		__entry->value    = value;
	),
	TP_printk("gpu=%u event %d = %llu",
		__entry->gpu_id, __entry->event_id, __entry->value)
);

/**
 * mali_total_alloc_pages_change - Reports that the total number of
 * allocated pages has changed.
 * @gpu_id:   Kbase device id
 * @event_id: Total number of pages allocated
 */
TRACE_EVENT(mali_total_alloc_pages_change,
	TP_PROTO(u32 gpu_id, s64 event_id),
	TP_ARGS(gpu_id, event_id),
	TP_STRUCT__entry(
		__field(u32, gpu_id)
		__field(s64, event_id)
	),
	TP_fast_assign(
		__entry->gpu_id   = gpu_id;
		__entry->event_id = event_id;
	),
	TP_printk("gpu=%u event=%lld", __entry->gpu_id, __entry->event_id)
);

#endif /* _TRACE_MALI_H */

#undef TRACE_INCLUDE_PATH
#undef linux
#define TRACE_INCLUDE_PATH .

/* This part must be outside protection */
#include <trace/define_trace.h>
