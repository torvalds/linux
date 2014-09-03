/*
 * Realtek RTL2832U SDR driver
 *
 * Copyright (C) 2013 Antti Palosaari <crope@iki.fi>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License along
 *    with this program; if not, write to the Free Software Foundation, Inc.,
 *    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * GNU Radio plugin "gr-kernel" for device usage will be on:
 * http://git.linuxtv.org/anttip/gr-kernel.git
 *
 * TODO:
 * Help is very highly welcome for these + all the others you could imagine:
 * - move controls to V4L2 API
 * - use libv4l2 for stream format conversions
 * - gr-kernel: switch to v4l2_mmap (current read eats a lot of cpu)
 * - SDRSharp support
 */

#ifndef RTL2832_SDR_H
#define RTL2832_SDR_H

#include <linux/kconfig.h>
#include <media/v4l2-subdev.h>

/* for config struct */
#include "rtl2832.h"

#if IS_ENABLED(CONFIG_DVB_RTL2832_SDR)
extern struct dvb_frontend *rtl2832_sdr_attach(struct dvb_frontend *fe,
	struct i2c_adapter *i2c, const struct rtl2832_config *cfg,
	struct v4l2_subdev *sd);
#else
static inline struct dvb_frontend *rtl2832_sdr_attach(struct dvb_frontend *fe,
	struct i2c_adapter *i2c, const struct rtl2832_config *cfg,
	struct v4l2_subdev *sd)
{
	dev_warn(&i2c->dev, "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif /* RTL2832_SDR_H */
