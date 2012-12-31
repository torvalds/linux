/*
 * Samsung Exynos5 SoC series FIMC-IS driver
 *
 * exynos5 fimc-is helper functions
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 * Contact: Jiyoung Shin<idon.shin@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/memory.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/videodev2.h>
#include <media/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <linux/videodev2_samsung.h>
#include <linux/gpio.h>
#include <linux/gpio_event.h>
#include <plat/gpio-cfg.h>

#include "fimc-is-core.h"
#include "fimc-is-regs.h"
#include "fimc-is-cmd.h"
#include "fimc-is-param.h"
#include "fimc-is-err.h"
#include "fimc-is-helper.h"
#include "fimc-is-misc.h"

/*
Default setting values
*/
static const struct sensor_param init_val_sensor_preview_still = {
	.frame_rate = {
		.frame_rate = DEFAULT_PREVIEW_STILL_FRAMERATE,
	},
};

static const struct isp_param init_val_isp_preview_still = {
	.control = {
		.cmd = CONTROL_COMMAND_START,
		.bypass = CONTROL_BYPASS_DISABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_PREVIEW_STILL_WIDTH,
		.height = DEFAULT_PREVIEW_STILL_HEIGHT,
#ifndef ISP_STRGEN
		.format = OTF_INPUT_FORMAT_BAYER,
#else
		.format = OTF_INPUT_FORMAT_STRGEN_COLORBAR_BAYER,
#endif
		.bitwidth = OTF_INPUT_BIT_WIDTH_10BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.reserved[3] = 0,
		.reserved[4] = 66666,
		.err = OTF_INPUT_ERROR_NO,
	},
	.dma1_input = {
		.cmd = DMA_INPUT_COMMAND_DISABLE,
		.width = 0, .height = 0,
		.format = 0, .bitwidth = 0, .plane = 0,
		.order = 0, .buffer_number = 0, .buffer_address = 0,
		.err = 0,
	},
	.dma2_input = {
		.cmd = DMA_INPUT_COMMAND_DISABLE,
		.width = 0, .height = 0,
		.format = 0, .bitwidth = 0, .plane = 0,
		.order = 0, .buffer_number = 0, .buffer_address = 0,
		.err = 0,
	},
	.aa = {
		.cmd = ISP_AA_COMMAND_START,
		.target = ISP_AA_TARGET_AF | ISP_AA_TARGET_AE |
						ISP_AA_TARGET_AWB,
		.mode = 0,
		.face = 0,
		.win_pos_x = 0, .win_pos_y = 0,
		.err = ISP_AF_ERROR_NO,
	},
	.flash = {
		.cmd = ISP_FLASH_COMMAND_DISABLE,
		.redeye = ISP_FLASH_REDEYE_DISABLE,
		.err = ISP_FLASH_ERROR_NO,
	},
	.awb = {
		.cmd = ISP_AWB_COMMAND_AUTO,
		.illumination = 0,
		.err = ISP_AWB_ERROR_NO,
	},
	.effect = {
		.cmd = ISP_IMAGE_EFFECT_DISABLE,
		.err = ISP_IMAGE_EFFECT_ERROR_NO,
	},
	.iso = {
		.cmd = ISP_ISO_COMMAND_AUTO,
		.value = 0,
		.err = ISP_ISO_ERROR_NO,
	},
	.adjust = {
		.cmd = ISP_ADJUST_COMMAND_AUTO,
		.contrast = 0,
		.saturation = 0,
		.sharpness = 0,
		.exposure = 0,
		.brightness = 0,
		.hue = 0,
		.shutter_time_min = 0,
		.shutter_time_max = 66666,
		.err = ISP_ADJUST_ERROR_NO,
	},
	.metering = {
		.cmd = ISP_METERING_COMMAND_MATRIX,
		.win_pos_x = 0, .win_pos_y = 0,
		.win_width = DEFAULT_PREVIEW_STILL_WIDTH,
		.win_height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.err = ISP_METERING_ERROR_NO,
	},
	.afc = {
		.cmd = ISP_AFC_COMMAND_AUTO,
		.manual = 0, .err = ISP_AFC_ERROR_NO,
	},
	.otf_output = {
		.cmd = OTF_OUTPUT_COMMAND_ENABLE,
		.width = DEFAULT_PREVIEW_STILL_WIDTH,
		.height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.format = OTF_OUTPUT_FORMAT_YUV444,
		.bitwidth = OTF_OUTPUT_BIT_WIDTH_12BIT,
		.order = OTF_OUTPUT_ORDER_BAYER_GR_BG,
		.err = OTF_OUTPUT_ERROR_NO,
	},
	.dma1_output = {
#ifndef ISP_DMA
		.cmd = DMA_OUTPUT_COMMAND_DISABLE,
#else
		.cmd = DMA_OUTPUT_COMMAND_ENABLE,
#endif
		.width = DEFAULT_PREVIEW_STILL_WIDTH,
		.height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.format = DMA_OUTPUT_FORMAT_YUV422,
		.bitwidth = DMA_OUTPUT_BIT_WIDTH_8BIT,
		.plane = DMA_OUTPUT_PLANE_3,
		.order = DMA_INPUT_ORDER_NO,
		.buffer_number = 1,
		.buffer_address = 0x50060400,
		.err = DMA_OUTPUT_ERROR_NO,
	},
	.dma2_output = {
#ifndef ISP_DMA
		.cmd = DMA_OUTPUT_COMMAND_DISABLE,
#else
		.cmd = DMA_OUTPUT_COMMAND_ENABLE,
#endif
		.width = DEFAULT_PREVIEW_STILL_WIDTH,
		.height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.format = DMA_OUTPUT_FORMAT_BAYER,
		.bitwidth = DMA_OUTPUT_BIT_WIDTH_10BIT,
		.plane = DMA_OUTPUT_PLANE_1,
		.order = DMA_OUTPUT_ORDER_GB_BG,
		.buffer_number = 1,
		.buffer_address = 0x501D0000,
		.err = DMA_OUTPUT_ERROR_NO,
	},
};

static const struct drc_param init_val_drc_preview_still = {
	.control = {
		.cmd = CONTROL_COMMAND_START,
		.bypass = CONTROL_BYPASS_ENABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_PREVIEW_STILL_WIDTH,
		.height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.format = OTF_INPUT_FORMAT_YUV444,
		.bitwidth = OTF_INPUT_BIT_WIDTH_12BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.err = OTF_INPUT_ERROR_NO,
	},
	.dma_input = {
		.cmd = DMA_INPUT_COMMAND_DISABLE,
		.width = 0, .height = 0,
		.format = 0, .bitwidth = 0, .plane = 0,
		.order = 0, .buffer_number = 0, .buffer_address = 0,
		.err = 0,
	},
	.otf_output = {
		.cmd = OTF_OUTPUT_COMMAND_ENABLE,
		.width = DEFAULT_PREVIEW_STILL_WIDTH,
		.height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.format = OTF_OUTPUT_FORMAT_YUV444,
		.bitwidth = OTF_INPUT_BIT_WIDTH_8BIT,
		.order = OTF_OUTPUT_ORDER_BAYER_GR_BG,
		.err = OTF_OUTPUT_ERROR_NO,
	},
};

static const struct scalerc_param init_val_scalerc_preview_still = {
	.control = {
		.cmd = CONTROL_COMMAND_START,
		.bypass = CONTROL_BYPASS_ENABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_PREVIEW_STILL_WIDTH,
		.height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.format = OTF_INPUT_FORMAT_YUV444,
		.bitwidth = OTF_INPUT_BIT_WIDTH_12BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.crop_offset_x = 0,
		.crop_offset_y = 0,
		.crop_width = 0,
		.crop_height = 0,
		.err = OTF_INPUT_ERROR_NO,
	},
	.effect = {
		.cmd = 0,
		.err = 0,
	},
	.crop = {
		.cmd = OTF_INPUT_COMMAND_DISABLE,
		.pos_x = 0,
		.pos_y = 0,
		.crop_width = DEFAULT_PREVIEW_STILL_WIDTH,
		.crop_height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.err = 0,
	},
	.scale = {
		.cmd = OTF_INPUT_COMMAND_DISABLE,
		.pre_h_ratio = 0,
		.pre_v_ratio = 0,
		.sh_factor = 0,
		.h_ratio = 0,
		.v_ratio = 0,
		.err = 0,
	},
	.otf_output = {
		.cmd = OTF_OUTPUT_COMMAND_ENABLE,
		.width = DEFAULT_PREVIEW_STILL_WIDTH,
		.height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.format = OTF_OUTPUT_FORMAT_YUV444,
		.bitwidth = OTF_OUTPUT_BIT_WIDTH_8BIT,
		.order = OTF_OUTPUT_ORDER_BAYER_GR_BG,
		.err = OTF_OUTPUT_ERROR_NO,
	},
	.dma_output = {
		.cmd = DMA_OUTPUT_COMMAND_DISABLE,
		.width = DEFAULT_PREVIEW_STILL_WIDTH,
		.height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.format = DMA_OUTPUT_FORMAT_YUV422,
		.bitwidth = DMA_OUTPUT_BIT_WIDTH_8BIT,
		.plane = DMA_OUTPUT_PLANE_1,
		.order = DMA_OUTPUT_ORDER_CrYCbY,
		.buffer_number = 0,
		.buffer_address = 0,
		.reserved[0] = 2,
		.err = DMA_OUTPUT_ERROR_NO,
	},
};

static const struct odc_param init_val_odc_preview_still = {
	.control = {
		.cmd = CONTROL_COMMAND_START,
		.bypass = CONTROL_BYPASS_ENABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_PREVIEW_STILL_WIDTH,
		.height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.format = OTF_INPUT_FORMAT_YUV422,
		.bitwidth = OTF_INPUT_BIT_WIDTH_8BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.crop_offset_x = 0,
		.crop_offset_y = 0,
		.crop_width = 0,
		.crop_height = 0,
		.err = OTF_INPUT_ERROR_NO,
	},
	.otf_output = {
		.cmd = OTF_OUTPUT_COMMAND_ENABLE,
		.width = DEFAULT_PREVIEW_STILL_WIDTH,
		.height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.format = OTF_OUTPUT_FORMAT_YUV422,
		.bitwidth = OTF_OUTPUT_BIT_WIDTH_8BIT,
		.order = OTF_OUTPUT_ORDER_BAYER_GR_BG,
		.err = OTF_OUTPUT_ERROR_NO,
	},
};

static const struct dis_param init_val_dis_preview_still = {
	.control = {
		.cmd = CONTROL_COMMAND_START,
		.bypass = CONTROL_BYPASS_ENABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_PREVIEW_STILL_WIDTH,
		.height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.format = OTF_INPUT_FORMAT_YUV422,
		.bitwidth = OTF_INPUT_BIT_WIDTH_8BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.crop_offset_x = 0,
		.crop_offset_y = 0,
		.crop_width = 0,
		.crop_height = 0,
		.err = OTF_INPUT_ERROR_NO,
	},
	.otf_output = {
		.cmd = OTF_OUTPUT_COMMAND_ENABLE,
		.width = DEFAULT_PREVIEW_STILL_WIDTH,
		.height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.format = OTF_OUTPUT_FORMAT_YUV422,
		.bitwidth = OTF_OUTPUT_BIT_WIDTH_8BIT,
		.order = OTF_OUTPUT_ORDER_BAYER_GR_BG,
		.err = OTF_OUTPUT_ERROR_NO,
	},
};
static const struct tdnr_param init_val_tdnr_preview_still = {
	.control = {
		.cmd = CONTROL_COMMAND_START,
		.bypass = CONTROL_BYPASS_ENABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_PREVIEW_STILL_WIDTH,
		.height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.format = OTF_INPUT_FORMAT_YUV422,
		.bitwidth = OTF_INPUT_BIT_WIDTH_8BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.err = OTF_INPUT_ERROR_NO,
	},
	.frame = {
		.cmd = 0,
		.err = 0,
	},
	.otf_output = {
		.cmd = OTF_OUTPUT_COMMAND_ENABLE,
		.width = DEFAULT_PREVIEW_STILL_WIDTH,
		.height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.format = OTF_OUTPUT_FORMAT_YUV422,
		.bitwidth = OTF_INPUT_BIT_WIDTH_8BIT,
		.order = OTF_OUTPUT_ORDER_BAYER_GR_BG,
		.err = OTF_OUTPUT_ERROR_NO,
	},
	.dma_output = {
		.cmd = DMA_OUTPUT_COMMAND_DISABLE,
		.width = DEFAULT_PREVIEW_STILL_WIDTH,
		.height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.format = DMA_OUTPUT_FORMAT_YUV420,
		.bitwidth = DMA_OUTPUT_BIT_WIDTH_8BIT,
		.plane = DMA_OUTPUT_PLANE_2,
		.order = DMA_OUTPUT_ORDER_YCbYCr,    /*FW error, need  to change*/
		.buffer_number = 0,
		.buffer_address = 0,
		.err = DMA_OUTPUT_ERROR_NO,
	},
};

static const struct scalerp_param init_val_scalerp_preview_still = {
	.control = {
		.cmd = CONTROL_COMMAND_START,
		.bypass = CONTROL_BYPASS_ENABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_PREVIEW_STILL_WIDTH,
		.height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.format = OTF_INPUT_FORMAT_YUV444,
		.bitwidth = OTF_INPUT_BIT_WIDTH_8BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.crop_offset_x = 0,
		.crop_offset_y = 0,
		.crop_width = 0,
		.crop_height = 0,
		.err = OTF_INPUT_ERROR_NO,
	},
	.effect = {
		.cmd = 0,
		.err = 0,
	},
	.crop = {
		.cmd = OTF_INPUT_COMMAND_DISABLE,
		.pos_x = 0,
		.pos_y = 0,
		.crop_width = DEFAULT_PREVIEW_STILL_WIDTH,
		.crop_height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.err = 0,
	},
	.scale = {
		.cmd = OTF_INPUT_COMMAND_DISABLE,
		.pre_h_ratio = 0,
		.pre_v_ratio = 0,
		.sh_factor = 0,
		.h_ratio = 0,
		.v_ratio = 0,
		.err = 0,
	},
	.rotation = {
		.cmd = 0,
		.err = 0,
	},
	.flip = {
		.cmd = 0,
		.err = 0,
	},
	.otf_output = {
		.cmd = OTF_OUTPUT_COMMAND_ENABLE,
		.width = DEFAULT_PREVIEW_STILL_WIDTH,
		.height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.format = OTF_INPUT_FORMAT_YUV444,
		.bitwidth = OTF_INPUT_BIT_WIDTH_8BIT,
		.order = OTF_OUTPUT_ORDER_BAYER_GR_BG,
		.err = OTF_OUTPUT_ERROR_NO,
	},
	.dma_output = {
		.cmd = DMA_OUTPUT_COMMAND_DISABLE,
		.width = DEFAULT_PREVIEW_STILL_WIDTH,
		.height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.format = OTF_OUTPUT_FORMAT_YUV420,
		.bitwidth = DMA_OUTPUT_BIT_WIDTH_8BIT,
		.plane = DMA_OUTPUT_PLANE_3,
		.order = DMA_OUTPUT_ORDER_NO,
		.buffer_number = 0,
		.buffer_address = 0,
		.err = DMA_OUTPUT_ERROR_NO,
	},
};

static const struct fd_param init_val_fd_preview_still = {
	.control = {
		.cmd = CONTROL_COMMAND_STOP,
		.bypass = CONTROL_BYPASS_DISABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_PREVIEW_STILL_WIDTH,
		.height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.format = OTF_INPUT_FORMAT_YUV444,
		.bitwidth = OTF_INPUT_BIT_WIDTH_8BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.err = OTF_INPUT_ERROR_NO,
	},
	.dma_input = {
		.cmd = DMA_INPUT_COMMAND_DISABLE,
		.width = 0, .height = 0,
		.format = 0, .bitwidth = 0, .plane = 0,
		.order = 0, .buffer_number = 0, .buffer_address = 0,
		.err = 0,
	},
	.config = {
		.cmd = FD_CONFIG_COMMAND_MAXIMUM_NUMBER |
			FD_CONFIG_COMMAND_ROLL_ANGLE |
			FD_CONFIG_COMMAND_YAW_ANGLE |
			FD_CONFIG_COMMAND_SMILE_MODE |
			FD_CONFIG_COMMAND_BLINK_MODE |
			FD_CONFIG_COMMAND_EYES_DETECT |
			FD_CONFIG_COMMAND_MOUTH_DETECT |
			FD_CONFIG_COMMAND_ORIENTATION |
			FD_CONFIG_COMMAND_ORIENTATION_VALUE,
		.max_number = 5,
		.roll_angle = FD_CONFIG_ROLL_ANGLE_FULL,
		.yaw_angle = FD_CONFIG_YAW_ANGLE_45,
		.smile_mode = FD_CONFIG_SMILE_MODE_DISABLE,
		.blink_mode = FD_CONFIG_BLINK_MODE_DISABLE,
		.eye_detect = FD_CONFIG_EYES_DETECT_ENABLE,
		.mouth_detect = FD_CONFIG_MOUTH_DETECT_DISABLE,
		.orientation = FD_CONFIG_ORIENTATION_DISABLE,
		.orientation_value = 0,
		.err = ERROR_FD_NO,
	},
};

static const struct sensor_param init_val_sensor_capture = {
	.frame_rate = {
		.frame_rate = DEFAULT_CAPTURE_STILL_FRAMERATE,
	},
};

static const struct isp_param init_val_isp_capture = {
	.control = {
		.cmd = CONTROL_COMMAND_START,
		.bypass = CONTROL_BYPASS_DISABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_CAPTURE_STILL_WIDTH,
		.height = DEFAULT_CAPTURE_STILL_HEIGHT,
#ifndef ISP_STRGEN
		.format = OTF_INPUT_FORMAT_BAYER,
#else
		.format = OTF_INPUT_FORMAT_STRGEN_COLORBAR_BAYER,
#endif
		.bitwidth = OTF_INPUT_BIT_WIDTH_10BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.reserved[3] = 0,
		.reserved[4] = 66666,
		.err = OTF_INPUT_ERROR_NO,
	},
	.dma1_input = {
		.cmd = DMA_INPUT_COMMAND_DISABLE,
		.width = 0, .height = 0,
		.format = 0, .bitwidth = 0, .plane = 0,
		.order = 0, .buffer_number = 0, .buffer_address = 0,
		.err = 0,
	},
	.dma2_input = {
		.cmd = DMA_INPUT_COMMAND_DISABLE,
		.width = 0, .height = 0,
		.format = 0, .bitwidth = 0, .plane = 0,
		.order = 0, .buffer_number = 0, .buffer_address = 0,
		.err = 0,
	},
	.aa = {
		.cmd = ISP_AA_COMMAND_START,
		.target = ISP_AA_TARGET_AF | ISP_AA_TARGET_AE |
						ISP_AA_TARGET_AWB,
		.mode = 0,
		.face = 0,
		.win_pos_x = 0, .win_pos_y = 0,
		.err = ISP_AF_ERROR_NO,
	},
	.flash = {
		.cmd = ISP_FLASH_COMMAND_DISABLE,
		.redeye = ISP_FLASH_REDEYE_DISABLE,
		.err = ISP_FLASH_ERROR_NO,
	},
	.awb = {
		.cmd = ISP_AWB_COMMAND_AUTO,
		.illumination = 0,
		.err = ISP_AWB_ERROR_NO,
	},
	.effect = {
		.cmd = ISP_IMAGE_EFFECT_DISABLE,
		.err = ISP_IMAGE_EFFECT_ERROR_NO,
	},
	.iso = {
		.cmd = ISP_ISO_COMMAND_AUTO,
		.value = 0,
		.err = ISP_ISO_ERROR_NO,
	},
	.adjust = {
		.cmd = ISP_ADJUST_COMMAND_AUTO,
		.contrast = 0,
		.saturation = 0,
		.sharpness = 0,
		.exposure = 0,
		.brightness = 0,
		.hue = 0,
		.shutter_time_min = 0,
		.shutter_time_max = 66666,
		.err = ISP_ADJUST_ERROR_NO,
	},
	.metering = {
		.cmd = ISP_METERING_COMMAND_MATRIX,
		.win_pos_x = 0, .win_pos_y = 0,
		.win_width = DEFAULT_CAPTURE_STILL_WIDTH,
		.win_height = DEFAULT_CAPTURE_STILL_HEIGHT,
		.err = ISP_METERING_ERROR_NO,
	},
	.afc = {
		.cmd = ISP_AFC_COMMAND_AUTO,
		.manual = 0, .err = ISP_AFC_ERROR_NO,
	},
	.otf_output = {
		.cmd = OTF_OUTPUT_COMMAND_ENABLE,
		.width = DEFAULT_CAPTURE_STILL_WIDTH,
		.height = DEFAULT_CAPTURE_STILL_HEIGHT,
		.format = OTF_OUTPUT_FORMAT_YUV444,
		.bitwidth = OTF_OUTPUT_BIT_WIDTH_12BIT,
		.order = OTF_OUTPUT_ORDER_BAYER_GR_BG,
		.err = OTF_OUTPUT_ERROR_NO,
	},
	.dma1_output = {
		.cmd = DMA_OUTPUT_COMMAND_DISABLE,
		.width = 0, .height = 0,
		.format = 0, .bitwidth = 0, .plane = 0,
		.order = 0, .buffer_number = 0, .buffer_address = 0,
		.err = DMA_OUTPUT_ERROR_NO,
	},
	.dma2_output = {
		.cmd = DMA_OUTPUT_COMMAND_DISABLE,
		.width = 0, .height = 0,
		.format = 0, .bitwidth = 0, .plane = 0,
		.order = 0, .buffer_number = 0, .buffer_address = 0,
		.err = DMA_OUTPUT_ERROR_NO,
	},
};

