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
struct gb_connection;
struct gb_bundle;
struct gb_host_device;

DECLARE_EVENT_CLASS(gb_message,

	TP_PROTO(struct gb_message *message),

	TP_ARGS(message),

	TP_STRUCT__entry(
		__field(u16, size)
		__field(u16, operation_id)
		__field(u8, type)
		__field(u8, result)
	),

	TP_fast_assign(
		__entry->size = le16_to_cpu(message->header->size);
		__entry->operation_id =
			le16_to_cpu(message->header->operation_id);
		__entry->type = message->header->type;
		__entry->result = message->header->result;
	),

	TP_printk("size=%hu operation_id=0x%04x type=0x%02x result=0x%02x",
		  __entry->size, __entry->operation_id,
		  __entry->type, __entry->result)
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

/*
 * Occurs in the host driver message_send() function just prior to
 * handing off the data to be processed by hardware.
 */
DEFINE_MESSAGE_EVENT(gb_message_submit);

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
 * Occurs after a new core operation has been created.
 */
DEFINE_OPERATION_EVENT(gb_operation_create_core);

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

DECLARE_EVENT_CLASS(gb_connection,

	TP_PROTO(struct gb_connection *connection),

	TP_ARGS(connection),

	TP_STRUCT__entry(
		__field(int, hd_bus_id)
		__field(u8, bundle_id)
		/* name contains "hd_cport_id/intf_id:cport_id" */
		__dynamic_array(char, name, sizeof(connection->name))
		__field(enum gb_connection_state, state)
		__field(unsigned long, flags)
	),

	TP_fast_assign(
		__entry->hd_bus_id = connection->hd->bus_id;
		__entry->bundle_id = connection->bundle ?
				connection->bundle->id : BUNDLE_ID_NONE;
		memcpy(__get_str(name), connection->name,
					sizeof(connection->name));
		__entry->state = connection->state;
		__entry->flags = connection->flags;
	),

	TP_printk("hd_bus_id=%d bundle_id=0x%02x name=\"%s\" state=%u flags=0x%lx",
		  __entry->hd_bus_id, __entry->bundle_id, __get_str(name),
		  (unsigned int)__entry->state, __entry->flags)
);

#define DEFINE_CONNECTION_EVENT(name)					\
		DEFINE_EVENT(gb_connection, name,			\
				TP_PROTO(struct gb_connection *connection), \
				TP_ARGS(connection))

/*
 * Occurs after a new connection is successfully created.
 */
DEFINE_CONNECTION_EVENT(gb_connection_create);

/*
 * Occurs when the last reference to a connection has been dropped,
 * before its resources are freed.
 */
DEFINE_CONNECTION_EVENT(gb_connection_release);

/*
 * Occurs when a new reference to connection is added, currently
 * only when a message over the connection is received.
 */
DEFINE_CONNECTION_EVENT(gb_connection_get);

/*
 * Occurs when a new reference to connection is dropped, after a
 * a received message is handled, or when the connection is
 * destroyed.
 */
DEFINE_CONNECTION_EVENT(gb_connection_put);

/*
 * Occurs when a request to enable a connection is made, either for
 * transmit only, or for both transmit and receive.
 */
DEFINE_CONNECTION_EVENT(gb_connection_enable);

/*
 * Occurs when a request to disable a connection is made, either for
 * receive only, or for both transmit and receive.  Also occurs when
 * a request to forcefully disable a connection is made.
 */
DEFINE_CONNECTION_EVENT(gb_connection_disable);

#undef DEFINE_CONNECTION_EVENT

DECLARE_EVENT_CLASS(gb_bundle,

	TP_PROTO(struct gb_bundle *bundle),

	TP_ARGS(bundle),

	TP_STRUCT__entry(
		__field(u8, intf_id)
		__field(u8, id)
		__field(u8, class)
		__field(size_t, num_cports)
	),

	TP_fast_assign(
		__entry->intf_id = bundle->intf->interface_id;
		__entry->id = bundle->id;
		__entry->class = bundle->class;
		__entry->num_cports = bundle->num_cports;
	),

	TP_printk("intf_id=0x%02x id=%02x class=0x%02x num_cports=%zu",
		  __entry->intf_id, __entry->id, __entry->class,
		  __entry->num_cports)
);

#define DEFINE_BUNDLE_EVENT(name)					\
		DEFINE_EVENT(gb_bundle, name,			\
				TP_PROTO(struct gb_bundle *bundle), \
				TP_ARGS(bundle))

/*
 * Occurs after a new bundle is successfully created.
 */
DEFINE_BUNDLE_EVENT(gb_bundle_create);

/*
 * Occurs when the last reference to a bundle has been dropped,
 * before its resources are freed.
 */
DEFINE_BUNDLE_EVENT(gb_bundle_release);

/*
 * Occurs when a bundle is added to an interface when the interface
 * is enabled.
 */
DEFINE_BUNDLE_EVENT(gb_bundle_add);

/*
 * Occurs when a registered bundle gets destroyed, normally at the
 * time an interface is disabled.
 */
DEFINE_BUNDLE_EVENT(gb_bundle_destroy);

#undef DEFINE_BUNDLE_EVENT

DECLARE_EVENT_CLASS(gb_interface,

	TP_PROTO(struct gb_interface *intf),

	TP_ARGS(intf),

	TP_STRUCT__entry(
		__field(u8, module_id)
		__field(u8, id)		/* Interface id */
		__field(u8, device_id)
		__field(int, disconnected)	/* bool */
		__field(int, ejected)		/* bool */
		__field(int, active)		/* bool */
		__field(int, enabled)		/* bool */
		__field(int, mode_switch)	/* bool */
	),

	TP_fast_assign(
		__entry->module_id = intf->module->module_id;
		__entry->id = intf->interface_id;
		__entry->device_id = intf->device_id;
		__entry->disconnected = intf->disconnected;
		__entry->ejected = intf->ejected;
		__entry->active = intf->active;
		__entry->enabled = intf->enabled;
		__entry->mode_switch = intf->mode_switch;
	),

	TP_printk("intf_id=%hhu device_id=%hhu module_id=%hhu D=%d J=%d A=%d E=%d M=%d",
		__entry->id, __entry->device_id, __entry->module_id,
		__entry->disconnected, __entry->ejected, __entry->active,
		__entry->enabled, __entry->mode_switch)
);

#define DEFINE_INTERFACE_EVENT(name)					\
		DEFINE_EVENT(gb_interface, name,			\
				TP_PROTO(struct gb_interface *intf),	\
				TP_ARGS(intf))

/*
 * Occurs after a new interface is successfully created.
 */
DEFINE_INTERFACE_EVENT(gb_interface_create);

/*
 * Occurs after the last reference to an interface has been dropped.
 */
DEFINE_INTERFACE_EVENT(gb_interface_release);

/*
 * Occurs after an interface been registerd.
 */
DEFINE_INTERFACE_EVENT(gb_interface_add);

/*
 * Occurs when a registered interface gets deregisterd.
 */
DEFINE_INTERFACE_EVENT(gb_interface_del);

/*
 * Occurs when a registered interface has been successfully
 * activated.
 */
DEFINE_INTERFACE_EVENT(gb_interface_activate);

/*
 * Occurs when an activated interface is being deactivated.
 */
DEFINE_INTERFACE_EVENT(gb_interface_deactivate);

/*
 * Occurs when an interface has been successfully enabled.
 */
DEFINE_INTERFACE_EVENT(gb_interface_enable);

/*
 * Occurs when an enabled interface is being disabled.
 */
DEFINE_INTERFACE_EVENT(gb_interface_disable);

#undef DEFINE_INTERFACE_EVENT

DECLARE_EVENT_CLASS(gb_module,

	TP_PROTO(struct gb_module *module),

	TP_ARGS(module),

	TP_STRUCT__entry(
		__field(int, hd_bus_id)
		__field(u8, module_id)
		__field(size_t, num_interfaces)
		__field(int, disconnected)	/* bool */
	),

	TP_fast_assign(
		__entry->hd_bus_id = module->hd->bus_id;
		__entry->module_id = module->module_id;
		__entry->num_interfaces = module->num_interfaces;
		__entry->disconnected = module->disconnected;
	),

	TP_printk("hd_bus_id=%d module_id=%hhu num_interfaces=%zu disconnected=%d",
		__entry->hd_bus_id, __entry->module_id,
		__entry->num_interfaces, __entry->disconnected)
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
		__field(size_t, num_cports)
		__field(size_t, buffer_size_max)
	),

	TP_fast_assign(
		__entry->bus_id = hd->bus_id;
		__entry->num_cports = hd->num_cports;
		__entry->buffer_size_max = hd->buffer_size_max;
	),

	TP_printk("bus_id=%d num_cports=%zu mtu=%zu",
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
 * connection to its SVC has been enabled.
 */
DEFINE_HD_EVENT(gb_hd_add);

/*
 * Occurs when a host device is being disconnected from the AP USB
 * host controller.
 */
DEFINE_HD_EVENT(gb_hd_del);

/*
 * Occurs when a host device has passed received data to the Greybus
 * core, after it has been determined it is destined for a valid
 * CPort.
 */
DEFINE_HD_EVENT(gb_hd_in);

#undef DEFINE_HD_EVENT

/*
 * Occurs on a TimeSync synchronization event or a TimeSync ping event.
 */
TRACE_EVENT(gb_timesync_irq,

	TP_PROTO(u8 ping, u8 strobe, u8 count, u64 frame_time),

	TP_ARGS(ping, strobe, count, frame_time),

	TP_STRUCT__entry(
		__field(u8, ping)
		__field(u8, strobe)
		__field(u8, count)
		__field(u64, frame_time)
	),

	TP_fast_assign(
		__entry->ping = ping;
		__entry->strobe = strobe;
		__entry->count = count;
		__entry->frame_time = frame_time;
	),

	TP_printk("%s %d/%d frame-time %llu\n",
		  __entry->ping ? "ping" : "strobe", __entry->strobe,
		  __entry->count, __entry->frame_time)
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

