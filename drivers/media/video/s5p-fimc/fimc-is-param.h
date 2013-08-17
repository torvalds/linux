/*
 * Samsung Exynos4 SoC series FIMC-IS slave interface driver
 *
 * Parameter region
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Younghwan Joo <yhwan.joo@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FIMC_IS_PARAMS_H_
#define FIMC_IS_PARAMS_H_

#define IS_REGION_VER		124  /* IS REGION VERSION 1.24 */

/* MACROs */
#define IS_UPDATE_PARAM_BIT(dev, num) \
	(num >= 32 ? set_bit((num - 32), \
	&dev->cfg_param[dev->scenario_id].p_region_index2) \
	: set_bit(num, &dev->cfg_param[dev->scenario_id].p_region_index1))

#define IS_UPDATE_PARAM_NUM(dev) \
		atomic_inc(&dev->cfg_param[dev->scenario_id].p_region_num)

#define IS_SET_PARAM_BIT(dev, num) \
	(num >= 32 ? set_bit((num - 32), &dev->p_region_index2) \
		: set_bit(num, &dev->p_region_index1))

#define IS_INC_PARAM_NUM(dev)		atomic_inc(&dev->p_region_num)

#define IS_PARAM_GLOBAL		(dev->is_p_region->parameter.global)
#define IS_PARAM_ISP		(dev->is_p_region->parameter.isp)
#define IS_PARAM_DRC		(dev->is_p_region->parameter.drc)
#define IS_PARAM_FD		(dev->is_p_region->parameter.fd)
#define IS_HEADER		(dev->is_p_region->header)
#define IS_FACE			(dev->is_p_region->face)
#define IS_PARAM_SIZE		(FIMC_IS_REGION_SIZE + 1)

/* Global control */
#define IS_SET_PARAM_GLOBAL_SHOTMODE_CMD(dev, x) \
		(dev->is_p_region->parameter.global.shotmode.cmd = x)
#define IS_SET_PARAM_GLOBAL_SHOTMODE_SKIPFRAMES(dev, x) \
		(dev->is_p_region->parameter.global.shotmode.skip_frames = x)

/* Sensor control */
#define IS_SENSOR_SET_FRAME_RATE(dev, x) \
		(dev->is_p_region->parameter.sensor.frame_rate.frame_rate = x)

/* ISP Macros */
#define IS_ISP_SET_PARAM_CONTROL_CMD(dev, x) \
		(dev->is_p_region->parameter.isp.control.cmd = x)
#define IS_ISP_SET_PARAM_CONTROL_BYPASS(dev, x) \
		(dev->is_p_region->parameter.isp.control.bypass = x)
#define IS_ISP_SET_PARAM_CONTROL_ERR(dev, x) \
		(dev->is_p_region->parameter.isp.control.err = x)

#define IS_ISP_SET_PARAM_OTF_INPUT_CMD(dev, x) \
		(dev->is_p_region->parameter.isp.otf_input.cmd = x)
#define IS_ISP_SET_PARAM_OTF_INPUT_WIDTH(dev, x) \
		(dev->is_p_region->parameter.isp.otf_input.width = x)
#define IS_ISP_SET_PARAM_OTF_INPUT_HEIGHT(dev, x) \
		(dev->is_p_region->parameter.isp.otf_input.height = x)
#define IS_ISP_SET_PARAM_OTF_INPUT_FORMAT(dev, x) \
		(dev->is_p_region->parameter.isp.otf_input.format = x)
#define IS_ISP_SET_PARAM_OTF_INPUT_BITWIDTH(dev, x) \
		(dev->is_p_region->parameter.isp.otf_input.bitwidth = x)
#define IS_ISP_SET_PARAM_OTF_INPUT_ORDER(dev, x) \
		(dev->is_p_region->parameter.isp.otf_input.order = x)
#define IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_X(dev, x) \
		(dev->is_p_region->parameter.isp.otf_input.crop_offset_x = x)
#define IS_ISP_SET_PARAM_OTF_INPUT_CROP_OFFSET_Y(dev, x) \
		(dev->is_p_region->parameter.isp.otf_input.crop_offset_y = x)
#define IS_ISP_SET_PARAM_OTF_INPUT_CROP_WIDTH(dev, x) \
		(dev->is_p_region->parameter.isp.otf_input.crop_width = x)
#define IS_ISP_SET_PARAM_OTF_INPUT_CROP_HEIGHT(dev, x) \
		(dev->is_p_region->parameter.isp.otf_input.crop_height = x)
#define IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MIN(dev, x) \
		(dev->is_p_region->parameter.isp.otf_input.frametime_min = x)
#define IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MAX(dev, x) \
		(dev->is_p_region->parameter.isp.otf_input.frametime_max = x)
#define IS_ISP_SET_PARAM_OTF_INPUT_ERR(dev, x) \
		(dev->is_p_region->parameter.isp.otf_input.err = x)

#define IS_ISP_SET_PARAM_DMA_INPUT1_CMD(dev, x) \
		(dev->is_p_region->parameter.isp.dma1_input.cmd = x)
#define IS_ISP_SET_PARAM_DMA_INPUT1_WIDTH(dev, x) \
		(dev->is_p_region->parameter.isp.dma1_input.width = x)
#define IS_ISP_SET_PARAM_DMA_INPUT1_HEIGHT(dev, x) \
		(dev->is_p_region->parameter.isp.dma1_input.height = x)
#define IS_ISP_SET_PARAM_DMA_INPUT1_FORMAT(dev, x) \
		(dev->is_p_region->parameter.isp.dma1_input.format = x)
#define IS_ISP_SET_PARAM_DMA_INPUT1_BITWIDTH(dev, x) \
		(dev->is_p_region->parameter.isp.dma1_input.bitwidth = x)
#define IS_ISP_SET_PARAM_DMA_INPUT1_PLANE(dev, x) \
		(dev->is_p_region->parameter.isp.dma1_input.plane = x)
#define IS_ISP_SET_PARAM_DMA_INPUT1_ORDER(dev, x) \
		(dev->is_p_region->parameter.isp.dma1_input.order = x)
#define IS_ISP_SET_PARAM_DMA_INPUT1_BUFFERNUM(dev, x) \
		(dev->is_p_region->parameter.isp.dma1_input.buffer_number = x)
#define IS_ISP_SET_PARAM_DMA_INPUT1_BUFFERADDR(dev, x) \
		(dev->is_p_region->parameter.isp.dma1_input.buffer_address = x)
#define IS_ISP_SET_PARAM_DMA_INPUT1_ERR(dev, x) \
		(dev->is_p_region->parameter.isp.dma1_input.err = x)

#define IS_ISP_SET_PARAM_DMA_INPUT2_CMD(dev, x) \
		(dev->is_p_region->parameter.isp.dma2_input.cmd = x)
#define IS_ISP_SET_PARAM_DMA_INPUT2_WIDTH(dev, x) \
		(dev->is_p_region->parameter.isp.dma2_input.width = x)
#define IS_ISP_SET_PARAM_DMA_INPUT2_HEIGHT(dev, x) \
		(dev->is_p_region->parameter.isp.dma2_input.height = x)
#define IS_ISP_SET_PARAM_DMA_INPUT2_FORMAT(dev, x) \
		(dev->is_p_region->parameter.isp.dma2_input.format = x)
#define IS_ISP_SET_PARAM_DMA_INPUT2_BITWIDTH(dev, x) \
		(dev->is_p_region->parameter.isp.dma2_input.bitwidth = x)
#define IS_ISP_SET_PARAM_DMA_INPUT2_PLANE(dev, x) \
		(dev->is_p_region->parameter.isp.dma2_input.plane = x)
#define IS_ISP_SET_PARAM_DMA_INPUT2_ORDER(dev, x) \
		(dev->is_p_region->parameter.isp.dma2_input.order = x)
#define IS_ISP_SET_PARAM_DMA_INPUT2_BUFFERNUM(dev, x) \
		(dev->is_p_region->parameter.isp.dma2_input.buffer_number = x)
#define IS_ISP_SET_PARAM_DMA_INPUT2_BUFFERADDR(dev, x) \
		(dev->is_p_region->parameter.isp.dma2_input.buffer_address = x)
#define IS_ISP_SET_PARAM_DMA_INPUT2_ERR(dev, x) \
		(dev->is_p_region->parameter.isp.dma2_input.err = x)

