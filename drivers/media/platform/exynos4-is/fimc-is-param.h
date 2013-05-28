/*
 * Samsung EXYNOS4x12 FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2011 - 2013 Samsung Electronics Co., Ltd.
 *
 * Authors: Younghwan Joo <yhwan.joo@samsung.com>
 *	    Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef FIMC_IS_PARAM_H_
#define FIMC_IS_PARAM_H_

#include <linux/compiler.h>

#define FIMC_IS_CONFIG_TIMEOUT		3000 /* ms */
#define IS_DEFAULT_WIDTH		1280
#define IS_DEFAULT_HEIGHT		720

#define DEFAULT_PREVIEW_STILL_WIDTH	IS_DEFAULT_WIDTH
#define DEFAULT_PREVIEW_STILL_HEIGHT	IS_DEFAULT_HEIGHT
#define DEFAULT_CAPTURE_STILL_WIDTH	IS_DEFAULT_WIDTH
#define DEFAULT_CAPTURE_STILL_HEIGHT	IS_DEFAULT_HEIGHT
#define DEFAULT_PREVIEW_VIDEO_WIDTH	IS_DEFAULT_WIDTH
#define DEFAULT_PREVIEW_VIDEO_HEIGHT	IS_DEFAULT_HEIGHT
#define DEFAULT_CAPTURE_VIDEO_WIDTH	IS_DEFAULT_WIDTH
#define DEFAULT_CAPTURE_VIDEO_HEIGHT	IS_DEFAULT_HEIGHT

#define DEFAULT_PREVIEW_STILL_FRAMERATE	30
#define DEFAULT_CAPTURE_STILL_FRAMERATE	15
#define DEFAULT_PREVIEW_VIDEO_FRAMERATE	30
#define DEFAULT_CAPTURE_VIDEO_FRAMERATE	30

#define FIMC_IS_REGION_VER		124 /* IS REGION VERSION 1.24 */
#define FIMC_IS_PARAM_SIZE		(FIMC_IS_REGION_SIZE + 1)
#define FIMC_IS_MAGIC_NUMBER		0x01020304
#define FIMC_IS_PARAM_MAX_SIZE		64 /* in bytes */
#define FIMC_IS_PARAM_MAX_ENTRIES	(FIMC_IS_PARAM_MAX_SIZE / 4)

/* The parameter bitmask bit definitions. */
enum is_param_bit {
	PARAM_GLOBAL_SHOTMODE,
	PARAM_SENSOR_CONTROL,
	PARAM_SENSOR_OTF_OUTPUT,
	PARAM_SENSOR_FRAME_RATE,
	PARAM_BUFFER_CONTROL,
	PARAM_BUFFER_OTF_INPUT,
	PARAM_BUFFER_OTF_OUTPUT,
	PARAM_ISP_CONTROL,
	PARAM_ISP_OTF_INPUT,
	PARAM_ISP_DMA1_INPUT,
	/* 10 */
	PARAM_ISP_DMA2_INPUT,
	PARAM_ISP_AA,
	PARAM_ISP_FLASH,
	PARAM_ISP_AWB,
	PARAM_ISP_IMAGE_EFFECT,
	PARAM_ISP_ISO,
	PARAM_ISP_ADJUST,
	PARAM_ISP_METERING,
	PARAM_ISP_AFC,
	PARAM_ISP_OTF_OUTPUT,
	/* 20 */
	PARAM_ISP_DMA1_OUTPUT,
	PARAM_ISP_DMA2_OUTPUT,
	PARAM_DRC_CONTROL,
	PARAM_DRC_OTF_INPUT,
	PARAM_DRC_DMA_INPUT,
	PARAM_DRC_OTF_OUTPUT,
	PARAM_SCALERC_CONTROL,
	PARAM_SCALERC_OTF_INPUT,
	PARAM_SCALERC_IMAGE_EFFECT,
	PARAM_SCALERC_INPUT_CROP,
	/* 30 */
	PARAM_SCALERC_OUTPUT_CROP,
	PARAM_SCALERC_OTF_OUTPUT,
	PARAM_SCALERC_DMA_OUTPUT,
	PARAM_ODC_CONTROL,
	PARAM_ODC_OTF_INPUT,
	PARAM_ODC_OTF_OUTPUT,
	PARAM_DIS_CONTROL,
	PARAM_DIS_OTF_INPUT,
	PARAM_DIS_OTF_OUTPUT,
	PARAM_TDNR_CONTROL,
	/* 40 */
	PARAM_TDNR_OTF_INPUT,
	PARAM_TDNR_1ST_FRAME,
	PARAM_TDNR_OTF_OUTPUT,
	PARAM_TDNR_DMA_OUTPUT,
	PARAM_SCALERP_CONTROL,
	PARAM_SCALERP_OTF_INPUT,
	PARAM_SCALERP_IMAGE_EFFECT,
	PARAM_SCALERP_INPUT_CROP,
	PARAM_SCALERP_OUTPUT_CROP,
	PARAM_SCALERP_ROTATION,
	/* 50 */
	PARAM_SCALERP_FLIP,
	PARAM_SCALERP_OTF_OUTPUT,
	PARAM_SCALERP_DMA_OUTPUT,
	PARAM_FD_CONTROL,
	PARAM_FD_OTF_INPUT,
	PARAM_FD_DMA_INPUT,
	PARAM_FD_CONFIG,
};

