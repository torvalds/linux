/*
 * Samsung Exynos5 SoC series FIMC-IS driver
 *
 * exynos5 fimc-is helper functions
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
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
#include <linux/videodev2_exynos_camera.h>
#include <linux/gpio.h>
#include <plat/gpio-cfg.h>
#include <media/exynos_fimc_is.h>


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
		.width = DEFAULT_CAPTURE_STILL_WIDTH,
		.height = DEFAULT_CAPTURE_STILL_HEIGHT,
		.format = OTF_INPUT_FORMAT_BAYER,
		.bitwidth = OTF_INPUT_BIT_WIDTH_10BIT,
		.order = OTF_INPUT_ORDER_BAYER_GR_BG,
		.crop_offset_x = 0,
		.crop_offset_y = 0,
		.crop_width = 0,
		.crop_height = 0,
		.frametime_min = 0,
		.frametime_max = 33333,
		.err = OTF_INPUT_ERROR_NO,
	},
	.dma1_input = {
		.cmd = DMA_INPUT_COMMAND_DISABLE,
		.width = 0,
		.height = 0,
		.format = 0,
		.bitwidth = 0,
		.plane = 0,
		.order = 0,
		.buffer_number = 0,
		.buffer_address = 0,
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
		.scene = 0,
		.sleep = 0,
		.face = 0,
		.touch_x = 0, .touch_y = 0,
		.manual_af_setting = 0,
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
		.err = ISP_ADJUST_ERROR_NO,
	},
	.metering = {
		.cmd = ISP_METERING_COMMAND_CENTER,
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
#ifdef DZOOM_EVT0
		.cmd = DMA_OUTPUT_COMMAND_ENABLE,
		.dma_out_mask = 0xFFFFFFFF,
#else
		.cmd = DMA_OUTPUT_COMMAND_DISABLE,
		.dma_out_mask = 0,
#endif
		.width = DEFAULT_CAPTURE_STILL_WIDTH,
		.height = DEFAULT_CAPTURE_STILL_HEIGHT,
		.format = DMA_INPUT_FORMAT_YUV444,
		.bitwidth = DMA_INPUT_BIT_WIDTH_8BIT,
		.plane = DMA_INPUT_PLANE_1,
		.order = DMA_INPUT_ORDER_YCbCr,
		.buffer_number = 0,
		.buffer_address = 0,
		.err = DMA_OUTPUT_ERROR_NO,
	},
	.dma2_output = {
		.cmd = DMA_OUTPUT_COMMAND_DISABLE,
		.width = DEFAULT_CAPTURE_STILL_WIDTH,
		.height = DEFAULT_CAPTURE_STILL_HEIGHT,
		.format = DMA_OUTPUT_FORMAT_BAYER,
		.bitwidth = DMA_OUTPUT_BIT_WIDTH_12BIT,
		.plane = DMA_OUTPUT_PLANE_1,
		.order = DMA_OUTPUT_ORDER_GB_BG,
		.buffer_number = 0,
		.buffer_address = 0,
		.dma_out_mask = 0xFFFFFFFF,
		.err = DMA_OUTPUT_ERROR_NO,
	},
};

static const struct drc_param init_val_drc_preview_still = {
	.control = {
		.cmd = CONTROL_COMMAND_START,
		.bypass = CONTROL_BYPASS_DISABLE,
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
		.width = DEFAULT_CAPTURE_STILL_WIDTH,
		.height = DEFAULT_CAPTURE_STILL_HEIGHT,
		.format = DMA_INPUT_FORMAT_YUV444,
		.bitwidth = DMA_INPUT_BIT_WIDTH_8BIT,
		.plane = DMA_INPUT_PLANE_1,
		.order = DMA_INPUT_ORDER_YCbCr,
		.buffer_number = 0,
		.buffer_address = 0,
		.err = 0,
	},
	.otf_output = {
		.cmd = OTF_OUTPUT_COMMAND_ENABLE,
		.width = DEFAULT_CAPTURE_STILL_WIDTH,
		.height = DEFAULT_CAPTURE_STILL_HEIGHT,
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
		.width = DEFAULT_CAPTURE_STILL_WIDTH,
		.height = DEFAULT_CAPTURE_STILL_HEIGHT,
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
	.input_crop = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.pos_x = 0,
		.pos_y = 0,
		.crop_width = DEFAULT_CAPTURE_STILL_CROP_WIDTH,
		.crop_height = DEFAULT_CAPTURE_STILL_CROP_HEIGHT,
		.in_width = DEFAULT_CAPTURE_STILL_WIDTH,
		.in_height = DEFAULT_CAPTURE_STILL_HEIGHT,
		.out_width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.out_height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
		.err = 0,
	},
	.output_crop = {
		.cmd = OTF_INPUT_COMMAND_DISABLE,
		.pos_x = 0,
		.pos_y = 0,
		.crop_width = DEFAULT_CAPTURE_STILL_WIDTH,
		.crop_height = DEFAULT_CAPTURE_STILL_HEIGHT,
		.format = DMA_OUTPUT_FORMAT_YUV422,
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
	.dma_output = {
		.cmd = DMA_OUTPUT_COMMAND_DISABLE,
		.width = DEFAULT_CAPTURE_STILL_WIDTH,
		.height = DEFAULT_CAPTURE_STILL_HEIGHT,
		.format = DMA_OUTPUT_FORMAT_YUV422,
		.bitwidth = DMA_OUTPUT_BIT_WIDTH_8BIT,
		.plane = DMA_OUTPUT_PLANE_1,
		.order = DMA_OUTPUT_ORDER_CrYCbY,
		.buffer_number = 0,
		.buffer_address = 0,
		.dma_out_mask = 0xffff,
		.reserved[0] = 2, /* unscaled*/
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
		.width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
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
		.width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
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
		.width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
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
		.width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
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
		.width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
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
		.width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
		.format = OTF_OUTPUT_FORMAT_YUV422,
		.bitwidth = OTF_INPUT_BIT_WIDTH_8BIT,
		.order = OTF_OUTPUT_ORDER_BAYER_GR_BG,
		.err = OTF_OUTPUT_ERROR_NO,
	},
	.dma_output = {
		.cmd = DMA_OUTPUT_COMMAND_DISABLE,
		.width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
		.format = DMA_OUTPUT_FORMAT_YUV420,
		.bitwidth = DMA_OUTPUT_BIT_WIDTH_8BIT,
		.plane = DMA_OUTPUT_PLANE_2,
		.order = DMA_OUTPUT_ORDER_CbCr,
		.buffer_number = 0,
		.buffer_address = 0,
		.dma_out_mask = 0xffff,
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
		.width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
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
	.input_crop = {
		.cmd = OTF_INPUT_COMMAND_ENABLE,
		.pos_x = 0,
		.pos_y = 0,
		.crop_width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.crop_height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
		.in_width = DEFAULT_CAPTURE_VIDEO_WIDTH,
		.in_height = DEFAULT_CAPTURE_VIDEO_HEIGHT,
		.out_width = DEFAULT_PREVIEW_STILL_WIDTH,
		.out_height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.err = 0,
	},
	.output_crop = {
		.cmd = OTF_INPUT_COMMAND_DISABLE,
		.pos_x = 0,
		.pos_y = 0,
		.crop_width = DEFAULT_PREVIEW_STILL_WIDTH,
		.crop_height = DEFAULT_PREVIEW_STILL_HEIGHT,
		.format = OTF_OUTPUT_FORMAT_YUV420,
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
		.dma_out_mask = 0xffff,
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
		.yaw_angle = FD_CONFIG_YAW_ANGLE_45_90,
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

int fimc_is_fw_clear_irq1_all(struct fimc_is_dev *dev)
{
	writel(0xFF, dev->regs + INTCR1);
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
int fimc_is_hw_get_sensor_type(enum exynos5_sensor_id sensor_id,
				enum exynos5_flite_id flite_id)
{
	int id = sensor_id;

	if (flite_id == FLITE_ID_A)
		id = sensor_id;
	else if (flite_id == FLITE_ID_B)
		id = sensor_id + 100;

	return id;
}

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
		dev->af.use_af = 1;
		dev->sensor.sensor_type = SENSOR_S5K4E5_CSI_A;
		writel(SENSOR_NAME_S5K4E5, dev->regs + ISSR2);
		writel(SENSOR_CONTROL_I2C0, dev->regs + ISSR3);
		break;
	case SENSOR_S5K4E5_CSI_B:
		dev->af.use_af = 1;
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
	if (dev->sensor.id_dual == id) {
		fimc_is_hw_wait_intmsr0_intmsd0(dev);
		writel(HIC_CLOSE_SENSOR, dev->regs + ISSR0);
		writel(dev->sensor.id_dual, dev->regs + ISSR1);
		writel(dev->sensor.id_dual, dev->regs + ISSR2);
		fimc_is_hw_set_intgr0_gd0(dev);
	}
}

void fimc_is_hw_diable_wdt(struct fimc_is_dev *dev)
{
	writel(0x0, dev->regs + WDT);
}

void fimc_is_hw_set_low_poweroff(struct fimc_is_dev *dev, int on)
{
	if (on) {
		printk(KERN_INFO "Set low poweroff mode\n");
		__raw_writel(0x0, PMUREG_ISP_ARM_OPTION);
		__raw_writel(0x1CF82000, PMUREG_ISP_LOW_POWER_OFF);
		dev->low_power_mode = true;
	} else {
		if (dev->low_power_mode) {
			printk(KERN_INFO "Clear low poweroff mode\n");
			__raw_writel(0xFFFFFFFF, PMUREG_ISP_ARM_OPTION);
			__raw_writel(0x8, PMUREG_ISP_LOW_POWER_OFF);
		}
		dev->low_power_mode = false;
	}
}

void fimc_is_hw_subip_poweroff(struct fimc_is_dev *dev)
{
	/* 1. Make FIMC-IS power-off state */
	fimc_is_hw_wait_intmsr0_intmsd0(dev);
	writel(HIC_POWER_DOWN, dev->regs + ISSR0);
	writel(dev->sensor.id_dual, dev->regs + ISSR1);
	fimc_is_hw_set_intgr0_gd0(dev);
}

int fimc_is_hw_a5_power_on(struct fimc_is_dev *isp)
{
	u32 cfg;
	u32 timeout;

	mutex_lock(&isp->lock);

	if (isp->low_power_mode)
		fimc_is_hw_set_low_poweroff(isp, false);

	dbg("%s\n", __func__);

	writel(0x7, PMUREG_ISP_CONFIGURATION);
	timeout = 1000;
	while ((__raw_readl(PMUREG_ISP_STATUS) & 0x7) != 0x7) {
		if (timeout == 0)
			err("A5 power on failed1\n");
		timeout--;
		udelay(1);
	}

	enable_mipi();

	/* init Clock */

	if (isp->pdata->regulator_on) {
		isp->pdata->regulator_on(isp->pdev);
	} else {
		dev_err(&isp->pdev->dev, "failed to regulator on\n");
		goto done;
	}

	if (isp->pdata->clk_cfg) {
		isp->pdata->clk_cfg(isp->pdev);
	} else {
		dev_err(&isp->pdev->dev, "failed to config clock\n");
		goto done;
	}


	if (isp->pdata->clk_on) {
		isp->pdata->clk_on(isp->pdev);
	} else {
		dev_err(&isp->pdev->dev, "failed to clock on\n");
		goto done;
	}

	/* 1. A5 start address setting */
#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
	cfg = isp->mem.base;
#elif defined(CONFIG_VIDEOBUF2_ION)
	cfg = isp->mem.dvaddr;
	if (isp->alloc_ctx)
		fimc_is_mem_resume(isp->alloc_ctx);
#endif

	dbg("mem.base(dvaddr) : 0x%08x\n", cfg);
	dbg("mem.base(kvaddr) : 0x%08x\n", (unsigned int)isp->mem.kvaddr);
	writel(cfg, isp->regs + BBOAR);

	/* 2. A5 power on*/
	writel(0x1, PMUREG_ISP_ARM_CONFIGURATION);

	/* 3. enable A5 */
	writel(0x00018000, PMUREG_ISP_ARM_OPTION);
	timeout = 1000;
	while ((__raw_readl(PMUREG_ISP_ARM_STATUS) & 0x1) != 0x1) {
		if (timeout == 0)
			err("A5 power on failed2\n");
		timeout--;
		udelay(1);
	}

	/* HACK : fimc_is_irq_handler() cannot
	 * set 1 on FIMC_IS_PWR_ST_POWER_ON_OFF */
	set_bit(FIMC_IS_PWR_ST_POWER_ON_OFF, &isp->power);

done:
	mutex_unlock(&isp->lock);
	return 0;
}

int fimc_is_hw_a5_power_off(struct fimc_is_dev *isp)
{
	u32 timeout;

	dbg("%s\n", __func__);

	mutex_lock(&isp->lock);

#if defined(CONFIG_VIDEOBUF2_ION)
	if (isp->alloc_ctx)
		fimc_is_mem_suspend(isp->alloc_ctx);
#endif

	if (isp->pdata->clk_off) {
		isp->pdata->clk_off(isp->pdev);
	} else {
		dev_err(&isp->pdev->dev, "failed to clock on\n");
		goto done;
	}

	/* will be enabled after regulator problem solved*/
	/*
	if (isp->pdata->regulator_off) {
		isp->pdata->regulator_off(isp->pdev);
	} else {
		dev_err(&isp->pdev->dev, "failed to regulator off\n");
		goto done;
	}
	*/

	/* 1. disable A5 */
	writel(0x0, PMUREG_ISP_ARM_OPTION);

	/* 2. A5 power off*/
	writel(0x0, PMUREG_ISP_ARM_CONFIGURATION);

	/* 3. Check A5 power off status register */
	timeout = 1000;
	while (__raw_readl(PMUREG_ISP_ARM_STATUS) & 0x1) {
		if (timeout == 0)
			err("A5 power off failed\n");
		timeout--;
		udelay(1);
	}

	/* 4. ISP Power down mode (LOWPWR) */
	writel(0x0, PMUREG_CMU_RESET_ISP_SYS_PWR_REG);

	writel(0x0, PMUREG_ISP_CONFIGURATION);

	timeout = 1000;
	while ((__raw_readl(PMUREG_ISP_STATUS) & 0x7)) {
		if (timeout == 0) {
			err("ISP power off failed --> Retry\n");
			/* Retry */
			__raw_writel(0x1CF82000, PMUREG_ISP_LOW_POWER_OFF);
			timeout = 1000;
			while ((__raw_readl(PMUREG_ISP_STATUS) & 0x7)) {
				if (timeout == 0)
					err("ISP power off failed\n");
				timeout--;
				udelay(1);
			}
		}
		timeout--;
		udelay(1);
	}

done:
	mutex_unlock(&isp->lock);
	return 0;
}

void fimc_is_hw_a5_power(struct fimc_is_dev *isp, int on)
{
#if defined(CONFIG_PM_RUNTIME)
	struct device *dev = &isp->pdev->dev;
#endif

	printk(KERN_INFO "%s(%d)\n", __func__, on);

#if defined(CONFIG_PM_RUNTIME)
	if (on)
		pm_runtime_get_sync(dev);
	else
		pm_runtime_put_sync(dev);
#else
	if (on)
		fimc_is_hw_a5_power_on(isp);
	else
		fimc_is_hw_a5_power_off(isp);
#endif
}

void fimc_is_hw_set_sensor_num(struct fimc_is_dev *dev)
{
	u32 cfg;
	writel(ISR_DONE, dev->regs + ISSR0);
	cfg = dev->sensor.id_dual;
	writel(cfg, dev->regs + ISSR1);
	/* param 1 */
	writel(IHC_GET_SENSOR_NUMBER, dev->regs + ISSR2);
	/* param 2 */
	cfg = dev->sensor_num;
	writel(cfg, dev->regs + ISSR3);
}

void fimc_is_hw_get_setfile_addr(struct fimc_is_dev *dev)
{
	/* 1. Get FIMC-IS setfile address */
	fimc_is_hw_wait_intmsr0_intmsd0(dev);
	writel(HIC_GET_SET_FILE_ADDR, dev->regs + ISSR0);
	writel(dev->sensor.id_dual, dev->regs + ISSR1);
	fimc_is_hw_set_intgr0_gd0(dev);
}

void fimc_is_hw_load_setfile(struct fimc_is_dev *dev)
{
	/* 1. Make FIMC-IS power-off state */
	fimc_is_hw_wait_intmsr0_intmsd0(dev);
	writel(HIC_LOAD_SET_FILE, dev->regs + ISSR0);
	writel(dev->sensor.id_dual, dev->regs + ISSR1);
	fimc_is_hw_set_intgr0_gd0(dev);
}

int fimc_is_hw_get_sensor_num(struct fimc_is_dev *dev)
{
	u32 cfg = readl(dev->regs + ISSR11);
	if (dev->sensor_num == cfg)
		return 0;
	else
		return cfg;
}

void fimc_is_hw_set_debug_level(struct fimc_is_dev *dev,
				int target,
				int module,
				int level)
{
	fimc_is_hw_wait_intmsr0_intmsd0(dev);
	writel(HIC_MSG_CONFIG, dev->regs + ISSR0);
	writel(dev->sensor.id_dual, dev->regs + ISSR1);

	writel(target, dev->regs + ISSR2);
	writel(module, dev->regs + ISSR3);
	writel(level, dev->regs + ISSR4);
	fimc_is_hw_set_intgr0_gd0(dev);
}

int fimc_is_hw_set_param(struct fimc_is_dev *dev)
{
	fimc_is_hw_wait_intmsr0_intmsd0(dev);
	writel(HIC_SET_PARAMETER, dev->regs + ISSR0);
	writel(dev->sensor.id_dual, dev->regs + ISSR1);

	writel(dev->scenario_id, dev->regs + ISSR2);

	writel(atomic_read(&dev->p_region_num), dev->regs + ISSR3);
	writel(dev->p_region_index1, dev->regs + ISSR4);
	writel(dev->p_region_index2, dev->regs + ISSR5);
	dbg("### set param\n");
	dbg("cmd :0x%08x\n", HIC_SET_PARAMETER);
	dbg("senorID :0x%08x\n", dev->sensor.id_dual);
	dbg("parma1 :0x%08x\n", dev->scenario_id);
	dbg("parma2 :0x%08x\n", atomic_read(&dev->p_region_num));
	dbg("parma3 :0x%08x\n", (unsigned int)dev->p_region_index1);
	dbg("parma4 :0x%08x\n", (unsigned int)dev->p_region_index2);

	fimc_is_hw_set_intgr0_gd0(dev);
	return 0;
}

int fimc_is_hw_update_bufmask(struct fimc_is_dev *dev, unsigned int dev_num)
{
	int buf_mask;
	int i = 0;
	int cnt = 0;

	buf_mask = dev->video[dev_num].buf_mask;

	for (i = 0; i < 16; i++) {
		if (((buf_mask & (1 << i)) >> i) == 1)
			cnt++;
	}
	dbg("dev_num: %u, buf_mask: %#x, cnt: %d\n", dev_num, buf_mask, cnt);

	if (cnt == 1) {
		err("ERR: Not enough buffers[dev_num: %u, buf_mask: %#x]\n",
							dev_num, buf_mask);
		goto done;
	}

	switch (dev_num) {
	case 0: /* Bayer */
		if (readl(dev->regs + ISSR23) != 0x0)
			dbg("WARN: Bayer buffer mask is unchecked\n");

		writel(buf_mask, dev->regs + ISSR23);
		break;
	case 1:  /* Scaler-C */
		if (readl(dev->regs + ISSR31) != 0x0)
			dbg("WARN: Scaler-C buffer mask is unchecked\n");

		writel(buf_mask, dev->regs + ISSR31);
		break;
	case 2: /* 3DNR */
		if (readl(dev->regs + ISSR39) != 0x0)
			dbg("WARN: 3DNR buffer mask is unchecked\n");

		writel(buf_mask, dev->regs + ISSR39);
		break;
	case 3: /* Scaler-P */
		if (readl(dev->regs + ISSR47) != 0x0)
			dbg("WARN: Scaler-P buffer mask is unchecked\n");

		writel(buf_mask, dev->regs + ISSR47);
		break;
	default:
		return -EINVAL;
	}

done:
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
		writel(dev->sensor.id_dual, dev->regs + ISSR1);
		fimc_is_hw_set_intgr0_gd0(dev);
	} else {
		fimc_is_hw_wait_intmsr0_intmsd0(dev);
		writel(HIC_STREAM_OFF, dev->regs + ISSR0);
		writel(dev->sensor.id_dual, dev->regs + ISSR1);
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
		writel(dev->sensor.id_dual, dev->regs + ISSR1);
		fimc_is_hw_set_intgr0_gd0(dev);
		break;
	case IS_MODE_PREVIEW_VIDEO:
		dev->scenario_id = ISS_PREVIEW_VIDEO;
		fimc_is_hw_wait_intmsr0_intmsd0(dev);
		clear_bit(IS_ST_RUN, &dev->state);
		set_bit(IS_ST_CHANGE_MODE, &dev->state);
		writel(HIC_PREVIEW_VIDEO, dev->regs + ISSR0);
		writel(dev->sensor.id_dual, dev->regs + ISSR1);
		fimc_is_hw_set_intgr0_gd0(dev);
		break;
	case IS_MODE_CAPTURE_STILL:
		dev->scenario_id = ISS_CAPTURE_STILL;
		fimc_is_hw_wait_intmsr0_intmsd0(dev);
		clear_bit(IS_ST_RUN, &dev->state);
		set_bit(IS_ST_CHANGE_MODE, &dev->state);
		writel(HIC_CAPTURE_STILL, dev->regs + ISSR0);
		writel(dev->sensor.id_dual, dev->regs + ISSR1);
		fimc_is_hw_set_intgr0_gd0(dev);
		break;
	case IS_MODE_CAPTURE_VIDEO:
		dev->scenario_id = ISS_CAPTURE_VIDEO;
		fimc_is_hw_wait_intmsr0_intmsd0(dev);
		clear_bit(IS_ST_RUN, &dev->state);
		set_bit(IS_ST_CHANGE_MODE, &dev->state);
		writel(HIC_CAPTURE_VIDEO, dev->regs + ISSR0);
		writel(dev->sensor.id_dual, dev->regs + ISSR1);
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
	dev->sensor.width =
		init_val_isp_preview_still.otf_input.width;
	dev->sensor.width =
		init_val_isp_preview_still.otf_input.height;
	IS_ISP_SET_PARAM_OTF_INPUT_FORMAT(dev,
		init_val_isp_preview_still.otf_input.format);
	IS_ISP_SET_PARAM_OTF_INPUT_BITWIDTH(dev,
		init_val_isp_preview_still.otf_input.bitwidth);
	IS_ISP_SET_PARAM_OTF_INPUT_ORDER(dev,
		init_val_isp_preview_still.otf_input.order);
	IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_X(dev,
		init_val_isp_preview_still.otf_input.crop_offset_x);
	IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_Y(dev,
		init_val_isp_preview_still.otf_input.crop_offset_y);
	IS_ISP_SET_PARAM_OTF_INPUT_CROP_WIDTH(dev,
		init_val_isp_preview_still.otf_input.crop_width);
	IS_ISP_SET_PARAM_OTF_INPUT_CROP_HEIGHT(dev,
		init_val_isp_preview_still.otf_input.crop_height);
	IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MIN(dev,
		init_val_isp_preview_still.otf_input.frametime_min);
	IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MAX(dev,
		init_val_isp_preview_still.otf_input.frametime_max);
	IS_ISP_SET_PARAM_OTF_INPUT_ERR(dev,
		init_val_isp_preview_still.otf_input.err);
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
	IS_ISP_SET_PARAM_AA_TOUCH_X(dev,
		init_val_isp_preview_still.aa.touch_x);
	IS_ISP_SET_PARAM_AA_TOUCH_Y(dev,
		init_val_isp_preview_still.aa.touch_y);
	IS_ISP_SET_PARAM_AA_MANUAL_AF(dev,
		init_val_isp_preview_still.aa.manual_af_setting);
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
#ifdef DZOOM_EVT0
	IS_ISP_SET_PARAM_DMA_OUTPUT1_BUFFER_NUMBER(dev,
		1);
	dev->is_p_region->shared[100] = (u32)dev->mem.dvaddr_isp;
	IS_ISP_SET_PARAM_DMA_OUTPUT1_BUFFER_ADDRESS(dev,
		(u32)dev->mem.dvaddr_shared + 100*sizeof(u32));
	dbg("ISP buf daddr : 0x%08x\n", dev->mem.dvaddr_isp);
	dbg("ISP buf kaddr : 0x%08x\n", dev->mem.kvaddr_isp);
#else
	IS_ISP_SET_PARAM_DMA_OUTPUT1_BUFFER_NUMBER(dev,
		init_val_isp_preview_still.dma1_output.buffer_number);
	IS_ISP_SET_PARAM_DMA_OUTPUT1_BUFFER_ADDRESS(dev,
		init_val_isp_preview_still.dma1_output.buffer_address);
#endif
	IS_ISP_SET_PARAM_DMA_OUTPUT1_MASK(dev,
		init_val_isp_preview_still.dma1_output.dma_out_mask);
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
	IS_ISP_SET_PARAM_DMA_OUTPUT2_MASK(dev,
		init_val_isp_preview_still.dma2_output.dma_out_mask);
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
	length = init_val_drc_preview_still.otf_output.width*
			init_val_drc_preview_still.otf_output.height;
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

	IS_SCALERC_SET_PARAM_INPUT_CROP_CMD(dev,
		init_val_scalerc_preview_still.input_crop.cmd);
	IS_SCALERC_SET_PARAM_INPUT_CROP_POS_X(dev,
		init_val_scalerc_preview_still.input_crop.pos_x);
	IS_SCALERC_SET_PARAM_INPUT_CROP_POS_Y(dev,
		init_val_scalerc_preview_still.input_crop.pos_y);
	IS_SCALERC_SET_PARAM_INPUT_CROP_WIDTH(dev,
		init_val_scalerc_preview_still.input_crop.crop_width);
	IS_SCALERC_SET_PARAM_INPUT_CROP_HEIGHT(dev,
		init_val_scalerc_preview_still.input_crop.crop_height);
	IS_SCALERC_SET_PARAM_INPUT_CROP_IN_WIDTH(dev,
		init_val_scalerc_preview_still.input_crop.in_width);
	IS_SCALERC_SET_PARAM_INPUT_CROP_IN_HEIGHT(dev,
		init_val_scalerc_preview_still.input_crop.in_height);
	IS_SCALERC_SET_PARAM_INPUT_CROP_OUT_WIDTH(dev,
		init_val_scalerc_preview_still.input_crop.out_width);
	IS_SCALERC_SET_PARAM_INPUT_CROP_OUT_HEIGHT(dev,
		init_val_scalerc_preview_still.input_crop.out_height);
	IS_SCALERC_SET_PARAM_INPUT_CROP_ERR(dev,
		init_val_scalerc_preview_still.input_crop.err);
	IS_SET_PARAM_BIT(dev, PARAM_SCALERC_INPUT_CROP);
	IS_INC_PARAM_NUM(dev);

	IS_SCALERC_SET_PARAM_OUTPUT_CROP_CMD(dev,
		init_val_scalerc_preview_still.output_crop.cmd);
	IS_SCALERC_SET_PARAM_OUTPUT_CROP_POS_X(dev,
		init_val_scalerc_preview_still.output_crop.pos_x);
	IS_SCALERC_SET_PARAM_OUTPUT_CROP_POS_Y(dev,
		init_val_scalerc_preview_still.output_crop.pos_y);
	IS_SCALERC_SET_PARAM_OUTPUT_CROP_CROP_WIDTH(dev,
		init_val_scalerc_preview_still.output_crop.crop_width);
	IS_SCALERC_SET_PARAM_OUTPUT_CROP_CROP_HEIGHT(dev,
		init_val_scalerc_preview_still.output_crop.crop_height);
	IS_SCALERC_SET_PARAM_OUTPUT_CROPG_FORMAT(dev,
		init_val_scalerc_preview_still.output_crop.format);
	IS_SCALERC_SET_PARAM_OUTPUT_CROP_ERR(dev,
		init_val_scalerc_preview_still.output_crop.err);
	IS_SET_PARAM_BIT(dev, PARAM_SCALERC_OUTPUT_CROP);
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
	IS_SCALERC_SET_PARAM_DMA_OUTPUT_MASK(dev,
		(0xff&0xffffffff));
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
	IS_TDNR_SET_PARAM_DMA_OUTPUT_MASK(dev,
		(0xff&0xffffffff));
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

	IS_SCALERP_SET_PARAM_INPUT_CROP_CMD(dev,
		init_val_scalerp_preview_still.input_crop.cmd);
	IS_SCALERP_SET_PARAM_INPUT_CROP_POS_X(dev,
		init_val_scalerp_preview_still.input_crop.pos_x);
	IS_SCALERP_SET_PARAM_INPUT_CROP_POS_Y(dev,
		init_val_scalerp_preview_still.input_crop.pos_y);
	IS_SCALERP_SET_PARAM_INPUT_CROP_WIDTH(dev,
		init_val_scalerp_preview_still.input_crop.crop_width);
	IS_SCALERP_SET_PARAM_INPUT_CROP_HEIGHT(dev,
		init_val_scalerp_preview_still.input_crop.crop_height);
	IS_SCALERP_SET_PARAM_INPUT_CROP_IN_WIDTH(dev,
		init_val_scalerp_preview_still.input_crop.in_width);
	IS_SCALERP_SET_PARAM_INPUT_CROP_IN_HEIGHT(dev,
		init_val_scalerp_preview_still.input_crop.in_height);
	IS_SCALERP_SET_PARAM_INPUT_CROP_OUT_WIDTH(dev,
		init_val_scalerp_preview_still.input_crop.out_width);
	IS_SCALERP_SET_PARAM_INPUT_CROP_OUT_HEIGHT(dev,
		init_val_scalerp_preview_still.input_crop.out_height);
	IS_SCALERP_SET_PARAM_INPUT_CROP_ERR(dev,
		init_val_scalerp_preview_still.input_crop.err);
	IS_SET_PARAM_BIT(dev, PARAM_SCALERP_INPUT_CROP);
	IS_INC_PARAM_NUM(dev);

	IS_SCALERP_SET_PARAM_OUTPUT_CROP_CMD(dev,
		init_val_scalerp_preview_still.output_crop.cmd);
	IS_SCALERP_SET_PARAM_OUTPUT_CROP_POS_X(dev,
		init_val_scalerp_preview_still.output_crop.pos_x);
	IS_SCALERP_SET_PARAM_OUTPUT_CROP_POS_Y(dev,
		init_val_scalerp_preview_still.output_crop.pos_y);
	IS_SCALERP_SET_PARAM_OUTPUT_CROP_CROP_WIDTH(dev,
		init_val_scalerp_preview_still.output_crop.crop_width);
	IS_SCALERP_SET_PARAM_OUTPUT_CROP_CROP_HEIGHT(dev,
		init_val_scalerp_preview_still.output_crop.crop_height);
	IS_SCALERP_SET_PARAM_OUTPUT_CROPG_FORMAT(dev,
		init_val_scalerp_preview_still.output_crop.format);
	IS_SCALERP_SET_PARAM_OUTPUT_CROP_ERR(dev,
		init_val_scalerp_preview_still.output_crop.err);
	IS_SET_PARAM_BIT(dev, PARAM_SCALERP_OUTPUT_CROP);
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
	IS_SCALERP_SET_PARAM_DMA_OUTPUT_MASK(dev,
		(0xff&0xffffffff));
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
}

int fimc_is_hw_change_size(struct fimc_is_dev *dev)
{
	u32 front_width, front_height, back_width, back_height;
	u32 dis_width, dis_height;
	u32 crop_width = 0, crop_height = 0, crop_x = 0, crop_y = 0;
	u32 front_crop_ratio, back_crop_ratio;

	/* ISP */
	IS_ISP_SET_PARAM_OTF_INPUT_WIDTH(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.width);
	IS_ISP_SET_PARAM_OTF_INPUT_HEIGHT(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.height);
	IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_INPUT);
	IS_INC_PARAM_NUM(dev);

	IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.width);
	IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.height);
	IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
	IS_INC_PARAM_NUM(dev);

	IS_ISP_SET_PARAM_OTF_OUTPUT_WIDTH(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.width);
	IS_ISP_SET_PARAM_OTF_OUTPUT_HEIGHT(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.height);
	IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_OUTPUT);
	IS_INC_PARAM_NUM(dev);

	IS_ISP_SET_PARAM_DMA_OUTPUT1_WIDTH(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.width);
	IS_ISP_SET_PARAM_DMA_OUTPUT1_HEIGHT(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.height);
	IS_SET_PARAM_BIT(dev, PARAM_ISP_DMA1_INPUT);
	IS_INC_PARAM_NUM(dev);

	IS_ISP_SET_PARAM_DMA_OUTPUT2_WIDTH(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.width);
	IS_ISP_SET_PARAM_DMA_OUTPUT2_HEIGHT(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.height);
	IS_SET_PARAM_BIT(dev, PARAM_ISP_DMA2_INPUT);
	IS_INC_PARAM_NUM(dev);

	/* DRC */
	IS_DRC_SET_PARAM_OTF_INPUT_WIDTH(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.width);
	IS_DRC_SET_PARAM_OTF_INPUT_HEIGHT(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.height);
	IS_SET_PARAM_BIT(dev, PARAM_DRC_OTF_INPUT);
	IS_INC_PARAM_NUM(dev);

	IS_DRC_SET_PARAM_DMA_INPUT_WIDTH(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.width);
	IS_DRC_SET_PARAM_DMA_INPUT_HEIGHT(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.height);
	IS_SET_PARAM_BIT(dev, PARAM_DRC_DMA_INPUT);
	IS_INC_PARAM_NUM(dev);

	IS_DRC_SET_PARAM_OTF_OUTPUT_WIDTH(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.width);
	IS_DRC_SET_PARAM_OTF_OUTPUT_HEIGHT(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.height);
	IS_SET_PARAM_BIT(dev, PARAM_DRC_OTF_OUTPUT);
	IS_INC_PARAM_NUM(dev);

	/* ScalerC */
	front_width = dev->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.width;
	front_height = dev->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.height;

	back_width = dev->video[FIMC_IS_VIDEO_NUM_SCALERP].frame.width;
	back_height = dev->video[FIMC_IS_VIDEO_NUM_SCALERP].frame.height;

	IS_SCALERC_SET_PARAM_OTF_INPUT_WIDTH(dev,
		front_width);
	IS_SCALERC_SET_PARAM_OTF_INPUT_HEIGHT(dev,
		front_height);
	IS_SET_PARAM_BIT(dev, PARAM_SCALERC_OTF_INPUT);
	IS_INC_PARAM_NUM(dev);

	IS_SCALERC_SET_PARAM_INPUT_CROP_IN_WIDTH(dev,
		front_width);
	IS_SCALERC_SET_PARAM_INPUT_CROP_IN_HEIGHT(dev,
		front_height);

	front_crop_ratio = front_width * 1000 / front_height;
	back_crop_ratio = back_width * 1000 / back_height;

	if (front_crop_ratio == back_crop_ratio) {
		crop_width = front_width;
		crop_height = front_height;

	} else if (front_crop_ratio < back_crop_ratio) {
		crop_width = front_width;
		crop_height = (front_width
				* (1000 * 100 / back_crop_ratio)) / 100;
		crop_width = ALIGN(crop_width, 8);
		crop_height = ALIGN(crop_height, 8);

	} else if (front_crop_ratio > back_crop_ratio) {
		crop_height = front_height;
		crop_width = (front_height
				* (back_crop_ratio * 100 / 1000)) / 100 ;
		crop_width = ALIGN(crop_width, 8);
		crop_height = ALIGN(crop_height, 8);
	}

	if (dev->back.dis_on) {
		dis_width = back_width * 125 / 100;
		dis_height = back_height * 125 / 100;
	} else {
		dis_width = back_width;
		dis_height = back_height;
	}

	IS_SCALERC_SET_PARAM_INPUT_CROP_OUT_WIDTH(dev,
		crop_width);
	IS_SCALERC_SET_PARAM_INPUT_CROP_OUT_HEIGHT(dev,
		crop_height);

	dbg("calulate crop size\n");
	dbg("front w: %d front h: %d\n", front_width, front_height);
	dbg("dis w: %d dis h: %d\n", dis_width, dis_height);
	dbg("back w: %d back h: %d\n", back_width, back_height);

	dbg("front_crop_ratio: %d back_crop_ratio: %d\n",
			front_crop_ratio, back_crop_ratio);

	crop_x = (front_width - crop_width) / 2;
	crop_y = (front_height - crop_height) / 2;
	crop_x &= 0xffe;
	crop_y &= 0xffe;

	dev->sensor.width = front_width;
	dev->sensor.height = front_height;
	dev->front.width = front_width;
	dev->front.height = front_height;
	dev->back.width = back_width;
	dev->back.height = back_height;
	dev->back.dis_width = dis_width;
	dev->back.dis_height = dis_height;

	dbg("crop w: %d crop h: %d\n", crop_width, crop_height);
	dbg("crop x: %d crop y: %d\n", crop_x, crop_y);

	IS_SCALERC_SET_PARAM_INPUT_CROP_WIDTH(dev,
		crop_width);
	IS_SCALERC_SET_PARAM_INPUT_CROP_HEIGHT(dev,
		crop_height);
	IS_SCALERC_SET_PARAM_INPUT_CROP_POS_X(dev,
		crop_x);
	IS_SCALERC_SET_PARAM_INPUT_CROP_POS_Y(dev,
		crop_y);

	IS_SET_PARAM_BIT(dev, PARAM_SCALERC_INPUT_CROP);
	IS_INC_PARAM_NUM(dev);

	IS_SCALERC_SET_PARAM_OUTPUT_CROP_CROP_WIDTH(dev,
		dis_width);
	IS_SCALERC_SET_PARAM_OUTPUT_CROP_CROP_HEIGHT(dev,
		dis_height);
	IS_SET_PARAM_BIT(dev, PARAM_SCALERC_OUTPUT_CROP);
	IS_INC_PARAM_NUM(dev);

	IS_SCALERC_SET_PARAM_OTF_OUTPUT_WIDTH(dev,
		dis_width);
	IS_SCALERC_SET_PARAM_OTF_OUTPUT_HEIGHT(dev,
		dis_height);
	IS_SET_PARAM_BIT(dev, PARAM_SCALERC_OTF_OUTPUT);
	IS_INC_PARAM_NUM(dev);

	IS_SCALERC_SET_PARAM_DMA_OUTPUT_WIDTH(dev,
		front_width);
	IS_SCALERC_SET_PARAM_DMA_OUTPUT_HEIGHT(dev,
		front_height);
	if ((front_width != dis_width) || (front_height != dis_height))
		IS_SCALERC_SET_PARAM_DMA_OUTPUT_OUTPATH(dev,
			2);  /* unscaled image */
	else
		IS_SCALERC_SET_PARAM_DMA_OUTPUT_OUTPATH(dev,
			1); /* scaled image */
	IS_SET_PARAM_BIT(dev, PARAM_SCALERC_DMA_OUTPUT);
	IS_INC_PARAM_NUM(dev);

	/* ODC */
	IS_ODC_SET_PARAM_OTF_INPUT_WIDTH(dev,
		dis_width);
	IS_ODC_SET_PARAM_OTF_INPUT_HEIGHT(dev,
		dis_height);
	IS_SET_PARAM_BIT(dev, PARAM_ODC_OTF_INPUT);
	IS_INC_PARAM_NUM(dev);

	IS_ODC_SET_PARAM_OTF_OUTPUT_WIDTH(dev,
		dis_width);
	IS_ODC_SET_PARAM_OTF_OUTPUT_HEIGHT(dev,
		dis_height);
	IS_SET_PARAM_BIT(dev, PARAM_ODC_OTF_OUTPUT);
	IS_INC_PARAM_NUM(dev);

	/* DIS */
	IS_DIS_SET_PARAM_OTF_INPUT_WIDTH(dev,
		dis_width);
	IS_DIS_SET_PARAM_OTF_INPUT_HEIGHT(dev,
		dis_height);
	IS_SET_PARAM_BIT(dev, PARAM_DIS_OTF_INPUT);
	IS_INC_PARAM_NUM(dev);

	IS_DIS_SET_PARAM_OTF_OUTPUT_WIDTH(dev,
		back_width);
	IS_DIS_SET_PARAM_OTF_OUTPUT_HEIGHT(dev,
		back_height);
	IS_SET_PARAM_BIT(dev, PARAM_DIS_OTF_OUTPUT);
	IS_INC_PARAM_NUM(dev);

	/* 3DNR */
	IS_TDNR_SET_PARAM_OTF_INPUT_WIDTH(dev,
		back_width);
	IS_TDNR_SET_PARAM_OTF_INPUT_HEIGHT(dev,
		back_height);
	IS_SET_PARAM_BIT(dev, PARAM_TDNR_OTF_INPUT);
	IS_INC_PARAM_NUM(dev);

	IS_TDNR_SET_PARAM_OTF_OUTPUT_WIDTH(dev,
		back_width);
	IS_TDNR_SET_PARAM_OTF_OUTPUT_HEIGHT(dev,
		back_height);
	IS_SET_PARAM_BIT(dev, PARAM_TDNR_OTF_OUTPUT);
	IS_INC_PARAM_NUM(dev);

	IS_TDNR_SET_PARAM_DMA_OUTPUT_WIDTH(dev,
		back_width);
	IS_TDNR_SET_PARAM_DMA_OUTPUT_HEIGHT(dev,
		back_height);
	IS_SET_PARAM_BIT(dev, PARAM_TDNR_DMA_OUTPUT);
	IS_INC_PARAM_NUM(dev);

	/* ScalerP */
	IS_SCALERP_SET_PARAM_OTF_INPUT_WIDTH(dev,
		back_width);
	IS_SCALERP_SET_PARAM_OTF_INPUT_HEIGHT(dev,
		back_height);
	IS_SET_PARAM_BIT(dev, PARAM_SCALERP_OTF_INPUT);
	IS_INC_PARAM_NUM(dev);

	IS_SCALERP_SET_PARAM_INPUT_CROP_IN_WIDTH(dev,
		back_width);
	IS_SCALERP_SET_PARAM_INPUT_CROP_IN_HEIGHT(dev,
		back_height);
	IS_SCALERP_SET_PARAM_INPUT_CROP_WIDTH(dev,
		back_width);
	IS_SCALERP_SET_PARAM_INPUT_CROP_HEIGHT(dev,
		back_height);
	IS_SCALERP_SET_PARAM_INPUT_CROP_OUT_WIDTH(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERP].frame.width);
	IS_SCALERP_SET_PARAM_INPUT_CROP_OUT_HEIGHT(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERP].frame.height);
	IS_SET_PARAM_BIT(dev, PARAM_SCALERP_INPUT_CROP);
	IS_INC_PARAM_NUM(dev);

	IS_SCALERP_SET_PARAM_OUTPUT_CROP_CROP_WIDTH(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERP].frame.width);
	IS_SCALERP_SET_PARAM_OUTPUT_CROP_CROP_HEIGHT(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERP].frame.height);
	IS_SET_PARAM_BIT(dev, PARAM_SCALERP_OUTPUT_CROP);
	IS_INC_PARAM_NUM(dev);

	IS_SCALERP_SET_PARAM_OTF_OUTPUT_WIDTH(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERP].frame.width);
	IS_SCALERP_SET_PARAM_OTF_OUTPUT_HEIGHT(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERP].frame.height);
	IS_SET_PARAM_BIT(dev, PARAM_SCALERP_OTF_OUTPUT);
	IS_INC_PARAM_NUM(dev);

	IS_SCALERP_SET_PARAM_DMA_OUTPUT_WIDTH(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERP].frame.width);
	IS_SCALERP_SET_PARAM_DMA_OUTPUT_HEIGHT(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERP].frame.height);
	IS_SET_PARAM_BIT(dev, PARAM_SCALERP_DMA_OUTPUT);
	IS_INC_PARAM_NUM(dev);

	/* FD */
	IS_FD_SET_PARAM_OTF_INPUT_WIDTH(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERP].frame.width);
	IS_FD_SET_PARAM_OTF_INPUT_HEIGHT(dev,
		dev->video[FIMC_IS_VIDEO_NUM_SCALERP].frame.height);
	IS_SET_PARAM_BIT(dev, PARAM_FD_OTF_INPUT);
	IS_INC_PARAM_NUM(dev);

	return 0;
}

