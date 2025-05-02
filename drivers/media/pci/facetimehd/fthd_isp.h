/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * FacetimeHD camera driver
 *
 * Copyright (C) 2014 Patrik Jakobsson (patrik.r.jakobsson@gmail.com)
 *
 */

#ifndef _ISP_H
#define _ISP_H

/* ISP memory types */
#define FTHD_MEM_FIRMWARE	1
#define FTHD_MEM_HEAP		2
#define FTHD_MEM_FW_QUEUE	3
#define FTHD_MEM_FW_ARGS        4
#define FTHD_MEM_CMD            5
#define FTHD_MEM_SHAREDMALLOC   6
#define FTHD_MEM_SET_FILE       7
#define FTHD_MEM_BUFFER         8

#define FTHD_MEM_SIZE		0x8000000	/* 128mb */
#define FTHD_MEM_FW_SIZE	0x800000	/* 8mb */

enum fthd_isp_cmds {
	CISP_CMD_START = 0x0,
	CISP_CMD_STOP = 0x1,
	CISP_CMD_RESET = 0x2,
	CISP_CMD_CONFIG_GET = 0x3,
	CISP_CMD_PRINT_ENABLE = 0x4,
	CISP_CMD_REG_FILE_LOAD = 0x5,
	CISP_CMD_BUILDINFO = 0x6,
	CISP_CMD_TIMEPROFILE_START = 0x7,
	CISP_CMD_TIMEPROFILE_STOP = 0x8,
	CISP_CMD_TIMEPROFILE_SHOW = 0x9,
	CISP_CMD_POWER_DOWN = 0xa,
	CISP_CMD_CH_START = 0x100,
	CISP_CMD_CH_STOP = 0x101,
	CISP_CMD_CH_RESET = 0x102,
	CISP_CMD_CH_STANDBY = 0x103,
	CISP_CMD_CH_BUFFER_RETURN = 0x104,
	CISP_CMD_CH_CAMERA_CONFIG_CURRENT_GET = 0x105,
	CISP_CMD_CH_CAMERA_CONFIG_GET = 0x106,
	CISP_CMD_CH_CAMERA_CONFIG_SELECT = 0x107,
	CISP_CMD_CH_RAW_FRAME_PROCESS = 0x108,
	CISP_CMD_CH_RAW_FRAME_PROCESS_START = 0x109,
	CISP_CMD_CH_RAW_FRAME_PROCESS_STOP = 0x10a,
	CISP_CMD_CH_I2C_READ = 0x10b,
	CISP_CMD_CH_I2C_WRITE = 0x10c,
	CISP_CMD_CH_INFO_GET = 0x10d,
	CISP_CMD_CH_BUFFER_RECYCLE_MODE_SET = 0x10e,
	CISP_CMD_CH_BUFFER_RECYCLE_START = 0x10f,
	CISP_CMD_CH_BUFFER_RECYCLE_STOP = 0x110,
	CISP_CMD_CH_SET_FILE_LOAD = 0x111,
	CISP_CMD_CH_CAPTURE_MODE_SET = 0x112,
	CISP_CMD_CH_RAW_FRAME_PROCESS_GO = 0x113,
	CISP_CMD_CH_EDGE_MAP_CONFIG_GET = 0x114,
	CISP_CMD_CH_SIF_PIXEL_FORMAT_SET = 0x115,
	CISP_CMD_CH_RPU_DMAOUT_CONFIG_GET = 0x116,
	CISP_CMD_CH_RPU_DMAOUT_ENABLE = 0x117,
	CISP_CMD_CH_RPU_DMAOUT_DISABLE = 0x118,
	CISP_CMD_CH_CAMERA_MIPI_FREQ_CURRENT_GET = 0x119,
	CISP_CMD_CH_CAMERA_MIPI_FREQUENCY_GET = 0x11a,
	CISP_CMD_CH_CAMERA_MIPI_FREQ_SELECT = 0x11b,
	CISP_CMD_CH_ISO_PARAMS_SET = 0x11c,
	CISP_CMD_CH_ISO_PARAMS_GET = 0x11d,
	CISP_CMD_CH_CAMERA_PIX_FREQ_CURRENT_GET = 0x11e,
	CISP_CMD_CH_CAMERA_PIX_FREQUENCY_GET = 0x11f,
	CISP_CMD_CH_CAMERA_PIX_FREQ_SELECT = 0x120,
	CISP_CMD_CH_CAMERA_ERR_COUNT_GET = 0x121,
	CISP_CMD_CH_CAMERA_CLOCK_DIVISOR_SET = 0x122,
	CISP_CMD_CH_CAMERA_CLOCK_DIVISOR_AUTO_MODE_SET = 0x123,
	CISP_CMD_CH_CAMERA_ERR_HANDLE_CONFIG = 0x124,
	CISP_CMD_CH_CAMERA_CHROMATIC_TYPE_SET = 0x125,
	CISP_CMD_CH_CAMERA_CHROMATIC_TYPE_GET = 0x126,
	CISP_CMD_CH_AE_START = 0x200,
	CISP_CMD_CH_AE_STOP = 0x201,
	CISP_CMD_CH_AE_AGC_PARAM_SET = 0x202,
	CISP_CMD_CH_AE_BIAS_EXPOSURE_GET = 0x203,
	CISP_CMD_CH_AE_BIAS_EXPOSURE_SET = 0x204,
	CISP_CMD_CH_AE_CLIP_GET = 0x205,
	CISP_CMD_CH_AE_CLIP_SET = 0x206,
	CISP_CMD_CH_AE_FRAME_RATE_MAX_GET = 0x207,
	CISP_CMD_CH_AE_FRAME_RATE_MAX_SET = 0x208,
	CISP_CMD_CH_AE_FRAME_RATE_MIN_GET = 0x209,
	CISP_CMD_CH_AE_FRAME_RATE_MIN_SET = 0x20a,
	CISP_CMD_CH_AE_GAIN_CAP_GET = 0x20b,
	CISP_CMD_CH_AE_GAIN_CAP_SET = 0x20c,
	CISP_CMD_CH_AE_INTEGRATION_TIME_MAX_GET = 0x20d,
	CISP_CMD_CH_AE_INTEGRATION_TIME_MAX_SET = 0x20e,
	CISP_CMD_CH_AE_INTEGRATION_TIME_SET = 0x20f,
	CISP_CMD_CH_AE_NOISE_REDUCTION_CONTROL_PARAM_GET = 0x210,
	CISP_CMD_CH_AE_NOISE_REDUCTION_CONTROL_PARAM_SET = 0x211,
	CISP_CMD_CH_AE_PARAM_GET = 0x212,
	CISP_CMD_CH_AE_PRE_FRAME_RATE_GET = 0x213,
	CISP_CMD_CH_AE_PRE_FRAME_RATE_SET = 0x214,
	CISP_CMD_CH_AE_RED_EYE_PARAM_GET = 0x215,
	CISP_CMD_CH_AE_RED_EYE_PARAM_SET = 0x216,
	CISP_CMD_CH_AE_SPEED_GET = 0x217,
	CISP_CMD_CH_AE_SPEED_SET = 0x218,
	CISP_CMD_CH_AE_STABILITY_GET = 0x219,
	CISP_CMD_CH_AE_STABILITY_SET = 0x21a,
	CISP_CMD_CH_AE_STROBE_PARAM_GET = 0x21b,
	CISP_CMD_CH_AE_STROBE_PARAM_SET = 0x21c,
	CISP_CMD_CH_AE_WINDOW_PARAM_GET = 0x21d,
	CISP_CMD_CH_AE_WINDOW_PARAM_SET = 0x21e,
	CISP_CMD_CH_AE_SDGC_PARAM_SET = 0x21f,
	CISP_CMD_CH_AE_DGC_PARAM_SET = 0x220,
	CISP_CMD_CH_AE_LUX_CALC_PARAMS_SET = 0x221,
	CISP_CMD_CH_AE_BRACKETING_PARAMS_SET = 0x222,
	CISP_CMD_CH_AE_TARGET_GET = 0x223,
	CISP_CMD_CH_AE_TARGET_SET = 0x224,
	CISP_CMD_CH_AE_PREFLASH_PARAM_SET = 0x225,
	CISP_CMD_CH_AE_UPDATE_SUSPEND = 0x226,
	CISP_CMD_CH_AE_UPDATE_RESUME = 0x227,
	CISP_CMD_CH_AE_STABILITY_TO_STABLE_GET = 0x228,
	CISP_CMD_CH_AE_STABILITY_TO_STABLE_SET = 0x229,
	CISP_CMD_CH_AE_SENSOR_INTEGRATION_TIME_MIN_GET = 0x22a,
	CISP_CMD_CH_AE_MANUAL_MODE_SET = 0x22b,
	CISP_CMD_CH_AE_SENSOR_INTEGRATION_TIME_MAX_GET = 0x22c,
	CISP_CMD_CH_AE_GAIN_CAP_MIN_GET = 0x22d,
	CISP_CMD_CH_AE_GAIN_CAP_MIN_SET = 0x22e,
	CISP_CMD_CH_AE_GAIN_CAP_MAX_WITH_EXP_GET = 0x22f,
	CISP_CMD_CH_AE_GAIN_CAP_MAX_WITH_EXP_SET = 0x230,
	CISP_CMD_CH_AE_GAIN_CAP_OFF_GET = 0x231,
	CISP_CMD_CH_AE_GAIN_CAP_OFF_SET = 0x232,
	CISP_CMD_CH_AE_BRACKETING_MANUAL_SET = 0x233,
	CISP_CMD_CH_AE_BRACKETING_MODE_SET = 0x234,
	CISP_CMD_CH_AE_MODE_SET = 0x235,
	CISP_CMD_CH_AE_MODE_GET = 0x236,
	CISP_CMD_CH_AE_PANO_LIMIT_SET = 0x237,
	CISP_CMD_CH_AE_INTEGRATION_GAIN_SET = 0x238,
	CISP_CMD_CH_AE_2WAYSPEED_SET = 0x239,
	CISP_CMD_CH_AE_2WAYSPEED_GET = 0x23a,
	CISP_CMD_CH_AWB_START = 0x300,
	CISP_CMD_CH_AWB_STOP = 0x301,
	CISP_CMD_CH_AWB_WINDOW_PARAM_GET = 0x302,
	CISP_CMD_CH_AWB_WINDOW_PARAM_SET = 0x303,
	CISP_CMD_CH_AWB_CCT_GET = 0x304,
	CISP_CMD_CH_AWB_CCT_MANUAL = 0x305,
	CISP_CMD_CH_AWB_CALIB_TABLE_SET = 0x306,
	CISP_CMD_CH_AWB_BRACKETING_PARAMS_SET = 0x307,
	CISP_CMD_CH_AWB_CCM_WARMUP_PARAMS_SET = 0x308,
	CISP_CMD_CH_AWB_CCM_WARMUP_MATRIX_SET = 0x309,
	CISP_CMD_CH_AWB_CCM_WARMUP_MATRIX_GET = 0x30a,
	CISP_CMD_CH_AWB_2ND_GAIN_ADAPTIVE_THRESHOLDS_SET = 0x30b,
	CISP_CMD_CH_AWB_2ND_GAIN_MANUAL = 0x30c,
	CISP_CMD_CH_AWB_2ND_GAIN_GET = 0x30d,
	CISP_CMD_CH_AWB_FLASH_GAIN_SET = 0x30e,
	CISP_CMD_CH_AWB_UPDATE_SUSPEND = 0x30f,
	CISP_CMD_CH_AWB_UPDATE_RESUME = 0x310,
	CISP_CMD_CH_AWB_1ST_GAIN_MANUAL = 0x311,
	CISP_CMD_CH_AF_START = 0x400,
	CISP_CMD_CH_AF_STOP = 0x401,
	CISP_CMD_CH_AF_EARLYOUT_GET = 0x402,
	CISP_CMD_CH_AF_EARLYOUT_SET = 0x403,
	CISP_CMD_CH_AF_FOCUS_POS_GET = 0x404,
	CISP_CMD_CH_AF_SEARCH_POS_GET = 0x405,
	CISP_CMD_CH_AF_SEARCH_POS_SET = 0x406,
	CISP_CMD_CH_AF_ONE_SHOT = 0x407,
	CISP_CMD_CH_AF_WINDOW_PARAM_GET = 0x408,
	CISP_CMD_CH_AF_WINDOW_PARAM_SET = 0x409,
	CISP_CMD_CH_AF_UPDATE_SUSPEND = 0x40a,
	CISP_CMD_CH_AF_UPDATE_RESUME = 0x40b,
	CISP_CMD_CH_AF_SOFTLANDING_SET = 0x40c,
	CISP_CMD_CH_AF_SOFTLANDING_GET = 0x40d,
	CISP_CMD_CH_SENSOR_FRAME_RATE_SET = 0x500,
	CISP_CMD_CH_SENSOR_NVM_GET = 0x501,
	CISP_CMD_CH_SENSOR_NVM_RELOAD = 0x502,
	CISP_CMD_CH_SENSOR_TEST_PATTERN_CONFIG = 0x503,
	CISP_CMD_CH_SENSOR_WARM_STARTUP_CONFIG = 0x504,
	CISP_CMD_CH_SENSOR_CUSTOM_SETTING_CONFIG = 0x505,
	CISP_CMD_CH_SENSOR_TEMPERATURE_GET = 0x506,
	CISP_CMD_CH_SENSOR_PERMODULE_LSC_INFO_GET = 0x507,
	CISP_CMD_CH_SENSOR_PERMODULE_LSC_GET = 0x508,
	CISP_CMD_CH_SENSOR_BLC_UPDATE_SUSPEND = 0x509,
	CISP_CMD_CH_SENSOR_BLC_UPDATE_RESUME = 0x50a,
	CISP_CMD_CH_SENSOR_POWER_ON = 0x50b,
	CISP_CMD_CH_SENSOR_POWER_OFF = 0x50c,
	CISP_CMD_CH_FOCUS_LIMITS_SET = 0x700,
	CISP_CMD_CH_FOCUS_LIMITS_GET = 0x701,
	CISP_CMD_CH_FOCUS_POSITION_SET = 0x702,
	CISP_CMD_CH_FOCUS_POSITION_GET = 0x703,
	CISP_CMD_CH_FOCUS_STEP_SIZE_SET = 0x704,
	CISP_CMD_CH_FOCUS_STEP_SIZE_GET = 0x705,
	CISP_CMD_CH_FOCUS_CAL_BITSHIFT_SET = 0x706,
	CISP_CMD_CH_FOCUS_REINIT = 0x707,
	CISP_CMD_CH_LED_TORCH_PARAM_GET = 0x600,
	CISP_CMD_CH_LED_TORCH_PARAM_SET = 0x601,
	CISP_CMD_CH_LED_TORCH_OFF = 0x602,
	CISP_CMD_CH_LED_TORCH_ON = 0x603,
	CISP_CMD_CH_LED_TORCH_ON_INDICATOR = 0x604,
	CISP_CMD_CH_LED_TORCH_MANUAL_SET = 0x605,
	CISP_CMD_CH_STATUS_LED_BrightnessMan_SET = 0x606,
	CISP_CMD_CH_STATUS_LED_BrightnessMan_GET = 0x607,
	CISP_CMD_CH_STATUS_LED_DEBUG_SET = 0x608,
	CISP_CMD_CH_CROP_GET = 0x800,
	CISP_CMD_CH_CROP_SET = 0x801,
	CISP_CMD_CH_BPC_START = 0x802,
	CISP_CMD_CH_BPC_STOP = 0x803,
	CISP_CMD_CH_COLOR_CAL_DATA_SET = 0x804,
	CISP_CMD_CH_COLOR_CAL_DATA_GET = 0x805,
	CISP_CMD_CH_COLOR_CAL_IDEAL_SET = 0x806,
	CISP_CMD_CH_COLOR_CAL_IDEAL_GET = 0x807,
	CISP_CMD_CH_COLOR_CAL_ABS_GET = 0x808,
	CISP_CMD_CH_COLOR_CAL_ABS_OVERRIDE = 0x809,
	CISP_CMD_CH_SCALER_CROP_SET = 0x80a,
	CISP_CMD_CH_COLOR_SATURATION_GET = 0xa00,
	CISP_CMD_CH_COLOR_SATURATION_SET = 0xa01,
	CISP_CMD_CH_TONE_CURVE_CUSTOM_GET = 0xa02,
	CISP_CMD_CH_TONE_CURVE_CUSTOM_SET = 0xa03,
	CISP_CMD_CH_COLOR_LSC_TABLE_SET = 0xa04,
	CISP_CMD_CH_COLOR_LSC_START = 0xa05,
	CISP_CMD_CH_COLOR_LSC_STOP = 0xa06,
	CISP_CMD_CH_SCALER_SHARPNESS_SET = 0xa07,
	CISP_CMD_CH_SCALER_SHARPNESS_GET = 0xa08,
	CISP_CMD_CH_SHARPNESS_SET = 0xa09,
	CISP_CMD_CH_SHARPNESS_GET = 0xa0a,
	CISP_CMD_CH_NOISE_REDUCTION_SET = 0xa0b,
	CISP_CMD_CH_NOISE_REDUCTION_GET = 0xa0c,
	CISP_CMD_CH_CHROMA_SUPPRESSION_SET = 0xa0d,
	CISP_CMD_CH_CHROMA_SUPPRESSION_GET = 0xa0e,
	CISP_CMD_CH_HISTOGRAM_ENABLE = 0xa0f,
	CISP_CMD_CH_COLOR_FULL_RES_LSC_TABLE_SET = 0xa10,
	CISP_CMD_CH_COLOR_FULL_RES_LSC_ENABLE = 0xa11,
	CISP_CMD_CH_COLOR_FULL_RES_LSC_DISABLE = 0xa12,
	CISP_CMD_CH_KNOB_MANUAL_CONTROL_ENABLE = 0xa13,
	CISP_CMD_CH_KNOB_MANUAL_CONTROL_DISABLE = 0xa14,
	CISP_CMD_CH_BE_LASETTING_SET = 0xa15,
	CISP_CMD_CH_BE_LASETTING_GET = 0xa16,
	CISP_CMD_CH_BE_LA_INPUTMODE_SET = 0xa17,
	CISP_CMD_CH_BE_LA_INPUTMODE_GET = 0xa18,
	CISP_CMD_CH_DRC_SET = 0xa19,
	CISP_CMD_CH_DRC_GET = 0xa1a,
	CISP_CMD_CH_RPU_HISTOGRAM_PARAM_SET = 0xa1b,
	CISP_CMD_CH_ALS_ENABLE = 0xa1c,
	CISP_CMD_CH_ALS_DISABLE = 0xa1d,
	CISP_CMD_CH_ALS_MODE_SET = 0xa1e,
	CISP_CMD_CH_ALS_CCT_MANUAL = 0xa1f,
	CISP_CMD_CH_ALS_POLYNOMIAL_SET = 0xa20,
	CISP_CMD_CH_ALS_POLYNOMIAL_GET = 0xa21,
	CISP_CMD_CH_COLOR_LSC_TABLE_GET = 0xa22,
	CISP_CMD_CH_ALS_TEST_PATTERN_SET = 0xa23,
	CISP_CMD_CH_BE_LA_SUSPEND = 0xa24,
	CISP_CMD_CH_BE_LA_RESUME = 0xa25,
	CISP_CMD_CH_COLOR_LSC_IDEAL_TABLE_SET = 0xa26,
	CISP_CMD_CH_ALS_CCT_LIMIT_SET = 0xa27,
	CISP_CMD_CH_TONE_CURVE_CUSTOM_BRACKETING_SET = 0xa28,
	CISP_CMD_CH_MANUAL_BPC_GAIN_THRESHOLD_SET = 0xa29,
	CISP_CMD_CH_COLOR_LSC_TABLE_SOURCE_SET = 0xa2a,
	CISP_CMD_CH_ALS_SUSPEND = 0xa2b,
	CISP_CMD_CH_ALS_RESUME = 0xa2c,
	CISP_CMD_CH_OUTPUT_CONFIG_GET = 0xb00,
	CISP_CMD_CH_OUTPUT_CONFIG_SET = 0xb01,
	CISP_CMD_CH_SCALER_BRIGHTNESS_SET = 0xb02,
	CISP_CMD_CH_SCALER_CONTRAST_SET = 0xb03,
	CISP_CMD_CH_SCALER_SATURATION_SET = 0xb04,
	CISP_CMD_CH_SCALER_HUE_SET = 0xb05,
	CISP_CMD_CH_DRC_START = 0xc00,
	CISP_CMD_CH_DRC_STOP = 0xc01,
	CISP_CMD_CH_DRC_OFFLINE = 0xc02,
	CISP_CMD_CH_DRC_OFFLINE_START = 0xc03,
	CISP_CMD_CH_DRC_OFFLINE_STOP = 0xc04,
	CISP_CMD_CH_FACE_DETECTION_START = 0xd00,
	CISP_CMD_CH_FACE_DETECTION_STOP = 0xd01,
	CISP_CMD_CH_FACE_DETECTION_CONFIG_GET = 0xd02,
	CISP_CMD_CH_FACE_DETECTION_CONFIG_SET = 0xd03,
	CISP_CMD_CH_FACE_DETECTION_DISABLE = 0xd04,
	CISP_CMD_CH_FACE_DETECTION_ENABLE = 0xd05,
	CISP_CMD_CH_FACE_DETECTION_INPUT_SET = 0xd06,
	CISP_CMD_CH_FACE_DETECTION_IMAGE_ORIENTATION_SET = 0xd07,
	CISP_CMD_CH_FACE_DETECTION_OFFLINE = 0xd08,
	CISP_CMD_CH_FACE_DETECTION_OFFLINE_START = 0xd09,
	CISP_CMD_CH_FACE_DETECTION_OFFLINE_STOP = 0xd0a,
	CISP_CMD_CH_FACE_DETECTION_MODE_SET = 0xd0b,
	CISP_CMD_CH_FACE_DETECTION_WINDOW_PARAM_SET = 0xd0c,
	CISP_CMD_CH_FACE_DETECTION_WINDOW_PARAM_GET = 0xd0d,
	CISP_CMD_CH_FRAMEDONE_TIMEOUT = 0xe00,
	CISP_CMD_CH_FOCUS_DRIVER_INIT_FAILED = 0xe01,
	CISP_CMD_CH_NVSTORAGE_INFO_GET = 0xf00,
	CISP_CMD_CH_NVSTORAGE_DATA_GET = 0xf01,
	CISP_CMD_CH_NVSTORAGE_DATA_SET = 0xf02,
	CISP_CMD_APPLE_SET_FILE_LOAD = 0x8000,
	CISP_CMD_APPLE_BUFFER_INFO_SET_GET = 0x8001,
	CISP_CMD_APPLE_CH_HARDWARE_BLOCK_ENABLE = 0x8100,
	CISP_CMD_APPLE_CH_HARDWARE_BLOCK_DISABLE = 0x8101,
	CISP_CMD_APPLE_CH_CONTEXTSWITCH_ENABLE = 0x8102,
	CISP_CMD_APPLE_CH_CONTEXTSWITCH_DISABLE = 0x8103,
	CISP_CMD_APPLE_CH_SENSOR_NOISE_MODEL_SET = 0x8104,
	CISP_CMD_APPLE_CH_SENSOR_NOISE_MODEL_GET = 0x8105,
	CISP_CMD_APPLE_CH_STREAMING_MODE_SET = 0x8106,
	CISP_CMD_APPLE_CH_STREAMING_MODE_GET = 0x8107,
	CISP_CMD_APPLE_CH_AE_WINDOW_PARAM_SET = 0x8200,
	CISP_CMD_APPLE_CH_AE_DYNAMIC_SCENE_METERING_CONFIG_SET = 0x8201,
	CISP_CMD_APPLE_CH_AE_DYNAMIC_SCENE_METERING_START = 0x8202,
	CISP_CMD_APPLE_CH_AE_DYNAMIC_SCENE_METERING_STOP = 0x8203,
	CISP_CMD_APPLE_CH_AE_WINDOW_PARAM_GET = 0x8204,
	CISP_CMD_APPLE_CH_AE_WINDOW_WEIGHT_SET = 0x8205,
	CISP_CMD_APPLE_CH_AE_METERING_MODE_SET = 0x8206,
	CISP_CMD_APPLE_CH_AE_METERING_MODE_GET = 0x8207,
	CISP_CMD_APPLE_CH_AE_FLICKER_FREQ_SET = 0x8208,
	CISP_CMD_APPLE_CH_AE_MAX_FRAMERATE_GAIN_LIMIT_SET = 0x8209,
	CISP_CMD_APPLE_CH_AE_MAX_FRAMERATE_GAIN_LIMIT_GET = 0x820a,
	CISP_CMD_APPLE_CH_AE_TILES_MATRIX_METADATA_ENABLE = 0x820b,
	CISP_CMD_APPLE_CH_AE_BINNING_GAIN_LUX_THRESHOLD_SET = 0x820c,
	CISP_CMD_APPLE_CH_AE_PSEUDO_Y_WEIGHT_SET = 0x820d,
	CISP_CMD_APPLE_CH_AE_FD_SCENE_METERING_CONFIG_SET = 0x820e,
	CISP_CMD_APPLE_CH_AE_GAIN_CONVERGENCE_NORMALIZATION_SET = 0x820f,
	CISP_CMD_APPLE_CH_AE_GAIN_CONVERGENCE_NORMALIZATION_GET = 0x8210,
	CISP_CMD_APPLE_CH_AE_FD_SCENE_METERING_CONFIG_GET = 0x8211,
	CISP_CMD_APPLE_CH_AWB_CCT_GET = 0x8300,
	CISP_CMD_APPLE_CH_AWB_CCT_MANUAL = 0x8301,
	CISP_CMD_APPLE_CH_AWB_WINDOW_PARAM_GET = 0x8302,
	CISP_CMD_APPLE_CH_AWB_WINDOW_PARAM_SET = 0x8303,
	CISP_CMD_APPLE_CH_AWB_CALIB_TABLE_SET = 0x8304,
	CISP_CMD_APPLE_CH_AWB_SCHEME_SET = 0x8305,
	CISP_CMD_APPLE_CH_AWB_SCHEME_GET = 0x8306,
	CISP_CMD_APPLE_CH_AWB_HISTOGRAM_WEIGHT_SET = 0x8307,
	CISP_CMD_APPLE_CH_AWB_LUXTABLE_PARAM_SET = 0x8308,
	CISP_CMD_APPLE_CH_AWB_PROJECTION_POINT_SET = 0x8309,
	CISP_CMD_APPLE_CH_AWB_HISTOGRAM_X_TO_CCT_LUT_SET = 0x830a,
	CISP_CMD_APPLE_CH_AWB_2D_CCM_SET = 0x830b,
	CISP_CMD_APPLE_CH_AWB_PRE_CCM_GAIN_GET = 0x830c,
	CISP_CMD_APPLE_CH_AWB_CCM_GET = 0x830d,
	CISP_CMD_APPLE_CH_AWB_TEMPORAL_COHERENCE_FILTER_SET = 0x830e,
	CISP_CMD_APPLE_CH_AWB_SUSPEND_UPON_AE_STABLE_SET = 0x830f,
	CISP_CMD_APPLE_CH_AWB_SUSPEND_UPON_AE_STABLE_GET = 0x8310,
	CISP_CMD_APPLE_CH_AWB_POST_TINT_PARAM_SET = 0x8311,
	CISP_CMD_APPLE_CH_AWB_MIX_LIGHTING_X_LOC_SET = 0x8312,
	CISP_CMD_APPLE_CH_AWB_MIX_LIGHTING_CCM_SET = 0x8313,
	CISP_CMD_APPLE_CH_AWB_TILE_STATS_Y_THRESHOLD_SET = 0x8314,
	CISP_CMD_APPLE_CH_AWB_RATIO_SPACE_2ND_GAIN_THRESHOLD_SET = 0x8315,
	CISP_CMD_APPLE_CH_AWB_HISTOGRAM_TRIM_FILTER_V_SET = 0x8316,
	CISP_CMD_APPLE_CH_AWB_HISTOGRAM_TRIM_FILTER_H_SET = 0x8317,
	CISP_CMD_APPLE_CH_AWB_HISTOGRAM_TRIM_SCALE_PROFILE_SET = 0x8318,
	CISP_CMD_APPLE_CH_AWB_CCM_LUX_CLIP_SET = 0x8319,
	CISP_CMD_APPLE_CH_AWB_MANUAL_WB_GAIN_SET = 0x831a,
	CISP_CMD_APPLE_CH_AWB_CALIBRATION_MATRIX_GET = 0x831b,
	CISP_CMD_APPLE_CH_AF_WINDOW_PARAM_GET = 0x8400,
	CISP_CMD_APPLE_CH_AF_WINDOW_PARAM_SET = 0x8401,
	CISP_CMD_APPLE_CH_AF_WINDOW_WEIGHT_GET = 0x8402,
	CISP_CMD_APPLE_CH_AF_WINDOW_WEIGHT_SET = 0x8403,
	CISP_CMD_APPLE_CH_AF_WINDOW_FD_CONFIG = 0x8404,
	CISP_CMD_APPLE_CH_AF_PEAK_PREDICT_ENABLE_SET = 0x8405,
	CISP_CMD_APPLE_CH_AF_FOCUS_POS_OVERRIDE_SET = 0x8406,
	CISP_CMD_APPLE_CH_AF_PEAK_TRACKING_ENABLE = 0x8407,
	CISP_CMD_APPLE_CH_AF_PEAK_TRACKING_START = 0x8408,
	CISP_CMD_APPLE_CH_AF_FOCUS_MODE_SET = 0x8409,
	CISP_CMD_APPLE_CH_AF_FOCUS_MODE_GET = 0x840a,
	CISP_CMD_APPLE_CH_AF_MATRIX_MODE_CONFIG_SET = 0x840b,
	CISP_CMD_APPLE_CH_AF_MATRIX_MODE_CONFIG_GET = 0x840c,
	CISP_CMD_APPLE_CH_AF_MATRIX_MODE_DEBUG_GET = 0x840d,
	CISP_CMD_APPLE_CH_AF_SCAN_HISTORY_GET = 0x840e,
	CISP_CMD_APPLE_CH_FESTAT_CONFIG_GET = 0xc000,
	CISP_CMD_APPLE_CH_TILE_REGION_SET = 0xc001,
	CISP_CMD_APPLE_CH_TILE_WEIGHT_SET = 0xc002,
	CISP_CMD_APPLE_CH_COLOR_LSC_TABLE_SET = 0xc003,
	CISP_CMD_APPLE_CH_PIXEL_FILTER_TABLE_SET = 0xc004,
	CISP_CMD_APPLE_CH_CSC_CONFIG_SET = 0xc005,
	CISP_CMD_APPLE_CH_CSC_CONFIG_GET = 0xc006,
	CISP_CMD_APPLE_CH_CSC2_CONFIG_SET = 0xc007,
	CISP_CMD_APPLE_CH_CSC2_CONFIG_GET = 0xc008,
	CISP_CMD_APPLE_CH_COLOR_HIST_CONFIG_SET = 0xc009,
	CISP_CMD_APPLE_CH_COLOR_HIST_CONFIG_GET = 0xc00a,
	CISP_CMD_APPLE_CH_CSC_GAMMA_SET = 0xc00b,
	CISP_CMD_APPLE_CH_COLOR_HIST_CAPTURE = 0xc00c,
	CISP_CMD_APPLE_CH_COLOR_LSC_IDEAL_TABLE_SET = 0xc00d,
	CISP_CMD_APPLE_CH_STATPIXELDMAOUTPUT_SOURCE_SET = 0xc00e,
	CISP_CMD_APPLE_CH_STATPIXELDMAOUTPUT_INFO_GET = 0xc00f,
	CISP_CMD_APPLE_CH_AFFILTER_COEFF_SET = 0xc010,
	CISP_CMD_APPLE_CH_AFFILTER_COEFF_GET = 0xc011,
	CISP_CMD_APPLE_CH_EDGE_MAP_CONFIGURE = 0xc012,
	CISP_CMD_APPLE_CH_AFHORZFILT_COEFF_SET = 0xc013,
	CISP_CMD_APPLE_CH_AFHORZFILT_ENABLE_SET = 0xc014,
	CISP_CMD_APPLE_CH_AFHORZFILT_SUMMODE_SET = 0xc015,
	CISP_CMD_APPLE_CH_AFHORZFILT_THD_SET = 0xc016,
	CISP_CMD_APPLE_CH_TEMPORAL_FILTER_START = 0xc100,
	CISP_CMD_APPLE_CH_TEMPORAL_FILTER_STOP = 0xc101,
	CISP_CMD_APPLE_CH_MOTION_HISTORY_START = 0xc102,
	CISP_CMD_APPLE_CH_MOTION_HISTORY_STOP = 0xc103,
	CISP_CMD_APPLE_CH_TEMPORAL_FILTER_CONFIG_SET = 0xc104,
	CISP_CMD_APPLE_CH_TEMPORAL_FILTER_CONFIG_GET = 0xc105,
	CISP_CMD_APPLE_CH_TEMPORAL_FILTER_GAIN_SET = 0xc106,
	CISP_CMD_APPLE_CH_TEMPORAL_FILTER_GAIN_GET = 0xc107,
	CISP_CMD_APPLE_CH_MOTION_LUT_SET = 0xc108,
	CISP_CMD_APPLE_CH_MOTION_LUT_GET = 0xc109,
	CISP_CMD_APPLE_CH_LUMA_LUT_SET = 0xc10a,
	CISP_CMD_APPLE_CH_LUMA_LUT_GET = 0xc10b,
	CISP_CMD_APPLE_CH_TNR_MODE_SET = 0xc10c,
	CISP_CMD_APPLE_CH_TNR_MODE_GET = 0xc10d,
	CISP_CMD_APPLE_CH_TNR_PARAM_SET = 0xc10e,
	CISP_CMD_APPLE_CH_TNR_PARAM_GET = 0xc10f,
	CISP_CMD_APPLE_CH_TNR_INTERPOLATION_ENABLE = 0xc110,
	CISP_CMD_APPLE_CH_TNR_AVERAGE_START = 0xc111,
	CISP_CMD_APPLE_CH_TNR_AVERAGE_STOP = 0xc112,
	CISP_CMD_APPLE_CH_TEMPORAL_FILTER_ENABLE = 0xc113,
	CISP_CMD_APPLE_CH_TEMPORAL_FILTER_DISABLE = 0xc114,
	CISP_CMD_APPLE_CH_TNR_AVERAGE_FRAME_COUNT_SET = 0xc115,
	CISP_CMD_APPLE_CH_TNR_TUNING_PARAMS_SET = 0xc116,
	CISP_CMD_APPLE_CH_TNR_LSC_GAIN_SET = 0xc117,
	CISP_CMD_APPLE_CH_BINNING_COMPENSATION_FILTER_START = 0xc200,
	CISP_CMD_APPLE_CH_BINNING_COMPENSATION_FILTER_STOP = 0xc201,
	CISP_CMD_APPLE_CH_TONE_CURVE_ADAPTATION_START = 0xc300,
	CISP_CMD_APPLE_CH_TONE_CURVE_ADAPTATION_STOP = 0xc301,
	CISP_CMD_APPLE_CH_TONE_CURVE_PARAM_SET = 0xc302,
	CISP_CMD_APPLE_CH_TONE_CURVE_PARAM_GET = 0xc303,
	CISP_CMD_APPLE_CH_TONE_CURVE_UPDATE_SUSPEND = 0xc304,
	CISP_CMD_APPLE_CH_TONE_CURVE_UPDATE_RESUME = 0xc305,
	CISP_CMD_APPLE_CH_TONE_CURVE_MANUAL_CONTROL_ENABLE = 0xc306,
	CISP_CMD_APPLE_CH_TONE_CURVE_MANUAL_CONTROL_DISABLE = 0xc307,
	CISP_CMD_APPLE_CH_TONE_CURVE_LUX_ADAPTATION_TABLE_SET = 0xc308,
	CISP_CMD_APPLE_CH_TONE_CURVE_LUX_ADAPTATION_TABLE_GET = 0xc309,
	CISP_CMD_APPLE_CH_TONE_CURVE_LUX_ADAPTATION_LUXSCALE_SET = 0xc30a,
	CISP_CMD_APPLE_CH_TONE_CURVE_LUX_ADAPTATION_LUXSCALE_GET = 0xc30b,
	CISP_CMD_APPLE_CH_TONE_CURVE_STABILITY_SET = 0xc30c,
	CISP_CMD_APPLE_CH_TONE_CURVE_STABILITY_GET = 0xc30d,
	CISP_CMD_APPLE_CH_AUTO_HDR_HISTOGRAM_ENABLE = 0xc30e,
	CISP_CMD_APPLE_CH_AUTO_HDR_HISTOGRAM_DISABLE = 0xc30f,
	CISP_CMD_APPLE_CH_SNF_START = 0xc400,
	CISP_CMD_APPLE_CH_SNF_STOP = 0xc401,
	CISP_CMD_APPLE_CH_SNF_SET = 0xc402,
	CISP_CMD_APPLE_CH_SNF_GET = 0xc403,
	CISP_CMD_APPLE_CH_SNF_RADIAL_GAIN_SET = 0xc404,
	CISP_CMD_APPLE_CH_DPC_ENABLE = 0xc500,
	CISP_CMD_APPLE_CH_DPC_START = 0xc501,
	CISP_CMD_APPLE_CH_DPC_STOP = 0xc502,
	CISP_CMD_APPLE_CH_DPC_CTRLVALUE_OVERRIDE = 0xc503,
	CISP_CMD_APPLE_CH_DPC_MAX_DYN_COUNT_OVERRIDE = 0xc504,
	CISP_CMD_APPLE_CH_DPC_DYN_THRESHOLD0_OVERRIDE = 0xc505,
	CISP_CMD_APPLE_CH_DPC_DYN_THRESHOLD1_OVERRIDE = 0xc506,
	CISP_CMD_APPLE_CH_DPC_DESP_THRESHOLD0_OVERRIDE = 0xc507,
	CISP_CMD_APPLE_CH_DPC_DESP_THRESHOLD1_OVERRIDE = 0xc508,
	CISP_CMD_APPLE_CH_DPC_MAX_CORNER_OVERRIDE = 0xc509,
	CISP_CMD_APPLE_CH_DPC_MAX_EDGE_OVERRIDE = 0xc50a,
	CISP_CMD_APPLE_CH_DPC_MAX_CENTER_OVERRIDE = 0xc50b,
	CISP_CMD_APPLE_CH_DPC_STATIC_DEFECTS_TABLE_SET = 0xc50c,
};

