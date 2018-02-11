/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Tracepoint header for s390 diagnose calls
 *
 * Copyright IBM Corp. 2015
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM s390

#if !defined(_TRACE_S390_DIAG_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_S390_DIAG_H

#include <linux/tracepoint.h>

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE

#define TRACE_INCLUDE_PATH asm/trace
#define TRACE_INCLUDE_FILE diag

TRACE_EVENT(s390_diagnose,
	TP_PROTO(unsigned short nr),
	TP_ARGS(nr),
	TP_STRUCT__entry(
		__field(unsigned short, nr)
	),
	TP_fast_assign(
		__entry->nr = nr;
	),
	TP_printk("nr=0x%x", __entry->nr)
);

#ifdef CONFIG_TRACEPOINTS
void trace_s390_diagnose_norecursion(int diag_nr);
#else
static inline void trace_s390_diagnose_norecursion(int diag_nr) { }
#endif

#endif /* _TRACE_S390_DIAG_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
