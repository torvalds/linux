/*
 * Copyright 2007-8 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
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
 * Authors: Dave Airlie
 *          Alex Deucher
 */
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/amdgpu_drm.h>
#include <drm/drm_fixed.h>
#include "amdgpu.h"
#include "atom.h"
#include "atom-bits.h"
#include "atombios_encoders.h"
#include "amdgpu_atombios.h"
#include "amdgpu_pll.h"
#include "amdgpu_connectors.h"

void amdgpu_atombios_crtc_overscan_setup(struct drm_crtc *crtc,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	SET_CRTC_OVERSCAN_PS_ALLOCATION args;
	int index = GetIndexIntoMasterTable(COMMAND, SetCRTC_OverScan);
	int a1, a2;

	memset(&args, 0, sizeof(args));

	args.ucCRTC = amdgpu_crtc->crtc_id;

	switch (amdgpu_crtc->rmx_type) {
	case RMX_CENTER:
		args.usOverscanTop = cpu_to_le16((adjusted_mode->crtc_vdisplay - mode->crtc_vdisplay) / 2);
		args.usOverscanBottom = cpu_to_le16((adjusted_mode->crtc_vdisplay - mode->crtc_vdisplay) / 2);
		args.usOverscanLeft = cpu_to_le16((adjusted_mode->crtc_hdisplay - mode->crtc_hdisplay) / 2);
		args.usOverscanRight = cpu_to_le16((adjusted_mode->crtc_hdisplay - mode->crtc_hdisplay) / 2);
		break;
	case RMX_ASPECT:
		a1 = mode->crtc_vdisplay * adjusted_mode->crtc_hdisplay;
		a2 = adjusted_mode->crtc_vdisplay * mode->crtc_hdisplay;

		if (a1 > a2) {
			args.usOverscanLeft = cpu_to_le16((adjusted_mode->crtc_hdisplay - (a2 / mode->crtc_vdisplay)) / 2);
			args.usOverscanRight = cpu_to_le16((adjusted_mode->crtc_hdisplay - (a2 / mode->crtc_vdisplay)) / 2);
		} else if (a2 > a1) {
			args.usOverscanTop = cpu_to_le16((adjusted_mode->crtc_vdisplay - (a1 / mode->crtc_hdisplay)) / 2);
			args.usOverscanBottom = cpu_to_le16((adjusted_mode->crtc_vdisplay - (a1 / mode->crtc_hdisplay)) / 2);
		}
		break;
	case RMX_FULL:
	default:
		args.usOverscanRight = cpu_to_le16(amdgpu_crtc->h_border);
		args.usOverscanLeft = cpu_to_le16(amdgpu_crtc->h_border);
		args.usOverscanBottom = cpu_to_le16(amdgpu_crtc->v_border);
		args.usOverscanTop = cpu_to_le16(amdgpu_crtc->v_border);
		break;
	}
	amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);
}

void amdgpu_atombios_crtc_scaler_setup(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	ENABLE_SCALER_PS_ALLOCATION args;
	int index = GetIndexIntoMasterTable(COMMAND, EnableScaler);

	memset(&args, 0, sizeof(args));

	args.ucScaler = amdgpu_crtc->crtc_id;

	switch (amdgpu_crtc->rmx_type) {
	case RMX_FULL:
		args.ucEnable = ATOM_SCALER_EXPANSION;
		break;
	case RMX_CENTER:
		args.ucEnable = ATOM_SCALER_CENTER;
		break;
	case RMX_ASPECT:
		args.ucEnable = ATOM_SCALER_EXPANSION;
		break;
	default:
		args.ucEnable = ATOM_SCALER_DISABLE;
		break;
	}
	amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);
}

void amdgpu_atombios_crtc_lock(struct drm_crtc *crtc, int lock)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = dev->dev_private;
	int index =
	    GetIndexIntoMasterTable(COMMAND, UpdateCRTC_DoubleBufferRegisters);
	ENABLE_CRTC_PS_ALLOCATION args;

	memset(&args, 0, sizeof(args));

	args.ucCRTC = amdgpu_crtc->crtc_id;
	args.ucEnable = lock;

	amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);
}

void amdgpu_atombios_crtc_enable(struct drm_crtc *crtc, int state)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = dev->dev_private;
	int index = GetIndexIntoMasterTable(COMMAND, EnableCRTC);
	ENABLE_CRTC_PS_ALLOCATION args;

	memset(&args, 0, sizeof(args));

	args.ucCRTC = amdgpu_crtc->crtc_id;
	args.ucEnable = state;

	amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);
}