static const struct drc_param init_val_drc_capture = {
	.control = {
		.cmd = CONTROL_COMMAND_START,
		.bypass = CONTROL_BYPASS_ENABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_CAPTURE_STILL_WIDTH,
		.height = DEFAULT_CAPTURE_STILL_HEIGHT,
		.format = OTF_INPUT_FORMAT_YUV444,
		.bitwidth = OTF_INPUT_BIT_WIDTH_12BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.err = OTF_INPUT_ERROR_NO,
	},
	.dma_input = {
		.cmd = DMA_INPUT_COMMAND_DISABLE,
		.width = 0, .height = 0,
		.format = 0, .bitwidth = 0, .plane = 0,
		.order = 0, .buffer_number = 0, .buffer_address = 0,
		.err = 0,
	},
	.otf_output = {
		.cmd = OTF_OUTPUT_COMMAND_ENABLE,
		.width = DEFAULT_CAPTURE_STILL_WIDTH,
		.height = DEFAULT_CAPTURE_STILL_HEIGHT,
		.format = OTF_OUTPUT_FORMAT_YUV444,
		.bitwidth = OTF_OUTPUT_BIT_WIDTH_8BIT,
		.order = OTF_OUTPUT_ORDER_BAYER_GR_BG,
		.err = OTF_OUTPUT_ERROR_NO,
	},
};

static const struct fd_param init_val_fd_capture = {
	.control = {
		.cmd = CONTROL_COMMAND_STOP,
		/* in FD case , bypass is not available */
		.bypass = CONTROL_BYPASS_DISABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_CAPTURE_STILL_WIDTH,
		.height = DEFAULT_CAPTURE_STILL_HEIGHT,
		.format = OTF_INPUT_FORMAT_YUV444,
		.bitwidth = OTF_OUTPUT_BIT_WIDTH_8BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.err = OTF_INPUT_ERROR_NO,
	},
	.dma_input = {
		.cmd = DMA_INPUT_COMMAND_DISABLE,
		.width = 0, .height = 0,
		.format = 0, .bitwidth = 0, .plane = 0,
		.order = 0, .buffer_number = 0, .buffer_address = 0,
		.err = 0,
	},
	.config = {
		.cmd = FD_CONFIG_COMMAND_MAXIMUM_NUMBER |
			FD_CONFIG_COMMAND_ROLL_ANGLE |
			FD_CONFIG_COMMAND_YAW_ANGLE |
			FD_CONFIG_COMMAND_SMILE_MODE |
			FD_CONFIG_COMMAND_BLINK_MODE |
			FD_CONFIG_COMMAND_EYES_DETECT |
			FD_CONFIG_COMMAND_MOUTH_DETECT |
			FD_CONFIG_COMMAND_ORIENTATION |
			FD_CONFIG_COMMAND_ORIENTATION_VALUE,
		.max_number = 5,
		.roll_angle = FD_CONFIG_ROLL_ANGLE_FULL,
		.yaw_angle = FD_CONFIG_YAW_ANGLE_45,
		.smile_mode = FD_CONFIG_SMILE_MODE_DISABLE,
		.blink_mode = FD_CONFIG_BLINK_MODE_DISABLE,
		.eye_detect = FD_CONFIG_EYES_DETECT_ENABLE,
		.mouth_detect = FD_CONFIG_MOUTH_DETECT_DISABLE,
		.orientation = FD_CONFIG_ORIENTATION_DISABLE,
		.orientation_value = 0,
		.err = ERROR_FD_NO,
	},
};

static const struct sensor_param init_val_sensor_preview_video = {
	.frame_rate = {
		.frame_rate = DEFAULT_PREVIEW_VIDEO_FRAMERATE,
	},
};

static const struct isp_param init_val_isp_preview_video = {
	.control = {
		.cmd = CONTROL_COMMAND_START,
		.bypass = CONTROL_BYPASS_DISABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_PREVIEW_VIDEO_WIDTH,
		.height = DEFAULT_PREVIEW_VIDEO_HEIGHT,
#ifndef ISP_STRGEN
		.format = OTF_INPUT_FORMAT_BAYER,
#else
		.format = OTF_INPUT_FORMAT_STRGEN_COLORBAR_BAYER,
#endif
		.bitwidth = OTF_INPUT_BIT_WIDTH_10BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.reserved[3] = 0,
		.reserved[4] = 66666,
		.err = OTF_INPUT_ERROR_NO,
	},
	.dma1_input = {
		.cmd = DMA_INPUT_COMMAND_DISABLE,
		.width = 0, .height = 0,
		.format = 0, .bitwidth = 0, .plane = 0,
		.order = 0, .buffer_number = 0, .buffer_address = 0,
		.err = 0,
	},
	.dma2_input = {
		.cmd = DMA_INPUT_COMMAND_DISABLE,
		.width = 0, .height = 0,
		.format = 0, .bitwidth = 0, .plane = 0,
		.order = 0, .buffer_number = 0, .buffer_address = 0,
		.err = 0,
	},
	.aa = {
		.cmd = ISP_AA_COMMAND_START,
		.target = ISP_AA_TARGET_AF | ISP_AA_TARGET_AE |
						ISP_AA_TARGET_AWB,
		.mode = 0,
		.face = 0,
		.win_pos_x = 0, .win_pos_y = 0,
		.err = ISP_AF_ERROR_NO,
	},
	.flash = {
		.cmd = ISP_FLASH_COMMAND_DISABLE,
		.redeye = ISP_FLASH_REDEYE_DISABLE,
		.err = ISP_FLASH_ERROR_NO,
	},
	.awb = {
		.cmd = ISP_AWB_COMMAND_AUTO,
		.illumination = 0,
		.err = ISP_AWB_ERROR_NO,
	},
	.effect = {
		.cmd = ISP_IMAGE_EFFECT_DISABLE,
		.err = ISP_IMAGE_EFFECT_ERROR_NO,
	},
	.iso = {
		.cmd = ISP_ISO_COMMAND_AUTO,
		.value = 0,
		.err = ISP_ISO_ERROR_NO,
	},
	.adjust = {
		.cmd = ISP_ADJUST_COMMAND_AUTO,
		.contrast = 0,
		.saturation = 0,
		.sharpness = 0,
		.exposure = 0,
		.brightness = 0,
		.hue = 0,
		.shutter_time_min = 0,
		.shutter_time_max = 33333,
		.err = ISP_ADJUST_ERROR_NO,
	},
	.metering = {
		.cmd = ISP_METERING_COMMAND_MATRIX,
		.win_pos_x = 0, .win_pos_y = 0,
		.win_width = DEFAULT_PREVIEW_VIDEO_WIDTH,
		.win_height = DEFAULT_PREVIEW_VIDEO_HEIGHT,
		.err = ISP_METERING_ERROR_NO,
	},
	.afc = {
		.cmd = ISP_AFC_COMMAND_AUTO,
		.manual = 0, .err = ISP_AFC_ERROR_NO,
	},
	.otf_output = {
		.cmd = OTF_OUTPUT_COMMAND_ENABLE,
		.width = DEFAULT_PREVIEW_VIDEO_WIDTH,
		.height = DEFAULT_PREVIEW_VIDEO_HEIGHT,
		.format = OTF_OUTPUT_FORMAT_YUV444,
		.bitwidth = OTF_OUTPUT_BIT_WIDTH_12BIT,
		.order = OTF_OUTPUT_ORDER_BAYER_GR_BG,
		.err = OTF_OUTPUT_ERROR_NO,
	},
	.dma1_output = {
#ifndef ISP_DMA
		.cmd = DMA_OUTPUT_COMMAND_DISABLE,
#else
		.cmd = DMA_OUTPUT_COMMAND_ENABLE,
#endif
		.width = DEFAULT_PREVIEW_VIDEO_WIDTH,
		.height = DEFAULT_PREVIEW_VIDEO_HEIGHT,
		.format = DMA_OUTPUT_FORMAT_YUV422,
		.bitwidth = DMA_OUTPUT_BIT_WIDTH_8BIT,
		.plane = DMA_OUTPUT_PLANE_3,
		.order = DMA_INPUT_ORDER_NO,
		.buffer_number = 1,
		.buffer_address = 0x50060400,
		.err = DMA_OUTPUT_ERROR_NO,
	},
	.dma2_output = {
#ifndef ISP_DMA
		.cmd = DMA_OUTPUT_COMMAND_DISABLE,
#else
		.cmd = DMA_OUTPUT_COMMAND_ENABLE,
#endif
		.width = DEFAULT_PREVIEW_VIDEO_WIDTH,
		.height = DEFAULT_PREVIEW_VIDEO_HEIGHT,
		.format = DMA_OUTPUT_FORMAT_BAYER,
		.bitwidth = DMA_OUTPUT_BIT_WIDTH_10BIT,
		.plane = DMA_OUTPUT_PLANE_1,
		.order = DMA_OUTPUT_ORDER_GB_BG,
		.buffer_number = 1,
		.buffer_address = 0x501D0000,
		.err = DMA_OUTPUT_ERROR_NO,
	},
};

static const struct drc_param init_val_drc_preview_video = {
	.control = {
		.cmd = CONTROL_COMMAND_START,
		.bypass = CONTROL_BYPASS_ENABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_PREVIEW_VIDEO_WIDTH,
		.height = DEFAULT_PREVIEW_VIDEO_HEIGHT,
		.format = OTF_INPUT_FORMAT_YUV444,
		.bitwidth = OTF_INPUT_BIT_WIDTH_12BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.err = OTF_INPUT_ERROR_NO,
	},
	.dma_input = {
		.cmd = DMA_INPUT_COMMAND_DISABLE,
		.width = 0, .height = 0,
		.format = 0, .bitwidth = 0, .plane = 0,
		.order = 0, .buffer_number = 0, .buffer_address = 0,
		.err = 0,
	},
	.otf_output = {
		.cmd = OTF_OUTPUT_COMMAND_ENABLE,
		.width = DEFAULT_PREVIEW_VIDEO_WIDTH,
		.height = DEFAULT_PREVIEW_VIDEO_HEIGHT,
		.format = OTF_OUTPUT_FORMAT_YUV444,
		.bitwidth = OTF_OUTPUT_BIT_WIDTH_8BIT,
		.order = OTF_OUTPUT_ORDER_BAYER_GR_BG,
		.err = OTF_OUTPUT_ERROR_NO,
	},
};

static const struct fd_param init_val_fd_preview_video = {
	.control = {
		.cmd = CONTROL_COMMAND_STOP,
		.bypass = CONTROL_BYPASS_DISABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_PREVIEW_VIDEO_WIDTH,
		.height = DEFAULT_PREVIEW_VIDEO_HEIGHT,
		.format = OTF_INPUT_FORMAT_YUV444,
		.bitwidth = OTF_INPUT_BIT_WIDTH_8BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.err = OTF_INPUT_ERROR_NO,
	},
	.dma_input = {
		.cmd = DMA_INPUT_COMMAND_DISABLE,
		.width = 0, .height = 0,
		.format = 0, .bitwidth = 0, .plane = 0,
		.order = 0, .buffer_number = 0, .buffer_address = 0,
		.err = 0,
	},
	.config = {
		.cmd = FD_CONFIG_COMMAND_MAXIMUM_NUMBER |
			FD_CONFIG_COMMAND_ROLL_ANGLE |
			FD_CONFIG_COMMAND_YAW_ANGLE |
			FD_CONFIG_COMMAND_SMILE_MODE |
			FD_CONFIG_COMMAND_BLINK_MODE |
			FD_CONFIG_COMMAND_EYES_DETECT |
			FD_CONFIG_COMMAND_MOUTH_DETECT |
			FD_CONFIG_COMMAND_ORIENTATION |
			FD_CONFIG_COMMAND_ORIENTATION_VALUE,
		.max_number = 5,
		.roll_angle = FD_CONFIG_ROLL_ANGLE_FULL,
		.yaw_angle = FD_CONFIG_YAW_ANGLE_45,
		.smile_mode = FD_CONFIG_SMILE_MODE_DISABLE,
		.blink_mode = FD_CONFIG_BLINK_MODE_DISABLE,
		.eye_detect = FD_CONFIG_EYES_DETECT_ENABLE,
		.mouth_detect = FD_CONFIG_MOUTH_DETECT_DISABLE,
		.orientation = FD_CONFIG_ORIENTATION_DISABLE,
		.orientation_value = 0,
		.err = ERROR_FD_NO,
	},
};


static const struct sensor_param init_val_sensor_camcording = {
	.frame_rate = {
		.frame_rate = DEFAULT_CAPTURE_VIDEO_FRAMERATE,
	},
};

static const struct isp_param init_val_isp_camcording = {
	.control = {
		.cmd = CONTROL_COMMAND_START,
		.bypass = CONTROL_BYPASS_DISABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
#ifndef ISP_STRGEN
		.format = OTF_INPUT_FORMAT_BAYER,
#else
		.format = OTF_INPUT_FORMAT_STRGEN_COLORBAR_BAYER,
#endif
		.bitwidth = OTF_INPUT_BIT_WIDTH_10BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.err = OTF_INPUT_ERROR_NO,
	},
	.dma1_input = {
		.cmd = DMA_INPUT_COMMAND_DISABLE,
		.width = 0, .height = 0,
		.format = 0, .bitwidth = 0, .plane = 0,
		.order = 0, .buffer_number = 0, .buffer_address = 0,
		.err = 0,
	},
	.dma2_input = {
		.cmd = DMA_INPUT_COMMAND_DISABLE,
		.width = 0, .height = 0,
		.format = 0, .bitwidth = 0, .plane = 0,
		.order = 0, .buffer_number = 0, .buffer_address = 0,
		.err = 0,
	},
	.aa = {
		.cmd = ISP_AA_COMMAND_START,
		.target = ISP_AA_TARGET_AF | ISP_AA_TARGET_AE |
						ISP_AA_TARGET_AWB,
		.mode = 0,
		.face = 0,
		.win_pos_x = 0, .win_pos_y = 0,
		.err = ISP_AF_ERROR_NO,
	},
	.flash = {
		.cmd = ISP_FLASH_COMMAND_DISABLE,
		.redeye = ISP_FLASH_REDEYE_DISABLE,
		.err = ISP_FLASH_ERROR_NO,
	},
	.awb = {
		.cmd = ISP_AWB_COMMAND_AUTO,
		.illumination = 0,
		.err = ISP_AWB_ERROR_NO,
	},
	.effect = {
		.cmd = ISP_IMAGE_EFFECT_DISABLE,
		.err = ISP_IMAGE_EFFECT_ERROR_NO,
	},
	.iso = {
		.cmd = ISP_ISO_COMMAND_AUTO,
		.value = 0,
		.err = ISP_ISO_ERROR_NO,
	},
	.adjust = {
		.cmd = ISP_ADJUST_COMMAND_AUTO,
		.contrast = 0,
		.saturation = 0,
		.sharpness = 0,
		.exposure = 0,
		.brightness = 0,
		.hue = 0,
		.shutter_time_min = 0,
		.shutter_time_max = 33333,
		.err = ISP_ADJUST_ERROR_NO,
	},
	.metering = {
		.cmd = ISP_METERING_COMMAND_MATRIX,
		.win_pos_x = 0, .win_pos_y = 0,
		.win_width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.win_height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
		.err = ISP_METERING_ERROR_NO,
	},
	.afc = {
		.cmd = ISP_AFC_COMMAND_AUTO,
		.manual = 0, .err = ISP_AFC_ERROR_NO,
	},
	.otf_output = {
		.cmd = OTF_OUTPUT_COMMAND_ENABLE,
		.width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
		.format = OTF_OUTPUT_FORMAT_YUV444,
		.bitwidth = OTF_OUTPUT_BIT_WIDTH_12BIT,
		.order = OTF_OUTPUT_ORDER_BAYER_GR_BG,
		.err = OTF_OUTPUT_ERROR_NO,
	},
	.dma1_output = {
#ifndef ISP_DMA
		.cmd = DMA_OUTPUT_COMMAND_DISABLE,
#else
		.cmd = DMA_OUTPUT_COMMAND_ENABLE,
#endif
		.width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
		.format = DMA_OUTPUT_FORMAT_YUV422,
		.bitwidth = DMA_OUTPUT_BIT_WIDTH_8BIT,
		.plane = DMA_OUTPUT_PLANE_3,
		.order = DMA_INPUT_ORDER_NO,
		.buffer_number = 1,
		.buffer_address = 0x50060400,
		.err = DMA_OUTPUT_ERROR_NO,
	},
	.dma2_output = {
#ifndef ISP_DMA
		.cmd = DMA_OUTPUT_COMMAND_DISABLE,
#else
		.cmd = DMA_OUTPUT_COMMAND_ENABLE,
#endif
		.width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
		.format = DMA_OUTPUT_FORMAT_BAYER,
		.bitwidth = DMA_OUTPUT_BIT_WIDTH_10BIT,
		.plane = DMA_OUTPUT_PLANE_1,
		.order = DMA_OUTPUT_ORDER_GB_BG,
		.buffer_number = 1,
		.buffer_address = 0x501D0000,
		.err = DMA_OUTPUT_ERROR_NO,
	},
};

static const struct drc_param init_val_drc_camcording = {
	.control = {
		.cmd = CONTROL_COMMAND_START,
		.bypass = CONTROL_BYPASS_ENABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
		.format = OTF_INPUT_FORMAT_YUV444,
		.bitwidth = OTF_INPUT_BIT_WIDTH_12BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.err = OTF_INPUT_ERROR_NO,
	},
	.dma_input = {
		.cmd = DMA_INPUT_COMMAND_DISABLE,
		.width = 0, .height = 0,
		.format = 0, .bitwidth = 0, .plane = 0,
		.order = 0, .buffer_number = 0, .buffer_address = 0,
		.err = 0,
	},
	.otf_output = {
		.cmd = OTF_OUTPUT_COMMAND_ENABLE,
		.width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
		.format = OTF_OUTPUT_FORMAT_YUV444,
		.bitwidth = OTF_OUTPUT_BIT_WIDTH_8BIT,
		.order = OTF_OUTPUT_ORDER_BAYER_GR_BG,
		.err = OTF_OUTPUT_ERROR_NO,
	},
};

