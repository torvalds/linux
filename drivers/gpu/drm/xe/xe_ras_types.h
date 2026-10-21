/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */

#ifndef _XE_RAS_TYPES_H_
#define _XE_RAS_TYPES_H_

#include <linux/types.h>

#define XE_RAS_NUM_COUNTERS			16
#define XE_RAS_NUM_ERROR_ARR			3
/* Error bits in IEH global error status register */
#define XE_RAS_SOC_IEH_PUNIT			BIT(1)
/* Device memory error categories */
#define XE_RAS_MEMORY_DB_ECC			BIT(1)
#define XE_RAS_MEMORY_POISON			BIT(2)
#define XE_RAS_MEMORY_DATA_PARITY		BIT(5)

/**
 * enum xe_ras_recovery_action - RAS recovery actions
 *
 * @XE_RAS_RECOVERY_ACTION_RECOVERED: Error recovered
 * @XE_RAS_RECOVERY_ACTION_RESET: Requires reset
 * @XE_RAS_RECOVERY_ACTION_DISCONNECT: Requires disconnect
 * @XE_RAS_RECOVERY_ACTION_MAX: Max action value
 *
 * This enum defines the possible recovery actions that can be taken in response
 * to RAS errors.
 */
enum xe_ras_recovery_action {
	XE_RAS_RECOVERY_ACTION_RECOVERED = 0,
	XE_RAS_RECOVERY_ACTION_RESET,
	XE_RAS_RECOVERY_ACTION_DISCONNECT,
	XE_RAS_RECOVERY_ACTION_MAX
};

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

/**
 * struct xe_ras_error_array - Details of the error types
 */
struct xe_ras_error_array {
	/** @value: Counter value of the detailed error */
	u32 value;
	/** @counter: Error counter */
	struct xe_ras_error_class counter;
	/** @timestamp: Timestamp */
	u64 timestamp;
	/** @details: Error details specific to the counter */
	u32 details[XE_RAS_NUM_COUNTERS];
} __packed;

/**
 * struct xe_ras_get_soc_error - Response from get soc error command
 */
struct xe_ras_get_soc_error {
	/** @num_errors: Number of errors reported in this response */
	u8 num_errors;
	/** @additional_errors: Indicates if the errors are pending */
	u8 additional_errors;
	/** @arr: Array of up to 3 errors */
	struct xe_ras_error_array arr[XE_RAS_NUM_ERROR_ARR];
} __packed;

/**
 * struct xe_ras_compute_error - Error details of Core Compute error
 */
struct xe_ras_compute_error {
	/** @log_header: Error Source and type */
	u32 log_header;
	/** @reserved: Reserved */
	u32 reserved[15];
} __packed;

/**
 * struct xe_ras_soc_error_source - Source of SoC error
 */
struct xe_ras_soc_error_source {
	/** @csc: CSC */
	u32 csc:1;
	/** @ieh: IEH (Integrated Error Handler) */
	u32 ieh:1;
	/** @reserved: Reserved for future use */
	u32 reserved:30;
} __packed;

/**
 * struct xe_ras_soc_error - Error details of SoC internal error
 */
struct xe_ras_soc_error {
	/** @source: Error source */
	struct xe_ras_soc_error_source source;
	/** @details: Error details specific to the error source */
	u32 details[15];
} __packed;

/**
 * struct xe_ras_csc_error - CSC error details
 */
struct xe_ras_csc_error {
	/** @reserved: Reserved for future use */
	u32 reserved;
	/** @hec_fw_error: CSC firmware error */
	u32 hec_fw_error;
} __packed;

/**
 * struct xe_ras_ieh_error - IEH (Integrated Error Handler) error details
 */
struct xe_ras_ieh_error {
	/** @reserved: Reserved for future use */
	u32 reserved;
	/** @global_error_status: Global error status */
	u32 global_error_status;
	/** @reserved1: Reserved for future use */
	u32 reserved1[2];
	/** @info: Additional information */
	u32 info[10];
} __packed;

/**
 * struct xe_ras_memory_error - Device memory error details
 */
struct xe_ras_memory_error {
	/** @category: Device memory error category */
	u8 category;
	/** @reserved: Reserved for future use */
	u8 reserved[7];
	/** @reserved1: Reserved for future use */
	u64 reserved1;
	/** @sw_address: Software address where error occurred */
	u64 sw_address;
	/** @reserved2: Reserved for future use */
	u32 reserved2[10];
} __packed;

/**
 * struct xe_ras_get_health_request - Request structure for obtaining gpu health
 */
struct xe_ras_get_health_request {
	/** @reserved: Reserved for future use. */
	u32 reserved[2];
} __packed;

/**
 * struct xe_ras_get_health_response - Response structure for obtaining gpu health
 */
struct xe_ras_get_health_response {
	/** @health: gpu health value */
	u8 health;
	/** @reserved: Reserved for future use */
	u8 reserved[3];
} __packed;

/**
 * struct xe_ras_set_health_request - Request structure for setting gpu health
 */
struct xe_ras_set_health_request {
	/** @health: gpu health value */
	u8 health;
	/** @reserved: Reserved for future use */
	u8 reserved[3];
} __packed;

/**
 * struct xe_ras_set_health_response - Response structure for setting gpu health
 */
struct xe_ras_set_health_response {
	/** @status: Status of set health operation */
	u32 status;
	/** @health: Resulting gpu health value */
	u8 health;
	/** @reserved: Reserved for future use */
	u8 reserved[3];
	/** @reserved1: Reserved for future use */
	u32 reserved1[2];
} __packed;
#endif
