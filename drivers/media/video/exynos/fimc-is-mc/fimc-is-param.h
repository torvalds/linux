/*
 * Samsung Exynos5 SoC series FIMC-IS driver
 *
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 * Contact: Jiyoung Shin<idon.shin@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FIMC_IS_PARAMS_H
#define FIMC_IS_PARAMS_H

#define IS_REGION_VER 115  /* IS REGION VERSION 1.15 */

/* MACROs */
#define IS_SET_PARAM_BIT(dev, num) \
	(num >= 32 ? set_bit((num-32), &dev->p_region_index2) \
		: set_bit(num, &dev->p_region_index1))
#define IS_INC_PARAM_NUM(dev)		atomic_inc(&dev->p_region_num)

#define IS_PARAM_GLOBAL		(dev->is_p_region->parameter.global)
#define IS_PARAM_ISP		(dev->is_p_region->parameter.isp)
#define IS_PARAM_DRC		(dev->is_p_region->parameter.drc)
#define IS_PARAM_FD		(dev->is_p_region->parameter.fd)
#define IS_HEADER		(dev->is_p_region->header)
#define IS_FACE			(dev->is_p_region->face)
#define IS_PARAM_SIZE		(FIMC_IS_REGION_SIZE+1)

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
#define IS_ISP_SET_PARAM_OTF_INPUT_ERR(dev, x) \
		(dev->is_p_region->parameter.isp.otf_input.err = x)
#define IS_ISP_SET_PARAM_OTF_INPUT_RESERVED0(dev, x) \
		(dev->is_p_region->parameter.isp.otf_input.reserved[0] = x)
#define IS_ISP_SET_PARAM_OTF_INPUT_RESERVED1(dev, x) \
		(dev->is_p_region->parameter.isp.otf_input.reserved[1] = x)
#define IS_ISP_SET_PARAM_OTF_INPUT_RESERVED2(dev, x) \
		(dev->is_p_region->parameter.isp.otf_input.reserved[2] = x)
#define IS_ISP_SET_PARAM_OTF_INPUT_RESERVED3(dev, x) \
		(dev->is_p_region->parameter.isp.otf_input.reserved[3] = x)
#define IS_ISP_SET_PARAM_OTF_INPUT_RESERVED4(dev, x) \
		(dev->is_p_region->parameter.isp.otf_input.reserved[4] = x)

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
#define IS_ISP_SET_PARAM_AA_FACE(dev, x) \
		(dev->is_p_region->parameter.isp.aa.face = x)
#define IS_ISP_SET_PARAM_AA_WIN_POS_X(dev, x) \
		(dev->is_p_region->parameter.isp.aa.win_pos_x = x)
#define IS_ISP_SET_PARAM_AA_WIN_POS_Y(dev, x) \
		(dev->is_p_region->parameter.isp.aa.win_pos_y = x)
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
#define IS_ISP_SET_PARAM_ADJUST_SHUTTER_TIME_MIN(dev, x) \
		(dev->is_p_region->parameter.isp.adjust.shutter_time_min = x)
#define IS_ISP_SET_PARAM_ADJUST_SHUTTER_TIME_MAX(dev, x) \
		(dev->is_p_region->parameter.isp.adjust.shutter_time_max = x)
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

/* SCALER-C Macros */
#define IS_SCALERC_SET_PARAM_CONTROL_CMD(dev, x) \
	(dev->is_p_region->parameter.scalerc.control.cmd = x)
#define IS_SCALERC_SET_PARAM_CONTROL_BYPASS(dev, x) \
	(dev->is_p_region->parameter.scalerc.control.bypass = x)
#define IS_SCALERC_SET_PARAM_CONTROL_ERR(dev, x) \
	(dev->is_p_region->parameter.scalerc.control.err = x)

#define IS_SCALERC_SET_PARAM_OTF_INPUT_CMD(dev, x) \
	(dev->is_p_region->parameter.scalerc.otf_input.cmd = x)
#define IS_SCALERC_SET_PARAM_OTF_INPUT_WIDTH(dev, x) \
	(dev->is_p_region->parameter.scalerc.otf_input.width = x)
#define IS_SCALERC_SET_PARAM_OTF_INPUT_HEIGHT(dev, x) \
	(dev->is_p_region->parameter.scalerc.otf_input.height = x)
#define IS_SCALERC_SET_PARAM_OTF_INPUT_FORMAT(dev, x) \
	(dev->is_p_region->parameter.scalerc.otf_input.format = x)
#define IS_SCALERC_SET_PARAM_OTF_INPUT_BITWIDTH(dev, x) \
	(dev->is_p_region->parameter.scalerc.otf_input.bitwidth = x)
#define IS_SCALERC_SET_PARAM_OTF_INPUT_ORDER(dev, x) \
	(dev->is_p_region->parameter.scalerc.otf_input.order = x)
#define IS_SCALERC_SET_PARAM_OTF_INPUT_ERR(dev, x) \
	(dev->is_p_region->parameter.scalerc.otf_input.err = x)

#define IS_SCALERC_SET_PARAM_EFFECT_CMD(dev, x) \
	(dev->is_p_region->parameter.scalerc.effect.cmd = x)
#define IS_SCALERC_SET_PARAM_EFFECT_ERR(dev, x) \
	(dev->is_p_region->parameter.scalerc.effect.err = x)

#define IS_SCALERC_SET_PARAM_CROP_CMD(dev, x) \
	(dev->is_p_region->parameter.scalerc.crop.cmd = x)
#define IS_SCALERC_SET_PARAM_CROP_POS_X(dev, x) \
	(dev->is_p_region->parameter.scalerc.crop.pos_x = x)
#define IS_SCALERC_SET_PARAM_CROP_POS_Y(dev, x) \
	(dev->is_p_region->parameter.scalerc.crop.pos_y = x)
#define IS_SCALERC_SET_PARAM_CROP_WIDTH(dev, x) \
	(dev->is_p_region->parameter.scalerc.crop.crop_width = x)
#define IS_SCALERC_SET_PARAM_CROP_HEIGHT(dev, x) \
	(dev->is_p_region->parameter.scalerc.crop.crop_height = x)
#define IS_SCALERC_SET_PARAM_CROP_ERR(dev, x) \
	(dev->is_p_region->parameter.scalerc.crop.err = x)

