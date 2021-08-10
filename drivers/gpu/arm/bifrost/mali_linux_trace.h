/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2011-2016, 2018-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
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
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mali

#if !defined(_TRACE_MALI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MALI_H

#include <linux/tracepoint.h>

#if defined(CONFIG_MALI_BIFROST_GATOR_SUPPORT)
#define MALI_JOB_SLOTS_EVENT_CHANGED

/*
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
#endif /* CONFIG_MALI_BIFROST_GATOR_SUPPORT */

/*
 * MMU subsystem tracepoints
 */

/* Fault status and exception code helpers
 *
 * Must be macros to allow use by user-side tracepoint tools
 *
 * bits 0:1 masked off code, and used for the level
 *
 * Tracepoint files get included more than once - protect against multiple
 * definition
 */
#ifndef __TRACE_MALI_MMU_HELPERS
#define __TRACE_MALI_MMU_HELPERS
/* Complex macros should be enclosed in parenthesis.
 *
 * We need to have those parentheses removed for our arrays of symbolic look-ups
 * for __print_symbolic() whilst also being able to use them outside trace code
 */
#define _ENSURE_PARENTHESIS(args...) args

#define KBASE_MMU_FAULT_CODE_EXCEPTION_NAME_PRINT(code) \
		(!KBASE_MMU_FAULT_CODE_VALID(code) ? "UNKNOWN,level=" : \
				__print_symbolic(((code) & ~3u), \
				KBASE_MMU_FAULT_CODE_SYMBOLIC_STRINGS))
#define KBASE_MMU_FAULT_CODE_LEVEL(code) \
	(((((code) & ~0x3u) == 0xC4) ? 4 : 0) + ((code) & 0x3u))

#define KBASE_MMU_FAULT_STATUS_CODE(status)	\
		((status) & 0xFFu)
#define KBASE_MMU_FAULT_STATUS_DECODED_STRING(status) \
		(((status) & (1u << 10)) ? "DECODER_FAULT" : "SLAVE_FAULT")

#define KBASE_MMU_FAULT_STATUS_EXCEPTION_NAME_PRINT(status) \
		KBASE_MMU_FAULT_CODE_EXCEPTION_NAME_PRINT( \
				KBASE_MMU_FAULT_STATUS_CODE(status))

#define KBASE_MMU_FAULT_STATUS_LEVEL(status) \
		KBASE_MMU_FAULT_CODE_LEVEL(KBASE_MMU_FAULT_STATUS_CODE(status))

#define KBASE_MMU_FAULT_STATUS_ACCESS(status) \
		((status) & AS_FAULTSTATUS_ACCESS_TYPE_MASK)
#define KBASE_MMU_FAULT_ACCESS_SYMBOLIC_STRINGS _ENSURE_PARENTHESIS(\
	{AS_FAULTSTATUS_ACCESS_TYPE_ATOMIC, "ATOMIC" }, \
	{AS_FAULTSTATUS_ACCESS_TYPE_EX,     "EXECUTE"}, \
	{AS_FAULTSTATUS_ACCESS_TYPE_READ,   "READ"   }, \
	{AS_FAULTSTATUS_ACCESS_TYPE_WRITE,  "WRITE"  })
#define KBASE_MMU_FAULT_STATUS_ACCESS_PRINT(status) \
		__print_symbolic(KBASE_MMU_FAULT_STATUS_ACCESS(status), \
				KBASE_MMU_FAULT_ACCESS_SYMBOLIC_STRINGS)

#if MALI_USE_CSF
#define KBASE_MMU_FAULT_CODE_VALID(code) \
		((code >= 0xC0 && code <= 0xEB) && \
		(!(code >= 0xC5 && code <= 0xC7)) && \
		(!(code >= 0xCC && code <= 0xD8)) && \
		(!(code >= 0xDC && code <= 0xDF)) && \
		(!(code >= 0xE1 && code <= 0xE3)))
