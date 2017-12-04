/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM emulation

#if !defined(_TRACE_EMULATION_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_EMULATION_H

#include <linux/tracepoint.h>

TRACE_EVENT(instruction_emulation,

	TP_PROTO(const char *instr, u64 addr),
	TP_ARGS(instr, addr),

	TP_STRUCT__entry(
		__string(instr, instr)
		__field(u64, addr)
	),

	TP_fast_assign(
		__assign_str(instr, instr);
		__entry->addr = addr;
	),

	TP_printk("instr=\"%s\" addr=0x%llx", __get_str(instr), __entry->addr)
);

#endif /* _TRACE_EMULATION_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .

#define TRACE_INCLUDE_FILE trace-events-emulation
#include <trace/define_trace.h>