#define IS_ISP_SET_PARAM_AA_CMD(dev, x) \
		(dev->is_p_region->parameter.isp.aa.cmd = x)
#define IS_ISP_SET_PARAM_AA_TARGET(dev, x) \
		(dev->is_p_region->parameter.isp.aa.target = x)
#define IS_ISP_SET_PARAM_AA_MODE(dev, x) \
		(dev->is_p_region->parameter.isp.aa.mode = x)
#define IS_ISP_SET_PARAM_AA_SCENE(dev, x) \
		(dev->is_p_region->parameter.isp.aa.scene = x)
#define IS_ISP_SET_PARAM_AA_SLEEP(dev, x) \
		(dev->is_p_region->parameter.isp.aa.sleep = x)
#define IS_ISP_SET_PARAM_AA_FACE(dev, x) \
		(dev->is_p_region->parameter.isp.aa.face = x)
#define IS_ISP_SET_PARAM_AA_TOUCH_X(dev, x) \
		(dev->is_p_region->parameter.isp.aa.touch_x = x)
#define IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, x) \
		(dev->is_p_region->parameter.isp.aa.touch_y = x)
#define IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, x) \
		(dev->is_p_region->parameter.isp.aa.manual_af_setting = x)
#define IS_ISP_SET_PARAM_AA_ERR(dev, x) \
		(dev->is_p_region->parameter.isp.aa.err = x)

#define IS_ISP_SET_PARAM_FLASH_CMD(dev, x) \
		(dev->is_p_region->parameter.isp.flash.cmd = x)
#define IS_ISP_SET_PARAM_FLASH_REDEYE(dev, x) \
		(dev->is_p_region->parameter.isp.flash.redeye = x)
#define IS_ISP_SET_PARAM_FLASH_ERR(dev, x) \
		(dev->is_p_region->parameter.isp.flash.err = x)

#define IS_ISP_SET_PARAM_AWB_CMD(dev, x) \
		(dev->is_p_region->parameter.isp.awb.cmd = x)
#define IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev, x) \
		(dev->is_p_region->parameter.isp.awb.illumination = x)
#define IS_ISP_SET_PARAM_AWB_ERR(dev, x) \
		(dev->is_p_region->parameter.isp.awb.err = x)

#define IS_ISP_SET_PARAM_EFFECT_CMD(dev, x) \
		(dev->is_p_region->parameter.isp.effect.cmd = x)
#define IS_ISP_SET_PARAM_EFFECT_ERR(dev, x) \
		(dev->is_p_region->parameter.isp.effect.err = x)

#define IS_ISP_SET_PARAM_ISO_CMD(dev, x) \
		(dev->is_p_region->parameter.isp.iso.cmd = x)
#define IS_ISP_SET_PARAM_ISO_VALUE(dev, x) \
		(dev->is_p_region->parameter.isp.iso.value = x)
#define IS_ISP_SET_PARAM_ISO_ERR(dev, x) \
		(dev->is_p_region->parameter.isp.iso.err = x)

#define IS_ISP_SET_PARAM_ADJUST_CMD(dev, x) \
		(dev->is_p_region->parameter.isp.adjust.cmd = x)
#define IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, x) \
		(dev->is_p_region->parameter.isp.adjust.contrast = x)
#define IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, x) \
		(dev->is_p_region->parameter.isp.adjust.saturation = x)
#define IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, x) \
		(dev->is_p_region->parameter.isp.adjust.sharpness = x)
#define IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, x) \
		(dev->is_p_region->parameter.isp.adjust.exposure = x)
#define IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, x) \
		(dev->is_p_region->parameter.isp.adjust.brightness = x)
#define IS_ISP_SET_PARAM_ADJUST_HUE(dev, x) \
		(dev->is_p_region->parameter.isp.adjust.hue = x)
#define IS_ISP_SET_PARAM_ADJUST_ERR(dev, x) \
		(dev->is_p_region->parameter.isp.adjust.err = x)

#define IS_ISP_SET_PARAM_METERING_CMD(dev, x) \
		(dev->is_p_region->parameter.isp.metering.cmd = x)
#define IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, x) \
		(dev->is_p_region->parameter.isp.metering.win_pos_x = x)
#define IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, x) \
		(dev->is_p_region->parameter.isp.metering.win_pos_y = x)
#define IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, x) \
		(dev->is_p_region->parameter.isp.metering.win_width = x)
#define IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, x) \
		(dev->is_p_region->parameter.isp.metering.win_height = x)
#define IS_ISP_SET_PARAM_METERING_ERR(dev, x) \
		(dev->is_p_region->parameter.isp.metering.err = x)

#define IS_ISP_SET_PARAM_AFC_CMD(dev, x) \
		(dev->is_p_region->parameter.isp.afc.cmd = x)
#define IS_ISP_SET_PARAM_AFC_MANUAL(dev, x) \
		(dev->is_p_region->parameter.isp.afc.manual = x)
#define IS_ISP_SET_PARAM_AFC_ERR(dev, x) \
		(dev->is_p_region->parameter.isp.afc.err = x)

#define IS_ISP_SET_PARAM_OTF_OUTPUT_CMD(dev, x) \
		(dev->is_p_region->parameter.isp.otf_output.cmd = x)
#define IS_ISP_SET_PARAM_OTF_OUTPUT_WIDTH(dev, x) \
		(dev->is_p_region->parameter.isp.otf_output.width = x)
#define IS_ISP_SET_PARAM_OTF_OUTPUT_HEIGHT(dev, x) \
		(dev->is_p_region->parameter.isp.otf_output.height = x)
#define IS_ISP_SET_PARAM_OTF_OUTPUT_FORMAT(dev, x) \
		(dev->is_p_region->parameter.isp.otf_output.format = x)
#define IS_ISP_SET_PARAM_OTF_OUTPUT_BITWIDTH(dev, x) \
		(dev->is_p_region->parameter.isp.otf_output.bitwidth = x)
#define IS_ISP_SET_PARAM_OTF_OUTPUT_ORDER(dev, x) \
		(dev->is_p_region->parameter.isp.otf_output.order = x)
#define IS_ISP_SET_PARAM_OTF_OUTPUT_ERR(dev, x) \
		(dev->is_p_region->parameter.isp.otf_output.err = x)

#define IS_ISP_SET_PARAM_DMA_OUTPUT1_CMD(dev, x) \
		(dev->is_p_region->parameter.isp.dma1_output.cmd = x)
#define IS_ISP_SET_PARAM_DMA_OUTPUT1_WIDTH(dev, x) \
		(dev->is_p_region->parameter.isp.dma1_output.width = x)
#define IS_ISP_SET_PARAM_DMA_OUTPUT1_HEIGHT(dev, x) \
		(dev->is_p_region->parameter.isp.dma1_output.height = x)
#define IS_ISP_SET_PARAM_DMA_OUTPUT1_FORMAT(dev, x) \
		(dev->is_p_region->parameter.isp.dma1_output.format = x)
#define IS_ISP_SET_PARAM_DMA_OUTPUT1_BITWIDTH(dev, x) \
		(dev->is_p_region->parameter.isp.dma1_output.bitwidth = x)
#define IS_ISP_SET_PARAM_DMA_OUTPUT1_PLANE(dev, x) \
		(dev->is_p_region->parameter.isp.dma1_output.plane = x)
#define IS_ISP_SET_PARAM_DMA_OUTPUT1_ORDER(dev, x) \
		(dev->is_p_region->parameter.isp.dma1_output.order = x)
#define IS_ISP_SET_PARAM_DMA_OUTPUT1_BUFFER_NUMBER(dev, x) \
		(dev->is_p_region->parameter.isp.dma1_output.buffer_number = x)
#define IS_ISP_SET_PARAM_DMA_OUTPUT1_BUFFER_ADDRESS(dev, x) \
		(dev->is_p_region->parameter.isp.dma1_output.buffer_address = x)
#define IS_ISP_SET_PARAM_DMA_OUTPUT1_NODIFY_DMA_DONE(dev, x) \
		(dev->is_p_region->parameter.isp.dma1_output.notify_dma_done = x)
#define IS_ISP_SET_PARAM_DMA_OUTPUT1_ERR(dev, x) \
		(dev->is_p_region->parameter.isp.dma1_output.err = x)

#define IS_ISP_SET_PARAM_DMA_OUTPUT2_CMD(dev, x) \
		(dev->is_p_region->parameter.isp.dma2_output.cmd = x)