#define KBASE_MMU_FAULT_CODE_SYMBOLIC_STRINGS _ENSURE_PARENTHESIS(\
		{0xC0, "TRANSLATION_FAULT_" }, \
		{0xC4, "TRANSLATION_FAULT_" }, \
		{0xC8, "PERMISSION_FAULT_" }, \
		{0xD0, "TRANSTAB_BUS_FAULT_" }, \
		{0xD8, "ACCESS_FLAG_" }, \
		{0xE0, "ADDRESS_SIZE_FAULT_IN" }, \
		{0xE4, "ADDRESS_SIZE_FAULT_OUT" }, \
		{0xE8, "MEMORY_ATTRIBUTES_FAULT_" })
#else /* MALI_USE_CSF */
#define KBASE_MMU_FAULT_CODE_VALID(code) \
	((code >= 0xC0 && code <= 0xEF) && \
		(!(code >= 0xC5 && code <= 0xC6)) && \
		(!(code >= 0xCC && code <= 0xCF)) && \
		(!(code >= 0xD4 && code <= 0xD7)) && \
		(!(code >= 0xDC && code <= 0xDF)))
#define KBASE_MMU_FAULT_CODE_SYMBOLIC_STRINGS _ENSURE_PARENTHESIS(\
		{0xC0, "TRANSLATION_FAULT_" }, \
		{0xC4, "TRANSLATION_FAULT(_7==_IDENTITY)_" }, \
		{0xC8, "PERMISSION_FAULT_" }, \
		{0xD0, "TRANSTAB_BUS_FAULT_" }, \
		{0xD8, "ACCESS_FLAG_" }, \
		{0xE0, "ADDRESS_SIZE_FAULT_IN" }, \
		{0xE4, "ADDRESS_SIZE_FAULT_OUT" }, \
		{0xE8, "MEMORY_ATTRIBUTES_FAULT_" }, \
		{0xEC, "MEMORY_ATTRIBUTES_NONCACHEABLE_" })
#endif /* MALI_USE_CSF */
#endif /* __TRACE_MALI_MMU_HELPERS */

/* trace_mali_mmu_page_fault_grow
 *
 * Tracepoint about a successful grow of a region due to a GPU page fault
 */
TRACE_EVENT(mali_mmu_page_fault_grow,
	TP_PROTO(struct kbase_va_region *reg, struct kbase_fault *fault,
		size_t new_pages),
	TP_ARGS(reg, fault, new_pages),
	TP_STRUCT__entry(
		__field(u64, start_addr)
		__field(u64, fault_addr)
		__field(u64, fault_extra_addr)
		__field(size_t, new_pages)
		__field(u32, status)
	),
	TP_fast_assign(
		__entry->start_addr       = ((u64)reg->start_pfn) << PAGE_SHIFT;
		__entry->fault_addr       = fault->addr;
		__entry->fault_extra_addr = fault->extra_addr;
		__entry->new_pages        = new_pages;
		__entry->status     = fault->status;
	),
	TP_printk("start=0x%llx fault_addr=0x%llx fault_extra_addr=0x%llx new_pages=%zu raw_fault_status=0x%x decoded_faultstatus=%s exception_type=0x%x,%s%u access_type=0x%x,%s source_id=0x%x",
		__entry->start_addr, __entry->fault_addr,
		__entry->fault_extra_addr, __entry->new_pages,
		__entry->status,
		KBASE_MMU_FAULT_STATUS_DECODED_STRING(__entry->status),
		KBASE_MMU_FAULT_STATUS_CODE(__entry->status),
		KBASE_MMU_FAULT_STATUS_EXCEPTION_NAME_PRINT(__entry->status),
		KBASE_MMU_FAULT_STATUS_LEVEL(__entry->status),
		KBASE_MMU_FAULT_STATUS_ACCESS(__entry->status) >> 8,
		KBASE_MMU_FAULT_STATUS_ACCESS_PRINT(__entry->status),
		__entry->status >> 16)
);




/*
 * Just-in-time memory allocation subsystem tracepoints
 */

