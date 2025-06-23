/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Surface System Aggregator Module (SSAM) user-space EC interface.
 *
 * Definitions, structs, and IOCTLs for the /dev/surface/aggregator misc
 * device. This device provides direct user-space access to the SSAM EC.
 * Intended for debugging and development.
 *
 * Copyright (C) 2020-2021 Maximilian Luz <luzmaximilian@gmail.com>
 */

#ifndef _LINUX_SURFACE_AGGREGATOR_CDEV_H
#define _LINUX_SURFACE_AGGREGATOR_CDEV_H

#include <linux/ioctl.h>
#include <linux/types.h>

/**
 * enum ssam_cdev_request_flags - Request flags for SSAM cdev request IOCTL.
 *
 * @SSAM_CDEV_REQUEST_HAS_RESPONSE:
 *	Specifies that the request expects a response. If not set, the request
 *	will be directly completed after its underlying packet has been
 *	transmitted. If set, the request transport system waits for a response
 *	of the request.
 *
 * @SSAM_CDEV_REQUEST_UNSEQUENCED:
 *	Specifies that the request should be transmitted via an unsequenced
 *	packet. If set, the request must not have a response, meaning that this
 *	flag and the %SSAM_CDEV_REQUEST_HAS_RESPONSE flag are mutually
 *	exclusive.
 */
enum ssam_cdev_request_flags {
	SSAM_CDEV_REQUEST_HAS_RESPONSE = 0x01,
	SSAM_CDEV_REQUEST_UNSEQUENCED  = 0x02,
};

/**
 * struct ssam_cdev_request - Controller request IOCTL argument.
 * @target_category: Target category of the SAM request.
 * @target_id:       Target ID of the SAM request.
 * @command_id:      Command ID of the SAM request.
 * @instance_id:     Instance ID of the SAM request.
 * @flags:           Request flags (see &enum ssam_cdev_request_flags).
 * @status:          Request status (output).
 * @payload:         Request payload (input data).
 * @payload.data:    Pointer to request payload data.
 * @payload.length:  Length of request payload data (in bytes).
 * @response:        Request response (output data).
 * @response.data:   Pointer to response buffer.
 * @response.length: On input: Capacity of response buffer (in bytes).
 *                   On output: Length of request response (number of bytes
 *                   in the buffer that are actually used).
 */
struct ssam_cdev_request {
	__u8 target_category;
	__u8 target_id;
	__u8 command_id;
	__u8 instance_id;
	__u16 flags;
	__s16 status;

	struct {
		__u64 data;
		__u16 length;
		__u8 __pad[6];
	} payload;

	struct {
		__u64 data;
		__u16 length;
		__u8 __pad[6];
	} response;
} __attribute__((__packed__));

/**
 * struct ssam_cdev_notifier_desc - Notifier descriptor.
 * @priority:        Priority value determining the order in which notifier
 *                   callbacks will be called. A higher value means higher
 *                   priority, i.e. the associated callback will be executed
 *                   earlier than other (lower priority) callbacks.
 * @target_category: The event target category for which this notifier should
 *                   receive events.
 *
 * Specifies the notifier that should be registered or unregistered,
 * specifically with which priority and for which target category of events.
 */
struct ssam_cdev_notifier_desc {
	__s32 priority;
	__u8 target_category;
} __attribute__((__packed__));

/**
 * struct ssam_cdev_event_desc - Event descriptor.
 * @reg:                 Registry via which the event will be enabled/disabled.
 * @reg.target_category: Target category for the event registry requests.
 * @reg.target_id:       Target ID for the event registry requests.
 * @reg.cid_enable:      Command ID for the event-enable request.
 * @reg.cid_disable:     Command ID for the event-disable request.
 * @id:                  ID specifying the event.
 * @id.target_category:  Target category of the event source.
 * @id.instance:         Instance ID of the event source.
 * @flags:               Flags used for enabling the event.
 *
 * Specifies which event should be enabled/disabled and how to do that.
 */
struct ssam_cdev_event_desc {
	struct {
		__u8 target_category;
		__u8 target_id;
		__u8 cid_enable;
		__u8 cid_disable;
	} reg;

	struct {
		__u8 target_category;
		__u8 instance;
	} id;

	__u8 flags;
} __attribute__((__packed__));

/**
 * struct ssam_cdev_event - SSAM event sent by the EC.
 * @target_category: Target category of the event source. See &enum ssam_ssh_tc.
 * @target_id:       Target ID of the event source.
 * @command_id:      Command ID of the event.
 * @instance_id:     Instance ID of the event source.
 * @length:          Length of the event payload in bytes.
 * @data:            Event payload data.
 */
struct ssam_cdev_event {
	__u8 target_category;
	__u8 target_id;
	__u8 command_id;
	__u8 instance_id;
	__u16 length;
	__u8 data[];
} __attribute__((__packed__));

#define SSAM_CDEV_REQUEST		_IOWR(0xA5, 1, struct ssam_cdev_request)
#define SSAM_CDEV_NOTIF_REGISTER	_IOW(0xA5, 2, struct ssam_cdev_notifier_desc)
#define SSAM_CDEV_NOTIF_UNREGISTER	_IOW(0xA5, 3, struct ssam_cdev_notifier_desc)
#define SSAM_CDEV_EVENT_ENABLE		_IOW(0xA5, 4, struct ssam_cdev_event_desc)
#define SSAM_CDEV_EVENT_DISABLE		_IOW(0xA5, 5, struct ssam_cdev_event_desc)

#endif /* _LINUX_SURFACE_AGGREGATOR_CDEV_H */
