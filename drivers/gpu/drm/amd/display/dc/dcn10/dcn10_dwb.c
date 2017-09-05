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

#if defined(CONFIG_DRM_AMD_DC_DCN1_0)

#include "reg_helper.h"
#include "resource.h"
#include "dwb.h"
#include "dcn10_dwb.h"


#define REG(reg)\
	dwbc10->dwbc_regs->reg

#define CTX \
	dwbc10->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	dwbc10->dwbc_shift->field_name, dwbc10->dwbc_mask->field_name

#define TO_DCN10_DWBC(dwbc_base) \
	container_of(dwbc_base, struct dcn10_dwbc, base)

static bool get_caps(struct dwbc *dwbc, struct dwb_caps *caps)
{
	if (caps) {
		caps->adapter_id = 0;	/* we only support 1 adapter currently */
		caps->hw_version = DCN_VERSION_1_0;
		caps->num_pipes = 2;
		memset(&caps->reserved, 0, sizeof(caps->reserved));
		memset(&caps->reserved2, 0, sizeof(caps->reserved2));
		caps->sw_version = dwb_ver_1_0;
		caps->caps.support_dwb = true;
		caps->caps.support_ogam = false;
		caps->caps.support_wbscl = true;
		caps->caps.support_ocsc = false;
		return true;
	} else {
		return false;
	}
}

