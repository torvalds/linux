/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_LATE_BIND_TYPES_H_
#define _XE_LATE_BIND_TYPES_H_

#include <linux/iosys-map.h>
#include <linux/mutex.h>
#include <linux/types.h>

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
};

#endif
