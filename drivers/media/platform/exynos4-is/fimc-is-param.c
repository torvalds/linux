/*
 * Samsung EXYNOS4x12 FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *
 * Authors: Younghwan Joo <yhwan.joo@samsung.com>
 *          Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define pr_fmt(fmt) "%s:%d " fmt, __func__, __LINE__

#include <linux/bug.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>

#include "fimc-is.h"
#include "fimc-is-command.h"
#include "fimc-is-errno.h"
#include "fimc-is-param.h"
#include "fimc-is-regs.h"
#include "fimc-is-sensor.h"

static void __hw_param_copy(void *dst, void *src)
{
	memcpy(dst, src, FIMC_IS_PARAM_MAX_SIZE);
}

void __fimc_is_hw_update_param_global_shotmode(struct fimc_is *is)
{
	struct param_global_shotmode *dst, *src;

	dst = &is->is_p_region->parameter.global.shotmode;
	src = &is->cfg_param[is->scenario_id].global.shotmode;
	__hw_param_copy(dst, src);
}

void __fimc_is_hw_update_param_sensor_framerate(struct fimc_is *is)
{
	struct param_sensor_framerate *dst, *src;

	dst = &is->is_p_region->parameter.sensor.frame_rate;
	src = &is->cfg_param[is->scenario_id].sensor.frame_rate;
	__hw_param_copy(dst, src);
}

int __fimc_is_hw_update_param(struct fimc_is *is, u32 offset)
{
	struct is_param_region *par = &is->is_p_region->parameter;
	struct is_config_param *cfg = &is->cfg_param[is->scenario_id];

	switch (offset) {
	case PARAM_ISP_CONTROL:
		__hw_param_copy(&par->isp.control, &cfg->isp.control);
		break;

	case PARAM_ISP_OTF_INPUT:
		__hw_param_copy(&par->isp.otf_input, &cfg->isp.otf_input);
		break;

	case PARAM_ISP_DMA1_INPUT:
		__hw_param_copy(&par->isp.dma1_input, &cfg->isp.dma1_input);
		break;

	case PARAM_ISP_DMA2_INPUT:
		__hw_param_copy(&par->isp.dma2_input, &cfg->isp.dma2_input);
		break;

	case PARAM_ISP_AA:
		__hw_param_copy(&par->isp.aa, &cfg->isp.aa);
		break;

	case PARAM_ISP_FLASH:
		__hw_param_copy(&par->isp.flash, &cfg->isp.flash);
		break;

	case PARAM_ISP_AWB:
		__hw_param_copy(&par->isp.awb, &cfg->isp.awb);
		break;

	case PARAM_ISP_IMAGE_EFFECT:
		__hw_param_copy(&par->isp.effect, &cfg->isp.effect);
		break;

	case PARAM_ISP_ISO:
		__hw_param_copy(&par->isp.iso, &cfg->isp.iso);
		break;

	case PARAM_ISP_ADJUST:
		__hw_param_copy(&par->isp.adjust, &cfg->isp.adjust);
		break;

	case PARAM_ISP_METERING:
		__hw_param_copy(&par->isp.metering, &cfg->isp.metering);
		break;

	case PARAM_ISP_AFC:
		__hw_param_copy(&par->isp.afc, &cfg->isp.afc);
		break;

	case PARAM_ISP_OTF_OUTPUT:
		__hw_param_copy(&par->isp.otf_output, &cfg->isp.otf_output);
		break;

	case PARAM_ISP_DMA1_OUTPUT:
		__hw_param_copy(&par->isp.dma1_output, &cfg->isp.dma1_output);
		break;

	case PARAM_ISP_DMA2_OUTPUT:
		__hw_param_copy(&par->isp.dma2_output, &cfg->isp.dma2_output);
		break;

	case PARAM_DRC_CONTROL:
		__hw_param_copy(&par->drc.control, &cfg->drc.control);
		break;

	case PARAM_DRC_OTF_INPUT:
		__hw_param_copy(&par->drc.otf_input, &cfg->drc.otf_input);
		break;

	case PARAM_DRC_DMA_INPUT:
		__hw_param_copy(&par->drc.dma_input, &cfg->drc.dma_input);
		break;

	case PARAM_DRC_OTF_OUTPUT:
		__hw_param_copy(&par->drc.otf_output, &cfg->drc.otf_output);
		break;

	case PARAM_FD_CONTROL:
		__hw_param_copy(&par->fd.control, &cfg->fd.control);
		break;

	case PARAM_FD_OTF_INPUT:
		__hw_param_copy(&par->fd.otf_input, &cfg->fd.otf_input);
		break;

	case PARAM_FD_DMA_INPUT:
		__hw_param_copy(&par->fd.dma_input, &cfg->fd.dma_input);
		break;

	case PARAM_FD_CONFIG:
		__hw_param_copy(&par->fd.config, &cfg->fd.config);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

int __is_hw_update_params(struct fimc_is *is)
{
	unsigned long *p_index1, *p_index2;
	int i, id, ret = 0;

	id = is->scenario_id;
	p_index1 = &is->cfg_param[id].p_region_index1;
	p_index2 = &is->cfg_param[id].p_region_index2;

	if (test_bit(PARAM_GLOBAL_SHOTMODE, p_index1))
		__fimc_is_hw_update_param_global_shotmode(is);

	if (test_bit(PARAM_SENSOR_FRAME_RATE, p_index1))
		__fimc_is_hw_update_param_sensor_framerate(is);

	for (i = PARAM_ISP_CONTROL; i < PARAM_DRC_CONTROL; i++) {
		if (test_bit(i, p_index1))
			ret = __fimc_is_hw_update_param(is, i);
	}

	for (i = PARAM_DRC_CONTROL; i < PARAM_SCALERC_CONTROL; i++) {
		if (test_bit(i, p_index1))
			ret = __fimc_is_hw_update_param(is, i);
	}

	for (i = PARAM_FD_CONTROL; i <= PARAM_FD_CONFIG; i++) {
		if (test_bit((i - 32), p_index2))
			ret = __fimc_is_hw_update_param(is, i);
	}

	return ret;
}

void __is_get_frame_size(struct fimc_is *is, struct v4l2_mbus_framefmt *mf)
{
	struct isp_param *isp;

	isp = &is->cfg_param[is->scenario_id].isp;
	mf->width = isp->otf_input.width;
	mf->height = isp->otf_input.height;
}

void __is_set_frame_size(struct fimc_is *is, struct v4l2_mbus_framefmt *mf)
{
	struct isp_param *isp;
	struct drc_param *drc;
	struct fd_param *fd;
	unsigned int mode;

	mode = is->scenario_id;
	isp = &is->cfg_param[mode].isp;
	drc = &is->cfg_param[mode].drc;
	fd = &is->cfg_param[mode].fd;

	/* Update isp size info (OTF only) */
	isp->otf_input.width = mf->width;
	isp->otf_input.height = mf->height;
	isp->otf_output.width = mf->width;
	isp->otf_output.height = mf->height;
	/* Update drc size info (OTF only) */
	drc->otf_input.width = mf->width;
	drc->otf_input.height = mf->height;
	drc->otf_output.width = mf->width;
	drc->otf_output.height = mf->height;
	/* Update fd size info (OTF only) */
	fd->otf_input.width = mf->width;
	fd->otf_input.height = mf->height;

	if (test_bit(PARAM_ISP_OTF_INPUT,
		      &is->cfg_param[mode].p_region_index1))
		return;

	/* Update field */
	fimc_is_set_param_bit(is, PARAM_ISP_OTF_INPUT);
	fimc_is_inc_param_num(is);
	fimc_is_set_param_bit(is, PARAM_ISP_OTF_OUTPUT);
	fimc_is_inc_param_num(is);
	fimc_is_set_param_bit(is, PARAM_DRC_OTF_INPUT);
	fimc_is_inc_param_num(is);
	fimc_is_set_param_bit(is, PARAM_DRC_OTF_OUTPUT);
	fimc_is_inc_param_num(is);
	fimc_is_set_param_bit(is, PARAM_FD_OTF_INPUT);
	fimc_is_inc_param_num(is);
}

