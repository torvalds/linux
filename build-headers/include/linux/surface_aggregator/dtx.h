/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Surface DTX (clipboard detachment system driver) user-space interface.
 *
 * Definitions, structs, and IOCTLs for the /dev/surface/dtx misc device. This
 * device allows user-space to control the clipboard detachment process on
 * Surface Book series devices.
 *
 * Copyright (C) 2020-2021 Maximilian Luz <luzmaximilian@gmail.com>
 */

#ifndef _LINUX_SURFACE_AGGREGATOR_DTX_H
#define _LINUX_SURFACE_AGGREGATOR_DTX_H

#include <linux/ioctl.h>
#include <linux/types.h>

/* Status/error categories */
#define SDTX_CATEGORY_STATUS		0x0000
#define SDTX_CATEGORY_RUNTIME_ERROR	0x1000
#define SDTX_CATEGORY_HARDWARE_ERROR	0x2000
#define SDTX_CATEGORY_UNKNOWN		0xf000

#define SDTX_CATEGORY_MASK		0xf000
#define SDTX_CATEGORY(value)		((value) & SDTX_CATEGORY_MASK)

#define SDTX_STATUS(code)		((code) | SDTX_CATEGORY_STATUS)
#define SDTX_ERR_RT(code)		((code) | SDTX_CATEGORY_RUNTIME_ERROR)
#define SDTX_ERR_HW(code)		((code) | SDTX_CATEGORY_HARDWARE_ERROR)
#define SDTX_UNKNOWN(code)		((code) | SDTX_CATEGORY_UNKNOWN)

#define SDTX_SUCCESS(value)		(SDTX_CATEGORY(value) == SDTX_CATEGORY_STATUS)

/* Latch status values */
#define SDTX_LATCH_CLOSED		SDTX_STATUS(0x00)
#define SDTX_LATCH_OPENED		SDTX_STATUS(0x01)

/* Base state values */
#define SDTX_BASE_DETACHED		SDTX_STATUS(0x00)
#define SDTX_BASE_ATTACHED		SDTX_STATUS(0x01)

/* Runtime errors (non-critical) */
#define SDTX_DETACH_NOT_FEASIBLE	SDTX_ERR_RT(0x01)
#define SDTX_DETACH_TIMEDOUT		SDTX_ERR_RT(0x02)

/* Hardware errors (critical) */
#define SDTX_ERR_FAILED_TO_OPEN		SDTX_ERR_HW(0x01)
#define SDTX_ERR_FAILED_TO_REMAIN_OPEN	SDTX_ERR_HW(0x02)
#define SDTX_ERR_FAILED_TO_CLOSE	SDTX_ERR_HW(0x03)

/* Base types */
#define SDTX_DEVICE_TYPE_HID		0x0100
#define SDTX_DEVICE_TYPE_SSH		0x0200

#define SDTX_DEVICE_TYPE_MASK		0x0f00
#define SDTX_DEVICE_TYPE(value)		((value) & SDTX_DEVICE_TYPE_MASK)

#define SDTX_BASE_TYPE_HID(id)		((id) | SDTX_DEVICE_TYPE_HID)
#define SDTX_BASE_TYPE_SSH(id)		((id) | SDTX_DEVICE_TYPE_SSH)

/**
 * enum sdtx_device_mode - Mode describing how (and if) the clipboard is
 * attached to the base of the device.
 * @SDTX_DEVICE_MODE_TABLET: The clipboard is detached from the base and the
 *                           device operates as tablet.
 * @SDTX_DEVICE_MODE_LAPTOP: The clipboard is attached normally to the base
 *                           and the device operates as laptop.
 * @SDTX_DEVICE_MODE_STUDIO: The clipboard is attached to the base in reverse.
 *                           The device operates as tablet with keyboard and
 *                           touchpad deactivated, however, the base battery
 *                           and, if present in the specific device model, dGPU
 *                           are available to the system.
 */
enum sdtx_device_mode {
	SDTX_DEVICE_MODE_TABLET		= 0x00,
	SDTX_DEVICE_MODE_LAPTOP		= 0x01,
	SDTX_DEVICE_MODE_STUDIO		= 0x02,
};

/**
 * struct sdtx_event - Event provided by reading from the DTX device file.
 * @length: Length of the event payload, in bytes.
 * @code:   Event code, detailing what type of event this is.
 * @data:   Payload of the event, containing @length bytes.
 *
 * See &enum sdtx_event_code for currently valid event codes.
 */
struct sdtx_event {
	__u16 length;
	__u16 code;
	__u8 data[];
} __attribute__((__packed__));

/**
 * enum sdtx_event_code - Code describing the type of an event.
 * @SDTX_EVENT_REQUEST:         Detachment request event type.
 * @SDTX_EVENT_CANCEL:          Cancel detachment process event type.
 * @SDTX_EVENT_BASE_CONNECTION: Base/clipboard connection change event type.
 * @SDTX_EVENT_LATCH_STATUS:    Latch status change event type.
 * @SDTX_EVENT_DEVICE_MODE:     Device mode change event type.
 *
 * Used in &struct sdtx_event to describe the type of the event. Further event
 * codes are reserved for future use. Any event parser should be able to
 * gracefully handle unknown events, i.e. by simply skipping them.
 *
 * Consult the DTX user-space interface documentation for details regarding
 * the individual event types.
 */
enum sdtx_event_code {
	SDTX_EVENT_REQUEST		= 1,
	SDTX_EVENT_CANCEL		= 2,
	SDTX_EVENT_BASE_CONNECTION	= 3,
	SDTX_EVENT_LATCH_STATUS		= 4,
	SDTX_EVENT_DEVICE_MODE		= 5,
};

/**
 * struct sdtx_base_info - Describes if and what type of base is connected.
 * @state:   The state of the connection. Valid values are %SDTX_BASE_DETACHED,
 *           %SDTX_BASE_ATTACHED, and %SDTX_DETACH_NOT_FEASIBLE (in case a base
 *           is attached but low clipboard battery prevents detachment). Other
 *           values are currently reserved.
 * @base_id: The type of base connected. Zero if no base is connected.
 */
struct sdtx_base_info {
	__u16 state;
	__u16 base_id;
} __attribute__((__packed__));

/* IOCTLs */
#define SDTX_IOCTL_EVENTS_ENABLE	_IO(0xa5, 0x21)
#define SDTX_IOCTL_EVENTS_DISABLE	_IO(0xa5, 0x22)

#define SDTX_IOCTL_LATCH_LOCK		_IO(0xa5, 0x23)
#define SDTX_IOCTL_LATCH_UNLOCK		_IO(0xa5, 0x24)

#define SDTX_IOCTL_LATCH_REQUEST	_IO(0xa5, 0x25)
#define SDTX_IOCTL_LATCH_CONFIRM	_IO(0xa5, 0x26)
#define SDTX_IOCTL_LATCH_HEARTBEAT	_IO(0xa5, 0x27)
#define SDTX_IOCTL_LATCH_CANCEL		_IO(0xa5, 0x28)

#define SDTX_IOCTL_GET_BASE_INFO	_IOR(0xa5, 0x29, struct sdtx_base_info)
#define SDTX_IOCTL_GET_DEVICE_MODE	_IOR(0xa5, 0x2a, __u16)
#define SDTX_IOCTL_GET_LATCH_STATUS	_IOR(0xa5, 0x2b, __u16)

#endif /* _LINUX_SURFACE_AGGREGATOR_DTX_H */
