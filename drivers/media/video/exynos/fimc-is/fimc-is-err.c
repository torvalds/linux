/*
 * Samsung Exynos4 SoC series FIMC-IS slave interface driver
 *
 * Error log interface functions
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 * Contact: Younghwan Joo, <yhwan.joo@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>

#include "fimc-is-core.h"
#include "fimc-is-regs.h"
#include "fimc-is-param.h"
#include "fimc-is-cmd.h"
#include "fimc-is-err.h"

void fimc_is_print_param_err_name(u32 err)
{
	switch (err) {
	/* Common error */
	case ERROR_COMMON_CMD:
		printk(KERN_ERR
			"ERROR_COMMON_CMD : Invalid Command Error!!\n");
		break;
	case ERROR_COMMON_PARAMETER:
		printk(KERN_ERR
		"ERROR_COMMON_PARAMETER : Invalid Parameter Error!!\n");
		break;
	case ERROR_COMMON_SETFILE_LOAD:
		printk(KERN_ERR
		"ERROR_COMMON_SETFILE_LOAD : Illegal Setfile Loading!!\n");
		break;
	case ERROR_COMMON_SETFILE_ADJUST:
		printk(KERN_ERR
		"ERROR_COMMON_SETFILE_ADJUST : Setfile isn't adjusted!!\n");
		break;
	case ERROR_COMMON_SETFILE_INDEX:
		printk(KERN_ERR
		"ERROR_COMMON_SETFILE_INDEX : Index of setfile is not valid (0~MAX_SETFILE_NUM-1)!!\n");
		break;
	case ERROR_COMMON_INPUT_PATH:
		printk(KERN_ERR
		"ERROR_COMMON_INPUT_PATH : Input path can be changed in ready state!!\n");
		break;
	case ERROR_COMMON_INPUT_INIT:
		printk(KERN_ERR
		"ERROR_COMMON_INPUT_INIT : IP can not start if input path is not set!!\n");
		break;
	case ERROR_COMMON_OUTPUT_PATH:
		printk(KERN_ERR
		"ERROR_COMMON_OUTPUT_PATH : Output path can be changed in ready state (stop)!!\n");
		break;
	case ERROR_COMMON_OUTPUT_INIT:
		printk(KERN_ERR
		"ERROR_COMMON_OUTPUT_INIT : IP can not start if output path is not set!!\n");
		break;
	case ERROR_CONTROL_BYPASS:
		printk(KERN_ERR "ERROR_CONTROL_BYPASS!!\n");
		break;
	case ERROR_OTF_INPUT_FORMAT:
		printk(KERN_ERR
		"ERROR_OTF_INPUT_FORMAT!! : invalid format  (DRC: YUV444, FD: YUV444, 422, 420)\n");
		break;
	case ERROR_OTF_INPUT_WIDTH:
		printk(KERN_ERR
		"ERROR_OTF_INPUT_WIDTH!! : invalid width (DRC: 128~8192, FD: 32~8190)\n");
		break;
	case ERROR_OTF_INPUT_HEIGHT:
		printk(KERN_ERR
		"ERROR_OTF_INPUT_HEIGHT!! : invalid bit-width (DRC: 8~12bits, FD: 8bit)\n");
		break;
	case ERROR_OTF_INPUT_BIT_WIDTH:
		printk(KERN_ERR
		"ERROR_OTF_INPUT_BIT_WIDTH!! : invalid bit-width (DRC: 8~12bits, FD: 8bit)\n");
		break;
	case ERROR_DMA_INPUT_WIDTH:
		printk(KERN_ERR
		"ERROR_DMA_INPUT_WIDTH!! : invalid width (DRC: 128~8192, FD: 32~8190)\n");
		break;
	case ERROR_DMA_INPUT_HEIGHT:
		printk(KERN_ERR
		"ERROR_DMA_INPUT_HEIGHT!! : invalid height (DRC: 64~8192, FD: 16~8190)\n");
		break;
	case ERROR_DMA_INPUT_FORMAT:
		printk(KERN_ERR
		"ERROR_DMA_INPUT_FORMAT!! : invalid format (DRC: YUV444 or YUV422, FD: YUV444,422,420)\n");
		break;
	case ERROR_DMA_INPUT_BIT_WIDTH:
		printk(KERN_ERR
		"ERROR_DMA_INPUT_BIT_WIDTH!! : invalid bit-width (DRC: 8~12bits, FD: 8bit)\n");
		break;
	case ERROR_DMA_INPUT_ORDER:
		printk(KERN_ERR
		"ERROR_DMA_INPUT_ORDER!! : invalid order(DRC: YYCbCrorYCbYCr,FD:NO,YYCbCr,YCbYCr,CbCr,CrCb)\n");
		break;
	case ERROR_DMA_INPUT_PLANE:
		printk(KERN_ERR
		"ERROR_DMA_INPUT_PLANE!! : invalid palne (DRC: 3, FD: 1, 2, 3)\n");
		break;
	case ERROR_OTF_OUTPUT_WIDTH:
		printk(KERN_ERR
		"ERROR_OTF_OUTPUT_WIDTH!! : invalid width (DRC: 128~8192)\n");
		break;
	case ERROR_OTF_OUTPUT_HEIGHT:
		printk(KERN_ERR
		"ERROR_OTF_OUTPUT_HEIGHT!! : invalid height (DRC: 64~8192)\n");
		break;
	case ERROR_OTF_OUTPUT_FORMAT:
		printk(KERN_ERR
		"ERROR_OTF_OUTPUT_FORMAT!! : invalid format (DRC: YUV444)\n");
		break;
	case ERROR_OTF_OUTPUT_BIT_WIDTH:
		printk(KERN_ERR
		"ERROR_OTF_OUTPUT_BIT_WIDTH!! : invalid bit-width (DRC: 8~12bits, FD: 8bit)\n");
		break;
	case ERROR_DMA_OUTPUT_WIDTH:
		printk(KERN_ERR "ERROR_DMA_OUTPUT_WIDTH!!\n");
		break;
	case ERROR_DMA_OUTPUT_HEIGHT:
		printk(KERN_ERR "ERROR_DMA_OUTPUT_HEIGHT!!\n");
		break;
	case ERROR_DMA_OUTPUT_FORMAT:
		printk(KERN_ERR "ERROR_DMA_OUTPUT_FORMAT!!\n");
		break;
	case ERROR_DMA_OUTPUT_BIT_WIDTH:
		printk(KERN_ERR "ERROR_DMA_OUTPUT_BIT_WIDTH!!\n");
		break;
	case ERROR_DMA_OUTPUT_PLANE:
		printk(KERN_ERR "ERROR_DMA_OUTPUT_PLANE!!\n");
		break;
	case ERROR_DMA_OUTPUT_ORDER:
		printk(KERN_ERR "ERROR_DMA_OUTPUT_ORDER!!\n");
		break;
	/* SENSOR Error(100~199) */
	case ERROR_SENSOR_I2C_FAIL:
		printk(KERN_ERR "ERROR_SENSOR_I2C_FAIL!!\n");
		break;
	case ERROR_SENSOR_INVALID_FRAMERATE:
		printk(KERN_ERR "ERROR_SENSOR_INVALID_FRAMERATE!!\n");
		break;
	case ERROR_SENSOR_INVALID_EXPOSURETIME:
		printk(KERN_ERR "ERROR_SENSOR_INVALID_EXPOSURETIME!!\n");
		break;
	case ERROR_SENSOR_INVALID_SIZE:
		printk(KERN_ERR "ERROR_SENSOR_INVALID_SIZE!!\n");
		break;
	case ERROR_SENSOR_INVALID_SETTING:
		printk(KERN_ERR "ERROR_SENSOR_INVALID_SETTING!!\n");
		break;
	case ERROR_SENSOR_ACTURATOR_INIT_FAIL:
		printk(KERN_ERR "ERROR_SENSOR_ACTURATOR_INIT_FAIL!!\n");
		break;
	case ERROR_SENSOR_INVALID_AF_POS:
		printk(KERN_ERR "ERROR_SENSOR_INVALID_AF_POS!!\n");
		break;
	case ERROR_SENSOR_UNSUPPORT_FUNC:
		printk(KERN_ERR "ERROR_SENSOR_UNSUPPORT_FUNC!!\n");
		break;
	case ERROR_SENSOR_UNSUPPORT_PERI:
		printk(KERN_ERR "ERROR_SENSOR_UNSUPPORT_PERI!!\n");
		break;
	case ERROR_SENSOR_UNSUPPORT_AF:
		printk(KERN_ERR "ERROR_SENSOR_UNSUPPORT_AF!!\n");
		break;
	/* ISP Error (200~299) */
	case ERROR_ISP_AF_BUSY:
		printk(KERN_ERR "ERROR_ISP_AF_BUSY!!\n");
		break;
	case ERROR_ISP_AF_INVALID_COMMAND:
		printk(KERN_ERR "ERROR_ISP_AF_INVALID_COMMAND!!\n");
		break;
	case ERROR_ISP_AF_INVALID_MODE:
		printk(KERN_ERR "ERROR_ISP_AF_INVALID_MODE!!\n");
		break;
	/* DRC Error (300~399) */
	/* FD Error  (400~499) */
	case ERROR_FD_CONFIG_MAX_NUMBER_STATE:
		printk(KERN_ERR "ERROR_FD_CONFIG_MAX_NUMBER_STATE!!\n");
		break;
	case ERROR_FD_CONFIG_MAX_NUMBER_INVALID:
		printk(KERN_ERR "ERROR_FD_CONFIG_MAX_NUMBER_INVALID!!\n");
		break;
	case ERROR_FD_CONFIG_YAW_ANGLE_STATE:
		printk(KERN_ERR "ERROR_FD_CONFIG_YAW_ANGLE_STATE!!\n");
		break;
	case ERROR_FD_CONFIG_YAW_ANGLE_INVALID:
		printk(KERN_ERR "ERROR_FD_CONFIG_YAW_ANGLE_INVALID!!\n");
		break;
	case ERROR_FD_CONFIG_ROLL_ANGLE_STATE:
		printk(KERN_ERR "ERROR_FD_CONFIG_ROLL_ANGLE_STATE!!\n");
		break;
	case ERROR_FD_CONFIG_ROLL_ANGLE_INVALID:
		printk(KERN_ERR "ERROR_FD_CONFIG_ROLL_ANGLE_INVALID!!\n");
		break;
	case ERROR_FD_CONFIG_SMILE_MODE_INVALID:
		printk(KERN_ERR "ERROR_FD_CONFIG_SMILE_MODE_INVALID!!\n");
		break;
	case ERROR_FD_CONFIG_BLINK_MODE_INVALID:
		printk(KERN_ERR "ERROR_FD_CONFIG_BLINK_MODE_INVALID!!\n");
		break;
	case ERROR_FD_CONFIG_EYES_DETECT_INVALID:
		printk(KERN_ERR "ERROR_FD_CONFIG_EYES_DETECT_INVALID!!\n");
		break;
	case ERROR_FD_CONFIG_MOUTH_DETECT_INVALID:
		printk(KERN_ERR "ERROR_FD_CONFIG_MOUTH_DETECT_INVALID!!\n");
		break;
	case ERROR_FD_CONFIG_ORIENTATION_STATE:
		printk(KERN_ERR "ERROR_FD_CONFIG_ORIENTATION_STATE!!\n");
		break;
	case ERROR_FD_CONFIG_ORIENTATION_INVALID:
		printk(KERN_ERR "ERROR_FD_CONFIG_ORIENTATION_INVALID!!\n");
		break;
	case ERROR_FD_CONFIG_ORIENTATION_VALUE_INVALID:
		printk(KERN_ERR
			"ERROR_FD_CONFIG_ORIENTATION_VALUE_INVALID!!\n");
		break;
	case ERROR_FD_RESULT:
		printk(KERN_ERR "ERROR_FD_RESULT!!\n");
		break;
	case ERROR_FD_MODE:
		printk(KERN_ERR "ERROR_FD_MODE!!\n");
		break;
	default:
		break;
	}
}