/* Interrupt map */
#define	FIMC_IS_INT_GENERAL			0
#define	FIMC_IS_INT_FRAME_DONE_ISP		1

/* Input */

#define CONTROL_COMMAND_STOP			0
#define CONTROL_COMMAND_START			1

#define CONTROL_BYPASS_DISABLE			0
#define CONTROL_BYPASS_ENABLE			1

#define CONTROL_ERROR_NONE			0

/* OTF (On-The-Fly) input interface commands */
#define OTF_INPUT_COMMAND_DISABLE		0
#define OTF_INPUT_COMMAND_ENABLE		1

/* OTF input interface color formats */
enum oft_input_fmt {
	OTF_INPUT_FORMAT_BAYER			= 0, /* 1 channel */
	OTF_INPUT_FORMAT_YUV444			= 1, /* 3 channels */
	OTF_INPUT_FORMAT_YUV422			= 2, /* 3 channels */
	OTF_INPUT_FORMAT_YUV420			= 3, /* 3 channels */
	OTF_INPUT_FORMAT_STRGEN_COLORBAR_BAYER	= 10,
	OTF_INPUT_FORMAT_BAYER_DMA		= 11,
};

#define OTF_INPUT_ORDER_BAYER_GR_BG		0

/* OTF input error codes */
#define OTF_INPUT_ERROR_NONE			0 /* Input setting is done */

/* DMA input commands */
#define DMA_INPUT_COMMAND_DISABLE		0
#define DMA_INPUT_COMMAND_ENABLE		1

/* DMA input color formats */
enum dma_input_fmt {
	DMA_INPUT_FORMAT_BAYER			= 0,
	DMA_INPUT_FORMAT_YUV444			= 1,
	DMA_INPUT_FORMAT_YUV422			= 2,
	DMA_INPUT_FORMAT_YUV420			= 3,
};

enum dma_input_order {
	/* (for DMA_INPUT_PLANE_3) */
	DMA_INPUT_ORDER_NO	= 0,
	/* (only valid at DMA_INPUT_PLANE_2) */
	DMA_INPUT_ORDER_CBCR	= 1,
	/* (only valid at DMA_INPUT_PLANE_2) */
	DMA_INPUT_ORDER_CRCB	= 2,
	/* (only valid at DMA_INPUT_PLANE_1 & DMA_INPUT_FORMAT_YUV444) */
	DMA_INPUT_ORDER_YCBCR	= 3,
	/* (only valid at DMA_INPUT_FORMAT_YUV422 & DMA_INPUT_PLANE_1) */
	DMA_INPUT_ORDER_YYCBCR	= 4,
	/* (only valid at DMA_INPUT_FORMAT_YUV422 & DMA_INPUT_PLANE_1) */
	DMA_INPUT_ORDER_YCBYCR	= 5,
	/* (only valid at DMA_INPUT_FORMAT_YUV422 & DMA_INPUT_PLANE_1) */
	DMA_INPUT_ORDER_YCRYCB	= 6,
	/* (only valid at DMA_INPUT_FORMAT_YUV422 & DMA_INPUT_PLANE_1) */
	DMA_INPUT_ORDER_CBYCRY	= 7,
	/* (only valid at DMA_INPUT_FORMAT_YUV422 & DMA_INPUT_PLANE_1) */
	DMA_INPUT_ORDER_CRYCBY	= 8,
	/* (only valid at DMA_INPUT_FORMAT_BAYER) */
	DMA_INPUT_ORDER_GR_BG	= 9
};

#define DMA_INPUT_ERROR_NONE			0 /* DMA input setting
						     is done */
/*
 * Data output parameter definitions
 */
#define OTF_OUTPUT_CROP_DISABLE			0
#define OTF_OUTPUT_CROP_ENABLE			1

#define OTF_OUTPUT_COMMAND_DISABLE		0
#define OTF_OUTPUT_COMMAND_ENABLE		1

enum otf_output_fmt {
	OTF_OUTPUT_FORMAT_YUV444		= 1,
	OTF_OUTPUT_FORMAT_YUV422		= 2,
	OTF_OUTPUT_FORMAT_YUV420		= 3,
	OTF_OUTPUT_FORMAT_RGB			= 4,
};

#define OTF_OUTPUT_ORDER_BAYER_GR_BG		0

#define OTF_OUTPUT_ERROR_NONE			0 /* Output Setting is done */

#define DMA_OUTPUT_COMMAND_DISABLE		0
#define DMA_OUTPUT_COMMAND_ENABLE		1

enum dma_output_fmt {
	DMA_OUTPUT_FORMAT_BAYER			= 0,
	DMA_OUTPUT_FORMAT_YUV444		= 1,
	DMA_OUTPUT_FORMAT_YUV422		= 2,
	DMA_OUTPUT_FORMAT_YUV420		= 3,
	DMA_OUTPUT_FORMAT_RGB			= 4,
};

