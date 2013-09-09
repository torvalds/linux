/* drivers/media/video/s5k4ecgx.h
 *
 * Driver for s5k4ecgx (2lane Mipi 5MP Camera) from SEC(LSI), firmware EVT1.1
 *
 * Copyright (C) 2012, Hardkernel Co.,Ltd.
 *
 * Author: ruppi.kim@hardkernel.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __S5K4ECGX_H__
#define __S5K4ECGX_H__
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/completion.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/s5k4ecgx_platform.h>
#include <linux/videodev2_exynos_camera.h>
#include <linux/videodev2_exynos_media.h>
#include <linux/workqueue.h>

#define S5K4ECGX_DRIVER_NAME	"S5K4ECGX"

#define S5K4ECGX_DELAY		0xFFFF0000

/************************************
 * FEATURE DEFINITIONS
 ************************************/
#define FEATURE_YUV_CAPTURE
/* #define CONFIG_LOAD_FILE */ /* for tuning */

/** Debuging Feature **/
#define CONFIG_CAM_DEBUG
#define CONFIG_CAM_TRACE /* Enable it with CONFIG_CAM_DEBUG */
#define CONFIG_CAM_AF_DEBUG /* Enable it with CONFIG_CAM_DEBUG */
/* #define DEBUG_WRITE_REGS */
/***********************************/
//#define CONFIG_VIDEO_S5K4ECGX_DEBUG
#ifdef CONFIG_VIDEO_S5K4ECGX_DEBUG
enum {
	S5K4ECGX_DEBUG_I2C		= 1U << 0,
	S5K4ECGX_DEBUG_I2C_BURSTS	= 1U << 1,
};
static uint32_t s5k4ecgx_debug_mask = S5K4ECGX_DEBUG_I2C_BURSTS | S5K4ECGX_DEBUG_I2C;
module_param_named(debug_mask, s5k4ecgx_debug_mask, uint, S_IWUSR | S_IRUGO);

#define s5k4ecgx_debug(mask, x...) \
	do { \
		if (s5k4ecgx_debug_mask & mask) \
			pr_info(x);	\
	} while (0)
#else
#define s5k4ecgx_debug(mask, x...)
#endif

#define TAG_NAME	"["S5K4ECGX_DRIVER_NAME"]"" "
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
#endif

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

#if defined(CONFIG_CAM_DEBUG) && defined(CONFIG_CAM_AF_DEBUG)
#define af_dbg(fmt, ...)	cam_dbg(fmt, ##__VA_ARGS__);
#else
#define af_dbg(fmt, ...)
#endif

#define CHECK_ERR_COND(condition, ret)	\
	do { if (unlikely(condition)) return ret; } while (0)
