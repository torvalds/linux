/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */


#include "reg_helper.h"
#include "resource.h"
#include "dwb.h"
#include "dcn30_dwb.h"


#define REG(reg)\
	dwbc30->dwbc_regs->reg

#define CTX \
	dwbc30->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	dwbc30->dwbc_shift->field_name, dwbc30->dwbc_mask->field_name

#define DC_LOGGER \
	dwbc30->base.ctx->logger

static bool dwb3_get_caps(struct dwbc *dwbc, struct dwb_caps *caps)
{
	if (caps) {
		caps->adapter_id = 0;	/* we only support 1 adapter currently */
		caps->hw_version = DCN_VERSION_3_0;
		caps->num_pipes = 2;
		memset(&caps->reserved, 0, sizeof(caps->reserved));
		memset(&caps->reserved2, 0, sizeof(caps->reserved2));
		caps->sw_version = dwb_ver_2_0;
		caps->caps.support_dwb = true;
		caps->caps.support_ogam = true;
		caps->caps.support_wbscl = true;
		caps->caps.support_ocsc = false;
		caps->caps.support_stereo = true;
		return true;
	} else {
		return false;
	}
}

void dwb3_config_fc(struct dwbc *dwbc, struct dc_dwb_params *params)
{
	struct dcn30_dwbc *dwbc30 = TO_DCN30_DWBC(dwbc);

	/* Set DWB source size */
	REG_UPDATE_2(FC_SOURCE_SIZE, FC_SOURCE_WIDTH, params->cnv_params.src_width,
			FC_SOURCE_HEIGHT, params->cnv_params.src_height);

	/* source size is not equal the source size, then enable cropping. */
	if (params->cnv_params.crop_en) {
		REG_UPDATE(FC_MODE_CTRL,    FC_WINDOW_CROP_EN, 1);
		REG_UPDATE(FC_WINDOW_START, FC_WINDOW_START_X, params->cnv_params.crop_x);
		REG_UPDATE(FC_WINDOW_START, FC_WINDOW_START_Y, params->cnv_params.crop_y);
		REG_UPDATE(FC_WINDOW_SIZE,  FC_WINDOW_WIDTH,   params->cnv_params.crop_width);
		REG_UPDATE(FC_WINDOW_SIZE,  FC_WINDOW_HEIGHT,  params->cnv_params.crop_height);
	} else {
		REG_UPDATE(FC_MODE_CTRL,    FC_WINDOW_CROP_EN, 0);
	}

	/* Set CAPTURE_RATE */
	REG_UPDATE(FC_MODE_CTRL, FC_FRAME_CAPTURE_RATE, params->capture_rate);

	dwb3_set_stereo(dwbc, &params->stereo_params);
}

bool dwb3_enable(struct dwbc *dwbc, struct dc_dwb_params *params)
{
	struct dcn30_dwbc *dwbc30 = TO_DCN30_DWBC(dwbc);
	DC_LOG_DWB("%s dwb3_enabled at inst = %d", __func__, dwbc->inst);

	/* Set WB_ENABLE (not double buffered; capture not enabled) */
	REG_UPDATE(DWB_ENABLE_CLK_CTRL, DWB_ENABLE, 1);

	/* Set FC parameters */
	dwb3_config_fc(dwbc, params);

	/* Program color processing unit */
	dwb3_program_hdr_mult(dwbc, params);
	dwb3_set_gamut_remap(dwbc, params);
	dwb3_ogam_set_input_transfer_func(dwbc, params->out_transfer_func);

	/* Program output denorm */
	dwb3_set_denorm(dwbc, params);

	/* Enable DWB capture enable (double buffered) */
	REG_UPDATE(FC_MODE_CTRL, FC_FRAME_CAPTURE_EN, DWB_FRAME_CAPTURE_ENABLE);

	/* First pixel count */
	REG_UPDATE(FC_FLOW_CTRL, FC_FIRST_PIXEL_DELAY_COUNT, 96);

	return true;
}

bool dwb3_disable(struct dwbc *dwbc)
{
	struct dcn30_dwbc *dwbc30 = TO_DCN30_DWBC(dwbc);

	/* disable FC */
	REG_UPDATE(FC_MODE_CTRL, FC_FRAME_CAPTURE_EN, DWB_FRAME_CAPTURE_DISABLE);

	/* disable WB */
	REG_UPDATE(DWB_ENABLE_CLK_CTRL, DWB_ENABLE, 0);

	DC_LOG_DWB("%s dwb3_disabled at inst = %d", __func__, dwbc->inst);
	return true;
}

bool dwb3_update(struct dwbc *dwbc, struct dc_dwb_params *params)
{
	struct dcn30_dwbc *dwbc30 = TO_DCN30_DWBC(dwbc);
	unsigned int pre_locked;

	/*
	 * Check if the caller has already locked DWB registers.
	 * If so: assume the caller will unlock, so don't touch the lock.
	 * If not: lock them for this update, then unlock after the
	 * update is complete.
	 */
	REG_GET(DWB_UPDATE_CTRL, DWB_UPDATE_LOCK, &pre_locked);
	DC_LOG_DWB("%s dwb update, inst = %d", __func__, dwbc->inst);

	if (pre_locked == 0) {
		/* Lock DWB registers */
		REG_UPDATE(DWB_UPDATE_CTRL, DWB_UPDATE_LOCK, 1);
	}

	/* Set FC parameters */
	dwb3_config_fc(dwbc, params);

	/* Program color processing unit */
	dwb3_program_hdr_mult(dwbc, params);
	dwb3_set_gamut_remap(dwbc, params);
	dwb3_ogam_set_input_transfer_func(dwbc, params->out_transfer_func);

	/* Program output denorm */
	dwb3_set_denorm(dwbc, params);

	if (pre_locked == 0) {
		/* Unlock DWB registers */
		REG_UPDATE(DWB_UPDATE_CTRL, DWB_UPDATE_LOCK, 0);
	}

	return true;
}

