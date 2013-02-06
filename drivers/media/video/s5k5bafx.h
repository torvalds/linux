/*
 * Driver for S5K5BAFX 2M ISP from Samsung
 *
 * Copyright (C) 2011,
 * DongSeong Lim<dongseong.lim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __S5K5BAFX_H
#define __S5K5BAFX_H

#include <linux/types.h>

/* #define CONFIG_CAM_DEBUG */

#define CAM_MAJOR	119
#define S5K5BAFX_DRIVER_NAME	"S5K5BAFX"

typedef enum {
	STREAM_STOP,
	STREAM_START,
} stream_cmd_t;

struct s5k5bafx_framesize {
	u32 width;
	u32 height;
};

struct s5k5bafx_exif {
	u32 shutter_speed;
	u16 iso;
};


/*
 * Driver information
 */
struct s5k5bafx_state {
	struct v4l2_subdev sd;
	struct device *s5k5bafx_dev;
	/*
	 * req_fmt is the requested format from the application.
	 * set_fmt is the output format of the camera. Finally FIMC
	 * converts the camera output(set_fmt) to the requested format
	 * with hardware scaler.
	 */
	struct v4l2_pix_format req_fmt;
	struct s5k5bafx_framesize preview_frmsizes;
	struct s5k5bafx_framesize capture_frmsizes;
	struct s5k5bafx_exif exif;

	enum v4l2_sensor_mode sensor_mode;
	s32 vt_mode;
	s32 check_dataline;
	s32 anti_banding;
	u32 req_fps;
	u32 set_fps;
	u32 initialized;
};

static inline struct s5k5bafx_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct s5k5bafx_state, sd);
}

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
#define S5K5BAFX_USLEEP
/* #define CONFIG_LOAD_FILE */


/*********** Sensor specific ************/
#define S5K5BAFX_CHIP_ID	0x05BA
#define S5K5BAFX_CHIP_REV	0xA0

/* #define S5K5BAFX_100MS_DELAY	0xAA55AA5F */
/* #define S5K5BAFX_10MS_DELAY	0xAA55AA5E */
#define S5K5BAFX_DELAY		0xFFFF0000
#define S5K5BAFX_DEF_APEX_DEN	100

/* Register address */
#define REG_PAGE_SHUTTER    0x7000
#define REG_ADDR_SHUTTER    0x14D0
#define REG_PAGE_ISO        0x7000
#define REG_ADDR_ISO        0x14C8


/* Start-up time for Smart-stay
 * device open + start preview + callback time */
#define SMARTSTAY_STARTUP_TIME	(20 + 1285 + 905)

#ifdef CONFIG_MACH_U1_KOR_LGT
#include  "s5k5bafx_setfile_lgt.h"
#else
#include  "s5k5bafx_setfile.h"
#endif

#endif /* __S5K5BAFX_H */