void amdgpu_atombios_crtc_blank(struct drm_crtc *crtc, int state)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = dev->dev_private;
	int index = GetIndexIntoMasterTable(COMMAND, BlankCRTC);
	BLANK_CRTC_PS_ALLOCATION args;

	memset(&args, 0, sizeof(args));

	args.ucCRTC = amdgpu_crtc->crtc_id;
	args.ucBlanking = state;

	amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);
}

void amdgpu_atombios_crtc_powergate(struct drm_crtc *crtc, int state)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = dev->dev_private;
	int index = GetIndexIntoMasterTable(COMMAND, EnableDispPowerGating);
	ENABLE_DISP_POWER_GATING_PS_ALLOCATION args;

	memset(&args, 0, sizeof(args));

	args.ucDispPipeId = amdgpu_crtc->crtc_id;
	args.ucEnable = state;

	amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);
}

void amdgpu_atombios_crtc_powergate_init(struct amdgpu_device *adev)
{
	int index = GetIndexIntoMasterTable(COMMAND, EnableDispPowerGating);
	ENABLE_DISP_POWER_GATING_PS_ALLOCATION args;

	memset(&args, 0, sizeof(args));

	args.ucEnable = ATOM_INIT;

	amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);
}

void amdgpu_atombios_crtc_set_dtd_timing(struct drm_crtc *crtc,
				  struct drm_display_mode *mode)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = dev->dev_private;
	SET_CRTC_USING_DTD_TIMING_PARAMETERS args;
	int index = GetIndexIntoMasterTable(COMMAND, SetCRTC_UsingDTDTiming);
	u16 misc = 0;

	memset(&args, 0, sizeof(args));
	args.usH_Size = cpu_to_le16(mode->crtc_hdisplay - (amdgpu_crtc->h_border * 2));
	args.usH_Blanking_Time =
		cpu_to_le16(mode->crtc_hblank_end - mode->crtc_hdisplay + (amdgpu_crtc->h_border * 2));
	args.usV_Size = cpu_to_le16(mode->crtc_vdisplay - (amdgpu_crtc->v_border * 2));
	args.usV_Blanking_Time =
		cpu_to_le16(mode->crtc_vblank_end - mode->crtc_vdisplay + (amdgpu_crtc->v_border * 2));
	args.usH_SyncOffset =
		cpu_to_le16(mode->crtc_hsync_start - mode->crtc_hdisplay + amdgpu_crtc->h_border);
	args.usH_SyncWidth =
		cpu_to_le16(mode->crtc_hsync_end - mode->crtc_hsync_start);
	args.usV_SyncOffset =
		cpu_to_le16(mode->crtc_vsync_start - mode->crtc_vdisplay + amdgpu_crtc->v_border);
	args.usV_SyncWidth =
		cpu_to_le16(mode->crtc_vsync_end - mode->crtc_vsync_start);
	args.ucH_Border = amdgpu_crtc->h_border;
	args.ucV_Border = amdgpu_crtc->v_border;

	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		misc |= ATOM_VSYNC_POLARITY;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		misc |= ATOM_HSYNC_POLARITY;
	if (mode->flags & DRM_MODE_FLAG_CSYNC)
		misc |= ATOM_COMPOSITESYNC;
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		misc |= ATOM_INTERLACE;
	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		misc |= ATOM_DOUBLE_CLOCK_MODE;

	args.susModeMiscInfo.usAccess = cpu_to_le16(misc);
	args.ucCRTC = amdgpu_crtc->crtc_id;

	amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);
}

union atom_enable_ss {
	ENABLE_SPREAD_SPECTRUM_ON_PPLL_PS_ALLOCATION v1;
	ENABLE_SPREAD_SPECTRUM_ON_PPLL_V2 v2;
	ENABLE_SPREAD_SPECTRUM_ON_PPLL_V3 v3;
};

static void amdgpu_atombios_crtc_program_ss(struct amdgpu_device *adev,
				     int enable,
				     int pll_id,
				     int crtc_id,
				     struct amdgpu_atom_ss *ss)
{
	unsigned i;
	int index = GetIndexIntoMasterTable(COMMAND, EnableSpreadSpectrumOnPPLL);
	union atom_enable_ss args;

