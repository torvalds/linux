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
struct gb_operation;
struct gb_host_device;

#define gb_bundle_name(message)						\
	(message->operation->connection->bundle ?			\
	dev_name(&message->operation->connection->bundle->dev) :	\
	dev_name(&message->operation->connection->hd->svc->dev))

DECLARE_EVENT_CLASS(gb_message,

	TP_PROTO(struct gb_message *message),

	TP_ARGS(message),

	TP_STRUCT__entry(
		__string(name, gb_bundle_name(message))
		__field(u16, op_id)
		__field(u16, intf_cport_id)
		__field(u16, hd_cport_id)
		__field(size_t, payload_size)
	),

	TP_fast_assign(
		__assign_str(name, gb_bundle_name(message))
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

#define DEFINE_MESSAGE_EVENT(name)					\
		DEFINE_EVENT(gb_message, name,				\
				TP_PROTO(struct gb_message *message),	\
				TP_ARGS(message))

/*
 * tracepoint name	greybus:gb_message_send
 * description		send a greybus message
 */
DEFINE_MESSAGE_EVENT(gb_message_send);

/*
 * tracepoint name	greybus:gb_message_recv_request
 * description		receive a greybus request
 */
DEFINE_MESSAGE_EVENT(gb_message_recv_request);

/*
 * tracepoint name	greybus:gb_message_recv_response
 * description		receive a greybus response
 */
DEFINE_MESSAGE_EVENT(gb_message_recv_response);

/*
 * tracepoint name	greybus:gb_message_cancel_outgoing
 * description		cancel outgoing greybus request
 */
DEFINE_MESSAGE_EVENT(gb_message_cancel_outgoing);

/*
 * tracepoint name	greybus:gb_message_cancel_incoming
 * description		cancel incoming greybus request
 */
DEFINE_MESSAGE_EVENT(gb_message_cancel_incoming);

#undef DEFINE_MESSAGE_EVENT

DECLARE_EVENT_CLASS(gb_operation,

	TP_PROTO(struct gb_operation *operation),

	TP_ARGS(operation),

	TP_STRUCT__entry(
		__field(u16, cport_id)	/* CPort of HD side of connection */
		__field(u16, id)	/* Operation ID */
		__field(u8, type)
		__field(unsigned long, flags)
		__field(int, active)
		__field(int, waiters)
		__field(int, errno)
	),

	TP_fast_assign(
		__entry->cport_id = operation->connection->hd_cport_id;
		__entry->id = operation->id;
		__entry->type = operation->type;
		__entry->flags = operation->flags;
		__entry->active = operation->active;
		__entry->waiters = atomic_read(&operation->waiters);
		__entry->errno = operation->errno;
	),

	TP_printk("id=%04x type=0x%02x cport_id=%04x flags=0x%lx active=%d waiters=%d errno=%d",
		  __entry->id, __entry->cport_id, __entry->type, __entry->flags,
		  __entry->active, __entry->waiters, __entry->errno)
);

#define DEFINE_OPERATION_EVENT(name)					\
		DEFINE_EVENT(gb_operation, name,			\
				TP_PROTO(struct gb_operation *operation), \
				TP_ARGS(operation))

/*
 * Occurs after a new operation is created for an outgoing request
 * has been successfully created.
 */
DEFINE_OPERATION_EVENT(gb_operation_create);

/*
 * Occurs after a new operation has been created for an incoming
 * request has been successfully created and initialized.
 */
DEFINE_OPERATION_EVENT(gb_operation_create_incoming);

/*
 * Occurs when the last reference to an operation has been dropped,
 * prior to freeing resources.
 */
DEFINE_OPERATION_EVENT(gb_operation_destroy);

/*
 * Occurs when an operation has been marked active, after updating
 * its active count.
 */
DEFINE_OPERATION_EVENT(gb_operation_get_active);

/*
 * Occurs when an operation has been marked active, before updating
 * its active count.
 */
DEFINE_OPERATION_EVENT(gb_operation_put_active);

#undef DEFINE_OPERATION_EVENT

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

#define DEFINE_HD_EVENT(name)						\
		DEFINE_EVENT(gb_host_device, name,			\
				TP_PROTO(struct gb_host_device *hd,	\
					u16 intf_cport_id,		\
					size_t payload_size),		\
				TP_ARGS(hd, intf_cport_id, payload_size))

/*
 * tracepoint name	greybus:gb_host_device_send
 * description		tracepoint representing the point data are transmitted
 */
DEFINE_HD_EVENT(gb_host_device_send);

/*
 * tracepoint name	greybus:gb_host_device_recv
 * description		tracepoint representing the point data are received
 */
DEFINE_HD_EVENT(gb_host_device_recv);

#undef DEFINE_HD_EVENT

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

