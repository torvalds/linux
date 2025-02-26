/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright © 2024 Intel Corporation */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM i915

#if !defined(__INTEL_UNCORE_TRACE_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __INTEL_UNCORE_TRACE_H__

#include "i915_reg_defs.h"

#include <linux/types.h>
#include <linux/tracepoint.h>

TRACE_EVENT_CONDITION(i915_reg_rw,
	TP_PROTO(bool write, i915_reg_t reg, u64 val, int len, bool trace),

	TP_ARGS(write, reg, val, len, trace),

	TP_CONDITION(trace),

	TP_STRUCT__entry(
		__field(u64, val)
		__field(u32, reg)
		__field(u16, write)
		__field(u16, len)
		),

	TP_fast_assign(
		__entry->val = (u64)val;
		__entry->reg = i915_mmio_reg_offset(reg);
		__entry->write = write;
		__entry->len = len;
		),

	TP_printk("%s reg=0x%x, len=%d, val=(0x%x, 0x%x)",
		__entry->write ? "write" : "read",
		__entry->reg, __entry->len,
		(u32)(__entry->val & 0xffffffff),
		(u32)(__entry->val >> 32))
);
#endif /* __INTEL_UNCORE_TRACE_H__ */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH ../../drivers/gpu/drm/i915
#define TRACE_INCLUDE_FILE intel_uncore_trace
#include <trace/define_trace.h>
