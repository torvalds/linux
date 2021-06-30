/* SPDX-License-Identifier: GPL-2.0 */
/*
 * System Control and Management Interface (SCMI) Message Protocol
 * notification header file containing some definitions, structures
 * and function prototypes related to SCMI Notification handling.
 *
 * Copyright (C) 2020-2021 ARM Ltd.
 */
#ifndef _SCMI_NOTIFY_H
#define _SCMI_NOTIFY_H

#include <linux/device.h>
#include <linux/ktime.h>
#include <linux/types.h>

#define SCMI_PROTO_QUEUE_SZ	4096

/**
 * struct scmi_event  - Describes an event to be supported
 * @id: Event ID
 * @max_payld_sz: Max possible size for the payload of a notification message
 * @max_report_sz: Max possible size for the report of a notification message
 *
 * Each SCMI protocol, during its initialization phase, can describe the events
 * it wishes to support in a few struct scmi_event and pass them to the core
 * using scmi_register_protocol_events().
 */
struct scmi_event {
	u8	id;
	size_t	max_payld_sz;
	size_t	max_report_sz;
};

struct scmi_protocol_handle;

/**
 * struct scmi_event_ops  - Protocol helpers called by the notification core.
 * @get_num_sources: Returns the number of possible events' sources for this
 *		     protocol
 * @set_notify_enabled: Enable/disable the required evt_id/src_id notifications
 *			using the proper custom protocol commands.
 *			Return 0 on Success
 * @fill_custom_report: fills a custom event report from the provided
 *			event message payld identifying the event
 *			specific src_id.
 *			Return NULL on failure otherwise @report now fully
 *			populated
 *
 * Context: Helpers described in &struct scmi_event_ops are called only in
 *	    process context.
 */
struct scmi_event_ops {
	int (*get_num_sources)(const struct scmi_protocol_handle *ph);
	int (*set_notify_enabled)(const struct scmi_protocol_handle *ph,
				  u8 evt_id, u32 src_id, bool enabled);
	void *(*fill_custom_report)(const struct scmi_protocol_handle *ph,
				    u8 evt_id, ktime_t timestamp,
				    const void *payld, size_t payld_sz,
				    void *report, u32 *src_id);
};

/**
 * struct scmi_protocol_events  - Per-protocol description of available events
 * @queue_sz: Size in bytes of the per-protocol queue to use.
 * @ops: Array of protocol-specific events operations.
 * @evts: Array of supported protocol's events.
 * @num_events: Number of supported protocol's events described in @evts.
 * @num_sources: Number of protocol's sources, should be greater than 0; if not
 *		 available at compile time, it will be provided at run-time via
 *		 @get_num_sources.
 */
struct scmi_protocol_events {
	size_t				queue_sz;
	const struct scmi_event_ops	*ops;
	const struct scmi_event		*evts;
	unsigned int			num_events;
	unsigned int			num_sources;
};

int scmi_notification_init(struct scmi_handle *handle);
void scmi_notification_exit(struct scmi_handle *handle);
int scmi_register_protocol_events(const struct scmi_handle *handle, u8 proto_id,
				  const struct scmi_protocol_handle *ph,
				  const struct scmi_protocol_events *ee);
void scmi_deregister_protocol_events(const struct scmi_handle *handle,
				     u8 proto_id);
int scmi_notify(const struct scmi_handle *handle, u8 proto_id, u8 evt_id,
		const void *buf, size_t len, ktime_t ts);

#endif /* _SCMI_NOTIFY_H */