int fimc_is_hw_get_sensor_max_framerate(struct fimc_is *is)
{
	switch (is->sensor->drvdata->id) {
	case FIMC_IS_SENSOR_ID_S5K6A3:
		return 30;
	default:
		return 15;
	}
}

void __is_set_sensor(struct fimc_is *is, int fps)
{
	struct sensor_param *sensor;
	struct isp_param *isp;
	unsigned long *p_index, mode;

	mode = is->scenario_id;
	p_index = &is->cfg_param[mode].p_region_index1;
	sensor = &is->cfg_param[mode].sensor;
	isp = &is->cfg_param[mode].isp;

	if (fps == 0) {
		sensor->frame_rate.frame_rate =
				fimc_is_hw_get_sensor_max_framerate(is);
		isp->otf_input.frametime_min = 0;
		isp->otf_input.frametime_max = 66666;
	} else {
		sensor->frame_rate.frame_rate = fps;
		isp->otf_input.frametime_min = 0;
		isp->otf_input.frametime_max = (u32)1000000 / fps;
	}

	if (!test_bit(PARAM_SENSOR_FRAME_RATE, p_index)) {
		fimc_is_set_param_bit(is, PARAM_SENSOR_FRAME_RATE);
		fimc_is_inc_param_num(is);
	}
	if (!test_bit(PARAM_ISP_OTF_INPUT, p_index)) {
		fimc_is_set_param_bit(is, PARAM_ISP_OTF_INPUT);
		fimc_is_inc_param_num(is);
	}
}

