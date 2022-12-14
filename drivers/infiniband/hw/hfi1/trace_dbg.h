/* SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause */
/*
* Copyright(c) 2015 - 2018 Intel Corporation.
*/

#if !defined(__HFI1_TRACE_EXTRA_H) || defined(TRACE_HEADER_MULTI_READ)
#define __HFI1_TRACE_EXTRA_H

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

#include "hfi.h"

/*
 * Note:
 * This produces a REALLY ugly trace in the console output when the string is
 * too long.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hfi1_dbg

#define MAX_MSG_LEN 512

DECLARE_EVENT_CLASS(hfi1_trace_template,
		    TP_PROTO(const char *function, struct va_format *vaf),
		    TP_ARGS(function, vaf),
		    TP_STRUCT__entry(__string(function, function)
				     __vstring(msg, vaf->fmt, vaf->va)
				     ),
		    TP_fast_assign(__assign_str(function, function);
				   __assign_vstr(msg, vaf->fmt, vaf->va);
				   ),
		    TP_printk("(%s) %s",
			      __get_str(function),
			      __get_str(msg))
);

/*
 * It may be nice to macroize the __hfi1_trace but the va_* stuff requires an
 * actual function to work and can not be in a macro.
 */
#define __hfi1_trace_def(lvl) \
void __printf(2, 3) __hfi1_trace_##lvl(const char *funct, char *fmt, ...); \
									\
DEFINE_EVENT(hfi1_trace_template, hfi1_ ##lvl,				\
	TP_PROTO(const char *function, struct va_format *vaf),		\
	TP_ARGS(function, vaf))

#define __hfi1_trace_fn(lvl) \
void __printf(2, 3) __hfi1_trace_##lvl(const char *func, char *fmt, ...)\
{									\
	struct va_format vaf = {					\
		.fmt = fmt,						\
	};								\
	va_list args;							\
									\
	va_start(args, fmt);						\
	vaf.va = &args;							\
	trace_hfi1_ ##lvl(func, &vaf);					\
	va_end(args);							\
	return;								\
}

/*
 * To create a new trace level simply define it below and as a __hfi1_trace_fn
 * in trace.c. This will create all the hooks for calling
 * hfi1_cdbg(LVL, fmt, ...); as well as take care of all
 * the debugfs stuff.
 */
__hfi1_trace_def(AFFINITY);
__hfi1_trace_def(PKT);
__hfi1_trace_def(PROC);
__hfi1_trace_def(SDMA);
__hfi1_trace_def(LINKVERB);
__hfi1_trace_def(DEBUG);
__hfi1_trace_def(SNOOP);
__hfi1_trace_def(CNTR);
__hfi1_trace_def(PIO);
__hfi1_trace_def(DC8051);
__hfi1_trace_def(FIRMWARE);
__hfi1_trace_def(RCVCTRL);
__hfi1_trace_def(TID);
__hfi1_trace_def(MMU);
__hfi1_trace_def(IOCTL);

#define hfi1_cdbg(which, fmt, ...) \
	__hfi1_trace_##which(__func__, fmt, ##__VA_ARGS__)

#define hfi1_dbg(fmt, ...) \
	hfi1_cdbg(DEBUG, fmt, ##__VA_ARGS__)

/*
 * Define HFI1_EARLY_DBG at compile time or here to enable early trace
 * messages. Do not check in an enablement for this.
 */

#ifdef HFI1_EARLY_DBG
#define hfi1_dbg_early(fmt, ...) \
	trace_printk(fmt, ##__VA_ARGS__)
#else
#define hfi1_dbg_early(fmt, ...)
#endif

#endif /* __HFI1_TRACE_EXTRA_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_dbg
#include <trace/define_trace.h>
