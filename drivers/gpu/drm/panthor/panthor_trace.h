/* SPDX-License-Identifier: GPL-2.0 or MIT */
/* Copyright 2025 Collabora ltd. */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM panthor

#if !defined(__PANTHOR_TRACE_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __PANTHOR_TRACE_H__

#include <linux/tracepoint.h>
#include <linux/types.h>

#include "panthor_hw.h"

/**
 * gpu_power_status - called whenever parts of GPU hardware are turned on or off
 * @dev: pointer to the &struct device, for printing the device name
 * @shader_bitmap: bitmap where a high bit indicates the shader core at a given
 *                 bit index is on, and a low bit indicates a shader core is
 *                 either powered off or absent
 * @tiler_bitmap: bitmap where a high bit indicates the tiler unit at a given
 *                bit index is on, and a low bit indicates a tiler unit is
 *                either powered off or absent
 * @l2_bitmap: bitmap where a high bit indicates the L2 cache at a given bit
 *             index is on, and a low bit indicates the L2 cache is either
 *             powered off or absent
 */
TRACE_EVENT_FN(gpu_power_status,
	TP_PROTO(const struct device *dev, u64 shader_bitmap, u64 tiler_bitmap,
		 u64 l2_bitmap),
	TP_ARGS(dev, shader_bitmap, tiler_bitmap, l2_bitmap),
	TP_STRUCT__entry(
		__string(dev_name, dev_name(dev))
		__field(u64, shader_bitmap)
		__field(u64, tiler_bitmap)
		__field(u64, l2_bitmap)
	),
	TP_fast_assign(
		__assign_str(dev_name);
		__entry->shader_bitmap	= shader_bitmap;
		__entry->tiler_bitmap	= tiler_bitmap;
		__entry->l2_bitmap	= l2_bitmap;
	),
	TP_printk("%s: shader_bitmap=0x%llx tiler_bitmap=0x%llx l2_bitmap=0x%llx",
		  __get_str(dev_name), __entry->shader_bitmap, __entry->tiler_bitmap,
		  __entry->l2_bitmap
	),
	panthor_hw_power_status_register, panthor_hw_power_status_unregister
);

/**
 * gpu_job_irq - called after a job interrupt from firmware completes
 * @dev: pointer to the &struct device, for printing the device name
 * @events: bitmask of BIT(CSG id) | BIT(31) for a global event
 * @duration_ns: Nanoseconds between job IRQ handler entry and exit
 *
 * The panthor_job_irq_handler() function instrumented by this tracepoint exits
 * once it has queued the firmware interrupts for processing, not when the
 * firmware interrupts are fully processed. This tracepoint allows for debugging
 * issues with delays in the workqueue's processing of events.
 */
TRACE_EVENT(gpu_job_irq,
	TP_PROTO(const struct device *dev, u32 events, u32 duration_ns),
	TP_ARGS(dev, events, duration_ns),
	TP_STRUCT__entry(
		__string(dev_name, dev_name(dev))
		__field(u32, events)
		__field(u32, duration_ns)
	),
	TP_fast_assign(
		__assign_str(dev_name);
		__entry->events		= events;
		__entry->duration_ns	= duration_ns;
	),
	TP_printk("%s: events=0x%x duration_ns=%d", __get_str(dev_name),
		  __entry->events, __entry->duration_ns)
);

#endif /* __PANTHOR_TRACE_H__ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE panthor_trace

#include <trace/define_trace.h>