void __is_set_init_isp_aa(struct fimc_is *is)
{
	struct isp_param *isp;

	isp = &is->cfg_param[is->scenario_id].isp;

	isp->aa.cmd = ISP_AA_COMMAND_START;
	isp->aa.target = ISP_AA_TARGET_AF | ISP_AA_TARGET_AE |
			 ISP_AA_TARGET_AWB;
	isp->aa.mode = 0;
	isp->aa.scene = 0;
	isp->aa.sleep = 0;
	isp->aa.face = 0;
	isp->aa.touch_x = 0;
	isp->aa.touch_y = 0;
	isp->aa.manual_af_setting = 0;
	isp->aa.err = ISP_AF_ERROR_NONE;

	fimc_is_set_param_bit(is, PARAM_ISP_AA);
	fimc_is_inc_param_num(is);
}

void __is_set_isp_flash(struct fimc_is *is, u32 cmd, u32 redeye)
{
	unsigned int mode = is->scenario_id;
	struct is_config_param *cfg = &is->cfg_param[mode];
	struct isp_param *isp = &cfg->isp;

	isp->flash.cmd = cmd;
	isp->flash.redeye = redeye;
	isp->flash.err = ISP_FLASH_ERROR_NONE;

	if (!test_bit(PARAM_ISP_FLASH, &cfg->p_region_index1)) {
		fimc_is_set_param_bit(is, PARAM_ISP_FLASH);
		fimc_is_inc_param_num(is);
	}
}

void __is_set_isp_awb(struct fimc_is *is, u32 cmd, u32 val)
{
	unsigned int mode = is->scenario_id;
	struct isp_param *isp;
	unsigned long *p_index;

	p_index = &is->cfg_param[mode].p_region_index1;
	isp = &is->cfg_param[mode].isp;

	isp->awb.cmd = cmd;
	isp->awb.illumination = val;
	isp->awb.err = ISP_AWB_ERROR_NONE;

	if (!test_bit(PARAM_ISP_AWB, p_index)) {
		fimc_is_set_param_bit(is, PARAM_ISP_AWB);
		fimc_is_inc_param_num(is);
	}
}

void __is_set_isp_effect(struct fimc_is *is, u32 cmd)
{
	unsigned int mode = is->scenario_id;
	struct isp_param *isp;
	unsigned long *p_index;

	p_index = &is->cfg_param[mode].p_region_index1;
	isp = &is->cfg_param[mode].isp;

	isp->effect.cmd = cmd;
	isp->effect.err = ISP_IMAGE_EFFECT_ERROR_NONE;

	if (!test_bit(PARAM_ISP_IMAGE_EFFECT, p_index)) {
		fimc_is_set_param_bit(is, PARAM_ISP_IMAGE_EFFECT);
		fimc_is_inc_param_num(is);
	}
}

void __is_set_isp_iso(struct fimc_is *is, u32 cmd, u32 val)
{
	unsigned int mode = is->scenario_id;
	struct isp_param *isp;
	unsigned long *p_index;

	p_index = &is->cfg_param[mode].p_region_index1;
	isp = &is->cfg_param[mode].isp;

	isp->iso.cmd = cmd;
	isp->iso.value = val;
	isp->iso.err = ISP_ISO_ERROR_NONE;

	if (!test_bit(PARAM_ISP_ISO, p_index)) {
		fimc_is_set_param_bit(is, PARAM_ISP_ISO);
		fimc_is_inc_param_num(is);
	}
}

void __is_set_isp_adjust(struct fimc_is *is, u32 cmd, u32 val)
{
	unsigned int mode = is->scenario_id;
	unsigned long *p_index;
	struct isp_param *isp;

	p_index = &is->cfg_param[mode].p_region_index1;
	isp = &is->cfg_param[mode].isp;

	switch (cmd) {
	case ISP_ADJUST_COMMAND_MANUAL_CONTRAST:
		isp->adjust.contrast = val;
		break;
	case ISP_ADJUST_COMMAND_MANUAL_SATURATION:
		isp->adjust.saturation = val;
		break;
	case ISP_ADJUST_COMMAND_MANUAL_SHARPNESS:
		isp->adjust.sharpness = val;
		break;
	case ISP_ADJUST_COMMAND_MANUAL_EXPOSURE:
		isp->adjust.exposure = val;
		break;
	case ISP_ADJUST_COMMAND_MANUAL_BRIGHTNESS:
		isp->adjust.brightness = val;
		break;
	case ISP_ADJUST_COMMAND_MANUAL_HUE:
		isp->adjust.hue = val;
		break;
	case ISP_ADJUST_COMMAND_AUTO:
		isp->adjust.contrast = 0;
		isp->adjust.saturation = 0;
		isp->adjust.sharpness = 0;
		isp->adjust.exposure = 0;
		isp->adjust.brightness = 0;
		isp->adjust.hue = 0;
		break;
	}

	if (!test_bit(PARAM_ISP_ADJUST, p_index)) {
		isp->adjust.cmd = cmd;
		isp->adjust.err = ISP_ADJUST_ERROR_NONE;
		fimc_is_set_param_bit(is, PARAM_ISP_ADJUST);
		fimc_is_inc_param_num(is);
	} else {
		isp->adjust.cmd |= cmd;
	}
}