static const struct fd_param init_val_fd_camcording = {
	.control = {
		.cmd = CONTROL_COMMAND_STOP,
		.bypass = CONTROL_BYPASS_DISABLE,
		.err = CONTROL_ERROR_NO,
	},
	.otf_input = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
		.format = OTF_INPUT_FORMAT_YUV444,
		.bitwidth = OTF_OUTPUT_BIT_WIDTH_8BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.err = OTF_INPUT_ERROR_NO,
	},
	.dma_input = {
		.cmd = DMA_INPUT_COMMAND_DISABLE,
		.width = 0, .height = 0,
		.format = 0, .bitwidth = 0, .plane = 0,
		.order = 0, .buffer_number = 0, .buffer_address = 0,
		.err = 0,
	},
	.config = {
		.cmd = FD_CONFIG_COMMAND_MAXIMUM_NUMBER |
			FD_CONFIG_COMMAND_ROLL_ANGLE |
			FD_CONFIG_COMMAND_YAW_ANGLE |
			FD_CONFIG_COMMAND_SMILE_MODE |
			FD_CONFIG_COMMAND_BLINK_MODE |
			FD_CONFIG_COMMAND_EYES_DETECT |
			FD_CONFIG_COMMAND_MOUTH_DETECT |
			FD_CONFIG_COMMAND_ORIENTATION |
			FD_CONFIG_COMMAND_ORIENTATION_VALUE,
		.max_number = 5,
		.roll_angle = FD_CONFIG_ROLL_ANGLE_FULL,
		.yaw_angle = FD_CONFIG_YAW_ANGLE_45,
		.smile_mode = FD_CONFIG_SMILE_MODE_DISABLE,
		.blink_mode = FD_CONFIG_BLINK_MODE_DISABLE,
		.eye_detect = FD_CONFIG_EYES_DETECT_ENABLE,
		.mouth_detect = FD_CONFIG_MOUTH_DETECT_DISABLE,
		.orientation = FD_CONFIG_ORIENTATION_DISABLE,
		.orientation_value = 0,
		.err = ERROR_FD_NO,
	},
};
/*
 Group 1. Interrupt
*/
void fimc_is_hw_set_intgr0_gd0(struct fimc_is_dev *dev)
{
	writel(INTGR0_INTGD0, dev->regs + INTGR0);
}

int fimc_is_hw_wait_intsr0_intsd0(struct fimc_is_dev *dev)
{
	u32 cfg = readl(dev->regs + INTSR0);
	u32 status = INTSR0_GET_INTSD0(cfg);
	while (status) {
		cfg = readl(dev->regs + INTSR0);
		status = INTSR0_GET_INTSD0(cfg);
	}
	return 0;
}

int fimc_is_hw_wait_intmsr0_intmsd0(struct fimc_is_dev *dev)
{
	u32 cfg = readl(dev->regs + INTMSR0);
	u32 status = INTMSR0_GET_INTMSD0(cfg);
	while (status) {
		cfg = readl(dev->regs + INTMSR0);
		status = INTMSR0_GET_INTMSD0(cfg);
	}
	return 0;
}

int fimc_is_fw_clear_irq1(struct fimc_is_dev *dev, unsigned int intr_pos)
{
	writel((1<<intr_pos), dev->regs + INTCR1);
	return 0;
}

int fimc_is_fw_clear_irq2(struct fimc_is_dev *dev)
{
	u32 cfg = readl(dev->regs + INTSR2);

	writel(cfg, dev->regs + INTCR2);
	return 0;
}

int fimc_is_fw_clear_insr1(struct fimc_is_dev *dev)
{

	writel(0, dev->regs + INTGR1);
	return 0;
}

/*
 Group 2. Common
*/
int fimc_is_hw_get_sensor_max_framerate(struct fimc_is_dev *dev)
{
	int max_framerate = 0;
	switch (dev->sensor.sensor_type) {
	case SENSOR_S5K3H2_CSI_A:
	case SENSOR_S5K3H2_CSI_B:
		max_framerate = 15;
		break;
	case SENSOR_S5K3H7_CSI_A:
	case SENSOR_S5K3H7_CSI_B:
		max_framerate = 30;
		break;
	case SENSOR_S5K6A3_CSI_A:
	case SENSOR_S5K6A3_CSI_B:
		max_framerate = 30;
		break;
	case SENSOR_S5K4E5_CSI_A:
	case SENSOR_S5K4E5_CSI_B:
		max_framerate = 30;
		break;
	default:
		max_framerate = 15;
	}
	return max_framerate;
}

void fimc_is_hw_open_sensor(struct fimc_is_dev *dev, u32 id, u32 sensor_index)
{
	fimc_is_hw_wait_intmsr0_intmsd0(dev);
	writel(HIC_OPEN_SENSOR, dev->regs + ISSR0);
	writel(id, dev->regs + ISSR1);
	switch (sensor_index) {
	case SENSOR_S5K3H2_CSI_A:
		dev->af.use_af = 1;
		dev->sensor.sensor_type = SENSOR_S5K3H2_CSI_A;
		writel(SENSOR_NAME_S5K3H2, dev->regs + ISSR2);
		writel(SENSOR_CONTROL_I2C0, dev->regs + ISSR3);
		break;
	case SENSOR_S5K3H2_CSI_B:
		dev->af.use_af = 1;
		dev->sensor.sensor_type = SENSOR_S5K3H2_CSI_B;
		writel(SENSOR_NAME_S5K3H2, dev->regs + ISSR2);
		writel(SENSOR_CONTROL_I2C1, dev->regs + ISSR3);
		break;
	case SENSOR_S5K6A3_CSI_A:
		dev->af.use_af = 0;
		dev->sensor.sensor_type = SENSOR_S5K6A3_CSI_A;
		writel(SENSOR_NAME_S5K6A3, dev->regs + ISSR2);
		writel(SENSOR_CONTROL_I2C0, dev->regs + ISSR3);
		break;
	case SENSOR_S5K6A3_CSI_B:
		dev->af.use_af = 0;
		dev->sensor.sensor_type = SENSOR_S5K6A3_CSI_B;
		writel(SENSOR_NAME_S5K6A3, dev->regs + ISSR2);
		writel(SENSOR_CONTROL_I2C1, dev->regs + ISSR3);
		break;
	case SENSOR_S5K4E5_CSI_A:
		dev->af.use_af = 0;
		dev->sensor.sensor_type = SENSOR_S5K4E5_CSI_A;
		writel(SENSOR_NAME_S5K4E5, dev->regs + ISSR2);
		writel(SENSOR_CONTROL_I2C0, dev->regs + ISSR3);
		break;
	case SENSOR_S5K4E5_CSI_B:
		dev->af.use_af = 0;
		dev->sensor.sensor_type = SENSOR_S5K4E5_CSI_B;
		writel(SENSOR_NAME_S5K4E5, dev->regs + ISSR2);
		writel(SENSOR_CONTROL_I2C1, dev->regs + ISSR3);
		break;
	}
	/* Parameter3 : Scenario ID(Initial Scenario) */
	writel(ISS_PREVIEW_STILL, dev->regs + ISSR4);
	fimc_is_hw_set_intgr0_gd0(dev);

}

void fimc_is_hw_close_sensor(struct fimc_is_dev *dev, u32 id)
{
	if (dev->sensor.id == id) {
		fimc_is_hw_wait_intmsr0_intmsd0(dev);
		writel(HIC_CLOSE_SENSOR, dev->regs + ISSR0);
		writel(dev->sensor.id, dev->regs + ISSR1);
		writel(dev->sensor.id, dev->regs + ISSR2);
		fimc_is_hw_set_intgr0_gd0(dev);
	}
}

void fimc_is_hw_diable_wdt(struct fimc_is_dev *dev)
{
	writel(0x0, dev->regs + WDT);
}

void fimc_is_hw_subip_poweroff(struct fimc_is_dev *dev)
{
	/* 1. Make FIMC-IS power-off state */
	fimc_is_hw_wait_intmsr0_intmsd0(dev);
	writel(HIC_POWER_DOWN, dev->regs + ISSR0);
	writel(dev->sensor.id, dev->regs + ISSR1);
	fimc_is_hw_set_intgr0_gd0(dev);
}

void fimc_is_hw_a5_power(struct fimc_is_dev *isp, int on)
{
	int ret = 0;
	struct device *dev = &isp->pdev->dev;

	printk(KERN_INFO "%s(%d)\n", __func__, on);
	if (on) {
		/* 2. enable ISP */
		clear_bit(FIMC_IS_PWR_ST_POWEROFF, &isp->power);
		set_bit(FIMC_IS_PWR_ST_POWERED, &isp->power);
		ret = pm_runtime_get_sync(dev);
	} else {

		clear_bit(FIMC_IS_PWR_ST_POWERED, &isp->power);

		/*start mipi & fimclite*/
		stop_fimc_lite();
		mdelay(10);
		stop_mipi_csi();

#if defined(CONFIG_VIDEOBUF2_ION)
		if (isp->alloc_ctx)
			fimc_is_mem_suspend(isp->alloc_ctx);
#endif
		if (isp->pdata->clk_off) {
			isp->pdata->clk_off(isp->pdev);
		} else {
			dev_err(&isp->pdev->dev, "failed to clock on\n");
			return;
		}
		ret = pm_runtime_put_sync(dev);
	}
}

void fimc_is_hw_set_sensor_num(struct fimc_is_dev *dev)
{
	u32 cfg;
	writel(ISR_DONE, dev->regs + ISSR0);
	cfg = dev->sensor.id;
	writel(cfg, dev->regs + ISSR1);
	/* param 1 */
	writel(IHC_GET_SENSOR_NUMBER, dev->regs + ISSR2);
	/* param 2 */
	cfg = dev->sensor_num;
	writel(cfg, dev->regs + ISSR3);
}

void fimc_is_hw_set_load_setfile(struct fimc_is_dev *dev)
{
	u32 cfg;
	writel(ISR_DONE, dev->regs + ISSR0);
	cfg = dev->sensor.id;
	writel(cfg, dev->regs + ISSR1);
	/* param 1 */
	writel(IHC_LOAD_SET_FILE, dev->regs + ISSR2);
	/* param 2 */
	cfg = dev->sensor_num;
	writel(cfg, dev->regs + ISSR3);
}

int fimc_is_hw_get_sensor_num(struct fimc_is_dev *dev)
{
	u32 cfg = readl(dev->regs + ISSR11);
	if (dev->sensor_num == cfg)
		return 0;
	else
		return cfg;
}

int fimc_is_hw_set_param(struct fimc_is_dev *dev)
{
	fimc_is_hw_wait_intmsr0_intmsd0(dev);
	writel(HIC_SET_PARAMETER, dev->regs + ISSR0);
	writel(dev->sensor.id, dev->regs + ISSR1);

	writel(dev->scenario_id, dev->regs + ISSR2);

	writel(atomic_read(&dev->p_region_num), dev->regs + ISSR3);
	writel(dev->p_region_index1, dev->regs + ISSR4);
	writel(dev->p_region_index2, dev->regs + ISSR5);
	dbg("### set param\n");
	dbg("cmd :0x%08x\n",HIC_SET_PARAMETER);
	dbg("senorID :0x%08x\n", dev->sensor.id);
	dbg("parma1 :0x%08x\n", dev->scenario_id);
	dbg("parma2 :0x%08x\n", atomic_read(&dev->p_region_num));
	dbg("parma3 :0x%08x\n", (unsigned int)dev->p_region_index1);
	dbg("parma4 :0x%08x\n", (unsigned int)dev->p_region_index2);

	fimc_is_hw_set_intgr0_gd0(dev);
	return 0;
}