void fimc_is_param_err_checker(struct fimc_is_dev *dev)
{
	/* Golbal */
	if (dev->is_p_region->parameter.global.shotmode.err)
		fimc_is_print_param_err_name(
			dev->is_p_region->parameter.global.shotmode.err);
	/* Sensor */
	if (dev->is_p_region->parameter.sensor.control.err)
		fimc_is_print_param_err_name(
			dev->is_p_region->parameter.sensor.control.err);
	if (dev->is_p_region->parameter.sensor.frame_rate.err)
		fimc_is_print_param_err_name(
			dev->is_p_region->parameter.sensor.frame_rate.err);
	if (dev->is_p_region->parameter.sensor.otf_output.err)
		fimc_is_print_param_err_name(
			dev->is_p_region->parameter.sensor.otf_output.err);
	/* ISP */
	if (dev->is_p_region->parameter.isp.control.err)
		fimc_is_print_param_err_name(
			dev->is_p_region->parameter.isp.control.err);
	if (dev->is_p_region->parameter.isp.otf_input.err)
		fimc_is_print_param_err_name(
			dev->is_p_region->parameter.isp.otf_input.err);
	if (dev->is_p_region->parameter.isp.dma1_input.err)
		fimc_is_print_param_err_name(
			dev->is_p_region->parameter.isp.dma1_input.err);
	if (dev->is_p_region->parameter.isp.dma2_input.err)
		fimc_is_print_param_err_name(
			dev->is_p_region->parameter.isp.dma2_input.err);
	if (dev->is_p_region->parameter.isp.aa.err)
		fimc_is_print_param_err_name(
			dev->is_p_region->parameter.isp.aa.err);
	if (dev->is_p_region->parameter.isp.flash.err)
		fimc_is_print_param_err_name(
			dev->is_p_region->parameter.isp.flash.err);
	if (dev->is_p_region->parameter.isp.awb.err)
		fimc_is_print_param_err_name(
			dev->is_p_region->parameter.isp.awb.err);
	if (dev->is_p_region->parameter.isp.effect.err)
		fimc_is_print_param_err_name(
			dev->is_p_region->parameter.isp.effect.err);
	if (dev->is_p_region->parameter.isp.iso.err)
		fimc_is_print_param_err_name(
			dev->is_p_region->parameter.isp.iso.err);
	if (dev->is_p_region->parameter.isp.adjust.err)
		fimc_is_print_param_err_name(
			dev->is_p_region->parameter.isp.adjust.err);
	if (dev->is_p_region->parameter.isp.metering.err)
		fimc_is_print_param_err_name(
			dev->is_p_region->parameter.isp.metering.err);
	if (dev->is_p_region->parameter.isp.afc.err)
		fimc_is_print_param_err_name(
			dev->is_p_region->parameter.isp.afc.err);
	/* FD */
	if (dev->is_p_region->parameter.fd.control.err)
		fimc_is_print_param_err_name(
			dev->is_p_region->parameter.fd.control.err);
	if (dev->is_p_region->parameter.fd.otf_input.err)
		fimc_is_print_param_err_name(
			dev->is_p_region->parameter.fd.otf_input.err);
	if (dev->is_p_region->parameter.fd.config.err)
		fimc_is_print_param_err_name(
			dev->is_p_region->parameter.fd.config.err);
}

