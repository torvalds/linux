/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */

#ifndef _XE_RAS_TYPES_H_
#define _XE_RAS_TYPES_H_

#include <linux/types.h>

#define XE_RAS_NUM_COUNTERS			16

/**
 * struct xe_ras_error_common - Error fields that are common across all products
 */
struct xe_ras_error_common {
	/** @severity: Error severity */
	u8 severity;
	/** @component: IP block where error originated */
	u8 component;
} __packed;

/**
 * struct xe_ras_error_unit - Error unit information
 */
struct xe_ras_error_unit {
	/** @tile: Tile identifier */
	u8 tile;
	/** @instance: Instance identifier specific to IP */
	u32 instance;
} __packed;

/**
 * struct xe_ras_error_cause - Error cause information
 */
struct xe_ras_error_cause {
	/** @cause: Cause/checker */
	u32 cause;
	/** @reserved: For future use */
	u8 reserved;
} __packed;

/**
 * struct xe_ras_error_product - Error fields that are specific to the product
 */
struct xe_ras_error_product {
	/** @unit: Unit within IP block */
	struct xe_ras_error_unit unit;
	/** @cause: Cause/checker */
	struct xe_ras_error_cause cause;
} __packed;

/**
 * struct xe_ras_error_class - Combines common and product-specific parts
 */
struct xe_ras_error_class {
	/** @common: Common error type and component */
	struct xe_ras_error_common common;
	/** @product: Product-specific unit and cause */
	struct xe_ras_error_product product;
} __packed;

/**
 * struct xe_ras_threshold_crossed - Data for threshold crossed event
 */
struct xe_ras_threshold_crossed {
	/** @ncounters: Number of error counters that crossed thresholds */
	u32 ncounters;
	/** @counters: Array of error counters that crossed threshold */
	struct xe_ras_error_class counters[XE_RAS_NUM_COUNTERS];
} __packed;

/**
 * struct xe_ras_get_counter_request - Request structure for get counter
 */
struct xe_ras_get_counter_request {
	/** @counter: Error counter to be queried */
	struct xe_ras_error_class counter;
	/** @reserved: Reserved for future use */
	u32 reserved;
} __packed;

/**
 * struct xe_ras_get_counter_response - Response structure for get counter
 */
struct xe_ras_get_counter_response {
	/** @counter: Error counter that was queried */
	struct xe_ras_error_class counter;
	/** @value: Current counter value */
	u32 value;
	/** @timestamp: Timestamp when counter was last updated */
	u64 timestamp;
	/** @threshold: Threshold value for the counter */
	u32 threshold;
	/** @reserved: Reserved  */
	u32 reserved[57];
} __packed;

/**
 * struct xe_ras_clear_counter_request - Request structure for clear counter
 */
struct xe_ras_clear_counter_request {
	/** @counter: Counter class to be cleared */
	struct xe_ras_error_class counter;
	/** @reserved: Reserved for future use */
	u32 reserved;
} __packed;

/**
 * struct xe_ras_clear_counter_response - Response structure for clear counter
 */
struct xe_ras_clear_counter_response {
	/** @counter: Counter class that was cleared */
	struct xe_ras_error_class counter;
	/** @reserved: Reserved */
	u32 reserved;
	/** @timestamp: Timestamp when the counter was cleared */
	u64 timestamp;
	/** @status: Status of the clear operation */
	u32 status;
	/** @reserved1: Reserved for future use */
	u32 reserved1[3];
} __packed;
#endif
