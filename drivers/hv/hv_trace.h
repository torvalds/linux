#undef TRACE_SYSTEM
#define TRACE_SYSTEM hyperv

#if !defined(_HV_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _HV_TRACE_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(vmbus_hdr_msg,
	TP_PROTO(const struct vmbus_channel_message_header *hdr),
	TP_ARGS(hdr),
	TP_STRUCT__entry(__field(unsigned int, msgtype)),
	TP_fast_assign(__entry->msgtype = hdr->msgtype;),
	TP_printk("msgtype=%u", __entry->msgtype)
);

DEFINE_EVENT(vmbus_hdr_msg, vmbus_on_msg_dpc,
	TP_PROTO(const struct vmbus_channel_message_header *hdr),
	TP_ARGS(hdr)
);

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE hv_trace
#endif /* _HV_TRACE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