enum isp_debug_cmds {
	CISP_CMD_DEBUG_BANNER=0,
	CISP_CMD_DEBUG_NOP1,
	CISP_CMD_DEBUG_NOP2,
	CISP_CMD_DEBUG_PS,
	CISP_CMD_DEBUG_GET_ROOT_HANDLE,
	CISP_CMD_DEBUG_GET_OBJECT_BY_NAME,
	CISP_CMD_DEBUG_GET_NUMBER_OF_CHILDREN,
	CISP_CMD_DEBUG_GET_CHILDREN_BY_INDEX,
	CISP_CMD_DEBUG_SHOW_OBJECT_GRAPH,
	CISP_CMD_DEBUG_DUMP_OBJECT,
	CISP_CMD_DEBUG_DUMP_ALL_OBJECTS,
	CISP_CMD_DEBUG_GET_DEBUG_LEVEL,
	CISP_CMD_DEBUG_SET_DEBUG_LEVEL,
	CISP_CMD_DEBUG_SET_DEBUG_LEVEL_RECURSIVE,
	CISP_CMD_DEBUG_GET_FSM_COUNT,
	CISP_CMD_DEBUG_GET_FSM_BY_INDEX,
	CISP_CMD_DEBUG_GET_FSM_BY_NAME,
	CISP_CMD_DEBUG_GET_FSM_DEBUG_LEVEL,
	CISP_CMD_DEBUG_SET_FSM_DEBUG_LEVEL,
	CISP_CMD_DEBUG_FSM_UNKNOWN, /* XXX: don't know what this cmd is doing yet */
	CISP_CMD_DEBUG_HEAP_STATISTICS,
	CISP_CMD_DEBUG_IRQ_STATISTICS,
	CISP_CMD_DEBUG_SHOW_SEMAPHORE_STATUS,
	CISP_CMD_DEBUG_START_CPU_PERFORMANCE_COUNTER,
	CISP_CMD_DEBUG_STOP_CPU_PERFORMANCE_COUNTER,
	CISP_CMD_DEBUG_SHOW_WIRING_OPERATIONS,
	CISP_CMD_DEBUG_SHOW_UNIT_TEST_STATUS,
	CISP_CMD_DEBUG_GET_ENVIRONMENT,
};