enum dma_output_order {
	DMA_OUTPUT_ORDER_NO		= 0,
	/* for DMA_OUTPUT_PLANE_3 */
	DMA_OUTPUT_ORDER_CBCR		= 1,
	/* only valid at DMA_INPUT_PLANE_2) */
	DMA_OUTPUT_ORDER_CRCB		= 2,
	/* only valid at DMA_OUTPUT_PLANE_2) */
	DMA_OUTPUT_ORDER_YYCBCR		= 3,
	/* only valid at DMA_OUTPUT_FORMAT_YUV422 & DMA_OUTPUT_PLANE_1 */
	DMA_OUTPUT_ORDER_YCBYCR		= 4,
	/* only valid at DMA_OUTPUT_FORMAT_YUV422 & DMA_OUTPUT_PLANE_1 */
	DMA_OUTPUT_ORDER_YCRYCB		= 5,
	/* only valid at DMA_OUTPUT_FORMAT_YUV422 & DMA_OUTPUT_PLANE_1 */
	DMA_OUTPUT_ORDER_CBYCRY		= 6,
	/* only valid at DMA_OUTPUT_FORMAT_YUV422 & DMA_OUTPUT_PLANE_1 */
	DMA_OUTPUT_ORDER_CRYCBY		= 7,
	/* only valid at DMA_OUTPUT_FORMAT_YUV422 & DMA_OUTPUT_PLANE_1 */
	DMA_OUTPUT_ORDER_YCBCR		= 8,
	/* only valid at DMA_OUTPUT_FORMAT_YUV444 & DMA_OUPUT_PLANE_1 */
	DMA_OUTPUT_ORDER_CRYCB		= 9,
	/* only valid at DMA_OUTPUT_FORMAT_YUV444 & DMA_OUPUT_PLANE_1 */
	DMA_OUTPUT_ORDER_CRCBY		= 10,
	/* only valid at DMA_OUTPUT_FORMAT_YUV444 & DMA_OUPUT_PLANE_1 */
	DMA_OUTPUT_ORDER_CBYCR		= 11,
	/* only valid at DMA_OUTPUT_FORMAT_YUV444 & DMA_OUPUT_PLANE_1 */
	DMA_OUTPUT_ORDER_YCRCB		= 12,
	/* only valid at DMA_OUTPUT_FORMAT_YUV444 & DMA_OUPUT_PLANE_1 */
	DMA_OUTPUT_ORDER_CBCRY		= 13,
	/* only valid at DMA_OUTPUT_FORMAT_YUV444 & DMA_OUPUT_PLANE_1 */
	DMA_OUTPUT_ORDER_BGR		= 14,
	/* only valid at DMA_OUTPUT_FORMAT_RGB */
	DMA_OUTPUT_ORDER_GB_BG		= 15
	/* only valid at DMA_OUTPUT_FORMAT_BAYER */
};

/* enum dma_output_notify_dma_done */
#define DMA_OUTPUT_NOTIFY_DMA_DONE_DISABLE	0
#define DMA_OUTPUT_NOTIFY_DMA_DONE_ENABLE	1

/* DMA output error codes */
#define DMA_OUTPUT_ERROR_NONE			0 /* DMA output setting
						     is done */

/* ----------------------  Global  ----------------------------------- */
#define GLOBAL_SHOTMODE_ERROR_NONE		0 /* shot-mode setting
						     is done */
/* 3A lock commands */
#define ISP_AA_COMMAND_START			0
#define ISP_AA_COMMAND_STOP			1

/* 3A lock target */
#define ISP_AA_TARGET_AF			1
#define ISP_AA_TARGET_AE			2
#define ISP_AA_TARGET_AWB			4

enum isp_af_mode {
	ISP_AF_MODE_MANUAL			= 0,
	ISP_AF_MODE_SINGLE			= 1,
	ISP_AF_MODE_CONTINUOUS			= 2,
	ISP_AF_MODE_TOUCH			= 3,
	ISP_AF_MODE_SLEEP			= 4,
	ISP_AF_MODE_INIT			= 5,
	ISP_AF_MODE_SET_CENTER_WINDOW		= 6,
	ISP_AF_MODE_SET_TOUCH_WINDOW		= 7
};

/* Face AF commands */
#define ISP_AF_FACE_DISABLE			0
#define ISP_AF_FACE_ENABLE			1

/* AF range */
#define ISP_AF_RANGE_NORMAL			0
#define ISP_AF_RANGE_MACRO			1

/* AF sleep */
#define ISP_AF_SLEEP_OFF			0
#define ISP_AF_SLEEP_ON				1

/* Continuous AF commands */
#define ISP_AF_CONTINUOUS_DISABLE		0
#define ISP_AF_CONTINUOUS_ENABLE		1

/* ISP AF error codes */
#define ISP_AF_ERROR_NONE			0 /* AF mode change is done */
#define ISP_AF_ERROR_NONE_LOCK_DONE		1 /* AF lock is done */

/* Flash commands */
#define ISP_FLASH_COMMAND_DISABLE		0
#define ISP_FLASH_COMMAND_MANUAL_ON		1 /* (forced flash) */
#define ISP_FLASH_COMMAND_AUTO			2
#define ISP_FLASH_COMMAND_TORCH			3 /* 3 sec */