/* Just-in-time memory allocation soft-job template. Override the TP_printk
 * further if need be. jit_id can be 0.
 */
DECLARE_EVENT_CLASS(mali_jit_softjob_template,
	TP_PROTO(struct kbase_va_region *reg, u8 jit_id),
	TP_ARGS(reg, jit_id),
	TP_STRUCT__entry(
		__field(u64, start_addr)
		__field(size_t, nr_pages)
		__field(size_t, backed_pages)
		__field(u8, jit_id)
	),
	TP_fast_assign(
		__entry->start_addr   = ((u64)reg->start_pfn) << PAGE_SHIFT;
		__entry->nr_pages     = reg->nr_pages;
		__entry->backed_pages = kbase_reg_current_backed_size(reg);
		__entry->jit_id       = jit_id;
	),
	TP_printk("jit_id=%u start=0x%llx va_pages=0x%zx backed_size=0x%zx",
		__entry->jit_id, __entry->start_addr, __entry->nr_pages,
		__entry->backed_pages)
);

/* trace_mali_jit_alloc()
 *
 * Tracepoint about a just-in-time memory allocation soft-job successfully
 * allocating memory
 */
DEFINE_EVENT(mali_jit_softjob_template, mali_jit_alloc,
	TP_PROTO(struct kbase_va_region *reg, u8 jit_id),
	TP_ARGS(reg, jit_id));

/* trace_mali_jit_free()
 *
 * Tracepoint about memory that was allocated just-in-time being freed
 * (which may happen either on free soft-job, or during rollback error
 * paths of an allocation soft-job, etc)
 *
 * Free doesn't immediately have the just-in-time memory allocation ID so
 * it's currently suppressed from the output - set jit_id to 0
 */
DEFINE_EVENT_PRINT(mali_jit_softjob_template, mali_jit_free,
	TP_PROTO(struct kbase_va_region *reg, u8 jit_id),
	TP_ARGS(reg, jit_id),
	TP_printk("start=0x%llx va_pages=0x%zx backed_size=0x%zx",
		__entry->start_addr, __entry->nr_pages, __entry->backed_pages));

#if !MALI_USE_CSF
#if MALI_JIT_PRESSURE_LIMIT_BASE
/* trace_mali_jit_report
 *
 * Tracepoint about the GPU data structure read to form a just-in-time memory
 * allocation report, and its calculated physical page usage
 */
TRACE_EVENT(mali_jit_report,
	TP_PROTO(struct kbase_jd_atom *katom, struct kbase_va_region *reg,
		unsigned int id_idx, u64 read_val, u64 used_pages),
	TP_ARGS(katom, reg, id_idx, read_val, used_pages),
	TP_STRUCT__entry(
		__field(u64, start_addr)
		__field(u64, read_val)
		__field(u64, used_pages)
		__field(unsigned long, flags)
		__field(u8, id_idx)
		__field(u8, jit_id)
	),
	TP_fast_assign(
		__entry->start_addr = ((u64)reg->start_pfn) << PAGE_SHIFT;
		__entry->read_val   = read_val;
		__entry->used_pages = used_pages;
		__entry->flags      = reg->flags;
		__entry->id_idx     = id_idx;
		__entry->jit_id     = katom->jit_ids[id_idx];
	),
	TP_printk("start=0x%llx jit_ids[%u]=%u read_type='%s' read_val=0x%llx used_pages=%llu",
		__entry->start_addr, __entry->id_idx, __entry->jit_id,
		__print_symbolic(__entry->flags,
			{ 0, "address"},
			{ KBASE_REG_TILER_ALIGN_TOP, "address with align" },
			{ KBASE_REG_HEAP_INFO_IS_SIZE, "size" },
			{ KBASE_REG_HEAP_INFO_IS_SIZE |
				KBASE_REG_TILER_ALIGN_TOP,
				"size with align (invalid)" }
		),
		__entry->read_val, __entry->used_pages)
);
#endif /* MALI_JIT_PRESSURE_LIMIT_BASE */
#endif /* !MALI_USE_CSF */

