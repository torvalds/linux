/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * FCI FC2580 silicon tuner driver
 *
 * Copyright (C) 2012 Antti Palosaari <crope@iki.fi>
 */

#ifndef FC2580_H
#define FC2580_H

#include <media/dvb_frontend.h>
#include <media/v4l2-subdev.h>
#include <linux/i2c.h>

/*
 * I2C address
 * 0x56, ...
 */

/**
 * struct fc2580_platform_data - Platform data for the fc2580 driver
 * @clk: Clock frequency (0 = internal clock).
 * @dvb_frontend: DVB frontend.
 * @get_v4l2_subdev: Get V4L2 subdev.
 */
struct fc2580_platform_data {
	u32 clk;
	struct dvb_frontend *dvb_frontend;

	struct v4l2_subdev* (*get_v4l2_subdev)(struct i2c_client *);
};

#endif
