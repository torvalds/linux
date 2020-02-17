/* SPDX-License-Identifier: GPL-2.0 */
/* Tracepoints for vfio_ccw driver
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
		 int errno),
	TP_ARGS(schid, command, errno),

	TP_STRUCT__entry(
		__field(u8, cssid)
		__field(u8, ssid)
		__field(u16, sch_no)
		__field(int, command)
		__field(int, errno)
	),

	TP_fast_assign(
		__entry->cssid = schid.cssid;
		__entry->ssid = schid.ssid;
		__entry->sch_no = schid.sch_no;
		__entry->command = command;
		__entry->errno = errno;
	),

	TP_printk("schid=%x.%x.%04x command=0x%x errno=%d",
		  __entry->cssid,
		  __entry->ssid,
		  __entry->sch_no,
		  __entry->command,
		  __entry->errno)
);

TRACE_EVENT(vfio_ccw_fsm_event,
	TP_PROTO(struct subchannel_id schid, int state, int event),
	TP_ARGS(schid, state, event),

	TP_STRUCT__entry(
		__field(u8, cssid)
		__field(u8, ssid)
		__field(u16, schno)
		__field(int, state)
		__field(int, event)
	),

	TP_fast_assign(
		__entry->cssid = schid.cssid;
		__entry->ssid = schid.ssid;
		__entry->schno = schid.sch_no;
		__entry->state = state;
		__entry->event = event;
	),

	TP_printk("schid=%x.%x.%04x state=%d event=%d",
		__entry->cssid, __entry->ssid, __entry->schno,
		__entry->state,
		__entry->event)
);

TRACE_EVENT(vfio_ccw_fsm_io_request,
	TP_PROTO(int fctl, struct subchannel_id schid, int errno, char *errstr),
	TP_ARGS(fctl, schid, errno, errstr),

	TP_STRUCT__entry(
		__field(u8, cssid)
		__field(u8, ssid)
		__field(u16, sch_no)
		__field(int, fctl)
		__field(int, errno)
		__field(char*, errstr)
	),

	TP_fast_assign(
		__entry->cssid = schid.cssid;
		__entry->ssid = schid.ssid;
		__entry->sch_no = schid.sch_no;
		__entry->fctl = fctl;
		__entry->errno = errno;
		__entry->errstr = errstr;
	),

	TP_printk("schid=%x.%x.%04x fctl=0x%x errno=%d info=%s",
		  __entry->cssid,
		  __entry->ssid,
		  __entry->sch_no,
		  __entry->fctl,
		  __entry->errno,
		  __entry->errstr)
);

#endif /* _VFIO_CCW_TRACE_ */

/* This part must be outside protection */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE vfio_ccw_trace

#include <trace/define_trace.h>