#define IS_SCALERC_SET_PARAM_SCALING_CMD(dev, x) \
	(dev->is_p_region->parameter.scalerc.scale.cmd = x)
#define IS_SCALERC_SET_PARAM_SCALING_PRE_H_RATIO(dev, x) \
	(dev->is_p_region->parameter.scalerc.scale.pre_h_ratio = x)
#define IS_SCALERC_SET_PARAM_SCALING_PRE_V_RATIO(dev, x) \
	(dev->is_p_region->parameter.scalerc.scale.pre_v_ratio = x)
#define IS_SCALERC_SET_PARAM_SCALING_SH_FACTOR(dev, x) \
	(dev->is_p_region->parameter.scalerc.scale.sh_factor = x)
#define IS_SCALERC_SET_PARAM_SCALING_H_RATIO(dev, x) \
	(dev->is_p_region->parameter.scalerc.scale.h_ratio = x)
#define IS_SCALERC_SET_PARAM_SCALING_V_RATIO(dev, x) \
	(dev->is_p_region->parameter.scalerc.scale.v_ratio = x)
#define IS_SCALERC_SET_PARAM_SCALING_ERR(dev, x) \
	(dev->is_p_region->parameter.scalerc.scale.err = x)

#define IS_SCALERC_SET_PARAM_OTF_OUTPUT_CMD(dev, x) \
	(dev->is_p_region->parameter.scalerc.otf_output.cmd = x)
#define IS_SCALERC_SET_PARAM_OTF_OUTPUT_WIDTH(dev, x) \
	(dev->is_p_region->parameter.scalerc.otf_output.width = x)
#define IS_SCALERC_SET_PARAM_OTF_OUTPUT_HEIGHT(dev, x) \
	(dev->is_p_region->parameter.scalerc.otf_output.height = x)
#define IS_SCALERC_SET_PARAM_OTF_OUTPUT_FORMAT(dev, x) \
	(dev->is_p_region->parameter.scalerc.otf_output.format = x)
#define IS_SCALERC_SET_PARAM_OTF_OUTPUT_BITWIDTH(dev, x) \
	(dev->is_p_region->parameter.scalerc.otf_output.bitwidth = x)
#define IS_SCALERC_SET_PARAM_OTF_OUTPUT_ORDER(dev, x) \
	(dev->is_p_region->parameter.scalerc.otf_output.order = x)
#define IS_SCALERC_SET_PARAM_OTF_OUTPUT_ERR(dev, x) \
	(dev->is_p_region->parameter.scalerc.otf_output.err = x)

#define IS_SCALERC_SET_PARAM_DMA_OUTPUT_CMD(dev, x) \
	(dev->is_p_region->parameter.scalerc.dma_output.cmd = x)
#define IS_SCALERC_SET_PARAM_DMA_OUTPUT_WIDTH(dev, x) \
	(dev->is_p_region->parameter.scalerc.dma_output.width = x)
#define IS_SCALERC_SET_PARAM_DMA_OUTPUT_HEIGHT(dev, x) \
	(dev->is_p_region->parameter.scalerc.dma_output.height = x)
#define IS_SCALERC_SET_PARAM_DMA_OUTPUT_FORMAT(dev, x) \
	(dev->is_p_region->parameter.scalerc.dma_output.format = x)
#define IS_SCALERC_SET_PARAM_DMA_OUTPUT_BITWIDTH(dev, x) \
	(dev->is_p_region->parameter.scalerc.dma_output.bitwidth = x)
#define IS_SCALERC_SET_PARAM_DMA_OUTPUT_PLANE(dev, x) \
	(dev->is_p_region->parameter.scalerc.dma_output.plane = x)
#define IS_SCALERC_SET_PARAM_DMA_OUTPUT_ORDER(dev, x) \
	(dev->is_p_region->parameter.scalerc.dma_output.order = x)
#define IS_SCALERC_SET_PARAM_DMA_OUTPUT_BUFFERNUM(dev, x) \
	(dev->is_p_region->parameter.scalerc.dma_output.buffer_number = x)
#define IS_SCALERC_SET_PARAM_DMA_OUTPUT_BUFFERADDR(dev, x) \
	(dev->is_p_region->parameter.scalerc.dma_output.buffer_address = x)
#define IS_SCALERC_SET_PARAM_DMA_OUTPUT_OUTPATH(dev, x) \
	(dev->is_p_region->parameter.scalerc.dma_output.reserved[0] = x)
#define IS_SCALERC_SET_PARAM_DMA_OUTPUT_ERR(dev, x) \
	(dev->is_p_region->parameter.scalerc.dma_output.err = x)

/* ODC Macros */
#define IS_ODC_SET_PARAM_CONTROL_CMD(dev, x) \
	(dev->is_p_region->parameter.odc.control.cmd = x)
#define IS_ODC_SET_PARAM_CONTROL_BYPASS(dev, x) \
	(dev->is_p_region->parameter.odc.control.bypass = x)
#define IS_ODC_SET_PARAM_CONTROL_ERR(dev, x) \
	(dev->is_p_region->parameter.odc.control.err = x)

#define IS_ODC_SET_PARAM_OTF_INPUT_CMD(dev, x) \
	(dev->is_p_region->parameter.odc.otf_input.cmd = x)
#define IS_ODC_SET_PARAM_OTF_INPUT_WIDTH(dev, x) \
	(dev->is_p_region->parameter.odc.otf_input.width = x)
#define IS_ODC_SET_PARAM_OTF_INPUT_HEIGHT(dev, x) \
	(dev->is_p_region->parameter.odc.otf_input.height = x)
#define IS_ODC_SET_PARAM_OTF_INPUT_FORMAT(dev, x) \
	(dev->is_p_region->parameter.odc.otf_input.format = x)
#define IS_ODC_SET_PARAM_OTF_INPUT_BITWIDTH(dev, x) \
	(dev->is_p_region->parameter.odc.otf_input.bitwidth = x)
#define IS_ODC_SET_PARAM_OTF_INPUT_ORDER(dev, x) \
	(dev->is_p_region->parameter.odc.otf_input.order = x)
#define IS_ODC_SET_PARAM_OTF_INPUT_ERR(dev, x) \
	(dev->is_p_region->parameter.odc.otf_input.err = x)

#define IS_ODC_SET_PARAM_OTF_OUTPUT_CMD(dev, x) \
	(dev->is_p_region->parameter.odc.otf_output.cmd = x)