struct isp_mem_obj {
	struct resource base;
	unsigned int type;
	resource_size_t size;
	resource_size_t size_aligned;
	unsigned long offset;
};

struct isp_fw_args {
	u32 __unknown;
	u32 fw_arg;
	u32 full_stats_mode;
};

struct isp_channel_info {
	char name[64]; /* really that big? */
	u32 type;
	u32 source;
	u32 size;
	u32 offset;
};

struct isp_cmd_hdr {
	u32 unknown0;
	u16 opcode;
	u16 status;
} __attribute__((packed));

struct isp_cmd_print_enable {
	u32 enable;
} __attribute__((packed));

struct isp_cmd_config {
	u32 field0;
	u32 field4;
	u32 field8;
	u32 fieldc;
	u32 field10;
	u32 field14;
	u32 field18;
	u32 field1c;
} __attribute__((packed));

struct isp_cmd_set_loadfile {
	u32 unknown;
	u32 addr;
	u32 length;
} __attribute__((packed));

struct isp_cmd_channel_info {
	u32 field_0;
	u32 field_4;
	u32 field_8;
	u32 field_c;
	u16 sensorid0; /* field 10 */
	u16 field_12;
	u32 field_14;
	u16 sensorid1; /* field 18 */
	u16 field_1a;
	u32 field_1c;
	u32 field_20;
	u8 unknown[52];
	u32 sensor_count;
	u8 unknown2[40];
	u8 sensor_serial_number[8];
	u8 camera_module_serial_number[18];
} __attribute__((packed));