/* Flash red-eye commads */
#define ISP_FLASH_REDEYE_DISABLE		0
#define ISP_FLASH_REDEYE_ENABLE			1

/* Flash error codes */
#define ISP_FLASH_ERROR_NONE			0 /* Flash setting is done */

/* --------------------------  AWB  ------------------------------------ */
enum isp_awb_command {
	ISP_AWB_COMMAND_AUTO			= 0,
	ISP_AWB_COMMAND_ILLUMINATION		= 1,
	ISP_AWB_COMMAND_MANUAL			= 2
};

enum isp_awb_illumination {
	ISP_AWB_ILLUMINATION_DAYLIGHT		= 0,
	ISP_AWB_ILLUMINATION_CLOUDY		= 1,
	ISP_AWB_ILLUMINATION_TUNGSTEN		= 2,
	ISP_AWB_ILLUMINATION_FLUORESCENT	= 3
};

/* ISP AWN error codes */
#define ISP_AWB_ERROR_NONE			0 /* AWB setting is done */

/* --------------------------  Effect  ----------------------------------- */
enum isp_imageeffect_command {
	ISP_IMAGE_EFFECT_DISABLE		= 0,
	ISP_IMAGE_EFFECT_MONOCHROME		= 1,
	ISP_IMAGE_EFFECT_NEGATIVE_MONO		= 2,
	ISP_IMAGE_EFFECT_NEGATIVE_COLOR		= 3,
	ISP_IMAGE_EFFECT_SEPIA			= 4
};

/* Image effect error codes */
#define ISP_IMAGE_EFFECT_ERROR_NONE		0 /* Image effect setting
						     is done */
/* ISO commands */
#define ISP_ISO_COMMAND_AUTO			0
#define ISP_ISO_COMMAND_MANUAL			1

/* ISO error codes */
#define ISP_ISO_ERROR_NONE			0 /* ISO setting is done */

/* ISP adjust commands */
#define ISP_ADJUST_COMMAND_AUTO			(0 << 0)
#define ISP_ADJUST_COMMAND_MANUAL_CONTRAST	(1 << 0)
#define ISP_ADJUST_COMMAND_MANUAL_SATURATION	(1 << 1)
#define ISP_ADJUST_COMMAND_MANUAL_SHARPNESS	(1 << 2)
#define ISP_ADJUST_COMMAND_MANUAL_EXPOSURE	(1 << 3)
#define ISP_ADJUST_COMMAND_MANUAL_BRIGHTNESS	(1 << 4)
#define ISP_ADJUST_COMMAND_MANUAL_HUE		(1 << 5)
#define ISP_ADJUST_COMMAND_MANUAL_ALL		0x7f

/* ISP adjustment error codes */
#define ISP_ADJUST_ERROR_NONE			0 /* Adjust setting is done */

/*
 *  Exposure metering
 */
enum isp_metering_command {
	ISP_METERING_COMMAND_AVERAGE	= 0,
	ISP_METERING_COMMAND_SPOT	= 1,
	ISP_METERING_COMMAND_MATRIX	= 2,
	ISP_METERING_COMMAND_CENTER	= 3
};

/* ISP metering error codes */
#define ISP_METERING_ERROR_NONE		0 /* Metering setting is done */

/*
 * AFC
 */
enum isp_afc_command {
	ISP_AFC_COMMAND_DISABLE		= 0,
	ISP_AFC_COMMAND_AUTO		= 1,
	ISP_AFC_COMMAND_MANUAL		= 2,
};

#define ISP_AFC_MANUAL_50HZ		50
#define ISP_AFC_MANUAL_60HZ		60

/* ------------------------  SCENE MODE--------------------------------- */
enum isp_scene_mode {
	ISP_SCENE_NONE			= 0,
	ISP_SCENE_PORTRAIT		= 1,
	ISP_SCENE_LANDSCAPE		= 2,
	ISP_SCENE_SPORTS		= 3,
	ISP_SCENE_PARTYINDOOR		= 4,
	ISP_SCENE_BEACHSNOW		= 5,
	ISP_SCENE_SUNSET		= 6,
	ISP_SCENE_DAWN			= 7,
	ISP_SCENE_FALL			= 8,
	ISP_SCENE_NIGHT			= 9,
	ISP_SCENE_AGAINSTLIGHTWLIGHT	= 10,
	ISP_SCENE_AGAINSTLIGHTWOLIGHT	= 11,
	ISP_SCENE_FIRE			= 12,
	ISP_SCENE_TEXT			= 13,
	ISP_SCENE_CANDLE		= 14
};

/* AFC error codes */
#define ISP_AFC_ERROR_NONE		0 /* AFC setting is done */

/* ----------------------------  FD  ------------------------------------- */
enum fd_config_command {
	FD_CONFIG_COMMAND_MAXIMUM_NUMBER	= 0x1,
	FD_CONFIG_COMMAND_ROLL_ANGLE		= 0x2,
	FD_CONFIG_COMMAND_YAW_ANGLE		= 0x4,
	FD_CONFIG_COMMAND_SMILE_MODE		= 0x8,
	FD_CONFIG_COMMAND_BLINK_MODE		= 0x10,
	FD_CONFIG_COMMAND_EYES_DETECT		= 0x20,
	FD_CONFIG_COMMAND_MOUTH_DETECT		= 0x40,
	FD_CONFIG_COMMAND_ORIENTATION		= 0x80,
	FD_CONFIG_COMMAND_ORIENTATION_VALUE	= 0x100
};

