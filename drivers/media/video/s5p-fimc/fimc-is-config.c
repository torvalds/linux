/*
 * Samsung EXYNOS4x12 FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2012 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 * Younghwan Joo <yhwan.joo@samsung.com>
 *
 * All rights reserved.
 */
#define DEBUG

#include <linux/bug.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/firmware.h>
#include <linux/gpio.h>
#include <plat/gpio-cfg.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "fimc-is.h"
#include "fimc-is-regs.h"
#include "fimc-is-cmd.h"
#include "fimc-is-errno.h"
#include "fimc-is-param.h"
#include "fimc-is-config.h"

#include <asm/cacheflush.h>

int fimc_is_hw_get_sensor_max_framerate(struct fimc_is *is)
{
	int max_fps = 0;

	switch (is->sensor[is->sensor_index].id) {
	case ID_S5K4E5:
		max_fps = 25;
		break;
	case ID_S5K6A3:
		max_fps = 30;
		break;
	default:
		max_fps = 15;
		break;
	}

	return max_fps;
}

int __fimc_is_hw_copy_param(void *lglobal, void *rglobal)
{
	if (!lglobal && !rglobal)
		return -EINVAL;

	memcpy(lglobal, rglobal, PARAMETER_MAX_SIZE);
	return 0;
}

int __fimc_is_hw_update_param_global_shotmode(struct fimc_is *is)
{
	struct param_global_shotmode *lshotmode, *rshotmode;

	lshotmode = &is->is_p_region->parameter.global.shotmode;
	rshotmode = &is->cfg_param[is->scenario_id].global.shotmode;
	return __fimc_is_hw_copy_param((void *)lshotmode, (void *)rshotmode);
}

int __fimc_is_hw_update_param_sensor_framerate(struct fimc_is *is)
{
	struct param_sensor_framerate *lshotmode, *rshotmode;

	lshotmode = &is->is_p_region->parameter.sensor.frame_rate;
	rshotmode = &is->cfg_param[is->scenario_id].sensor.frame_rate;
	return __fimc_is_hw_copy_param((void *)lshotmode, (void *)rshotmode);
}