#define IS_ODC_SET_PARAM_OTF_OUTPUT_WIDTH(dev, x) \
	(dev->is_p_region->parameter.odc.otf_output.width = x)
#define IS_ODC_SET_PARAM_OTF_OUTPUT_HEIGHT(dev, x) \
	(dev->is_p_region->parameter.odc.otf_output.height = x)
#define IS_ODC_SET_PARAM_OTF_OUTPUT_FORMAT(dev, x) \
	(dev->is_p_region->parameter.odc.otf_output.format = x)
#define IS_ODC_SET_PARAM_OTF_OUTPUT_BITWIDTH(dev, x) \
	(dev->is_p_region->parameter.odc.otf_output.bitwidth = x)
#define IS_ODC_SET_PARAM_OTF_OUTPUT_ORDER(dev, x) \
	(dev->is_p_region->parameter.odc.otf_output.order = x)
#define IS_ODC_SET_PARAM_OTF_OUTPUT_ERR(dev, x) \
	(dev->is_p_region->parameter.odc.otf_output.err = x)

/* DIS Macros */
#define IS_DIS_SET_PARAM_CONTROL_CMD(dev, x) \
	(dev->is_p_region->parameter.dis.control.cmd = x)
#define IS_DIS_SET_PARAM_CONTROL_BYPASS(dev, x) \
	(dev->is_p_region->parameter.dis.control.bypass = x)
#define IS_DIS_SET_PARAM_CONTROL_ERR(dev, x) \
	(dev->is_p_region->parameter.dis.control.err = x)

#define IS_DIS_SET_PARAM_OTF_INPUT_CMD(dev, x) \
	(dev->is_p_region->parameter.dis.otf_input.cmd = x)
#define IS_DIS_SET_PARAM_OTF_INPUT_WIDTH(dev, x) \
	(dev->is_p_region->parameter.dis.otf_input.width = x)
#define IS_DIS_SET_PARAM_OTF_INPUT_HEIGHT(dev, x) \
	(dev->is_p_region->parameter.dis.otf_input.height = x)
#define IS_DIS_SET_PARAM_OTF_INPUT_FORMAT(dev, x) \
	(dev->is_p_region->parameter.dis.otf_input.format = x)
#define IS_DIS_SET_PARAM_OTF_INPUT_BITWIDTH(dev, x) \
	(dev->is_p_region->parameter.dis.otf_input.bitwidth = x)
#define IS_DIS_SET_PARAM_OTF_INPUT_ORDER(dev, x) \
	(dev->is_p_region->parameter.dis.otf_input.order = x)
#define IS_DIS_SET_PARAM_OTF_INPUT_ERR(dev, x) \
	(dev->is_p_region->parameter.dis.otf_input.err = x)

#define IS_DIS_SET_PARAM_OTF_OUTPUT_CMD(dev, x) \
	(dev->is_p_region->parameter.dis.otf_output.cmd = x)
#define IS_DIS_SET_PARAM_OTF_OUTPUT_WIDTH(dev, x) \
	(dev->is_p_region->parameter.dis.otf_output.width = x)
#define IS_DIS_SET_PARAM_OTF_OUTPUT_HEIGHT(dev, x) \
	(dev->is_p_region->parameter.dis.otf_output.height = x)
#define IS_DIS_SET_PARAM_OTF_OUTPUT_FORMAT(dev, x) \
	(dev->is_p_region->parameter.dis.otf_output.format = x)
#define IS_DIS_SET_PARAM_OTF_OUTPUT_BITWIDTH(dev, x) \
	(dev->is_p_region->parameter.dis.otf_output.bitwidth = x)
#define IS_DIS_SET_PARAM_OTF_OUTPUT_ORDER(dev, x) \
	(dev->is_p_region->parameter.dis.otf_output.order = x)
#define IS_DIS_SET_PARAM_OTF_OUTPUT_ERR(dev, x) \
	(dev->is_p_region->parameter.dis.otf_output.err = x)

/* TDNR Macros */
#define IS_TDNR_SET_PARAM_CONTROL_CMD(dev, x) \
	(dev->is_p_region->parameter.tdnr.control.cmd = x)
#define IS_TDNR_SET_PARAM_CONTROL_BYPASS(dev, x) \
	(dev->is_p_region->parameter.tdnr.control.bypass = x)
#define IS_TDNR_SET_PARAM_CONTROL_ERR(dev, x) \
	(dev->is_p_region->parameter.tdnr.control.err = x)

#define IS_TDNR_SET_PARAM_OTF_INPUT_CMD(dev, x) \
	(dev->is_p_region->parameter.tdnr.otf_input.cmd = x)
#define IS_TDNR_SET_PARAM_OTF_INPUT_WIDTH(dev, x) \
	(dev->is_p_region->parameter.tdnr.otf_input.width = x)
#define IS_TDNR_SET_PARAM_OTF_INPUT_HEIGHT(dev, x) \
	(dev->is_p_region->parameter.tdnr.otf_input.height = x)
#define IS_TDNR_SET_PARAM_OTF_INPUT_FORMAT(dev, x) \
	(dev->is_p_region->parameter.tdnr.otf_input.format = x)
#define IS_TDNR_SET_PARAM_OTF_INPUT_BITWIDTH(dev, x) \
	(dev->is_p_region->parameter.tdnr.otf_input.bitwidth = x)
#define IS_TDNR_SET_PARAM_OTF_INPUT_ORDER(dev, x) \
	(dev->is_p_region->parameter.tdnr.otf_input.order = x)
#define IS_TDNR_SET_PARAM_OTF_INPUT_ERR(dev, x) \
	(dev->is_p_region->parameter.tdnr.otf_input.err = x)

#define IS_TDNR_SET_PARAM_FRAME_CMD(dev, x) \
	(dev->is_p_region->parameter.tdnr.frame.cmd = x)
#define IS_TDNR_SET_PARAM_FRAME_ERR(dev, x) \
	(dev->is_p_region->parameter.tdnr.frame.err = x)

#define IS_TDNR_SET_PARAM_OTF_OUTPUT_CMD(dev, x) \
	(dev->is_p_region->parameter.tdnr.otf_output.cmd = x)
#define IS_TDNR_SET_PARAM_OTF_OUTPUT_WIDTH(dev, x) \
	(dev->is_p_region->parameter.tdnr.otf_output.width = x)