	if (enable) {
		/* Don't mess with SS if percentage is 0 or external ss.
		 * SS is already disabled previously, and disabling it
		 * again can cause display problems if the pll is already
		 * programmed.
		 */
		if (ss->percentage == 0)
			return;
		if (ss->type & ATOM_EXTERNAL_SS_MASK)
			return;
	} else {
		for (i = 0; i < adev->mode_info.num_crtc; i++) {
			if (adev->mode_info.crtcs[i] &&
			    adev->mode_info.crtcs[i]->enabled &&
			    i != crtc_id &&
			    pll_id == adev->mode_info.crtcs[i]->pll_id) {
				/* one other crtc is using this pll don't turn
				 * off spread spectrum as it might turn off
				 * display on active crtc
				 */
				return;
			}
		}
	}

	memset(&args, 0, sizeof(args));

	args.v3.usSpreadSpectrumAmountFrac = cpu_to_le16(0);
	args.v3.ucSpreadSpectrumType = ss->type & ATOM_SS_CENTRE_SPREAD_MODE_MASK;
	switch (pll_id) {
	case ATOM_PPLL1:
		args.v3.ucSpreadSpectrumType |= ATOM_PPLL_SS_TYPE_V3_P1PLL;
		break;
	case ATOM_PPLL2:
		args.v3.ucSpreadSpectrumType |= ATOM_PPLL_SS_TYPE_V3_P2PLL;
		break;
	case ATOM_DCPLL:
		args.v3.ucSpreadSpectrumType |= ATOM_PPLL_SS_TYPE_V3_DCPLL;
		break;
	case ATOM_PPLL_INVALID:
		return;
	}
	args.v3.usSpreadSpectrumAmount = cpu_to_le16(ss->amount);
	args.v3.usSpreadSpectrumStep = cpu_to_le16(ss->step);
	args.v3.ucEnable = enable;

	amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);
}

union adjust_pixel_clock {
	ADJUST_DISPLAY_PLL_PS_ALLOCATION v1;
	ADJUST_DISPLAY_PLL_PS_ALLOCATION_V3 v3;
};

