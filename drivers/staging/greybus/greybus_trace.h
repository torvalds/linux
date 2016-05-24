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
 * Occurs immediately before calling a host device's message_send()
 * method.
 */
DEFINE_MESSAGE_EVENT(gb_message_send);

/*
 * Occurs after an incoming request message has been received
 */
DEFINE_MESSAGE_EVENT(gb_message_recv_request);

/*
 * Occurs after an incoming response message has been received,
 * after its matching request has been found.
 */
DEFINE_MESSAGE_EVENT(gb_message_recv_response);

/*
 * Occurs after an operation has been canceled, possibly before the
 * cancellation is complete.
 */
DEFINE_MESSAGE_EVENT(gb_message_cancel_outgoing);

/*
 * Occurs when an incoming request is cancelled; if the response has
 * been queued for sending, this occurs after it is sent.
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

DECLARE_EVENT_CLASS(gb_module,

	TP_PROTO(struct gb_module *module),

	TP_ARGS(module),

	TP_STRUCT__entry(
		__field(int, hd_bus_id)
		__field(u8, module_id)
		__field(u8, num_interfaces)
		__field(bool, disconnected)
	),

	TP_fast_assign(
		__entry->hd_bus_id = module->hd->bus_id;
		__entry->module_id = module->module_id;
		__entry->disconnected = module->disconnected;
	),

	TP_printk("greybus: hd_bus_id=%d module_id=%hhu disconnected=%u",
		__entry->hd_bus_id, __entry->module_id, __entry->disconnected)
);

#define DEFINE_MODULE_EVENT(name)					\
		DEFINE_EVENT(gb_module, name,				\
				TP_PROTO(struct gb_module *module),	\
				TP_ARGS(module))

/*
 * Occurs after a new module is successfully created, before
 * creating any of its interfaces.
 */
DEFINE_MODULE_EVENT(gb_module_create);

/*
 * Occurs after the last reference to a module has been dropped.
 */
DEFINE_MODULE_EVENT(gb_module_release);

/*
 * Occurs after a module is successfully created, before registering
 * any of its interfaces.
 */
DEFINE_MODULE_EVENT(gb_module_add);

/*
 * Occurs when a module is deleted, before deregistering its
 * interfaces.
 */
DEFINE_MODULE_EVENT(gb_module_del);

#undef DEFINE_MODULE_EVENT

DECLARE_EVENT_CLASS(gb_host_device,

	TP_PROTO(struct gb_host_device *hd),

	TP_ARGS(hd),

	TP_STRUCT__entry(
		__field(int, bus_id)
		__field(u8, num_cports)
		__field(size_t, buffer_size_max)
	),

	TP_fast_assign(
		__entry->bus_id = hd->bus_id;
		__entry->num_cports = hd->num_cports;
		__entry->buffer_size_max = hd->buffer_size_max;
	),

	TP_printk("greybus: bus_id=%d num_cports=%hu mtu=%zu",
		__entry->bus_id, __entry->num_cports,
		__entry->buffer_size_max)
);

#define DEFINE_HD_EVENT(name)						\
		DEFINE_EVENT(gb_host_device, name,			\
				TP_PROTO(struct gb_host_device *hd),	\
				TP_ARGS(hd))

/*
 * Occurs after a new host device is successfully created, before
 * its SVC has been set up.
 */
DEFINE_HD_EVENT(gb_hd_create);

/*
 * Occurs after the last reference to a host device has been
 * dropped.
 */
DEFINE_HD_EVENT(gb_hd_release);

/*
 * Occurs after a new host device has been added, after the
 * connection to its SVC has * been enabled.
 */
DEFINE_HD_EVENT(gb_hd_add);

/*
 * Occurs when a host device is being disconnected from the AP USB
 * host controller.
 */
DEFINE_HD_EVENT(gb_hd_del);

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