#define IS_TDNR_SET_PARAM_OTF_OUTPUT_HEIGHT(dev, x) \
	(dev->is_p_region->parameter.tdnr.otf_output.height = x)
#define IS_TDNR_SET_PARAM_OTF_OUTPUT_FORMAT(dev, x) \
	(dev->is_p_region->parameter.tdnr.otf_output.format = x)
#define IS_TDNR_SET_PARAM_OTF_OUTPUT_BITWIDTH(dev, x) \
	(dev->is_p_region->parameter.tdnr.otf_output.bitwidth = x)
#define IS_TDNR_SET_PARAM_OTF_OUTPUT_ORDER(dev, x) \
	(dev->is_p_region->parameter.tdnr.otf_output.order = x)
#define IS_TDNR_SET_PARAM_OTF_OUTPUT_ERR(dev, x) \
	(dev->is_p_region->parameter.tdnr.otf_output.err = x)

#define IS_TDNR_SET_PARAM_DMA_OUTPUT_CMD(dev, x) \
	(dev->is_p_region->parameter.tdnr.dma_output.cmd = x)
#define IS_TDNR_SET_PARAM_DMA_OUTPUT_WIDTH(dev, x) \
	(dev->is_p_region->parameter.tdnr.dma_output.width = x)
#define IS_TDNR_SET_PARAM_DMA_OUTPUT_HEIGHT(dev, x) \
	(dev->is_p_region->parameter.tdnr.dma_output.height = x)
#define IS_TDNR_SET_PARAM_DMA_OUTPUT_FORMAT(dev, x) \
	(dev->is_p_region->parameter.tdnr.dma_output.format = x)
#define IS_TDNR_SET_PARAM_DMA_OUTPUT_BITWIDTH(dev, x) \
	(dev->is_p_region->parameter.tdnr.dma_output.bitwidth = x)
#define IS_TDNR_SET_PARAM_DMA_OUTPUT_PLANE(dev, x) \
	(dev->is_p_region->parameter.tdnr.dma_output.plane = x)
#define IS_TDNR_SET_PARAM_DMA_OUTPUT_ORDER(dev, x) \
	(dev->is_p_region->parameter.tdnr.dma_output.order = x)
#define IS_TDNR_SET_PARAM_DMA_OUTPUT_BUFFERNUM(dev, x) \
	(dev->is_p_region->parameter.tdnr.dma_output.buffer_number = x)
#define IS_TDNR_SET_PARAM_DMA_OUTPUT_BUFFERADDR(dev, x) \
	(dev->is_p_region->parameter.tdnr.dma_output.buffer_address = x)
#define IS_TDNR_SET_PARAM_DMA_OUTPUT_ERR(dev, x) \
	(dev->is_p_region->parameter.tdnr.dma_output.err = x)

/* SCALER-P Macros */
#define IS_SCALERP_SET_PARAM_CONTROL_CMD(dev, x) \
	(dev->is_p_region->parameter.scalerp.control.cmd = x)
#define IS_SCALERP_SET_PARAM_CONTROL_BYPASS(dev, x) \
	(dev->is_p_region->parameter.scalerp.control.bypass = x)
#define IS_SCALERP_SET_PARAM_CONTROL_ERR(dev, x) \
	(dev->is_p_region->parameter.scalerp.control.err = x)

#define IS_SCALERP_SET_PARAM_OTF_INPUT_CMD(dev, x) \
	(dev->is_p_region->parameter.scalerp.otf_input.cmd = x)
#define IS_SCALERP_SET_PARAM_OTF_INPUT_WIDTH(dev, x) \
	(dev->is_p_region->parameter.scalerp.otf_input.width = x)
#define IS_SCALERP_SET_PARAM_OTF_INPUT_HEIGHT(dev, x) \
	(dev->is_p_region->parameter.scalerp.otf_input.height = x)
#define IS_SCALERP_SET_PARAM_OTF_INPUT_FORMAT(dev, x) \
	(dev->is_p_region->parameter.scalerp.otf_input.format = x)
#define IS_SCALERP_SET_PARAM_OTF_INPUT_BITWIDTH(dev, x) \
	(dev->is_p_region->parameter.scalerp.otf_input.bitwidth = x)
#define IS_SCALERP_SET_PARAM_OTF_INPUT_ORDER(dev, x) \
	(dev->is_p_region->parameter.scalerp.otf_input.order = x)
#define IS_SCALERP_SET_PARAM_OTF_INPUT_ERR(dev, x) \
	(dev->is_p_region->parameter.scalerp.otf_input.err = x)

#define IS_SCALERP_SET_PARAM_EFFECT_CMD(dev, x) \
	(dev->is_p_region->parameter.scalerp.effect.cmd = x)
#define IS_SCALERP_SET_PARAM_EFFECT_ERR(dev, x) \
	(dev->is_p_region->parameter.scalerp.effect.err = x)

#define IS_SCALERP_SET_PARAM_CROP_CMD(dev, x) \
	(dev->is_p_region->parameter.scalerp.crop.cmd = x)
#define IS_SCALERP_SET_PARAM_CROP_POS_X(dev, x) \
	(dev->is_p_region->parameter.scalerp.crop.pos_x = x)
#define IS_SCALERP_SET_PARAM_CROP_POS_Y(dev, x) \
	(dev->is_p_region->parameter.scalerp.crop.pos_y = x)
#define IS_SCALERP_SET_PARAM_CROP_WIDTH(dev, x) \
	(dev->is_p_region->parameter.scalerp.crop.crop_width = x)
#define IS_SCALERP_SET_PARAM_CROP_HEIGHT(dev, x) \
	(dev->is_p_region->parameter.scalerp.crop.crop_height = x)
#define IS_SCALERP_SET_PARAM_CROP_ERR(dev, x) \
	(dev->is_p_region->parameter.scalerp.crop.err = x)

#define IS_SCALERP_SET_PARAM_SCALING_CMD(dev, x) \
	(dev->is_p_region->parameter.scalerp.scale.cmd = x)
#define IS_SCALERP_SET_PARAM_SCALING_PRE_H_RATIO(dev, x) \
	(dev->is_p_region->parameter.scalerp.scale.pre_h_ratio = x)