static bool enable(struct dwbc *dwbc)
{
	struct dcn10_dwbc *dwbc10 = TO_DCN10_DWBC(dwbc);

	/* disable first. */
	dwbc->funcs->disable(dwbc);

	/* disable power gating */
	REG_UPDATE_5(WB_EC_CONFIG, DISPCLK_R_WB_GATE_DIS, 1,
				 DISPCLK_G_WB_GATE_DIS, 1, DISPCLK_G_WBSCL_GATE_DIS, 1,
				 WB_LB_LS_DIS, 1, WB_LUT_LS_DIS, 1);

	REG_UPDATE(WB_ENABLE, WB_ENABLE, 1);

	/* lock buffer0~buffer3 */
	REG_UPDATE(MCIF_WB_BUFMGR_SW_CONTROL, MCIF_WB_BUFMGR_SW_LOCK, 0xf);

	/* buffer address for packing mode or Luma in planar mode */
	REG_UPDATE(MCIF_WB_BUF_1_ADDR_Y, MCIF_WB_BUF_1_ADDR_Y, (dwbc->config.basic_settings.luma_address[0] & 0xffffffff));
/* 	REG_UPDATE(MCIF_WB_BUF_1_ADDR_Y_HIGH, MCIF_WB_BUF_1_ADDR_Y_HIGH, (dwbc->config.basic_settings.luma_address[0] >> 32)); */
	/* right eye sub-buffer address offset for packing mode or Luma in planar mode */
	REG_UPDATE(MCIF_WB_BUF_1_ADDR_Y_OFFSET, MCIF_WB_BUF_1_ADDR_Y_OFFSET, 0);

	/* buffer address for Chroma in planar mode (unused in packing mode) */
	REG_UPDATE(MCIF_WB_BUF_1_ADDR_C, MCIF_WB_BUF_1_ADDR_C, (dwbc->config.basic_settings.chroma_address[0] & 0xffffffff));
/* 	REG_UPDATE(MCIF_WB_BUF_1_ADDR_C_HIGH, MCIF_WB_BUF_1_ADDR_C_HIGH, (dwbc->config.basic_settings.chroma_address[0] >> 32)); */
	/* right eye offset for packing mode or Luma in planar mode */
	REG_UPDATE(MCIF_WB_BUF_1_ADDR_C_OFFSET, MCIF_WB_BUF_1_ADDR_C_OFFSET, 0);

	/* buffer address for packing mode or Luma in planar mode */
	REG_UPDATE(MCIF_WB_BUF_2_ADDR_Y, MCIF_WB_BUF_2_ADDR_Y, (dwbc->config.basic_settings.luma_address[1] & 0xffffffff));
/* 	REG_UPDATE(MCIF_WB_BUF_2_ADDR_Y_HIGH, MCIF_WB_BUF_2_ADDR_Y_HIGH, (dwbc->config.basic_settings.luma_address[1] >> 32)); */
	/* right eye sub-buffer address offset for packing mode or Luma in planar mode */
	REG_UPDATE(MCIF_WB_BUF_2_ADDR_Y_OFFSET, MCIF_WB_BUF_2_ADDR_Y_OFFSET, 0);

	/* buffer address for Chroma in planar mode (unused in packing mode) */
	REG_UPDATE(MCIF_WB_BUF_2_ADDR_C, MCIF_WB_BUF_2_ADDR_C, (dwbc->config.basic_settings.chroma_address[1] & 0xffffffff));
/* 	REG_UPDATE(MCIF_WB_BUF_2_ADDR_C_HIGH, MCIF_WB_BUF_2_ADDR_C_HIGH, (dwbc->config.basic_settings.chroma_address[1] >> 32)); */
	/* right eye offset for packing mode or Luma in planar mode */
	REG_UPDATE(MCIF_WB_BUF_2_ADDR_C_OFFSET, MCIF_WB_BUF_2_ADDR_C_OFFSET, 0);

	/* buffer address for packing mode or Luma in planar mode */
	REG_UPDATE(MCIF_WB_BUF_3_ADDR_Y, MCIF_WB_BUF_3_ADDR_Y, (dwbc->config.basic_settings.luma_address[2] & 0xffffffff));
/* 	REG_UPDATE(MCIF_WB_BUF_3_ADDR_Y_HIGH, MCIF_WB_BUF_3_ADDR_Y_HIGH, (dwbc->config.basic_settings.luma_address[2] >> 32)); */
	/* right eye sub-buffer address offset for packing mode or Luma in planar mode */
	REG_UPDATE(MCIF_WB_BUF_3_ADDR_Y_OFFSET, MCIF_WB_BUF_3_ADDR_Y_OFFSET, 0);

	/* buffer address for Chroma in planar mode (unused in packing mode) */
	REG_UPDATE(MCIF_WB_BUF_3_ADDR_C, MCIF_WB_BUF_3_ADDR_C, (dwbc->config.basic_settings.chroma_address[2] & 0xffffffff));
/* 	REG_UPDATE(MCIF_WB_BUF_3_ADDR_C_HIGH, MCIF_WB_BUF_3_ADDR_C_HIGH, (dwbc->config.basic_settings.chroma_address[2] >> 32)); */
	/* right eye offset for packing mode or Luma in planar mode */
	REG_UPDATE(MCIF_WB_BUF_3_ADDR_C_OFFSET, MCIF_WB_BUF_3_ADDR_C_OFFSET, 0);

	/* buffer address for packing mode or Luma in planar mode */
	REG_UPDATE(MCIF_WB_BUF_4_ADDR_Y, MCIF_WB_BUF_4_ADDR_Y, (dwbc->config.basic_settings.luma_address[3] & 0xffffffff));
/* 	REG_UPDATE(MCIF_WB_BUF_4_ADDR_Y_HIGH, MCIF_WB_BUF_4_ADDR_Y_HIGH, (dwbc->config.basic_settings.luma_address[3] >> 32)); */
	/* right eye sub-buffer address offset for packing mode or Luma in planar mode */
	REG_UPDATE(MCIF_WB_BUF_4_ADDR_Y_OFFSET, MCIF_WB_BUF_4_ADDR_Y_OFFSET, 0);

	/* buffer address for Chroma in planar mode (unused in packing mode) */
	REG_UPDATE(MCIF_WB_BUF_4_ADDR_C, MCIF_WB_BUF_4_ADDR_C, (dwbc->config.basic_settings.chroma_address[3] & 0xffffffff));
/* 	REG_UPDATE(MCIF_WB_BUF_4_ADDR_C_HIGH, MCIF_WB_BUF_4_ADDR_C_HIGH, (dwbc->config.basic_settings.chroma_address[3] >> 32)); */
	/* right eye offset for packing mode or Luma in planar mode */
	REG_UPDATE(MCIF_WB_BUF_4_ADDR_C_OFFSET, MCIF_WB_BUF_4_ADDR_C_OFFSET, 0);

	/* setup luma & chroma size */
	REG_UPDATE(MCIF_WB_BUF_LUMA_SIZE, MCIF_WB_BUF_LUMA_SIZE, dwbc->config.basic_settings.luma_pitch * dwbc->config.basic_settings.dest_height);	/* should be enough to contain a whole frame Luma data, same for stereo mode */
	REG_UPDATE(MCIF_WB_BUF_CHROMA_SIZE, MCIF_WB_BUF_CHROMA_SIZE, dwbc->config.basic_settings.chroma_pitch * dwbc->config.basic_settings.dest_height);	/* should be enough to contain a whole frame Luma data, same for stereo mode */

	/* enable address fence */
	REG_UPDATE(MCIF_WB_BUFMGR_SW_CONTROL, MCIF_WB_BUF_ADDR_FENCE_EN, 1);

	/* setup pitch */
	REG_UPDATE_2(MCIF_WB_BUF_PITCH, MCIF_WB_BUF_LUMA_PITCH, dwbc->config.basic_settings.luma_pitch,
				 MCIF_WB_BUF_CHROMA_PITCH, dwbc->config.basic_settings.chroma_pitch);

	/* Set pitch for MC cache warm up mode */
	/* Pitch is 256 bytes aligned. The default pitch is 4K */
	REG_UPDATE(MCIF_WB_WARM_UP_CNTL, MCIF_WB_PITCH_SIZE_WARMUP, 0x10);	/* default is 0x10 */

	/* Programmed by the video driver based on the CRTC timing (for DWB) */
	REG_UPDATE(MCIF_WB_ARBITRATION_CONTROL, MCIF_WB_TIME_PER_PIXEL, 0);

	/* Programming dwb watermark */
	/* Watermark to generate urgent in MCIF_WB_CLI, value is determined by MCIF_WB_CLI_WATERMARK_MASK. */
	/* Program in ns. A formula will be provided in the pseudo code to calculate the value. */
	REG_UPDATE(MCIF_WB_SCLK_CHANGE, MCIF_WB_CLI_WATERMARK_MASK, 0x0);
	REG_UPDATE(MCIF_WB_WATERMARK, MCIF_WB_CLI_WATERMARK, 0xffff);	/* urgent_watermarkA */
	REG_UPDATE(MCIF_WB_SCLK_CHANGE, MCIF_WB_CLI_WATERMARK_MASK, 0x1);
	REG_UPDATE(MCIF_WB_WATERMARK, MCIF_WB_CLI_WATERMARK, 0xffff);	/* urgent_watermarkB */
	REG_UPDATE(MCIF_WB_SCLK_CHANGE, MCIF_WB_CLI_WATERMARK_MASK, 0x2);
	REG_UPDATE(MCIF_WB_WATERMARK, MCIF_WB_CLI_WATERMARK, 0xffff);	/* urgent_watermarkC */
	REG_UPDATE(MCIF_WB_SCLK_CHANGE, MCIF_WB_CLI_WATERMARK_MASK, 0x3);
	REG_UPDATE(MCIF_WB_WATERMARK, MCIF_WB_CLI_WATERMARK, 0xffff);	/* urgent_watermarkD */

	/* Programming nb pstate watermark */
	REG_UPDATE(MCIF_WB_NB_PSTATE_CONTROL, NB_PSTATE_CHANGE_WATERMARK_MASK, 0x0);
	REG_UPDATE(MCIF_WB_NB_PSTATE_LATENCY_WATERMARK, NB_PSTATE_CHANGE_REFRESH_WATERMARK, 0xffff);	/* nbp_state_change_watermarkA */
	REG_UPDATE(MCIF_WB_NB_PSTATE_CONTROL, NB_PSTATE_CHANGE_WATERMARK_MASK, 0x1);
	REG_UPDATE(MCIF_WB_NB_PSTATE_LATENCY_WATERMARK, NB_PSTATE_CHANGE_REFRESH_WATERMARK, 0xffff);	/* nbp_state_change_watermarkB */
	REG_UPDATE(MCIF_WB_NB_PSTATE_CONTROL, NB_PSTATE_CHANGE_WATERMARK_MASK, 0x2);
	REG_UPDATE(MCIF_WB_NB_PSTATE_LATENCY_WATERMARK, NB_PSTATE_CHANGE_REFRESH_WATERMARK, 0xffff);	/* nbp_state_change_watermarkC */
	REG_UPDATE(MCIF_WB_NB_PSTATE_CONTROL, NB_PSTATE_CHANGE_WATERMARK_MASK, 0x3);
	REG_UPDATE(MCIF_WB_NB_PSTATE_LATENCY_WATERMARK, NB_PSTATE_CHANGE_REFRESH_WATERMARK, 0xffff);	/* nbp_state_change_watermarkD */

	/* buf_lh_capability = (buffer_size / byte_per_pixel)*time_per_pixel;   //client buffer latency hiding capability */
	/* if (MCIF_WB_CLI_WATERMARK * 2 < buf_lh_capability)  //factor '2' can be adjusted if better value is identified during bringup/debug */
	/* 	MULTI_LEVEL_QOS_CTRL.MAX_SCALED_TIME_TO_URGENT = MCIF_WB_CLI_WATERMARK * 2; */
	/* else */
	/* 	MULTI_LEVEL_QOS_CTRL.MAX_SCALED_TIME_TO_URGENT = buf_lh_capability;   //ensure QoS can be fully mapped to [0:15] region in any scenario */

	REG_UPDATE(MCIF_WB_BUFMGR_VCE_CONTROL, MCIF_WB_BUFMGR_SLICE_SIZE, 31);

	/* Set arbitration unit for Luma/Chroma */
	/* arb_unit=2 should be chosen for more efficiency */
	REG_UPDATE(MCIF_WB_ARBITRATION_CONTROL, MCIF_WB_CLIENT_ARBITRATION_SLICE, 2);	/* Arbitration size, 0: 512 bytes 1: 1024 bytes 2: 2048 Bytes */

	/* Program VMID, don't support virtual mode, won't set VMID */
	/* REG_UPDATE(MCIF_WB_BUFMGR_SW_CONTROL, MCIF_WB_P_VMID, pVmid); */

	if (dwbc->config.basic_settings.input_pipe_select == dwb_pipe0) {
		REG_UPDATE(DWB_SOURCE_SELECT, OPTC_DWB0_SOURCE_SELECT, dwbc->config.basic_settings.input_src_select - dwb_src_otg0);
	} else if (dwbc->config.basic_settings.input_pipe_select == dwb_pipe1) {
		REG_UPDATE(DWB_SOURCE_SELECT, OPTC_DWB1_SOURCE_SELECT, dwbc->config.basic_settings.input_src_select - dwb_src_otg0);
	}

	/* Set interrupt mask */
	REG_UPDATE(MCIF_WB_BUFMGR_SW_CONTROL, MCIF_WB_BUFMGR_SW_INT_EN, 0);   /* Disable interrupt to SW. (the default value is 0.) */
	REG_UPDATE(MCIF_WB_BUFMGR_SW_CONTROL, MCIF_WB_BUFMGR_SW_SLICE_INT_EN, 0);   /* Disable slice complete interrupt to SW.(the default value is 0.) */
	REG_UPDATE(MCIF_WB_BUFMGR_SW_CONTROL, MCIF_WB_BUFMGR_SW_OVERRUN_INT_EN, 0);   /* Disable frame buffer overrun interrupt to SW. (the default value is 0.) */

	REG_UPDATE(MCIF_WB_BUFMGR_VCE_CONTROL, MCIF_WB_BUFMGR_VCE_INT_EN, 1);   /* Enable interrupt to VCE */
	REG_UPDATE(MCIF_WB_BUFMGR_VCE_CONTROL, MCIF_WB_BUFMGR_VCE_SLICE_INT_EN, 0);   /* Disable slice complete interrupt to VCE. */

	/* ////////////////// */
	/* Enable Mcifwb */
	REG_UPDATE(MCIF_WB_BUFMGR_SW_CONTROL, MCIF_WB_BUFMGR_ENABLE, 1);			  /* Start working */

	/* unlock sw lock. */
	REG_UPDATE(MCIF_WB_BUFMGR_SW_CONTROL, MCIF_WB_BUFMGR_SW_LOCK, 0);

	dwbc->status.enabled = true;

	return true;
}

