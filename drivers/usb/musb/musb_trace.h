/*
 * musb_trace.h - MUSB Controller Trace Support
 *
 * Copyright (C) 2015 Texas Instruments Incorporated - http://www.ti.com
 *
 * Author: Bin Liu <b-liu@ti.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM musb

#if !defined(__MUSB_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __MUSB_TRACE_H

#include <linux/types.h>
#include <linux/tracepoint.h>
#include "musb_core.h"

#define MUSB_MSG_MAX   500

TRACE_EVENT(musb_log,
	TP_PROTO(struct musb *musb, struct va_format *vaf),
	TP_ARGS(musb, vaf),
	TP_STRUCT__entry(
		__string(name, dev_name(musb->controller))
		__dynamic_array(char, msg, MUSB_MSG_MAX)
	),
	TP_fast_assign(
		__assign_str(name, dev_name(musb->controller));
		vsnprintf(__get_str(msg), MUSB_MSG_MAX, vaf->fmt, *vaf->va);
	),
	TP_printk("%s: %s", __get_str(name), __get_str(msg))
);

#endif /* __MUSB_TRACE_H */

/* this part has to be here */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE musb_trace

#include <trace/define_trace.h>