#define IS_SCALERP_SET_PARAM_SCALING_PRE_V_RATIO(dev, x) \
	(dev->is_p_region->parameter.scalerp.scale.pre_v_ratio = x)
#define IS_SCALERP_SET_PARAM_SCALING_SH_FACTOR(dev, x) \
	(dev->is_p_region->parameter.scalerp.scale.sh_factor = x)
#define IS_SCALERP_SET_PARAM_SCALING_H_RATIO(dev, x) \
	(dev->is_p_region->parameter.scalerp.scale.h_ratio = x)
#define IS_SCALERP_SET_PARAM_SCALING_V_RATIO(dev, x) \
	(dev->is_p_region->parameter.scalerp.scale.v_ratio = x)
#define IS_SCALERP_SET_PARAM_SCALING_ERR(dev, x) \
	(dev->is_p_region->parameter.scalerp.scale.err = x)

#define IS_SCALERP_SET_PARAM_ROTATION_CMD(dev, x) \
	(dev->is_p_region->parameter.scalerp.rotation.cmd = x)
#define IS_SCALERP_SET_PARAM_ROTATION_ERR(dev, x) \
	(dev->is_p_region->parameter.scalerp.rotation.err = x)

#define IS_SCALERP_SET_PARAM_FLIP_CMD(dev, x) \
	(dev->is_p_region->parameter.scalerp.flip.cmd = x)
#define IS_SCALERP_SET_PARAM_FLIP_ERR(dev, x) \
	(dev->is_p_region->parameter.scalerp.flip.err = x)

#define IS_SCALERP_SET_PARAM_OTF_OUTPUT_CMD(dev, x) \
	(dev->is_p_region->parameter.scalerp.otf_output.cmd = x)
#define IS_SCALERP_SET_PARAM_OTF_OUTPUT_WIDTH(dev, x) \
	(dev->is_p_region->parameter.scalerp.otf_output.width = x)
#define IS_SCALERP_SET_PARAM_OTF_OUTPUT_HEIGHT(dev, x) \
	(dev->is_p_region->parameter.scalerp.otf_output.height = x)
#define IS_SCALERP_SET_PARAM_OTF_OUTPUT_FORMAT(dev, x) \
	(dev->is_p_region->parameter.scalerp.otf_output.format = x)
#define IS_SCALERP_SET_PARAM_OTF_OUTPUT_BITWIDTH(dev, x) \
	(dev->is_p_region->parameter.scalerp.otf_output.bitwidth = x)
#define IS_SCALERP_SET_PARAM_OTF_OUTPUT_ORDER(dev, x) \
	(dev->is_p_region->parameter.scalerp.otf_output.order = x)
#define IS_SCALERP_SET_PARAM_OTF_OUTPUT_ERR(dev, x) \
	(dev->is_p_region->parameter.scalerp.otf_output.err = x)

#define IS_SCALERP_SET_PARAM_DMA_OUTPUT_CMD(dev, x) \
	(dev->is_p_region->parameter.scalerp.dma_output.cmd = x)
#define IS_SCALERP_SET_PARAM_DMA_OUTPUT_WIDTH(dev, x) \
	(dev->is_p_region->parameter.scalerp.dma_output.width = x)
#define IS_SCALERP_SET_PARAM_DMA_OUTPUT_HEIGHT(dev, x) \
	(dev->is_p_region->parameter.scalerp.dma_output.height = x)
#define IS_SCALERP_SET_PARAM_DMA_OUTPUT_FORMAT(dev, x) \
	(dev->is_p_region->parameter.scalerp.dma_output.format = x)
#define IS_SCALERP_SET_PARAM_DMA_OUTPUT_BITWIDTH(dev, x) \
	(dev->is_p_region->parameter.scalerp.dma_output.bitwidth = x)
#define IS_SCALERP_SET_PARAM_DMA_OUTPUT_PLANE(dev, x) \
	(dev->is_p_region->parameter.scalerp.dma_output.plane = x)
#define IS_SCALERP_SET_PARAM_DMA_OUTPUT_ORDER(dev, x) \
	(dev->is_p_region->parameter.scalerp.dma_output.order = x)
#define IS_SCALERP_SET_PARAM_DMA_OUTPUT_BUFFERNUM(dev, x) \
	(dev->is_p_region->parameter.scalerp.dma_output.buffer_number = x)
#define IS_SCALERP_SET_PARAM_DMA_OUTPUT_BUFFERADDR(dev, x) \
	(dev->is_p_region->parameter.scalerp.dma_output.buffer_address = x)
#define IS_SCALERP_SET_PARAM_DMA_OUTPUT_ERR(dev, x) \
	(dev->is_p_region->parameter.scalerp.dma_output.err = x)

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

#ifndef BIT0
#define  BIT0     0x00000001
#define  BIT1     0x00000002
#define  BIT2     0x00000004
#define  BIT3     0x00000008
#define  BIT4     0x00000010
#define  BIT5     0x00000020
#define  BIT6     0x00000040
#define  BIT7     0x00000080
#define  BIT8     0x00000100
#define  BIT9     0x00000200
#define  BIT10    0x00000400
#define  BIT11    0x00000800
#define  BIT12    0x00001000
#define  BIT13    0x00002000
#define  BIT14    0x00004000
#define  BIT15    0x00008000
#define  BIT16    0x00010000
#define  BIT17    0x00020000
#define  BIT18    0x00040000
#define  BIT19    0x00080000
#define  BIT20    0x00100000
#define  BIT21    0x00200000
#define  BIT22    0x00400000
#define  BIT23    0x00800000
#define  BIT24    0x01000000
#define  BIT25    0x02000000
#define  BIT26    0x04000000
#define  BIT27    0x08000000
#define  BIT28    0x10000000
#define  BIT29    0x20000000
#define  BIT30    0x40000000
#define  BIT31    0x80000000
#define  BIT32    0x0000000100000000ULL
#define  BIT33    0x0000000200000000ULL
#define  BIT34    0x0000000400000000ULL
#define  BIT35    0x0000000800000000ULL
#define  BIT36    0x0000001000000000ULL
#define  BIT37    0x0000002000000000ULL
#define  BIT38    0x0000004000000000ULL
#define  BIT39    0x0000008000000000ULL
#define  BIT40    0x0000010000000000ULL
#define  BIT41    0x0000020000000000ULL
#define  BIT42    0x0000040000000000ULL
#define  BIT43    0x0000080000000000ULL
#define  BIT44    0x0000100000000000ULL
#define  BIT45    0x0000200000000000ULL
#define  BIT46    0x0000400000000000ULL
#define  BIT47    0x0000800000000000ULL
#define  BIT48    0x0001000000000000ULL
#define  BIT49    0x0002000000000000ULL
#define  BIT50    0x0004000000000000ULL
#define  BIT51    0x0008000000000000ULL
#define  BIT52    0x0010000000000000ULL
#define  BIT53    0x0020000000000000ULL
#define  BIT54    0x0040000000000000ULL
#define  BIT55    0x0080000000000000ULL
#define  BIT56    0x0100000000000000ULL
#define  BIT57    0x0200000000000000ULL
#define  BIT58    0x0400000000000000ULL
#define  BIT59    0x0800000000000000ULL
#define  BIT60    0x1000000000000000ULL
#define  BIT61    0x2000000000000000ULL
#define  BIT62    0x4000000000000000ULL
#define  BIT63    0x8000000000000000ULL
#define  INC_BIT(bit) (bit<<1)
#define  INC_NUM(bit) (bit + 1)
#endif