struct isp_cmd_channel_camera_config {
	u32 unknown;
	u32 channel;
	u8 data[88];
};

struct isp_cmd_channel_set_crop {
	u32 channel;
	u32 x1;
	u32 y1;
	u32 x2;
	u32 y2;
};

struct isp_cmd_channel_output_config {
	u32 channel;
	u32 x1;
	u32 y1;
	u32 unknown3;
	u32 pixelformat;
	u32 x2;
	u32 x3;
	u32 unknown5;
};

struct isp_cmd_channel_recycle_mode {
	u32 channel;
	u32 mode;
};

struct isp_cmd_channel_camera_config_select {
	u32 channel;
	u32 config;
};

struct isp_cmd_channel_drc_start {
	u32 channel;
};

struct isp_cmd_channel_tone_curve_adaptation_start {
	u32 channel;
};

struct isp_cmd_channel_sif_format_set {
	u32 channel;
	u8 param1;
	u8 param2;
	u8 unknown0;
	u8 unknown1;
};

struct isp_cmd_channel_camera_err_handle_config {
	u32 channel;
	u16 param1;
	u16 param2;
};

struct isp_cmd_channel_streaming_mode {
	u32 channel;
	u16 mode;
	u16 unknown;
};

struct isp_cmd_channel_frame_rate_set {
	u32 channel;
	u16 rate;
};