enum fd_config_roll_angle {
	FD_CONFIG_ROLL_ANGLE_BASIC		= 0,
	FD_CONFIG_ROLL_ANGLE_PRECISE_BASIC	= 1,
	FD_CONFIG_ROLL_ANGLE_SIDES		= 2,
	FD_CONFIG_ROLL_ANGLE_PRECISE_SIDES	= 3,
	FD_CONFIG_ROLL_ANGLE_FULL		= 4,
	FD_CONFIG_ROLL_ANGLE_PRECISE_FULL	= 5,
};

enum fd_config_yaw_angle {
	FD_CONFIG_YAW_ANGLE_0			= 0,
	FD_CONFIG_YAW_ANGLE_45			= 1,
	FD_CONFIG_YAW_ANGLE_90			= 2,
	FD_CONFIG_YAW_ANGLE_45_90		= 3,
};

/* Smile mode configuration */
#define FD_CONFIG_SMILE_MODE_DISABLE		0
#define FD_CONFIG_SMILE_MODE_ENABLE		1

/* Blink mode configuration */
#define FD_CONFIG_BLINK_MODE_DISABLE		0
#define FD_CONFIG_BLINK_MODE_ENABLE		1

/* Eyes detection configuration */
#define FD_CONFIG_EYES_DETECT_DISABLE		0
#define FD_CONFIG_EYES_DETECT_ENABLE		1

/* Mouth detection configuration */
#define FD_CONFIG_MOUTH_DETECT_DISABLE		0
#define FD_CONFIG_MOUTH_DETECT_ENABLE		1

#define FD_CONFIG_ORIENTATION_DISABLE		0
#define FD_CONFIG_ORIENTATION_ENABLE		1

struct param_control {
	u32 cmd;
	u32 bypass;
	u32 buffer_address;
	u32 buffer_size;
	u32 skip_frames; /* only valid at ISP */
	u32 reserved[FIMC_IS_PARAM_MAX_ENTRIES - 6];
	u32 err;
};

struct param_otf_input {
	u32 cmd;
	u32 width;
	u32 height;
	u32 format;
	u32 bitwidth;
	u32 order;
	u32 crop_offset_x;
	u32 crop_offset_y;
	u32 crop_width;
	u32 crop_height;
	u32 frametime_min;
	u32 frametime_max;
	u32 reserved[FIMC_IS_PARAM_MAX_ENTRIES - 13];
	u32 err;
};

struct param_dma_input {
	u32 cmd;
	u32 width;
	u32 height;
	u32 format;
	u32 bitwidth;
	u32 plane;
	u32 order;
	u32 buffer_number;
	u32 buffer_address;
	u32 reserved[FIMC_IS_PARAM_MAX_ENTRIES - 10];
	u32 err;
};

struct param_otf_output {
	u32 cmd;
	u32 width;
	u32 height;
	u32 format;
	u32 bitwidth;
	u32 order;
	u32 reserved[FIMC_IS_PARAM_MAX_ENTRIES - 7];
	u32 err;
};

struct param_dma_output {
	u32 cmd;
	u32 width;
	u32 height;
	u32 format;
	u32 bitwidth;
	u32 plane;
	u32 order;
	u32 buffer_number;
	u32 buffer_address;
	u32 notify_dma_done;
	u32 dma_out_mask;
	u32 reserved[FIMC_IS_PARAM_MAX_ENTRIES - 12];
	u32 err;
};

struct param_global_shotmode {
	u32 cmd;
	u32 skip_frames;
	u32 reserved[FIMC_IS_PARAM_MAX_ENTRIES - 3];
	u32 err;
};

struct param_sensor_framerate {
	u32 frame_rate;
	u32 reserved[FIMC_IS_PARAM_MAX_ENTRIES - 2];
	u32 err;
};

struct param_isp_aa {
	u32 cmd;
	u32 target;
	u32 mode;
	u32 scene;
	u32 sleep;
	u32 face;
	u32 touch_x;
	u32 touch_y;
	u32 manual_af_setting;
	u32 reserved[FIMC_IS_PARAM_MAX_ENTRIES - 10];
	u32 err;
};

struct param_isp_flash {
	u32 cmd;
	u32 redeye;
	u32 reserved[FIMC_IS_PARAM_MAX_ENTRIES - 3];
	u32 err;
};

struct param_isp_awb {
	u32 cmd;
	u32 illumination;
	u32 reserved[FIMC_IS_PARAM_MAX_ENTRIES - 3];
	u32 err;
};

struct param_isp_imageeffect {
	u32 cmd;
	u32 reserved[FIMC_IS_PARAM_MAX_ENTRIES - 2];
	u32 err;
};

struct param_isp_iso {
	u32 cmd;
	u32 value;
	u32 reserved[FIMC_IS_PARAM_MAX_ENTRIES - 3];
	u32 err;
};