#define MAGIC_NUMBER 0x01020304

#define PARAMETER_MAX_SIZE    64  /* in byte */
#define PARAMETER_MAX_MEMBER  (PARAMETER_MAX_SIZE/4)

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
	PARAM_SCALERC_CROP,
	PARAM_SCALERC_SCALING = 30,
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
	PARAM_SCALERP_CROP,
	PARAM_SCALERP_SCALING,
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
			: (1<<(OFFSET_TO_NUM(offset)-32))
#define LOWBIT_OF_NUM(num)		(num >= 32 ? 0 : BIT0<<num)
#define HIGHBIT_OF_NUM(num)		(num >= 32 ? BIT0<<(num-32) : 0)

/* 0~31 */
#define PARAM_GLOBAL_SHOTMODE		0
#define PARAM_SENSOR_CONTROL		INC_NUM(PARAM_GLOBAL_SHOTMODE)
#define PARAM_SENSOR_OTF_OUTPUT		INC_NUM(PARAM_SENSOR_CONTROL)
#define PARAM_SENSOR_FRAME_RATE		INC_NUM(PARAM_SENSOR_OTF_OUTPUT)
#define PARAM_BUFFER_CONTROL		INC_NUM(PARAM_SENSOR_FRAME_RATE)
#define PARAM_BUFFER_OTF_INPUT		INC_NUM(PARAM_BUFFER_CONTROL)
#define PARAM_BUFFER_OTF_OUTPUT		INC_NUM(PARAM_BUFFER_OTF_INPUT)
#define PARAM_ISP_CONTROL		INC_NUM(PARAM_BUFFER_OTF_OUTPUT)
#define PARAM_ISP_OTF_INPUT		INC_NUM(PARAM_ISP_CONTROL)
#define PARAM_ISP_DMA1_INPUT		INC_NUM(PARAM_ISP_OTF_INPUT)
#define PARAM_ISP_DMA2_INPUT		INC_NUM(PARAM_ISP_DMA1_INPUT)
#define PARAM_ISP_AA			INC_NUM(PARAM_ISP_DMA2_INPUT)
#define PARAM_ISP_FLASH			INC_NUM(PARAM_ISP_AA)
#define PARAM_ISP_AWB			INC_NUM(PARAM_ISP_FLASH)
#define PARAM_ISP_IMAGE_EFFECT		INC_NUM(PARAM_ISP_AWB)
#define PARAM_ISP_ISO			INC_NUM(PARAM_ISP_IMAGE_EFFECT)
#define PARAM_ISP_ADJUST		INC_NUM(PARAM_ISP_ISO)
#define PARAM_ISP_METERING		INC_NUM(PARAM_ISP_ADJUST)
#define PARAM_ISP_AFC			INC_NUM(PARAM_ISP_METERING)
#define PARAM_ISP_OTF_OUTPUT		INC_NUM(PARAM_ISP_AFC)
#define PARAM_ISP_DMA1_OUTPUT		INC_NUM(PARAM_ISP_OTF_OUTPUT)
#define PARAM_ISP_DMA2_OUTPUT		INC_NUM(PARAM_ISP_DMA1_OUTPUT)
#define PARAM_DRC_CONTROL		INC_NUM(PARAM_ISP_DMA2_OUTPUT)
#define PARAM_DRC_OTF_INPUT		INC_NUM(PARAM_DRC_CONTROL)
#define PARAM_DRC_DMA_INPUT		INC_NUM(PARAM_DRC_OTF_INPUT)
#define PARAM_DRC_OTF_OUTPUT		INC_NUM(PARAM_DRC_DMA_INPUT)
#define PARAM_SCALERC_CONTROL		INC_NUM(PARAM_DRC_OTF_OUTPUT)
#define PARAM_SCALERC_OTF_INPUT		INC_NUM(PARAM_SCALERC_CONTROL)
#define PARAM_SCALERC_IMAGE_EFFECT	INC_NUM(PARAM_SCALERC_OTF_INPUT)
#define PARAM_SCALERC_CROP		INC_NUM(PARAM_SCALERC_IMAGE_EFFECT)
#define PARAM_SCALERC_SCALING		INC_NUM(PARAM_SCALERC_CROP)
#define PARAM_SCALERC_OTF_OUTPUT	INC_NUM(PARAM_SCALERC_SCALING)

