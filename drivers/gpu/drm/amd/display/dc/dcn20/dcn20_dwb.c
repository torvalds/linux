/*
 * Copyright 2012-17 Advanced Micro Devices, Inc.
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
#include "dcn20_dwb.h"


#define REG(reg)\
	dwbc20->dwbc_regs->reg

#define CTX \
	dwbc20->base.ctx

#define DC_LOGGER \
	dwbc20->base.ctx->logger
#undef FN
#define FN(reg_name, field_name) \
	dwbc20->dwbc_shift->field_name, dwbc20->dwbc_mask->field_name

enum dwb_outside_pix_strategy {
	DWB_OUTSIDE_PIX_STRATEGY_BLACK = 0,
	DWB_OUTSIDE_PIX_STRATEGY_EDGE  = 1
};

static bool dwb2_get_caps(struct dwbc *dwbc, struct dwb_caps *caps)
{
	struct dcn20_dwbc *dwbc20 = TO_DCN20_DWBC(dwbc);
	if (caps) {
		caps->adapter_id = 0;	/* we only support 1 adapter currently */
		caps->hw_version = DCN_VERSION_2_0;
		caps->num_pipes = 1;
		memset(&caps->reserved, 0, sizeof(caps->reserved));
		memset(&caps->reserved2, 0, sizeof(caps->reserved2));
		caps->sw_version = dwb_ver_1_0;
		caps->caps.support_dwb = true;
		caps->caps.support_ogam = false;
		caps->caps.support_wbscl = false;
		caps->caps.support_ocsc = false;
		DC_LOG_DWB("%s SUPPORTED! inst = %d", __func__, dwbc20->base.inst);
		return true;
	} else {
		DC_LOG_DWB("%s NOT SUPPORTED! inst = %d", __func__, dwbc20->base.inst);
		return false;
	}
}

void dwb2_config_dwb_cnv(struct dwbc *dwbc, struct dc_dwb_params *params)
{
	struct dcn20_dwbc *dwbc20 = TO_DCN20_DWBC(dwbc);
	DC_LOG_DWB("%s inst = %d", __func__, dwbc20->base.inst);

	/* Set DWB source size */
	REG_UPDATE_2(CNV_SOURCE_SIZE, CNV_SOURCE_WIDTH, params->cnv_params.src_width,
			CNV_SOURCE_HEIGHT, params->cnv_params.src_height);

	/* source size is not equal the source size, then enable cropping. */
	if (params->cnv_params.crop_en) {
		REG_UPDATE(CNV_MODE, CNV_WINDOW_CROP_EN, 1);
		REG_UPDATE(CNV_WINDOW_START, CNV_WINDOW_START_X, params->cnv_params.crop_x);
		REG_UPDATE(CNV_WINDOW_START, CNV_WINDOW_START_Y, params->cnv_params.crop_y);
		REG_UPDATE(CNV_WINDOW_SIZE,  CNV_WINDOW_WIDTH,   params->cnv_params.crop_width);
		REG_UPDATE(CNV_WINDOW_SIZE,  CNV_WINDOW_HEIGHT,  params->cnv_params.crop_height);
	} else {
		REG_UPDATE(CNV_MODE, CNV_WINDOW_CROP_EN, 0);
	}

	/* Set CAPTURE_RATE */
	REG_UPDATE(CNV_MODE, CNV_FRAME_CAPTURE_RATE, params->capture_rate);

	/* Set CNV output pixel depth */
	REG_UPDATE(CNV_MODE, CNV_OUT_BPC, params->cnv_params.cnv_out_bpc);
}

static bool dwb2_enable(struct dwbc *dwbc, struct dc_dwb_params *params)
{
	struct dcn20_dwbc *dwbc20 = TO_DCN20_DWBC(dwbc);

	/* Only chroma scaling (sub-sampling) is supported in DCN2 */
	if ((params->cnv_params.src_width  != params->dest_width) ||
	    (params->cnv_params.src_height != params->dest_height)) {

		DC_LOG_DWB("%s inst = %d, FAILED!LUMA SCALING NOT SUPPORTED", __func__, dwbc20->base.inst);
		return false;
	}
	DC_LOG_DWB("%s inst = %d, ENABLED", __func__, dwbc20->base.inst);

	/* disable power gating */
	//REG_UPDATE_5(WB_EC_CONFIG, DISPCLK_R_WB_GATE_DIS, 1,
	//			 DISPCLK_G_WB_GATE_DIS, 1, DISPCLK_G_WBSCL_GATE_DIS, 1,
	//			 WB_LB_LS_DIS, 1, WB_LUT_LS_DIS, 1);

	/* Set WB_ENABLE (not double buffered; capture not enabled) */
	REG_UPDATE(WB_ENABLE, WB_ENABLE, 1);

	/* Set CNV parameters */
	dwb2_config_dwb_cnv(dwbc, params);

	/* Set scaling parameters */
	dwb2_set_scaler(dwbc, params);

	/* Enable DWB capture enable (double buffered) */
	REG_UPDATE(CNV_MODE, CNV_FRAME_CAPTURE_EN, DWB_FRAME_CAPTURE_ENABLE);

	// disable warmup
	REG_UPDATE(WB_WARM_UP_MODE_CTL1, GMC_WARM_UP_ENABLE, 0);

	return true;
}