int fimc_is_hw_get_param(struct fimc_is_dev *dev, u16 offset)
{
	dev->i2h_cmd.num_valid_args = offset;
	switch (offset) {
	case 1:
		dev->i2h_cmd.arg[0] = readl(dev->regs + ISSR12);
		dev->i2h_cmd.arg[1] = 0;
		dev->i2h_cmd.arg[2] = 0;
		dev->i2h_cmd.arg[3] = 0;
		break;
	case 2:
		dev->i2h_cmd.arg[0] = readl(dev->regs + ISSR12);
		dev->i2h_cmd.arg[1] = readl(dev->regs + ISSR13);
		dev->i2h_cmd.arg[2] = 0;
		dev->i2h_cmd.arg[3] = 0;
		break;
	case 3:
		dev->i2h_cmd.arg[0] = readl(dev->regs + ISSR12);
		dev->i2h_cmd.arg[1] = readl(dev->regs + ISSR13);
		dev->i2h_cmd.arg[2] = readl(dev->regs + ISSR14);
		dev->i2h_cmd.arg[3] = 0;
		break;
	case 4:
		dev->i2h_cmd.arg[0] = readl(dev->regs + ISSR12);
		dev->i2h_cmd.arg[1] = readl(dev->regs + ISSR13);
		dev->i2h_cmd.arg[2] = readl(dev->regs + ISSR14);
		dev->i2h_cmd.arg[3] = readl(dev->regs + ISSR15);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

void fimc_is_hw_set_stream(struct fimc_is_dev *dev, int on)
{
	if (on) {
		fimc_is_hw_wait_intmsr0_intmsd0(dev);
		writel(HIC_STREAM_ON, dev->regs + ISSR0);
		writel(dev->sensor.id, dev->regs + ISSR1);
		fimc_is_hw_set_intgr0_gd0(dev);
	} else {
		fimc_is_hw_wait_intmsr0_intmsd0(dev);
		writel(HIC_STREAM_OFF, dev->regs + ISSR0);
		writel(dev->sensor.id, dev->regs + ISSR1);
		fimc_is_hw_set_intgr0_gd0(dev);
	}
}

void fimc_is_hw_change_mode(struct fimc_is_dev *dev, int val)
{
	switch (val) {
	case IS_MODE_PREVIEW_STILL:
		dev->scenario_id = ISS_PREVIEW_STILL;
		fimc_is_hw_wait_intmsr0_intmsd0(dev);
		clear_bit(IS_ST_RUN, &dev->state);
		set_bit(IS_ST_CHANGE_MODE, &dev->state);
		writel(HIC_PREVIEW_STILL, dev->regs + ISSR0);
		writel(dev->sensor.id, dev->regs + ISSR1);
		fimc_is_hw_set_intgr0_gd0(dev);
		break;
	case IS_MODE_PREVIEW_VIDEO:
		dev->scenario_id = ISS_PREVIEW_VIDEO;
		fimc_is_hw_wait_intmsr0_intmsd0(dev);
		clear_bit(IS_ST_RUN, &dev->state);
		set_bit(IS_ST_CHANGE_MODE, &dev->state);
		writel(HIC_PREVIEW_VIDEO, dev->regs + ISSR0);
		writel(dev->sensor.id, dev->regs + ISSR1);
		fimc_is_hw_set_intgr0_gd0(dev);
		break;
	case IS_MODE_CAPTURE_STILL:
		dev->scenario_id = ISS_CAPTURE_STILL;
		fimc_is_hw_wait_intmsr0_intmsd0(dev);
		clear_bit(IS_ST_RUN, &dev->state);
		set_bit(IS_ST_CHANGE_MODE, &dev->state);
		writel(HIC_CAPTURE_STILL, dev->regs + ISSR0);
		writel(dev->sensor.id, dev->regs + ISSR1);
		fimc_is_hw_set_intgr0_gd0(dev);
		break;
	case IS_MODE_CAPTURE_VIDEO:
		dev->scenario_id = ISS_CAPTURE_VIDEO;
		fimc_is_hw_wait_intmsr0_intmsd0(dev);
		clear_bit(IS_ST_RUN, &dev->state);
		set_bit(IS_ST_CHANGE_MODE, &dev->state);
		writel(HIC_CAPTURE_VIDEO, dev->regs + ISSR0);
		writel(dev->sensor.id, dev->regs + ISSR1);
		fimc_is_hw_set_intgr0_gd0(dev);
		break;
	}
}
/*
 Group 3. Initial setting
*/
void fimc_is_hw_set_init(struct fimc_is_dev *dev)
{
	u32 length;

	switch (dev->scenario_id) {
	case ISS_PREVIEW_STILL:
		IS_SET_PARAM_GLOBAL_SHOTMODE_CMD(dev, 0);
		IS_SET_PARAM_BIT(dev, PARAM_GLOBAL_SHOTMODE);
		IS_INC_PARAM_NUM(dev);
		IS_SENSOR_SET_FRAME_RATE(dev, DEFAULT_PREVIEW_STILL_FRAMERATE);
		IS_SET_PARAM_BIT(dev, PARAM_SENSOR_FRAME_RATE);
		IS_INC_PARAM_NUM(dev);
		/* ISP */
		IS_ISP_SET_PARAM_CONTROL_CMD(dev,
			init_val_isp_preview_still.control.cmd);
		IS_ISP_SET_PARAM_CONTROL_BYPASS(dev,
			init_val_isp_preview_still.control.bypass);
		IS_ISP_SET_PARAM_CONTROL_ERR(dev,
			init_val_isp_preview_still.control.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_OTF_INPUT_CMD(dev,
			init_val_isp_preview_still.otf_input.cmd);
		IS_ISP_SET_PARAM_OTF_INPUT_WIDTH(dev,
			init_val_isp_preview_still.otf_input.width);
		IS_ISP_SET_PARAM_OTF_INPUT_HEIGHT(dev,
			init_val_isp_preview_still.otf_input.height);
		dev->sensor.width_prev =
			init_val_isp_preview_still.otf_input.width;
		dev->sensor.height_prev =
			init_val_isp_preview_still.otf_input.height;
		IS_ISP_SET_PARAM_OTF_INPUT_FORMAT(dev,
			init_val_isp_preview_still.otf_input.format);
		IS_ISP_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
			init_val_isp_preview_still.otf_input.bitwidth);
		IS_ISP_SET_PARAM_OTF_INPUT_ORDER(dev,
			init_val_isp_preview_still.otf_input.order);
		IS_ISP_SET_PARAM_OTF_INPUT_ERR(dev,
			init_val_isp_preview_still.otf_input.err);
		IS_ISP_SET_PARAM_OTF_INPUT_RESERVED3(dev,
			init_val_isp_preview_still.otf_input.reserved[3]);
		IS_ISP_SET_PARAM_OTF_INPUT_RESERVED4(dev,
			init_val_isp_preview_still.otf_input.reserved[4]);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_DMA_INPUT1_CMD(dev,
			init_val_isp_preview_still.dma1_input.cmd);
		IS_ISP_SET_PARAM_DMA_INPUT1_WIDTH(dev,
			init_val_isp_preview_still.dma1_input.width);
		IS_ISP_SET_PARAM_DMA_INPUT1_HEIGHT(dev,
			init_val_isp_preview_still.dma1_input.height);
		IS_ISP_SET_PARAM_DMA_INPUT1_FORMAT(dev,
			init_val_isp_preview_still.dma1_input.format);
		IS_ISP_SET_PARAM_DMA_INPUT1_BITWIDTH(dev,
			init_val_isp_preview_still.dma1_input.bitwidth);
		IS_ISP_SET_PARAM_DMA_INPUT1_PLANE(dev,
			init_val_isp_preview_still.dma1_input.plane);
		IS_ISP_SET_PARAM_DMA_INPUT1_ORDER(dev,
			init_val_isp_preview_still.dma1_input.order);
		IS_ISP_SET_PARAM_DMA_INPUT1_BUFFERNUM(dev,
			init_val_isp_preview_still.dma1_input.buffer_number);
		IS_ISP_SET_PARAM_DMA_INPUT1_BUFFERADDR(dev,
			init_val_isp_preview_still.dma1_input.buffer_address);
		IS_ISP_SET_PARAM_DMA_INPUT1_ERR(dev,
			init_val_isp_preview_still.dma1_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_DMA1_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_DMA_INPUT2_CMD(dev,
			init_val_isp_preview_still.dma2_input.cmd);
		IS_ISP_SET_PARAM_DMA_INPUT2_WIDTH(dev,
			init_val_isp_preview_still.dma2_input.width);
		IS_ISP_SET_PARAM_DMA_INPUT2_HEIGHT(dev,
			init_val_isp_preview_still.dma2_input.height);
		IS_ISP_SET_PARAM_DMA_INPUT2_FORMAT(dev,
			init_val_isp_preview_still.dma2_input.format);
		IS_ISP_SET_PARAM_DMA_INPUT2_BITWIDTH(dev,
			init_val_isp_preview_still.dma2_input.bitwidth);
		IS_ISP_SET_PARAM_DMA_INPUT2_PLANE(dev,
			init_val_isp_preview_still.dma2_input.plane);
		IS_ISP_SET_PARAM_DMA_INPUT2_ORDER(dev,
			init_val_isp_preview_still.dma2_input.order);
		IS_ISP_SET_PARAM_DMA_INPUT2_BUFFERNUM(dev,
			init_val_isp_preview_still.dma2_input.buffer_number);
		IS_ISP_SET_PARAM_DMA_INPUT2_BUFFERADDR(dev,
			init_val_isp_preview_still.dma2_input.buffer_address);
		IS_ISP_SET_PARAM_DMA_INPUT2_ERR(dev,
			init_val_isp_preview_still.dma2_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_DMA2_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_AA_CMD(dev,
				init_val_isp_preview_still.aa.cmd);
		IS_ISP_SET_PARAM_AA_TARGET(dev,
				init_val_isp_preview_still.aa.target);
		IS_ISP_SET_PARAM_AA_MODE(dev,
				init_val_isp_preview_still.aa.mode);
		IS_ISP_SET_PARAM_AA_FACE(dev,
				init_val_isp_preview_still.aa.face);
		IS_ISP_SET_PARAM_AA_WIN_POS_X(dev,
			init_val_isp_preview_still.aa.win_pos_x);
		IS_ISP_SET_PARAM_AA_WIN_POS_Y(dev,
			init_val_isp_preview_still.aa.win_pos_y);
		IS_ISP_SET_PARAM_AA_ERR(dev,
			init_val_isp_preview_still.aa.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_FLASH_CMD(dev,
			init_val_isp_preview_still.flash.cmd);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev,
			init_val_isp_preview_still.flash.redeye);
		IS_ISP_SET_PARAM_FLASH_ERR(dev,
			init_val_isp_preview_still.flash.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_AWB_CMD(dev,
			init_val_isp_preview_still.awb.cmd);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
			init_val_isp_preview_still.awb.illumination);
		IS_ISP_SET_PARAM_AWB_ERR(dev,
			init_val_isp_preview_still.awb.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_EFFECT_CMD(dev,
			init_val_isp_preview_still.effect.cmd);
		IS_ISP_SET_PARAM_EFFECT_ERR(dev,
			init_val_isp_preview_still.effect.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_IMAGE_EFFECT);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_ISO_CMD(dev,
			init_val_isp_preview_still.iso.cmd);
		IS_ISP_SET_PARAM_ISO_VALUE(dev,
			init_val_isp_preview_still.iso.value);
		IS_ISP_SET_PARAM_ISO_ERR(dev,
			init_val_isp_preview_still.iso.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
			init_val_isp_preview_still.adjust.cmd);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev,
			init_val_isp_preview_still.adjust.contrast);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev,
			init_val_isp_preview_still.adjust.saturation);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev,
			init_val_isp_preview_still.adjust.sharpness);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev,
			init_val_isp_preview_still.adjust.exposure);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev,
			init_val_isp_preview_still.adjust.brightness);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev,
			init_val_isp_preview_still.adjust.hue);
		IS_ISP_SET_PARAM_ADJUST_SHUTTER_TIME_MIN(dev,
			init_val_isp_preview_still.adjust.shutter_time_min);
		IS_ISP_SET_PARAM_ADJUST_SHUTTER_TIME_MAX(dev,
			init_val_isp_preview_still.adjust.shutter_time_max);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev,
			init_val_isp_preview_still.adjust.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_METERING_CMD(dev,
			init_val_isp_preview_still.metering.cmd);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev,
			init_val_isp_preview_still.metering.win_pos_x);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev,
			init_val_isp_preview_still.metering.win_pos_y);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev,
			init_val_isp_preview_still.metering.win_width);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev,
			init_val_isp_preview_still.metering.win_height);
		IS_ISP_SET_PARAM_METERING_ERR(dev,
			init_val_isp_preview_still.metering.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_AFC_CMD(dev,
			init_val_isp_preview_still.afc.cmd);
		IS_ISP_SET_PARAM_AFC_MANUAL(dev,
			init_val_isp_preview_still.afc.manual);
		IS_ISP_SET_PARAM_AFC_ERR(dev,
			init_val_isp_preview_still.afc.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AFC);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_OTF_OUTPUT_CMD(dev,
			init_val_isp_preview_still.otf_output.cmd);
		IS_ISP_SET_PARAM_OTF_OUTPUT_WIDTH(dev,
			init_val_isp_preview_still.otf_output.width);
		IS_ISP_SET_PARAM_OTF_OUTPUT_HEIGHT(dev,
			init_val_isp_preview_still.otf_output.height);
		IS_ISP_SET_PARAM_OTF_OUTPUT_FORMAT(dev,
			init_val_isp_preview_still.otf_output.format);
		IS_ISP_SET_PARAM_OTF_OUTPUT_BITWIDTH(dev,
			init_val_isp_preview_still.otf_output.bitwidth);
		IS_ISP_SET_PARAM_OTF_OUTPUT_ORDER(dev,
			init_val_isp_preview_still.otf_output.order);
		IS_ISP_SET_PARAM_OTF_OUTPUT_ERR(dev,
			init_val_isp_preview_still.otf_output.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_OUTPUT);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_CMD(dev,
			init_val_isp_preview_still.dma1_output.cmd);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_WIDTH(dev,
			init_val_isp_preview_still.dma1_output.width);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_HEIGHT(dev,
			init_val_isp_preview_still.dma1_output.height);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_FORMAT(dev,
			init_val_isp_preview_still.dma1_output.format);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_BITWIDTH(dev,
			init_val_isp_preview_still.dma1_output.bitwidth);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_PLANE(dev,
			init_val_isp_preview_still.dma1_output.plane);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_ORDER(dev,
			init_val_isp_preview_still.dma1_output.order);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_BUFFER_NUMBER(dev,
			init_val_isp_preview_still.dma1_output.buffer_number);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_BUFFER_ADDRESS(dev,
			init_val_isp_preview_still.dma1_output.buffer_address);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_ERR(dev,
			init_val_isp_preview_still.dma1_output.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_DMA1_OUTPUT);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_CMD(dev,
			init_val_isp_preview_still.dma2_output.cmd);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_WIDTH(dev,
			init_val_isp_preview_still.dma2_output.width);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_HEIGHT(dev,
			init_val_isp_preview_still.dma2_output.height);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_FORMAT(dev,
			init_val_isp_preview_still.dma2_output.format);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_BITWIDTH(dev,
			init_val_isp_preview_still.dma2_output.bitwidth);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_PLANE(dev,
			init_val_isp_preview_still.dma2_output.plane);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_ORDER(dev,
			init_val_isp_preview_still.dma2_output.order);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_BUFFER_NUMBER(dev,
			init_val_isp_preview_still.dma2_output.buffer_number);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_BUFFER_ADDRESS(dev,
			init_val_isp_preview_still.dma2_output.buffer_address);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_ERR(dev,
			init_val_isp_preview_still.dma2_output.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_DMA2_OUTPUT);
		IS_INC_PARAM_NUM(dev);

		/* DRC */
		IS_DRC_SET_PARAM_CONTROL_CMD(dev,
			init_val_drc_preview_still.control.cmd);
		IS_DRC_SET_PARAM_CONTROL_BYPASS(dev,
			init_val_drc_preview_still.control.bypass);
		IS_DRC_SET_PARAM_CONTROL_ERR(dev,
			init_val_drc_preview_still.control.err);
		IS_SET_PARAM_BIT(dev, PARAM_DRC_CONTROL);
		IS_INC_PARAM_NUM(dev);
		IS_DRC_SET_PARAM_OTF_INPUT_CMD(dev,
			init_val_drc_preview_still.otf_input.cmd);
		IS_DRC_SET_PARAM_OTF_INPUT_WIDTH(dev,
			init_val_drc_preview_still.otf_input.width);
		IS_DRC_SET_PARAM_OTF_INPUT_HEIGHT(dev,
			init_val_drc_preview_still.otf_input.height);
		IS_DRC_SET_PARAM_OTF_INPUT_FORMAT(dev,
			init_val_drc_preview_still.otf_input.format);
		IS_DRC_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
			init_val_drc_preview_still.otf_input.bitwidth);
		IS_DRC_SET_PARAM_OTF_INPUT_ORDER(dev,
			init_val_drc_preview_still.otf_input.order);
		IS_DRC_SET_PARAM_OTF_INPUT_ERR(dev,
			init_val_drc_preview_still.otf_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_DRC_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_DRC_SET_PARAM_DMA_INPUT_CMD(dev,
			init_val_drc_preview_still.dma_input.cmd);
		IS_DRC_SET_PARAM_DMA_INPUT_WIDTH(dev,
			init_val_drc_preview_still.dma_input.width);
		IS_DRC_SET_PARAM_DMA_INPUT_HEIGHT(dev,
			init_val_drc_preview_still.dma_input.height);
		IS_DRC_SET_PARAM_DMA_INPUT_FORMAT(dev,
			init_val_drc_preview_still.dma_input.format);
		IS_DRC_SET_PARAM_DMA_INPUT_BITWIDTH(dev,
			init_val_drc_preview_still.dma_input.bitwidth);
		IS_DRC_SET_PARAM_DMA_INPUT_PLANE(dev,
			init_val_drc_preview_still.dma_input.plane);
		IS_DRC_SET_PARAM_DMA_INPUT_ORDER(dev,
			init_val_drc_preview_still.dma_input.order);
		IS_DRC_SET_PARAM_DMA_INPUT_BUFFERNUM(dev,
			init_val_drc_preview_still.dma_input.buffer_number);
		IS_DRC_SET_PARAM_DMA_INPUT_BUFFERADDR(dev,
			init_val_drc_preview_still.dma_input.buffer_address);
		IS_DRC_SET_PARAM_DMA_INPUT_ERR(dev,
			init_val_drc_preview_still.dma_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_DRC_DMA_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_DRC_SET_PARAM_OTF_OUTPUT_CMD(dev,
			init_val_drc_preview_still.otf_output.cmd);
		IS_DRC_SET_PARAM_OTF_OUTPUT_WIDTH(dev,
			init_val_drc_preview_still.otf_output.width);
		IS_DRC_SET_PARAM_OTF_OUTPUT_HEIGHT(dev,
			init_val_drc_preview_still.otf_output.height);
		IS_DRC_SET_PARAM_OTF_OUTPUT_FORMAT(dev,
			init_val_drc_preview_still.otf_output.format);
		IS_DRC_SET_PARAM_OTF_OUTPUT_BITWIDTH(dev,
			init_val_drc_preview_still.otf_output.bitwidth);
		IS_DRC_SET_PARAM_OTF_OUTPUT_ORDER(dev,
			init_val_drc_preview_still.otf_output.order);
		IS_DRC_SET_PARAM_OTF_OUTPUT_ERR(dev,
			init_val_drc_preview_still.otf_output.err);
		length = init_val_drc_preview_still.otf_output.width*init_val_drc_preview_still.otf_output.height;
		IS_SET_PARAM_BIT(dev, PARAM_DRC_OTF_OUTPUT);
		IS_INC_PARAM_NUM(dev);

		/* SCALER-C Macros */
		IS_SCALERC_SET_PARAM_CONTROL_CMD(dev,
			init_val_scalerc_preview_still.control.cmd);
		IS_SCALERC_SET_PARAM_CONTROL_BYPASS(dev,
			init_val_scalerc_preview_still.control.bypass);
		IS_SCALERC_SET_PARAM_CONTROL_ERR(dev,
			init_val_scalerc_preview_still.control.err);
		IS_SET_PARAM_BIT(dev, PARAM_SCALERC_CONTROL);
		IS_INC_PARAM_NUM(dev);

		IS_SCALERC_SET_PARAM_OTF_INPUT_CMD(dev,
			init_val_scalerc_preview_still.otf_input.cmd);
		IS_SCALERC_SET_PARAM_OTF_INPUT_WIDTH(dev,
			init_val_scalerc_preview_still.otf_input.width);
		IS_SCALERC_SET_PARAM_OTF_INPUT_HEIGHT(dev,
			init_val_scalerc_preview_still.otf_input.height);
		IS_SCALERC_SET_PARAM_OTF_INPUT_FORMAT(dev,
			init_val_scalerc_preview_still.otf_input.format);
		IS_SCALERC_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
			init_val_scalerc_preview_still.otf_input.bitwidth);
		IS_SCALERC_SET_PARAM_OTF_INPUT_ORDER(dev,
			init_val_scalerc_preview_still.otf_input.order);
		IS_SCALERC_SET_PARAM_OTF_INPUT_ERR(dev,
			init_val_scalerc_preview_still.otf_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_SCALERC_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);

		IS_SCALERC_SET_PARAM_EFFECT_CMD(dev,
			init_val_scalerc_preview_still.effect.cmd);
		IS_SCALERC_SET_PARAM_EFFECT_ERR(dev,
			init_val_scalerc_preview_still.effect.err);
		IS_SET_PARAM_BIT(dev, PARAM_SCALERC_IMAGE_EFFECT);
		IS_INC_PARAM_NUM(dev);

		IS_SCALERC_SET_PARAM_CROP_CMD(dev,
			init_val_scalerc_preview_still.crop.cmd);
		IS_SCALERC_SET_PARAM_CROP_POS_X(dev,
			init_val_scalerc_preview_still.crop.pos_x);
		IS_SCALERC_SET_PARAM_CROP_POS_Y(dev,
			init_val_scalerc_preview_still.crop.pos_y);
		IS_SCALERC_SET_PARAM_CROP_WIDTH(dev,
			init_val_scalerc_preview_still.crop.crop_width);
		IS_SCALERC_SET_PARAM_CROP_HEIGHT(dev,
			init_val_scalerc_preview_still.crop.crop_height);
		IS_SCALERC_SET_PARAM_CROP_ERR(dev,
			init_val_scalerc_preview_still.crop.err);
		IS_SET_PARAM_BIT(dev, PARAM_SCALERC_CROP);
		IS_INC_PARAM_NUM(dev);

		IS_SCALERC_SET_PARAM_SCALING_CMD(dev,
			init_val_scalerc_preview_still.scale.cmd);
		IS_SCALERC_SET_PARAM_SCALING_PRE_H_RATIO(dev,
			init_val_scalerc_preview_still.scale.pre_h_ratio);
		IS_SCALERC_SET_PARAM_SCALING_PRE_V_RATIO(dev,
			init_val_scalerc_preview_still.scale.pre_v_ratio);
		IS_SCALERC_SET_PARAM_SCALING_SH_FACTOR(dev,
			init_val_scalerc_preview_still.scale.sh_factor);
		IS_SCALERC_SET_PARAM_SCALING_H_RATIO(dev,
			init_val_scalerc_preview_still.scale.h_ratio);
		IS_SCALERC_SET_PARAM_SCALING_V_RATIO(dev,
			init_val_scalerc_preview_still.scale.v_ratio);
		IS_SCALERC_SET_PARAM_SCALING_ERR(dev,
			init_val_scalerc_preview_still.scale.err);
		IS_SET_PARAM_BIT(dev, PARAM_SCALERC_SCALING);
		IS_INC_PARAM_NUM(dev);

		IS_SCALERC_SET_PARAM_OTF_OUTPUT_CMD(dev,
			init_val_scalerc_preview_still.otf_output.cmd);
		IS_SCALERC_SET_PARAM_OTF_OUTPUT_WIDTH(dev,
			init_val_scalerc_preview_still.otf_output.width);
		IS_SCALERC_SET_PARAM_OTF_OUTPUT_HEIGHT(dev,
			init_val_scalerc_preview_still.otf_output.height);
		IS_SCALERC_SET_PARAM_OTF_OUTPUT_FORMAT(dev,
			init_val_scalerc_preview_still.otf_output.format);
		IS_SCALERC_SET_PARAM_OTF_OUTPUT_BITWIDTH(dev,
			init_val_scalerc_preview_still.otf_output.bitwidth);
		IS_SCALERC_SET_PARAM_OTF_OUTPUT_ORDER(dev,
			init_val_scalerc_preview_still.otf_output.order);
		IS_SCALERC_SET_PARAM_OTF_OUTPUT_ERR(dev,
			init_val_scalerc_preview_still.otf_output.err);
		IS_SET_PARAM_BIT(dev, PARAM_SCALERC_OTF_OUTPUT);
		IS_INC_PARAM_NUM(dev);

		IS_SCALERC_SET_PARAM_DMA_OUTPUT_CMD(dev,
			init_val_scalerc_preview_still.dma_output.cmd);
		IS_SCALERC_SET_PARAM_DMA_OUTPUT_WIDTH(dev,
			init_val_scalerc_preview_still.dma_output.width);
		IS_SCALERC_SET_PARAM_DMA_OUTPUT_HEIGHT(dev,
			init_val_scalerc_preview_still.dma_output.height);
		IS_SCALERC_SET_PARAM_DMA_OUTPUT_FORMAT(dev,
			init_val_scalerc_preview_still.dma_output.format);
		IS_SCALERC_SET_PARAM_DMA_OUTPUT_BITWIDTH(dev,
			init_val_scalerc_preview_still.dma_output.bitwidth);
		IS_SCALERC_SET_PARAM_DMA_OUTPUT_PLANE(dev,
			init_val_scalerc_preview_still.dma_output.plane);
		IS_SCALERC_SET_PARAM_DMA_OUTPUT_ORDER(dev,
			init_val_scalerc_preview_still.dma_output.order);
		IS_SCALERC_SET_PARAM_DMA_OUTPUT_BUFFERNUM(dev,
			init_val_scalerc_preview_still.dma_output.buffer_number);
		IS_SCALERC_SET_PARAM_DMA_OUTPUT_BUFFERADDR(dev,
			init_val_scalerc_preview_still.dma_output.buffer_address);
		IS_SCALERC_SET_PARAM_DMA_OUTPUT_OUTPATH(dev,
			init_val_scalerc_preview_still.dma_output.reserved[0]);
		IS_SCALERC_SET_PARAM_DMA_OUTPUT_ERR(dev,
			init_val_scalerc_preview_still.dma_output.err);
		IS_SET_PARAM_BIT(dev, PARAM_SCALERC_DMA_OUTPUT);
		IS_INC_PARAM_NUM(dev);

		/* ODC Macros */
		IS_ODC_SET_PARAM_CONTROL_CMD(dev,
			init_val_odc_preview_still.control.cmd);
		IS_ODC_SET_PARAM_CONTROL_BYPASS(dev,
			init_val_odc_preview_still.control.bypass);
		IS_ODC_SET_PARAM_CONTROL_ERR(dev,
			init_val_odc_preview_still.control.err);
		IS_SET_PARAM_BIT(dev, PARAM_ODC_CONTROL);
		IS_INC_PARAM_NUM(dev);

		IS_ODC_SET_PARAM_OTF_INPUT_CMD(dev,
			init_val_odc_preview_still.otf_input.cmd);
		IS_ODC_SET_PARAM_OTF_INPUT_WIDTH(dev,
			init_val_odc_preview_still.otf_input.width);
		IS_ODC_SET_PARAM_OTF_INPUT_HEIGHT(dev,
			init_val_odc_preview_still.otf_input.height);
		IS_ODC_SET_PARAM_OTF_INPUT_FORMAT(dev,
			init_val_odc_preview_still.otf_input.format);
		IS_ODC_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
			init_val_odc_preview_still.otf_input.bitwidth);
		IS_ODC_SET_PARAM_OTF_INPUT_ORDER(dev,
			init_val_odc_preview_still.otf_input.order);
		IS_ODC_SET_PARAM_OTF_INPUT_ERR(dev,
			init_val_odc_preview_still.otf_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_ODC_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);

		IS_ODC_SET_PARAM_OTF_OUTPUT_CMD(dev,
			init_val_odc_preview_still.otf_output.cmd);
		IS_ODC_SET_PARAM_OTF_OUTPUT_WIDTH(dev,
			init_val_odc_preview_still.otf_output.width);
		IS_ODC_SET_PARAM_OTF_OUTPUT_HEIGHT(dev,
			init_val_odc_preview_still.otf_output.height);
		IS_ODC_SET_PARAM_OTF_OUTPUT_FORMAT(dev,
			init_val_odc_preview_still.otf_output.format);
		IS_ODC_SET_PARAM_OTF_OUTPUT_BITWIDTH(dev,
			init_val_odc_preview_still.otf_output.bitwidth);
		IS_ODC_SET_PARAM_OTF_OUTPUT_ORDER(dev,
			init_val_odc_preview_still.otf_output.order);
		IS_ODC_SET_PARAM_OTF_OUTPUT_ERR(dev,
			init_val_odc_preview_still.otf_output.err);
		IS_SET_PARAM_BIT(dev, PARAM_ODC_OTF_OUTPUT);
		IS_INC_PARAM_NUM(dev);

		/* DIS Macros */
		IS_DIS_SET_PARAM_CONTROL_CMD(dev,
			init_val_dis_preview_still.control.cmd);
		IS_DIS_SET_PARAM_CONTROL_BYPASS(dev,
			init_val_dis_preview_still.control.bypass);
		IS_DIS_SET_PARAM_CONTROL_ERR(dev,
			init_val_dis_preview_still.control.err);
		IS_SET_PARAM_BIT(dev, PARAM_DIS_CONTROL);
		IS_INC_PARAM_NUM(dev);

		IS_DIS_SET_PARAM_OTF_INPUT_CMD(dev,
			init_val_dis_preview_still.otf_input.cmd);
		IS_DIS_SET_PARAM_OTF_INPUT_WIDTH(dev,
			init_val_dis_preview_still.otf_input.width);
		IS_DIS_SET_PARAM_OTF_INPUT_HEIGHT(dev,
			init_val_dis_preview_still.otf_input.height);
		IS_DIS_SET_PARAM_OTF_INPUT_FORMAT(dev,
			init_val_dis_preview_still.otf_input.format);
		IS_DIS_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
			init_val_dis_preview_still.otf_input.bitwidth);
		IS_DIS_SET_PARAM_OTF_INPUT_ORDER(dev,
			init_val_dis_preview_still.otf_input.order);
		IS_DIS_SET_PARAM_OTF_INPUT_ERR(dev,
			init_val_dis_preview_still.otf_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_DIS_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);

		IS_DIS_SET_PARAM_OTF_OUTPUT_CMD(dev,
			init_val_dis_preview_still.otf_output.cmd);
		IS_DIS_SET_PARAM_OTF_OUTPUT_WIDTH(dev,
			init_val_dis_preview_still.otf_output.width);
		IS_DIS_SET_PARAM_OTF_OUTPUT_HEIGHT(dev,
			init_val_dis_preview_still.otf_output.height);
		IS_DIS_SET_PARAM_OTF_OUTPUT_FORMAT(dev,
			init_val_dis_preview_still.otf_output.format);
		IS_DIS_SET_PARAM_OTF_OUTPUT_BITWIDTH(dev,
			init_val_dis_preview_still.otf_output.bitwidth);
		IS_DIS_SET_PARAM_OTF_OUTPUT_ORDER(dev,
			init_val_dis_preview_still.otf_output.order);
		IS_DIS_SET_PARAM_OTF_OUTPUT_ERR(dev,
			init_val_dis_preview_still.otf_output.err);
		IS_SET_PARAM_BIT(dev, PARAM_DIS_OTF_OUTPUT);
		IS_INC_PARAM_NUM(dev);

		/* TDNR Macros */
		IS_TDNR_SET_PARAM_CONTROL_CMD(dev,
			init_val_tdnr_preview_still.control.cmd);
		IS_TDNR_SET_PARAM_CONTROL_BYPASS(dev,
			init_val_tdnr_preview_still.control.bypass);
		IS_TDNR_SET_PARAM_CONTROL_ERR(dev,
			init_val_tdnr_preview_still.control.err);
		IS_SET_PARAM_BIT(dev, PARAM_TDNR_CONTROL);
		IS_INC_PARAM_NUM(dev);

		IS_TDNR_SET_PARAM_OTF_INPUT_CMD(dev,
			init_val_tdnr_preview_still.otf_input.cmd);
		IS_TDNR_SET_PARAM_OTF_INPUT_WIDTH(dev,
			init_val_tdnr_preview_still.otf_input.width);
		IS_TDNR_SET_PARAM_OTF_INPUT_HEIGHT(dev,
			init_val_tdnr_preview_still.otf_input.height);
		IS_TDNR_SET_PARAM_OTF_INPUT_FORMAT(dev,
			init_val_tdnr_preview_still.otf_input.format);
		IS_TDNR_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
			init_val_tdnr_preview_still.otf_input.bitwidth);
		IS_TDNR_SET_PARAM_OTF_INPUT_ORDER(dev,
			init_val_tdnr_preview_still.otf_input.order);
		IS_TDNR_SET_PARAM_OTF_INPUT_ERR(dev,
			init_val_tdnr_preview_still.otf_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_TDNR_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);

		IS_TDNR_SET_PARAM_FRAME_CMD(dev,
			init_val_tdnr_preview_still.frame.cmd);
		IS_TDNR_SET_PARAM_FRAME_ERR(dev,
			init_val_tdnr_preview_still.frame.err);
		IS_SET_PARAM_BIT(dev, PARAM_TDNR_1ST_FRAME);
		IS_INC_PARAM_NUM(dev);

		IS_TDNR_SET_PARAM_OTF_OUTPUT_CMD(dev,
			init_val_tdnr_preview_still.otf_output.cmd);
		IS_TDNR_SET_PARAM_OTF_OUTPUT_WIDTH(dev,
			init_val_tdnr_preview_still.otf_output.width);
		IS_TDNR_SET_PARAM_OTF_OUTPUT_HEIGHT(dev,
			init_val_tdnr_preview_still.otf_output.height);
		IS_TDNR_SET_PARAM_OTF_OUTPUT_FORMAT(dev,
			init_val_tdnr_preview_still.otf_output.format);
		IS_TDNR_SET_PARAM_OTF_OUTPUT_BITWIDTH(dev,
			init_val_tdnr_preview_still.otf_output.bitwidth);
		IS_TDNR_SET_PARAM_OTF_OUTPUT_ORDER(dev,
			init_val_tdnr_preview_still.otf_output.order);
		IS_TDNR_SET_PARAM_OTF_OUTPUT_ERR(dev,
			init_val_tdnr_preview_still.otf_output.err);
		IS_SET_PARAM_BIT(dev, PARAM_TDNR_OTF_OUTPUT);
		IS_INC_PARAM_NUM(dev);

		IS_TDNR_SET_PARAM_DMA_OUTPUT_CMD(dev,
			init_val_tdnr_preview_still.dma_output.cmd);
		IS_TDNR_SET_PARAM_DMA_OUTPUT_WIDTH(dev,
			init_val_tdnr_preview_still.dma_output.width);
		IS_TDNR_SET_PARAM_DMA_OUTPUT_HEIGHT(dev,
			init_val_tdnr_preview_still.dma_output.height);
		IS_TDNR_SET_PARAM_DMA_OUTPUT_FORMAT(dev,
			init_val_tdnr_preview_still.dma_output.format);
		IS_TDNR_SET_PARAM_DMA_OUTPUT_BITWIDTH(dev,
			init_val_tdnr_preview_still.dma_output.bitwidth);
		IS_TDNR_SET_PARAM_DMA_OUTPUT_PLANE(dev,
			init_val_tdnr_preview_still.dma_output.plane);
		IS_TDNR_SET_PARAM_DMA_OUTPUT_ORDER(dev,
			init_val_tdnr_preview_still.dma_output.order);
		IS_TDNR_SET_PARAM_DMA_OUTPUT_BUFFERNUM(dev,
			init_val_tdnr_preview_still.dma_output.buffer_number);
		IS_TDNR_SET_PARAM_DMA_OUTPUT_BUFFERADDR(dev,
			init_val_tdnr_preview_still.dma_output.buffer_address);
		IS_TDNR_SET_PARAM_DMA_OUTPUT_ERR(dev,
			init_val_tdnr_preview_still.dma_output.err);
		IS_SET_PARAM_BIT(dev, PARAM_TDNR_DMA_OUTPUT);
		IS_INC_PARAM_NUM(dev);

		/* SCALER-P Macros */
		IS_SCALERP_SET_PARAM_CONTROL_CMD(dev,
			init_val_scalerp_preview_still.control.cmd);
		IS_SCALERP_SET_PARAM_CONTROL_BYPASS(dev,
			init_val_scalerp_preview_still.control.bypass);
		IS_SCALERP_SET_PARAM_CONTROL_ERR(dev,
			init_val_scalerp_preview_still.control.err);
		IS_SET_PARAM_BIT(dev, PARAM_SCALERP_CONTROL);
		IS_INC_PARAM_NUM(dev);

		IS_SCALERP_SET_PARAM_OTF_INPUT_CMD(dev,
			init_val_scalerp_preview_still.otf_input.cmd);
		IS_SCALERP_SET_PARAM_OTF_INPUT_WIDTH(dev,
			init_val_scalerp_preview_still.otf_input.width);
		IS_SCALERP_SET_PARAM_OTF_INPUT_HEIGHT(dev,
			init_val_scalerp_preview_still.otf_input.height);
		IS_SCALERP_SET_PARAM_OTF_INPUT_FORMAT(dev,
			init_val_scalerp_preview_still.otf_input.format);
		IS_SCALERP_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
			init_val_scalerp_preview_still.otf_input.bitwidth);
		IS_SCALERP_SET_PARAM_OTF_INPUT_ORDER(dev,
			init_val_scalerp_preview_still.otf_input.order);
		IS_SCALERP_SET_PARAM_OTF_INPUT_ERR(dev,
			init_val_scalerp_preview_still.otf_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_SCALERP_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);

		IS_SCALERP_SET_PARAM_EFFECT_CMD(dev,
			init_val_scalerp_preview_still.effect.cmd);
		IS_SCALERP_SET_PARAM_EFFECT_ERR(dev,
			init_val_scalerp_preview_still.effect.err);
		IS_SET_PARAM_BIT(dev, PARAM_SCALERP_IMAGE_EFFECT);
		IS_INC_PARAM_NUM(dev);

		IS_SCALERP_SET_PARAM_CROP_CMD(dev,
			init_val_scalerp_preview_still.crop.cmd);
		IS_SCALERP_SET_PARAM_CROP_POS_X(dev,
			init_val_scalerp_preview_still.crop.pos_x);
		IS_SCALERP_SET_PARAM_CROP_POS_Y(dev,
			init_val_scalerp_preview_still.crop.pos_y);
		IS_SCALERP_SET_PARAM_CROP_WIDTH(dev,
			init_val_scalerp_preview_still.crop.crop_width);
		IS_SCALERP_SET_PARAM_CROP_HEIGHT(dev,
			init_val_scalerp_preview_still.crop.crop_height);
		IS_SCALERP_SET_PARAM_CROP_ERR(dev,
			init_val_scalerp_preview_still.crop.err);
		IS_SET_PARAM_BIT(dev, PARAM_SCALERP_CROP);
		IS_INC_PARAM_NUM(dev);

		IS_SCALERP_SET_PARAM_SCALING_CMD(dev,
			init_val_scalerp_preview_still.scale.cmd);
		IS_SCALERP_SET_PARAM_SCALING_PRE_H_RATIO(dev,
			init_val_scalerp_preview_still.scale.pre_h_ratio);
		IS_SCALERP_SET_PARAM_SCALING_PRE_V_RATIO(dev,
			init_val_scalerp_preview_still.scale.pre_v_ratio);
		IS_SCALERP_SET_PARAM_SCALING_SH_FACTOR(dev,
			init_val_scalerp_preview_still.scale.sh_factor);
		IS_SCALERP_SET_PARAM_SCALING_H_RATIO(dev,
			init_val_scalerp_preview_still.scale.h_ratio);
		IS_SCALERP_SET_PARAM_SCALING_V_RATIO(dev,
			init_val_scalerp_preview_still.scale.v_ratio);
		IS_SCALERP_SET_PARAM_SCALING_ERR(dev,
			init_val_scalerp_preview_still.scale.err);
		IS_SET_PARAM_BIT(dev, PARAM_SCALERP_SCALING);
		IS_INC_PARAM_NUM(dev);

		IS_SCALERP_SET_PARAM_ROTATION_CMD(dev,
			init_val_scalerp_preview_still.rotation.cmd);
		IS_SCALERP_SET_PARAM_ROTATION_ERR(dev,
			init_val_scalerp_preview_still.rotation.err);
		IS_SET_PARAM_BIT(dev, PARAM_SCALERP_ROTATION);
		IS_INC_PARAM_NUM(dev);

		IS_SCALERP_SET_PARAM_FLIP_CMD(dev,
			init_val_scalerp_preview_still.flip.cmd);
		IS_SCALERP_SET_PARAM_FLIP_ERR(dev,
			init_val_scalerp_preview_still.flip.err);
		IS_SET_PARAM_BIT(dev, PARAM_SCALERP_FLIP);
		IS_INC_PARAM_NUM(dev);

		IS_SCALERP_SET_PARAM_OTF_OUTPUT_CMD(dev,
			init_val_scalerp_preview_still.otf_output.cmd);
		IS_SCALERP_SET_PARAM_OTF_OUTPUT_WIDTH(dev,
			init_val_scalerp_preview_still.otf_output.width);
		IS_SCALERP_SET_PARAM_OTF_OUTPUT_HEIGHT(dev,
			init_val_scalerp_preview_still.otf_output.height);
		IS_SCALERP_SET_PARAM_OTF_OUTPUT_FORMAT(dev,
			init_val_scalerp_preview_still.otf_output.format);
		IS_SCALERP_SET_PARAM_OTF_OUTPUT_BITWIDTH(dev,
			init_val_scalerp_preview_still.otf_output.bitwidth);
		IS_SCALERP_SET_PARAM_OTF_OUTPUT_ORDER(dev,
			init_val_scalerp_preview_still.otf_output.order);
		IS_SCALERP_SET_PARAM_OTF_OUTPUT_ERR(dev,
			init_val_scalerp_preview_still.otf_output.err);
		IS_SET_PARAM_BIT(dev, PARAM_SCALERP_OTF_OUTPUT);
		IS_INC_PARAM_NUM(dev);

		IS_SCALERP_SET_PARAM_DMA_OUTPUT_CMD(dev,
			init_val_scalerp_preview_still.dma_output.cmd);
		IS_SCALERP_SET_PARAM_DMA_OUTPUT_WIDTH(dev,
			init_val_scalerp_preview_still.dma_output.width);
		IS_SCALERP_SET_PARAM_DMA_OUTPUT_HEIGHT(dev,
			init_val_scalerp_preview_still.dma_output.height);
		IS_SCALERP_SET_PARAM_DMA_OUTPUT_FORMAT(dev,
			init_val_scalerp_preview_still.dma_output.format);
		IS_SCALERP_SET_PARAM_DMA_OUTPUT_BITWIDTH(dev,
			init_val_scalerp_preview_still.dma_output.bitwidth);
		IS_SCALERP_SET_PARAM_DMA_OUTPUT_PLANE(dev,
			init_val_scalerp_preview_still.dma_output.plane);
		IS_SCALERP_SET_PARAM_DMA_OUTPUT_ORDER(dev,
			init_val_scalerp_preview_still.dma_output.order);
		IS_SCALERP_SET_PARAM_DMA_OUTPUT_BUFFERNUM(dev,
			init_val_scalerp_preview_still.dma_output.buffer_number);
		IS_SCALERP_SET_PARAM_DMA_OUTPUT_BUFFERADDR(dev,
			init_val_scalerp_preview_still.dma_output.buffer_address);
		IS_SCALERP_SET_PARAM_DMA_OUTPUT_ERR(dev,
			init_val_scalerp_preview_still.dma_output.err);
		IS_SET_PARAM_BIT(dev, PARAM_SCALERP_DMA_OUTPUT);
		IS_INC_PARAM_NUM(dev);

		/* FD */
		IS_FD_SET_PARAM_CONTROL_CMD(dev,
			init_val_fd_preview_still.control.cmd);
		IS_FD_SET_PARAM_CONTROL_BYPASS(dev,
			init_val_fd_preview_still.control.bypass);
		IS_FD_SET_PARAM_CONTROL_ERR(dev,
			init_val_fd_preview_still.control.err);
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONTROL);
		IS_INC_PARAM_NUM(dev);
		IS_FD_SET_PARAM_OTF_INPUT_CMD(dev,
			init_val_fd_preview_still.otf_input.cmd);
		IS_FD_SET_PARAM_OTF_INPUT_WIDTH(dev,
			init_val_fd_preview_still.otf_input.width);
		IS_FD_SET_PARAM_OTF_INPUT_HEIGHT(dev,
			init_val_fd_preview_still.otf_input.height);
		IS_FD_SET_PARAM_OTF_INPUT_FORMAT(dev,
			init_val_fd_preview_still.otf_input.format);
		IS_FD_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
			init_val_fd_preview_still.otf_input.bitwidth);
		IS_FD_SET_PARAM_OTF_INPUT_ORDER(dev,
			init_val_fd_preview_still.otf_input.order);
		IS_FD_SET_PARAM_OTF_INPUT_ERR(dev,
			init_val_fd_preview_still.otf_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_FD_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_FD_SET_PARAM_DMA_INPUT_CMD(dev,
			init_val_fd_preview_still.dma_input.cmd);
		IS_FD_SET_PARAM_DMA_INPUT_WIDTH(dev,
			init_val_fd_preview_still.dma_input.width);
		IS_FD_SET_PARAM_DMA_INPUT_HEIGHT(dev,
			init_val_fd_preview_still.dma_input.height);
		IS_FD_SET_PARAM_DMA_INPUT_FORMAT(dev,
			init_val_fd_preview_still.dma_input.format);
		IS_FD_SET_PARAM_DMA_INPUT_BITWIDTH(dev,
			init_val_fd_preview_still.dma_input.bitwidth);
		IS_FD_SET_PARAM_DMA_INPUT_PLANE(dev,
			init_val_fd_preview_still.dma_input.plane);
		IS_FD_SET_PARAM_DMA_INPUT_ORDER(dev,
			init_val_fd_preview_still.dma_input.order);
		IS_FD_SET_PARAM_DMA_INPUT_BUFFERNUM(dev,
			init_val_fd_preview_still.dma_input.buffer_number);
		IS_FD_SET_PARAM_DMA_INPUT_BUFFERADDR(dev,
			init_val_fd_preview_still.dma_input.buffer_address);
		IS_FD_SET_PARAM_DMA_INPUT_ERR(dev,
			init_val_fd_preview_still.dma_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_FD_DMA_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_FD_SET_PARAM_FD_CONFIG_CMD(dev,
			init_val_fd_preview_still.config.cmd);
		IS_FD_SET_PARAM_FD_CONFIG_MAX_NUMBER(dev,
			init_val_fd_preview_still.config.max_number);
		IS_FD_SET_PARAM_FD_CONFIG_ROLL_ANGLE(dev,
			init_val_fd_preview_still.config.roll_angle);
		IS_FD_SET_PARAM_FD_CONFIG_YAW_ANGLE(dev,
			init_val_fd_preview_still.config.yaw_angle);
		IS_FD_SET_PARAM_FD_CONFIG_SMILE_MODE(dev,
			init_val_fd_preview_still.config.smile_mode);
		IS_FD_SET_PARAM_FD_CONFIG_BLINK_MODE(dev,
			init_val_fd_preview_still.config.blink_mode);
		IS_FD_SET_PARAM_FD_CONFIG_EYE_DETECT(dev,
			init_val_fd_preview_still.config.eye_detect);
		IS_FD_SET_PARAM_FD_CONFIG_MOUTH_DETECT(dev,
			init_val_fd_preview_still.config.mouth_detect);
		IS_FD_SET_PARAM_FD_CONFIG_ORIENTATION(dev,
			init_val_fd_preview_still.config.orientation);
		IS_FD_SET_PARAM_FD_CONFIG_ORIENTATION_VALUE(dev,
			init_val_fd_preview_still.config.orientation_value);
		IS_FD_SET_PARAM_FD_CONFIG_ERR(dev,
			init_val_fd_preview_still.config.err);
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONFIG);
		IS_INC_PARAM_NUM(dev);
		break;
	case ISS_PREVIEW_VIDEO:
		IS_SET_PARAM_GLOBAL_SHOTMODE_CMD(dev, 1);
		IS_SET_PARAM_BIT(dev, PARAM_GLOBAL_SHOTMODE);
		IS_INC_PARAM_NUM(dev);
		IS_SENSOR_SET_FRAME_RATE(dev, DEFAULT_PREVIEW_VIDEO_FRAMERATE);
		IS_SET_PARAM_BIT(dev, PARAM_SENSOR_FRAME_RATE);
		IS_INC_PARAM_NUM(dev);
		/* ISP */
		IS_ISP_SET_PARAM_CONTROL_CMD(dev,
			init_val_isp_preview_video.control.cmd);
		IS_ISP_SET_PARAM_CONTROL_BYPASS(dev,
			init_val_isp_preview_video.control.bypass);
		IS_ISP_SET_PARAM_CONTROL_ERR(dev,
			init_val_isp_preview_video.control.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_OTF_INPUT_CMD(dev,
			init_val_isp_preview_video.otf_input.cmd);
		IS_ISP_SET_PARAM_OTF_INPUT_WIDTH(dev,
			init_val_isp_preview_video.otf_input.width);
		IS_ISP_SET_PARAM_OTF_INPUT_HEIGHT(dev,
			init_val_isp_preview_video.otf_input.height);
		dev->sensor.width_prev_cam =
			init_val_isp_preview_video.otf_input.width;
		dev->sensor.height_prev_cam =
			init_val_isp_preview_video.otf_input.height;
		IS_ISP_SET_PARAM_OTF_INPUT_FORMAT(dev,
			init_val_isp_preview_video.otf_input.format);
		IS_ISP_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
			init_val_isp_preview_video.otf_input.bitwidth);
		IS_ISP_SET_PARAM_OTF_INPUT_ORDER(dev,
			init_val_isp_preview_video.otf_input.order);
		IS_ISP_SET_PARAM_OTF_INPUT_ERR(dev,
			init_val_isp_preview_video.otf_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_DMA_INPUT1_CMD(dev,
			init_val_isp_preview_video.dma1_input.cmd);
		IS_ISP_SET_PARAM_DMA_INPUT1_WIDTH(dev,
			init_val_isp_preview_video.dma1_input.width);
		IS_ISP_SET_PARAM_DMA_INPUT1_HEIGHT(dev,
			init_val_isp_preview_video.dma1_input.height);
		IS_ISP_SET_PARAM_DMA_INPUT1_FORMAT(dev,
			init_val_isp_preview_video.dma1_input.format);
		IS_ISP_SET_PARAM_DMA_INPUT1_BITWIDTH(dev,
			init_val_isp_preview_video.dma1_input.bitwidth);
		IS_ISP_SET_PARAM_DMA_INPUT1_PLANE(dev,
			init_val_isp_preview_video.dma1_input.plane);
		IS_ISP_SET_PARAM_DMA_INPUT1_ORDER(dev,
			init_val_isp_preview_video.dma1_input.order);
		IS_ISP_SET_PARAM_DMA_INPUT1_BUFFERNUM(dev,
			init_val_isp_preview_video.dma1_input.buffer_number);
		IS_ISP_SET_PARAM_DMA_INPUT1_BUFFERADDR(dev,
			init_val_isp_preview_video.dma1_input.buffer_address);
		IS_ISP_SET_PARAM_DMA_INPUT1_ERR(dev,
			init_val_isp_preview_video.dma1_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_DMA1_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_DMA_INPUT2_CMD(dev,
			init_val_isp_preview_video.dma2_input.cmd);
		IS_ISP_SET_PARAM_DMA_INPUT2_WIDTH(dev,
			init_val_isp_preview_video.dma2_input.width);
		IS_ISP_SET_PARAM_DMA_INPUT2_HEIGHT(dev,
			init_val_isp_preview_video.dma2_input.height);
		IS_ISP_SET_PARAM_DMA_INPUT2_FORMAT(dev,
			init_val_isp_preview_video.dma2_input.format);
		IS_ISP_SET_PARAM_DMA_INPUT2_BITWIDTH(dev,
			init_val_isp_preview_video.dma2_input.bitwidth);
		IS_ISP_SET_PARAM_DMA_INPUT2_PLANE(dev,
			init_val_isp_preview_video.dma2_input.plane);
		IS_ISP_SET_PARAM_DMA_INPUT2_ORDER(dev,
			init_val_isp_preview_video.dma2_input.order);
		IS_ISP_SET_PARAM_DMA_INPUT2_BUFFERNUM(dev,
			init_val_isp_preview_video.dma2_input.buffer_number);
		IS_ISP_SET_PARAM_DMA_INPUT2_BUFFERADDR(dev,
			init_val_isp_preview_video.dma2_input.buffer_address);
		IS_ISP_SET_PARAM_DMA_INPUT2_ERR(dev,
			init_val_isp_preview_video.dma2_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_DMA2_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_AA_CMD(dev,
			init_val_isp_preview_video.aa.cmd);
		IS_ISP_SET_PARAM_AA_TARGET(dev,
			init_val_isp_preview_video.aa.target);
		IS_ISP_SET_PARAM_AA_MODE(dev,
			init_val_isp_preview_video.aa.mode);
		IS_ISP_SET_PARAM_AA_FACE(dev,
			init_val_isp_preview_video.aa.face);
		IS_ISP_SET_PARAM_AA_WIN_POS_X(dev,
			init_val_isp_preview_video.aa.win_pos_x);
		IS_ISP_SET_PARAM_AA_WIN_POS_Y(dev,
			init_val_isp_preview_video.aa.win_pos_y);
		IS_ISP_SET_PARAM_AA_ERR(dev,
			init_val_isp_preview_video.aa.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_FLASH_CMD(dev,
			init_val_isp_preview_video.flash.cmd);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev,
			init_val_isp_preview_video.flash.redeye);
		IS_ISP_SET_PARAM_FLASH_ERR(dev,
			init_val_isp_preview_video.flash.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_AWB_CMD(dev,
			init_val_isp_preview_video.awb.cmd);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
			init_val_isp_preview_video.awb.illumination);
		IS_ISP_SET_PARAM_AWB_ERR(dev,
			init_val_isp_preview_video.awb.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_EFFECT_CMD(dev,
			init_val_isp_preview_video.effect.cmd);
		IS_ISP_SET_PARAM_EFFECT_ERR(dev,
			init_val_isp_preview_video.effect.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_IMAGE_EFFECT);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_ISO_CMD(dev,
			init_val_isp_preview_video.iso.cmd);
		IS_ISP_SET_PARAM_ISO_VALUE(dev,
			init_val_isp_preview_video.iso.value);
		IS_ISP_SET_PARAM_ISO_ERR(dev,
			init_val_isp_preview_video.iso.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
			init_val_isp_preview_video.adjust.cmd);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev,
			init_val_isp_preview_video.adjust.contrast);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev,
			init_val_isp_preview_video.adjust.saturation);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev,
			init_val_isp_preview_video.adjust.sharpness);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev,
			init_val_isp_preview_video.adjust.exposure);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev,
			init_val_isp_preview_video.adjust.brightness);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev,
			init_val_isp_preview_video.adjust.hue);
		IS_ISP_SET_PARAM_ADJUST_SHUTTER_TIME_MIN(dev,
			init_val_isp_preview_video.adjust.shutter_time_min);
		IS_ISP_SET_PARAM_ADJUST_SHUTTER_TIME_MAX(dev,
			init_val_isp_preview_video.adjust.shutter_time_max);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev,
			init_val_isp_preview_video.adjust.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_METERING_CMD(dev,
			init_val_isp_preview_video.metering.cmd);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev,
			init_val_isp_preview_video.metering.win_pos_x);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev,
			init_val_isp_preview_video.metering.win_pos_y);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev,
			init_val_isp_preview_video.metering.win_width);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev,
			init_val_isp_preview_video.metering.win_height);
		IS_ISP_SET_PARAM_METERING_ERR(dev,
			init_val_isp_preview_video.metering.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_AFC_CMD(dev,
			init_val_isp_preview_video.afc.cmd);
		IS_ISP_SET_PARAM_AFC_MANUAL(dev,
			init_val_isp_preview_video.afc.manual);
		IS_ISP_SET_PARAM_AFC_ERR(dev,
			init_val_isp_preview_video.afc.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AFC);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_OTF_OUTPUT_CMD(dev,
			init_val_isp_preview_video.otf_output.cmd);
		IS_ISP_SET_PARAM_OTF_OUTPUT_WIDTH(dev,
			init_val_isp_preview_video.otf_output.width);
		IS_ISP_SET_PARAM_OTF_OUTPUT_HEIGHT(dev,
			init_val_isp_preview_video.otf_output.height);
		IS_ISP_SET_PARAM_OTF_OUTPUT_FORMAT(dev,
			init_val_isp_preview_video.otf_output.format);
		IS_ISP_SET_PARAM_OTF_OUTPUT_BITWIDTH(dev,
			init_val_isp_preview_video.otf_output.bitwidth);
		IS_ISP_SET_PARAM_OTF_OUTPUT_ORDER(dev,
			init_val_isp_preview_video.otf_output.order);
		IS_ISP_SET_PARAM_OTF_OUTPUT_ERR(dev,
			init_val_isp_preview_video.otf_output.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_OUTPUT);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_CMD(dev,
			init_val_isp_preview_video.dma1_output.cmd);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_WIDTH(dev,
			init_val_isp_preview_video.dma1_output.width);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_HEIGHT(dev,
			init_val_isp_preview_video.dma1_output.height);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_FORMAT(dev,
			init_val_isp_preview_video.dma1_output.format);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_BITWIDTH(dev,
			init_val_isp_preview_video.dma1_output.bitwidth);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_PLANE(dev,
			init_val_isp_preview_video.dma1_output.plane);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_ORDER(dev,
			init_val_isp_preview_video.dma1_output.order);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_BUFFER_NUMBER(dev,
			init_val_isp_preview_video.dma1_output.buffer_number);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_BUFFER_ADDRESS(dev,
			init_val_isp_preview_video.dma1_output.buffer_address);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_ERR(dev,
			init_val_isp_preview_video.dma1_output.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_DMA1_OUTPUT);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_CMD(dev,
			init_val_isp_preview_video.dma2_output.cmd);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_WIDTH(dev,
			init_val_isp_preview_video.dma2_output.width);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_HEIGHT(dev,
			init_val_isp_preview_video.dma2_output.height);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_FORMAT(dev,
			init_val_isp_preview_video.dma2_output.format);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_BITWIDTH(dev,
			init_val_isp_preview_video.dma2_output.bitwidth);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_PLANE(dev,
			init_val_isp_preview_video.dma2_output.plane);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_ORDER(dev,
			init_val_isp_preview_video.dma2_output.order);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_BUFFER_NUMBER(dev,
			init_val_isp_preview_video.dma2_output.buffer_number);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_BUFFER_ADDRESS(dev,
			init_val_isp_preview_video.dma2_output.buffer_address);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_ERR(dev,
			init_val_isp_preview_video.dma2_output.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_DMA2_OUTPUT);
		IS_INC_PARAM_NUM(dev);

		/* DRC */
		IS_DRC_SET_PARAM_CONTROL_CMD(dev,
			init_val_drc_preview_video.control.cmd);
		IS_DRC_SET_PARAM_CONTROL_BYPASS(dev,
			init_val_drc_preview_video.control.bypass);
		IS_DRC_SET_PARAM_CONTROL_ERR(dev,
			init_val_drc_preview_video.control.err);
		IS_SET_PARAM_BIT(dev, PARAM_DRC_CONTROL);
		IS_INC_PARAM_NUM(dev);
		IS_DRC_SET_PARAM_OTF_INPUT_CMD(dev,
			init_val_drc_preview_video.otf_input.cmd);
		IS_DRC_SET_PARAM_OTF_INPUT_WIDTH(dev,
			init_val_drc_preview_video.otf_input.width);
		IS_DRC_SET_PARAM_OTF_INPUT_HEIGHT(dev,
			init_val_drc_preview_video.otf_input.height);
		IS_DRC_SET_PARAM_OTF_INPUT_FORMAT(dev,
			init_val_drc_preview_video.otf_input.format);
		IS_DRC_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
			init_val_drc_preview_video.otf_input.bitwidth);
		IS_DRC_SET_PARAM_OTF_INPUT_ORDER(dev,
			init_val_drc_preview_video.otf_input.order);
		IS_DRC_SET_PARAM_OTF_INPUT_ERR(dev,
			init_val_drc_preview_video.otf_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_DRC_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_DRC_SET_PARAM_DMA_INPUT_CMD(dev,
			init_val_drc_preview_video.dma_input.cmd);
		IS_DRC_SET_PARAM_DMA_INPUT_WIDTH(dev,
			init_val_drc_preview_video.dma_input.width);
		IS_DRC_SET_PARAM_DMA_INPUT_HEIGHT(dev,
			init_val_drc_preview_video.dma_input.height);
		IS_DRC_SET_PARAM_DMA_INPUT_FORMAT(dev,
			init_val_drc_preview_video.dma_input.format);
		IS_DRC_SET_PARAM_DMA_INPUT_BITWIDTH(dev,
			init_val_drc_preview_video.dma_input.bitwidth);
		IS_DRC_SET_PARAM_DMA_INPUT_PLANE(dev,
			init_val_drc_preview_video.dma_input.plane);
		IS_DRC_SET_PARAM_DMA_INPUT_ORDER(dev,
			init_val_drc_preview_video.dma_input.order);
		IS_DRC_SET_PARAM_DMA_INPUT_BUFFERNUM(dev,
			init_val_drc_preview_video.dma_input.buffer_number);
		IS_DRC_SET_PARAM_DMA_INPUT_BUFFERADDR(dev,
			init_val_drc_preview_video.dma_input.buffer_address);
		IS_DRC_SET_PARAM_DMA_INPUT_ERR(dev,
			init_val_drc_preview_video.dma_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_DRC_DMA_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_DRC_SET_PARAM_OTF_OUTPUT_CMD(dev,
			init_val_drc_preview_video.otf_output.cmd);
		IS_DRC_SET_PARAM_OTF_OUTPUT_WIDTH(dev,
			init_val_drc_preview_video.otf_output.width);
		IS_DRC_SET_PARAM_OTF_OUTPUT_HEIGHT(dev,
			init_val_drc_preview_video.otf_output.height);
		IS_DRC_SET_PARAM_OTF_OUTPUT_FORMAT(dev,
			init_val_drc_preview_video.otf_output.format);
		IS_DRC_SET_PARAM_OTF_OUTPUT_BITWIDTH(dev,
			init_val_drc_preview_video.otf_output.bitwidth);
		IS_DRC_SET_PARAM_OTF_OUTPUT_ORDER(dev,
			init_val_drc_preview_video.otf_output.order);
		IS_DRC_SET_PARAM_OTF_OUTPUT_ERR(dev,
			init_val_drc_preview_video.otf_output.err);
		IS_SET_PARAM_BIT(dev, PARAM_DRC_OTF_OUTPUT);
		IS_INC_PARAM_NUM(dev);

		/* FD */
		IS_FD_SET_PARAM_CONTROL_CMD(dev,
			init_val_fd_preview_video.control.cmd);
		IS_FD_SET_PARAM_CONTROL_BYPASS(dev,
			init_val_fd_preview_video.control.bypass);
		IS_FD_SET_PARAM_CONTROL_ERR(dev,
			init_val_fd_preview_video.control.err);
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONTROL);
		IS_INC_PARAM_NUM(dev);
		IS_FD_SET_PARAM_OTF_INPUT_CMD(dev,
			init_val_fd_preview_video.otf_input.cmd);
		IS_FD_SET_PARAM_OTF_INPUT_WIDTH(dev,
			init_val_fd_preview_video.otf_input.width);
		IS_FD_SET_PARAM_OTF_INPUT_HEIGHT(dev,
			init_val_fd_preview_video.otf_input.height);
		IS_FD_SET_PARAM_OTF_INPUT_FORMAT(dev,
			init_val_fd_preview_video.otf_input.format);
		IS_FD_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
			init_val_fd_preview_video.otf_input.bitwidth);
		IS_FD_SET_PARAM_OTF_INPUT_ORDER(dev,
			init_val_fd_preview_video.otf_input.order);
		IS_FD_SET_PARAM_OTF_INPUT_ERR(dev,
			init_val_fd_preview_video.otf_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_FD_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_FD_SET_PARAM_DMA_INPUT_CMD(dev,
			init_val_fd_preview_video.dma_input.cmd);
		IS_FD_SET_PARAM_DMA_INPUT_WIDTH(dev,
			init_val_fd_preview_video.dma_input.width);
		IS_FD_SET_PARAM_DMA_INPUT_HEIGHT(dev,
			init_val_fd_preview_video.dma_input.height);
		IS_FD_SET_PARAM_DMA_INPUT_FORMAT(dev,
			init_val_fd_preview_video.dma_input.format);
		IS_FD_SET_PARAM_DMA_INPUT_BITWIDTH(dev,
			init_val_fd_preview_video.dma_input.bitwidth);
		IS_FD_SET_PARAM_DMA_INPUT_PLANE(dev,
			init_val_fd_preview_video.dma_input.plane);
		IS_FD_SET_PARAM_DMA_INPUT_ORDER(dev,
			init_val_fd_preview_video.dma_input.order);
		IS_FD_SET_PARAM_DMA_INPUT_BUFFERNUM(dev,
			init_val_fd_preview_video.dma_input.buffer_number);
		IS_FD_SET_PARAM_DMA_INPUT_BUFFERADDR(dev,
			init_val_fd_preview_video.dma_input.buffer_address);
		IS_FD_SET_PARAM_DMA_INPUT_ERR(dev,
			init_val_fd_preview_video.dma_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_FD_DMA_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_FD_SET_PARAM_FD_CONFIG_CMD(dev,
			init_val_fd_preview_video.config.cmd);
		IS_FD_SET_PARAM_FD_CONFIG_MAX_NUMBER(dev,
			init_val_fd_preview_video.config.max_number);
		IS_FD_SET_PARAM_FD_CONFIG_ROLL_ANGLE(dev,
			init_val_fd_preview_video.config.roll_angle);
		IS_FD_SET_PARAM_FD_CONFIG_YAW_ANGLE(dev,
			init_val_fd_preview_video.config.yaw_angle);
		IS_FD_SET_PARAM_FD_CONFIG_SMILE_MODE(dev,
			init_val_fd_preview_video.config.smile_mode);
		IS_FD_SET_PARAM_FD_CONFIG_BLINK_MODE(dev,
			init_val_fd_preview_video.config.blink_mode);
		IS_FD_SET_PARAM_FD_CONFIG_EYE_DETECT(dev,
			init_val_fd_preview_video.config.eye_detect);
		IS_FD_SET_PARAM_FD_CONFIG_MOUTH_DETECT(dev,
			init_val_fd_preview_video.config.mouth_detect);
		IS_FD_SET_PARAM_FD_CONFIG_ORIENTATION(dev,
			init_val_fd_preview_video.config.orientation);
		IS_FD_SET_PARAM_FD_CONFIG_ORIENTATION_VALUE(dev,
			init_val_fd_preview_video.config.orientation_value);
		IS_FD_SET_PARAM_FD_CONFIG_ERR(dev,
			init_val_fd_preview_video.config.err);
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONFIG);
		IS_INC_PARAM_NUM(dev);
		break;

	case ISS_CAPTURE_STILL:
		IS_SET_PARAM_GLOBAL_SHOTMODE_CMD(dev, 1);
		IS_SET_PARAM_BIT(dev, PARAM_GLOBAL_SHOTMODE);
		IS_INC_PARAM_NUM(dev);
		IS_SENSOR_SET_FRAME_RATE(dev, DEFAULT_CAPTURE_STILL_FRAMERATE);
		IS_SET_PARAM_BIT(dev, PARAM_SENSOR_FRAME_RATE);
		IS_INC_PARAM_NUM(dev);
		/* ISP */
		IS_ISP_SET_PARAM_CONTROL_CMD(dev,
			init_val_isp_capture.control.cmd);
		IS_ISP_SET_PARAM_CONTROL_BYPASS(dev,
			init_val_isp_capture.control.bypass);
		IS_ISP_SET_PARAM_CONTROL_ERR(dev,
			init_val_isp_capture.control.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_OTF_INPUT_CMD(dev,
			init_val_isp_capture.otf_input.cmd);
		IS_ISP_SET_PARAM_OTF_INPUT_WIDTH(dev,
			init_val_isp_capture.otf_input.width);
		IS_ISP_SET_PARAM_OTF_INPUT_HEIGHT(dev,
			init_val_isp_capture.otf_input.height);
		IS_ISP_SET_PARAM_OTF_INPUT_FORMAT(dev,
			init_val_isp_capture.otf_input.format);
		IS_ISP_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
			init_val_isp_capture.otf_input.bitwidth);
		IS_ISP_SET_PARAM_OTF_INPUT_ORDER(dev,
			init_val_isp_capture.otf_input.order);
		IS_ISP_SET_PARAM_OTF_INPUT_ERR(dev,
			init_val_isp_capture.otf_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_DMA_INPUT1_CMD(dev,
			init_val_isp_capture.dma1_input.cmd);
		IS_ISP_SET_PARAM_DMA_INPUT1_WIDTH(dev,
			init_val_isp_capture.dma1_input.width);
		IS_ISP_SET_PARAM_DMA_INPUT1_HEIGHT(dev,
			init_val_isp_capture.dma1_input.height);
		IS_ISP_SET_PARAM_DMA_INPUT1_FORMAT(dev,
			init_val_isp_capture.dma1_input.format);
		IS_ISP_SET_PARAM_DMA_INPUT1_BITWIDTH(dev,
			init_val_isp_capture.dma1_input.bitwidth);
		IS_ISP_SET_PARAM_DMA_INPUT1_PLANE(dev,
			init_val_isp_capture.dma1_input.plane);
		IS_ISP_SET_PARAM_DMA_INPUT1_ORDER(dev,
			init_val_isp_capture.dma1_input.order);
		IS_ISP_SET_PARAM_DMA_INPUT1_BUFFERNUM(dev,
			init_val_isp_capture.dma1_input.buffer_number);
		IS_ISP_SET_PARAM_DMA_INPUT1_BUFFERADDR(dev,
			init_val_isp_capture.dma1_input.buffer_address);
		IS_ISP_SET_PARAM_DMA_INPUT1_ERR(dev,
			init_val_isp_capture.dma1_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_DMA1_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_DMA_INPUT2_CMD(dev,
			init_val_isp_capture.dma2_input.cmd);
		IS_ISP_SET_PARAM_DMA_INPUT2_WIDTH(dev,
			init_val_isp_capture.dma2_input.width);
		IS_ISP_SET_PARAM_DMA_INPUT2_HEIGHT(dev,
			init_val_isp_capture.dma2_input.height);
		IS_ISP_SET_PARAM_DMA_INPUT2_FORMAT(dev,
			init_val_isp_capture.dma2_input.format);
		IS_ISP_SET_PARAM_DMA_INPUT2_BITWIDTH(dev,
			init_val_isp_capture.dma2_input.bitwidth);
		IS_ISP_SET_PARAM_DMA_INPUT2_PLANE(dev,
			init_val_isp_capture.dma2_input.plane);
		IS_ISP_SET_PARAM_DMA_INPUT2_ORDER(dev,
			init_val_isp_capture.dma2_input.order);
		IS_ISP_SET_PARAM_DMA_INPUT2_BUFFERNUM(dev,
			init_val_isp_capture.dma2_input.buffer_number);
		IS_ISP_SET_PARAM_DMA_INPUT2_BUFFERADDR(dev,
			init_val_isp_capture.dma2_input.buffer_address);
		IS_ISP_SET_PARAM_DMA_INPUT2_ERR(dev,
			init_val_isp_capture.dma2_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_DMA2_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_AA_CMD(dev, init_val_isp_capture.aa.cmd);
		IS_ISP_SET_PARAM_AA_TARGET(dev, init_val_isp_capture.aa.target);
		IS_ISP_SET_PARAM_AA_MODE(dev, init_val_isp_capture.aa.mode);
		IS_ISP_SET_PARAM_AA_FACE(dev, init_val_isp_capture.aa.face);
		IS_ISP_SET_PARAM_AA_WIN_POS_X(dev,
			init_val_isp_capture.aa.win_pos_x);
		IS_ISP_SET_PARAM_AA_WIN_POS_Y(dev,
			init_val_isp_capture.aa.win_pos_y);
		IS_ISP_SET_PARAM_AA_ERR(dev, init_val_isp_capture.aa.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_FLASH_CMD(dev,
			init_val_isp_capture.flash.cmd);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev,
			init_val_isp_capture.flash.redeye);
		IS_ISP_SET_PARAM_FLASH_ERR(dev,
			init_val_isp_capture.flash.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_AWB_CMD(dev, init_val_isp_capture.awb.cmd);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
			init_val_isp_capture.awb.illumination);
		IS_ISP_SET_PARAM_AWB_ERR(dev, init_val_isp_capture.awb.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_EFFECT_CMD(dev,
			init_val_isp_capture.effect.cmd);
		IS_ISP_SET_PARAM_EFFECT_ERR(dev,
			init_val_isp_capture.effect.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_IMAGE_EFFECT);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_ISO_CMD(dev,
			init_val_isp_capture.iso.cmd);
		IS_ISP_SET_PARAM_ISO_VALUE(dev,
			init_val_isp_capture.iso.value);
		IS_ISP_SET_PARAM_ISO_ERR(dev,
			init_val_isp_capture.iso.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
			init_val_isp_capture.adjust.cmd);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev,
			init_val_isp_capture.adjust.contrast);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev,
			init_val_isp_capture.adjust.saturation);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev,
			init_val_isp_capture.adjust.sharpness);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev,
			init_val_isp_capture.adjust.exposure);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev,
			init_val_isp_capture.adjust.brightness);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev,
			init_val_isp_capture.adjust.hue);
		IS_ISP_SET_PARAM_ADJUST_SHUTTER_TIME_MIN(dev,
			init_val_isp_capture.adjust.shutter_time_min);
		IS_ISP_SET_PARAM_ADJUST_SHUTTER_TIME_MAX(dev,
			init_val_isp_capture.adjust.shutter_time_max);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev,
			init_val_isp_capture.adjust.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_METERING_CMD(dev,
			init_val_isp_capture.metering.cmd);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev,
			init_val_isp_capture.metering.win_pos_x);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev,
			init_val_isp_capture.metering.win_pos_y);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev,
			init_val_isp_capture.metering.win_width);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev,
			init_val_isp_capture.metering.win_height);
		IS_ISP_SET_PARAM_METERING_ERR(dev,
			init_val_isp_capture.metering.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_AFC_CMD(dev, init_val_isp_capture.afc.cmd);
		IS_ISP_SET_PARAM_AFC_MANUAL(dev,
			init_val_isp_capture.afc.manual);
		IS_ISP_SET_PARAM_AFC_ERR(dev, init_val_isp_capture.afc.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AFC);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_OTF_OUTPUT_CMD(dev,
			init_val_isp_capture.otf_output.cmd);
		IS_ISP_SET_PARAM_OTF_OUTPUT_WIDTH(dev,
			init_val_isp_capture.otf_output.width);
		IS_ISP_SET_PARAM_OTF_OUTPUT_HEIGHT(dev,
			init_val_isp_capture.otf_output.height);
		IS_ISP_SET_PARAM_OTF_OUTPUT_FORMAT(dev,
			init_val_isp_capture.otf_output.format);
		IS_ISP_SET_PARAM_OTF_OUTPUT_BITWIDTH(dev,
			init_val_isp_capture.otf_output.bitwidth);
		IS_ISP_SET_PARAM_OTF_OUTPUT_ORDER(dev,
			init_val_isp_capture.otf_output.order);
		IS_ISP_SET_PARAM_OTF_OUTPUT_ERR(dev,
			init_val_isp_capture.otf_output.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_OUTPUT);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_CMD(dev,
			init_val_isp_capture.dma1_output.cmd);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_WIDTH(dev,
			init_val_isp_capture.dma1_output.width);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_HEIGHT(dev,
			init_val_isp_capture.dma1_output.height);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_FORMAT(dev,
			init_val_isp_capture.dma1_output.format);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_BITWIDTH(dev,
			init_val_isp_capture.dma1_output.bitwidth);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_PLANE(dev,
			init_val_isp_capture.dma1_output.plane);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_ORDER(dev,
			init_val_isp_capture.dma1_output.order);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_BUFFER_NUMBER(dev,
			init_val_isp_capture.dma1_output.buffer_number);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_BUFFER_ADDRESS(dev,
			init_val_isp_capture.dma1_output.buffer_address);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_ERR(dev,
			init_val_isp_capture.dma1_output.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_DMA1_OUTPUT);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_CMD(dev,
			init_val_isp_capture.dma2_output.cmd);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_WIDTH(dev,
			init_val_isp_capture.dma2_output.width);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_HEIGHT(dev,
			init_val_isp_capture.dma2_output.height);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_FORMAT(dev,
			init_val_isp_capture.dma2_output.format);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_BITWIDTH(dev,
			init_val_isp_capture.dma2_output.bitwidth);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_PLANE(dev,
			init_val_isp_capture.dma2_output.plane);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_ORDER(dev,
			init_val_isp_capture.dma2_output.order);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_BUFFER_NUMBER(dev,
			init_val_isp_capture.dma2_output.buffer_number);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_BUFFER_ADDRESS(dev,
			init_val_isp_capture.dma2_output.buffer_address);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_ERR(dev,
			init_val_isp_capture.dma2_output.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_DMA2_OUTPUT);
		IS_INC_PARAM_NUM(dev);

		/* DRC */
		IS_DRC_SET_PARAM_CONTROL_CMD(dev,
			init_val_drc_capture.control.cmd);
		IS_DRC_SET_PARAM_CONTROL_BYPASS(dev,
			init_val_drc_capture.control.bypass);
		IS_DRC_SET_PARAM_CONTROL_ERR(dev,
			init_val_drc_capture.control.err);
		IS_SET_PARAM_BIT(dev, PARAM_DRC_CONTROL);
		IS_INC_PARAM_NUM(dev);
		IS_DRC_SET_PARAM_OTF_INPUT_CMD(dev,
			init_val_drc_capture.otf_input.cmd);
		IS_DRC_SET_PARAM_OTF_INPUT_WIDTH(dev,
			init_val_drc_capture.otf_input.width);
		IS_DRC_SET_PARAM_OTF_INPUT_HEIGHT(dev,
			init_val_drc_capture.otf_input.height);
		IS_DRC_SET_PARAM_OTF_INPUT_FORMAT(dev,
			init_val_drc_capture.otf_input.format);
		IS_DRC_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
			init_val_drc_capture.otf_input.bitwidth);
		IS_DRC_SET_PARAM_OTF_INPUT_ORDER(dev,
			init_val_drc_capture.otf_input.order);
		IS_DRC_SET_PARAM_OTF_INPUT_ERR(dev,
			init_val_drc_capture.otf_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_DRC_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_DRC_SET_PARAM_DMA_INPUT_CMD(dev,
			init_val_drc_capture.dma_input.cmd);
		IS_DRC_SET_PARAM_DMA_INPUT_WIDTH(dev,
			init_val_drc_capture.dma_input.width);
		IS_DRC_SET_PARAM_DMA_INPUT_HEIGHT(dev,
			init_val_drc_capture.dma_input.height);
		IS_DRC_SET_PARAM_DMA_INPUT_FORMAT(dev,
			init_val_drc_capture.dma_input.format);
		IS_DRC_SET_PARAM_DMA_INPUT_BITWIDTH(dev,
			init_val_drc_capture.dma_input.bitwidth);
		IS_DRC_SET_PARAM_DMA_INPUT_PLANE(dev,
			init_val_drc_capture.dma_input.plane);
		IS_DRC_SET_PARAM_DMA_INPUT_ORDER(dev,
			init_val_drc_capture.dma_input.order);
		IS_DRC_SET_PARAM_DMA_INPUT_BUFFERNUM(dev,
			init_val_drc_capture.dma_input.buffer_number);
		IS_DRC_SET_PARAM_DMA_INPUT_BUFFERADDR(dev,
			init_val_drc_capture.dma_input.buffer_address);
		IS_DRC_SET_PARAM_DMA_INPUT_ERR(dev,
			init_val_drc_capture.dma_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_DRC_DMA_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_DRC_SET_PARAM_OTF_OUTPUT_CMD(dev,
			init_val_drc_capture.otf_output.cmd);
		IS_DRC_SET_PARAM_OTF_OUTPUT_WIDTH(dev,
			init_val_drc_capture.otf_output.width);
		IS_DRC_SET_PARAM_OTF_OUTPUT_HEIGHT(dev,
			init_val_drc_capture.otf_output.height);
		IS_DRC_SET_PARAM_OTF_OUTPUT_FORMAT(dev,
			init_val_drc_capture.otf_output.format);
		IS_DRC_SET_PARAM_OTF_OUTPUT_BITWIDTH(dev,
			init_val_drc_capture.otf_output.bitwidth);
		IS_DRC_SET_PARAM_OTF_OUTPUT_ORDER(dev,
			init_val_drc_capture.otf_output.order);
		IS_DRC_SET_PARAM_OTF_OUTPUT_ERR(dev,
			init_val_drc_capture.otf_output.err);
		IS_SET_PARAM_BIT(dev, PARAM_DRC_OTF_OUTPUT);
		IS_INC_PARAM_NUM(dev);

		/* FD */
		IS_FD_SET_PARAM_CONTROL_CMD(dev,
			init_val_fd_capture.control.cmd);
		IS_FD_SET_PARAM_CONTROL_BYPASS(dev,
			init_val_fd_capture.control.bypass);
		IS_FD_SET_PARAM_CONTROL_ERR(dev,
			init_val_fd_capture.control.err);
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONTROL);
		IS_INC_PARAM_NUM(dev);
		IS_FD_SET_PARAM_OTF_INPUT_CMD(dev,
			init_val_fd_capture.otf_input.cmd);
		IS_FD_SET_PARAM_OTF_INPUT_WIDTH(dev,
			init_val_fd_capture.otf_input.width);
		IS_FD_SET_PARAM_OTF_INPUT_HEIGHT(dev,
			init_val_fd_capture.otf_input.height);
		IS_FD_SET_PARAM_OTF_INPUT_FORMAT(dev,
			init_val_fd_capture.otf_input.format);
		IS_FD_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
			init_val_fd_capture.otf_input.bitwidth);
		IS_FD_SET_PARAM_OTF_INPUT_ORDER(dev,
			init_val_fd_capture.otf_input.order);
		IS_FD_SET_PARAM_OTF_INPUT_ERR(dev,
			init_val_fd_capture.otf_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_FD_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_FD_SET_PARAM_DMA_INPUT_CMD(dev,
			init_val_fd_capture.dma_input.cmd);
		IS_FD_SET_PARAM_DMA_INPUT_WIDTH(dev,
			init_val_fd_capture.dma_input.width);
		IS_FD_SET_PARAM_DMA_INPUT_HEIGHT(dev,
			init_val_fd_capture.dma_input.height);
		IS_FD_SET_PARAM_DMA_INPUT_FORMAT(dev,
			init_val_fd_capture.dma_input.format);
		IS_FD_SET_PARAM_DMA_INPUT_BITWIDTH(dev,
			init_val_fd_capture.dma_input.bitwidth);
		IS_FD_SET_PARAM_DMA_INPUT_PLANE(dev,
			init_val_fd_capture.dma_input.plane);
		IS_FD_SET_PARAM_DMA_INPUT_ORDER(dev,
			init_val_fd_capture.dma_input.order);
		IS_FD_SET_PARAM_DMA_INPUT_BUFFERNUM(dev,
			init_val_fd_capture.dma_input.buffer_number);
		IS_FD_SET_PARAM_DMA_INPUT_BUFFERADDR(dev,
			init_val_fd_capture.dma_input.buffer_address);
		IS_FD_SET_PARAM_DMA_INPUT_ERR(dev,
			init_val_fd_capture.dma_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_FD_DMA_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_FD_SET_PARAM_FD_CONFIG_CMD(dev,
			init_val_fd_capture.config.cmd);
		IS_FD_SET_PARAM_FD_CONFIG_MAX_NUMBER(dev,
			init_val_fd_capture.config.max_number);
		IS_FD_SET_PARAM_FD_CONFIG_ROLL_ANGLE(dev,
			init_val_fd_capture.config.roll_angle);
		IS_FD_SET_PARAM_FD_CONFIG_YAW_ANGLE(dev,
			init_val_fd_capture.config.yaw_angle);
		IS_FD_SET_PARAM_FD_CONFIG_SMILE_MODE(dev,
			init_val_fd_capture.config.smile_mode);
		IS_FD_SET_PARAM_FD_CONFIG_BLINK_MODE(dev,
			init_val_fd_capture.config.blink_mode);
		IS_FD_SET_PARAM_FD_CONFIG_EYE_DETECT(dev,
			init_val_fd_capture.config.eye_detect);
		IS_FD_SET_PARAM_FD_CONFIG_MOUTH_DETECT(dev,
			init_val_fd_capture.config.mouth_detect);
		IS_FD_SET_PARAM_FD_CONFIG_ORIENTATION(dev,
			init_val_fd_capture.config.orientation);
		IS_FD_SET_PARAM_FD_CONFIG_ORIENTATION_VALUE(dev,
			init_val_fd_capture.config.orientation_value);
		IS_FD_SET_PARAM_FD_CONFIG_ERR(dev,
			init_val_fd_capture.config.err);
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONFIG);
		IS_INC_PARAM_NUM(dev);
		break;

	case ISS_CAPTURE_VIDEO:
		IS_SET_PARAM_GLOBAL_SHOTMODE_CMD(dev, 1);
		IS_SET_PARAM_BIT(dev, PARAM_SENSOR_FRAME_RATE);
		IS_INC_PARAM_NUM(dev);
		IS_SENSOR_SET_FRAME_RATE(dev, DEFAULT_CAPTURE_VIDEO_FRAMERATE);
		IS_SET_PARAM_BIT(dev, PARAM_SENSOR_CONTROL);
		IS_INC_PARAM_NUM(dev);
		/* ISP */
		IS_ISP_SET_PARAM_CONTROL_CMD(dev,
			init_val_isp_camcording.control.cmd);
		IS_ISP_SET_PARAM_CONTROL_BYPASS(dev,
			init_val_isp_camcording.control.bypass);
		IS_ISP_SET_PARAM_CONTROL_ERR(dev,
			init_val_isp_camcording.control.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_OTF_INPUT_CMD(dev,
			init_val_isp_camcording.otf_input.cmd);
		IS_ISP_SET_PARAM_OTF_INPUT_WIDTH(dev,
			init_val_isp_camcording.otf_input.width);
		IS_ISP_SET_PARAM_OTF_INPUT_HEIGHT(dev,
			init_val_isp_camcording.otf_input.height);
		IS_ISP_SET_PARAM_OTF_INPUT_FORMAT(dev,
			init_val_isp_camcording.otf_input.format);
		IS_ISP_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
			init_val_isp_camcording.otf_input.bitwidth);
		IS_ISP_SET_PARAM_OTF_INPUT_ORDER(dev,
			init_val_isp_camcording.otf_input.order);
		IS_ISP_SET_PARAM_OTF_INPUT_ERR(dev,
			init_val_isp_camcording.otf_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_DMA_INPUT1_CMD(dev,
			init_val_isp_camcording.dma1_input.cmd);
		IS_ISP_SET_PARAM_DMA_INPUT1_WIDTH(dev,
			init_val_isp_camcording.dma1_input.width);
		IS_ISP_SET_PARAM_DMA_INPUT1_HEIGHT(dev,
			init_val_isp_camcording.dma1_input.height);
		IS_ISP_SET_PARAM_DMA_INPUT1_FORMAT(dev,
			init_val_isp_camcording.dma1_input.format);
		IS_ISP_SET_PARAM_DMA_INPUT1_BITWIDTH(dev,
			init_val_isp_camcording.dma1_input.bitwidth);
		IS_ISP_SET_PARAM_DMA_INPUT1_PLANE(dev,
			init_val_isp_camcording.dma1_input.plane);
		IS_ISP_SET_PARAM_DMA_INPUT1_ORDER(dev,
			init_val_isp_camcording.dma1_input.order);
		IS_ISP_SET_PARAM_DMA_INPUT1_BUFFERNUM(dev,
			init_val_isp_camcording.dma1_input.buffer_number);
		IS_ISP_SET_PARAM_DMA_INPUT1_BUFFERADDR(dev,
			init_val_isp_camcording.dma1_input.buffer_address);
		IS_ISP_SET_PARAM_DMA_INPUT1_ERR(dev,
			init_val_isp_camcording.dma1_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_DMA1_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_DMA_INPUT2_CMD(dev,
			init_val_isp_camcording.dma2_input.cmd);
		IS_ISP_SET_PARAM_DMA_INPUT2_WIDTH(dev,
			init_val_isp_camcording.dma2_input.width);
		IS_ISP_SET_PARAM_DMA_INPUT2_HEIGHT(dev,
			init_val_isp_camcording.dma2_input.height);
		IS_ISP_SET_PARAM_DMA_INPUT2_FORMAT(dev,
			init_val_isp_camcording.dma2_input.format);
		IS_ISP_SET_PARAM_DMA_INPUT2_BITWIDTH(dev,
			init_val_isp_camcording.dma2_input.bitwidth);
		IS_ISP_SET_PARAM_DMA_INPUT2_PLANE(dev,
			init_val_isp_camcording.dma2_input.plane);
		IS_ISP_SET_PARAM_DMA_INPUT2_ORDER(dev,
			init_val_isp_camcording.dma2_input.order);
		IS_ISP_SET_PARAM_DMA_INPUT2_BUFFERNUM(dev,
			init_val_isp_camcording.dma2_input.buffer_number);
		IS_ISP_SET_PARAM_DMA_INPUT2_BUFFERADDR(dev,
			init_val_isp_camcording.dma2_input.buffer_address);
		IS_ISP_SET_PARAM_DMA_INPUT2_ERR(dev,
			init_val_isp_camcording.dma2_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_DMA2_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_AA_CMD(dev, init_val_isp_camcording.aa.cmd);
		IS_ISP_SET_PARAM_AA_TARGET(dev,
			init_val_isp_camcording.aa.target);
		IS_ISP_SET_PARAM_AA_MODE(dev, init_val_isp_camcording.aa.mode);
		IS_ISP_SET_PARAM_AA_FACE(dev, init_val_isp_camcording.aa.face);
		IS_ISP_SET_PARAM_AA_WIN_POS_X(dev,
			init_val_isp_camcording.aa.win_pos_x);
		IS_ISP_SET_PARAM_AA_WIN_POS_Y(dev,
			init_val_isp_camcording.aa.win_pos_y);
		IS_ISP_SET_PARAM_AA_ERR(dev, init_val_isp_camcording.aa.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_FLASH_CMD(dev,
			init_val_isp_camcording.flash.cmd);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev,
			init_val_isp_camcording.flash.redeye);
		IS_ISP_SET_PARAM_FLASH_ERR(dev,
			init_val_isp_camcording.flash.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_AWB_CMD(dev, init_val_isp_camcording.awb.cmd);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
			init_val_isp_camcording.awb.illumination);
		IS_ISP_SET_PARAM_AWB_ERR(dev, init_val_isp_camcording.awb.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_EFFECT_CMD(dev,
			init_val_isp_camcording.effect.cmd);
		IS_ISP_SET_PARAM_EFFECT_ERR(dev,
			init_val_isp_camcording.effect.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_IMAGE_EFFECT);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_ISO_CMD(dev,
			init_val_isp_camcording.iso.cmd);
		IS_ISP_SET_PARAM_ISO_VALUE(dev,
			init_val_isp_camcording.iso.value);
		IS_ISP_SET_PARAM_ISO_ERR(dev,
			init_val_isp_camcording.iso.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
			init_val_isp_camcording.adjust.cmd);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev,
			init_val_isp_camcording.adjust.contrast);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev,
			init_val_isp_camcording.adjust.saturation);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev,
			init_val_isp_camcording.adjust.sharpness);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev,
			init_val_isp_camcording.adjust.exposure);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev,
			init_val_isp_camcording.adjust.brightness);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev,
			init_val_isp_camcording.adjust.hue);
		IS_ISP_SET_PARAM_ADJUST_SHUTTER_TIME_MIN(dev,
			init_val_isp_camcording.adjust.shutter_time_min);
		IS_ISP_SET_PARAM_ADJUST_SHUTTER_TIME_MAX(dev,
			init_val_isp_camcording.adjust.shutter_time_max);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev,
			init_val_isp_camcording.adjust.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_METERING_CMD(dev,
			init_val_isp_camcording.metering.cmd);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev,
			init_val_isp_camcording.metering.win_pos_x);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev,
			init_val_isp_camcording.metering.win_pos_y);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev,
			init_val_isp_camcording.metering.win_width);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev,
			init_val_isp_camcording.metering.win_height);
		IS_ISP_SET_PARAM_METERING_ERR(dev,
			init_val_isp_camcording.metering.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_AFC_CMD(dev, init_val_isp_camcording.afc.cmd);
		IS_ISP_SET_PARAM_AFC_MANUAL(dev,
			init_val_isp_camcording.afc.manual);
		IS_ISP_SET_PARAM_AFC_ERR(dev, init_val_isp_camcording.afc.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AFC);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_OTF_OUTPUT_CMD(dev,
			init_val_isp_camcording.otf_output.cmd);
		IS_ISP_SET_PARAM_OTF_OUTPUT_WIDTH(dev,
			init_val_isp_camcording.otf_output.width);
		IS_ISP_SET_PARAM_OTF_OUTPUT_HEIGHT(dev,
			init_val_isp_camcording.otf_output.height);
		IS_ISP_SET_PARAM_OTF_OUTPUT_FORMAT(dev,
			init_val_isp_camcording.otf_output.format);
		IS_ISP_SET_PARAM_OTF_OUTPUT_BITWIDTH(dev,
			init_val_isp_camcording.otf_output.bitwidth);
		IS_ISP_SET_PARAM_OTF_OUTPUT_ORDER(dev,
			init_val_isp_camcording.otf_output.order);
		IS_ISP_SET_PARAM_OTF_OUTPUT_ERR(dev,
			init_val_isp_camcording.otf_output.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_OUTPUT);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_CMD(dev,
			init_val_isp_camcording.dma1_output.cmd);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_WIDTH(dev,
			init_val_isp_camcording.dma1_output.width);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_HEIGHT(dev,
			init_val_isp_camcording.dma1_output.height);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_FORMAT(dev,
			init_val_isp_camcording.dma1_output.format);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_BITWIDTH(dev,
			init_val_isp_camcording.dma1_output.bitwidth);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_PLANE(dev,
			init_val_isp_camcording.dma1_output.plane);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_ORDER(dev,
			init_val_isp_camcording.dma1_output.order);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_BUFFER_NUMBER(dev,
			init_val_isp_camcording.dma1_output.buffer_number);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_BUFFER_ADDRESS(dev,
			init_val_isp_camcording.dma1_output.buffer_address);
		IS_ISP_SET_PARAM_DMA_OUTPUT1_ERR(dev,
			init_val_isp_camcording.dma1_output.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_DMA1_OUTPUT);
		IS_INC_PARAM_NUM(dev);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_CMD(dev,
			init_val_isp_camcording.dma2_output.cmd);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_WIDTH(dev,
			init_val_isp_camcording.dma2_output.width);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_HEIGHT(dev,
			init_val_isp_camcording.dma2_output.height);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_FORMAT(dev,
			init_val_isp_camcording.dma2_output.format);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_BITWIDTH(dev,
			init_val_isp_camcording.dma2_output.bitwidth);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_PLANE(dev,
			init_val_isp_camcording.dma2_output.plane);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_ORDER(dev,
			init_val_isp_camcording.dma2_output.order);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_BUFFER_NUMBER(dev,
			init_val_isp_camcording.dma2_output.buffer_number);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_BUFFER_ADDRESS(dev,
			init_val_isp_camcording.dma2_output.buffer_address);
		IS_ISP_SET_PARAM_DMA_OUTPUT2_ERR(dev,
			init_val_isp_camcording.dma2_output.err);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_DMA2_OUTPUT);
		IS_INC_PARAM_NUM(dev);

		/* DRC */
		IS_DRC_SET_PARAM_CONTROL_CMD(dev,
			init_val_drc_camcording.control.cmd);
		IS_DRC_SET_PARAM_CONTROL_BYPASS(dev,
			init_val_drc_camcording.control.bypass);
		IS_DRC_SET_PARAM_CONTROL_ERR(dev,
			init_val_drc_camcording.control.err);
		IS_SET_PARAM_BIT(dev, PARAM_DRC_CONTROL);
		IS_INC_PARAM_NUM(dev);
		IS_DRC_SET_PARAM_OTF_INPUT_CMD(dev,
			init_val_drc_camcording.otf_input.cmd);
		IS_DRC_SET_PARAM_OTF_INPUT_WIDTH(dev,
			init_val_drc_camcording.otf_input.width);
		IS_DRC_SET_PARAM_OTF_INPUT_HEIGHT(dev,
			init_val_drc_camcording.otf_input.height);
		IS_DRC_SET_PARAM_OTF_INPUT_FORMAT(dev,
			init_val_drc_camcording.otf_input.format);
		IS_DRC_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
			init_val_drc_camcording.otf_input.bitwidth);
		IS_DRC_SET_PARAM_OTF_INPUT_ORDER(dev,
			init_val_drc_camcording.otf_input.order);
		IS_DRC_SET_PARAM_OTF_INPUT_ERR(dev,
			init_val_drc_camcording.otf_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_DRC_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_DRC_SET_PARAM_DMA_INPUT_CMD(dev,
			init_val_drc_camcording.dma_input.cmd);
		IS_DRC_SET_PARAM_DMA_INPUT_WIDTH(dev,
			init_val_drc_camcording.dma_input.width);
		IS_DRC_SET_PARAM_DMA_INPUT_HEIGHT(dev,
			init_val_drc_camcording.dma_input.height);
		IS_DRC_SET_PARAM_DMA_INPUT_FORMAT(dev,
			init_val_drc_camcording.dma_input.format);
		IS_DRC_SET_PARAM_DMA_INPUT_BITWIDTH(dev,
			init_val_drc_camcording.dma_input.bitwidth);
		IS_DRC_SET_PARAM_DMA_INPUT_PLANE(dev,
			init_val_drc_camcording.dma_input.plane);
		IS_DRC_SET_PARAM_DMA_INPUT_ORDER(dev,
			init_val_drc_camcording.dma_input.order);
		IS_DRC_SET_PARAM_DMA_INPUT_BUFFERNUM(dev,
			init_val_drc_camcording.dma_input.buffer_number);
		IS_DRC_SET_PARAM_DMA_INPUT_BUFFERADDR(dev,
			init_val_drc_camcording.dma_input.buffer_address);
		IS_DRC_SET_PARAM_DMA_INPUT_ERR(dev,
			init_val_drc_camcording.dma_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_DRC_DMA_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_DRC_SET_PARAM_OTF_OUTPUT_CMD(dev,
			init_val_drc_camcording.otf_output.cmd);
		IS_DRC_SET_PARAM_OTF_OUTPUT_WIDTH(dev,
			init_val_drc_camcording.otf_output.width);
		IS_DRC_SET_PARAM_OTF_OUTPUT_HEIGHT(dev,
			init_val_drc_camcording.otf_output.height);
		IS_DRC_SET_PARAM_OTF_OUTPUT_FORMAT(dev,
			init_val_drc_camcording.otf_output.format);
		IS_DRC_SET_PARAM_OTF_OUTPUT_BITWIDTH(dev,
			init_val_drc_camcording.otf_output.bitwidth);
		IS_DRC_SET_PARAM_OTF_OUTPUT_ORDER(dev,
			init_val_drc_camcording.otf_output.order);
		IS_DRC_SET_PARAM_OTF_OUTPUT_ERR(dev,
			init_val_drc_camcording.otf_output.err);
		IS_SET_PARAM_BIT(dev, PARAM_DRC_OTF_OUTPUT);
		IS_INC_PARAM_NUM(dev);

		/* FD */
		IS_FD_SET_PARAM_CONTROL_CMD(dev,
			init_val_fd_camcording.control.cmd);
		IS_FD_SET_PARAM_CONTROL_BYPASS(dev,
			init_val_fd_camcording.control.bypass);
		IS_FD_SET_PARAM_CONTROL_ERR(dev,
			init_val_fd_camcording.control.err);
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONTROL);
		IS_INC_PARAM_NUM(dev);
		IS_FD_SET_PARAM_OTF_INPUT_CMD(dev,
			init_val_fd_camcording.otf_input.cmd);
		IS_FD_SET_PARAM_OTF_INPUT_WIDTH(dev,
			init_val_fd_camcording.otf_input.width);
		IS_FD_SET_PARAM_OTF_INPUT_HEIGHT(dev,
			init_val_fd_camcording.otf_input.height);
		IS_FD_SET_PARAM_OTF_INPUT_FORMAT(dev,
			init_val_fd_camcording.otf_input.format);
		IS_FD_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
			init_val_fd_camcording.otf_input.bitwidth);
		IS_FD_SET_PARAM_OTF_INPUT_ORDER(dev,
			init_val_fd_camcording.otf_input.order);
		IS_FD_SET_PARAM_OTF_INPUT_ERR(dev,
			init_val_fd_camcording.otf_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_FD_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_FD_SET_PARAM_DMA_INPUT_CMD(dev,
			init_val_fd_camcording.dma_input.cmd);
		IS_FD_SET_PARAM_DMA_INPUT_WIDTH(dev,
			init_val_fd_camcording.dma_input.width);
		IS_FD_SET_PARAM_DMA_INPUT_HEIGHT(dev,
			init_val_fd_camcording.dma_input.height);
		IS_FD_SET_PARAM_DMA_INPUT_FORMAT(dev,
			init_val_fd_camcording.dma_input.format);
		IS_FD_SET_PARAM_DMA_INPUT_BITWIDTH(dev,
			init_val_fd_camcording.dma_input.bitwidth);
		IS_FD_SET_PARAM_DMA_INPUT_PLANE(dev,
			init_val_fd_camcording.dma_input.plane);
		IS_FD_SET_PARAM_DMA_INPUT_ORDER(dev,
			init_val_fd_camcording.dma_input.order);
		IS_FD_SET_PARAM_DMA_INPUT_BUFFERNUM(dev,
			init_val_fd_camcording.dma_input.buffer_number);
		IS_FD_SET_PARAM_DMA_INPUT_BUFFERADDR(dev,
			init_val_fd_camcording.dma_input.buffer_address);
		IS_FD_SET_PARAM_DMA_INPUT_ERR(dev,
			init_val_fd_camcording.dma_input.err);
		IS_SET_PARAM_BIT(dev, PARAM_FD_DMA_INPUT);
		IS_INC_PARAM_NUM(dev);
		IS_FD_SET_PARAM_FD_CONFIG_CMD(dev,
			init_val_fd_camcording.config.cmd);
		IS_FD_SET_PARAM_FD_CONFIG_MAX_NUMBER(dev,
			init_val_fd_camcording.config.max_number);
		IS_FD_SET_PARAM_FD_CONFIG_ROLL_ANGLE(dev,
			init_val_fd_camcording.config.roll_angle);
		IS_FD_SET_PARAM_FD_CONFIG_YAW_ANGLE(dev,
			init_val_fd_camcording.config.yaw_angle);
		IS_FD_SET_PARAM_FD_CONFIG_SMILE_MODE(dev,
			init_val_fd_camcording.config.smile_mode);
		IS_FD_SET_PARAM_FD_CONFIG_BLINK_MODE(dev,
			init_val_fd_camcording.config.blink_mode);
		IS_FD_SET_PARAM_FD_CONFIG_EYE_DETECT(dev,
			init_val_fd_camcording.config.eye_detect);
		IS_FD_SET_PARAM_FD_CONFIG_MOUTH_DETECT(dev,
			init_val_fd_camcording.config.mouth_detect);
		IS_FD_SET_PARAM_FD_CONFIG_ORIENTATION(dev,
			init_val_fd_camcording.config.orientation);
		IS_FD_SET_PARAM_FD_CONFIG_ORIENTATION_VALUE(dev,
			init_val_fd_camcording.config.orientation_value);
		IS_FD_SET_PARAM_FD_CONFIG_ERR(dev,
			init_val_fd_camcording.config.err);
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONFIG);
		IS_INC_PARAM_NUM(dev);
		break;
	}
}
