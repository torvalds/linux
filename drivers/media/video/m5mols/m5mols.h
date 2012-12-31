/*
 * Header for M5MOLS 8M Pixel camera sensor with ISP
 *
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 * Author: HeungJun Kim <riverful.kim@samsung.com>
 *
 * Copyright (C) 2009 Samsung Electronics Co., Ltd.
 * Author: Dongsoo Nathaniel Kim <dongsoo45.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef M5MOLS_H
#define M5MOLS_H

#include <media/v4l2-subdev.h>
#include "m5mols_reg.h"

#define M5MO_JPEG_MAXSIZE	0x3A0000
#define M5MO_THUMB_MAXSIZE	0xFC00
#define M5MO_POST_MAXSIZE	0xBB800
#define M5MO_JPEG_MEMSIZE	M5MO_JPEG_MAXSIZE + M5MO_THUMB_MAXSIZE + M5MO_POST_MAXSIZE

#define v4l2msg(fmt, arg...)	do {				\
	v4l2_dbg(1, m5mols_debug, &info->sd, fmt, ## arg);	\
} while (0)

extern int m5mols_debug;

enum m5mols_mode {
	MODE_SYSINIT,
	MODE_PARMSET,
	MODE_MONITOR,
	MODE_CAPTURE,
	MODE_UNKNOWN,
};

enum m5mols_restype {
	M5MOLS_RESTYPE_MONITOR,
	M5MOLS_RESTYPE_CAPTURE,
	M5MOLS_RESTYPE_MAX,
};

enum m5mols_status {
	STATUS_SYSINIT,
	STATUS_PARMSET,
	STATUS_MONITOR,
	STATUS_AUTO_FOCUS,
	STATUS_FACE_DETECTION,
	STATUS_DUAL_CAPTURE,
	STATUS_SINGLE_CAPTURE,
	STATUS_PREVIEW,
	STATUS_UNKNOWN,
};

enum m5mols_intterrupt_bit {
	INT_BIT_MODE,
	INT_BIT_AF,
	INT_BIT_ZOOM,
	INT_BIT_CAPTURE,
	INT_BIT_FRAME_SYNC,
	INT_BIT_FD,
	INT_BIT_LENS_INIT,
	INT_BIT_SOUND,
};

enum m5mols_i2c_size {
	I2C_8BIT	= 1,
	I2C_16BIT	= 2,
	I2C_32BIT	= 4,
	I2C_MAX		= 4,
};

enum m5mols_fps {
	M5MOLS_FPS_AUTO	= 0,
	M5MOLS_FPS_10	= 10,
	M5MOLS_FPS_12	= 12,
	M5MOLS_FPS_15	= 15,
	M5MOLS_FPS_20	= 20,
	M5MOLS_FPS_21	= 21,
	M5MOLS_FPS_22	= 22,
	M5MOLS_FPS_23	= 23,
	M5MOLS_FPS_24	= 24,
	M5MOLS_FPS_30	= 30,
	M5MOLS_FPS_MAX	= M5MOLS_FPS_30,
};

enum m5mols_res_type {
	M5MOLS_RES_MON,
	/* It's not supported below yet. */
	M5MOLS_RES_CAPTURE,
	M5MOLS_RES_MAX,
};

struct m5mols_resolution {
	u8			value;
	enum m5mols_res_type	type;
	u16			width;
	u16			height;
};

struct m5mols_format {
	enum v4l2_mbus_pixelcode code;
	enum v4l2_colorspace colorspace;
};

struct m5mols_control {
	u32	id;
	s32	min;
	s32	max;
	u32	step;
	s32	def;
};

struct m5mols_exif {
	u32	exposure_time;
	u32	shutter_speed;
	u32	aperture;
	u32	brightness;
	u32	exposure_bias;
	u16	iso_speed;
	u16	flash;
	u16	sdr;		/* subject(object) distance range */
	u16	qval;		/* This is not written precisely in datasheet. */
};

struct m5mols_capture {
	struct m5mols_exif		exif;
	u32				main;
	u32				thumb;
	u32				total;
};

struct m5mols_version {
	u8	ctm_code;	/* customer code */
	u8	pj_code;	/* project code */
	u16	fw;		/* firmware version */
	u16	hw;		/* hardware version */
	u16	parm;		/* parameter version */
	u16	awb;		/* AWB version */
};