struct isp_cmd_channel_ae_speed_set {
	u32 channel;
	u16 speed;
};

struct isp_cmd_channel_ae_stability_set {
	u32 channel;
	u16 stability;
};

struct isp_cmd_channel_ae_stability_to_stable_set {
	u32 channel;
	u16 value;
};

struct isp_cmd_channel_face_detection_start {
	u32 channel;
};

struct isp_cmd_channel_face_detection_stop {
	u32 channel;
};

struct isp_cmd_channel_face_detection_enable {
	u32 channel;
};

struct isp_cmd_channel_face_detection_disable {
	u32 channel;
};

struct isp_cmd_channel_temporal_filter_start {
	u32 channel;
};

struct isp_cmd_channel_temporal_filter_stop {
	u32 channel;
};

struct isp_cmd_channel_temporal_filter_enable {
	u32 channel;
};

struct isp_cmd_channel_temporal_filter_disable {
	u32 channel;
};

struct isp_cmd_channel_motion_history_start {
	u32 channel;
};

struct isp_cmd_channel_motion_history_stop {
	u32 channel;
};

struct isp_cmd_channel_ae_metering_mode_set {
	u32 channel;
	u32 mode;
};

struct isp_cmd_channel_start {
	u32 channel;
};

struct isp_cmd_channel_stop {
	u32 channel;
};