static bool disable(struct dwbc *dwbc)
{
	struct dcn10_dwbc *dwbc10 = TO_DCN10_DWBC(dwbc);

	/* disable CNV */
	REG_UPDATE(CNV_MODE, CNV_FRAME_CAPTURE_EN, 0);

	/* disable WB */
	REG_UPDATE(WB_ENABLE, WB_ENABLE, 0);

	/* soft reset */
	REG_UPDATE(WB_SOFT_RESET, WB_SOFT_RESET, 1);
	REG_UPDATE(WB_SOFT_RESET, WB_SOFT_RESET, 0);

	/* enable power gating */
	REG_UPDATE_5(WB_EC_CONFIG, DISPCLK_R_WB_GATE_DIS, 0,
				 DISPCLK_G_WB_GATE_DIS, 0, DISPCLK_G_WBSCL_GATE_DIS, 0,
				 WB_LB_LS_DIS, 0, WB_LUT_LS_DIS, 0);

	/* disable buffer manager */
	REG_UPDATE(MCIF_WB_BUFMGR_SW_CONTROL, MCIF_WB_BUFMGR_ENABLE, 0);

	dwbc->status.enabled = false;

	return true;
}

static bool get_status(struct dwbc *dwbc, struct dwb_status *status)
{
	if (status) {
		memcpy(status, &dwbc->status, sizeof(struct dwb_status));
		return true;
	} else {
		return false;
	}
}