static u32 amdgpu_atombios_crtc_adjust_pll(struct drm_crtc *crtc,
				    struct drm_display_mode *mode)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct drm_encoder *encoder = amdgpu_crtc->encoder;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct drm_connector *connector = amdgpu_get_connector_for_encoder(encoder);
	u32 adjusted_clock = mode->clock;
	int encoder_mode = amdgpu_atombios_encoder_get_encoder_mode(encoder);
	u32 dp_clock = mode->clock;
	u32 clock = mode->clock;
	int bpc = amdgpu_crtc->bpc;
	bool is_duallink = amdgpu_dig_monitor_is_duallink(encoder, mode->clock);
	union adjust_pixel_clock args;
	u8 frev, crev;
	int index;

	amdgpu_crtc->pll_flags = AMDGPU_PLL_USE_FRAC_FB_DIV;

	if ((amdgpu_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT | ATOM_DEVICE_DFP_SUPPORT)) ||
	    (amdgpu_encoder_get_dp_bridge_encoder_id(encoder) != ENCODER_OBJECT_ID_NONE)) {
		if (connector) {
			struct amdgpu_connector *amdgpu_connector = to_amdgpu_connector(connector);
			struct amdgpu_connector_atom_dig *dig_connector =
				amdgpu_connector->con_priv;

			dp_clock = dig_connector->dp_clock;
		}
	}

	/* use recommended ref_div for ss */
	if (amdgpu_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) {
		if (amdgpu_crtc->ss_enabled) {
			if (amdgpu_crtc->ss.refdiv) {
				amdgpu_crtc->pll_flags |= AMDGPU_PLL_USE_REF_DIV;
				amdgpu_crtc->pll_reference_div = amdgpu_crtc->ss.refdiv;
				amdgpu_crtc->pll_flags |= AMDGPU_PLL_USE_FRAC_FB_DIV;
			}
		}
	}

	/* DVO wants 2x pixel clock if the DVO chip is in 12 bit mode */
	if (amdgpu_encoder->encoder_id == ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1)
		adjusted_clock = mode->clock * 2;
	if (amdgpu_encoder->active_device & (ATOM_DEVICE_TV_SUPPORT))
		amdgpu_crtc->pll_flags |= AMDGPU_PLL_PREFER_CLOSEST_LOWER;
	if (amdgpu_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT))
		amdgpu_crtc->pll_flags |= AMDGPU_PLL_IS_LCD;


	/* adjust pll for deep color modes */
	if (encoder_mode == ATOM_ENCODER_MODE_HDMI) {
		switch (bpc) {
		case 8:
		default:
			break;
		case 10:
			clock = (clock * 5) / 4;
			break;
		case 12:
			clock = (clock * 3) / 2;
			break;
		case 16:
			clock = clock * 2;
			break;
		}
	}

	/* DCE3+ has an AdjustDisplayPll that will adjust the pixel clock
	 * accordingly based on the encoder/transmitter to work around
	 * special hw requirements.
	 */
	index = GetIndexIntoMasterTable(COMMAND, AdjustDisplayPll);
	if (!amdgpu_atom_parse_cmd_header(adev->mode_info.atom_context, index, &frev,
				   &crev))
		return adjusted_clock;

	memset(&args, 0, sizeof(args));

	switch (frev) {
	case 1:
		switch (crev) {
		case 1:
		case 2:
			args.v1.usPixelClock = cpu_to_le16(clock / 10);
			args.v1.ucTransmitterID = amdgpu_encoder->encoder_id;
			args.v1.ucEncodeMode = encoder_mode;
			if (amdgpu_crtc->ss_enabled && amdgpu_crtc->ss.percentage)
				args.v1.ucConfig |=
					ADJUST_DISPLAY_CONFIG_SS_ENABLE;

			amdgpu_atom_execute_table(adev->mode_info.atom_context,
					   index, (uint32_t *)&args);
			adjusted_clock = le16_to_cpu(args.v1.usPixelClock) * 10;
			break;
		case 3:
			args.v3.sInput.usPixelClock = cpu_to_le16(clock / 10);
			args.v3.sInput.ucTransmitterID = amdgpu_encoder->encoder_id;
			args.v3.sInput.ucEncodeMode = encoder_mode;
			args.v3.sInput.ucDispPllConfig = 0;
			if (amdgpu_crtc->ss_enabled && amdgpu_crtc->ss.percentage)
				args.v3.sInput.ucDispPllConfig |=
					DISPPLL_CONFIG_SS_ENABLE;
			if (ENCODER_MODE_IS_DP(encoder_mode)) {
				args.v3.sInput.ucDispPllConfig |=
					DISPPLL_CONFIG_COHERENT_MODE;
				/* 16200 or 27000 */
				args.v3.sInput.usPixelClock = cpu_to_le16(dp_clock / 10);
			} else if (amdgpu_encoder->devices & (ATOM_DEVICE_DFP_SUPPORT)) {
				struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;
				if (dig->coherent_mode)
					args.v3.sInput.ucDispPllConfig |=
						DISPPLL_CONFIG_COHERENT_MODE;
				if (is_duallink)
					args.v3.sInput.ucDispPllConfig |=
						DISPPLL_CONFIG_DUAL_LINK;
			}
			if (amdgpu_encoder_get_dp_bridge_encoder_id(encoder) !=
			    ENCODER_OBJECT_ID_NONE)
				args.v3.sInput.ucExtTransmitterID =
					amdgpu_encoder_get_dp_bridge_encoder_id(encoder);
			else
				args.v3.sInput.ucExtTransmitterID = 0;

			amdgpu_atom_execute_table(adev->mode_info.atom_context,
					   index, (uint32_t *)&args);
			adjusted_clock = le32_to_cpu(args.v3.sOutput.ulDispPllFreq) * 10;
			if (args.v3.sOutput.ucRefDiv) {
				amdgpu_crtc->pll_flags |= AMDGPU_PLL_USE_FRAC_FB_DIV;
				amdgpu_crtc->pll_flags |= AMDGPU_PLL_USE_REF_DIV;
				amdgpu_crtc->pll_reference_div = args.v3.sOutput.ucRefDiv;
			}
			if (args.v3.sOutput.ucPostDiv) {
				amdgpu_crtc->pll_flags |= AMDGPU_PLL_USE_FRAC_FB_DIV;
				amdgpu_crtc->pll_flags |= AMDGPU_PLL_USE_POST_DIV;
				amdgpu_crtc->pll_post_div = args.v3.sOutput.ucPostDiv;
			}
			break;
		default:
			DRM_ERROR("Unknown table version %d %d\n", frev, crev);
			return adjusted_clock;
		}
		break;
	default:
		DRM_ERROR("Unknown table version %d %d\n", frev, crev);
		return adjusted_clock;
	}

	return adjusted_clock;
}

union set_pixel_clock {
	SET_PIXEL_CLOCK_PS_ALLOCATION base;
	PIXEL_CLOCK_PARAMETERS v1;
	PIXEL_CLOCK_PARAMETERS_V2 v2;
	PIXEL_CLOCK_PARAMETERS_V3 v3;
	PIXEL_CLOCK_PARAMETERS_V5 v5;
	PIXEL_CLOCK_PARAMETERS_V6 v6;
	PIXEL_CLOCK_PARAMETERS_V7 v7;
};