void __is_set_isp_metering(struct fimc_is *is, u32 id, u32 val)
{
	struct isp_param *isp;
	unsigned long *p_index, mode;

	mode = is->scenario_id;
	p_index = &is->cfg_param[mode].p_region_index1;
	isp = &is->cfg_param[mode].isp;

	switch (id) {
	case IS_METERING_CONFIG_CMD:
		isp->metering.cmd = val;
		break;
	case IS_METERING_CONFIG_WIN_POS_X:
		isp->metering.win_pos_x = val;
		break;
	case IS_METERING_CONFIG_WIN_POS_Y:
		isp->metering.win_pos_y = val;
		break;
	case IS_METERING_CONFIG_WIN_WIDTH:
		isp->metering.win_width = val;
		break;
	case IS_METERING_CONFIG_WIN_HEIGHT:
		isp->metering.win_height = val;
		break;
	default:
		return;
	}

	if (!test_bit(PARAM_ISP_METERING, p_index)) {
		isp->metering.err = ISP_METERING_ERROR_NONE;
		fimc_is_set_param_bit(is, PARAM_ISP_METERING);
		fimc_is_inc_param_num(is);
	}
}

void __is_set_isp_afc(struct fimc_is *is, u32 cmd, u32 val)
{
	struct isp_param *isp;
	unsigned long *p_index, mode;

	mode = is->scenario_id;
	p_index = &is->cfg_param[mode].p_region_index1;
	isp = &is->cfg_param[mode].isp;

	isp->afc.cmd = cmd;
	isp->afc.manual = val;
	isp->afc.err = ISP_AFC_ERROR_NONE;

	if (!test_bit(PARAM_ISP_AFC, p_index)) {
		fimc_is_set_param_bit(is, PARAM_ISP_AFC);
		fimc_is_inc_param_num(is);
	}
}

void __is_set_drc_control(struct fimc_is *is, u32 val)
{
	struct drc_param *drc;
	unsigned long *p_index, mode;

	mode = is->scenario_id;
	p_index = &is->cfg_param[mode].p_region_index1;
	drc = &is->cfg_param[mode].drc;

	drc->control.bypass = val;

	if (!test_bit(PARAM_DRC_CONTROL, p_index)) {
		fimc_is_set_param_bit(is, PARAM_DRC_CONTROL);
		fimc_is_inc_param_num(is);
	}
}

void __is_set_fd_control(struct fimc_is *is, u32 val)
{
	struct fd_param *fd;
	unsigned long *p_index, mode;

	mode = is->scenario_id;
	p_index = &is->cfg_param[mode].p_region_index2;
	fd = &is->cfg_param[mode].fd;

	fd->control.cmd = val;

	if (!test_bit((PARAM_FD_CONFIG - 32), p_index)) {
		fimc_is_set_param_bit(is, PARAM_FD_CONTROL);
		fimc_is_inc_param_num(is);
	}
}

void __is_set_fd_config_maxface(struct fimc_is *is, u32 val)
{
	struct fd_param *fd;
	unsigned long *p_index, mode;

	mode = is->scenario_id;
	p_index = &is->cfg_param[mode].p_region_index2;
	fd = &is->cfg_param[mode].fd;

	fd->config.max_number = val;

	if (!test_bit((PARAM_FD_CONFIG - 32), p_index)) {
		fd->config.cmd = FD_CONFIG_COMMAND_MAXIMUM_NUMBER;
		fd->config.err = ERROR_FD_NONE;
		fimc_is_set_param_bit(is, PARAM_FD_CONFIG);
		fimc_is_inc_param_num(is);
	} else {
		fd->config.cmd |= FD_CONFIG_COMMAND_MAXIMUM_NUMBER;
	}
}

void __is_set_fd_config_rollangle(struct fimc_is *is, u32 val)
{
	struct fd_param *fd;
	unsigned long *p_index, mode;

	mode = is->scenario_id;
	p_index = &is->cfg_param[mode].p_region_index2;
	fd = &is->cfg_param[mode].fd;

	fd->config.roll_angle = val;

	if (!test_bit((PARAM_FD_CONFIG - 32), p_index)) {
		fd->config.cmd = FD_CONFIG_COMMAND_ROLL_ANGLE;
		fd->config.err = ERROR_FD_NONE;
		fimc_is_set_param_bit(is, PARAM_FD_CONFIG);
		fimc_is_inc_param_num(is);
	} else {
		fd->config.cmd |= FD_CONFIG_COMMAND_ROLL_ANGLE;
	}
}