static bool dump_frame(struct dwbc *dwbc, struct dwb_frame_info *frame_info,
					   unsigned char *luma_buffer, unsigned char *chroma_buffer,
					   unsigned char *dest_luma_buffer, unsigned char *dest_chroma_buffer)
{
	struct dcn10_dwbc *dwbc10 = TO_DCN10_DWBC(dwbc);

	REG_UPDATE(MCIF_WB_BUFMGR_SW_CONTROL, MCIF_WB_BUFMGR_SW_LOCK, 0xf);

	memcpy(dest_luma_buffer, luma_buffer, dwbc->config.basic_settings.luma_pitch * dwbc->config.basic_settings.dest_height);
	memcpy(dest_chroma_buffer, chroma_buffer, dwbc->config.basic_settings.chroma_pitch * dwbc->config.basic_settings.dest_height / 2);

	REG_UPDATE(MCIF_WB_BUFMGR_SW_CONTROL, MCIF_WB_BUFMGR_SW_LOCK, 0x0);

	frame_info->format = dwbc->config.advanced_settings.out_format;
	frame_info->width = dwbc->config.basic_settings.dest_width;
	frame_info->height = dwbc->config.basic_settings.dest_height;
	frame_info->luma_pitch = dwbc->config.basic_settings.luma_pitch;
	frame_info->chroma_pitch = dwbc->config.basic_settings.chroma_pitch;
	frame_info->size = dwbc->config.basic_settings.dest_height * (dwbc->config.basic_settings.luma_pitch + dwbc->config.basic_settings.chroma_pitch);

	return true;
}