int __fimc_is_hw_update_param(struct fimc_is *is, u32 offset)
{
	void *lshotmode, *rshotmode;

	switch (offset) {
	case PARAM_ISP_CONTROL:
		lshotmode = &is->is_p_region->parameter.isp.control;
		rshotmode = &is->cfg_param[is->scenario_id].isp.control;
		break;
	case PARAM_ISP_OTF_INPUT:
		lshotmode = &is->is_p_region->parameter.isp.otf_input;
		rshotmode = &is->cfg_param[is->scenario_id].isp.otf_input;
		break;
	case PARAM_ISP_DMA1_INPUT:
		lshotmode = &is->is_p_region->parameter.isp.dma1_input;
		rshotmode = &is->cfg_param[is->scenario_id].isp.dma1_input;
		break;
	case PARAM_ISP_DMA2_INPUT:
		lshotmode = &is->is_p_region->parameter.isp.dma2_input;
		rshotmode = &is->cfg_param[is->scenario_id].isp.dma2_input;
		break;
	case PARAM_ISP_AA:
		lshotmode = &is->is_p_region->parameter.isp.aa;
		rshotmode = &is->cfg_param[is->scenario_id].isp.aa;
		break;
	case PARAM_ISP_FLASH:
		lshotmode = &is->is_p_region->parameter.isp.flash;
		rshotmode = &is->cfg_param[is->scenario_id].isp.flash;
		break;
	case PARAM_ISP_AWB:
		lshotmode = &is->is_p_region->parameter.isp.awb;
		rshotmode = &is->cfg_param[is->scenario_id].isp.awb;
		break;
	case PARAM_ISP_IMAGE_EFFECT:
		lshotmode = &is->is_p_region->parameter.isp.effect;
		rshotmode = &is->cfg_param[is->scenario_id].isp.effect;
		break;
	case PARAM_ISP_ISO:
		lshotmode = &is->is_p_region->parameter.isp.iso;
		rshotmode = &is->cfg_param[is->scenario_id].isp.iso;
		break;
	case PARAM_ISP_ADJUST:
		lshotmode = &is->is_p_region->parameter.isp.adjust;
		rshotmode = &is->cfg_param[is->scenario_id].isp.adjust;
		break;
	case PARAM_ISP_METERING:
		lshotmode = &is->is_p_region->parameter.isp.metering;
		rshotmode = &is->cfg_param[is->scenario_id].isp.metering;
		break;
	case PARAM_ISP_AFC:
		lshotmode = &is->is_p_region->parameter.isp.afc;
		rshotmode = &is->cfg_param[is->scenario_id].isp.afc;
		break;
	case PARAM_ISP_OTF_OUTPUT:
		lshotmode = &is->is_p_region->parameter.isp.otf_output;
		rshotmode = &is->cfg_param[is->scenario_id].isp.otf_output;
		break;
	case PARAM_ISP_DMA1_OUTPUT:
		lshotmode = &is->is_p_region->parameter.isp.dma1_output;
		rshotmode = &is->cfg_param[is->scenario_id].isp.dma1_output;
		break;
	case PARAM_ISP_DMA2_OUTPUT:
		lshotmode = &is->is_p_region->parameter.isp.dma2_output;
		rshotmode = &is->cfg_param[is->scenario_id].isp.dma2_output;
		break;
	case PARAM_DRC_CONTROL:
		lshotmode = &is->is_p_region->parameter.drc.control;
		rshotmode = &is->cfg_param[is->scenario_id].drc.control;
		break;
	case PARAM_DRC_OTF_INPUT:
		lshotmode = &is->is_p_region->parameter.drc.otf_input;
		rshotmode = &is->cfg_param[is->scenario_id].drc.otf_input;
		break;
	case PARAM_DRC_DMA_INPUT:
		lshotmode = &is->is_p_region->parameter.drc.dma_input;
		rshotmode = &is->cfg_param[is->scenario_id].drc.dma_input;
		break;
	case PARAM_DRC_OTF_OUTPUT:
		lshotmode = &is->is_p_region->parameter.drc.otf_output;
		rshotmode = &is->cfg_param[is->scenario_id].drc.otf_output;
		break;
	case PARAM_FD_CONTROL:
		lshotmode = &is->is_p_region->parameter.fd.control;
		rshotmode = &is->cfg_param[is->scenario_id].fd.control;
		break;
	case PARAM_FD_OTF_INPUT:
		lshotmode = &is->is_p_region->parameter.fd.otf_input;
		rshotmode = &is->cfg_param[is->scenario_id].fd.otf_input;
		break;
	case PARAM_FD_DMA_INPUT:
		lshotmode = &is->is_p_region->parameter.fd.dma_input;
		rshotmode = &is->cfg_param[is->scenario_id].fd.dma_input;
		break;
	case PARAM_FD_CONFIG:
		lshotmode = &is->is_p_region->parameter.fd.config;
		rshotmode = &is->cfg_param[is->scenario_id].fd.config;
		break;
	default:
		return -EINVAL;
	}

	return __fimc_is_hw_copy_param(lshotmode, rshotmode);
}

