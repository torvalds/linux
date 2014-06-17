/*
 * Copyright (C) 2012 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef ARMADA_SLAVE_H
#define ARMADA_SLAVE_H

#include <linux/i2c.h>
#include <drm/drmP.h>

struct armada_drm_slave_config {
	int i2c_adapter_id;
	uint32_t crtcs;
	uint8_t polled;
	bool interlace_allowed;
	bool doublescan_allowed;
	struct i2c_board_info info;
};

int armada_drm_connector_slave_create(struct drm_device *dev,
	const struct armada_drm_slave_config *);

#endif
