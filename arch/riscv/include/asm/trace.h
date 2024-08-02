/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM riscv

#if !defined(_TRACE_RISCV_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RISCV_H

#include <linux/tracepoint.h>

TRACE_EVENT_CONDITION(sbi_call,
	TP_PROTO(int ext, int fid),
	TP_ARGS(ext, fid),
	TP_CONDITION(ext != SBI_EXT_HSM),

	TP_STRUCT__entry(
		__field(int, ext)
		__field(int, fid)
	),

	TP_fast_assign(
		__entry->ext = ext;
		__entry->fid = fid;
	),

	TP_printk("ext=0x%x fid=%d", __entry->ext, __entry->fid)
);

TRACE_EVENT_CONDITION(sbi_return,
	TP_PROTO(int ext, long error, long value),
	TP_ARGS(ext, error, value),
	TP_CONDITION(ext != SBI_EXT_HSM),

	TP_STRUCT__entry(
		__field(long, error)
		__field(long, value)
	),

	TP_fast_assign(
		__entry->error = error;
		__entry->value = value;
	),

	TP_printk("error=%ld value=0x%lx", __entry->error, __entry->value)
);

#endif /* _TRACE_RISCV_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE

#define TRACE_INCLUDE_PATH asm
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