bool dwb3_is_enabled(struct dwbc *dwbc)
{
	struct dcn30_dwbc *dwbc30 = TO_DCN30_DWBC(dwbc);
	unsigned int dwb_enabled = 0;
	unsigned int fc_frame_capture_en = 0;

	REG_GET(DWB_ENABLE_CLK_CTRL, DWB_ENABLE, &dwb_enabled);
	REG_GET(FC_MODE_CTRL, FC_FRAME_CAPTURE_EN, &fc_frame_capture_en);

	return ((dwb_enabled != 0) && (fc_frame_capture_en != 0));
}

void dwb3_set_stereo(struct dwbc *dwbc,
		struct dwb_stereo_params *stereo_params)
{
	struct dcn30_dwbc *dwbc30 = TO_DCN30_DWBC(dwbc);

	if (stereo_params->stereo_enabled) {
		REG_UPDATE(FC_MODE_CTRL, FC_EYE_SELECTION,       stereo_params->stereo_eye_select);
		REG_UPDATE(FC_MODE_CTRL, FC_STEREO_EYE_POLARITY, stereo_params->stereo_polarity);
		DC_LOG_DWB("%s dwb stereo enabled", __func__);
	} else {
		REG_UPDATE(FC_MODE_CTRL, FC_EYE_SELECTION, 0);
		DC_LOG_DWB("%s dwb stereo disabled", __func__);
	}
}

void dwb3_set_new_content(struct dwbc *dwbc,
						bool is_new_content)
{
	struct dcn30_dwbc *dwbc30 = TO_DCN30_DWBC(dwbc);

	REG_UPDATE(FC_MODE_CTRL, FC_NEW_CONTENT, is_new_content);
}

void dwb3_set_denorm(struct dwbc *dwbc, struct dc_dwb_params *params)
{
	struct dcn30_dwbc *dwbc30 = TO_DCN30_DWBC(dwbc);

	/* Set output format*/
	REG_UPDATE(DWB_OUT_CTRL, OUT_FORMAT, params->cnv_params.fc_out_format);

	/* Set output denorm */
	if (params->cnv_params.fc_out_format == DWB_OUT_FORMAT_32BPP_ARGB ||
			params->cnv_params.fc_out_format == DWB_OUT_FORMAT_32BPP_RGBA) {
		REG_UPDATE(DWB_OUT_CTRL, OUT_DENORM, params->cnv_params.out_denorm_mode);
		REG_UPDATE(DWB_OUT_CTRL, OUT_MAX,    params->cnv_params.out_max_pix_val);
		REG_UPDATE(DWB_OUT_CTRL, OUT_MIN,    params->cnv_params.out_min_pix_val);
	}
}


static const struct dwbc_funcs dcn30_dwbc_funcs = {
	.get_caps		= dwb3_get_caps,
	.enable			= dwb3_enable,
	.disable		= dwb3_disable,
	.update			= dwb3_update,
	.is_enabled		= dwb3_is_enabled,
	.set_stereo		= dwb3_set_stereo,
	.set_new_content	= dwb3_set_new_content,
	.dwb_program_output_csc	= NULL,
	.dwb_ogam_set_input_transfer_func	= dwb3_ogam_set_input_transfer_func, //TODO: rename
	.dwb_set_scaler		= NULL,
};

void dcn30_dwbc_construct(struct dcn30_dwbc *dwbc30,
		struct dc_context *ctx,
		const struct dcn30_dwbc_registers *dwbc_regs,
		const struct dcn30_dwbc_shift *dwbc_shift,
		const struct dcn30_dwbc_mask *dwbc_mask,
		int inst)
{
	dwbc30->base.ctx = ctx;

	dwbc30->base.inst = inst;
	dwbc30->base.funcs = &dcn30_dwbc_funcs;

	dwbc30->dwbc_regs = dwbc_regs;
	dwbc30->dwbc_shift = dwbc_shift;
	dwbc30->dwbc_mask = dwbc_mask;
}

void dwb3_set_host_read_rate_control(struct dwbc *dwbc, bool host_read_delay)
{
	struct dcn30_dwbc *dwbc30 = TO_DCN30_DWBC(dwbc);

	/*
	 * Set maximum delay of host read access to DWBSCL LUT or OGAM LUT if there are no
	 * idle cycles in HW pipeline (in number of clock cycles times 4)
	 */
	REG_UPDATE(DWB_HOST_READ_CONTROL, DWB_HOST_READ_RATE_CONTROL, host_read_delay);

	DC_LOG_DWB("%s dwb3_rate_control at inst = %d", __func__, dwbc->inst);
}
