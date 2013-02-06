/*
 * Driver for S5K5BBGX 2M ISP from Samsung
 *
 * Copyright (C) 2011,
 * DongSeong Lim<dongseong.lim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __S5K5BBGX_H
#define __S5K5BBGX_H

#include <linux/types.h>

#define S5K5BBGX_DRIVER_NAME	"S5K5BBGX"

extern struct class *camera_class;
extern int s5k5bbgx_power_reset(void);

enum stream_cmd_t {
	STREAM_STOP,
	STREAM_START,
};

struct s5k5bbgx_framesize {
	u32 width;
	u32 height;
};

struct s5k5bbgx_exif {
	u32 shutter_speed;
	u16 iso;
};


/*
 * Driver information
 */
struct s5k5bbgx_state {
	struct v4l2_subdev sd;
struct device *s5k5bbgx_dev;
	/*
	 * req_fmt is the requested format from the application.
	 * set_fmt is the output format of the camera. Finally FIMC
	 * converts the camera output(set_fmt) to the requested format
	 * with hardware scaler.
	 */
	struct v4l2_pix_format req_fmt;
	struct s5k5bbgx_framesize preview_frmsizes;
	struct s5k5bbgx_framesize capture_frmsizes;
	struct s5k5bbgx_exif exif;

	enum v4l2_sensor_mode sensor_mode;
	s32 vt_mode;
	s32 check_dataline;
	u32 req_fps;
	u32 set_fps;
	u32 initialized;
};

static inline struct s5k5bbgx_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct s5k5bbgx_state, sd);
}

/*#define CONFIG_CAM_DEBUG*/
#define cam_warn(fmt, ...)	\
	do { \
		printk(KERN_WARNING "%s: " fmt, __func__, ##__VA_ARGS__); \
	} while (0)

#define cam_err(fmt, ...)	\
	do { \
		printk(KERN_ERR "%s: " fmt, __func__, ##__VA_ARGS__); \
	} while (0)

#define cam_info(fmt, ...)	\
	do { \
		printk(KERN_INFO "%s: " fmt, __func__, ##__VA_ARGS__); \
	} while (0)

#ifdef CONFIG_CAM_DEBUG
#define cam_dbg(fmt, ...)	\
	do { \
		printk(KERN_DEBUG "%s: " fmt, __func__, ##__VA_ARGS__); \
	} while (0)
#else
#define cam_dbg(fmt, ...)
#endif /* CONFIG_CAM_DEBUG */


/************ driver feature ************/
#define S5K5BBGX_USLEEP
/* #define CONFIG_LOAD_FILE */


/*********** Sensor specific ************/
/* #define S5K5BBGX_100MS_DELAY	0xAA55AA5F */
/* #define S5K5BBGX_10MS_DELAY	0xAA55AA5E */
#define S5K5BBGX_DELAY		0xFFFF0000
#define S5K5BBGX_DEF_APEX_DEN	100

/* Register address */
#define REG_PAGE_SHUTTER    0x7000
#define REG_ADDR_SHUTTER    0x238C
#define REG_PAGE_ISO        0x7000
#define REG_ADDR_ISO        0x2390
#define REG_PAGE_CAPTURE_STATUS    0x7000
#define REG_ADDR_CAPTURE_STATUS    0x0142
#define S5K5BBGX_READ_STATUS_RETRIES   20

#include  "s5k5bbgx_setfile.h"

#endif /* __S5K5BBGX_H */