static bool set_basic_settings(struct dwbc *dwbc,
							   const struct dwb_basic_settings *basic_settings)
{
	if (basic_settings) {
		memcpy(&dwbc->config.basic_settings, basic_settings, sizeof(struct dwb_basic_settings));
		return true;
	} else {
		return false;
	}
}

static bool get_basic_settings(struct dwbc *dwbc,
							   struct dwb_basic_settings *basic_settings)
{
	if (basic_settings) {
		memcpy(basic_settings, &dwbc->config.basic_settings, sizeof(struct dwb_basic_settings));
		return true;
	} else {
		return false;
	}
}

static bool set_advanced_settings(struct dwbc *dwbc,
								  const struct dwb_advanced_settings *advanced_settings)
{
	if (advanced_settings) {
		if (advanced_settings->uFlag & sf_output_format) {
			dwbc->config.advanced_settings.uFlag |= sf_output_format;
			dwbc->config.advanced_settings.out_format = advanced_settings->out_format;
		}

		if (advanced_settings->uFlag & sf_capture_rate) {
			dwbc->config.advanced_settings.uFlag |= sf_capture_rate;
			dwbc->config.advanced_settings.capture_rate = advanced_settings->capture_rate;
		}

		return true;
	} else {
		return false;
	}
}

static bool get_advanced_settings(struct dwbc *dwbc,
								  struct dwb_advanced_settings *advanced_settings)
{
	if (advanced_settings) {
		memcpy(advanced_settings, &dwbc->config.advanced_settings, sizeof(struct dwb_advanced_settings));
		return true;
	} else {
		return false;
	}
}

static bool reset_advanced_settings(struct dwbc *dwbc)
{
	dwbc->config.advanced_settings.uFlag = 0;
	dwbc->config.advanced_settings.out_format = dwb_scaler_mode_bypass444;
	dwbc->config.advanced_settings.capture_rate = dwb_capture_rate_0;

	return true;
}

const struct dwbc_funcs dcn10_dwbc_funcs = {
	.get_caps = get_caps,
	.enable = enable,
	.disable = disable,
	.get_status = get_status,
	.dump_frame = dump_frame,
	.set_basic_settings = set_basic_settings,
	.get_basic_settings = get_basic_settings,
	.set_advanced_settings = set_advanced_settings,
	.get_advanced_settings = get_advanced_settings,
	.reset_advanced_settings = reset_advanced_settings,
};

void dcn10_dwbc_construct(struct dcn10_dwbc *dwbc10,
						  struct dc_context *ctx,
						  const struct dcn10_dwbc_registers *dwbc_regs,
						  const struct dcn10_dwbc_shift *dwbc_shift,
						  const struct dcn10_dwbc_mask *dwbc_mask,
						  int inst)
{
	dwbc10->base.ctx = ctx;

	dwbc10->base.inst = inst;
	dwbc10->base.funcs = &dcn10_dwbc_funcs;

	dwbc10->dwbc_regs = dwbc_regs;
	dwbc10->dwbc_shift = dwbc_shift;
	dwbc10->dwbc_mask = dwbc_mask;
}


#endif