/* on DCE5, make sure the voltage is high enough to support the
 * required disp clk.
 */
void amdgpu_atombios_crtc_set_disp_eng_pll(struct amdgpu_device *adev,
					   u32 dispclk)
{
	u8 frev, crev;
	int index;
	union set_pixel_clock args;

	memset(&args, 0, sizeof(args));

	index = GetIndexIntoMasterTable(COMMAND, SetPixelClock);
	if (!amdgpu_atom_parse_cmd_header(adev->mode_info.atom_context, index, &frev,
				   &crev))
		return;

	switch (frev) {
	case 1:
		switch (crev) {
		case 5:
			/* if the default dcpll clock is specified,
			 * SetPixelClock provides the dividers
			 */
			args.v5.ucCRTC = ATOM_CRTC_INVALID;
			args.v5.usPixelClock = cpu_to_le16(dispclk);
			args.v5.ucPpll = ATOM_DCPLL;
			break;
		case 6:
			/* if the default dcpll clock is specified,
			 * SetPixelClock provides the dividers
			 */
			args.v6.ulDispEngClkFreq = cpu_to_le32(dispclk);
			if (adev->asic_type == CHIP_TAHITI ||
			    adev->asic_type == CHIP_PITCAIRN ||
			    adev->asic_type == CHIP_VERDE ||
			    adev->asic_type == CHIP_OLAND)
				args.v6.ucPpll = ATOM_PPLL0;
			else
				args.v6.ucPpll = ATOM_EXT_PLL1;
			break;
		default:
			DRM_ERROR("Unknown table version %d %d\n", frev, crev);
			return;
		}
		break;
	default:
		DRM_ERROR("Unknown table version %d %d\n", frev, crev);
		return;
	}
	amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);
}

union set_dce_clock {
	SET_DCE_CLOCK_PS_ALLOCATION_V1_1 v1_1;
	SET_DCE_CLOCK_PS_ALLOCATION_V2_1 v2_1;
};

u32 amdgpu_atombios_crtc_set_dce_clock(struct amdgpu_device *adev,
				       u32 freq, u8 clk_type, u8 clk_src)
{
	u8 frev, crev;
	int index;
	union set_dce_clock args;
	u32 ret_freq = 0;

	memset(&args, 0, sizeof(args));

	index = GetIndexIntoMasterTable(COMMAND, SetDCEClock);
	if (!amdgpu_atom_parse_cmd_header(adev->mode_info.atom_context, index, &frev,
				   &crev))
		return 0;

	switch (frev) {
	case 2:
		switch (crev) {
		case 1:
			args.v2_1.asParam.ulDCEClkFreq = cpu_to_le32(freq); /* 10kHz units */
			args.v2_1.asParam.ucDCEClkType = clk_type;
			args.v2_1.asParam.ucDCEClkSrc = clk_src;
			amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);
			ret_freq = le32_to_cpu(args.v2_1.asParam.ulDCEClkFreq) * 10;
			break;
		default:
			DRM_ERROR("Unknown table version %d %d\n", frev, crev);
			return 0;
		}
		break;
	default:
		DRM_ERROR("Unknown table version %d %d\n", frev, crev);
		return 0;
	}

	return ret_freq;
}

static bool is_pixel_clock_source_from_pll(u32 encoder_mode, int pll_id)
{
	if (ENCODER_MODE_IS_DP(encoder_mode)) {
		if (pll_id < ATOM_EXT_PLL1)
			return true;
		else
			return false;
	} else {
		return true;
	}
}

void amdgpu_atombios_crtc_program_pll(struct drm_crtc *crtc,
				      u32 crtc_id,
				      int pll_id,
				      u32 encoder_mode,
				      u32 encoder_id,
				      u32 clock,
				      u32 ref_div,
				      u32 fb_div,
				      u32 frac_fb_div,
				      u32 post_div,
				      int bpc,
				      bool ss_enabled,
				      struct amdgpu_atom_ss *ss)
{
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = dev->dev_private;
	u8 frev, crev;
	int index = GetIndexIntoMasterTable(COMMAND, SetPixelClock);
	union set_pixel_clock args;

	memset(&args, 0, sizeof(args));

	if (!amdgpu_atom_parse_cmd_header(adev->mode_info.atom_context, index, &frev,
				   &crev))
		return;