#define IS_ISP_SET_PARAM_DMA_OUTPUT2_WIDTH(dev, x) \
		(dev->is_p_region->parameter.isp.dma2_output.width = x)
#define IS_ISP_SET_PARAM_DMA_OUTPUT2_HEIGHT(dev, x) \
		(dev->is_p_region->parameter.isp.dma2_output.height = x)
#define IS_ISP_SET_PARAM_DMA_OUTPUT2_FORMAT(dev, x) \
		(dev->is_p_region->parameter.isp.dma2_output.format = x)
#define IS_ISP_SET_PARAM_DMA_OUTPUT2_BITWIDTH(dev, x) \
		(dev->is_p_region->parameter.isp.dma2_output.bitwidth = x)
#define IS_ISP_SET_PARAM_DMA_OUTPUT2_PLANE(dev, x) \
		(dev->is_p_region->parameter.isp.dma2_output.plane = x)
#define IS_ISP_SET_PARAM_DMA_OUTPUT2_ORDER(dev, x) \
		(dev->is_p_region->parameter.isp.dma2_output.order = x)
#define IS_ISP_SET_PARAM_DMA_OUTPUT2_BUFFER_NUMBER(dev, x) \
		(dev->is_p_region->parameter.isp.dma2_output.buffer_number = x)
#define IS_ISP_SET_PARAM_DMA_OUTPUT2_BUFFER_ADDRESS(dev, x) \
		(dev->is_p_region->parameter.isp.dma2_output.buffer_address = x)
#define IS_ISP_SET_PARAM_DMA_OUTPUT2_NODIFY_DMA_DONE(dev, x) \
		(dev->is_p_region->parameter.isp.dma2_output.notify_dma_done = x)
#define IS_ISP_SET_PARAM_DMA_OUTPUT2_ERR(dev, x) \
		(dev->is_p_region->parameter.isp.dma2_output.err = x)

/* DRC Macros */
#define IS_DRC_SET_PARAM_CONTROL_CMD(dev, x) \
		(dev->is_p_region->parameter.drc.control.cmd = x)
#define IS_DRC_SET_PARAM_CONTROL_BYPASS(dev, x) \
		(dev->is_p_region->parameter.drc.control.bypass = x)
#define IS_DRC_SET_PARAM_CONTROL_ERR(dev, x) \
		(dev->is_p_region->parameter.drc.control.err = x)

#define IS_DRC_SET_PARAM_OTF_INPUT_CMD(dev, x) \
		(dev->is_p_region->parameter.drc.otf_input.cmd = x)
#define IS_DRC_SET_PARAM_OTF_INPUT_WIDTH(dev, x) \
		(dev->is_p_region->parameter.drc.otf_input.width = x)
#define IS_DRC_SET_PARAM_OTF_INPUT_HEIGHT(dev, x) \
		(dev->is_p_region->parameter.drc.otf_input.height = x)
#define IS_DRC_SET_PARAM_OTF_INPUT_FORMAT(dev, x) \
		(dev->is_p_region->parameter.drc.otf_input.format = x)
#define IS_DRC_SET_PARAM_OTF_INPUT_BITWIDTH(dev, x) \
		(dev->is_p_region->parameter.drc.otf_input.bitwidth = x)
#define IS_DRC_SET_PARAM_OTF_INPUT_ORDER(dev, x) \
		(dev->is_p_region->parameter.drc.otf_input.order = x)
#define IS_DRC_SET_PARAM_OTF_INPUT_ERR(dev, x) \
		(dev->is_p_region->parameter.drc.otf_input.err = x)

#define IS_DRC_SET_PARAM_DMA_INPUT_CMD(dev, x) \
		(dev->is_p_region->parameter.drc.dma_input.cmd = x)
#define IS_DRC_SET_PARAM_DMA_INPUT_WIDTH(dev, x) \
		(dev->is_p_region->parameter.drc.dma_input.width = x)
#define IS_DRC_SET_PARAM_DMA_INPUT_HEIGHT(dev, x) \
		(dev->is_p_region->parameter.drc.dma_input.height = x)
#define IS_DRC_SET_PARAM_DMA_INPUT_FORMAT(dev, x) \
		(dev->is_p_region->parameter.drc.dma_input.format = x)
#define IS_DRC_SET_PARAM_DMA_INPUT_BITWIDTH(dev, x) \
		(dev->is_p_region->parameter.drc.dma_input.bitwidth = x)
#define IS_DRC_SET_PARAM_DMA_INPUT_PLANE(dev, x) \
		(dev->is_p_region->parameter.drc.dma_input.plane = x)
#define IS_DRC_SET_PARAM_DMA_INPUT_ORDER(dev, x) \
		(dev->is_p_region->parameter.drc.dma_input.order = x)
#define IS_DRC_SET_PARAM_DMA_INPUT_BUFFERNUM(dev, x) \
		(dev->is_p_region->parameter.drc.dma_input.buffer_number = x)
#define IS_DRC_SET_PARAM_DMA_INPUT_BUFFERADDR(dev, x) \
		(dev->is_p_region->parameter.drc.dma_input.buffer_address = x)
#define IS_DRC_SET_PARAM_DMA_INPUT_ERR(dev, x) \
		(dev->is_p_region->parameter.drc.dma_input.err = x)

#define IS_DRC_SET_PARAM_OTF_OUTPUT_CMD(dev, x) \
		(dev->is_p_region->parameter.drc.otf_output.cmd = x)
#define IS_DRC_SET_PARAM_OTF_OUTPUT_WIDTH(dev, x) \
		(dev->is_p_region->parameter.drc.otf_output.width = x)
#define IS_DRC_SET_PARAM_OTF_OUTPUT_HEIGHT(dev, x) \
		(dev->is_p_region->parameter.drc.otf_output.height = x)
#define IS_DRC_SET_PARAM_OTF_OUTPUT_FORMAT(dev, x) \
		(dev->is_p_region->parameter.drc.otf_output.format = x)
#define IS_DRC_SET_PARAM_OTF_OUTPUT_BITWIDTH(dev, x) \
		(dev->is_p_region->parameter.drc.otf_output.bitwidth = x)
#define IS_DRC_SET_PARAM_OTF_OUTPUT_ORDER(dev, x) \
		(dev->is_p_region->parameter.drc.otf_output.order = x)
#define IS_DRC_SET_PARAM_OTF_OUTPUT_ERR(dev, x) \
		(dev->is_p_region->parameter.drc.otf_output.err = x)

#define IS_DRC_GET_PARAM_OTF_OUTPUT_WIDTH(dev, x) \
		(x = dev->is_p_region->parameter.drc.otf_output.width)
#define IS_DRC_GET_PARAM_OTF_OUTPUT_HEIGHT(dev, x) \
		(x = dev->is_p_region->parameter.drc.otf_output.height)
/* FD Macros */
#define IS_FD_SET_PARAM_CONTROL_CMD(dev, x) \
		(dev->is_p_region->parameter.fd.control.cmd = x)
#define IS_FD_SET_PARAM_CONTROL_BYPASS(dev, x) \
		(dev->is_p_region->parameter.fd.control.bypass = x)
#define IS_FD_SET_PARAM_CONTROL_ERR(dev, x) \
		(dev->is_p_region->parameter.fd.control.err = x)

#define IS_FD_SET_PARAM_OTF_INPUT_CMD(dev, x) \
		(dev->is_p_region->parameter.fd.otf_input.cmd = x)
#define IS_FD_SET_PARAM_OTF_INPUT_WIDTH(dev, x) \
		(dev->is_p_region->parameter.fd.otf_input.width = x)
#define IS_FD_SET_PARAM_OTF_INPUT_HEIGHT(dev, x) \
		(dev->is_p_region->parameter.fd.otf_input.height = x)
#define IS_FD_SET_PARAM_OTF_INPUT_FORMAT(dev, x) \
		(dev->is_p_region->parameter.fd.otf_input.format = x)
#define IS_FD_SET_PARAM_OTF_INPUT_BITWIDTH(dev, x) \
		(dev->is_p_region->parameter.fd.otf_input.bitwidth = x)
#define IS_FD_SET_PARAM_OTF_INPUT_ORDER(dev, x) \
		(dev->is_p_region->parameter.fd.otf_input.order = x)
#define IS_FD_SET_PARAM_OTF_INPUT_ERR(dev, x) \
		(dev->is_p_region->parameter.fd.otf_input.err = x)

