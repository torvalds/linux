/*
 * Greybus driver and device API
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM greybus

#if !defined(_TRACE_GREYBUS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_GREYBUS_H

#include <linux/tracepoint.h>

struct gb_message;
struct gb_host_device;

DECLARE_EVENT_CLASS(gb_message,

	TP_PROTO(struct gb_message *message),

	TP_ARGS(message),

	TP_STRUCT__entry(
		__string(name, dev_name(&message->operation->connection->bundle->dev))
		__field(u16, op_id)
		__field(u16, intf_cport_id)
		__field(u16, hd_cport_id)
		__field(size_t, payload_size)
	),

	TP_fast_assign(
		__assign_str(name, dev_name(&message->operation->connection->bundle->dev))
		__entry->op_id = message->operation->id;
		__entry->intf_cport_id =
			message->operation->connection->intf_cport_id;
		__entry->hd_cport_id =
			message->operation->connection->hd_cport_id;
		__entry->payload_size = message->payload_size;
	),

	TP_printk("greybus:%s op=%04x if_id=%u hd_id=%u l=%zu",
		  __get_str(name), __entry->op_id, __entry->intf_cport_id,
		  __entry->hd_cport_id, __entry->payload_size)
);

/*
 * tracepoint name	greybus:gb_message_send
 * description		send a greybus message
 * location		operation.c:gb_message_send
 */
DEFINE_EVENT(gb_message, gb_message_send,

	TP_PROTO(struct gb_message *message),

	TP_ARGS(message)
);

/*
 * tracepoint name	greybus:gb_message_recv_request
 * description		receive a greybus request
 * location		operation.c:gb_connection_recv_request
 */
DEFINE_EVENT(gb_message, gb_message_recv_request,

	TP_PROTO(struct gb_message *message),

	TP_ARGS(message)
);

/*
 * tracepoint name	greybus:gb_message_recv_response
 * description		receive a greybus response
 * location		operation.c:gb_connection_recv_response
 */
DEFINE_EVENT(gb_message, gb_message_recv_response,

	TP_PROTO(struct gb_message *message),

	TP_ARGS(message)
);

/*
 * tracepoint name	greybus:gb_message_cancel_outgoing
 * description		cancel outgoing greybus request
 * location		operation.c:gb_message_cancel
 */
DEFINE_EVENT(gb_message, gb_message_cancel_outgoing,

	TP_PROTO(struct gb_message *message),

	TP_ARGS(message)
);

/*
 * tracepoint name	greybus:gb_message_cancel_incoming
 * description		cancel incoming greybus request
 * location		operation.c:gb_message_cancel_incoming
 */
DEFINE_EVENT(gb_message, gb_message_cancel_incoming,

	TP_PROTO(struct gb_message *message),

	TP_ARGS(message)
);

DECLARE_EVENT_CLASS(gb_host_device,

	TP_PROTO(struct gb_host_device *hd, u16 intf_cport_id,
		 size_t payload_size),

	TP_ARGS(hd, intf_cport_id, payload_size),

	TP_STRUCT__entry(
		__string(name, dev_name(&hd->dev))
		__field(u16, intf_cport_id)
		__field(size_t, payload_size)
	),

	TP_fast_assign(
		__assign_str(name, dev_name(&hd->dev))
		__entry->intf_cport_id = intf_cport_id;
		__entry->payload_size = payload_size;
	),

	TP_printk("greybus:%s if_id=%u l=%zu", __get_str(name),
		  __entry->intf_cport_id, __entry->payload_size)
);

/*
 * tracepoint name	greybus:gb_host_device_send
 * description		tracepoint representing the point data are transmitted
 * location		es2.c:message_send
 */
DEFINE_EVENT(gb_host_device, gb_host_device_send,

	TP_PROTO(struct gb_host_device *hd, u16 intf_cport_id,
		 size_t payload_size),

	TP_ARGS(hd, intf_cport_id, payload_size)
);

/*
 * tracepoint name	greybus:gb_host_device_recv
 * description		tracepoint representing the point data are received
 * location		es2.c:cport_in_callback
 */
DEFINE_EVENT(gb_host_device, gb_host_device_recv,

	TP_PROTO(struct gb_host_device *hd, u16 intf_cport_id,
		 size_t payload_size),

	TP_ARGS(hd, intf_cport_id, payload_size)
);

#endif /* _TRACE_GREYBUS_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

/*
 * TRACE_INCLUDE_FILE is not needed if the filename and TRACE_SYSTEM are equal
 */
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE greybus_trace
#include <trace/define_trace.h>