/* 32~63 */
#define PARAM_SCALERC_DMA_OUTPUT	INC_NUM(PARAM_SCALERC_OTF_OUTPUT)
#define PARAM_ODC_CONTROL		INC_NUM(PARAM_SCALERC_DMA_OUTPUT)
#define PARAM_ODC_OTF_INPUT		INC_NUM(PARAM_ODC_CONTROL)
#define PARAM_ODC_OTF_OUTPUT		INC_NUM(PARAM_ODC_OTF_INPUT)
#define PARAM_DIS_CONTROL		INC_NUM(PARAM_ODC_OTF_OUTPUT)
#define PARAM_DIS_OTF_INPUT		INC_NUM(PARAM_DIS_CONTROL)
#define PARAM_DIS_OTF_OUTPUT		INC_NUM(PARAM_DIS_OTF_INPUT)
#define PARAM_TDNR_CONTROL		INC_NUM(PARAM_DIS_OTF_OUTPUT)
#define PARAM_TDNR_OTF_INPUT		INC_NUM(PARAM_TDNR_CONTROL)
#define PARAM_TDNR_1ST_FRAME		INC_NUM(PARAM_TDNR_OTF_INPUT)
#define PARAM_TDNR_OTF_OUTPUT		INC_NUM(PARAM_TDNR_1ST_FRAME)
#define PARAM_TDNR_DMA_OUTPUT		INC_NUM(PARAM_TDNR_OTF_OUTPUT)
#define PARAM_SCALERP_CONTROL		INC_NUM(PARAM_TDNR_DMA_OUTPUT)
#define PARAM_SCALERP_OTF_INPUT		INC_NUM(PARAM_SCALERP_CONTROL)
#define PARAM_SCALERP_IMAGE_EFFECT	INC_NUM(PARAM_SCALERP_OTF_INPUT)
#define PARAM_SCALERP_CROP		INC_NUM(PARAM_SCALERP_IMAGE_EFFECT)
#define PARAM_SCALERP_SCALING		INC_NUM(PARAM_SCALERP_CROP)
#define PARAM_SCALERP_ROTATION		INC_NUM(PARAM_SCALERP_SCALING)
#define PARAM_SCALERP_FLIP		INC_NUM(PARAM_SCALERP_ROTATION)
#define PARAM_SCALERP_OTF_OUTPUT	INC_NUM(PARAM_SCALERP_FLIP)
#define PARAM_SCALERP_DMA_OUTPUT	INC_NUM(PARAM_SCALERP_OTF_OUTPUT)
#define PARAM_FD_CONTROL		INC_NUM(PARAM_SCALERP_DMA_OUTPUT)
#define PARAM_FD_OTF_INPUT		INC_NUM(PARAM_FD_CONTROL)
#define PARAM_FD_DMA_INPUT		INC_NUM(PARAM_FD_OTF_INPUT)
#define PARAM_FD_CONFIG			INC_NUM(PARAM_FD_DMA_INPUT)
#define PARAM_END			INC_NUM(PARAM_FD_CONFIG)

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

/* Enumerations
*
*/
/* ----------------------  INTR map-------------------------------- */
enum interrupt_map{
	INTR_GENERAL = 0,
	INTR_FRAME_DONE_ISP = 1,
	INTR_FRAME_DONE_SCALERC = 2,
	INTR_FRAME_DONE_TDNR = 3,
	INTR_FRAME_DONE_SCALERP = 4
};

/* ----------------------  Input  ----------------------------------- */
enum control_command {
	CONTROL_COMMAND_STOP	= 0,
	CONTROL_COMMAND_START	= 1
};

enum bypass_command {
	CONTROL_BYPASS_DISABLE		= 0,
	CONTROL_BYPASS_ENABLE		= 1
};

enum control_error {
	CONTROL_ERROR_NO			= 0
};

enum otf_input_command {
	OTF_INPUT_COMMAND_DISABLE	= 0,
	OTF_INPUT_COMMAND_ENABLE	= 1
};

enum otf_input_format {
	OTF_INPUT_FORMAT_BAYER		= 0, /* 1 Channel */
	OTF_INPUT_FORMAT_YUV444		= 1, /* 3 Channel */
	OTF_INPUT_FORMAT_YUV422		= 2, /* 3 Channel */
	OTF_INPUT_FORMAT_YUV420		= 3, /* 3 Channel */
	OTF_INPUT_FORMAT_STRGEN_COLORBAR_BAYER = 10,
	OTF_INPUT_FORMAT_BAYER_DMA	= 11,
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
	OTF_INPUT_ORDER_BAYER_GR_BG         = 0,
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
	OTF_OUTPUT_ORDER_BAYER_GR_BG         = 0,
};