struct param_isp_adjust {
	u32 cmd;
	s32 contrast;
	s32 saturation;
	s32 sharpness;
	s32 exposure;
	s32 brightness;
	s32 hue;
	u32 reserved[FIMC_IS_PARAM_MAX_ENTRIES - 8];
	u32 err;
};

struct param_isp_metering {
	u32 cmd;
	u32 win_pos_x;
	u32 win_pos_y;
	u32 win_width;
	u32 win_height;
	u32 reserved[FIMC_IS_PARAM_MAX_ENTRIES - 6];
	u32 err;
};

struct param_isp_afc {
	u32 cmd;
	u32 manual;
	u32 reserved[FIMC_IS_PARAM_MAX_ENTRIES - 3];
	u32 err;
};

struct param_scaler_imageeffect {
	u32 cmd;
	u32 arbitrary_cb;
	u32 arbitrary_cr;
	u32 reserved[FIMC_IS_PARAM_MAX_ENTRIES - 4];
	u32 err;
};

struct param_scaler_input_crop {
	u32 cmd;
	u32 crop_offset_x;
	u32 crop_offset_y;
	u32 crop_width;
	u32 crop_height;
	u32 in_width;
	u32 in_height;
	u32 out_width;
	u32 out_height;
	u32 reserved[FIMC_IS_PARAM_MAX_ENTRIES - 10];
	u32 err;
};

struct param_scaler_output_crop {
	u32 cmd;
	u32 crop_offset_x;
	u32 crop_offset_y;
	u32 crop_width;
	u32 crop_height;
	u32 out_format;
	u32 reserved[FIMC_IS_PARAM_MAX_ENTRIES - 7];
	u32 err;
};

struct param_scaler_rotation {
	u32 cmd;
	u32 reserved[FIMC_IS_PARAM_MAX_ENTRIES - 2];
	u32 err;
};

struct param_scaler_flip {
	u32 cmd;
	u32 reserved[FIMC_IS_PARAM_MAX_ENTRIES - 2];
	u32 err;
};

struct param_3dnr_1stframe {
	u32 cmd;
	u32 reserved[FIMC_IS_PARAM_MAX_ENTRIES - 2];
	u32 err;
};

struct param_fd_config {
	u32 cmd;
	u32 max_number;
	u32 roll_angle;
	u32 yaw_angle;
	u32 smile_mode;
	u32 blink_mode;
	u32 eye_detect;
	u32 mouth_detect;
	u32 orientation;
	u32 orientation_value;
	u32 reserved[FIMC_IS_PARAM_MAX_ENTRIES - 11];
	u32 err;
};

struct global_param {
	struct param_global_shotmode	shotmode;
};

struct sensor_param {
	struct param_control		control;
	struct param_otf_output		otf_output;
	struct param_sensor_framerate	frame_rate;
} __packed;

struct buffer_param {
	struct param_control		control;
	struct param_otf_input		otf_input;
	struct param_otf_output		otf_output;
} __packed;

struct isp_param {
	struct param_control		control;
	struct param_otf_input		otf_input;
	struct param_dma_input		dma1_input;
	struct param_dma_input		dma2_input;
	struct param_isp_aa		aa;
	struct param_isp_flash		flash;
	struct param_isp_awb		awb;
	struct param_isp_imageeffect	effect;
	struct param_isp_iso		iso;
	struct param_isp_adjust		adjust;
	struct param_isp_metering	metering;
	struct param_isp_afc		afc;
	struct param_otf_output		otf_output;
	struct param_dma_output		dma1_output;
	struct param_dma_output		dma2_output;
} __packed;

struct drc_param {
	struct param_control		control;
	struct param_otf_input		otf_input;
	struct param_dma_input		dma_input;
	struct param_otf_output		otf_output;
} __packed;

struct scalerc_param {
	struct param_control		control;
	struct param_otf_input		otf_input;
	struct param_scaler_imageeffect	effect;
	struct param_scaler_input_crop	input_crop;
	struct param_scaler_output_crop	output_crop;
	struct param_otf_output		otf_output;
	struct param_dma_output		dma_output;
} __packed;

struct odc_param {
	struct param_control		control;
	struct param_otf_input		otf_input;
	struct param_otf_output		otf_output;
} __packed;

struct dis_param {
	struct param_control		control;
	struct param_otf_output		otf_input;
	struct param_otf_output		otf_output;
} __packed;

struct tdnr_param {
	struct param_control		control;
	struct param_otf_input		otf_input;
	struct param_3dnr_1stframe	frame;
	struct param_otf_output		otf_output;
	struct param_dma_output		dma_output;
} __packed;

struct scalerp_param {
	struct param_control		control;
	struct param_otf_input		otf_input;
	struct param_scaler_imageeffect	effect;
	struct param_scaler_input_crop	input_crop;
	struct param_scaler_output_crop	output_crop;
	struct param_scaler_rotation	rotation;
	struct param_scaler_flip	flip;
	struct param_otf_output		otf_output;
	struct param_dma_output		dma_output;
} __packed;