struct isp_cmd_channel_brightness_set {
	u32 channel;
	u32 brightness;
};

struct isp_cmd_channel_contrast_set {
	u32 channel;
	u32 contrast;
};

struct isp_cmd_channel_saturation_set {
	u32 channel;
	u32 contrast;
};

struct isp_cmd_channel_hue_set {
	u32 channel;
	u32 contrast;
};

struct isp_cmd_channel {
	u32 channel;
};

struct isp_cmd_channel_buffer_return {
	u32 channel;
};


struct fthd_isp_debug_cmd {
	u32 show_errors;
	u32 arg[64];
};

#define to_isp_mem_obj(x) container_of((x), struct isp_mem_obj, base)

extern int isp_init(struct fthd_private *dev_priv);
extern int isp_uninit(struct fthd_private *dev_priv);

extern int isp_mem_init(struct fthd_private *dev_priv);
extern struct isp_mem_obj *isp_mem_create(struct fthd_private *dev_priv,
					  unsigned int type,
					  resource_size_t size);
extern int isp_mem_destroy(struct isp_mem_obj *obj);
extern int fthd_isp_cmd_start(struct fthd_private *dev_priv);
extern int fthd_isp_cmd_stop(struct fthd_private *dev_priv);
extern int isp_powerdown(struct fthd_private *dev_priv);
extern int fthd_isp_cmd_print_enable(struct fthd_private *dev_priv, int enable);
extern int fthd_isp_cmd_set_loadfile(struct fthd_private *dev_priv);
extern int fthd_isp_cmd_channel_info(struct fthd_private *dev_priv);
extern int fthd_isp_cmd_channel_start(struct fthd_private *dev_priv);
extern int fthd_isp_cmd_channel_stop(struct fthd_private *dev_priv);
extern int fthd_isp_cmd_channel_camera_config(struct fthd_private *dev_priv);
extern int fthd_isp_cmd_channel_crop_set(struct fthd_private *dev_priv, int channel,
					 int x1, int y1, int x2, int y2);