void fimc_is_hw_set_default_size(struct fimc_is_dev *dev, int  sensor_id)
{
	switch (sensor_id) {
	case SENSOR_NAME_S5K6A3:
		dev->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.width =
			DEFAULT_6A3_STILLSHOT_WIDTH;
		dev->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.height =
			DEFAULT_6A3_STILLSHOT_HEIGHT;
		dev->video[FIMC_IS_VIDEO_NUM_SCALERP].frame.width =
			DEFAULT_6A3_PREVIEW_WIDTH;
		dev->video[FIMC_IS_VIDEO_NUM_SCALERP].frame.height =
			DEFAULT_6A3_PREVIEW_HEIGHT;
		dev->video[FIMC_IS_VIDEO_NUM_3DNR].frame.width =
			DEFAULT_6A3_VIDEO_WIDTH;
		dev->video[FIMC_IS_VIDEO_NUM_3DNR].frame.height =
			DEFAULT_6A3_VIDEO_HEIGHT;
		break;
	case SENSOR_NAME_S5K4E5:
		dev->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.width =
			DEFAULT_4E5_STILLSHOT_WIDTH;
		dev->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.height =
			DEFAULT_4E5_STILLSHOT_HEIGHT;
		dev->video[FIMC_IS_VIDEO_NUM_SCALERP].frame.width =
			DEFAULT_4E5_PREVIEW_WIDTH;
		dev->video[FIMC_IS_VIDEO_NUM_SCALERP].frame.height =
			DEFAULT_4E5_PREVIEW_HEIGHT;
		dev->video[FIMC_IS_VIDEO_NUM_3DNR].frame.width =
			DEFAULT_4E5_VIDEO_WIDTH;
		dev->video[FIMC_IS_VIDEO_NUM_3DNR].frame.height =
			DEFAULT_4E5_VIDEO_HEIGHT;
		break;
	default:
		dev->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.width =
			DEFAULT_CAPTURE_STILL_WIDTH;
		dev->video[FIMC_IS_VIDEO_NUM_SCALERC].frame.height =
			DEFAULT_CAPTURE_STILL_HEIGHT;
		dev->video[FIMC_IS_VIDEO_NUM_SCALERP].frame.width =
			DEFAULT_PREVIEW_STILL_WIDTH;
		dev->video[FIMC_IS_VIDEO_NUM_SCALERP].frame.height =
			DEFAULT_PREVIEW_STILL_HEIGHT;
		dev->video[FIMC_IS_VIDEO_NUM_3DNR].frame.width =
			DEFAULT_CAPTURE_VIDEO_WIDTH;
		dev->video[FIMC_IS_VIDEO_NUM_3DNR].frame.height =
			DEFAULT_CAPTURE_VIDEO_HEIGHT;
		break;
	}
}
