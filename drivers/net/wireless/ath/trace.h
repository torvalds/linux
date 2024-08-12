/*
 * Copyright (c) 2014 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#if !defined(_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_H

#include <linux/tracepoint.h>
#include "ath.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM ath

#if !defined(CONFIG_ATH_TRACEPOINTS)

#undef TRACE_EVENT
#define TRACE_EVENT(name, proto, ...) static inline void trace_ ## name(proto) {}

#endif /* CONFIG_ATH_TRACEPOINTS */

TRACE_EVENT(ath_log,

	    TP_PROTO(struct wiphy *wiphy,
		     struct va_format *vaf),

	    TP_ARGS(wiphy, vaf),

	    TP_STRUCT__entry(
		    __string(device, wiphy_name(wiphy))
		    __string(driver, KBUILD_MODNAME)
		    __vstring(msg, vaf->fmt, vaf->va)
	    ),

	    TP_fast_assign(
		    __assign_str(device);
		    __assign_str(driver);
		    __assign_vstr(msg, vaf->fmt, vaf->va);
	    ),

	    TP_printk(
		    "%s %s %s",
		    __get_str(driver),
		    __get_str(device),
		    __get_str(msg)
	    )
);

#endif /* _TRACE_H || TRACE_HEADER_MULTI_READ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>
