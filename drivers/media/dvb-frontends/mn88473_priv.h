/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Panasonic MN88473 DVB-T/T2/C demodulator driver
 *
 * Copyright (C) 2014 Antti Palosaari <crope@iki.fi>
 */

#ifndef MN88473_PRIV_H
#define MN88473_PRIV_H

#include <media/dvb_frontend.h>
#include <linux/int_log.h>
#include "mn88473.h"
#include <linux/math64.h>
#include <linux/firmware.h>
#include <linux/regmap.h>

#define MN88473_FIRMWARE "dvb-demod-mn88473-01.fw"

struct mn88473_dev {
	struct i2c_client *client[3];
	struct regmap *regmap[3];
	struct dvb_frontend frontend;
	u16 i2c_wr_max;
	bool active;
	u32 clk;
};

#endif