extern int fthd_isp_cmd_channel_output_config_set(struct fthd_private *dev_priv, int channel, int x, int y, int pixelformat);
extern int fthd_isp_cmd_channel_recycle_mode(struct fthd_private *dev_priv, int channel, int mode);
extern int fthd_isp_cmd_channel_recycle_start(struct fthd_private *dev_priv, int channel);
extern int fthd_isp_cmd_channel_camera_config_select(struct fthd_private *dev_priv, int channel, int config);
extern int fthd_isp_cmd_channel_drc_start(struct fthd_private *dev_priv, int channel);
extern int fthd_isp_cmd_channel_tone_curve_adaptation_start(struct fthd_private *dev_priv, int channel);
extern int fthd_isp_cmd_channel_sif_pixel_format(struct fthd_private *dev_priv, int channel, int param1, int param2);
extern int fthd_isp_cmd_channel_error_handling_config(struct fthd_private *dev_priv, int channel, int param1, int param2);
extern int fthd_isp_cmd_channel_streaming_mode(struct fthd_private *dev_priv, int channel, int mode);
extern int fthd_isp_cmd_channel_frame_rate_min(struct fthd_private *dev_priv, int channel, int rate);
extern int fthd_isp_cmd_channel_frame_rate_max(struct fthd_private *dev_priv, int channel, int rate);
extern int fthd_isp_cmd_camera_config(struct fthd_private *dev_priv);
extern int fthd_isp_cmd_channel_ae_speed_set(struct fthd_private *dev_priv, int channel, int speed);
extern int fthd_isp_cmd_channel_ae_stability_set(struct fthd_private *dev_priv, int channel, int stability);
extern int fthd_isp_cmd_channel_ae_stability_to_stable_set(struct fthd_private *dev_priv, int channel, int value);
extern int fthd_isp_cmd_channel_face_detection_enable(struct fthd_private *dev_priv, int channel);
extern int fthd_isp_cmd_channel_face_detection_disable(struct fthd_private *dev_priv, int channel);
extern int fthd_isp_cmd_channel_face_detection_start(struct fthd_private *dev_priv, int channel);
extern int fthd_isp_cmd_channel_face_detection_stop(struct fthd_private *dev_priv, int channel);
extern int fthd_isp_cmd_channel_temporal_filter_start(struct fthd_private *dev_priv, int channel);
extern int fthd_isp_cmd_channel_temporal_filter_stop(struct fthd_private *dev_priv, int channel);
extern int fthd_isp_cmd_channel_temporal_filter_enable(struct fthd_private *dev_priv, int channel);
extern int fthd_isp_cmd_channel_temporal_filter_disable(struct fthd_private *dev_priv, int channel);
extern int fthd_isp_cmd_channel_motion_history_start(struct fthd_private *dev_priv, int channel);
extern int fthd_isp_cmd_channel_motion_history_stop(struct fthd_private *dev_priv, int channel);
extern int fthd_isp_cmd_channel_ae_metering_mode_set(struct fthd_private *dev_priv, int channel, int mode);
extern int fthd_isp_cmd_channel_brightness_set(struct fthd_private *dev_priv, int channel, int brightness);
extern int fthd_isp_cmd_channel_contrast_set(struct fthd_private *dev_priv, int channel, int contrast);
extern int fthd_isp_cmd_channel_saturation_set(struct fthd_private *dev_priv, int channel, int saturation);
extern int fthd_isp_cmd_channel_hue_set(struct fthd_private *dev_priv, int channel, int hue);
extern int fthd_isp_cmd_channel_awb(struct fthd_private *dev_priv, int channel, int hue);
extern int fthd_isp_cmd_channel_buffer_return(struct fthd_private *dev_priv, int channel);
extern int fthd_start_channel(struct fthd_private *dev_priv, int channel);
extern int fthd_stop_channel(struct fthd_private *dev_priv, int channel);
extern int fthd_isp_debug_cmd(struct fthd_private *dev_priv, enum fthd_isp_cmds command, void *buf,
			      int request_len, int *response_len);

#endif
