/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * et8ek8_reg.h
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Sakari Ailus <sakari.ailus@iki.fi>
 *          Tuukka Toivonen <tuukkat76@gmail.com>
 */

#ifndef ET8EK8REGS_H
#define ET8EK8REGS_H

#include <linux/i2c.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <linux/v4l2-subdev.h>

struct v4l2_mbus_framefmt;
struct v4l2_subdev_pad_mbus_code_enum;

struct et8ek8_mode {
	/* Physical sensor resolution and current image window */
	u16 sensor_width;
	u16 sensor_height;
	u16 sensor_window_origin_x;
	u16 sensor_window_origin_y;
	u16 sensor_window_width;
	u16 sensor_window_height;

	/* Image data coming from sensor (after scaling) */
	u16 width;
	u16 height;
	u16 window_origin_x;
	u16 window_origin_y;
	u16 window_width;
	u16 window_height;

	u32 pixel_clock;		/* in Hz */
	u32 ext_clock;			/* in Hz */
	struct v4l2_fract timeperframe;
	u32 max_exp;			/* Maximum exposure value */
	u32 bus_format;			/* MEDIA_BUS_FMT_ */
	u32 sensitivity;		/* 16.16 fixed point */
};

#define ET8EK8_REG_8BIT			1
#define ET8EK8_REG_16BIT		2
#define ET8EK8_REG_DELAY		100
#define ET8EK8_REG_TERM			0xff
struct et8ek8_reg {
	u16 type;
	u16 reg;			/* 16-bit offset */
	u32 val;			/* 8/16/32-bit value */
};

/* Possible struct smia_reglist types. */
#define ET8EK8_REGLIST_STANDBY		0
#define ET8EK8_REGLIST_POWERON		1
#define ET8EK8_REGLIST_RESUME		2
#define ET8EK8_REGLIST_STREAMON		3
#define ET8EK8_REGLIST_STREAMOFF	4
#define ET8EK8_REGLIST_DISABLED		5

#define ET8EK8_REGLIST_MODE		10

#define ET8EK8_REGLIST_LSC_ENABLE	100
#define ET8EK8_REGLIST_LSC_DISABLE	101
#define ET8EK8_REGLIST_ANR_ENABLE	102
#define ET8EK8_REGLIST_ANR_DISABLE	103

struct et8ek8_reglist {
	u32 type;
	struct et8ek8_mode mode;
	struct et8ek8_reg regs[];
};

#define ET8EK8_MAX_LEN			32
struct et8ek8_meta_reglist {
	char version[ET8EK8_MAX_LEN];
	union {
		struct et8ek8_reglist *ptr;
	} reglist[];
};

extern struct et8ek8_meta_reglist meta_reglist;

#endif /* ET8EK8REGS */
