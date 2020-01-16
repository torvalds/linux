/* SPDX-License-Identifier: GPL-2.0
 * Tracepoints for vfio_ccw driver
 *
 * Copyright IBM Corp. 2018
 *
 * Author(s): Dong Jia Shi <bjsdjshi@linux.vnet.ibm.com>
 *            Halil Pasic <pasic@linux.vnet.ibm.com>
 */

#include "cio.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM vfio_ccw

#if !defined(_VFIO_CCW_TRACE_) || defined(TRACE_HEADER_MULTI_READ)
#define _VFIO_CCW_TRACE_

#include <linux/tracepoint.h>

TRACE_EVENT(vfio_ccw_fsm_async_request,
	TP_PROTO(struct subchannel_id schid,
		 int command,
		 int erryes),
	TP_ARGS(schid, command, erryes),

	TP_STRUCT__entry(
		__field(u8, cssid)
		__field(u8, ssid)
		__field(u16, sch_yes)
		__field(int, command)
		__field(int, erryes)
	),

	TP_fast_assign(
		__entry->cssid = schid.cssid;
		__entry->ssid = schid.ssid;
		__entry->sch_yes = schid.sch_yes;
		__entry->command = command;
		__entry->erryes = erryes;
	),

	TP_printk("schid=%x.%x.%04x command=0x%x erryes=%d",
		  __entry->cssid,
		  __entry->ssid,
		  __entry->sch_yes,
		  __entry->command,
		  __entry->erryes)
);

TRACE_EVENT(vfio_ccw_fsm_event,
	TP_PROTO(struct subchannel_id schid, int state, int event),
	TP_ARGS(schid, state, event),

	TP_STRUCT__entry(
		__field(u8, cssid)
		__field(u8, ssid)
		__field(u16, schyes)
		__field(int, state)
		__field(int, event)
	),

	TP_fast_assign(
		__entry->cssid = schid.cssid;
		__entry->ssid = schid.ssid;
		__entry->schyes = schid.sch_yes;
		__entry->state = state;
		__entry->event = event;
	),

	TP_printk("schid=%x.%x.%04x state=%d event=%d",
		__entry->cssid, __entry->ssid, __entry->schyes,
		__entry->state,
		__entry->event)
);

TRACE_EVENT(vfio_ccw_fsm_io_request,
	TP_PROTO(int fctl, struct subchannel_id schid, int erryes, char *errstr),
	TP_ARGS(fctl, schid, erryes, errstr),

	TP_STRUCT__entry(
		__field(u8, cssid)
		__field(u8, ssid)
		__field(u16, sch_yes)
		__field(int, fctl)
		__field(int, erryes)
		__field(char*, errstr)
	),

	TP_fast_assign(
		__entry->cssid = schid.cssid;
		__entry->ssid = schid.ssid;
		__entry->sch_yes = schid.sch_yes;
		__entry->fctl = fctl;
		__entry->erryes = erryes;
		__entry->errstr = errstr;
	),

	TP_printk("schid=%x.%x.%04x fctl=0x%x erryes=%d info=%s",
		  __entry->cssid,
		  __entry->ssid,
		  __entry->sch_yes,
		  __entry->fctl,
		  __entry->erryes,
		  __entry->errstr)
);

#endif /* _VFIO_CCW_TRACE_ */

/* This part must be outside protection */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE vfio_ccw_trace

#include <trace/define_trace.h>