int _is_hw_update_param(struct fimc_is *is)
{
	unsigned long *p_index1, *p_index2;
	int i, id, ret = 0;

	id = is->scenario_id;
	p_index1 = &is->cfg_param[id].p_region_index1;
	p_index2 = &is->cfg_param[id].p_region_index2;

	if (test_bit(PARAM_GLOBAL_SHOTMODE, p_index1))
		ret = __fimc_is_hw_update_param_global_shotmode(is);

	if (test_bit(PARAM_SENSOR_FRAME_RATE, p_index1))
		ret = __fimc_is_hw_update_param_sensor_framerate(is);

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

void __is_get_size(struct fimc_is *is, struct v4l2_mbus_framefmt *mf)
{
	struct isp_param *isp;
	u32 mode;

	mode = is->scenario_id;
	isp = &is->cfg_param[mode].isp;

	mf->width = isp->otf_input.width;
	mf->height = isp->otf_input.height;
}

void __is_set_size(struct fimc_is *is, struct v4l2_mbus_framefmt *mf)
{
	struct isp_param *isp;
	struct drc_param *drc;
	struct fd_param *fd;
	u32 mode;

	mode = is->scenario_id;
	isp = &is->cfg_param[mode].isp;
	drc = &is->cfg_param[mode].drc;
	fd = &is->cfg_param[mode].fd;

	/* Update isp size info (otf only) */
	isp->otf_input.width = mf->width;
	isp->otf_input.height = mf->height;
	isp->otf_output.width = mf->width;
	isp->otf_output.height = mf->height;
	/* Update drc size info (otf only) */
	drc->otf_input.width = mf->width;
	drc->otf_input.height = mf->height;
	drc->otf_output.width = mf->width;
	drc->otf_output.height = mf->height;
	/* Update fd size info (otf only) */
	fd->otf_input.width = mf->width;
	fd->otf_input.height = mf->height;

	/* Update field */
	if (!test_bit(PARAM_ISP_OTF_INPUT,
					&is->cfg_param[mode].p_region_index1)) {
		IS_UPDATE_PARAM_BIT(is, PARAM_ISP_OTF_INPUT);
		IS_UPDATE_PARAM_NUM(is);
		IS_UPDATE_PARAM_BIT(is, PARAM_ISP_OTF_OUTPUT);
		IS_UPDATE_PARAM_NUM(is);
		IS_UPDATE_PARAM_BIT(is, PARAM_DRC_OTF_INPUT);
		IS_UPDATE_PARAM_NUM(is);
		IS_UPDATE_PARAM_BIT(is, PARAM_DRC_OTF_OUTPUT);
		IS_UPDATE_PARAM_NUM(is);
		IS_UPDATE_PARAM_BIT(is, PARAM_FD_OTF_INPUT);
		IS_UPDATE_PARAM_NUM(is);
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
		IS_UPDATE_PARAM_BIT(is, PARAM_SENSOR_FRAME_RATE);
		IS_UPDATE_PARAM_NUM(is);
	}
	if (!test_bit(PARAM_ISP_OTF_INPUT, p_index)) {
		IS_UPDATE_PARAM_BIT(is, PARAM_ISP_OTF_INPUT);
		IS_UPDATE_PARAM_NUM(is);
	}
}

void __is_set_isp_aa_af_start_stop(struct fimc_is *is, int cmd)
{
	struct isp_param *isp;
	struct is_af_info *af;
	unsigned long *p_index, mode;

	mode = is->scenario_id;
	p_index = &is->cfg_param[mode].p_region_index1;
	isp = &is->cfg_param[mode].isp;
	af = &is->af;

	switch (cmd) {
	case IS_FOCUS_STOP:
		isp->aa.cmd = ISP_AA_COMMAND_STOP;
		break;
	case IS_FOCUS_START:
		isp->aa.cmd = ISP_AA_COMMAND_START;
		break;
	}

	isp->aa.target = ISP_AA_TARGET_AF;
	if (!test_bit(PARAM_ISP_AA, p_index)) {
		IS_UPDATE_PARAM_BIT(is, PARAM_ISP_AA);
		IS_UPDATE_PARAM_NUM(is);
	}
}

void __is_set_isp_aa_af_mode(struct fimc_is *is, int cmd)
{
	struct isp_param *isp;
	struct is_af_info *af;
	unsigned long *p_index, mode;

	mode = is->scenario_id;
	p_index = &is->cfg_param[mode].p_region_index1;
	isp = &is->cfg_param[mode].isp;
	af = &is->af;

	af->af_state = FIMC_IS_AF_SETCONFIG;
	switch (cmd) {
	case IS_FOCUS_MODE_AUTO:
		af->mode = cmd;
		isp->aa.cmd = ISP_AA_COMMAND_START;
		isp->aa.mode = ISP_AF_MODE_SINGLE;
		isp->aa.scene = ISP_AF_SCENE_NORMAL;
		isp->aa.sleep = ISP_AF_SLEEP_OFF;
		isp->aa.face = ISP_AF_FACE_DISABLE;
		isp->aa.touch_x = 0;
		isp->aa.touch_y = 0;
		isp->aa.manual_af_setting = 0;
		isp->aa.err = ISP_AF_ERROR_NO;
		break;
	case IS_FOCUS_MODE_MACRO:
		af->mode = cmd;
		isp->aa.cmd = ISP_AA_COMMAND_START;
		isp->aa.mode = ISP_AF_MODE_SINGLE;
		isp->aa.scene = ISP_AF_SCENE_MACRO;
		isp->aa.sleep = ISP_AF_SLEEP_OFF;
		isp->aa.face = ISP_AF_FACE_DISABLE;
		isp->aa.touch_x = 0;
		isp->aa.touch_y = 0;
		isp->aa.manual_af_setting = 0;
		isp->aa.err = ISP_AF_ERROR_NO;
		break;
	case IS_FOCUS_MODE_CONTINUOUS:
		af->mode = cmd;
		isp->aa.cmd = ISP_AA_COMMAND_START;
		isp->aa.mode = ISP_AF_MODE_CONTINUOUS;
		isp->aa.scene = ISP_AF_SCENE_NORMAL;
		isp->aa.sleep = ISP_AF_SLEEP_OFF;
		isp->aa.face = ISP_AF_FACE_DISABLE;
		isp->aa.touch_x = 0;
		isp->aa.touch_y = 0;
		isp->aa.manual_af_setting = 0;
		isp->aa.err = ISP_AF_ERROR_NO;
		break;
	case IS_FOCUS_MODE_TOUCH:
		af->mode = cmd;
		isp->aa.cmd = ISP_AA_COMMAND_START;
		isp->aa.mode = ISP_AF_MODE_TOUCH;
		isp->aa.scene = ISP_AF_SCENE_NORMAL;
		isp->aa.sleep = ISP_AF_SLEEP_OFF;
		isp->aa.face = ISP_AF_FACE_DISABLE;
		isp->aa.touch_x = af->pos_x;
		isp->aa.touch_y = af->pos_y;
		isp->aa.manual_af_setting = 0;
		isp->aa.err = ISP_AF_ERROR_NO;
		break;
	case IS_FOCUS_MODE_IDLE:
		af->af_state = FIMC_IS_AF_IDLE;
	case IS_FOCUS_MODE_INFINITY:
		af->mode = cmd;
		isp->aa.cmd = ISP_AA_COMMAND_START;
		isp->aa.mode = ISP_AF_MODE_MANUAL;
		isp->aa.scene = ISP_AF_SCENE_NORMAL;
		isp->aa.sleep = ISP_AF_SLEEP_OFF;
		isp->aa.face = ISP_AF_FACE_DISABLE;
		isp->aa.touch_x = 0;
		isp->aa.touch_y = 0;
		isp->aa.manual_af_setting = 0;
		isp->aa.err = ISP_AF_ERROR_NO;
		break;
	case IS_FOCUS_MODE_FACEDETECT:
		af->mode = cmd;
		isp->aa.cmd = ISP_AA_COMMAND_START;
		isp->aa.mode = ISP_AF_MODE_CONTINUOUS;
		isp->aa.scene = ISP_AF_SCENE_NORMAL;
		isp->aa.sleep = ISP_AF_SLEEP_OFF;
		isp->aa.face = ISP_AF_FACE_ENABLE;
		isp->aa.touch_x = 0;
		isp->aa.touch_y = 0;
		isp->aa.manual_af_setting = 0;
		isp->aa.err = ISP_AF_ERROR_NO;
		break;
	}

	is->af.af_lock_state = 0;
	is->af.ae_lock_state = 0;
	is->af.awb_lock_state = 0;

	if (!test_bit(PARAM_ISP_AA, p_index)) {
		isp->aa.target = ISP_AA_TARGET_AF;
		IS_UPDATE_PARAM_BIT(is, PARAM_ISP_AA);
		IS_UPDATE_PARAM_NUM(is);
	} else {
		isp->aa.target |= ISP_AA_TARGET_AF;
	}
}

void __is_set_isp_flash(struct fimc_is *is, u32 cmd, u32 redeye)
{
	struct isp_param *isp;
	unsigned long *p_index, mode;

	mode = is->scenario_id;
	p_index = &is->cfg_param[mode].p_region_index1;
	isp = &is->cfg_param[mode].isp;

	isp->flash.cmd = cmd;
	isp->flash.redeye = redeye;
	isp->flash.err = ISP_FLASH_ERROR_NO;

	if (!test_bit(PARAM_ISP_FLASH, p_index)) {
		IS_UPDATE_PARAM_BIT(is, PARAM_ISP_FLASH);
		IS_UPDATE_PARAM_NUM(is);
	}
}

void __is_set_isp_awb(struct fimc_is *is, u32 cmd, u32 val)
{
	struct isp_param *isp;
	unsigned long *p_index, mode;

	mode = is->scenario_id;
	p_index = &is->cfg_param[mode].p_region_index1;
	isp = &is->cfg_param[mode].isp;

	isp->awb.cmd = cmd;
	isp->awb.illumination = val;
	isp->awb.err = ISP_AWB_ERROR_NO;

	if (!test_bit(PARAM_ISP_AWB, p_index)) {
		IS_UPDATE_PARAM_BIT(is, PARAM_ISP_AWB);
		IS_UPDATE_PARAM_NUM(is);
	}
}

void __is_set_isp_effect(struct fimc_is *is, u32 cmd)
{
	struct isp_param *isp;
	unsigned long *p_index, mode;

	mode = is->scenario_id;
	p_index = &is->cfg_param[mode].p_region_index1;
	isp = &is->cfg_param[mode].isp;

	isp->effect.cmd = cmd;
	isp->effect.err = ISP_IMAGE_EFFECT_ERROR_NO;

	if (!test_bit(PARAM_ISP_IMAGE_EFFECT, p_index)) {
		IS_UPDATE_PARAM_BIT(is, PARAM_ISP_IMAGE_EFFECT);
		IS_UPDATE_PARAM_NUM(is);
	}
}

void __is_set_isp_iso(struct fimc_is *is, u32 cmd, u32 val)
{
	struct isp_param *isp;
	unsigned long *p_index, mode;

	mode = is->scenario_id;
	p_index = &is->cfg_param[mode].p_region_index1;
	isp = &is->cfg_param[mode].isp;

	isp->iso.cmd = cmd;
	isp->iso.value = val;
	isp->iso.err = ISP_ISO_ERROR_NO;

	if (!test_bit(PARAM_ISP_ISO, p_index)) {
		IS_UPDATE_PARAM_BIT(is, PARAM_ISP_ISO);
		IS_UPDATE_PARAM_NUM(is);
	}
}

void __is_set_isp_adjust(struct fimc_is *is, u32 cmd, u32 val)
{
	struct isp_param *isp;
	unsigned long *p_index, mode;

	mode = is->scenario_id;
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
		isp->adjust.err = ISP_ADJUST_ERROR_NO;
		IS_UPDATE_PARAM_BIT(is, PARAM_ISP_ADJUST);
		IS_UPDATE_PARAM_NUM(is);
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
		isp->metering.err = ISP_METERING_ERROR_NO;
		IS_UPDATE_PARAM_BIT(is, PARAM_ISP_METERING);
		IS_UPDATE_PARAM_NUM(is);
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
	isp->afc.err = ISP_AFC_ERROR_NO;

	if (!test_bit(PARAM_ISP_AFC, p_index)) {
		IS_UPDATE_PARAM_BIT(is, PARAM_ISP_AFC);
		IS_UPDATE_PARAM_NUM(is);
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
		IS_UPDATE_PARAM_BIT(is, PARAM_DRC_CONTROL);
		IS_UPDATE_PARAM_NUM(is);
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
		IS_UPDATE_PARAM_BIT(is, PARAM_FD_CONTROL);
		IS_UPDATE_PARAM_NUM(is);
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
		fd->config.err = ERROR_FD_NO;
		IS_UPDATE_PARAM_BIT(is, PARAM_FD_CONFIG);
		IS_UPDATE_PARAM_NUM(is);
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
		fd->config.err = ERROR_FD_NO;
		IS_UPDATE_PARAM_BIT(is, PARAM_FD_CONFIG);
		IS_UPDATE_PARAM_NUM(is);
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
		fd->config.err = ERROR_FD_NO;
		IS_UPDATE_PARAM_BIT(is, PARAM_FD_CONFIG);
		IS_UPDATE_PARAM_NUM(is);
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
		fd->config.err = ERROR_FD_NO;
		IS_UPDATE_PARAM_BIT(is, PARAM_FD_CONFIG);
		IS_UPDATE_PARAM_NUM(is);
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
		fd->config.err = ERROR_FD_NO;
		IS_UPDATE_PARAM_BIT(is, PARAM_FD_CONFIG);
		IS_UPDATE_PARAM_NUM(is);
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
		fd->config.err = ERROR_FD_NO;
		IS_UPDATE_PARAM_BIT(is, PARAM_FD_CONFIG);
		IS_UPDATE_PARAM_NUM(is);
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
		fd->config.err = ERROR_FD_NO;
		IS_UPDATE_PARAM_BIT(is, PARAM_FD_CONFIG);
		IS_UPDATE_PARAM_NUM(is);
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
		fd->config.err = ERROR_FD_NO;
		IS_UPDATE_PARAM_BIT(is, PARAM_FD_CONFIG);
		IS_UPDATE_PARAM_NUM(is);
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
		fd->config.err = ERROR_FD_NO;
		IS_UPDATE_PARAM_BIT(is, PARAM_FD_CONFIG);
		IS_UPDATE_PARAM_NUM(is);
	} else {
		fd->config.cmd |= FD_CONFIG_COMMAND_ORIENTATION_VALUE;
	}
}

void fimc_is_set_init_value(struct fimc_is *is)
{
	struct global_param *global;
	struct sensor_param *sensor;
	struct isp_param *isp;
	struct drc_param *drc;
	struct fd_param *fd;
	unsigned long *p_index1, *p_index2;
	u32 mode;

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
	IS_UPDATE_PARAM_BIT(is, PARAM_GLOBAL_SHOTMODE);
	IS_UPDATE_PARAM_NUM(is);

	/* ISP */
	isp->control.cmd = CONTROL_COMMAND_START;
	isp->control.bypass = CONTROL_BYPASS_DISABLE;
	isp->control.err = CONTROL_ERROR_NO;
	IS_UPDATE_PARAM_BIT(is, PARAM_ISP_CONTROL);
	IS_UPDATE_PARAM_NUM(is);

	isp->otf_input.cmd = OTF_INPUT_COMMAND_ENABLE;
	if (!test_bit(PARAM_ISP_OTF_INPUT, p_index1)) {
		isp->otf_input.width = DEFAULT_PREVIEW_STILL_WIDTH;
		isp->otf_input.height = DEFAULT_PREVIEW_STILL_HEIGHT;
		IS_UPDATE_PARAM_BIT(is, PARAM_ISP_OTF_INPUT);
		IS_UPDATE_PARAM_NUM(is);
	}
	if (is->sensor[is->sensor_index].test_pattern_flg)
		isp->otf_input.format = OTF_INPUT_FORMAT_STRGEN_COLORBAR_BAYER;
	else
		isp->otf_input.format = OTF_INPUT_FORMAT_BAYER;
	isp->otf_input.bitwidth = OTF_INPUT_BIT_WIDTH_10BIT;
	isp->otf_input.order = OTF_INPUT_ORDER_BAYER_GR_BG;
	isp->otf_input.crop_offset_x = 0;
	isp->otf_input.crop_offset_y = 0;
	isp->otf_input.err = OTF_INPUT_ERROR_NO;

	isp->dma1_input.cmd = DMA_INPUT_COMMAND_DISABLE;
	isp->dma1_input.width = 0;
	isp->dma1_input.height = 0;
	isp->dma1_input.format = 0;
	isp->dma1_input.bitwidth = 0;
	isp->dma1_input.plane = 0;
	isp->dma1_input.order = 0;
	isp->dma1_input.buffer_number = 0;
	isp->dma1_input.width = 0;
	isp->dma1_input.err = DMA_INPUT_ERROR_NO;
	IS_UPDATE_PARAM_BIT(is, PARAM_ISP_DMA1_INPUT);
	IS_UPDATE_PARAM_NUM(is);

	isp->dma2_input.cmd = DMA_INPUT_COMMAND_DISABLE;
	isp->dma2_input.width = 0;
	isp->dma2_input.height = 0;
	isp->dma2_input.format = 0;
	isp->dma2_input.bitwidth = 0;
	isp->dma2_input.plane = 0;
	isp->dma2_input.order = 0;
	isp->dma2_input.buffer_number = 0;
	isp->dma2_input.width = 0;
	isp->dma2_input.err = DMA_INPUT_ERROR_NO;
	IS_UPDATE_PARAM_BIT(is, PARAM_ISP_DMA2_INPUT);
	IS_UPDATE_PARAM_NUM(is);

	if (!test_bit(PARAM_ISP_AA, p_index1))
		__is_set_isp_aa_af_mode(is, IS_FOCUS_MODE_IDLE);
	isp->aa.target |= ISP_AA_TARGET_AE | ISP_AA_TARGET_AWB;

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
		IS_UPDATE_PARAM_BIT(is, PARAM_ISP_OTF_OUTPUT);
		IS_UPDATE_PARAM_NUM(is);
	}
	isp->otf_output.format = OTF_OUTPUT_FORMAT_YUV444;
	isp->otf_output.bitwidth = OTF_OUTPUT_BIT_WIDTH_12BIT;
	isp->otf_output.order = 0;
	isp->otf_output.err = OTF_OUTPUT_ERROR_NO;

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
		isp->dma1_output.err = DMA_OUTPUT_ERROR_NO;
		IS_UPDATE_PARAM_BIT(is, PARAM_ISP_DMA1_OUTPUT);
		IS_UPDATE_PARAM_NUM(is);
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
		isp->dma2_output.err = DMA_OUTPUT_ERROR_NO;
		IS_UPDATE_PARAM_BIT(is, PARAM_ISP_DMA2_OUTPUT);
		IS_UPDATE_PARAM_NUM(is);
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
		IS_UPDATE_PARAM_BIT(is, PARAM_DRC_OTF_INPUT);
		IS_UPDATE_PARAM_NUM(is);
	}
	drc->otf_input.format = OTF_INPUT_FORMAT_YUV444;
	drc->otf_input.bitwidth = OTF_INPUT_BIT_WIDTH_12BIT;
	drc->otf_input.order = 0;
	drc->otf_input.err = OTF_INPUT_ERROR_NO;

	drc->dma_input.cmd = DMA_INPUT_COMMAND_DISABLE;
	drc->dma_input.width = 0;
	drc->dma_input.height = 0;
	drc->dma_input.format = 0;
	drc->dma_input.bitwidth = 0;
	drc->dma_input.plane = 0;
	drc->dma_input.order = 0;
	drc->dma_input.buffer_number = 0;
	drc->dma_input.width = 0;
	drc->dma_input.err = DMA_INPUT_ERROR_NO;
	IS_UPDATE_PARAM_BIT(is, PARAM_DRC_DMA_INPUT);
	IS_UPDATE_PARAM_NUM(is);

	drc->otf_output.cmd = OTF_OUTPUT_COMMAND_ENABLE;
	if (!test_bit(PARAM_DRC_OTF_OUTPUT, p_index1)) {
		drc->otf_output.width = DEFAULT_PREVIEW_STILL_WIDTH;
		drc->otf_output.height = DEFAULT_PREVIEW_STILL_HEIGHT;
		IS_UPDATE_PARAM_BIT(is, PARAM_DRC_OTF_OUTPUT);
		IS_UPDATE_PARAM_NUM(is);
	}
	drc->otf_output.format = OTF_OUTPUT_FORMAT_YUV444;
	drc->otf_output.bitwidth = OTF_OUTPUT_BIT_WIDTH_8BIT;
	drc->otf_output.order = 0;
	drc->otf_output.err = OTF_OUTPUT_ERROR_NO;

	/* FD */
	__is_set_fd_control(is, CONTROL_COMMAND_STOP);
	fd->control.bypass = CONTROL_BYPASS_DISABLE;

	fd->otf_input.cmd = OTF_INPUT_COMMAND_ENABLE;
	if (!test_bit((PARAM_FD_OTF_INPUT - 32), p_index2)) {
		fd->otf_input.width = DEFAULT_PREVIEW_STILL_WIDTH;
		fd->otf_input.height = DEFAULT_PREVIEW_STILL_HEIGHT;
		IS_UPDATE_PARAM_BIT(is, PARAM_FD_OTF_INPUT);
		IS_UPDATE_PARAM_NUM(is);
	}
	fd->otf_input.format = OTF_INPUT_FORMAT_YUV444;
	fd->otf_input.bitwidth = OTF_INPUT_BIT_WIDTH_8BIT;
	fd->otf_input.order = 0;
	fd->otf_input.err = OTF_INPUT_ERROR_NO;

	fd->dma_input.cmd = DMA_INPUT_COMMAND_DISABLE;
	fd->dma_input.width = 0;
	fd->dma_input.height = 0;
	fd->dma_input.format = 0;
	fd->dma_input.bitwidth = 0;
	fd->dma_input.plane = 0;
	fd->dma_input.order = 0;
	fd->dma_input.buffer_number = 0;
	fd->dma_input.width = 0;
	fd->dma_input.err = DMA_INPUT_ERROR_NO;
	IS_UPDATE_PARAM_BIT(is, PARAM_FD_DMA_INPUT);
	IS_UPDATE_PARAM_NUM(is);

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