void __is_set_fd_config_yawangle(struct fimc_is *is, u32 val)
{
	struct fd_param *fd;
	unsigned long *p_index, mode;

	mode = is->scenario_id;
	p_index = &is->cfg_param[mode].p_region_index2;
	fd = &is->cfg_param[mode].fd;

	fd->config.yaw_angle = val;

	if (!test_bit((PARAM_FD_CONFIG - 32), p_index)) {
		fd->config.cmd = FD_CONFIG_COMMAND_YAW_ANGLE;
		fd->config.err = ERROR_FD_NONE;
		fimc_is_set_param_bit(is, PARAM_FD_CONFIG);
		fimc_is_inc_param_num(is);
	} else {
		fd->config.cmd |= FD_CONFIG_COMMAND_YAW_ANGLE;
	}
}

void __is_set_fd_config_smilemode(struct fimc_is *is, u32 val)
{
	struct fd_param *fd;
	unsigned long *p_index, mode;

	mode = is->scenario_id;
	p_index = &is->cfg_param[mode].p_region_index2;
	fd = &is->cfg_param[mode].fd;

	fd->config.smile_mode = val;

	if (!test_bit((PARAM_FD_CONFIG - 32), p_index)) {
		fd->config.cmd = FD_CONFIG_COMMAND_SMILE_MODE;
		fd->config.err = ERROR_FD_NONE;
		fimc_is_set_param_bit(is, PARAM_FD_CONFIG);
		fimc_is_inc_param_num(is);
	} else {
		fd->config.cmd |= FD_CONFIG_COMMAND_SMILE_MODE;
	}
}

void __is_set_fd_config_blinkmode(struct fimc_is *is, u32 val)
{
	struct fd_param *fd;
	unsigned long *p_index, mode;

	mode = is->scenario_id;
	p_index = &is->cfg_param[mode].p_region_index2;
	fd = &is->cfg_param[mode].fd;

	fd->config.blink_mode = val;

	if (!test_bit((PARAM_FD_CONFIG - 32), p_index)) {
		fd->config.cmd = FD_CONFIG_COMMAND_BLINK_MODE;
		fd->config.err = ERROR_FD_NONE;
		fimc_is_set_param_bit(is, PARAM_FD_CONFIG);
		fimc_is_inc_param_num(is);
	} else {
		fd->config.cmd |= FD_CONFIG_COMMAND_BLINK_MODE;
	}
}

void __is_set_fd_config_eyedetect(struct fimc_is *is, u32 val)
{
	struct fd_param *fd;
	unsigned long *p_index, mode;

	mode = is->scenario_id;
	p_index = &is->cfg_param[mode].p_region_index2;
	fd = &is->cfg_param[mode].fd;

	fd->config.eye_detect = val;

	if (!test_bit((PARAM_FD_CONFIG - 32), p_index)) {
		fd->config.cmd = FD_CONFIG_COMMAND_EYES_DETECT;
		fd->config.err = ERROR_FD_NONE;
		fimc_is_set_param_bit(is, PARAM_FD_CONFIG);
		fimc_is_inc_param_num(is);
	} else {
		fd->config.cmd |= FD_CONFIG_COMMAND_EYES_DETECT;
	}
}

void __is_set_fd_config_mouthdetect(struct fimc_is *is, u32 val)
{
	struct fd_param *fd;
	unsigned long *p_index, mode;

	mode = is->scenario_id;
	p_index = &is->cfg_param[mode].p_region_index2;
	fd = &is->cfg_param[mode].fd;

	fd->config.mouth_detect = val;

	if (!test_bit((PARAM_FD_CONFIG - 32), p_index)) {
		fd->config.cmd = FD_CONFIG_COMMAND_MOUTH_DETECT;
		fd->config.err = ERROR_FD_NONE;
		fimc_is_set_param_bit(is, PARAM_FD_CONFIG);
		fimc_is_inc_param_num(is);
	} else {
		fd->config.cmd |= FD_CONFIG_COMMAND_MOUTH_DETECT;
	}
}

void __is_set_fd_config_orientation(struct fimc_is *is, u32 val)
{
	struct fd_param *fd;
	unsigned long *p_index, mode;

	mode = is->scenario_id;
	p_index = &is->cfg_param[mode].p_region_index2;
	fd = &is->cfg_param[mode].fd;

	fd->config.orientation = val;

	if (!test_bit((PARAM_FD_CONFIG - 32), p_index)) {
		fd->config.cmd = FD_CONFIG_COMMAND_ORIENTATION;
		fd->config.err = ERROR_FD_NONE;
		fimc_is_set_param_bit(is, PARAM_FD_CONFIG);
		fimc_is_inc_param_num(is);
	} else {
		fd->config.cmd |= FD_CONFIG_COMMAND_ORIENTATION;
	}
}