	switch (frev) {
	case 1:
		switch (crev) {
		case 1:
			if (clock == ATOM_DISABLE)
				return;
			args.v1.usPixelClock = cpu_to_le16(clock / 10);
			args.v1.usRefDiv = cpu_to_le16(ref_div);
			args.v1.usFbDiv = cpu_to_le16(fb_div);
			args.v1.ucFracFbDiv = frac_fb_div;
			args.v1.ucPostDiv = post_div;
			args.v1.ucPpll = pll_id;
			args.v1.ucCRTC = crtc_id;
			args.v1.ucRefDivSrc = 1;
			break;
		case 2:
			args.v2.usPixelClock = cpu_to_le16(clock / 10);
			args.v2.usRefDiv = cpu_to_le16(ref_div);
			args.v2.usFbDiv = cpu_to_le16(fb_div);
			args.v2.ucFracFbDiv = frac_fb_div;
			args.v2.ucPostDiv = post_div;
			args.v2.ucPpll = pll_id;
			args.v2.ucCRTC = crtc_id;
			args.v2.ucRefDivSrc = 1;
			break;
		case 3:
			args.v3.usPixelClock = cpu_to_le16(clock / 10);
			args.v3.usRefDiv = cpu_to_le16(ref_div);
			args.v3.usFbDiv = cpu_to_le16(fb_div);
			args.v3.ucFracFbDiv = frac_fb_div;
			args.v3.ucPostDiv = post_div;
			args.v3.ucPpll = pll_id;
			if (crtc_id == ATOM_CRTC2)
				args.v3.ucMiscInfo = PIXEL_CLOCK_MISC_CRTC_SEL_CRTC2;
			else
				args.v3.ucMiscInfo = PIXEL_CLOCK_MISC_CRTC_SEL_CRTC1;
			if (ss_enabled && (ss->type & ATOM_EXTERNAL_SS_MASK))
				args.v3.ucMiscInfo |= PIXEL_CLOCK_MISC_REF_DIV_SRC;
			args.v3.ucTransmitterId = encoder_id;
			args.v3.ucEncoderMode = encoder_mode;
			break;
		case 5:
			args.v5.ucCRTC = crtc_id;
			args.v5.usPixelClock = cpu_to_le16(clock / 10);
			args.v5.ucRefDiv = ref_div;
			args.v5.usFbDiv = cpu_to_le16(fb_div);
			args.v5.ulFbDivDecFrac = cpu_to_le32(frac_fb_div * 100000);
			args.v5.ucPostDiv = post_div;
			args.v5.ucMiscInfo = 0; /* HDMI depth, etc. */
			if ((ss_enabled && (ss->type & ATOM_EXTERNAL_SS_MASK)) &&
			    (pll_id < ATOM_EXT_PLL1))
				args.v5.ucMiscInfo |= PIXEL_CLOCK_V5_MISC_REF_DIV_SRC;
			if (encoder_mode == ATOM_ENCODER_MODE_HDMI) {
				switch (bpc) {
				case 8:
				default:
					args.v5.ucMiscInfo |= PIXEL_CLOCK_V5_MISC_HDMI_24BPP;
					break;
				case 10:
					/* yes this is correct, the atom define is wrong */
					args.v5.ucMiscInfo |= PIXEL_CLOCK_V5_MISC_HDMI_32BPP;
					break;
				case 12:
					/* yes this is correct, the atom define is wrong */
					args.v5.ucMiscInfo |= PIXEL_CLOCK_V5_MISC_HDMI_30BPP;
					break;
				}
			}
			args.v5.ucTransmitterID = encoder_id;
			args.v5.ucEncoderMode = encoder_mode;
			args.v5.ucPpll = pll_id;
			break;
		case 6:
			args.v6.ulDispEngClkFreq = cpu_to_le32(crtc_id << 24 | clock / 10);
			args.v6.ucRefDiv = ref_div;
			args.v6.usFbDiv = cpu_to_le16(fb_div);
			args.v6.ulFbDivDecFrac = cpu_to_le32(frac_fb_div * 100000);
			args.v6.ucPostDiv = post_div;
			args.v6.ucMiscInfo = 0; /* HDMI depth, etc. */
			if ((ss_enabled && (ss->type & ATOM_EXTERNAL_SS_MASK)) &&
			    (pll_id < ATOM_EXT_PLL1) &&
			    !is_pixel_clock_source_from_pll(encoder_mode, pll_id))
				args.v6.ucMiscInfo |= PIXEL_CLOCK_V6_MISC_REF_DIV_SRC;
			if (encoder_mode == ATOM_ENCODER_MODE_HDMI) {
				switch (bpc) {
				case 8:
				default:
					args.v6.ucMiscInfo |= PIXEL_CLOCK_V6_MISC_HDMI_24BPP;
					break;
				case 10:
					args.v6.ucMiscInfo |= PIXEL_CLOCK_V6_MISC_HDMI_30BPP_V6;
					break;
				case 12:
					args.v6.ucMiscInfo |= PIXEL_CLOCK_V6_MISC_HDMI_36BPP_V6;
					break;
				case 16:
					args.v6.ucMiscInfo |= PIXEL_CLOCK_V6_MISC_HDMI_48BPP;
					break;
				}
			}
			args.v6.ucTransmitterID = encoder_id;
			args.v6.ucEncoderMode = encoder_mode;
			args.v6.ucPpll = pll_id;
			break;
		case 7:
			args.v7.ulPixelClock = cpu_to_le32(clock * 10); /* 100 hz units */
			args.v7.ucMiscInfo = 0;
			if ((encoder_mode == ATOM_ENCODER_MODE_DVI) &&
			    (clock > 165000))
				args.v7.ucMiscInfo |= PIXEL_CLOCK_V7_MISC_DVI_DUALLINK_EN;
			args.v7.ucCRTC = crtc_id;
			if (encoder_mode == ATOM_ENCODER_MODE_HDMI) {
				switch (bpc) {
				case 8:
				default:
					args.v7.ucDeepColorRatio = PIXEL_CLOCK_V7_DEEPCOLOR_RATIO_DIS;
					break;
				case 10:
					args.v7.ucDeepColorRatio = PIXEL_CLOCK_V7_DEEPCOLOR_RATIO_5_4;
					break;
				case 12:
					args.v7.ucDeepColorRatio = PIXEL_CLOCK_V7_DEEPCOLOR_RATIO_3_2;
					break;
				case 16:
					args.v7.ucDeepColorRatio = PIXEL_CLOCK_V7_DEEPCOLOR_RATIO_2_1;
					break;
				}
			}
			args.v7.ucTransmitterID = encoder_id;
			args.v7.ucEncoderMode = encoder_mode;
			args.v7.ucPpll = pll_id;
			break;
		default:
			DRM_ERROR("Unknown table version %d %d\n", frev, crev);
			return;
		}
		break;
	default:
		DRM_ERROR("Unknown table version %d %d\n", frev, crev);
		return;
	}

	amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);
}

int amdgpu_atombios_crtc_prepare_pll(struct drm_crtc *crtc,
			      struct drm_display_mode *mode)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_encoder *amdgpu_encoder =
		to_amdgpu_encoder(amdgpu_crtc->encoder);
	int encoder_mode = amdgpu_atombios_encoder_get_encoder_mode(amdgpu_crtc->encoder);

	amdgpu_crtc->bpc = 8;
	amdgpu_crtc->ss_enabled = false;

	if ((amdgpu_encoder->active_device & (ATOM_DEVICE_LCD_SUPPORT | ATOM_DEVICE_DFP_SUPPORT)) ||
	    (amdgpu_encoder_get_dp_bridge_encoder_id(amdgpu_crtc->encoder) != ENCODER_OBJECT_ID_NONE)) {
		struct amdgpu_encoder_atom_dig *dig = amdgpu_encoder->enc_priv;
		struct drm_connector *connector =
			amdgpu_get_connector_for_encoder(amdgpu_crtc->encoder);
		struct amdgpu_connector *amdgpu_connector =
			to_amdgpu_connector(connector);
		struct amdgpu_connector_atom_dig *dig_connector =
			amdgpu_connector->con_priv;
		int dp_clock;

		/* Assign mode clock for hdmi deep color max clock limit check */
		amdgpu_connector->pixelclock_for_modeset = mode->clock;
		amdgpu_crtc->bpc = amdgpu_connector_get_monitor_bpc(connector);

		switch (encoder_mode) {
		case ATOM_ENCODER_MODE_DP_MST:
		case ATOM_ENCODER_MODE_DP:
			/* DP/eDP */
			dp_clock = dig_connector->dp_clock / 10;
			amdgpu_crtc->ss_enabled =
				amdgpu_atombios_get_asic_ss_info(adev, &amdgpu_crtc->ss,
								 ASIC_INTERNAL_SS_ON_DP,
								 dp_clock);
			break;
		case ATOM_ENCODER_MODE_LVDS:
			amdgpu_crtc->ss_enabled =
				amdgpu_atombios_get_asic_ss_info(adev,
								 &amdgpu_crtc->ss,
								 dig->lcd_ss_id,
								 mode->clock / 10);
			break;
		case ATOM_ENCODER_MODE_DVI:
			amdgpu_crtc->ss_enabled =
				amdgpu_atombios_get_asic_ss_info(adev,
								 &amdgpu_crtc->ss,
								 ASIC_INTERNAL_SS_ON_TMDS,
								 mode->clock / 10);
			break;
		case ATOM_ENCODER_MODE_HDMI:
			amdgpu_crtc->ss_enabled =
				amdgpu_atombios_get_asic_ss_info(adev,
								 &amdgpu_crtc->ss,
								 ASIC_INTERNAL_SS_ON_HDMI,
								 mode->clock / 10);
			break;
		default:
			break;
		}
	}

	/* adjust pixel clock as needed */
	amdgpu_crtc->adjusted_clock = amdgpu_atombios_crtc_adjust_pll(crtc, mode);

	return 0;
}