#define IS_FD_SET_PARAM_DMA_INPUT_CMD(dev, x) \
		(dev->is_p_region->parameter.fd.dma_input.cmd = x)
#define IS_FD_SET_PARAM_DMA_INPUT_WIDTH(dev, x) \
		(dev->is_p_region->parameter.fd.dma_input.width = x)
#define IS_FD_SET_PARAM_DMA_INPUT_HEIGHT(dev, x) \
		(dev->is_p_region->parameter.fd.dma_input.height = x)
#define IS_FD_SET_PARAM_DMA_INPUT_FORMAT(dev, x) \
		(dev->is_p_region->parameter.fd.dma_input.format = x)
#define IS_FD_SET_PARAM_DMA_INPUT_BITWIDTH(dev, x) \
		(dev->is_p_region->parameter.fd.dma_input.bitwidth = x)
#define IS_FD_SET_PARAM_DMA_INPUT_PLANE(dev, x) \
		(dev->is_p_region->parameter.fd.dma_input.plane = x)
#define IS_FD_SET_PARAM_DMA_INPUT_ORDER(dev, x) \
		(dev->is_p_region->parameter.fd.dma_input.order = x)
#define IS_FD_SET_PARAM_DMA_INPUT_BUFFERNUM(dev, x) \
		(dev->is_p_region->parameter.fd.dma_input.buffer_number = x)
#define IS_FD_SET_PARAM_DMA_INPUT_BUFFERADDR(dev, x) \
		(dev->is_p_region->parameter.fd.dma_input.buffer_address = x)
#define IS_FD_SET_PARAM_DMA_INPUT_ERR(dev, x) \
		(dev->is_p_region->parameter.fd.dma_input.err = x)

#define IS_FD_SET_PARAM_FD_CONFIG_CMD(dev, x) \
		(dev->is_p_region->parameter.fd.config.cmd = x)
#define IS_FD_SET_PARAM_FD_CONFIG_MAX_NUMBER(dev, x) \
		(dev->is_p_region->parameter.fd.config.max_number = x)
#define IS_FD_SET_PARAM_FD_CONFIG_ROLL_ANGLE(dev, x) \
		(dev->is_p_region->parameter.fd.config.roll_angle = x)
#define IS_FD_SET_PARAM_FD_CONFIG_YAW_ANGLE(dev, x) \
		(dev->is_p_region->parameter.fd.config.yaw_angle = x)
#define IS_FD_SET_PARAM_FD_CONFIG_SMILE_MODE(dev, x) \
		(dev->is_p_region->parameter.fd.config.smile_mode = x)
#define IS_FD_SET_PARAM_FD_CONFIG_BLINK_MODE(dev, x) \
		(dev->is_p_region->parameter.fd.config.blink_mode = x)
#define IS_FD_SET_PARAM_FD_CONFIG_EYE_DETECT(dev, x) \
		(dev->is_p_region->parameter.fd.config.eye_detect = x)
#define IS_FD_SET_PARAM_FD_CONFIG_MOUTH_DETECT(dev, x) \
		(dev->is_p_region->parameter.fd.config.mouth_detect = x)
#define IS_FD_SET_PARAM_FD_CONFIG_ORIENTATION(dev, x) \
		(dev->is_p_region->parameter.fd.config.orientation = x)
#define IS_FD_SET_PARAM_FD_CONFIG_ORIENTATION_VALUE(dev, x) \
		(dev->is_p_region->parameter.fd.config.orientation_value = x)
#define IS_FD_SET_PARAM_FD_CONFIG_ERR(dev, x) \
		(dev->is_p_region->parameter.fd.config.err = x)

#define IS_SENSOR_SET_TUNE_EXPOSURE(dev, x) \
		(dev->is_p_region->tune.sensor.exposure = x)
#define IS_SENSOR_SET_TUNE_ANALOG_GAIN(dev, x) \
		(dev->is_p_region->tune.sensor.analog_gain = x)
#define IS_SENSOR_SET_TUNE_FRAME_RATE(dev, x) \
		(dev->is_p_region->tune.sensor.frame_rate = x)
#define IS_SENSOR_SET_TUNE_ACTUATOR_POSITION(dev, x) \
		(dev->is_p_region->tune.sensor.actuator_position = x)

#define MAGIC_NUMBER 0x01020304

#define PARAMETER_MAX_SIZE	64	/* in byte */
#define PARAMETER_MAX_MEMBER	(PARAMETER_MAX_SIZE/4)

enum is_entry {
	ENTRY_GLOBAL,
	ENTRY_SENSOR,
	ENTRY_BUFFER,
	ENTRY_ISP,
	ENTRY_DRC,
	ENTRY_SCALERC,
	ENTRY_ODC,
	ENTRY_DIS,
	ENTRY_TDNR,
	ENTRY_SCALERP,
	ENTRY_LHFD, /* 10 */
	ENTRY_END
};

enum is_param_set_bit {
	PARAM_GLOBAL_SHOTMODE = 0,
	PARAM_SENSOR_CONTROL,
	PARAM_SENSOR_OTF_OUTPUT,
	PARAM_SENSOR_FRAME_RATE,
	PARAM_BUFFER_CONTROL,
	PARAM_BUFFER_OTF_INPUT,
	PARAM_BUFFER_OTF_OUTPUT,
	PARAM_ISP_CONTROL,
	PARAM_ISP_OTF_INPUT,
	PARAM_ISP_DMA1_INPUT,
	PARAM_ISP_DMA2_INPUT = 10,
	PARAM_ISP_AA,
	PARAM_ISP_FLASH,
	PARAM_ISP_AWB,
	PARAM_ISP_IMAGE_EFFECT,
	PARAM_ISP_ISO,
	PARAM_ISP_ADJUST,
	PARAM_ISP_METERING,
	PARAM_ISP_AFC,
	PARAM_ISP_OTF_OUTPUT,
	PARAM_ISP_DMA1_OUTPUT = 20,
	PARAM_ISP_DMA2_OUTPUT,
	PARAM_DRC_CONTROL,
	PARAM_DRC_OTF_INPUT,
	PARAM_DRC_DMA_INPUT,
	PARAM_DRC_OTF_OUTPUT,
	PARAM_SCALERC_CONTROL,
	PARAM_SCALERC_OTF_INPUT,
	PARAM_SCALERC_IMAGE_EFFECT,
	PARAM_SCALERC_INPUT_CROP,
	PARAM_SCALERC_OUTPUT_CROP = 30,
	PARAM_SCALERC_OTF_OUTPUT,
	PARAM_SCALERC_DMA_OUTPUT = 32,
	PARAM_ODC_CONTROL,
	PARAM_ODC_OTF_INPUT,
	PARAM_ODC_OTF_OUTPUT,
	PARAM_DIS_CONTROL,
	PARAM_DIS_OTF_INPUT,
	PARAM_DIS_OTF_OUTPUT,
	PARAM_TDNR_CONTROL,
	PARAM_TDNR_OTF_INPUT = 40,
	PARAM_TDNR_1ST_FRAME,
	PARAM_TDNR_OTF_OUTPUT,
	PARAM_TDNR_DMA_OUTPUT,
	PARAM_SCALERP_CONTROL,
	PARAM_SCALERP_OTF_INPUT,
	PARAM_SCALERP_IMAGE_EFFECT,
	PARAM_SCALERP_INPUT_CROP,
	PARAM_SCALERP_OUTPUT_CROP,
	PARAM_SCALERP_ROTATION,
	PARAM_SCALERP_FLIP = 50,
	PARAM_SCALERP_OTF_OUTPUT,
	PARAM_SCALERP_DMA_OUTPUT,
	PARAM_FD_CONTROL,
	PARAM_FD_OTF_INPUT,
	PARAM_FD_DMA_INPUT,
	PARAM_FD_CONFIG = 56,
	PARAM_END,
};

#define ADDRESS_TO_OFFSET(start, end)	((uint32)end - (uint32)start)
#define OFFSET_TO_NUM(offset)		((offset)>>6)
#define IS_OFFSET_LOWBIT(offset)	(OFFSET_TO_NUM(offset) >= \
						32 ? false : true)
#define OFFSET_TO_BIT(offset) \
		(IS_OFFSET_LOWBIT(offset) ? (1<<OFFSET_TO_NUM(offset)) \
			: (1<<(OFFSET_TO_NUM(offset)-32)))
