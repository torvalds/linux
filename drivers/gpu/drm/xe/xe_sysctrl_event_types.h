/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */

#ifndef _XE_SYSCTRL_EVENT_TYPES_H_
#define _XE_SYSCTRL_EVENT_TYPES_H_

#include <linux/types.h>

#define XE_SYSCTRL_EVENT_DATA_LEN		59

/* Modify as needed */
#define XE_SYSCTRL_EVENT_FLOOD			16

/**
 * enum xe_sysctrl_event - Events reported by System Controller
 *
 * @XE_SYSCTRL_EVENT_THRESHOLD_CROSSED: Error counter threshold crossed
 */
enum xe_sysctrl_event {
	XE_SYSCTRL_EVENT_THRESHOLD_CROSSED	= 0x01,
};

/**
 * struct xe_sysctrl_event_request - Request structure for pending event
 */
struct xe_sysctrl_event_request {
	/** @vector: MSI-X vector that was triggered */
	u32 vector;
	/** @fn: Function index (0-7) of PCIe device */
	u32 fn:8;
	/** @reserved: Reserved for future use */
	u32 reserved:24;
	/** @reserved1: Reserved for future use */
	u32 reserved1[2];
} __packed;

/**
 * struct xe_sysctrl_event_response - Response structure for pending event
 */
struct xe_sysctrl_event_response {
	/** @count: Pending event count after this response */
	u32 count;
	/** @event: Pending event type */
	u32 event;
	/** @timestamp: Timestamp of most recent event */
	u64 timestamp;
	/** @extended: Event has extended payload */
	u32 extended:1;
	/** @reserved: Reserved for future use */
	u32 reserved:31;
	/** @data: Generic event data */
	u32 data[XE_SYSCTRL_EVENT_DATA_LEN];
} __packed;

#endif /* _XE_SYSCTRL_EVENT_TYPES_H_ */