struct m5mols_info {
	struct v4l2_subdev		sd;
	struct media_pad pad;
	int res_type;
	u8 resolution;
	struct v4l2_mbus_framefmt	fmt[M5MOLS_RES_MAX];
	struct v4l2_fract		tpf;

	struct v4l2_ctrl_handler	handle;
	struct {
		/* support only AE of the Monitor Mode in this version */
		struct v4l2_ctrl	*autoexposure;
		struct v4l2_ctrl	*exposure;
		bool			is_ae_lock;
	};
	struct v4l2_ctrl		*autofocus;
	bool				is_focus;
	struct v4l2_ctrl		*autowb;
	bool				is_awb_lock;
	struct v4l2_ctrl		*colorfx;
	struct v4l2_ctrl		*saturation;
	struct v4l2_ctrl		*zoom;
	struct v4l2_ctrl		*jpeg_size;
	struct v4l2_ctrl		*encoded_size;

	enum m5mols_mode		mode;
	enum m5mols_mode		mode_backup;
	enum m5mols_status		status;
	enum v4l2_mbus_pixelcode	code;

	struct m5mols_capture		cap;
	wait_queue_head_t		cap_wait;
	bool				captured;

	const struct m5mols_platform_data	*pdata;
	struct m5mols_version		ver;
	struct work_struct		work;
	bool				power;

	/* for additional power if needed. */
	int (*set_power)(struct device *dev, int on);
};

/* control functions */
int m5mols_set_ctrl(struct v4l2_ctrl *ctrl);

/* I2C functions - referenced by below I2C helper functions */
int m5mols_read_reg(struct v4l2_subdev *sd, enum m5mols_i2c_size size,
		u8 category, u8 cmd, u32 *val);
int m5mols_write_reg(struct v4l2_subdev *sd, enum m5mols_i2c_size size,
		u8 category, u8 cmd, u32 val);
int m5mols_check_busy(struct v4l2_subdev *sd,
		u8 category, u8 cmd, u32 value);
int m5mols_set_mode(struct v4l2_subdev *sd, enum m5mols_mode mode);
enum m5mols_status m5mols_get_status(struct v4l2_subdev *sd);

/*
 * helper functions
 */
static inline struct m5mols_info *to_m5mols(struct v4l2_subdev *sd)
{
	return container_of(sd, struct m5mols_info, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct m5mols_info, handle)->sd;
}

static inline bool is_streaming(struct v4l2_subdev *sd)
{
	struct m5mols_info *info = to_m5mols(sd);
	return (info->mode == MODE_MONITOR) || (info->mode == MODE_CAPTURE);
}

static inline bool is_stoped(struct v4l2_subdev *sd)
{
	struct m5mols_info *info = to_m5mols(sd);
	return (info->mode != MODE_MONITOR) && (info->mode != MODE_CAPTURE);
}

static inline bool is_powerup(struct v4l2_subdev *sd)
{
	struct m5mols_info *info = to_m5mols(sd);
	return info->power;
}

static inline int m5mols_set_mode_backup(struct v4l2_subdev *sd,
		enum m5mols_mode mode)
{
	struct m5mols_info *info = to_m5mols(sd);

	info->mode_backup = info->mode;
	return m5mols_set_mode(sd, mode);
}

static inline int m5mols_set_mode_restore(struct v4l2_subdev *sd)
{
	struct m5mols_info *info = to_m5mols(sd);
	int ret;

	ret = m5mols_set_mode(sd, info->mode_backup);
	if (!ret)
		info->mode = info->mode_backup;
	return ret;
}

static inline int __must_check i2c_w8_system(struct v4l2_subdev *sd,
		u8 cmd, u32 val)
{
	return m5mols_write_reg(sd, I2C_8BIT, CAT_SYSTEM, cmd, val);
}

static inline int __must_check i2c_w8_param(struct v4l2_subdev *sd,
		u8 cmd, u32 val)
{
	return m5mols_write_reg(sd, I2C_8BIT, CAT_PARAM, cmd, val);
}

static inline int __must_check i2c_w8_mon(struct v4l2_subdev *sd,
		u8 cmd, u32 val)
{
	return m5mols_write_reg(sd, I2C_8BIT, CAT_MON, cmd, val);
}