#define LOWBIT_OF_NUM(num)		(num >= 32 ? 0 : BIT0<<num)
#define HIGHBIT_OF_NUM(num)		(num >= 32 ? BIT0<<(num-32) : 0)

#define PARAM_STRNUM_GLOBAL		(PARAM_GLOBAL_SHOTMODE)
#define PARAM_RANGE_GLOBAL		1
#define PARAM_STRNUM_SENSOR		(PARAM_SENSOR_BYPASS)
#define PARAM_RANGE_SENSOR		3
#define PARAM_STRNUM_BUFFER		(PARAM_BUFFER_BYPASS)
#define PARAM_RANGE_BUFFER		3
#define PARAM_STRNUM_ISP		(PARAM_ISP_BYPASS)
#define PARAM_RANGE_ISP			15
#define PARAM_STRNUM_DRC		(PARAM_DRC_BYPASS)
#define PARAM_RANGE_DRC			4
#define PARAM_STRNUM_SCALERC		(PARAM_SCALERC_BYPASS)
#define PARAM_RANGE_SCALERC		7
#define PARAM_STRNUM_ODC		(PARAM_ODC_BYPASS)
#define PARAM_RANGE_ODC			3
#define PARAM_STRNUM_DIS		(PARAM_DIS_BYPASS)
#define PARAM_RANGE_DIS			3
#define PARAM_STRNUM_TDNR		(PARAM_TDNR_BYPASS)
#define PARAM_RANGE_TDNR		5
#define PARAM_STRNUM_SCALERP		(PARAM_SCALERP_BYPASS)
#define PARAM_RANGE_SCALERP		9
#define PARAM_STRNUM_LHFD		(PARAM_FD_BYPASS)
#define PARAM_RANGE_LHFD		4

/* Enumerations */
/* ----------------------  INTR map-------------------------------- */
enum interrupt_map {
	INTR_GENERAL = 0,
	INTR_FRAME_DONE_ISP = 1,
	INTR_FRAME_DONE_SCALERC = 2,
	INTR_FRAME_DONE_TDNR = 3,
	INTR_FRAME_DONE_SCALERP = 4
};

/* ----------------------  Input  ----------------------------------- */
enum control_command {
	CONTROL_COMMAND_STOP		= 0,
	CONTROL_COMMAND_START		= 1
};

enum bypass_command {
	CONTROL_BYPASS_DISABLE		= 0,
	CONTROL_BYPASS_ENABLE		= 1
};

enum control_error {
	CONTROL_ERROR_NO		= 0
};

enum otf_input_command {
	OTF_INPUT_COMMAND_DISABLE	= 0,
	OTF_INPUT_COMMAND_ENABLE	= 1
};

enum otf_input_format {
	OTF_INPUT_FORMAT_BAYER			= 0, /* 1 Channel */
	OTF_INPUT_FORMAT_YUV444			= 1, /* 3 Channel */
	OTF_INPUT_FORMAT_YUV422			= 2, /* 3 Channel */
	OTF_INPUT_FORMAT_YUV420			= 3, /* 3 Channel */
	OTF_INPUT_FORMAT_STRGEN_COLORBAR_BAYER	= 10,
	OTF_INPUT_FORMAT_BAYER_DMA		= 11,
};

enum otf_input_bitwidth {
	OTF_INPUT_BIT_WIDTH_14BIT	= 14,
	OTF_INPUT_BIT_WIDTH_12BIT	= 12,
	OTF_INPUT_BIT_WIDTH_11BIT	= 11,
	OTF_INPUT_BIT_WIDTH_10BIT	= 10,
	OTF_INPUT_BIT_WIDTH_9BIT	= 9,
	OTF_INPUT_BIT_WIDTH_8BIT	= 8
};

enum otf_input_order {
	OTF_INPUT_ORDER_BAYER_GR_BG	= 0,
};

enum otf_intput_error {
	OTF_INPUT_ERROR_NO		= 0 /* Input setting is done */
};

enum dma_input_command {
	DMA_INPUT_COMMAND_DISABLE	= 0,
	DMA_INPUT_COMMAND_ENABLE	= 1
};

enum dma_inut_format {
	DMA_INPUT_FORMAT_BAYER		= 0,
	DMA_INPUT_FORMAT_YUV444		= 1,
	DMA_INPUT_FORMAT_YUV422		= 2,
	DMA_INPUT_FORMAT_YUV420		= 3,
};

enum dma_input_bitwidth {
	DMA_INPUT_BIT_WIDTH_14BIT	= 14,
	DMA_INPUT_BIT_WIDTH_12BIT	= 12,
	DMA_INPUT_BIT_WIDTH_11BIT	= 11,
	DMA_INPUT_BIT_WIDTH_10BIT	= 10,
	DMA_INPUT_BIT_WIDTH_9BIT	= 9,
	DMA_INPUT_BIT_WIDTH_8BIT	= 8
};

enum dma_input_plane {
	DMA_INPUT_PLANE_3	= 3,
	DMA_INPUT_PLANE_2	= 2,
	DMA_INPUT_PLANE_1	= 1
};

enum dma_input_order {
	/* (for DMA_INPUT_PLANE_3) */
	DMA_INPUT_ORDER_NO	= 0,
	/* (only valid at DMA_INPUT_PLANE_2) */
	DMA_INPUT_ORDER_CbCr	= 1,
	/* (only valid at DMA_INPUT_PLANE_2) */
	DMA_INPUT_ORDER_CrCb	= 2,
	/* (only valid at DMA_INPUT_PLANE_1 & DMA_INPUT_FORMAT_YUV444) */
	DMA_INPUT_ORDER_YCbCr	= 3,
	/* (only valid at DMA_INPUT_FORMAT_YUV422 & DMA_INPUT_PLANE_1) */
	DMA_INPUT_ORDER_YYCbCr	= 4,
	/* (only valid at DMA_INPUT_FORMAT_YUV422 & DMA_INPUT_PLANE_1) */
	DMA_INPUT_ORDER_YCbYCr	= 5,
	/* (only valid at DMA_INPUT_FORMAT_YUV422 & DMA_INPUT_PLANE_1) */
	DMA_INPUT_ORDER_YCrYCb	= 6,
	/* (only valid at DMA_INPUT_FORMAT_YUV422 & DMA_INPUT_PLANE_1) */
	DMA_INPUT_ORDER_CbYCrY	= 7,
	/* (only valid at DMA_INPUT_FORMAT_YUV422 & DMA_INPUT_PLANE_1) */
	DMA_INPUT_ORDER_CrYCbY	= 8,
	/* (only valid at DMA_INPUT_FORMAT_BAYER) */
	DMA_INPUT_ORDER_GR_BG	= 9
};

enum dma_input_error {
	DMA_INPUT_ERROR_NO	= 0 /*  DMA input setting is done */
};

/* ----------------------  Output  ----------------------------------- */
enum otf_output_crop {
	OTF_OUTPUT_CROP_DISABLE		= 0,
	OTF_OUTPUT_CROP_ENABLE		= 1
};

enum otf_output_command {
	OTF_OUTPUT_COMMAND_DISABLE	= 0,
	OTF_OUTPUT_COMMAND_ENABLE	= 1
};

enum orf_output_format {
	OTF_OUTPUT_FORMAT_YUV444	= 1,
	OTF_OUTPUT_FORMAT_YUV422	= 2,
	OTF_OUTPUT_FORMAT_YUV420	= 3,
	OTF_OUTPUT_FORMAT_RGB		= 4
};

enum otf_output_bitwidth {
	OTF_OUTPUT_BIT_WIDTH_14BIT	= 14,
	OTF_OUTPUT_BIT_WIDTH_12BIT	= 12,
	OTF_OUTPUT_BIT_WIDTH_11BIT	= 11,
	OTF_OUTPUT_BIT_WIDTH_10BIT	= 10,
	OTF_OUTPUT_BIT_WIDTH_9BIT	= 9,
	OTF_OUTPUT_BIT_WIDTH_8BIT	= 8
};

enum otf_output_order {
	OTF_OUTPUT_ORDER_BAYER_GR_BG	= 0,
};

enum otf_output_error {
	OTF_OUTPUT_ERROR_NO		= 0 /* Output Setting is done */
};

enum dma_output_command {
	DMA_OUTPUT_COMMAND_DISABLE	= 0,
	DMA_OUTPUT_COMMAND_ENABLE	= 1
};

