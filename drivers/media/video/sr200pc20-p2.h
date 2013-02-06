/*
 * Driver for SR200PC20 2M ISP from Samsung
 *
 * Copyright (C) 2011,
 * DongSeong Lim<dongseong.lim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __SR200PC20_H
#define __SR200PC20_H

#include <linux/types.h>

#define SR200PC20_DRIVER_NAME	"SR200PC20"

#define NEW_CAM_DRV

typedef enum {
	STREAM_STOP,
	STREAM_START,
} stream_cmd_t;

struct sr200pc20_framesize {
	u32 width;
	u32 height;
};

struct sr200pc20_exif {
	u32 shutter_speed;
	u16 iso;
};


/*
 * Driver information
 */
struct sr200pc20_state {
	struct v4l2_subdev sd;
	/*
	 * req_fmt is the requested format from the application.
	 * set_fmt is the output format of the camera. Finally FIMC
	 * converts the camera output(set_fmt) to the requested format
	 * with hardware scaler.
	 */
	struct v4l2_pix_format req_fmt;
	struct sr200pc20_framesize preview_frmsizes;
	struct sr200pc20_framesize capture_frmsizes;
	struct sr200pc20_exif exif;

	enum v4l2_sensor_mode sensor_mode;
	s32 vt_mode;
	s32 check_dataline;
	u32 req_fps;
	u32 set_fps;
	u32 initialized;
	u32 anti_banding;
};

static inline struct sr200pc20_state *to_state(struct v4l2_subdev *sd) {
	return container_of(sd, struct sr200pc20_state, sd);
}

/* #define CONFIG_CAM_DEBUG */
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

#define CHECK_ERR_COND(condition, ret)	\
	do { if (unlikely(condition)) return (ret); } while (0)
#define CHECK_ERR_COND_MSG(condition, ret, fmt, ...) \
	if (unlikely(condition)) { \
		cam_err("%s: ERROR, " fmt, __func__, ##__VA_ARGS__); \
		return ret; \
	}

#define CHECK_ERR(x)	CHECK_ERR_COND(((x) < 0), (x))
#define CHECK_ERR_MSG(x, fmt, ...) \
	CHECK_ERR_COND_MSG(((x) < 0), (x), fmt, ##__VA_ARGS__)

/************ driver feature ************/
#define sr200pc20_USLEEP
/* #define CONFIG_LOAD_FILE */

typedef struct regs_array_type {
    unsigned short subaddr;
    unsigned short value;
} regs_short_t;

#if 1 /*defined (CONFIG_MACH_P2_REV02)*/
#include  "sr200pc20_regs-p2.h"
#else
#include  "sr200pc20_regs.h"
#endif

#endif /* __SR200PC20_H */