void fimc_is_print_err_number(u32 num_err)
{
	if ((num_err & IS_ERROR_TIME_OUT_FLAG)) {
		printk(KERN_ERR "IS_ERROR_TIME_OUT !!\n");
		num_err -= IS_ERROR_TIME_OUT_FLAG;
	}

	switch (num_err) {
	/* General */
	case IS_ERROR_INVALID_COMMAND:
		printk(KERN_ERR "IS_ERROR_INVALID_COMMAND !!\n");
		break;
	case IS_ERROR_REQUEST_FAIL:
		printk(KERN_ERR "IS_ERROR_REQUEST_FAIL !!\n");
		break;
	case IS_ERROR_INVALID_SCENARIO:
		printk(KERN_ERR "IS_ERROR_INVALID_SCENARIO !!\n");
		break;
	case IS_ERROR_INVALID_SENSORID:
		printk(KERN_ERR "IS_ERROR_INVALID_SENSORID !!\n");
		break;
	case IS_ERROR_INVALID_MODE_CHANGE:
		printk(KERN_ERR "IS_ERROR_INVALID_MODE_CHANGE !!\n");
		break;
	case IS_ERROR_INVALID_MAGIC_NUMBER:
		printk(KERN_ERR "IS_ERROR_INVALID_MAGIC_NUMBER !!\n");
		break;
	case IS_ERROR_INVALID_SETFILE_HDR:
		printk(KERN_ERR "IS_ERROR_INVALID_SETFILE_HDR !!\n");
		break;
	case IS_ERROR_BUSY:
		printk(KERN_ERR "IS_ERROR_BUSY !!\n");
		break;
	case IS_ERROR_SET_PARAMETER:
		printk(KERN_ERR "IS_ERROR_SET_PARAMETER !!\n");
		break;
	case IS_ERROR_INVALID_PATH:
		printk(KERN_ERR "IS_ERROR_INVALID_PATH !!\n");
		break;
	case IS_ERROR_OPEN_SENSOR_FAIL:
		printk(KERN_ERR "IS_ERROR_OPEN_SENSOR_FAIL !!\n");
		break;
	case IS_ERROR_ENTRY_MSG_THREAD_DOWN:
		printk(KERN_ERR "IS_ERROR_ENTRY_MSG_THREAD_DOWN !!\n");
		break;
	case IS_ERROR_ISP_FRAME_END_NOT_DONE:
		printk(KERN_ERR "IS_ERROR_ISP_FRAME_END_NOT_DONE !!\n");
		break;
	case IS_ERROR_DRC_FRAME_END_NOT_DONE:
		printk(KERN_ERR "IS_ERROR_DRC_FRAME_END_NOT_DONE !!\n");
		break;
	case IS_ERROR_SCALERC_FRAME_END_NOT_DONE:
		printk(KERN_ERR "IS_ERROR_SCALERC_FRAME_END_NOT_DONE !!\n");
		break;
	case IS_ERROR_ODC_FRAME_END_NOT_DONE:
		printk(KERN_ERR "IS_ERROR_ODC_FRAME_END_NOT_DONE !!\n");
		break;
	case IS_ERROR_DIS_FRAME_END_NOT_DONE:
		printk(KERN_ERR "IS_ERROR_DIS_FRAME_END_NOT_DONE !!\n");
		break;
	case IS_ERROR_TDNR_FRAME_END_NOT_DONE:
		printk(KERN_ERR "IS_ERROR_TDNR_FRAME_END_NOT_DONE !!\n");
		break;
	case IS_ERROR_SCALERP_FRAME_END_NOT_DONE:
		printk(KERN_ERR "IS_ERROR_SCALERP_FRAME_END_NOT_DONE !!\n");
		break;
	case IS_ERROR_WAIT_STREAM_OFF_NOT_DONE:
		printk(KERN_ERR "IS_ERROR_WAIT_STREAM_OFF_NOT_DONE !!\n");
		break;
	case IS_ERROR_NO_MSG_IS_RECEIVED:
		printk(KERN_ERR "IS_ERROR_NO_MSG_IS_RECEIVED !!\n");
		break;
	case IS_ERROR_SENSOR_MSG_FAIL:
		printk(KERN_ERR "IS_ERROR_SENSOR_MSG_FAIL !!\n");
		break;
	case IS_ERROR_ISP_MSG_FAIL:
		printk(KERN_ERR "IS_ERROR_ISP_MSG_FAIL !!\n");
		break;
	case IS_ERROR_DRC_MSG_FAIL:
		printk(KERN_ERR "IS_ERROR_DRC_MSG_FAIL !!\n");
		break;
	case IS_ERROR_LHFD_MSG_FAIL:
		printk(KERN_ERR "IS_ERROR_LHFD_MSG_FAIL !!\n");
		break;
	case IS_ERROR_UNKNOWN:
		printk(KERN_ERR "IS_ERROR_UNKNOWN !!\n");
		break;
	/* Sensor */
	case IS_ERROR_SENSOR_PWRDN_FAIL:
		printk(KERN_ERR "IS_ERROR_SENSOR_PWRDN_FAIL !!\n");
		break;
	/* ISP */
	case IS_ERROR_ISP_PWRDN_FAIL:
		printk(KERN_ERR "IS_ERROR_ISP_PWRDN_FAIL !!\n");
		break;
	case IS_ERROR_ISP_MULTIPLE_INPUT:
		printk(KERN_ERR "IS_ERROR_ISP_MULTIPLE_INPUT !!\n");
		break;
	case IS_ERROR_ISP_ABSENT_INPUT:
		printk(KERN_ERR "IS_ERROR_ISP_ABSENT_INPUT !!\n");
		break;
	case IS_ERROR_ISP_ABSENT_OUTPUT:
		printk(KERN_ERR "IS_ERROR_ISP_ABSENT_OUTPUT !!\n");
		break;
	case IS_ERROR_ISP_NONADJACENT_OUTPUT:
		printk(KERN_ERR "IS_ERROR_ISP_NONADJACENT_OUTPUT !!\n");
		break;
	case IS_ERROR_ISP_FORMAT_MISMATCH:
		printk(KERN_ERR "IS_ERROR_ISP_FORMAT_MISMATCH !!\n");
		break;
	case IS_ERROR_ISP_WIDTH_MISMATCH:
		printk(KERN_ERR "IS_ERROR_ISP_WIDTH_MISMATCH !!\n");
		break;
	case IS_ERROR_ISP_HEIGHT_MISMATCH:
		printk(KERN_ERR "IS_ERROR_ISP_HEIGHT_MISMATCH !!\n");
		break;
	case IS_ERROR_ISP_BITWIDTH_MISMATCH:
		printk(KERN_ERR "IS_ERROR_ISP_BITWIDTH_MISMATCH !!\n");
		break;
	case IS_ERROR_ISP_FRAME_END_TIME_OUT:
		printk(KERN_ERR "IS_ERROR_ISP_FRAME_END_TIME_OUT !!\n");
		break;
	/* DRC */
	case IS_ERROR_DRC_PWRDN_FAIL:
		printk(KERN_ERR "IS_ERROR_DRC_PWRDN_FAIL !!\n");
		break;
	case IS_ERROR_DRC_MULTIPLE_INPUT:
		printk(KERN_ERR "IS_ERROR_DRC_MULTIPLE_INPUT !!\n");
		break;
	case IS_ERROR_DRC_ABSENT_INPUT:
		printk(KERN_ERR "IS_ERROR_DRC_ABSENT_INPUT !!\n");
		break;
	case IS_ERROR_DRC_NONADJACENT_INPUT:
		printk(KERN_ERR "IS_ERROR_DRC_NONADJACENT_INPUT !!\n");
		break;
	case IS_ERROR_DRC_ABSENT_OUTPUT:
		printk(KERN_ERR "IS_ERROR_DRC_ABSENT_OUTPUT !!\n");
		break;
	case IS_ERROR_DRC_NONADJACENT_OUTPUT:
		printk(KERN_ERR "IS_ERROR_DRC_NONADJACENT_OUTPUT !!\n");
		break;
	case IS_ERROR_DRC_FORMAT_MISMATCH:
		printk(KERN_ERR "IS_ERROR_DRC_FORMAT_MISMATCH !!\n");
		break;
	case IS_ERROR_DRC_WIDTH_MISMATCH:
		printk(KERN_ERR "IS_ERROR_DRC_WIDTH_MISMATCH !!\n");
		break;
	case IS_ERROR_DRC_HEIGHT_MISMATCH:
		printk(KERN_ERR "IS_ERROR_DRC_HEIGHT_MISMATCH !!\n");
		break;
	case IS_ERROR_DRC_BITWIDTH_MISMATCH:
		printk(KERN_ERR "IS_ERROR_DRC_BITWIDTH_MISMATCH !!\n");
		break;
	case IS_ERROR_DRC_FRAME_END_TIME_OUT:
		printk(KERN_ERR "IS_ERROR_DRC_FRAME_END_TIME_OUT !!\n");
		break;
	/* FD */
	case IS_ERROR_FD_PWRDN_FAIL:
		printk(KERN_ERR "IS_ERROR_FD_PWRDN_FAIL !!\n");
		break;
	case IS_ERROR_FD_MULTIPLE_INPUT:
		printk(KERN_ERR "IS_ERROR_FD_MULTIPLE_INPUT !!\n");
		break;
	case IS_ERROR_FD_ABSENT_INPUT:
		printk(KERN_ERR "IS_ERROR_FD_ABSENT_INPUT !!\n");
		break;
	case IS_ERROR_FD_NONADJACENT_INPUT:
		printk(KERN_ERR "IS_ERROR_FD_NONADJACENT_INPUT !!\n");
		break;
	case IS_ERROR_LHFD_FRAME_END_TIME_OUT:
		printk(KERN_ERR "IS_ERROR_LHFD_FRAME_END_TIME_OUT !!\n");
		break;
	}
}