TRACE_DEFINE_ENUM(KBASE_JIT_REPORT_ON_ALLOC_OR_FREE);
#if MALI_JIT_PRESSURE_LIMIT_BASE
/* trace_mali_jit_report_pressure
 *
 * Tracepoint about change in physical memory pressure, due to the information
 * about a region changing. Examples include:
 * - a report on a region that was allocated just-in-time
 * - just-in-time allocation of a region
 * - free of a region that was allocated just-in-time
 */
TRACE_EVENT(mali_jit_report_pressure,
	TP_PROTO(struct kbase_va_region *reg, u64 new_used_pages,
		u64 new_pressure, unsigned int flags),
	TP_ARGS(reg, new_used_pages, new_pressure, flags),
	TP_STRUCT__entry(
		__field(u64, start_addr)
		__field(u64, used_pages)
		__field(u64, new_used_pages)
		__field(u64, new_pressure)
		__field(unsigned int, flags)
	),
	TP_fast_assign(
		__entry->start_addr     = ((u64)reg->start_pfn) << PAGE_SHIFT;
		__entry->used_pages     = reg->used_pages;
		__entry->new_used_pages = new_used_pages;
		__entry->new_pressure   = new_pressure;
		__entry->flags          = flags;
	),
	TP_printk("start=0x%llx old_used_pages=%llu new_used_pages=%llu new_pressure=%llu report_flags=%s",
		__entry->start_addr, __entry->used_pages,
		__entry->new_used_pages, __entry->new_pressure,
		__print_flags(__entry->flags, "|",
			{ KBASE_JIT_REPORT_ON_ALLOC_OR_FREE,
				"HAPPENED_ON_ALLOC_OR_FREE" }))
);
#endif /* MALI_JIT_PRESSURE_LIMIT_BASE */

#ifndef __TRACE_SYSGRAPH_ENUM
#define __TRACE_SYSGRAPH_ENUM
/* Enum of sysgraph message IDs */
enum sysgraph_msg {
	SGR_ARRIVE,
	SGR_SUBMIT,
	SGR_COMPLETE,
	SGR_POST,
	SGR_ACTIVE,
	SGR_INACTIVE
};
#endif /* __TRACE_SYSGRAPH_ENUM */

/* A template for SYSGRAPH events
 *
 * Most of the sysgraph events contain only one input argument
 * which is atom_id therefore they will be using a common template
 */
TRACE_EVENT(sysgraph,
	TP_PROTO(enum sysgraph_msg message, unsigned int proc_id,
		unsigned int atom_id),
	TP_ARGS(message, proc_id, atom_id),
	TP_STRUCT__entry(
		__field(unsigned int, proc_id)
		__field(enum sysgraph_msg, message)
		__field(unsigned int, atom_id)
	),
	TP_fast_assign(
		__entry->proc_id    = proc_id;
		__entry->message    = message;
		__entry->atom_id    = atom_id;
	),
	TP_printk("msg=%u proc_id=%u, param1=%d", __entry->message,
		 __entry->proc_id,  __entry->atom_id)
);

/* A template for SYSGRAPH GPU events
 *
 * Sysgraph events that record start/complete events
 * on GPU also record a js value in addition to the
 * atom id.
 */
TRACE_EVENT(sysgraph_gpu,
	TP_PROTO(enum sysgraph_msg message, unsigned int proc_id,
		unsigned int atom_id, unsigned int js),
	TP_ARGS(message, proc_id, atom_id, js),
	TP_STRUCT__entry(
		__field(unsigned int, proc_id)
		__field(enum sysgraph_msg, message)
		__field(unsigned int, atom_id)
		__field(unsigned int, js)
	),
	TP_fast_assign(
		__entry->proc_id    = proc_id;
		__entry->message    = message;
		__entry->atom_id    = atom_id;
		__entry->js         = js;
	),
	TP_printk("msg=%u proc_id=%u, param1=%d, param2=%d",
		  __entry->message,  __entry->proc_id,
		  __entry->atom_id, __entry->js)
);