enum dma_output_format {
	DMA_OUTPUT_FORMAT_BAYER		= 0,
	DMA_OUTPUT_FORMAT_YUV444	= 1,
	DMA_OUTPUT_FORMAT_YUV422	= 2,
	DMA_OUTPUT_FORMAT_YUV420	= 3,
	DMA_OUTPUT_FORMAT_RGB		= 4
};

enum dma_output_bitwidth {
	DMA_OUTPUT_BIT_WIDTH_14BIT	= 14,
	DMA_OUTPUT_BIT_WIDTH_12BIT	= 12,
	DMA_OUTPUT_BIT_WIDTH_11BIT	= 11,
	DMA_OUTPUT_BIT_WIDTH_10BIT	= 10,
	DMA_OUTPUT_BIT_WIDTH_9BIT	= 9,
	DMA_OUTPUT_BIT_WIDTH_8BIT	= 8
};

enum dma_output_plane {
	DMA_OUTPUT_PLANE_3		= 3,
	DMA_OUTPUT_PLANE_2		= 2,
	DMA_OUTPUT_PLANE_1		= 1
};

enum dma_output_order {
	DMA_OUTPUT_ORDER_NO		= 0,
	/* (for DMA_OUTPUT_PLANE_3) */
	DMA_OUTPUT_ORDER_CbCr		= 1,
	/* (only valid at DMA_INPUT_PLANE_2) */
	DMA_OUTPUT_ORDER_CrCb		= 2,
	/* (only valid at DMA_OUTPUT_PLANE_2) */
	DMA_OUTPUT_ORDER_YYCbCr		= 3,
	/* (only valid at DMA_OUTPUT_FORMAT_YUV422 & DMA_OUTPUT_PLANE_1) */
	DMA_OUTPUT_ORDER_YCbYCr		= 4,
	/* (only valid at DMA_OUTPUT_FORMAT_YUV422 & DMA_OUTPUT_PLANE_1) */
	DMA_OUTPUT_ORDER_YCrYCb		= 5,
	/* (only valid at DMA_OUTPUT_FORMAT_YUV422 & DMA_OUTPUT_PLANE_1) */
	DMA_OUTPUT_ORDER_CbYCrY		= 6,
	/* (only valid at DMA_OUTPUT_FORMAT_YUV422 & DMA_OUTPUT_PLANE_1) */
	DMA_OUTPUT_ORDER_CrYCbY		= 7,
	/* (only valid at DMA_OUTPUT_FORMAT_YUV422 & DMA_OUTPUT_PLANE_1) */
	DMA_OUTPUT_ORDER_YCbCr		= 8,
	/* (only valid at DMA_OUTPUT_FORMAT_YUV444 & DMA_OUPUT_PLANE_1) */
	DMA_OUTPUT_ORDER_CrYCb		= 9,
	/* (only valid at DMA_OUTPUT_FORMAT_YUV444 & DMA_OUPUT_PLANE_1) */
	DMA_OUTPUT_ORDER_CrCbY		= 10,
	/* (only valid at DMA_OUTPUT_FORMAT_YUV444 & DMA_OUPUT_PLANE_1) */
	DMA_OUTPUT_ORDER_CbYCr		= 11,
	/* (only valid at DMA_OUTPUT_FORMAT_YUV444 & DMA_OUPUT_PLANE_1) */
	DMA_OUTPUT_ORDER_YCrCb		= 12,
	/* (only valid at DMA_OUTPUT_FORMAT_YUV444 & DMA_OUPUT_PLANE_1) */
	DMA_OUTPUT_ORDER_CbCrY		= 13,
	/* (only valid at DMA_OUTPUT_FORMAT_YUV444 & DMA_OUPUT_PLANE_1) */
	DMA_OUTPUT_ORDER_BGR		= 14,
	/* (only valid at DMA_OUTPUT_FORMAT_RGB) */
	DMA_OUTPUT_ORDER_GB_BG		= 15
	/* (only valid at DMA_OUTPUT_FORMAT_BAYER) */
};

enum dma_output_notify_dma_done {
	DMA_OUTPUT_NOTIFY_DMA_DONE_DISABLE	= 0,
	DMA_OUTPUT_NOTIFY_DMA_DONE_ENBABLE	= 1,
};

enum dma_output_error {
	DMA_OUTPUT_ERROR_NO		= 0 /* DMA output setting is done */
};

/* ----------------------  Global  ----------------------------------- */
enum global_shotmode_error {
	GLOBAL_SHOTMODE_ERROR_NO	= 0 /* shot-mode setting is done */
};

/* -------------------------  AA  ------------------------------------ */
enum isp_lock_command {
	ISP_AA_COMMAND_START	= 0,
	ISP_AA_COMMAND_STOP	= 1
};

enum isp_lock_target {
	ISP_AA_TARGET_AF	= 1,
	ISP_AA_TARGET_AE	= 2,
	ISP_AA_TARGET_AWB	= 4
};

enum isp_af_mode {
	ISP_AF_MODE_MANUAL		= 0,
	ISP_AF_MODE_SINGLE		= 1,
	ISP_AF_MODE_CONTINUOUS		= 2,
	ISP_AF_MODE_TOUCH		= 3,
	ISP_AF_MODE_SLEEP		= 4,
	ISP_AF_MODE_INIT		= 5,
	ISP_AF_MODE_SET_CENTER_WINDOW	= 6,
	ISP_AF_MODE_SET_TOUCH_WINDOW	= 7
};

enum isp_af_face {
	ISP_AF_FACE_DISABLE		= 0,
	ISP_AF_FACE_ENABLE		= 1
};

enum isp_af_scene {
	ISP_AF_SCENE_NORMAL		= 0,
	ISP_AF_SCENE_MACRO		= 1
};

enum isp_af_sleep {
	ISP_AF_SLEEP_OFF		= 0,
	ISP_AF_SLEEP_ON			= 1
};

enum isp_af_continuous {
	ISP_AF_CONTINUOUS_DISABLE	= 0,
	ISP_AF_CONTINUOUS_ENABLE	= 1
};

enum isp_af_error {
	ISP_AF_ERROR_NO			= 0, /* AF mode change is done */
	ISP_AF_EROOR_NO_LOCK_DONE	= 1  /* AF lock is done */
};

/* -------------------------  Flash  ------------------------------------- */
enum isp_flash_command {
	ISP_FLASH_COMMAND_DISABLE	= 0 ,
	ISP_FLASH_COMMAND_MANUALON	= 1, /* (forced flash) */
	ISP_FLASH_COMMAND_AUTO		= 2,
	ISP_FLASH_COMMAND_TORCH		= 3  /* 3 sec */
};

enum isp_flash_redeye {
	ISP_FLASH_REDEYE_DISABLE	= 0,
	ISP_FLASH_REDEYE_ENABLE		= 1
};

enum isp_flash_error {
	ISP_FLASH_ERROR_NO		= 0 /* Flash setting is done */
};

/* --------------------------  AWB  ------------------------------------ */
enum isp_awb_command {
	ISP_AWB_COMMAND_AUTO		= 0,
	ISP_AWB_COMMAND_ILLUMINATION	= 1,
	ISP_AWB_COMMAND_MANUAL		= 2
};

enum isp_awb_illumination {
	ISP_AWB_ILLUMINATION_DAYLIGHT		= 0,
	ISP_AWB_ILLUMINATION_CLOUDY		= 1,
	ISP_AWB_ILLUMINATION_TUNGSTEN		= 2,
	ISP_AWB_ILLUMINATION_FLUORESCENT	= 3
};

enum isp_awb_error {
	ISP_AWB_ERROR_NO		= 0 /* AWB setting is done */
};

/* --------------------------  Effect  ----------------------------------- */
enum isp_imageeffect_command {
	ISP_IMAGE_EFFECT_DISABLE		= 0,
	ISP_IMAGE_EFFECT_MONOCHROME		= 1,
	ISP_IMAGE_EFFECT_NEGATIVE_MONO		= 2,
	ISP_IMAGE_EFFECT_NEGATIVE_COLOR		= 3,
	ISP_IMAGE_EFFECT_SEPIA			= 4
};

enum isp_imageeffect_error {
	ISP_IMAGE_EFFECT_ERROR_NO	= 0 /* Image effect setting is done */
};

