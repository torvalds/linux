/* SPDX-License-Identifier: GPL-2.0
 * Tracepoints for vfio_ccw driver
 *
 * Copyright IBM Corp. 2018
 *
 * Author(s): Dong Jia Shi <bjsdjshi@linux.vnet.ibm.com>
 *            Halil Pasic <pasic@linux.vnet.ibm.com>
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM vfio_ccw

#if !defined(_VFIO_CCW_TRACE_) || defined(TRACE_HEADER_MULTI_READ)
#define _VFIO_CCW_TRACE_

#include <linux/tracepoint.h>

TRACE_EVENT(vfio_ccw_io_fctl,
	TP_PROTO(int fctl, struct subchannel_id schid, int errno, char *errstr),
	TP_ARGS(fctl, schid, errno, errstr),

	TP_STRUCT__entry(
		__field(int, fctl)
		__field_struct(struct subchannel_id, schid)
		__field(int, errno)
		__field(char*, errstr)
	),

	TP_fast_assign(
		__entry->fctl = fctl;
		__entry->schid = schid;
		__entry->errno = errno;
		__entry->errstr = errstr;
	),

	TP_printk("schid=%x.%x.%04x fctl=%x errno=%d info=%s",
		  __entry->schid.cssid,
		  __entry->schid.ssid,
		  __entry->schid.sch_no,
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
