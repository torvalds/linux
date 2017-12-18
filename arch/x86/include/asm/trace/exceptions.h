/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM exceptions

#if !defined(_TRACE_PAGE_FAULT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PAGE_FAULT_H

#include <linux/tracepoint.h>
#include <asm/trace/common.h>

extern int trace_pagefault_reg(void);
extern void trace_pagefault_unreg(void);

DECLARE_EVENT_CLASS(x86_exceptions,

	TP_PROTO(unsigned long address, struct pt_regs *regs,
		 unsigned long error_code),

	TP_ARGS(address, regs, error_code),

	TP_STRUCT__entry(
		__field(		unsigned long, address	)
		__field(		unsigned long, ip	)
		__field(		unsigned long, error_code )
	),

	TP_fast_assign(
		__entry->address = address;
		__entry->ip = regs->ip;
		__entry->error_code = error_code;
	),

	TP_printk("address=%pf ip=%pf error_code=0x%lx",
		  (void *)__entry->address, (void *)__entry->ip,
		  __entry->error_code) );

#define DEFINE_PAGE_FAULT_EVENT(name)				\
DEFINE_EVENT_FN(x86_exceptions, name,				\
	TP_PROTO(unsigned long address,	struct pt_regs *regs,	\
		 unsigned long error_code),			\
	TP_ARGS(address, regs, error_code),			\
	trace_pagefault_reg, trace_pagefault_unreg);

DEFINE_PAGE_FAULT_EVENT(page_fault_user);
DEFINE_PAGE_FAULT_EVENT(page_fault_kernel);

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE exceptions
#endif /*  _TRACE_PAGE_FAULT_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