/* ---------------------------  ISO  ------------------------------------ */
enum isp_iso_command {
	ISP_ISO_COMMAND_AUTO		= 0,
	ISP_ISO_COMMAND_MANUAL		= 1
};

enum iso_error {
	ISP_ISO_ERROR_NO		= 0 /* ISO setting is done */
};

/* --------------------------  Adjust  ----------------------------------- */
enum iso_adjust_command {
	ISP_ADJUST_COMMAND_AUTO			= 0,
	ISP_ADJUST_COMMAND_MANUAL_CONTRAST	= (1 << 0),
	ISP_ADJUST_COMMAND_MANUAL_SATURATION	= (1 << 1),
	ISP_ADJUST_COMMAND_MANUAL_SHARPNESS	= (1 << 2),
	ISP_ADJUST_COMMAND_MANUAL_EXPOSURE	= (1 << 3),
	ISP_ADJUST_COMMAND_MANUAL_BRIGHTNESS	= (1 << 4),
	ISP_ADJUST_COMMAND_MANUAL_HUE		= (1 << 5),
	ISP_ADJUST_COMMAND_MANUAL_ALL		= 0x7F,
};

enum isp_adjust_error {
	ISP_ADJUST_ERROR_NO		= 0 /* Adjust setting is done */
};

/* -------------------------  Metering  ---------------------------------- */
enum isp_metering_command {
	ISP_METERING_COMMAND_AVERAGE	= 0,
	ISP_METERING_COMMAND_SPOT	= 1,
	ISP_METERING_COMMAND_MATRIX	= 2,
	ISP_METERING_COMMAND_CENTER	= 3
};

enum isp_metering_error {
	ISP_METERING_ERROR_NO	= 0 /* Metering setting is done */
};

/* --------------------------  AFC  ----------------------------------- */
enum isp_afc_command {
	ISP_AFC_COMMAND_DISABLE		= 0,
	ISP_AFC_COMMAND_AUTO		= 1,
	ISP_AFC_COMMAND_MANUAL		= 2
};

enum isp_afc_manual {
	ISP_AFC_MANUAL_50HZ		= 50,
	ISP_AFC_MANUAL_60HZ		= 60
};

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

enum isp_afc_error {
	ISP_AFC_ERROR_NO	= 0 /* AFC setting is done */
};

/* --------------------------  Scaler  --------------------------------- */
enum scaler_imageeffect_command {
	SCALER_IMAGE_EFFECT_COMMNAD_DISABLE	= 0,
	SCALER_IMAGE_EFFECT_COMMNAD_ARBITRARY	= 1,
	SCALER_IMAGE_EFFECT_COMMAND_NEGATIVE	= 2,
	SCALER_IMAGE_EFFECT_COMMAND_ARTFREEZE	= 3,
	SCALER_IMAGE_EFFECT_COMMAND_EMBOSSING	= 4,
	SCALER_IMAGE_EFFECT_COMMAND_SILHOUETTE	= 5
};

enum scaler_imageeffect_error {
	SCALER_IMAGE_EFFECT_ERROR_NO		= 0
};

enum scaler_crop_command {
	SCALER_CROP_COMMAND_DISABLE		= 0,
	SCALER_CROP_COMMAND_ENABLE		= 1
};

enum scaler_crop_error {
	SCALER_CROP_ERROR_NO			= 0 /* crop setting is done */
};

enum scaler_scaling_command {
	SCALER_SCALING_COMMNAD_DISABLE		= 0,
	SCALER_SCALING_COMMAND_UP		= 1,
	SCALER_SCALING_COMMAND_DOWN		= 2
};

enum scaler_dma_out_sel {
	SCALER_DMA_OUT_IMAGE_EFFECT		= 0,
	SCALER_DMA_OUT_SCALED			= 1,
	SCALER_DMA_OUT_UNSCALED			= 2
};

enum scaler_scaling_error {
	SCALER_SCALING_ERROR_NO			= 0
};


enum scaler_rotation_command {
	SCALER_ROTATION_COMMAND_DISABLE		= 0,
	SCALER_ROTATION_COMMAND_CLOCKWISE90	= 1
};

enum scaler_rotation_error {
	SCALER_ROTATION_ERROR_NO		= 0
};

enum scaler_flip_command {
	SCALER_FLIP_COMMAND_NORMAL		= 0,
	SCALER_FLIP_COMMAND_X_MIRROR		= 1,
	SCALER_FLIP_COMMAND_Y_MIRROR		= 2,
	SCALER_FLIP_COMMAND_XY_MIRROR		= 3 /* (180 rotation) */
};

enum scaler_flip_error {
	SCALER_FLIP_ERROR_NO			= 0 /* flip setting is done */
};

/* --------------------------  3DNR  ----------------------------------- */
enum tdnr_1st_frame_command {
	TDNR_1ST_FRAME_COMMAND_NOPROCESSING	= 0,
	TDNR_1ST_FRAME_COMMAND_2DNR		= 1
};

enum tdnr_1st_frame_error {
	TDNR_1ST_FRAME_ERROR_NO			= 0 /*1st frame setting is done*/
};

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

enum fd_config_smile_mode {
	FD_CONFIG_SMILE_MODE_DISABLE		= 0,
	FD_CONFIG_SMILE_MODE_ENABLE		= 1
};

enum fd_config_blink_mode {
	FD_CONFIG_BLINK_MODE_DISABLE		= 0,
	FD_CONFIG_BLINK_MODE_ENABLE		= 1
};

enum fd_config_eye_result {
	FD_CONFIG_EYES_DETECT_DISABLE		= 0,
	FD_CONFIG_EYES_DETECT_ENABLE		= 1
};

enum fd_config_mouth_result {
	FD_CONFIG_MOUTH_DETECT_DISABLE		= 0,
	FD_CONFIG_MOUTH_DETECT_ENABLE		= 1
};

enum fd_config_orientation {
	FD_CONFIG_ORIENTATION_DISABLE		= 0,
	FD_CONFIG_ORIENTATION_ENABLE		= 1
};

struct param_control {
	u32 cmd;
	u32 bypass;
	u32 buffer_address;
	u32 buffer_size;
	u32 first_drop_frames; /* only valid at ISP */
	u32 reserved[PARAMETER_MAX_MEMBER-6];
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
	u32 reserved[PARAMETER_MAX_MEMBER-13];
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
	u32 reserved[PARAMETER_MAX_MEMBER-10];
	u32 err;
};

struct param_otf_output {
	u32 cmd;
	u32 width;
	u32 height;
	u32 format;
	u32 bitwidth;
	u32 order;
	u32 reserved[PARAMETER_MAX_MEMBER-7];
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
	u32 reserved[PARAMETER_MAX_MEMBER-12];
	u32 err;
};

struct param_global_shotmode {
	u32 cmd;
	u32 skip_frames;
	u32 reserved[PARAMETER_MAX_MEMBER-3];
	u32 err;
};

struct param_sensor_framerate {
	u32 frame_rate;
	u32 reserved[PARAMETER_MAX_MEMBER-2];
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
	u32 reserved[PARAMETER_MAX_MEMBER-10];
	u32 err;
};

struct param_isp_flash {
	u32 cmd;
	u32 redeye;
	u32 reserved[PARAMETER_MAX_MEMBER-3];
	u32 err;
};

struct param_isp_awb {
	u32 cmd;
	u32 illumination;
	u32 reserved[PARAMETER_MAX_MEMBER-3];
	u32 err;
};

struct param_isp_imageeffect {
	u32 cmd;
	u32 reserved[PARAMETER_MAX_MEMBER-2];
	u32 err;
};

struct param_isp_iso {
	u32 cmd;
	u32 value;
	u32 reserved[PARAMETER_MAX_MEMBER-3];
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
	u32 reserved[PARAMETER_MAX_MEMBER-8];
	u32 err;
};

struct param_isp_metering {
	u32 cmd;
	u32 win_pos_x;
	u32 win_pos_y;
	u32 win_width;
	u32 win_height;
	u32 reserved[PARAMETER_MAX_MEMBER-6];
	u32 err;
};

struct param_isp_afc {
	u32 cmd;
	u32 manual;
	u32 reserved[PARAMETER_MAX_MEMBER-3];
	u32 err;
};

struct param_scaler_imageeffect {
	u32 cmd;
	u32 arbitrary_cb;
	u32 arbitrary_cr;
	u32 reserved[PARAMETER_MAX_MEMBER-4];
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
	u32 reserved[PARAMETER_MAX_MEMBER-10];
	u32 err;
};