static inline int __must_check i2c_w8_ae(struct v4l2_subdev *sd,
		u8 cmd, u32 val)
{
	return m5mols_write_reg(sd, I2C_8BIT, CAT_AE, cmd, val);
}

static inline int __must_check i2c_w16_ae(struct v4l2_subdev *sd,
		u8 cmd, u32 val)
{
	return m5mols_write_reg(sd, I2C_16BIT, CAT_AE, cmd, val);
}

static inline int __must_check i2c_w8_wb(struct v4l2_subdev *sd,
		u8 cmd, u32 val)
{
	return m5mols_write_reg(sd, I2C_8BIT, CAT_WB, cmd, val);
}

static inline int __must_check i2c_w8_lens(struct v4l2_subdev *sd,
		u8 cmd, u32 val)
{
	return m5mols_write_reg(sd, I2C_8BIT, CAT_LENS, cmd, val);
}

static inline int __must_check i2c_w8_capt_parm(struct v4l2_subdev *sd,
		u8 cmd, u32 val)
{
	return m5mols_write_reg(sd, I2C_8BIT, CAT_CAPTURE_PARAMETER, cmd, val);
}

static inline int __must_check i2c_w8_capt_ctrl(struct v4l2_subdev *sd,
		u8 cmd, u32 val)
{
	return m5mols_write_reg(sd, I2C_8BIT, CAT_CAPTURE_CONTROL, cmd, val);
}

static inline int __must_check i2c_w8_flash(struct v4l2_subdev *sd,
		u8 cmd, u32 val)
{
	return m5mols_write_reg(sd, I2C_8BIT, CAT_FLASH, cmd, val);
}

static inline int __must_check i2c_r8_system(struct v4l2_subdev *sd,
		u8 cmd, u32 *val)
{
	return m5mols_read_reg(sd, I2C_8BIT, CAT_SYSTEM, cmd, val);
}

static inline int __must_check i2c_r8_param(struct v4l2_subdev *sd,
		u8 cmd, u32 *val)
{
	return m5mols_read_reg(sd, I2C_8BIT, CAT_PARAM, cmd, val);
}

static inline int __must_check i2c_r8_mon(struct v4l2_subdev *sd,
		u8 cmd, u32 *val)
{
	return m5mols_read_reg(sd, I2C_8BIT, CAT_MON, cmd, val);
}

static inline int __must_check i2c_r8_ae(struct v4l2_subdev *sd,
		u8 cmd, u32 *val)
{
	return m5mols_read_reg(sd, I2C_8BIT, CAT_AE, cmd, val);
}

static inline int __must_check i2c_r16_ae(struct v4l2_subdev *sd,
		u8 cmd, u32 *val)
{
	return m5mols_read_reg(sd, I2C_16BIT, CAT_AE, cmd, val);
}

static inline int __must_check i2c_r8_lens(struct v4l2_subdev *sd,
		u8 cmd, u32 *val)
{
	return m5mols_read_reg(sd, I2C_8BIT, CAT_LENS, cmd, val);
}

static inline int __must_check i2c_r32_capt_ctrl(struct v4l2_subdev *sd,
		u8 cmd, u32 *val)
{
	return m5mols_read_reg(sd, I2C_32BIT, CAT_CAPTURE_CONTROL, cmd, val);
}

static inline int __must_check i2c_r16_exif(struct v4l2_subdev *sd,
		u8 cmd, u32 *val)
{
	return m5mols_read_reg(sd, I2C_16BIT, CAT_EXIF, cmd, val);
}

static inline int __must_check i2c_r32_exif(struct v4l2_subdev *sd,
		u8 cmd, u32 *val)
{
	return m5mols_read_reg(sd, I2C_32BIT, CAT_EXIF, cmd, val);
}

static int m5mols_set_ae_lock(struct m5mols_info *info, bool lock)
{
	struct v4l2_subdev *sd = &info->sd;

	info->is_ae_lock = lock;

	return i2c_w8_ae(sd, CAT3_AE_LOCK, !!lock);
}

static int m5mols_set_awb_lock(struct m5mols_info *info, bool lock)
{
	struct v4l2_subdev *sd = &info->sd;

	info->is_awb_lock = lock;

	return i2c_w8_wb(sd, CAT6_AWB_LOCK, !!lock);
}
#endif	/* M5MOLS_H */
