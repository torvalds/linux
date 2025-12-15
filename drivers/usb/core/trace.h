/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Google LLC
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM usbcore

#if !defined(_USB_CORE_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _USB_CORE_TRACE_H

#include <linux/types.h>
#include <linux/tracepoint.h>
#include <linux/usb.h>

DECLARE_EVENT_CLASS(usb_core_log_usb_device,
	TP_PROTO(struct usb_device *udev),
	TP_ARGS(udev),
	TP_STRUCT__entry(
		__string(name, dev_name(&udev->dev))
		__field(enum usb_device_speed, speed)
		__field(enum usb_device_state, state)
		__field(unsigned short, bus_mA)
		__field(unsigned, authorized)
	),
	TP_fast_assign(
		__assign_str(name);
		__entry->speed = udev->speed;
		__entry->state = udev->state;
		__entry->bus_mA = udev->bus_mA;
		__entry->authorized = udev->authorized;
	),
	TP_printk("usb %s speed %s state %s %dmA [%s]",
		__get_str(name),
		usb_speed_string(__entry->speed),
		usb_state_string(__entry->state),
		__entry->bus_mA,
		__entry->authorized ? "authorized" : "unauthorized")
);

DEFINE_EVENT(usb_core_log_usb_device, usb_set_device_state,
	TP_PROTO(struct usb_device *udev),
	TP_ARGS(udev)
);

DEFINE_EVENT(usb_core_log_usb_device, usb_alloc_dev,
	TP_PROTO(struct usb_device *udev),
	TP_ARGS(udev)
);


#endif /* _USB_CORE_TRACE_H */

/* this part has to be here */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