bool dwb2_disable(struct dwbc *dwbc)
{
	struct dcn20_dwbc *dwbc20 = TO_DCN20_DWBC(dwbc);
	DC_LOG_DWB("%s inst = %d, Disabled", __func__, dwbc20->base.inst);

	/* disable CNV */
	REG_UPDATE(CNV_MODE, CNV_FRAME_CAPTURE_EN, DWB_FRAME_CAPTURE_DISABLE);

	/* disable WB */
	REG_UPDATE(WB_ENABLE, WB_ENABLE, 0);

	/* soft reset */
	REG_UPDATE(WB_SOFT_RESET, WB_SOFT_RESET, 1);
	REG_UPDATE(WB_SOFT_RESET, WB_SOFT_RESET, 0);

	/* enable power gating */
	//REG_UPDATE_5(WB_EC_CONFIG, DISPCLK_R_WB_GATE_DIS, 0,
	//			 DISPCLK_G_WB_GATE_DIS, 0, DISPCLK_G_WBSCL_GATE_DIS, 0,
	//			 WB_LB_LS_DIS, 0, WB_LUT_LS_DIS, 0);

	return true;
}

static bool dwb2_update(struct dwbc *dwbc, struct dc_dwb_params *params)
{
	struct dcn20_dwbc *dwbc20 = TO_DCN20_DWBC(dwbc);
	unsigned int pre_locked;

	/* Only chroma scaling (sub-sampling) is supported in DCN2 */
	if ((params->cnv_params.src_width != params->dest_width) ||
			(params->cnv_params.src_height != params->dest_height)) {
		DC_LOG_DWB("%s inst = %d, FAILED!LUMA SCALING NOT SUPPORTED", __func__, dwbc20->base.inst);
		return false;
	}
	DC_LOG_DWB("%s inst = %d, scaling", __func__, dwbc20->base.inst);

	/*
	 * Check if the caller has already locked CNV registers.
	 * If so: assume the caller will unlock, so don't touch the lock.
	 * If not: lock them for this update, then unlock after the
	 * update is complete.
	 */
	REG_GET(CNV_UPDATE, CNV_UPDATE_LOCK, &pre_locked);

	if (pre_locked == 0) {
		/* Lock DWB registers */
		REG_UPDATE(CNV_UPDATE, CNV_UPDATE_LOCK, 1);
	}

	/* Set CNV parameters */
	dwb2_config_dwb_cnv(dwbc, params);

	/* Set scaling parameters */
	dwb2_set_scaler(dwbc, params);

	if (pre_locked == 0) {
		/* Unlock DWB registers */
		REG_UPDATE(CNV_UPDATE, CNV_UPDATE_LOCK, 0);
	}

	return true;
}

bool dwb2_is_enabled(struct dwbc *dwbc)
{
	struct dcn20_dwbc *dwbc20 = TO_DCN20_DWBC(dwbc);
	unsigned int wb_enabled = 0;
	unsigned int cnv_frame_capture_en = 0;

	REG_GET(WB_ENABLE, WB_ENABLE, &wb_enabled);
	REG_GET(CNV_MODE, CNV_FRAME_CAPTURE_EN, &cnv_frame_capture_en);

	return ((wb_enabled != 0) && (cnv_frame_capture_en != 0));
}

void dwb2_set_stereo(struct dwbc *dwbc,
		struct dwb_stereo_params *stereo_params)
{
	struct dcn20_dwbc *dwbc20 = TO_DCN20_DWBC(dwbc);
	DC_LOG_DWB("%s inst = %d, enabled =%d", __func__,\
		dwbc20->base.inst, stereo_params->stereo_enabled);

	if (stereo_params->stereo_enabled) {
		REG_UPDATE(CNV_MODE, CNV_STEREO_TYPE,     stereo_params->stereo_type);
		REG_UPDATE(CNV_MODE, CNV_EYE_SELECTION,   stereo_params->stereo_eye_select);
		REG_UPDATE(CNV_MODE, CNV_STEREO_POLARITY, stereo_params->stereo_polarity);
	} else {
		REG_UPDATE(CNV_MODE, CNV_EYE_SELECTION, 0);
	}
}

void dwb2_set_new_content(struct dwbc *dwbc,
						bool is_new_content)
{
	struct dcn20_dwbc *dwbc20 = TO_DCN20_DWBC(dwbc);
	DC_LOG_DWB("%s inst = %d", __func__, dwbc20->base.inst);

	REG_UPDATE(CNV_MODE, CNV_NEW_CONTENT, is_new_content);
}

static void dwb2_set_warmup(struct dwbc *dwbc,
		struct dwb_warmup_params *warmup_params)
{
	struct dcn20_dwbc *dwbc20 = TO_DCN20_DWBC(dwbc);
	DC_LOG_DWB("%s inst = %d", __func__, dwbc20->base.inst);

