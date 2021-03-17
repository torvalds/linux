/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM amdgpu_dm

#if !defined(_AMDGPU_DM_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _AMDGPU_DM_TRACE_H_

#include <linux/tracepoint.h>

TRACE_EVENT(amdgpu_dc_rreg,
	TP_PROTO(unsigned long *read_count, uint32_t reg, uint32_t value),
	TP_ARGS(read_count, reg, value),
	TP_STRUCT__entry(
			__field(uint32_t, reg)
			__field(uint32_t, value)
		),
	TP_fast_assign(
			__entry->reg = reg;
			__entry->value = value;
			*read_count = *read_count + 1;
		),
	TP_printk("reg=0x%08lx, value=0x%08lx",
			(unsigned long)__entry->reg,
			(unsigned long)__entry->value)
);

TRACE_EVENT(amdgpu_dc_wreg,
	TP_PROTO(unsigned long *write_count, uint32_t reg, uint32_t value),
	TP_ARGS(write_count, reg, value),
	TP_STRUCT__entry(
			__field(uint32_t, reg)
			__field(uint32_t, value)
		),
	TP_fast_assign(
			__entry->reg = reg;
			__entry->value = value;
			*write_count = *write_count + 1;
		),
	TP_printk("reg=0x%08lx, value=0x%08lx",
			(unsigned long)__entry->reg,
			(unsigned long)__entry->value)
);


TRACE_EVENT(amdgpu_dc_performance,
	TP_PROTO(unsigned long read_count, unsigned long write_count,
		unsigned long *last_read, unsigned long *last_write,
		const char *func, unsigned int line),
	TP_ARGS(read_count, write_count, last_read, last_write, func, line),
	TP_STRUCT__entry(
			__field(uint32_t, reads)
			__field(uint32_t, writes)
			__field(uint32_t, read_delta)
			__field(uint32_t, write_delta)
			__string(func, func)
			__field(uint32_t, line)
			),
	TP_fast_assign(
			__entry->reads = read_count;
			__entry->writes = write_count;
			__entry->read_delta = read_count - *last_read;
			__entry->write_delta = write_count - *last_write;
			__assign_str(func, func);
			__entry->line = line;
			*last_read = read_count;
			*last_write = write_count;
			),
	TP_printk("%s:%d reads=%08ld (%08ld total), writes=%08ld (%08ld total)",
			__get_str(func), __entry->line,
			(unsigned long)__entry->read_delta,
			(unsigned long)__entry->reads,
			(unsigned long)__entry->write_delta,
			(unsigned long)__entry->writes)
);
#endif /* _AMDGPU_DM_TRACE_H_ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE amdgpu_dm_trace
#include <trace/define_trace.h>