int fimc_is_hw_initialize(struct fimc_is *is)
{
	struct v4l2_subdev *sensor = &is->sensor[is->sensor_index].subdev;
	struct fimc_is_sensor *is_sensor = &is->sensor[is->sensor_index];
	u32 previous_id;
	int ret = 0;

	pr_info("Init seq 1. sensor power up\n");
	v4l2_subdev_call(sensor, core, s_power, 1);
	msleep(20);

	/* Init sequence : Open sensor */
	pr_info("Init seq 2. Open sensor\n");
	/* FIXME : sensor info must be updated by platform data */
	fimc_is_hw_open_sensor(is, 0,
				SENSOR_ID(is_sensor->id, is_sensor->i2c_ch));
	ret = wait_event_timeout(is->irq_queue,
				 test_bit(IS_ST_OPEN_SENSOR, &is->state),
				 FIMC_IS_SENSOR_OPEN_TIMEOUT);
	if (!ret) {
		pr_err("Sensor open timed out\n");
		return -EINVAL;
	}

	/* Init sequence 2: Load setfile */
	/* 2a. get setfile address */
	pr_info("Init seq 3. Get setfile addr\n");
	fimc_is_hw_get_setfile_addr(is);
	ret = wait_event_timeout(is->irq_queue,
				 test_bit(IS_ST_SETFILE_LOADED, &is->state),
				 FIMC_IS_CONFIG_TIMEOUT);
	if (!ret) {
		pr_err("Get setfile address timed out\n");
		return -EINVAL;
	}
	pr_info("setfile.base: %#x\n", is->setfile.base);
	/* 2b. load setfile */
	fimc_is_load_setfile(is);

	clear_bit(IS_ST_SETFILE_LOADED, &is->state);
	fimc_is_hw_load_setfile(is);
	ret = wait_event_timeout(is->irq_queue,
				 test_bit(IS_ST_SETFILE_LOADED, &is->state),
				 FIMC_IS_CONFIG_TIMEOUT);
	if (!ret) {
		pr_err("Load setfile command timed out\n");
		return -EINVAL;
	}
	pr_info("FIMC-IS Setfile info: %s\n", is->fw.setfile_info);
	/* Check magic number */
	if (is->is_p_region->shared[MAX_SHARED_COUNT - 1] != MAGIC_NUMBER)
		pr_err("Magic number error!\n");
	/* Display region information (DEBUG only) */
	pr_info("Parameter region addr = %#x\n", virt_to_phys(is->is_p_region));
	pr_info("Shared region addr = %#x\n",
					virt_to_phys(is->is_shared_region));
	is->sensor[is->sensor_index].frame_count = 0;
	is->setfile.sub_index = 0;
	/* Init sequence 3: Stream off */
	fimc_is_hw_set_stream(is, 0);
	ret = wait_event_timeout(is->irq_queue,
			test_bit(IS_ST_STREAM_OFF, &is->state),
			FIMC_IS_CONFIG_TIMEOUT);
	if (!ret) {
		pr_err("wait timeout - stream off\n");
		return -EINVAL;
	}

	/* preserve previous id */
	previous_id = is->scenario_id;
	/* Init sequence 4: Set init value - PREVIEW_STILL mode */
	pr_info("Default setting : preview_still\n");
	is->scenario_id = ISS_PREVIEW_STILL;
	fimc_is_set_init_value(is);
	ret = fimc_is_itf_s_param(is, true);

	/* Init sequence 5: Set init value - PREVIEW_VIDEO mode */
	pr_info("Default setting : preview_video\n");
	is->scenario_id = ISS_PREVIEW_VIDEO;
	fimc_is_set_init_value(is);
	ret = fimc_is_itf_s_param(is, true);

	/* Init sequence 6: Set init value - CAPTURE_STILL mode */
	pr_info("Default setting : capture_still\n");
	is->scenario_id = ISS_CAPTURE_STILL;
	fimc_is_set_init_value(is);
	ret = fimc_is_itf_s_param(is, true);

	/* Init sequence 7: Set init value - CAPTURE_VIDEO mode */
	pr_info("Default setting : capture_video\n");
	is->scenario_id = ISS_CAPTURE_VIDEO;
	fimc_is_set_init_value(is);
	ret = fimc_is_itf_s_param(is, true);

	/* Set default mode */
	is->scenario_id = previous_id;

	set_bit(IS_ST_INIT_DONE, &is->state);
	pr_info("Init sequence completed!! Ready to use\n");

	return 0;
}