	REG_UPDATE(WB_WARM_UP_MODE_CTL1, GMC_WARM_UP_ENABLE, warmup_params->warmup_en);
	REG_UPDATE(WB_WARM_UP_MODE_CTL1, WIDTH_WARMUP, warmup_params->warmup_width);
	REG_UPDATE(WB_WARM_UP_MODE_CTL1, HEIGHT_WARMUP, warmup_params->warmup_height);

	REG_UPDATE(WB_WARM_UP_MODE_CTL2, DATA_VALUE_WARMUP, warmup_params->warmup_data);
	REG_UPDATE(WB_WARM_UP_MODE_CTL2, MODE_WARMUP, warmup_params->warmup_mode);
	REG_UPDATE(WB_WARM_UP_MODE_CTL2, DATA_DEPTH_WARMUP, warmup_params->warmup_depth);
}

void dwb2_set_scaler(struct dwbc *dwbc, struct dc_dwb_params *params)
{
	struct dcn20_dwbc *dwbc20 = TO_DCN20_DWBC(dwbc);
	DC_LOG_DWB("%s inst = %d", __func__, dwbc20->base.inst);

	/* Program scaling mode */
	REG_UPDATE_2(WBSCL_MODE, WBSCL_MODE, params->out_format,
			WBSCL_OUT_BIT_DEPTH, params->output_depth);

	if (params->out_format != dwb_scaler_mode_bypass444) {
		/* Program output size */
		REG_UPDATE(WBSCL_DEST_SIZE, WBSCL_DEST_WIDTH,	params->dest_width);
		REG_UPDATE(WBSCL_DEST_SIZE, WBSCL_DEST_HEIGHT,	params->dest_height);

		/* Program round offsets */
		REG_UPDATE(WBSCL_ROUND_OFFSET, WBSCL_ROUND_OFFSET_Y_RGB, 0x40);
		REG_UPDATE(WBSCL_ROUND_OFFSET, WBSCL_ROUND_OFFSET_CBCR,  0x200);

		/* Program clamp values */
		REG_UPDATE(WBSCL_CLAMP_Y_RGB,	WBSCL_CLAMP_UPPER_Y_RGB,	0x3fe);
		REG_UPDATE(WBSCL_CLAMP_Y_RGB,	WBSCL_CLAMP_LOWER_Y_RGB,	0x1);
		REG_UPDATE(WBSCL_CLAMP_CBCR,	WBSCL_CLAMP_UPPER_CBCR,		0x3fe);
		REG_UPDATE(WBSCL_CLAMP_CBCR,	WBSCL_CLAMP_LOWER_CBCR,		0x1);

		/* Program outside pixel strategy to use edge pixels */
		REG_UPDATE(WBSCL_OUTSIDE_PIX_STRATEGY, WBSCL_OUTSIDE_PIX_STRATEGY, DWB_OUTSIDE_PIX_STRATEGY_EDGE);

		if (params->cnv_params.crop_en) {
			/* horizontal scale */
			dwb_program_horz_scalar(dwbc20, params->cnv_params.crop_width,
							params->dest_width,
							params->scaler_taps);

			/* vertical scale */
			dwb_program_vert_scalar(dwbc20, params->cnv_params.crop_height,
							params->dest_height,
							params->scaler_taps,
							params->subsample_position);
		} else {
			/* horizontal scale */
			dwb_program_horz_scalar(dwbc20, params->cnv_params.src_width,
							params->dest_width,
							params->scaler_taps);

			/* vertical scale */
			dwb_program_vert_scalar(dwbc20, params->cnv_params.src_height,
							params->dest_height,
							params->scaler_taps,
							params->subsample_position);
		}
	}

}

static const struct dwbc_funcs dcn20_dwbc_funcs = {
	.get_caps		= dwb2_get_caps,
	.enable			= dwb2_enable,
	.disable		= dwb2_disable,
	.update			= dwb2_update,
	.is_enabled		= dwb2_is_enabled,
	.set_stereo		= dwb2_set_stereo,
	.set_new_content	= dwb2_set_new_content,
	.set_warmup		= dwb2_set_warmup,
	.dwb_set_scaler		= dwb2_set_scaler,
};

void dcn20_dwbc_construct(struct dcn20_dwbc *dwbc20,
		struct dc_context *ctx,
		const struct dcn20_dwbc_registers *dwbc_regs,
		const struct dcn20_dwbc_shift *dwbc_shift,
		const struct dcn20_dwbc_mask *dwbc_mask,
		int inst)
{
	dwbc20->base.ctx = ctx;

	dwbc20->base.inst = inst;
	dwbc20->base.funcs = &dcn20_dwbc_funcs;

	dwbc20->dwbc_regs = dwbc_regs;
	dwbc20->dwbc_shift = dwbc_shift;
	dwbc20->dwbc_mask = dwbc_mask;
}