void amdgpu_atombios_crtc_set_pll(struct drm_crtc *crtc, struct drm_display_mode *mode)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_encoder *amdgpu_encoder =
		to_amdgpu_encoder(amdgpu_crtc->encoder);
	u32 pll_clock = mode->clock;
	u32 clock = mode->clock;
	u32 ref_div = 0, fb_div = 0, frac_fb_div = 0, post_div = 0;
	struct amdgpu_pll *pll;
	int encoder_mode = amdgpu_atombios_encoder_get_encoder_mode(amdgpu_crtc->encoder);

	/* pass the actual clock to amdgpu_atombios_crtc_program_pll for HDMI */
	if ((encoder_mode == ATOM_ENCODER_MODE_HDMI) &&
	    (amdgpu_crtc->bpc > 8))
		clock = amdgpu_crtc->adjusted_clock;

	switch (amdgpu_crtc->pll_id) {
	case ATOM_PPLL1:
		pll = &adev->clock.ppll[0];
		break;
	case ATOM_PPLL2:
		pll = &adev->clock.ppll[1];
		break;
	case ATOM_PPLL0:
	case ATOM_PPLL_INVALID:
	default:
		pll = &adev->clock.ppll[2];
		break;
	}

	/* update pll params */
	pll->flags = amdgpu_crtc->pll_flags;
	pll->reference_div = amdgpu_crtc->pll_reference_div;
	pll->post_div = amdgpu_crtc->pll_post_div;

	amdgpu_pll_compute(pll, amdgpu_crtc->adjusted_clock, &pll_clock,
			    &fb_div, &frac_fb_div, &ref_div, &post_div);

	amdgpu_atombios_crtc_program_ss(adev, ATOM_DISABLE, amdgpu_crtc->pll_id,
				 amdgpu_crtc->crtc_id, &amdgpu_crtc->ss);

	amdgpu_atombios_crtc_program_pll(crtc, amdgpu_crtc->crtc_id, amdgpu_crtc->pll_id,
				  encoder_mode, amdgpu_encoder->encoder_id, clock,
				  ref_div, fb_div, frac_fb_div, post_div,
				  amdgpu_crtc->bpc, amdgpu_crtc->ss_enabled, &amdgpu_crtc->ss);

	if (amdgpu_crtc->ss_enabled) {
		/* calculate ss amount and step size */
		u32 step_size;
		u32 amount = (((fb_div * 10) + frac_fb_div) *
			      (u32)amdgpu_crtc->ss.percentage) /
			(100 * (u32)amdgpu_crtc->ss.percentage_divider);
		amdgpu_crtc->ss.amount = (amount / 10) & ATOM_PPLL_SS_AMOUNT_V2_FBDIV_MASK;
		amdgpu_crtc->ss.amount |= ((amount - (amount / 10)) << ATOM_PPLL_SS_AMOUNT_V2_NFRAC_SHIFT) &
			ATOM_PPLL_SS_AMOUNT_V2_NFRAC_MASK;
		if (amdgpu_crtc->ss.type & ATOM_PPLL_SS_TYPE_V2_CENTRE_SPREAD)
			step_size = (4 * amount * ref_div * ((u32)amdgpu_crtc->ss.rate * 2048)) /
				(125 * 25 * pll->reference_freq / 100);
		else
			step_size = (2 * amount * ref_div * ((u32)amdgpu_crtc->ss.rate * 2048)) /
				(125 * 25 * pll->reference_freq / 100);
		amdgpu_crtc->ss.step = step_size;

		amdgpu_atombios_crtc_program_ss(adev, ATOM_ENABLE, amdgpu_crtc->pll_id,
					 amdgpu_crtc->crtc_id, &amdgpu_crtc->ss);
	}
}