struct fd_param {
	struct param_control		control;
	struct param_otf_input		otf_input;
	struct param_dma_input		dma_input;
	struct param_fd_config		config;
} __packed;

struct is_param_region {
	struct global_param		global;
	struct sensor_param		sensor;
	struct buffer_param		buf;
	struct isp_param		isp;
	struct drc_param		drc;
	struct scalerc_param		scalerc;
	struct odc_param		odc;
	struct dis_param		dis;
	struct tdnr_param		tdnr;
	struct scalerp_param		scalerp;
	struct fd_param			fd;
} __packed;

#define NUMBER_OF_GAMMA_CURVE_POINTS	32

struct is_tune_sensor {
	u32 exposure;
	u32 analog_gain;
	u32 frame_rate;
	u32 actuator_position;
};

struct is_tune_gammacurve {
	u32 num_pts_x[NUMBER_OF_GAMMA_CURVE_POINTS];
	u32 num_pts_y_r[NUMBER_OF_GAMMA_CURVE_POINTS];
	u32 num_pts_y_g[NUMBER_OF_GAMMA_CURVE_POINTS];
	u32 num_pts_y_b[NUMBER_OF_GAMMA_CURVE_POINTS];
};

struct is_tune_isp {
	/* Brightness level: range 0...100, default 7. */
	u32 brightness_level;
	/* Contrast level: range -127...127, default 0. */
	s32 contrast_level;
	/* Saturation level: range -127...127, default 0. */
	s32 saturation_level;
	s32 gamma_level;
	struct is_tune_gammacurve gamma_curve[4];
	/* Hue: range -127...127, default 0. */
	s32 hue;
	/* Sharpness blur: range -127...127, default 0. */
	s32 sharpness_blur;
	/* Despeckle : range -127~127, default : 0 */
	s32 despeckle;
	/* Edge color supression: range -127...127, default 0. */
	s32 edge_color_supression;
	/* Noise reduction: range -127...127, default 0. */
	s32 noise_reduction;
	/* (32 * 4 + 9) * 4 = 548 bytes */
} __packed;

struct is_tune_region {
	struct is_tune_sensor sensor;
	struct is_tune_isp isp;
} __packed;

struct rational {
	u32 num;
	u32 den;
};

struct srational {
	s32 num;
	s32 den;
};

#define FLASH_FIRED_SHIFT			0
#define FLASH_NOT_FIRED				0
#define FLASH_FIRED				1

#define FLASH_STROBE_SHIFT			1
#define FLASH_STROBE_NO_DETECTION		0
#define FLASH_STROBE_RESERVED			1
#define FLASH_STROBE_RETURN_LIGHT_NOT_DETECTED	2
#define FLASH_STROBE_RETURN_LIGHT_DETECTED	3

#define FLASH_MODE_SHIFT			3
#define FLASH_MODE_UNKNOWN			0
#define FLASH_MODE_COMPULSORY_FLASH_FIRING	1
#define FLASH_MODE_COMPULSORY_FLASH_SUPPRESSION	2
#define FLASH_MODE_AUTO_MODE			3

#define FLASH_FUNCTION_SHIFT			5
#define FLASH_FUNCTION_PRESENT			0
#define FLASH_FUNCTION_NONE			1

#define FLASH_RED_EYE_SHIFT			6
#define FLASH_RED_EYE_DISABLED			0
#define FLASH_RED_EYE_SUPPORTED			1

enum apex_aperture_value {
	F1_0	= 0,
	F1_4	= 1,
	F2_0	= 2,
	F2_8	= 3,
	F4_0	= 4,
	F5_6	= 5,
	F8_9	= 6,
	F11_0	= 7,
	F16_0	= 8,
	F22_0	= 9,
	F32_0	= 10,
};

struct exif_attribute {
	struct rational exposure_time;
	struct srational shutter_speed;
	u32 iso_speed_rating;
	u32 flash;
	struct srational brightness;
} __packed;

struct is_frame_header {
	u32 valid;
	u32 bad_mark;
	u32 captured;
	u32 frame_number;
	struct exif_attribute exif;
} __packed;

struct is_fd_rect {
	u32 offset_x;
	u32 offset_y;
	u32 width;
	u32 height;
};

struct is_face_marker {
	u32 frame_number;
	struct is_fd_rect face;
	struct is_fd_rect left_eye;
	struct is_fd_rect right_eye;
	struct is_fd_rect mouth;
	u32 roll_angle;
	u32 yaw_angle;
	u32 confidence;
	s32 smile_level;
	s32 blink_level;
} __packed;

#define MAX_FRAME_COUNT				8
#define MAX_FRAME_COUNT_PREVIEW			4
#define MAX_FRAME_COUNT_CAPTURE			1
#define MAX_FACE_COUNT				16
#define MAX_SHARED_COUNT			500

struct is_region {
	struct is_param_region parameter;
	struct is_tune_region tune;
	struct is_frame_header header[MAX_FRAME_COUNT];
	struct is_face_marker face[MAX_FACE_COUNT];
	u32 shared[MAX_SHARED_COUNT];
} __packed;

struct is_debug_frame_descriptor {
	u32 sensor_frame_time;
	u32 sensor_exposure_time;
	s32 sensor_analog_gain;
	/* monitor for AA */
	u32 req_lei;

