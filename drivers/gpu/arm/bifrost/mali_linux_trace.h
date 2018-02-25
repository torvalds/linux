/*
 *
 * (C) COPYRIGHT 2011-2016 ARM Limited. All rights reserved.
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
 * mali_job_slots_event - called from mali_kbase_core_linux.c
 * @event_id: ORed together bitfields representing a type of event, made with the GATOR_MAKE_EVENT() macro.
 */
TRACE_EVENT(mali_job_slots_event,
	TP_PROTO(unsigned int event_id, unsigned int tgid, unsigned int pid,
			unsigned char job_id),
	TP_ARGS(event_id, tgid, pid, job_id),
	TP_STRUCT__entry(
		__field(unsigned int, event_id)
		__field(unsigned int, tgid)
		__field(unsigned int, pid)
		__field(unsigned char, job_id)
	),
	TP_fast_assign(
		__entry->event_id = event_id;
		__entry->tgid = tgid;
		__entry->pid = pid;
		__entry->job_id = job_id;
	),
	TP_printk("event=%u tgid=%u pid=%u job_id=%u",
		__entry->event_id, __entry->tgid, __entry->pid, __entry->job_id)
);

/**
 * mali_pm_status - Called by mali_kbase_pm_driver.c
 * @event_id: core type (shader, tiler, l2 cache)
 * @value: 64bits bitmask reporting either power status of the cores (1-ON, 0-OFF)
 */
TRACE_EVENT(mali_pm_status,
	TP_PROTO(unsigned int event_id, unsigned long long value),
	TP_ARGS(event_id, value),
	TP_STRUCT__entry(
		__field(unsigned int, event_id)
		__field(unsigned long long, value)
	),
	TP_fast_assign(
		__entry->event_id = event_id;
		__entry->value = value;
	),
	TP_printk("event %u = %llu", __entry->event_id, __entry->value)
);

/**
 * mali_pm_power_on - Called by mali_kbase_pm_driver.c
 * @event_id: core type (shader, tiler, l2 cache)
 * @value: 64bits bitmask reporting the cores to power up
 */
TRACE_EVENT(mali_pm_power_on,
	TP_PROTO(unsigned int event_id, unsigned long long value),
	TP_ARGS(event_id, value),
	TP_STRUCT__entry(
		__field(unsigned int, event_id)
		__field(unsigned long long, value)
	),
	TP_fast_assign(
		__entry->event_id = event_id;
		__entry->value = value;
	),
	TP_printk("event %u = %llu", __entry->event_id, __entry->value)
);

/**
 * mali_pm_power_off - Called by mali_kbase_pm_driver.c
 * @event_id: core type (shader, tiler, l2 cache)
 * @value: 64bits bitmask reporting the cores to power down
 */
TRACE_EVENT(mali_pm_power_off,
	TP_PROTO(unsigned int event_id, unsigned long long value),
	TP_ARGS(event_id, value),
	TP_STRUCT__entry(
		__field(unsigned int, event_id)
		__field(unsigned long long, value)
	),
	TP_fast_assign(
		__entry->event_id = event_id;
		__entry->value = value;
	),
	TP_printk("event %u = %llu", __entry->event_id, __entry->value)
);

/**
 * mali_page_fault_insert_pages - Called by page_fault_worker()
 * it reports an MMU page fault resulting in new pages being mapped.
 * @event_id: MMU address space number.
 * @value: number of newly allocated pages
 */
TRACE_EVENT(mali_page_fault_insert_pages,
	TP_PROTO(int event_id, unsigned long value),
	TP_ARGS(event_id, value),
	TP_STRUCT__entry(
		__field(int, event_id)
		__field(unsigned long, value)
	),
	TP_fast_assign(
		__entry->event_id = event_id;
		__entry->value = value;
	),
	TP_printk("event %d = %lu", __entry->event_id, __entry->value)
);

/**
 * mali_mmu_as_in_use - Called by assign_and_activate_kctx_addr_space()
 * it reports that a certain MMU address space is in use now.
 * @event_id: MMU address space number.
 */
TRACE_EVENT(mali_mmu_as_in_use,
	TP_PROTO(int event_id),
	TP_ARGS(event_id),
	TP_STRUCT__entry(
		__field(int, event_id)
	),
	TP_fast_assign(
		__entry->event_id = event_id;
	),
	TP_printk("event=%d", __entry->event_id)
);

/**
 * mali_mmu_as_released - Called by kbasep_js_runpool_release_ctx_internal()
 * it reports that a certain MMU address space has been released now.
 * @event_id: MMU address space number.
 */
TRACE_EVENT(mali_mmu_as_released,
	TP_PROTO(int event_id),
	TP_ARGS(event_id),
	TP_STRUCT__entry(
		__field(int, event_id)
	),
	TP_fast_assign(
		__entry->event_id = event_id;
	),
	TP_printk("event=%d", __entry->event_id)
);

/**
 * mali_total_alloc_pages_change - Called by kbase_atomic_add_pages()
 *                                 and by kbase_atomic_sub_pages()
 * it reports that the total number of allocated pages is changed.
 * @event_id: number of pages to be added or subtracted (according to the sign).
 */
TRACE_EVENT(mali_total_alloc_pages_change,
	TP_PROTO(long long int event_id),
	TP_ARGS(event_id),
	TP_STRUCT__entry(
		__field(long long int, event_id)
	),
	TP_fast_assign(
		__entry->event_id = event_id;
	),
	TP_printk("event=%lld", __entry->event_id)
);

#endif				/*  _TRACE_MALI_H */

#undef TRACE_INCLUDE_PATH
#undef linux
#define TRACE_INCLUDE_PATH .

/* This part must be outside protection */
#include <trace/define_trace.h>