struct param_scaler_output_crop {
	u32 cmd;
	u32 crop_offset_x;
	u32 crop_offset_y;
	u32 crop_width;
	u32 crop_height;
	u32 out_format;
	u32 reserved[PARAMETER_MAX_MEMBER-7];
	u32 err;
};

struct param_scaler_rotation {
	u32 cmd;
	u32 reserved[PARAMETER_MAX_MEMBER-2];
	u32 err;
};

struct param_scaler_flip {
	u32 cmd;
	u32 reserved[PARAMETER_MAX_MEMBER-2];
	u32 err;
};

struct param_3dnr_1stframe {
	u32 cmd;
	u32 reserved[PARAMETER_MAX_MEMBER-2];
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
	u32 reserved[PARAMETER_MAX_MEMBER-11];
	u32 err;
};

struct global_param {
	struct param_global_shotmode	shotmode; /* 0 */
};

/* To be added */
struct sensor_param {
	struct param_control		control;
	struct param_otf_output		otf_output;
	struct param_sensor_framerate	frame_rate;
};

struct buffer_param {
	struct param_control	control;
	struct param_otf_input	otf_input;
	struct param_otf_output	otf_output;
};

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
};

struct drc_param {
	struct param_control		control;
	struct param_otf_input		otf_input;
	struct param_dma_input		dma_input;
	struct param_otf_output		otf_output;
};

struct scalerc_param {
	struct param_control			control;
	struct param_otf_input			otf_input;
	struct param_scaler_imageeffect		effect;
	struct param_scaler_input_crop		input_crop;
	struct param_scaler_output_crop		output_crop;
	struct param_otf_output			otf_output;
	struct param_dma_output			dma_output;
};

struct odc_param {
	struct param_control		control;
	struct param_otf_input		otf_input;
	struct param_otf_output		otf_output;
};

struct dis_param {
	struct param_control		control;
	struct param_otf_output		otf_input;
	struct param_otf_output		otf_output;
};

struct tdnr_param {
	struct param_control		control;
	struct param_otf_input		otf_input;
	struct param_3dnr_1stframe	frame;
	struct param_otf_output		otf_output;
	struct param_dma_output		dma_output;
};

struct scalerp_param {
	struct param_control			control;
	struct param_otf_input			otf_input;
	struct param_scaler_imageeffect		effect;
	struct param_scaler_input_crop		input_crop;
	struct param_scaler_output_crop		output_crop;
	struct param_scaler_rotation		rotation;
	struct param_scaler_flip		flip;
	struct param_otf_output			otf_output;
	struct param_dma_output			dma_output;
};

struct fd_param {
	struct param_control			control;
	struct param_otf_input			otf_input;
	struct param_dma_input			dma_input;
	struct param_fd_config			config;
};

struct is_param_region {
	struct global_param	global;
	struct sensor_param	sensor;
	struct buffer_param	buf;
	struct isp_param	isp;
	struct drc_param	drc;
	struct scalerc_param	scalerc;
	struct odc_param	odc;
	struct dis_param	dis;
	struct tdnr_param	tdnr;
	struct scalerp_param	scalerp;
	struct fd_param		fd;
};

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
	/* Brightness level : range 0~100, default : 7 */
	u32 brightness_level;
	/* Contrast level : range -127~127, default : 0 */
	s32 contrast_level;
	/* Saturation level : range -127~127, default : 0 */
	s32 saturation_level;
	s32 gamma_level;
	struct is_tune_gammacurve gamma_curve[4];
	/* Hue : range -127~127, default : 0 */
	s32 hue;
	/* Sharpness blur : range -127~127, default : 0 */
	s32 sharpness_blur;
	/* Despeckle : range -127~127, default : 0 */
	s32 despeckle;
	/* Edge color supression : range -127~127, default : 0 */
	s32 edge_color_supression;
	/* Noise reduction : range -127~127, default : 0 */
	s32 noise_reduction;
	/* (32*4 + 9)*4 = 548 bytes */
};

struct is_tune_region {
	struct is_tune_sensor sensor;
	struct is_tune_isp isp;
};

struct rational_t {
	u32 num;
	u32 den;
};

struct srational_t {
	s32 num;
	s32 den;
};

#define FLASH_FIRED_SHIFT	0
#define FLASH_NOT_FIRED		0
#define FLASH_FIRED		1

#define FLASH_STROBE_SHIFT				1
#define FLASH_STROBE_NO_DETECTION			0
#define FLASH_STROBE_RESERVED				1
#define FLASH_STROBE_RETURN_LIGHT_NOT_DETECTED		2
#define FLASH_STROBE_RETURN_LIGHT_DETECTED		3

#define FLASH_MODE_SHIFT			3
#define FLASH_MODE_UNKNOWN			0
#define FLASH_MODE_COMPULSORY_FLASH_FIRING	1
#define FLASH_MODE_COMPULSORY_FLASH_SUPPRESSION	2
#define FLASH_MODE_AUTO_MODE			3

#define FLASH_FUNCTION_SHIFT		5
#define FLASH_FUNCTION_PRESENT		0
#define FLASH_FUNCTION_NONE		1

#define FLASH_RED_EYE_SHIFT		6
#define FLASH_RED_EYE_DISABLED		0
#define FLASH_RED_EYE_SUPPORTED		1

enum apex_aperture_value {
	F1_0		= 0,
	F1_4		= 1,
	F2_0		= 2,
	F2_8		= 3,
	F4_0		= 4,
	F5_6		= 5,
	F8_9		= 6,
	F11_0		= 7,
	F16_0		= 8,
	F22_0		= 9,
	F32_0		= 10,
};

struct exif_attribute {
	struct rational_t exposure_time;
	struct srational_t shutter_speed;
	u32 iso_speed_rating;
	u32 flash;
	struct srational_t brightness;
};

struct is_frame_header {
	u32 valid;
	u32 bad_mark;
	u32 captured;
	u32 frame_number;
	struct exif_attribute exif;
};

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
};

#define MAX_FRAME_COUNT		8
#define MAX_FRAME_COUNT_PREVIEW	4
#define MAX_FRAME_COUNT_CAPTURE	1
#define MAX_FACE_COUNT		16

#define MAX_SHARED_COUNT	500

struct is_region {
	struct is_param_region	parameter;
	struct is_tune_region	tune;
	struct is_frame_header	header[MAX_FRAME_COUNT];
	struct is_face_marker	face[MAX_FACE_COUNT];
	u32			shared[MAX_SHARED_COUNT];
};

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
	u32	frame_time;
	u32	exposure_time;
	s32	analog_gain;

	u32	r_gain;
	u32	g_gain;
	u32	b_gain;

	u32	af_position;
	u32	af_status;
	/* 0 : SIRC_ISP_CAMERA_AUTOFOCUSMESSAGE_NOMESSAGE */
	/* 1 : SIRC_ISP_CAMERA_AUTOFOCUSMESSAGE_REACHED */
	/* 2 : SIRC_ISP_CAMERA_AUTOFOCUSMESSAGE_UNABLETOREACH */
	/* 3 : SIRC_ISP_CAMERA_AUTOFOCUSMESSAGE_LOST */
	/* default : unknown */
	u32	af_scene_type;

	u32	frame_descp_onoff_control;
	u32	frame_descp_update_done;
	u32	frame_descp_idx;
	u32	frame_descp_max_idx;
	struct is_debug_frame_descriptor
		dbg_frame_descp_ctx[MAX_FRAMEDESCRIPTOR_CONTEXT_NUM];

	u32	chip_id;
	u32	chip_rev_no;
	u8	isp_fw_ver_no[MAX_VERSION_DISPLAY_BUF];
	u8	isp_fw_ver_date[MAX_VERSION_DISPLAY_BUF];
	u8	sirc_sdk_ver_no[MAX_VERSION_DISPLAY_BUF];
	u8	sirc_sdk_rev_no[MAX_VERSION_DISPLAY_BUF];
	u8	sirc_sdk_rev_date[MAX_VERSION_DISPLAY_BUF];
};

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
	/* Activatiing sensor self calibration mode (6A3) */
	u32 self_calibration_mode;
	/* This field is to adjust I2c clock based on ACLK200 */
	/* This value is varied in case of rev 0.2 */
	u32 i2c_sclk;
};
#endif