	u32 next_next_lei_exp;
	u32 next_next_lei_a_gain;
	u32 next_next_lei_d_gain;
	u32 next_next_lei_statlei;
	u32 next_next_lei_lei;

	u32 dummy0;
};

#define MAX_FRAMEDESCRIPTOR_CONTEXT_NUM	(30*20)	/* 600 frames */
#define MAX_VERSION_DISPLAY_BUF	32

struct is_share_region {
	u32 frame_time;
	u32 exposure_time;
	s32 analog_gain;

	u32 r_gain;
	u32 g_gain;
	u32 b_gain;

	u32 af_position;
	u32 af_status;
	/* 0 : SIRC_ISP_CAMERA_AUTOFOCUSMESSAGE_NOMESSAGE */
	/* 1 : SIRC_ISP_CAMERA_AUTOFOCUSMESSAGE_REACHED */
	/* 2 : SIRC_ISP_CAMERA_AUTOFOCUSMESSAGE_UNABLETOREACH */
	/* 3 : SIRC_ISP_CAMERA_AUTOFOCUSMESSAGE_LOST */
	/* default : unknown */
	u32 af_scene_type;

	u32 frame_descp_onoff_control;
	u32 frame_descp_update_done;
	u32 frame_descp_idx;
	u32 frame_descp_max_idx;
	struct is_debug_frame_descriptor
		dbg_frame_descp_ctx[MAX_FRAMEDESCRIPTOR_CONTEXT_NUM];

	u32 chip_id;
	u32 chip_rev_no;
	u8 isp_fw_ver_no[MAX_VERSION_DISPLAY_BUF];
	u8 isp_fw_ver_date[MAX_VERSION_DISPLAY_BUF];
	u8 sirc_sdk_ver_no[MAX_VERSION_DISPLAY_BUF];
	u8 sirc_sdk_rev_no[MAX_VERSION_DISPLAY_BUF];
	u8 sirc_sdk_rev_date[MAX_VERSION_DISPLAY_BUF];
} __packed;

struct is_debug_control {
	u32 write_point;	/* 0~ 500KB boundary */
	u32 assert_flag;	/* 0: Not invoked, 1: Invoked */
	u32 pabort_flag;	/* 0: Not invoked, 1: Invoked */
	u32 dabort_flag;	/* 0: Not invoked, 1: Invoked */
};

struct sensor_open_extended {
	u32 actuator_type;
	u32 mclk;
	u32 mipi_lane_num;
	u32 mipi_speed;
	/* Skip setfile loading when fast_open_sensor is not 0 */
	u32 fast_open_sensor;
	/* Activating sensor self calibration mode (6A3) */
	u32 self_calibration_mode;
	/* This field is to adjust I2c clock based on ACLK200 */
	/* This value is varied in case of rev 0.2 */
	u32 i2c_sclk;
};

struct fimc_is;

int fimc_is_hw_get_sensor_max_framerate(struct fimc_is *is);
void fimc_is_set_initial_params(struct fimc_is *is);
unsigned int __get_pending_param_count(struct fimc_is *is);

int  __is_hw_update_params(struct fimc_is *is);
void __is_get_frame_size(struct fimc_is *is, struct v4l2_mbus_framefmt *mf);
void __is_set_frame_size(struct fimc_is *is, struct v4l2_mbus_framefmt *mf);
void __is_set_sensor(struct fimc_is *is, int fps);
void __is_set_isp_aa_ae(struct fimc_is *is);
void __is_set_isp_flash(struct fimc_is *is, u32 cmd, u32 redeye);
void __is_set_isp_awb(struct fimc_is *is, u32 cmd, u32 val);
void __is_set_isp_effect(struct fimc_is *is, u32 cmd);
void __is_set_isp_iso(struct fimc_is *is, u32 cmd, u32 val);
void __is_set_isp_adjust(struct fimc_is *is, u32 cmd, u32 val);
void __is_set_isp_metering(struct fimc_is *is, u32 id, u32 val);
void __is_set_isp_afc(struct fimc_is *is, u32 cmd, u32 val);
void __is_set_drc_control(struct fimc_is *is, u32 val);
void __is_set_fd_control(struct fimc_is *is, u32 val);
void __is_set_fd_config_maxface(struct fimc_is *is, u32 val);
void __is_set_fd_config_rollangle(struct fimc_is *is, u32 val);
void __is_set_fd_config_yawangle(struct fimc_is *is, u32 val);
void __is_set_fd_config_smilemode(struct fimc_is *is, u32 val);
void __is_set_fd_config_blinkmode(struct fimc_is *is, u32 val);
void __is_set_fd_config_eyedetect(struct fimc_is *is, u32 val);
void __is_set_fd_config_mouthdetect(struct fimc_is *is, u32 val);
void __is_set_fd_config_orientation(struct fimc_is *is, u32 val);
void __is_set_fd_config_orientation_val(struct fimc_is *is, u32 val);
void __is_set_isp_aa_af_mode(struct fimc_is *is, int cmd);
void __is_set_isp_aa_af_start_stop(struct fimc_is *is, int cmd);

#endif