void __is_set_fd_config_orientation_val(struct fimc_is *is, u32 val)
{
	struct fd_param *fd;
	unsigned long *p_index, mode;

	mode = is->scenario_id;
	p_index = &is->cfg_param[mode].p_region_index2;
	fd = &is->cfg_param[mode].fd;

	fd->config.orientation_value = val;

	if (!test_bit((PARAM_FD_CONFIG - 32), p_index)) {
		fd->config.cmd = FD_CONFIG_COMMAND_ORIENTATION_VALUE;
		fd->config.err = ERROR_FD_NONE;
		fimc_is_set_param_bit(is, PARAM_FD_CONFIG);
		fimc_is_inc_param_num(is);
	} else {
		fd->config.cmd |= FD_CONFIG_COMMAND_ORIENTATION_VALUE;
	}
}

void fimc_is_set_initial_params(struct fimc_is *is)
{
	struct global_param *global;
	struct sensor_param *sensor;
	struct isp_param *isp;
	struct drc_param *drc;
	struct fd_param *fd;
	unsigned long *p_index1, *p_index2;
	unsigned int mode;

	mode = is->scenario_id;
	global = &is->cfg_param[mode].global;
	sensor = &is->cfg_param[mode].sensor;
	isp = &is->cfg_param[mode].isp;
	drc = &is->cfg_param[mode].drc;
	fd = &is->cfg_param[mode].fd;
	p_index1 = &is->cfg_param[mode].p_region_index1;
	p_index2 = &is->cfg_param[mode].p_region_index2;

	/* Global */
	global->shotmode.cmd = 1;
	fimc_is_set_param_bit(is, PARAM_GLOBAL_SHOTMODE);
	fimc_is_inc_param_num(is);

	/* ISP */
	isp->control.cmd = CONTROL_COMMAND_START;
	isp->control.bypass = CONTROL_BYPASS_DISABLE;
	isp->control.err = CONTROL_ERROR_NONE;
	fimc_is_set_param_bit(is, PARAM_ISP_CONTROL);
	fimc_is_inc_param_num(is);

	isp->otf_input.cmd = OTF_INPUT_COMMAND_ENABLE;
	if (!test_bit(PARAM_ISP_OTF_INPUT, p_index1)) {
		isp->otf_input.width = DEFAULT_PREVIEW_STILL_WIDTH;
		isp->otf_input.height = DEFAULT_PREVIEW_STILL_HEIGHT;
		fimc_is_set_param_bit(is, PARAM_ISP_OTF_INPUT);
		fimc_is_inc_param_num(is);
	}
	if (is->sensor->test_pattern)
		isp->otf_input.format = OTF_INPUT_FORMAT_STRGEN_COLORBAR_BAYER;
	else
		isp->otf_input.format = OTF_INPUT_FORMAT_BAYER;
	isp->otf_input.bitwidth = 10;
	isp->otf_input.order = OTF_INPUT_ORDER_BAYER_GR_BG;
	isp->otf_input.crop_offset_x = 0;
	isp->otf_input.crop_offset_y = 0;
	isp->otf_input.err = OTF_INPUT_ERROR_NONE;

	isp->dma1_input.cmd = DMA_INPUT_COMMAND_DISABLE;
	isp->dma1_input.width = 0;
	isp->dma1_input.height = 0;
	isp->dma1_input.format = 0;
	isp->dma1_input.bitwidth = 0;
	isp->dma1_input.plane = 0;
	isp->dma1_input.order = 0;
	isp->dma1_input.buffer_number = 0;
	isp->dma1_input.width = 0;
	isp->dma1_input.err = DMA_INPUT_ERROR_NONE;
	fimc_is_set_param_bit(is, PARAM_ISP_DMA1_INPUT);
	fimc_is_inc_param_num(is);

	isp->dma2_input.cmd = DMA_INPUT_COMMAND_DISABLE;
	isp->dma2_input.width = 0;
	isp->dma2_input.height = 0;
	isp->dma2_input.format = 0;
	isp->dma2_input.bitwidth = 0;
	isp->dma2_input.plane = 0;
	isp->dma2_input.order = 0;
	isp->dma2_input.buffer_number = 0;
	isp->dma2_input.width = 0;
	isp->dma2_input.err = DMA_INPUT_ERROR_NONE;
	fimc_is_set_param_bit(is, PARAM_ISP_DMA2_INPUT);
	fimc_is_inc_param_num(is);

	isp->aa.cmd = ISP_AA_COMMAND_START;
	isp->aa.target = ISP_AA_TARGET_AE | ISP_AA_TARGET_AWB;
	fimc_is_set_param_bit(is, PARAM_ISP_AA);
	fimc_is_inc_param_num(is);

	if (!test_bit(PARAM_ISP_FLASH, p_index1))
		__is_set_isp_flash(is, ISP_FLASH_COMMAND_DISABLE,
						ISP_FLASH_REDEYE_DISABLE);

	if (!test_bit(PARAM_ISP_AWB, p_index1))
		__is_set_isp_awb(is, ISP_AWB_COMMAND_AUTO, 0);

	if (!test_bit(PARAM_ISP_IMAGE_EFFECT, p_index1))
		__is_set_isp_effect(is, ISP_IMAGE_EFFECT_DISABLE);

	if (!test_bit(PARAM_ISP_ISO, p_index1))
		__is_set_isp_iso(is, ISP_ISO_COMMAND_AUTO, 0);

	if (!test_bit(PARAM_ISP_ADJUST, p_index1)) {
		__is_set_isp_adjust(is, ISP_ADJUST_COMMAND_MANUAL_CONTRAST, 0);
		__is_set_isp_adjust(is,
				ISP_ADJUST_COMMAND_MANUAL_SATURATION, 0);
		__is_set_isp_adjust(is, ISP_ADJUST_COMMAND_MANUAL_SHARPNESS, 0);
		__is_set_isp_adjust(is, ISP_ADJUST_COMMAND_MANUAL_EXPOSURE, 0);
		__is_set_isp_adjust(is,
				ISP_ADJUST_COMMAND_MANUAL_BRIGHTNESS, 0);
		__is_set_isp_adjust(is, ISP_ADJUST_COMMAND_MANUAL_HUE, 0);
	}

	if (!test_bit(PARAM_ISP_METERING, p_index1)) {
		__is_set_isp_metering(is, 0, ISP_METERING_COMMAND_CENTER);
		__is_set_isp_metering(is, 1, 0);
		__is_set_isp_metering(is, 2, 0);
		__is_set_isp_metering(is, 3, 0);
		__is_set_isp_metering(is, 4, 0);
	}

	if (!test_bit(PARAM_ISP_AFC, p_index1))
		__is_set_isp_afc(is, ISP_AFC_COMMAND_AUTO, 0);

	isp->otf_output.cmd = OTF_OUTPUT_COMMAND_ENABLE;
	if (!test_bit(PARAM_ISP_OTF_OUTPUT, p_index1)) {
		isp->otf_output.width = DEFAULT_PREVIEW_STILL_WIDTH;
		isp->otf_output.height = DEFAULT_PREVIEW_STILL_HEIGHT;
		fimc_is_set_param_bit(is, PARAM_ISP_OTF_OUTPUT);
		fimc_is_inc_param_num(is);
	}
	isp->otf_output.format = OTF_OUTPUT_FORMAT_YUV444;
	isp->otf_output.bitwidth = 12;
	isp->otf_output.order = 0;
	isp->otf_output.err = OTF_OUTPUT_ERROR_NONE;

	if (!test_bit(PARAM_ISP_DMA1_OUTPUT, p_index1)) {
		isp->dma1_output.cmd = DMA_OUTPUT_COMMAND_DISABLE;
		isp->dma1_output.width = 0;
		isp->dma1_output.height = 0;
		isp->dma1_output.format = 0;
		isp->dma1_output.bitwidth = 0;
		isp->dma1_output.plane = 0;
		isp->dma1_output.order = 0;
		isp->dma1_output.buffer_number = 0;
		isp->dma1_output.buffer_address = 0;
		isp->dma1_output.notify_dma_done = 0;
		isp->dma1_output.dma_out_mask = 0;
		isp->dma1_output.err = DMA_OUTPUT_ERROR_NONE;
		fimc_is_set_param_bit(is, PARAM_ISP_DMA1_OUTPUT);
		fimc_is_inc_param_num(is);
	}

	if (!test_bit(PARAM_ISP_DMA2_OUTPUT, p_index1)) {
		isp->dma2_output.cmd = DMA_OUTPUT_COMMAND_DISABLE;
		isp->dma2_output.width = 0;
		isp->dma2_output.height = 0;
		isp->dma2_output.format = 0;
		isp->dma2_output.bitwidth = 0;
		isp->dma2_output.plane = 0;
		isp->dma2_output.order = 0;
		isp->dma2_output.buffer_number = 0;
		isp->dma2_output.buffer_address = 0;
		isp->dma2_output.notify_dma_done = 0;
		isp->dma2_output.dma_out_mask = 0;
		isp->dma2_output.err = DMA_OUTPUT_ERROR_NONE;
		fimc_is_set_param_bit(is, PARAM_ISP_DMA2_OUTPUT);
		fimc_is_inc_param_num(is);
	}

	/* Sensor */
	if (!test_bit(PARAM_SENSOR_FRAME_RATE, p_index1)) {
		if (!mode)
			__is_set_sensor(is, 0);
	}

	/* DRC */
	drc->control.cmd = CONTROL_COMMAND_START;
	__is_set_drc_control(is, CONTROL_BYPASS_ENABLE);

	drc->otf_input.cmd = OTF_INPUT_COMMAND_ENABLE;
	if (!test_bit(PARAM_DRC_OTF_INPUT, p_index1)) {
		drc->otf_input.width = DEFAULT_PREVIEW_STILL_WIDTH;
		drc->otf_input.height = DEFAULT_PREVIEW_STILL_HEIGHT;
		fimc_is_set_param_bit(is, PARAM_DRC_OTF_INPUT);
		fimc_is_inc_param_num(is);
	}
	drc->otf_input.format = OTF_INPUT_FORMAT_YUV444;
	drc->otf_input.bitwidth = 12;
	drc->otf_input.order = 0;
	drc->otf_input.err = OTF_INPUT_ERROR_NONE;

	drc->dma_input.cmd = DMA_INPUT_COMMAND_DISABLE;
	drc->dma_input.width = 0;
	drc->dma_input.height = 0;
	drc->dma_input.format = 0;
	drc->dma_input.bitwidth = 0;
	drc->dma_input.plane = 0;
	drc->dma_input.order = 0;
	drc->dma_input.buffer_number = 0;
	drc->dma_input.width = 0;
	drc->dma_input.err = DMA_INPUT_ERROR_NONE;
	fimc_is_set_param_bit(is, PARAM_DRC_DMA_INPUT);
	fimc_is_inc_param_num(is);

	drc->otf_output.cmd = OTF_OUTPUT_COMMAND_ENABLE;
	if (!test_bit(PARAM_DRC_OTF_OUTPUT, p_index1)) {
		drc->otf_output.width = DEFAULT_PREVIEW_STILL_WIDTH;
		drc->otf_output.height = DEFAULT_PREVIEW_STILL_HEIGHT;
		fimc_is_set_param_bit(is, PARAM_DRC_OTF_OUTPUT);
		fimc_is_inc_param_num(is);
	}
	drc->otf_output.format = OTF_OUTPUT_FORMAT_YUV444;
	drc->otf_output.bitwidth = 8;
	drc->otf_output.order = 0;
	drc->otf_output.err = OTF_OUTPUT_ERROR_NONE;

	/* FD */
	__is_set_fd_control(is, CONTROL_COMMAND_STOP);
	fd->control.bypass = CONTROL_BYPASS_DISABLE;

	fd->otf_input.cmd = OTF_INPUT_COMMAND_ENABLE;
	if (!test_bit((PARAM_FD_OTF_INPUT - 32), p_index2)) {
		fd->otf_input.width = DEFAULT_PREVIEW_STILL_WIDTH;
		fd->otf_input.height = DEFAULT_PREVIEW_STILL_HEIGHT;
		fimc_is_set_param_bit(is, PARAM_FD_OTF_INPUT);
		fimc_is_inc_param_num(is);
	}
	fd->otf_input.format = OTF_INPUT_FORMAT_YUV444;
	fd->otf_input.bitwidth = 8;
	fd->otf_input.order = 0;
	fd->otf_input.err = OTF_INPUT_ERROR_NONE;

	fd->dma_input.cmd = DMA_INPUT_COMMAND_DISABLE;
	fd->dma_input.width = 0;
	fd->dma_input.height = 0;
	fd->dma_input.format = 0;
	fd->dma_input.bitwidth = 0;
	fd->dma_input.plane = 0;
	fd->dma_input.order = 0;
	fd->dma_input.buffer_number = 0;
	fd->dma_input.width = 0;
	fd->dma_input.err = DMA_INPUT_ERROR_NONE;
	fimc_is_set_param_bit(is, PARAM_FD_DMA_INPUT);
	fimc_is_inc_param_num(is);

	__is_set_fd_config_maxface(is, 5);
	__is_set_fd_config_rollangle(is, FD_CONFIG_ROLL_ANGLE_FULL);
	__is_set_fd_config_yawangle(is, FD_CONFIG_YAW_ANGLE_45_90);
	__is_set_fd_config_smilemode(is, FD_CONFIG_SMILE_MODE_DISABLE);
	__is_set_fd_config_blinkmode(is, FD_CONFIG_BLINK_MODE_DISABLE);
	__is_set_fd_config_eyedetect(is, FD_CONFIG_EYES_DETECT_ENABLE);
	__is_set_fd_config_mouthdetect(is, FD_CONFIG_MOUTH_DETECT_DISABLE);
	__is_set_fd_config_orientation(is, FD_CONFIG_ORIENTATION_DISABLE);
	__is_set_fd_config_orientation_val(is, 0);
}
