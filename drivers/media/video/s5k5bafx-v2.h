/*
 * Driver for S5K5BAFX 2M ISP from Samsung
 *
 * Copyright (c) 2011, Samsung Electronics. All rights reserved
 * Author: dongseong.lim
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __S5K5BAFX_H
#define __S5K5BAFX_H

#include <linux/types.h>

#define S5K5BAFX_DRIVER_NAME	"S5K5BAFX"

/************************************
 * FEATURE DEFINITIONS
 ************************************/
/* #define S5K5BAFX_USLEEP */
#define S5K5BAFX_BURST_MODE
/* #define CONFIG_LOAD_FILE */
#define SUPPORT_FACTORY_TEST

/** Debuging Feature **/
#define CONFIG_CAM_DEBUG
/* #define CONFIG_CAM_TRACE *//* Enable it with CONFIG_CAM_DEBUG */
/***********************************/

#define CAM_MAJOR	119

#define TAG_NAME	"["S5K5BAFX_DRIVER_NAME"]"" "
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

enum s5k5bafx_fps_index {
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
	
struct s5k5bafx_framesize {
	u32 width;
	u32 height;
};

struct s5k5bafx_fps {
	u32 index;
	u32 fps;
};

struct s5k5bafx_exif {
	u32 exp_time_den;
	u32 shutter_speed;
	u16 iso;
};

struct s5k5bafx_stream_time {
	struct timeval curr_time;
	struct timeval before_time;
};

#define GET_ELAPSED_TIME(cur, before) \
		(((cur).tv_sec - (before).tv_sec) * USEC_PER_SEC \
		+ ((cur).tv_usec - (before).tv_usec))


#ifdef CONFIG_LOAD_FILE
struct s5k5bafx_regset_table {
	const u32	*reg;
	int		array_size;
	char		*name;
};

#define S5K5BAFX_REGSET(x, y)		\
	[(x)] = {					\
		.reg		= (y),			\
		.array_size	= ARRAY_SIZE((y)),	\
		.name		= #y,			\
}

#define S5K5BAFX_REGSET_TABLE(y)		\
	{					\
		.reg		= (y),			\
		.array_size	= ARRAY_SIZE((y)),	\
		.name		= #y,			\
}
#else
struct s5k5bafx_regset_table {
	const u32	*reg;
	int		array_size;
};

#define S5K5BAFX_REGSET(x, y)		\
	[(x)] = {					\
		.reg		= (y),			\
		.array_size	= ARRAY_SIZE((y)),	\
}

#define S5K5BAFX_REGSET_TABLE(y)		\
	{					\
		.reg		= (y),			\
		.array_size	= ARRAY_SIZE((y)),	\
}
#endif

#define EV_MIN_VLAUE	EV_MINUS_4
#define ANTI_BANDING_MAX	ANTI_BANDING_50_60Hz
#define CAM_VT_MODE_FD		(CAM_VT_MODE_VOIP + 1)
#undef CAM_VT_MODE_MAX
#define CAM_VT_MODE_MAX		(CAM_VT_MODE_FD + 1)
#define INIT_MODE_MAX		CAM_VT_MODE_MAX
#define GET_EV_INDEX(EV)	((EV) - (EV_MIN_VLAUE))

struct s5k5bafx_regs {
	struct s5k5bafx_regset_table ev[GET_EV_INDEX(EV_MAX_V4L2)];
	struct s5k5bafx_regset_table blur[BLUR_LEVEL_MAX];
	/* struct s5k5bafx_regset_table capture_size[S5K5BAFX_CAPTURE_MAX];*/
	struct s5k5bafx_regset_table preview_start;
	struct s5k5bafx_regset_table capture_start;
	struct s5k5bafx_regset_table fps[I_FPS_MAX];
	struct s5k5bafx_regset_table init[INIT_MODE_MAX];
	struct s5k5bafx_regset_table init_recording[ANTI_BANDING_MAX];
	struct s5k5bafx_regset_table get_light_level;
	struct s5k5bafx_regset_table get_iso;
	struct s5k5bafx_regset_table get_shutterspeed;
	struct s5k5bafx_regset_table stream_stop;
	struct s5k5bafx_regset_table dtp_on;
	struct s5k5bafx_regset_table dtp_off;
};

/*
 * Driver information
 */
struct s5k5bafx_state {
	struct v4l2_subdev sd;
	struct s5k5bafx_platform_data *pdata;
	/*
	 * req_fmt is the requested format from the application.
	 * set_fmt is the output format of the camera. Finally FIMC
	 * converts the camera output(set_fmt) to the requested format
	 * with hardware scaler.
	 */
	struct v4l2_pix_format req_fmt;
	struct s5k5bafx_framesize default_frmsizes;
	struct s5k5bafx_framesize preview_frmsizes;
	struct s5k5bafx_framesize capture_frmsizes;
	struct s5k5bafx_exif exif;
	struct s5k5bafx_stream_time stream_time;
	const struct s5k5bafx_regs *regs;
	struct mutex ctrl_lock;

	enum v4l2_sensor_mode sensor_mode;
	s32 *init_mode;
	s32 vt_mode;
	s32 anti_banding;
	s32 req_fps;
	s32 fps;
#ifdef CONFIG_USE_SW_I2C
	u32 cpufreq_lock_level;
#endif
	u8 *dbg_level;
#ifdef S5K5BAFX_BURST_MODE
	u8 *burst_buf;
#endif
	u32 check_dataline:1;
	u32 need_wait_streamoff:1;
	u32 initialized:1;
};

#if !defined(CONFIG_MACH_PX)
extern struct class *camera_class;
#endif

static inline struct s5k5bafx_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct s5k5bafx_state, sd);
}

static inline void msleep_debug(struct v4l2_subdev *sd, u32 msecs)
{
	u32 delta_halfrange; /* in us unit */

	if (unlikely(!msecs))
		return;

	cam_dbg("delay for %dms\n", msecs);

	if (msecs <= 7)
		delta_halfrange = 100;
	else
		delta_halfrange = 300;

	if (msecs <= 20)
		usleep_range((msecs * 1000 - delta_halfrange),
			(msecs * 1000 + delta_halfrange));
	else
		msleep(msecs);
}

/* Start-up time for Smart-stay
 * device open + start preview + callback time */
#define SMARTSTAY_STARTUP_TIME	(20 + 1000 + 0)

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

#define TUNING_FILE_PATH "/mnt/sdcard/s5k5bafx_regs.h"
#endif

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

#ifdef CONFIG_MACH_P8
#include  "s5k5bafx_regs-p8.h"
#elif defined(CONFIG_MACH_U1_KOR_LGT)
#include  "s5k5bafx_setfile_lgt.h"
#else
#include  "s5k5bafx_setfile.h"
#endif

#endif /* __S5K5BAFX_H */
