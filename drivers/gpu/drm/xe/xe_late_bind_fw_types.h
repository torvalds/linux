/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_LATE_BIND_TYPES_H_
#define _XE_LATE_BIND_TYPES_H_

#include <linux/iosys-map.h>
#include <linux/mutex.h>
#include <linux/types.h>

#define XE_LB_MAX_PAYLOAD_SIZE SZ_4K

/**
 * xe_late_bind_fw_id - enum to determine late binding fw index
 */
enum xe_late_bind_fw_id {
	XE_LB_FW_FAN_CONTROL = 0,
	XE_LB_FW_MAX_ID
};

/**
 * struct xe_late_bind_fw
 */
struct xe_late_bind_fw {
	/** @id: firmware index */
	u32 id;
	/** @blob_path: firmware binary path */
	char blob_path[PATH_MAX];
	/** @type: firmware type */
	u32  type;
	/** @flags: firmware flags */
	u32  flags;
	/** @payload: to store the late binding blob */
	const u8  *payload;
	/** @payload_size: late binding blob payload_size */
	size_t payload_size;
};

/**
 * struct xe_late_bind_component - Late Binding services component
 * @mei_dev: device that provide Late Binding service.
 * @ops: Ops implemented by Late Binding driver, used by Xe driver.
 *
 * Communication between Xe and MEI drivers for Late Binding services
 */
struct xe_late_bind_component {
	struct device *mei_dev;
	const struct late_bind_component_ops *ops;
};

/**
 * struct xe_late_bind
 */
struct xe_late_bind {
	/** @component: struct for communication with mei component */
	struct xe_late_bind_component component;
	/** @late_bind_fw: late binding firmware array */
	struct xe_late_bind_fw late_bind_fw[XE_LB_FW_MAX_ID];
};

#endif