enum otf_output_error {
	OTF_OUTPUT_ERROR_NO = 0 /* Output Setting is done */
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
	ISP_AWB_COMMAND_ILLUMINATION	= 1
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
	ISP_ADJUST_COMMAND_AUTO		= 0,
	ISP_ADJUST_COMMAND_MANUAL	= 1
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

enum isp_afc_error {
	ISP_AFC_ERROR_NO	= 0 /* AFC setting is done */
};

/* --------------------------  Scaler  --------------------------------- */
enum scaler_imageeffect_command {
	SCALER_IMAGE_EFFECT_COMMNAD_DISABLE	= 0,
	SCALER_IMAGE_EFFECT_COMMNAD_SEPIA_CB	= 1,
	SCALER_IMAGE_EFFECT_COMMAND_SEPIA_CR	= 2,
	SCALER_IMAGE_EFFECT_COMMAND_NEGATIVE	= 3,
	SCALER_IMAGE_EFFECT_COMMAND_ARTFREEZE	= 4,
	SCALER_IMAGE_EFFECT_COMMAND_EMBOSSING	= 5,
	SCALER_IMAGE_EFFECT_COMMAND_SILHOUETTE	= 6
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
	TDNR_1ST_FRAME_ERROR_NO			= 0
		/*1st frame setting is done*/
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
	u32	cmd;
	u32	bypass;
	u32	reserved[PARAMETER_MAX_MEMBER-3];
	u32	err;
};

struct param_otf_input {
	u32	cmd;
	u32	width;
	u32	height;
	u32	format;
	u32	bitwidth;
	u32	order;
	u32	crop_offset_x;
	u32	crop_offset_y;
	u32	crop_width;
	u32	crop_height;
	u32	reserved[PARAMETER_MAX_MEMBER-11];
	u32	err;
};

struct param_dma_input {
	u32	cmd;
	u32	width;
	u32	height;
	u32	format;
	u32	bitwidth;
	u32	plane;
	u32	order;
	u32	buffer_number;
	u32	buffer_address;
	u32	reserved[PARAMETER_MAX_MEMBER-10];
	u32	err;
};

struct param_otf_output {
	u32	cmd;
	u32	width;
	u32	height;
	u32	format;
	u32	bitwidth;
	u32	order;
	u32	reserved[PARAMETER_MAX_MEMBER-7];
	u32	err;
};

struct param_dma_output {
	u32	cmd;
	u32	width;
	u32	height;
	u32	format;
	u32	bitwidth;
	u32	plane;
	u32	order;
	u32	buffer_number;
	u32	buffer_address;
	u32	notify_dma_done;
	u32	reserved[PARAMETER_MAX_MEMBER-11];
	u32	err;
};

struct param_global_shotmode {
	u32	cmd;
	u32	skip_frames;
	u32	reserved[PARAMETER_MAX_MEMBER-3];
	u32	err;
};

struct param_sensor_framerate {
	u32	frame_rate;
	u32	reserved[PARAMETER_MAX_MEMBER-2];
	u32	err;
};

#if 0
struct param_isp_aa {
	u32	cmd;
	u32	target;
	u32	mode;
	u32	face;
	u32	continuous;
	u32	win_pos_x;
	u32	win_pos_y;
	u32	win_width;
	u32	win_height;
	u32	manual_af_setting;
	u32	reserved[PARAMETER_MAX_MEMBER-11];
	u32	err;
};
#else
struct param_isp_aa {
	u32	cmd;
	u32	target;
	u32	mode;
	u32	face;
	u32	win_pos_x;
	u32	win_pos_y;
	u32	reserved[PARAMETER_MAX_MEMBER-7];
	u32	err;
};
#endif
struct param_isp_flash {
	u32	cmd;
	u32	redeye;
	u32	reserved[PARAMETER_MAX_MEMBER-3];
	u32	err;
};

struct param_isp_awb {
	u32	cmd;
	u32	illumination;
	u32	reserved[PARAMETER_MAX_MEMBER-3];
	u32	err;
};

struct param_isp_imageeffect {
	u32	cmd;
	u32	reserved[PARAMETER_MAX_MEMBER-2];
	u32	err;
};

struct param_isp_iso {
	u32	cmd;
	u32	value;
	u32	reserved[PARAMETER_MAX_MEMBER-3];
	u32	err;
};

struct param_isp_adjust {
	u32	cmd;
	s32	contrast;
	s32	saturation;
	s32	sharpness;
	s32	exposure;
	s32	brightness;
	s32	hue;
	s32	shutter_time_min;
	s32	shutter_time_max;
	u32	reserved[PARAMETER_MAX_MEMBER-10];
	u32	err;
};

struct param_isp_metering {
	u32	cmd;
	u32	win_pos_x;
	u32	win_pos_y;
	u32	win_width;
	u32	win_height;
	u32	reserved[PARAMETER_MAX_MEMBER-6];
	u32	err;
};

struct param_isp_afc {
	u32	cmd;
	u32	manual;
	u32	reserved[PARAMETER_MAX_MEMBER-3];
	u32	err;
};

struct param_scaler_imageeffect {
	u32	cmd;
	u32	reserved[PARAMETER_MAX_MEMBER-2];
	u32	err;
};

struct param_scaler_crop {
	u32	cmd;
	u32	pos_x;
	u32	pos_y;
	u32	crop_width;
	u32	crop_height;
	u32	reserved[PARAMETER_MAX_MEMBER-6];
	u32	err;
};

struct param_scaler_scaling {
	u32	cmd;
	u32	pre_h_ratio;
	u32	pre_v_ratio;
	u32	sh_factor;
	u32	h_ratio;
	u32	v_ratio;
	u32	reserved[PARAMETER_MAX_MEMBER-7];
	u32	err;
};

struct param_scaler_rotation {
	u32	cmd;
	u32	reserved[PARAMETER_MAX_MEMBER-2];
	u32	err;
};

struct param_scaler_flip {
	u32	cmd;
	u32	reserved[PARAMETER_MAX_MEMBER-2];
	u32	err;
};

struct param_3dnr_1stframe {
	u32	cmd;
	u32	reserved[PARAMETER_MAX_MEMBER-2];
	u32	err;
};

struct param_fd_config {
	u32	cmd;
	u32	max_number;
	u32	roll_angle;
	u32	yaw_angle;
	u32	smile_mode;
	u32	blink_mode;
	u32	eye_detect;
	u32	mouth_detect;
	u32	orientation;
	u32	orientation_value;
	u32	reserved[PARAMETER_MAX_MEMBER-11];
	u32	err;
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
	struct param_control		control;
	struct param_otf_input		otf_input;
	struct param_scaler_imageeffect	effect;
	struct param_scaler_crop	crop;
	struct param_scaler_scaling	scale;
	struct param_otf_output		otf_output;
	struct param_dma_output		dma_output;
};

struct odc_param {
	struct param_control		control;
	struct param_otf_input		otf_input;
	struct param_otf_output		otf_output;
};

struct dis_param {
	struct param_control		control;
	struct param_otf_input		otf_input;
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
	struct param_scaler_crop		crop;
	struct param_scaler_scaling		scale;
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

#define	NUMBER_OF_GAMMA_CURVE_POINTS	32

struct is_tune_gammacurve {
	u32 num_pts_x[NUMBER_OF_GAMMA_CURVE_POINTS];
	u32 num_pts_y_r[NUMBER_OF_GAMMA_CURVE_POINTS];
	u32 num_pts_y_g[NUMBER_OF_GAMMA_CURVE_POINTS];
	u32 num_pts_y_b[NUMBER_OF_GAMMA_CURVE_POINTS];
};

struct is_tune_region {
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
	struct exif_attribute	exif;
};

struct is_fd_rect {
	u32 offset_x;
	u32 offset_y;
	u32 width;
	u32 height;
};

struct is_face_marker {
	u32	frame_number;
	struct is_fd_rect face;
	struct is_fd_rect left_eye;
	struct is_fd_rect right_eye;
	struct is_fd_rect mouth;
	u32	roll_angle;
	u32	confidence;
	u32	smile_level;
	u32	blink_level;
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
	u32	sensor_frame_time;
	u32	sensor_exposure_time;
	u32	sensor_analog_gain;
	u32	req_lei;
};

#define MAX_FRAMEDESCRIPTOR_CONTEXT_NUM	(30*30)	/* 30 sec */
struct is_share_region {
	u32	frame_time;
	u32	exposure_time;
	s32	analog_gain;

	u32	r_gain;
	u32	g_gain;
	u32	b_gain;

	u32	af_position;
	u32	af_status;

	u32	frame_descp_onoff_control;
	u32	frame_descp_update_done;
	u32	frame_descp_idx;
	struct is_debug_frame_descriptor
		dbg_frame_descp_ctx[MAX_FRAMEDESCRIPTOR_CONTEXT_NUM];
};

#endif
