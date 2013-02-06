/*
 * Driver for SR200PC20 2M ISP from Samsung
 *
 * Copyright (c) 2011, Samsung Electronics. All rights reserved
 * Author: dongseong.lim
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

/************************************
 * FEATURE DEFINITIONS
 ************************************/
/* #define SR200PC20_USLEEP */
/* #define CONFIG_LOAD_FILE */
#define SUPPORT_FACTORY_TEST
#define NEW_CAM_DRV

/** Debuging Feature **/
#define CONFIG_CAM_DEBUG
#define CONFIG_CAM_TRACE /* Enable it with CONFIG_CAM_DEBUG */
/***********************************/

#define TAG_NAME	"["SR200PC20_DRIVER_NAME"]"" "
#define cam_err(fmt, ...)	\
	printk(KERN_ERR TAG_NAME fmt, ##__VA_ARGS__)
#define cam_warn(fmt, ...)	\
	printk(KERN_WARNING TAG_NAME fmt, ##__VA_ARGS__)
#define cam_info(fmt, ...)	\
	printk(KERN_INFO TAG_NAME fmt, ##__VA_ARGS__)

#if defined(CONFIG_CAM_DEBUG)
#define cam_dbg(fmt, ...)	\
	printk(KERN_DEBUG TAG_NAME fmt, ##__VA_ARGS__)
#else
#define cam_dbg(fmt, ...)	\
	do { \
		if (*to_state(sd)->dbg_level & CAMDBG_LEVEL_DEBUG) \
			printk(KERN_DEBUG TAG_NAME fmt, ##__VA_ARGS__); \
	} while (0)
#endif /* CONFIG_CAM_DEBUG */

#if defined(CONFIG_CAM_DEBUG) && defined(CONFIG_CAM_TRACE)
#define cam_trace(fmt, ...)	cam_dbg("%s: " fmt, __func__, ##__VA_ARGS__);
#else
#define cam_trace(fmt, ...)	\
	do { \
		if (*to_state(sd)->dbg_level & CAMDBG_LEVEL_TRACE) \
			printk(KERN_DEBUG TAG_NAME "%s: " fmt, \
				__func__, ##__VA_ARGS__); \
	} while (0)
#endif

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



enum stream_cmd {
	STREAM_STOP,
	STREAM_START,
};

enum sr200pc20_fps_index {
	I_FPS_0,
	I_FPS_7,
	I_FPS_10,
	I_FPS_12,
	I_FPS_15,
	I_FPS_25,
	I_FPS_30,
	I_FPS_MAX,
};
#define DEFAULT_FPS	15

struct sr200pc20_framesize {
	u32 width;
	u32 height;
};

struct sr200pc20_fps {
	u32 index;
	u32 fps;
};

struct sr200pc20_exif {
	u16 exp_time_den;
	u16 iso;
	u32 shutter_speed;
};

struct sr200pc20_stream_time {
	struct timeval curr_time;
	struct timeval before_time;
};

#define GET_ELAPSED_TIME(cur, before) \
		(((cur).tv_sec - (before).tv_sec) * USEC_PER_SEC \
		+ ((cur).tv_usec - (before).tv_usec))

typedef struct regs_array_type {
	u16 subaddr;
	u16 value;
} regs_short_t;

#ifdef CONFIG_LOAD_FILE
struct sr200pc20_regset_table {
	const regs_short_t	*reg;
	int			array_size;
	char			*name;
};

#define SR200PC20_REGSET(x, y)		\
	[(x)] = {					\
		.reg		= (y),			\
		.array_size	= ARRAY_SIZE((y)),	\
		.name		= #y,			\
}

#define SR200PC20_REGSET_TABLE(y)		\
	{					\
		.reg		= (y),			\
		.array_size	= ARRAY_SIZE((y)),	\
		.name		= #y,			\
}
#else
struct sr200pc20_regset_table {
	const regs_short_t	*reg;
	int			array_size;
};

#define SR200PC20_REGSET(x, y)		\
	[(x)] = {					\
		.reg		= (y),			\
		.array_size	= ARRAY_SIZE((y)),	\
}

#define SR200PC20_REGSET_TABLE(y)		\
	{					\
		.reg		= (y),			\
		.array_size	= ARRAY_SIZE((y)),	\
}
#endif

#define EV_MIN_VLAUE	EV_MINUS_4
#define GET_EV_INDEX(EV)	((EV) - (EV_MIN_VLAUE))

struct sr200pc20_regs {
	struct sr200pc20_regset_table ev[GET_EV_INDEX(EV_MAX_V4L2)];
	struct sr200pc20_regset_table blur[BLUR_LEVEL_MAX];
	/* struct sr200pc20_regset_table capture_size[SR200PC20_CAPTURE_MAX];*/
	struct sr200pc20_regset_table preview_start;
	struct sr200pc20_regset_table capture_start;
	struct sr200pc20_regset_table fps[I_FPS_MAX];
	struct sr200pc20_regset_table init;
	struct sr200pc20_regset_table init_vt;
	struct sr200pc20_regset_table init_vt_wifi;
	struct sr200pc20_regset_table init_recording;
	struct sr200pc20_regset_table get_light_level;
	struct sr200pc20_regset_table stream_stop;
	struct sr200pc20_regset_table dtp_on;
	struct sr200pc20_regset_table dtp_off;
};

/*
 * Driver information
 */
struct sr200pc20_state {
	struct v4l2_subdev sd;
	struct sr200pc20_platform_data *pdata;
	/*
	 * req_fmt is the requested format from the application.
	 * set_fmt is the output format of the camera. Finally FIMC
	 * converts the camera output(set_fmt) to the requested format
	 * with hardware scaler.
	 */
	struct v4l2_pix_format req_fmt;
	struct sr200pc20_framesize default_frmsizes;
	struct sr200pc20_framesize preview_frmsizes;
	struct sr200pc20_framesize capture_frmsizes;
	struct sr200pc20_exif exif;
	struct sr200pc20_stream_time stream_time;
	const struct sr200pc20_regs *regs;
	struct mutex ctrl_lock;

	enum v4l2_sensor_mode sensor_mode;
	s32 vt_mode;
	s32 req_fps;
	s32 fps;
	u8 *dbg_level;

	u32 check_dataline:1;
	u32 need_wait_streamoff:1;
	u32 first_preview:1;
	u32 initialized:1;
};

static inline struct sr200pc20_state *to_state(struct v4l2_subdev *sd) {
	return container_of(sd, struct sr200pc20_state, sd);
}

static inline void debug_msleep(struct v4l2_subdev *sd, u32 msecs)
{
	cam_dbg("delay for %dms\n", msecs);
	msleep(msecs);
}

/*********** Sensor specific ************/
#define DELAY_SEQ               0xFF
#define SR200PC20_CHIP_ID	0x92


#ifdef CONFIG_LOAD_FILE
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

struct test {
	u8 data;
	struct test *nextBuf;
};
static struct test *testBuf;
static s32 large_file;

#define TEST_INIT	\
{			\
	.data = 0;	\
	.nextBuf = NULL;	\
}
#if 0
#define dbg_setfile(fmt, ...)	\
	printk(KERN_ERR TAG_NAME fmt, ##__VA_ARGS__)
#else
#define dbg_setfile(fmt, ...)
#endif /* 0 */

#ifdef CONFIG_VIDEO_SR200PC20_P2
#define TUNING_FILE_PATH "/mnt/sdcard/sr200pc20_regs-p2.h"
#elif defined(CONFIG_VIDEO_SR200PC20_P4W)
#define TUNING_FILE_PATH "/mnt/sdcard/sr200pc20_regs-p4w.h"
#else
#define TUNING_FILE_PATH NULL
#endif /* CONFIG_VIDEO_SR200PC20_P2 */

#endif /* CONFIG_LOAD_FILE */

#ifdef CONFIG_VIDEO_SR200PC20_P2
#include  "sr200pc20_regs-p4w.h"
/* #include  "sr200pc20_regs-p2.h" */
#else
#include  "sr200pc20_regs-p4w.h"
#endif

#endif /* __SR200PC20_H */

