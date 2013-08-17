/*
 * Samsung Exynos5 SoC series Sensor driver
 *
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef EXYNOS_SENSOR_H
#define EXYNOS_SENSOR_H

#include <media/v4l2-subdev.h>

#define sensor_info(fmt, args...) \
		printk(KERN_INFO "[INFO]%s:%d: "fmt "\n", __func__, __LINE__, ##args)
#define sensor_err(fmt, args...) \
		printk(KERN_ERR "[ERROR]%s:%d: "fmt "\n", __func__, __LINE__, ##args)
#define sensor_warn(fmt, args...) \
		printk(KERN_WARNING "[WARNNING]%s:%d: "fmt "\n", __func__, __LINE__, ##args)

#ifdef DEBUG
#define sensor_dbg(fmt, args...) \
		printk(KERN_DEBUG "[DEBUG]%s:%d: " fmt "\n", __func__, __LINE__, ##args)
#else
#define sensor_dbg(fmt, args...)
#endif

#define v4l2msg(fmt, arg...)	do {				\
	v4l2_dbg(1, sensor_debug, &info->sd, fmt, ## arg);	\
} while (0)

extern int sensor_debug;

enum sensor_status {
	STATUS_STANDBY,
	STATUS_STREAMING,
};

enum sensor_i2c_size {
	I2C_8BIT	= 1,
	I2C_16BIT	= 2,
	I2C_32BIT	= 4,
	I2C_MAX		= 4,
};

struct sensor_req_list {
	struct sensor_req_list *next;
	u32 addr;
	u8 value;
};

struct sensor_format {
	enum v4l2_mbus_pixelcode code;
	enum v4l2_colorspace colorspace;
};

struct sensor_info {
	struct v4l2_subdev		sd;
	struct media_pad		pad;
	struct v4l2_mbus_framefmt	fmt;
	enum v4l2_mbus_pixelcode	code;
	struct v4l2_fract		tpf;

	struct v4l2_ctrl_handler	handle;

	struct v4l2_ctrl	*ae_target;
	struct v4l2_ctrl	*ae_weight[8];
	struct v4l2_ctrl	*rg_weight;
	struct v4l2_ctrl	*ae_speed;
	struct v4l2_ctrl	*shutter;
	struct v4l2_ctrl	*gain;
	struct v4l2_ctrl	*bit_converting;
	struct v4l2_ctrl	*autoexposure;
	struct v4l2_ctrl	*framerate;
	enum sensor_status		status;
	bool	power;
	struct sensor_req_list	*req_head;
	struct sensor_req_list	*req_tail;

	const struct exynos_fimc_is_sensor_platform_data	*pdata;
	struct work_struct		work;
	u32				tasklet_param_str;
	struct tasklet_struct		tasklet_flite_str;
	u32				tasklet_param_end;
	struct tasklet_struct		tasklet_flite_end;
};

struct sensor_req_list *sensor_get_request(struct v4l2_subdev *sd);
static int sensor_drain_request(struct v4l2_subdev *sd, bool isValid);

/* I2C functions - referenced by below I2C helper functions */
int vision_sensor_read_reg(struct v4l2_subdev *sd,
		u32 addr, u8 *val, enum sensor_i2c_size size);
int vision_sensor_write_reg(struct v4l2_subdev *sd,
		u32 addr, u8 val, enum sensor_i2c_size size);
int vision_sensor_check_busy(struct v4l2_subdev *sd, u8 addr, u32 value);

/*
 * helper functions
 */
static inline struct sensor_info *to_sensor(struct v4l2_subdev *sd)
{
	return container_of(sd, struct sensor_info, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct sensor_info, handle)->sd;
}

static inline bool is_streaming(struct v4l2_subdev *sd)
{
	struct sensor_info *info = to_sensor(sd);
	return (info->status == STATUS_STREAMING);
}

static inline bool is_powerup(struct v4l2_subdev *sd)
{
	struct sensor_info *info = to_sensor(sd);
	return info->power;
}
#endif