/* Tracepoint files get included more than once - protect against multiple
 * definition
 */
#undef KBASE_JIT_REPORT_GPU_MEM_SIZE

/* Size in bytes of the memory surrounding the location used for a just-in-time
 * memory allocation report
 */
#define KBASE_JIT_REPORT_GPU_MEM_SIZE (4 * sizeof(u64))

/* trace_mali_jit_report_gpu_mem
 *
 * Tracepoint about the GPU memory nearby the location used for a just-in-time
 * memory allocation report
 */
TRACE_EVENT(mali_jit_report_gpu_mem,
	TP_PROTO(u64 base_addr, u64 reg_addr, u64 *gpu_mem, unsigned int flags),
	TP_ARGS(base_addr, reg_addr, gpu_mem, flags),
	TP_STRUCT__entry(
		__field(u64, base_addr)
		__field(u64, reg_addr)
		__array(u64, mem_values,
			KBASE_JIT_REPORT_GPU_MEM_SIZE / sizeof(u64))
		__field(unsigned int, flags)
	),
	TP_fast_assign(
		__entry->base_addr = base_addr;
		__entry->reg_addr  = reg_addr;
		memcpy(__entry->mem_values, gpu_mem,
				sizeof(__entry->mem_values));
		__entry->flags     = flags;
	),
	TP_printk("start=0x%llx read GPU memory base=0x%llx values=%s report_flags=%s",
		__entry->reg_addr, __entry->base_addr,
		__print_array(__entry->mem_values,
				ARRAY_SIZE(__entry->mem_values), sizeof(u64)),
		__print_flags(__entry->flags, "|",
			{ KBASE_JIT_REPORT_ON_ALLOC_OR_FREE,
				"HAPPENED_ON_ALLOC_OR_FREE" }))
);

/* trace_mali_jit_trim_from_region
 *
 * Tracepoint about trimming physical pages from a region
 */
TRACE_EVENT(mali_jit_trim_from_region,
	TP_PROTO(struct kbase_va_region *reg, size_t freed_pages,
		size_t old_pages, size_t available_pages, size_t new_pages),
	TP_ARGS(reg, freed_pages, old_pages, available_pages, new_pages),
	TP_STRUCT__entry(
		__field(u64, start_addr)
		__field(size_t, freed_pages)
		__field(size_t, old_pages)
		__field(size_t, available_pages)
		__field(size_t, new_pages)
	),
	TP_fast_assign(
		__entry->start_addr      = ((u64)reg->start_pfn) << PAGE_SHIFT;
		__entry->freed_pages     = freed_pages;
		__entry->old_pages       = old_pages;
		__entry->available_pages = available_pages;
		__entry->new_pages       = new_pages;
	),
	TP_printk("start=0x%llx freed_pages=%zu old_pages=%zu available_pages=%zu new_pages=%zu",
		__entry->start_addr, __entry->freed_pages, __entry->old_pages,
		__entry->available_pages, __entry->new_pages)
);

/* trace_mali_jit_trim
 *
 * Tracepoint about total trimmed physical pages
 */
TRACE_EVENT(mali_jit_trim,
	TP_PROTO(size_t freed_pages),
	TP_ARGS(freed_pages),
	TP_STRUCT__entry(
		__field(size_t, freed_pages)
	),
	TP_fast_assign(
		__entry->freed_pages  = freed_pages;
	),
	TP_printk("freed_pages=%zu", __entry->freed_pages)
);

#include "debug/mali_kbase_debug_linux_ktrace.h"

#endif /* _TRACE_MALI_H */

#undef TRACE_INCLUDE_PATH
/* lwn.net/Articles/383362 suggests this should remain as '.', and instead
 * extend CFLAGS
 */
#define TRACE_INCLUDE_PATH .
#undef  TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE mali_linux_trace

/* This part must be outside protection */
#include <trace/define_trace.h>