#define CHECK_ERR_COND_MSG(condition, ret, fmt, ...) \
		if (unlikely(condition)) { \
			cam_err("%s: ERROR, " fmt, __func__, ##__VA_ARGS__); \
			return ret; \
		}

#define CHECK_ERR(x)	CHECK_ERR_COND(((x) < 0), (x))
#define CHECK_ERR_MSG(x, fmt, ...) \
	CHECK_ERR_COND_MSG(((x) < 0), (x), fmt, ##__VA_ARGS__)


#ifdef CONFIG_LOAD_FILE
#define S5K4ECGX_BURST_WRITE_REGS(sd, A) ({ \
	int ret; \
		cam_info("BURST_WRITE_REGS: reg_name=%s from setfile\n", #A); \
		ret = s5k4ecgx_write_regs_from_sd(sd, #A); \
		ret; \
	})
#else
#define S5K4ECGX_BURST_WRITE_REGS(sd, A) \
	s5k4ecgx_burst_write_regs(sd, A, (sizeof(A) / sizeof(A[0])), #A)
#endif

/* result values returned to HAL */
enum {
	AUTO_FOCUS_FAILED,
	AUTO_FOCUS_DONE,
	AUTO_FOCUS_CANCELLED,
};

enum af_operation_status {
	AF_NONE = 0,
	AF_START,
	AF_CANCEL,
	AF_INITIAL,
};


enum preflash_status {
	PREFLASH_NONE = 0,
	PREFLASH_OFF,
	PREFLASH_ON,
};

enum s5k4ecgx_oprmode {
	S5K4ECGX_OPRMODE_VIDEO = 0,
	S5K4ECGX_OPRMODE_IMAGE = 1,
};

enum stream_cmd {
	STREAM_STOP,
	STREAM_START,
};

enum wide_req_cmd {
	WIDE_REQ_NONE,
	WIDE_REQ_CHANGE,
	WIDE_REQ_RESTORE,
};

enum s5k4ecgx_preview_frame_size {
	S5K4ECGX_PREVIEW_QCIF = 0,	/*1 176x144 */
	S5K4ECGX_PREVIEW_CIF,		/*2 352x288 */ 
	S5K4ECGX_PREVIEW_VGA,		/*3 640x480 */
	S5K4ECGX_PREVIEW_D1,		/*4 720x480 */
	S5K4ECGX_PREVIEW_960,		/*5 960x640 */
	S5K4ECGX_PREVIEW_720P,		/*6 1280x720 */
	S5K4ECGX_PREVIEW_MAX,
};

/* Capture Size List: Capture size is defined as below.
 *
 *	S5K4ECGX_CAPTURE_VGA:		640x480
 *	S5K4ECGX_CAPTURE_WVGA:		800x480
 *	S5K4ECGX_CAPTURE_SVGA:		800x600
 *	S5K4ECGX_CAPTURE_WSVGA:		1024x600
 *	S5K4ECGX_CAPTURE_1MP:		1280x960
 *	S5K4ECGX_CAPTURE_W1MP:		1600x960
 *	S5K4ECGX_CAPTURE_2MP:		UXGA - 1600x1200
 *	S5K4ECGX_CAPTURE_W2MP:		35mm Academy Offset Standard 1.66
 *					2048x1232, 2.4MP
 *	S5K4ECGX_CAPTURE_3MP:		QXGA  - 2048x1536
 *	S5K4ECGX_CAPTURE_W4MP:		WQXGA - 2560x1536
 *	S5K4ECGX_CAPTURE_5MP:		2560x1920
 */

enum s5k4ecgx_capture_frame_size {
	S5K4ECGX_CAPTURE_VGA = 0,	/* 640x480 */
	S5K4ECGX_CAPTURE_1MP,		/* 1280x960 */
	S5K4ECGX_CAPTURE_2MP,		/* UXGA  - 1600x1200 */
	S5K4ECGX_CAPTURE_3MP,		/* QXGA  - 2048x1536 */
	S5K4ECGX_CAPTURE_5MP,		/* 2560x1920 */
	S5K4ECGX_CAPTURE_MAX,
};

#ifdef CONFIG_VIDEO_S5K4ECGX_P2
#define PREVIEW_WIDE_SIZE	S5K4ECGX_PREVIEW_1024x552
#else
#define PREVIEW_WIDE_SIZE	S5K4ECGX_PREVIEW_1024x576
#endif
#define CAPTURE_WIDE_SIZE	S5K4ECGX_CAPTURE_W2MP

enum s5k4ecgx_fps_index {
	I_FPS_0,
	I_FPS_7,
	I_FPS_10,
	I_FPS_12,
	I_FPS_15,
	I_FPS_20,
	I_FPS_30,
	I_FPS_MAX,
};

enum ae_awb_lock {
	AEAWB_UNLOCK = 0,
	AEAWB_LOCK,
	AEAWB_LOCK_MAX,
};

struct s5k4ecgx_control {
	u32 id;
	s32 value;
	s32 default_value;
};

#define S5K4ECGX_INIT_CONTROL(ctrl_id, default_val) \
	{					\
		.id = ctrl_id,			\
		.value = default_val,		\
		.default_value = default_val,	\
	}

struct s5k4ecgx_framesize {
	s32 index;
	u32 width;
	u32 height;
};

#define FRM_RATIO(framesize) \
	(((framesize)->width) * 10 / ((framesize)->height))

struct s5k4ecgx_fps {
	u32 index;
	u32 fps;
};

struct s5k4ecgx_version {
	u32 major;
	u32 minor;
};

struct s5k4ecgx_date_info {
	u32 year;
	u32 month;
	u32 date;
};

enum s5k4ecgx_runmode {
	S5K4ECGX_RUNMODE_NOTREADY,
	S5K4ECGX_RUNMODE_INIT,
	/*S5K4ECGX_RUNMODE_IDLE,*/
	S5K4ECGX_RUNMODE_RUNNING, /* previewing */
	S5K4ECGX_RUNMODE_RUNNING_STOP,
	S5K4ECGX_RUNMODE_CAPTURING,
	S5K4ECGX_RUNMODE_CAPTURE_STOP,
};

struct s5k4ecgx_firmware {
	u32 addr;
	u32 size;
};

struct s5k4ecgx_jpeg_param {
	u32 enable;
	u32 quality;
	u32 main_size;		/* Main JPEG file size */
	u32 thumb_size;		/* Thumbnail file size */
	u32 main_offset;
	u32 thumb_offset;
	u32 postview_offset;
};

struct s5k4ecgx_position {
	s32 x;
	s32 y;
};

struct s5k4ecgx_rect {
	s32 x;
	s32 y;
	u32 width;
	u32 height;
};

struct gps_info_common {
	u32 direction;
	u32 dgree;
	u32 minute;
	u32 second;
};

struct s5k4ecgx_gps_info {
	u8 gps_buf[8];
	u8 altitude_buf[4];
	s32 gps_timeStamp;
};

struct s5k4ecgx_exif {
	u16 exp_time_den;
	u16 iso;
	u16 flash;

	int tv;			/* shutter speed */
	int bv;			/* brightness */
	int ebv;		/* exposure bias */
};

/* EXIF - flash filed */
#define EXIF_FLASH_FIRED		(0x01)
#define EXIF_FLASH_MODE_FIRING		(0x01)
#define EXIF_FLASH_MODE_SUPPRESSION	(0x01 << 1)
#define EXIF_FLASH_MODE_AUTO		(0x03 << 3)

struct s5k4ecgx_regset {
	u32 size;
	u8 *data;
};

struct s5k4ecgx_stream_time {
	struct timeval curr_time;
	struct timeval before_time;
};

#define GET_ELAPSED_TIME(cur, before) \
		(((cur).tv_sec - (before).tv_sec) * USEC_PER_SEC \
		+ ((cur).tv_usec - (before).tv_usec))

#ifdef CONFIG_LOAD_FILE
#define DEBUG_WRITE_REGS
struct s5k4ecgx_regset_table {
	const char	*const name;
};

#define S5K4ECGX_REGSET(x, y)		\
	[(x)] = {			\
		.name		= #y,	\
}

#define S5K4ECGX_REGSET_TABLE(y)	\
	{				\
		.name		= #y,	\
}
#else
struct s5k4ecgx_regset_table {
	const u32	*const reg;
	const u32	array_size;
#ifdef DEBUG_WRITE_REGS
	const char	*const name;
#endif
};

#ifdef DEBUG_WRITE_REGS
#define S5K4ECGX_REGSET(x, y)		\
	[(x)] = {					\
		.reg		= (y),			\
		.array_size	= ARRAY_SIZE((y)),	\
		.name		= #y,			\
}

#define S5K4ECGX_REGSET_TABLE(y)		\
	{					\
		.reg		= (y),			\
		.array_size	= ARRAY_SIZE((y)),	\
		.name		= #y,			\
}
#else
#define S5K4ECGX_REGSET(x, y)		\
	[(x)] = {					\
		.reg		= (y),			\
		.array_size	= ARRAY_SIZE((y)),	\
}

#define S5K4ECGX_REGSET_TABLE(y)		\
	{					\
		.reg		= (y),			\
		.array_size	= ARRAY_SIZE((y)),	\
}
#endif /* DEBUG_WRITE_REGS */
#endif /* CONFIG_LOAD_FILE */

struct s5k4ecgx_regs {
	struct s5k4ecgx_regset_table ev[EV_MAX];
	struct s5k4ecgx_regset_table metering[METERING_MAX];
	struct s5k4ecgx_regset_table iso[ISO_MAX];
	struct s5k4ecgx_regset_table effect[IMAGE_EFFECT_MAX];
	struct s5k4ecgx_regset_table white_balance[WHITE_BALANCE_MAX];
	struct s5k4ecgx_regset_table preview_size[S5K4ECGX_PREVIEW_MAX];
	struct s5k4ecgx_regset_table capture_start[S5K4ECGX_CAPTURE_MAX];
	struct s5k4ecgx_regset_table scene_mode[SCENE_MODE_MAX];
	struct s5k4ecgx_regset_table saturation[SATURATION_MAX];
	struct s5k4ecgx_regset_table contrast[CONTRAST_MAX];
	struct s5k4ecgx_regset_table sharpness[SHARPNESS_MAX];
	struct s5k4ecgx_regset_table fps[I_FPS_MAX];
	struct s5k4ecgx_regset_table preview_return;
	struct s5k4ecgx_regset_table jpeg_quality_high;
	struct s5k4ecgx_regset_table jpeg_quality_normal;
	struct s5k4ecgx_regset_table jpeg_quality_low;
	struct s5k4ecgx_regset_table flash_start;
	struct s5k4ecgx_regset_table flash_end;

	struct s5k4ecgx_regset_table af_assist_flash_start;
	struct s5k4ecgx_regset_table af_assist_flash_end;
	struct s5k4ecgx_regset_table af_low_light_mode_on;
	struct s5k4ecgx_regset_table af_low_light_mode_off;

	struct s5k4ecgx_regset_table ae_awb_lock_on;
	struct s5k4ecgx_regset_table ae_awb_lock_off;
	struct s5k4ecgx_regset_table restore_cap;
	struct s5k4ecgx_regset_table change_wide_cap;
	struct s5k4ecgx_regset_table af_macro_mode_1;
	struct s5k4ecgx_regset_table af_macro_mode_2;
	struct s5k4ecgx_regset_table af_macro_mode_3;
	struct s5k4ecgx_regset_table af_normal_mode_1;
	struct s5k4ecgx_regset_table af_normal_mode_2;
	struct s5k4ecgx_regset_table af_normal_mode_3;

	struct s5k4ecgx_regset_table single_af_start;
	struct s5k4ecgx_regset_table video_af_start;
	struct s5k4ecgx_regset_table single_af_off_1;
	struct s5k4ecgx_regset_table single_af_off_2;
	struct s5k4ecgx_regset_table init_arm;
	struct s5k4ecgx_regset_table init_reg;

	struct s5k4ecgx_regset_table get_ae_stable_status;
	struct s5k4ecgx_regset_table get_light_level;
	struct s5k4ecgx_regset_table get_1st_af_search_status;
	struct s5k4ecgx_regset_table get_2nd_af_search_status;
	struct s5k4ecgx_regset_table get_capture_status;
	
	struct s5k4ecgx_regset_table get_esd_status;
	struct s5k4ecgx_regset_table get_iso;
	struct s5k4ecgx_regset_table get_ev;
	struct s5k4ecgx_regset_table get_ae_stable;
	struct s5k4ecgx_regset_table get_shutterspeed;
	struct s5k4ecgx_regset_table update_preview;
	struct s5k4ecgx_regset_table update_hd_preview;
	struct s5k4ecgx_regset_table stream_stop;
};

struct s5k4ecgx_state {
	struct s5k4ecgx_platform_data *pdata;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_pix_format req_fmt;
	struct v4l2_streamparm strm;
		
	struct s5k4ecgx_framesize *preview;
	struct s5k4ecgx_framesize *capture;
	struct s5k4ecgx_exif exif;
	struct s5k4ecgx_jpeg_param jpeg;
	struct s5k4ecgx_stream_time stream_time;
	const struct s5k4ecgx_regs *regs;
	struct mutex ctrl_lock;
	struct mutex af_lock;
	enum s5k4ecgx_runmode runmode;
	enum v4l2_sensor_mode sensor_mode;
	enum v4l2_pix_format_mode format_mode;
	enum v4l2_flash_mode flash_mode;
	enum v4l2_scene_mode scene_mode;
	enum v4l2_wb_mode wb_mode;
	enum af_operation_status af_status;
	/* To switch from nornal ratio to wide ratio.*/
	enum wide_req_cmd wide_cmd;

	bool sensor_af_in_low_light_mode;

	s32 vt_mode;
	s32 req_fps;
	s32 fps;
	s32 freq;		/* MCLK in Hz */
	u32 one_frame_delay_ms;
	u32 light_level;	/* light level */
	u8 *dbg_level;

#if defined(CONFIG_EXYNOS4_CPUFREQ)
	u32 cpufreq_lock_level;
#endif
	u32 recording:1;
	u32 hd_videomode:1;
	u32 flash_on:1;
	u32 ignore_flash:1;
	u32 need_update_frmsize:1;
	u32 need_wait_streamoff:1;
	u32 initialized:1;
};

static inline struct  s5k4ecgx_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct s5k4ecgx_state, sd);
}

static inline void debug_msleep(struct v4l2_subdev *sd, u32 msecs)
{
	cam_dbg("delay for %dms\n", msecs);
	msleep(msecs);
}

#if defined(FEATURE_YUV_CAPTURE)
/* JPEG MEMORY SIZE */
#define SENSOR_JPEG_OUTPUT_MAXSIZE	0x29999A /*2726298bytes, 2.6M */
#define EXTRA_MEMSIZE			(0 * SZ_1K)
#define SENSOR_JPEG_SNAPSHOT_MEMSIZE 	0x99C600
#define SENSOR_JPEG_THUMB_MAXSIZE		0xFC00
#define SENSOR_JPEG_POST_MAXSIZE		0xBB800
#endif

/*********** Sensor specific ************/
#define S5K4ECGX_CHIP_ID	0x4EC0
#define S5K4ECGX_CHIP_REV	0x0011

#define FORMAT_FLAGS_COMPRESSED		0x3

#define POLL_TIME_MS			5
#define CAPTURE_POLL_TIME_MS    500

/* maximum time for one frame in norma light */
#define ONE_FRAME_DELAY_MS_NORMAL		66
/* maximum time for one frame in low light: minimum 10fps. */
#define ONE_FRAME_DELAY_MS_LOW			100
/* maximum time for one frame in night mode: 6fps */
#define ONE_FRAME_DELAY_MS_NIGHTMODE		166

/* maximum time for one frame at minimum fps (15fps) in normal mode */
#define NORMAL_MODE_MAX_ONE_FRAME_DELAY_MS     67
/* maximum time for one frame at minimum fps (4fps) in night mode */
#define NIGHT_MODE_MAX_ONE_FRAME_DELAY_MS     250

/* level at or below which we need to enable flash when in auto mode */
#define LOW_LIGHT_LEVEL		0x1D

/* level at or below which we need to use low light capture mode */
#define HIGH_LIGHT_LEVEL	0x80

#define FIRST_AF_SEARCH_COUNT   80
#define SECOND_AF_SEARCH_COUNT  80
#define AE_STABLE_SEARCH_COUNT	7 /* 4->7. but ae-unstable still occurs. */

#define FIRST_SETTING_FOCUS_MODE_DELAY_MS	100
#define SECOND_SETTING_FOCUS_MODE_DELAY_MS	200

/* Sensor AF first,second window size.
 * we use constant values intead of reading sensor register */
#define FIRST_WINSIZE_X			512
#define FIRST_WINSIZE_Y			568
#define SCND_WINSIZE_X			230
#define SCND_WINSIZE_Y			306

/* The Path of Setfile */
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

#if defined(CONFIG_VIDEO_S5K4ECGX_ODROIDQ)
#define TUNING_FILE_PATH "/mnt/sdcard/s5k4ecgx_regs_odroid.h"
#else
#define TUNING_FILE_PATH NULL
#endif
#endif /* CONFIG_LOAD_FILE*/

#include "s5k4ecgx_regs_odroid.h"

#endif /* __S5K4ECGX_H__ */
