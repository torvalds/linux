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
#include <drm/radeon_drm.h>
#include <drm/drm_fixed.h>
#include "radeon.h"
#include "atom.h"
#include "atom-bits.h"

static void atombios_overscan_setup(struct drm_crtc *crtc,
				    struct drm_display_mode *mode,
				    struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	SET_CRTC_OVERSCAN_PS_ALLOCATION args;
	int index = GetIndexIntoMasterTable(COMMAND, SetCRTC_OverScan);
	int a1, a2;

	memset(&args, 0, sizeof(args));

	args.ucCRTC = radeon_crtc->crtc_id;

	switch (radeon_crtc->rmx_type) {
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
		args.usOverscanRight = cpu_to_le16(radeon_crtc->h_border);
		args.usOverscanLeft = cpu_to_le16(radeon_crtc->h_border);
		args.usOverscanBottom = cpu_to_le16(radeon_crtc->v_border);
		args.usOverscanTop = cpu_to_le16(radeon_crtc->v_border);
		break;
	}
	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

static void atombios_scaler_setup(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	ENABLE_SCALER_PS_ALLOCATION args;
	int index = GetIndexIntoMasterTable(COMMAND, EnableScaler);

	/* fixme - fill in enc_priv for atom dac */
	enum radeon_tv_std tv_std = TV_STD_NTSC;
	bool is_tv = false, is_cv = false;
	struct drm_encoder *encoder;

	if (!ASIC_IS_AVIVO(rdev) && radeon_crtc->crtc_id)
		return;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		/* find tv std */
		if (encoder->crtc == crtc) {
			struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
			if (radeon_encoder->active_device & ATOM_DEVICE_TV_SUPPORT) {
				struct radeon_encoder_atom_dac *tv_dac = radeon_encoder->enc_priv;
				tv_std = tv_dac->tv_std;
				is_tv = true;
			}
		}
	}

	memset(&args, 0, sizeof(args));

	args.ucScaler = radeon_crtc->crtc_id;

	if (is_tv) {
		switch (tv_std) {
		case TV_STD_NTSC:
		default:
			args.ucTVStandard = ATOM_TV_NTSC;
			break;
		case TV_STD_PAL:
			args.ucTVStandard = ATOM_TV_PAL;
			break;
		case TV_STD_PAL_M:
			args.ucTVStandard = ATOM_TV_PALM;
			break;
		case TV_STD_PAL_60:
			args.ucTVStandard = ATOM_TV_PAL60;
			break;
		case TV_STD_NTSC_J:
			args.ucTVStandard = ATOM_TV_NTSCJ;
			break;
		case TV_STD_SCART_PAL:
			args.ucTVStandard = ATOM_TV_PAL; /* ??? */
			break;
		case TV_STD_SECAM:
			args.ucTVStandard = ATOM_TV_SECAM;
			break;
		case TV_STD_PAL_CN:
			args.ucTVStandard = ATOM_TV_PALCN;
			break;
		}
		args.ucEnable = SCALER_ENABLE_MULTITAP_MODE;
	} else if (is_cv) {
		args.ucTVStandard = ATOM_TV_CV;
		args.ucEnable = SCALER_ENABLE_MULTITAP_MODE;
	} else {
		switch (radeon_crtc->rmx_type) {
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
			if (ASIC_IS_AVIVO(rdev))
				args.ucEnable = ATOM_SCALER_DISABLE;
			else
				args.ucEnable = ATOM_SCALER_CENTER;
			break;
		}
	}
	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
	if ((is_tv || is_cv)
	    && rdev->family >= CHIP_RV515 && rdev->family <= CHIP_R580) {
		atom_rv515_force_tv_scaler(rdev, radeon_crtc);
	}
}

static void atombios_lock_crtc(struct drm_crtc *crtc, int lock)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	int index =
	    GetIndexIntoMasterTable(COMMAND, UpdateCRTC_DoubleBufferRegisters);
	ENABLE_CRTC_PS_ALLOCATION args;

	memset(&args, 0, sizeof(args));

	args.ucCRTC = radeon_crtc->crtc_id;
	args.ucEnable = lock;

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

static void atombios_enable_crtc(struct drm_crtc *crtc, int state)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	int index = GetIndexIntoMasterTable(COMMAND, EnableCRTC);
	ENABLE_CRTC_PS_ALLOCATION args;

	memset(&args, 0, sizeof(args));

	args.ucCRTC = radeon_crtc->crtc_id;
	args.ucEnable = state;

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

static void atombios_enable_crtc_memreq(struct drm_crtc *crtc, int state)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	int index = GetIndexIntoMasterTable(COMMAND, EnableCRTCMemReq);
	ENABLE_CRTC_PS_ALLOCATION args;

	memset(&args, 0, sizeof(args));

	args.ucCRTC = radeon_crtc->crtc_id;
	args.ucEnable = state;

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

static void atombios_blank_crtc(struct drm_crtc *crtc, int state)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	int index = GetIndexIntoMasterTable(COMMAND, BlankCRTC);
	BLANK_CRTC_PS_ALLOCATION args;

	memset(&args, 0, sizeof(args));

	args.ucCRTC = radeon_crtc->crtc_id;
	args.ucBlanking = state;

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

static void atombios_powergate_crtc(struct drm_crtc *crtc, int state)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	int index = GetIndexIntoMasterTable(COMMAND, EnableDispPowerGating);
	ENABLE_DISP_POWER_GATING_PARAMETERS_V2_1 args;

	memset(&args, 0, sizeof(args));

	args.ucDispPipeId = radeon_crtc->crtc_id;
	args.ucEnable = state;

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

void atombios_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		radeon_crtc->enabled = true;
		/* adjust pm to dpms changes BEFORE enabling crtcs */
		radeon_pm_compute_clocks(rdev);
		/* disable crtc pair power gating before programming */
		if (ASIC_IS_DCE6(rdev) && !radeon_crtc->in_mode_set)
			atombios_powergate_crtc(crtc, ATOM_DISABLE);
		atombios_enable_crtc(crtc, ATOM_ENABLE);
		if (ASIC_IS_DCE3(rdev) && !ASIC_IS_DCE6(rdev))
			atombios_enable_crtc_memreq(crtc, ATOM_ENABLE);
		atombios_blank_crtc(crtc, ATOM_DISABLE);
		drm_vblank_post_modeset(dev, radeon_crtc->crtc_id);
		radeon_crtc_load_lut(crtc);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		drm_vblank_pre_modeset(dev, radeon_crtc->crtc_id);
		if (radeon_crtc->enabled)
			atombios_blank_crtc(crtc, ATOM_ENABLE);
		if (ASIC_IS_DCE3(rdev) && !ASIC_IS_DCE6(rdev))
			atombios_enable_crtc_memreq(crtc, ATOM_DISABLE);
		atombios_enable_crtc(crtc, ATOM_DISABLE);
		radeon_crtc->enabled = false;
		/* power gating is per-pair */
		if (ASIC_IS_DCE6(rdev) && !radeon_crtc->in_mode_set) {
			struct drm_crtc *other_crtc;
			struct radeon_crtc *other_radeon_crtc;
			list_for_each_entry(other_crtc, &rdev->ddev->mode_config.crtc_list, head) {
				other_radeon_crtc = to_radeon_crtc(other_crtc);
				if (((radeon_crtc->crtc_id == 0) && (other_radeon_crtc->crtc_id == 1)) ||
				    ((radeon_crtc->crtc_id == 1) && (other_radeon_crtc->crtc_id == 0)) ||
				    ((radeon_crtc->crtc_id == 2) && (other_radeon_crtc->crtc_id == 3)) ||
				    ((radeon_crtc->crtc_id == 3) && (other_radeon_crtc->crtc_id == 2)) ||
				    ((radeon_crtc->crtc_id == 4) && (other_radeon_crtc->crtc_id == 5)) ||
				    ((radeon_crtc->crtc_id == 5) && (other_radeon_crtc->crtc_id == 4))) {
					/* if both crtcs in the pair are off, enable power gating */
					if (other_radeon_crtc->enabled == false)
						atombios_powergate_crtc(crtc, ATOM_ENABLE);
					break;
				}
			}
		}
		/* adjust pm to dpms changes AFTER disabling crtcs */
		radeon_pm_compute_clocks(rdev);
		break;
	}
}

static void
atombios_set_crtc_dtd_timing(struct drm_crtc *crtc,
			     struct drm_display_mode *mode)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	SET_CRTC_USING_DTD_TIMING_PARAMETERS args;
	int index = GetIndexIntoMasterTable(COMMAND, SetCRTC_UsingDTDTiming);
	u16 misc = 0;

	memset(&args, 0, sizeof(args));
	args.usH_Size = cpu_to_le16(mode->crtc_hdisplay - (radeon_crtc->h_border * 2));
	args.usH_Blanking_Time =
		cpu_to_le16(mode->crtc_hblank_end - mode->crtc_hdisplay + (radeon_crtc->h_border * 2));
	args.usV_Size = cpu_to_le16(mode->crtc_vdisplay - (radeon_crtc->v_border * 2));
	args.usV_Blanking_Time =
		cpu_to_le16(mode->crtc_vblank_end - mode->crtc_vdisplay + (radeon_crtc->v_border * 2));
	args.usH_SyncOffset =
		cpu_to_le16(mode->crtc_hsync_start - mode->crtc_hdisplay + radeon_crtc->h_border);
	args.usH_SyncWidth =
		cpu_to_le16(mode->crtc_hsync_end - mode->crtc_hsync_start);
	args.usV_SyncOffset =
		cpu_to_le16(mode->crtc_vsync_start - mode->crtc_vdisplay + radeon_crtc->v_border);
	args.usV_SyncWidth =
		cpu_to_le16(mode->crtc_vsync_end - mode->crtc_vsync_start);
	args.ucH_Border = radeon_crtc->h_border;
	args.ucV_Border = radeon_crtc->v_border;

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
	args.ucCRTC = radeon_crtc->crtc_id;

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

static void atombios_crtc_set_timing(struct drm_crtc *crtc,
				     struct drm_display_mode *mode)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	SET_CRTC_TIMING_PARAMETERS_PS_ALLOCATION args;
	int index = GetIndexIntoMasterTable(COMMAND, SetCRTC_Timing);
	u16 misc = 0;

	memset(&args, 0, sizeof(args));
	args.usH_Total = cpu_to_le16(mode->crtc_htotal);
	args.usH_Disp = cpu_to_le16(mode->crtc_hdisplay);
	args.usH_SyncStart = cpu_to_le16(mode->crtc_hsync_start);
	args.usH_SyncWidth =
		cpu_to_le16(mode->crtc_hsync_end - mode->crtc_hsync_start);
	args.usV_Total = cpu_to_le16(mode->crtc_vtotal);
	args.usV_Disp = cpu_to_le16(mode->crtc_vdisplay);
	args.usV_SyncStart = cpu_to_le16(mode->crtc_vsync_start);
	args.usV_SyncWidth =
		cpu_to_le16(mode->crtc_vsync_end - mode->crtc_vsync_start);

	args.ucOverscanRight = radeon_crtc->h_border;
	args.ucOverscanLeft = radeon_crtc->h_border;
	args.ucOverscanBottom = radeon_crtc->v_border;
	args.ucOverscanTop = radeon_crtc->v_border;

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
	args.ucCRTC = radeon_crtc->crtc_id;

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

static void atombios_disable_ss(struct radeon_device *rdev, int pll_id)
{
	u32 ss_cntl;

	if (ASIC_IS_DCE4(rdev)) {
		switch (pll_id) {
		case ATOM_PPLL1:
			ss_cntl = RREG32(EVERGREEN_P1PLL_SS_CNTL);
			ss_cntl &= ~EVERGREEN_PxPLL_SS_EN;
			WREG32(EVERGREEN_P1PLL_SS_CNTL, ss_cntl);
			break;
		case ATOM_PPLL2:
			ss_cntl = RREG32(EVERGREEN_P2PLL_SS_CNTL);
			ss_cntl &= ~EVERGREEN_PxPLL_SS_EN;
			WREG32(EVERGREEN_P2PLL_SS_CNTL, ss_cntl);
			break;
		case ATOM_DCPLL:
		case ATOM_PPLL_INVALID:
			return;
		}
	} else if (ASIC_IS_AVIVO(rdev)) {
		switch (pll_id) {
		case ATOM_PPLL1:
			ss_cntl = RREG32(AVIVO_P1PLL_INT_SS_CNTL);
			ss_cntl &= ~1;
			WREG32(AVIVO_P1PLL_INT_SS_CNTL, ss_cntl);
			break;
		case ATOM_PPLL2:
			ss_cntl = RREG32(AVIVO_P2PLL_INT_SS_CNTL);
			ss_cntl &= ~1;
			WREG32(AVIVO_P2PLL_INT_SS_CNTL, ss_cntl);
			break;
		case ATOM_DCPLL:
		case ATOM_PPLL_INVALID:
			return;
		}
	}
}


union atom_enable_ss {
	ENABLE_LVDS_SS_PARAMETERS lvds_ss;
	ENABLE_LVDS_SS_PARAMETERS_V2 lvds_ss_2;
	ENABLE_SPREAD_SPECTRUM_ON_PPLL_PS_ALLOCATION v1;
	ENABLE_SPREAD_SPECTRUM_ON_PPLL_V2 v2;
	ENABLE_SPREAD_SPECTRUM_ON_PPLL_V3 v3;
};

static void atombios_crtc_program_ss(struct radeon_device *rdev,
				     int enable,
				     int pll_id,
				     struct radeon_atom_ss *ss)
{
	int index = GetIndexIntoMasterTable(COMMAND, EnableSpreadSpectrumOnPPLL);
	union atom_enable_ss args;

	memset(&args, 0, sizeof(args));

	if (ASIC_IS_DCE5(rdev)) {
		args.v3.usSpreadSpectrumAmountFrac = cpu_to_le16(0);
		args.v3.ucSpreadSpectrumType = ss->type & ATOM_SS_CENTRE_SPREAD_MODE_MASK;
		switch (pll_id) {
		case ATOM_PPLL1:
			args.v3.ucSpreadSpectrumType |= ATOM_PPLL_SS_TYPE_V3_P1PLL;
			args.v3.usSpreadSpectrumAmount = cpu_to_le16(ss->amount);
			args.v3.usSpreadSpectrumStep = cpu_to_le16(ss->step);
			break;
		case ATOM_PPLL2:
			args.v3.ucSpreadSpectrumType |= ATOM_PPLL_SS_TYPE_V3_P2PLL;
			args.v3.usSpreadSpectrumAmount = cpu_to_le16(ss->amount);
			args.v3.usSpreadSpectrumStep = cpu_to_le16(ss->step);
			break;
		case ATOM_DCPLL:
			args.v3.ucSpreadSpectrumType |= ATOM_PPLL_SS_TYPE_V3_DCPLL;
			args.v3.usSpreadSpectrumAmount = cpu_to_le16(0);
			args.v3.usSpreadSpectrumStep = cpu_to_le16(0);
			break;
		case ATOM_PPLL_INVALID:
			return;
		}
		args.v3.ucEnable = enable;
		if ((ss->percentage == 0) || (ss->type & ATOM_EXTERNAL_SS_MASK) || ASIC_IS_DCE61(rdev))
			args.v3.ucEnable = ATOM_DISABLE;
	} else if (ASIC_IS_DCE4(rdev)) {
		args.v2.usSpreadSpectrumPercentage = cpu_to_le16(ss->percentage);
		args.v2.ucSpreadSpectrumType = ss->type & ATOM_SS_CENTRE_SPREAD_MODE_MASK;
		switch (pll_id) {
		case ATOM_PPLL1:
			args.v2.ucSpreadSpectrumType |= ATOM_PPLL_SS_TYPE_V2_P1PLL;
			args.v2.usSpreadSpectrumAmount = cpu_to_le16(ss->amount);
			args.v2.usSpreadSpectrumStep = cpu_to_le16(ss->step);
			break;
		case ATOM_PPLL2:
			args.v2.ucSpreadSpectrumType |= ATOM_PPLL_SS_TYPE_V2_P2PLL;
			args.v2.usSpreadSpectrumAmount = cpu_to_le16(ss->amount);
			args.v2.usSpreadSpectrumStep = cpu_to_le16(ss->step);
			break;
		case ATOM_DCPLL:
			args.v2.ucSpreadSpectrumType |= ATOM_PPLL_SS_TYPE_V2_DCPLL;
			args.v2.usSpreadSpectrumAmount = cpu_to_le16(0);
			args.v2.usSpreadSpectrumStep = cpu_to_le16(0);
			break;
		case ATOM_PPLL_INVALID:
			return;
		}
		args.v2.ucEnable = enable;
		if ((ss->percentage == 0) || (ss->type & ATOM_EXTERNAL_SS_MASK) || ASIC_IS_DCE41(rdev))
			args.v2.ucEnable = ATOM_DISABLE;
	} else if (ASIC_IS_DCE3(rdev)) {
		args.v1.usSpreadSpectrumPercentage = cpu_to_le16(ss->percentage);
		args.v1.ucSpreadSpectrumType = ss->type & ATOM_SS_CENTRE_SPREAD_MODE_MASK;
		args.v1.ucSpreadSpectrumStep = ss->step;
		args.v1.ucSpreadSpectrumDelay = ss->delay;
		args.v1.ucSpreadSpectrumRange = ss->range;
		args.v1.ucPpll = pll_id;
		args.v1.ucEnable = enable;
	} else if (ASIC_IS_AVIVO(rdev)) {
		if ((enable == ATOM_DISABLE) || (ss->percentage == 0) ||
		    (ss->type & ATOM_EXTERNAL_SS_MASK)) {
			atombios_disable_ss(rdev, pll_id);
			return;
		}
		args.lvds_ss_2.usSpreadSpectrumPercentage = cpu_to_le16(ss->percentage);
		args.lvds_ss_2.ucSpreadSpectrumType = ss->type & ATOM_SS_CENTRE_SPREAD_MODE_MASK;
		args.lvds_ss_2.ucSpreadSpectrumStep = ss->step;
		args.lvds_ss_2.ucSpreadSpectrumDelay = ss->delay;
		args.lvds_ss_2.ucSpreadSpectrumRange = ss->range;
		args.lvds_ss_2.ucEnable = enable;
	} else {
		if ((enable == ATOM_DISABLE) || (ss->percentage == 0) ||
		    (ss->type & ATOM_EXTERNAL_SS_MASK)) {
			atombios_disable_ss(rdev, pll_id);
			return;
		}
		args.lvds_ss.usSpreadSpectrumPercentage = cpu_to_le16(ss->percentage);
		args.lvds_ss.ucSpreadSpectrumType = ss->type & ATOM_SS_CENTRE_SPREAD_MODE_MASK;
		args.lvds_ss.ucSpreadSpectrumStepSize_Delay = (ss->step & 3) << 2;
		args.lvds_ss.ucSpreadSpectrumStepSize_Delay |= (ss->delay & 7) << 4;
		args.lvds_ss.ucEnable = enable;
	}
	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

union adjust_pixel_clock {
	ADJUST_DISPLAY_PLL_PS_ALLOCATION v1;
	ADJUST_DISPLAY_PLL_PS_ALLOCATION_V3 v3;
};

static u32 atombios_adjust_pll(struct drm_crtc *crtc,
			       struct drm_display_mode *mode,
			       struct radeon_pll *pll,
			       bool ss_enabled,
			       struct radeon_atom_ss *ss)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct drm_encoder *encoder = NULL;
	struct radeon_encoder *radeon_encoder = NULL;
	struct drm_connector *connector = NULL;
	u32 adjusted_clock = mode->clock;
	int encoder_mode = 0;
	u32 dp_clock = mode->clock;
	int bpc = 8;
	bool is_duallink = false;

	/* reset the pll flags */
	pll->flags = 0;

	if (ASIC_IS_AVIVO(rdev)) {
		if ((rdev->family == CHIP_RS600) ||
		    (rdev->family == CHIP_RS690) ||
		    (rdev->family == CHIP_RS740))
			pll->flags |= (/*RADEON_PLL_USE_FRAC_FB_DIV |*/
				       RADEON_PLL_PREFER_CLOSEST_LOWER);

		if (ASIC_IS_DCE32(rdev) && mode->clock > 200000)	/* range limits??? */
			pll->flags |= RADEON_PLL_PREFER_HIGH_FB_DIV;
		else
			pll->flags |= RADEON_PLL_PREFER_LOW_REF_DIV;

		if (rdev->family < CHIP_RV770)
			pll->flags |= RADEON_PLL_PREFER_MINM_OVER_MAXP;
		/* use frac fb div on APUs */
		if (ASIC_IS_DCE41(rdev) || ASIC_IS_DCE61(rdev))
			pll->flags |= RADEON_PLL_USE_FRAC_FB_DIV;
	} else {
		pll->flags |= RADEON_PLL_LEGACY;

		if (mode->clock > 200000)	/* range limits??? */
			pll->flags |= RADEON_PLL_PREFER_HIGH_FB_DIV;
		else
			pll->flags |= RADEON_PLL_PREFER_LOW_REF_DIV;
	}

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (encoder->crtc == crtc) {
			radeon_encoder = to_radeon_encoder(encoder);
			connector = radeon_get_connector_for_encoder(encoder);
			/* if (connector && connector->display_info.bpc)
				bpc = connector->display_info.bpc; */
			encoder_mode = atombios_get_encoder_mode(encoder);
			is_duallink = radeon_dig_monitor_is_duallink(encoder, mode->clock);
			if ((radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT | ATOM_DEVICE_DFP_SUPPORT)) ||
			    (radeon_encoder_get_dp_bridge_encoder_id(encoder) != ENCODER_OBJECT_ID_NONE)) {
				if (connector) {
					struct radeon_connector *radeon_connector = to_radeon_connector(connector);
					struct radeon_connector_atom_dig *dig_connector =
						radeon_connector->con_priv;

					dp_clock = dig_connector->dp_clock;
				}
			}

			/* use recommended ref_div for ss */
			if (radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) {
				if (ss_enabled) {
					if (ss->refdiv) {
						pll->flags |= RADEON_PLL_USE_REF_DIV;
						pll->reference_div = ss->refdiv;
						if (ASIC_IS_AVIVO(rdev))
							pll->flags |= RADEON_PLL_USE_FRAC_FB_DIV;
					}
				}
			}

			if (ASIC_IS_AVIVO(rdev)) {
				/* DVO wants 2x pixel clock if the DVO chip is in 12 bit mode */
				if (radeon_encoder->encoder_id == ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1)
					adjusted_clock = mode->clock * 2;
				if (radeon_encoder->active_device & (ATOM_DEVICE_TV_SUPPORT))
					pll->flags |= RADEON_PLL_PREFER_CLOSEST_LOWER;
				if (radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT))
					pll->flags |= RADEON_PLL_IS_LCD;
			} else {
				if (encoder->encoder_type != DRM_MODE_ENCODER_DAC)
					pll->flags |= RADEON_PLL_NO_ODD_POST_DIV;
				if (encoder->encoder_type == DRM_MODE_ENCODER_LVDS)
					pll->flags |= RADEON_PLL_USE_REF_DIV;
			}
			break;
		}
	}

	/* DCE3+ has an AdjustDisplayPll that will adjust the pixel clock
	 * accordingly based on the encoder/transmitter to work around
	 * special hw requirements.
	 */
	if (ASIC_IS_DCE3(rdev)) {
		union adjust_pixel_clock args;
		u8 frev, crev;
		int index;

		index = GetIndexIntoMasterTable(COMMAND, AdjustDisplayPll);
		if (!atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev,
					   &crev))
			return adjusted_clock;

		memset(&args, 0, sizeof(args));

		switch (frev) {
		case 1:
			switch (crev) {
			case 1:
			case 2:
				args.v1.usPixelClock = cpu_to_le16(mode->clock / 10);
				args.v1.ucTransmitterID = radeon_encoder->encoder_id;
				args.v1.ucEncodeMode = encoder_mode;
				if (ss_enabled && ss->percentage)
					args.v1.ucConfig |=
						ADJUST_DISPLAY_CONFIG_SS_ENABLE;

				atom_execute_table(rdev->mode_info.atom_context,
						   index, (uint32_t *)&args);
				adjusted_clock = le16_to_cpu(args.v1.usPixelClock) * 10;
				break;
			case 3:
				args.v3.sInput.usPixelClock = cpu_to_le16(mode->clock / 10);
				args.v3.sInput.ucTransmitterID = radeon_encoder->encoder_id;
				args.v3.sInput.ucEncodeMode = encoder_mode;
				args.v3.sInput.ucDispPllConfig = 0;
				if (ss_enabled && ss->percentage)
					args.v3.sInput.ucDispPllConfig |=
						DISPPLL_CONFIG_SS_ENABLE;
				if (ENCODER_MODE_IS_DP(encoder_mode)) {
					args.v3.sInput.ucDispPllConfig |=
						DISPPLL_CONFIG_COHERENT_MODE;
					/* 16200 or 27000 */
					args.v3.sInput.usPixelClock = cpu_to_le16(dp_clock / 10);
				} else if (radeon_encoder->devices & (ATOM_DEVICE_DFP_SUPPORT)) {
					struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
					if (encoder_mode == ATOM_ENCODER_MODE_HDMI)
						/* deep color support */
						args.v3.sInput.usPixelClock =
							cpu_to_le16((mode->clock * bpc / 8) / 10);
					if (dig->coherent_mode)
						args.v3.sInput.ucDispPllConfig |=
							DISPPLL_CONFIG_COHERENT_MODE;
					if (is_duallink)
						args.v3.sInput.ucDispPllConfig |=
							DISPPLL_CONFIG_DUAL_LINK;
				}
				if (radeon_encoder_get_dp_bridge_encoder_id(encoder) !=
				    ENCODER_OBJECT_ID_NONE)
					args.v3.sInput.ucExtTransmitterID =
						radeon_encoder_get_dp_bridge_encoder_id(encoder);
				else
					args.v3.sInput.ucExtTransmitterID = 0;

				atom_execute_table(rdev->mode_info.atom_context,
						   index, (uint32_t *)&args);
				adjusted_clock = le32_to_cpu(args.v3.sOutput.ulDispPllFreq) * 10;
				if (args.v3.sOutput.ucRefDiv) {
					pll->flags |= RADEON_PLL_USE_FRAC_FB_DIV;
					pll->flags |= RADEON_PLL_USE_REF_DIV;
					pll->reference_div = args.v3.sOutput.ucRefDiv;
				}
				if (args.v3.sOutput.ucPostDiv) {
					pll->flags |= RADEON_PLL_USE_FRAC_FB_DIV;
					pll->flags |= RADEON_PLL_USE_POST_DIV;
					pll->post_div = args.v3.sOutput.ucPostDiv;
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
};

/* on DCE5, make sure the voltage is high enough to support the
 * required disp clk.
 */
static void atombios_crtc_set_disp_eng_pll(struct radeon_device *rdev,
				    u32 dispclk)
{
	u8 frev, crev;
	int index;
	union set_pixel_clock args;

	memset(&args, 0, sizeof(args));

	index = GetIndexIntoMasterTable(COMMAND, SetPixelClock);
	if (!atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev,
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
			if (ASIC_IS_DCE61(rdev))
				args.v6.ucPpll = ATOM_EXT_PLL1;
			else if (ASIC_IS_DCE6(rdev))
				args.v6.ucPpll = ATOM_PPLL0;
			else
				args.v6.ucPpll = ATOM_DCPLL;
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
	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

static void atombios_crtc_program_pll(struct drm_crtc *crtc,
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
				      struct radeon_atom_ss *ss)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	u8 frev, crev;
	int index = GetIndexIntoMasterTable(COMMAND, SetPixelClock);
	union set_pixel_clock args;

	memset(&args, 0, sizeof(args));

	if (!atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev,
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
			args.v3.ucMiscInfo = (pll_id << 2);
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
			if (ss_enabled && (ss->type & ATOM_EXTERNAL_SS_MASK))
				args.v5.ucMiscInfo |= PIXEL_CLOCK_V5_MISC_REF_DIV_SRC;
			switch (bpc) {
			case 8:
			default:
				args.v5.ucMiscInfo |= PIXEL_CLOCK_V5_MISC_HDMI_24BPP;
				break;
			case 10:
				args.v5.ucMiscInfo |= PIXEL_CLOCK_V5_MISC_HDMI_30BPP;
				break;
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
			if (ss_enabled && (ss->type & ATOM_EXTERNAL_SS_MASK))
				args.v6.ucMiscInfo |= PIXEL_CLOCK_V6_MISC_REF_DIV_SRC;
			switch (bpc) {
			case 8:
			default:
				args.v6.ucMiscInfo |= PIXEL_CLOCK_V6_MISC_HDMI_24BPP;
				break;
			case 10:
				args.v6.ucMiscInfo |= PIXEL_CLOCK_V6_MISC_HDMI_30BPP;
				break;
			case 12:
				args.v6.ucMiscInfo |= PIXEL_CLOCK_V6_MISC_HDMI_36BPP;
				break;
			case 16:
				args.v6.ucMiscInfo |= PIXEL_CLOCK_V6_MISC_HDMI_48BPP;
				break;
			}
			args.v6.ucTransmitterID = encoder_id;
			args.v6.ucEncoderMode = encoder_mode;
			args.v6.ucPpll = pll_id;
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

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

static void atombios_crtc_set_pll(struct drm_crtc *crtc, struct drm_display_mode *mode)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct drm_encoder *encoder = NULL;
	struct radeon_encoder *radeon_encoder = NULL;
	u32 pll_clock = mode->clock;
	u32 ref_div = 0, fb_div = 0, frac_fb_div = 0, post_div = 0;
	struct radeon_pll *pll;
	u32 adjusted_clock;
	int encoder_mode = 0;
	struct radeon_atom_ss ss;
	bool ss_enabled = false;
	int bpc = 8;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (encoder->crtc == crtc) {
			radeon_encoder = to_radeon_encoder(encoder);
			encoder_mode = atombios_get_encoder_mode(encoder);
			break;
		}
	}

	if (!radeon_encoder)
		return;

	switch (radeon_crtc->pll_id) {
	case ATOM_PPLL1:
		pll = &rdev->clock.p1pll;
		break;
	case ATOM_PPLL2:
		pll = &rdev->clock.p2pll;
		break;
	case ATOM_DCPLL:
	case ATOM_PPLL_INVALID:
	default:
		pll = &rdev->clock.dcpll;
		break;
	}

	if ((radeon_encoder->active_device & (ATOM_DEVICE_LCD_SUPPORT | ATOM_DEVICE_DFP_SUPPORT)) ||
	    (radeon_encoder_get_dp_bridge_encoder_id(encoder) != ENCODER_OBJECT_ID_NONE)) {
		struct radeon_encoder_atom_dig *dig = radeon_encoder->enc_priv;
		struct drm_connector *connector =
			radeon_get_connector_for_encoder(encoder);
		struct radeon_connector *radeon_connector =
			to_radeon_connector(connector);
		struct radeon_connector_atom_dig *dig_connector =
			radeon_connector->con_priv;
		int dp_clock;

		/* if (connector->display_info.bpc)
			bpc = connector->display_info.bpc; */

		switch (encoder_mode) {
		case ATOM_ENCODER_MODE_DP_MST:
		case ATOM_ENCODER_MODE_DP:
			/* DP/eDP */
			dp_clock = dig_connector->dp_clock / 10;
			if (ASIC_IS_DCE4(rdev))
				ss_enabled =
					radeon_atombios_get_asic_ss_info(rdev, &ss,
									 ASIC_INTERNAL_SS_ON_DP,
									 dp_clock);
			else {
				if (dp_clock == 16200) {
					ss_enabled =
						radeon_atombios_get_ppll_ss_info(rdev, &ss,
										 ATOM_DP_SS_ID2);
					if (!ss_enabled)
						ss_enabled =
							radeon_atombios_get_ppll_ss_info(rdev, &ss,
											 ATOM_DP_SS_ID1);
				} else
					ss_enabled =
						radeon_atombios_get_ppll_ss_info(rdev, &ss,
										 ATOM_DP_SS_ID1);
			}
			break;
		case ATOM_ENCODER_MODE_LVDS:
			if (ASIC_IS_DCE4(rdev))
				ss_enabled = radeon_atombios_get_asic_ss_info(rdev, &ss,
									      dig->lcd_ss_id,
									      mode->clock / 10);
			else
				ss_enabled = radeon_atombios_get_ppll_ss_info(rdev, &ss,
									      dig->lcd_ss_id);
			break;
		case ATOM_ENCODER_MODE_DVI:
			if (ASIC_IS_DCE4(rdev))
				ss_enabled =
					radeon_atombios_get_asic_ss_info(rdev, &ss,
									 ASIC_INTERNAL_SS_ON_TMDS,
									 mode->clock / 10);
			break;
		case ATOM_ENCODER_MODE_HDMI:
			if (ASIC_IS_DCE4(rdev))
				ss_enabled =
					radeon_atombios_get_asic_ss_info(rdev, &ss,
									 ASIC_INTERNAL_SS_ON_HDMI,
									 mode->clock / 10);
			break;
		default:
			break;
		}
	}

	/* adjust pixel clock as needed */
	adjusted_clock = atombios_adjust_pll(crtc, mode, pll, ss_enabled, &ss);

	if (radeon_encoder->active_device & (ATOM_DEVICE_TV_SUPPORT))
		/* TV seems to prefer the legacy algo on some boards */
		radeon_compute_pll_legacy(pll, adjusted_clock, &pll_clock, &fb_div, &frac_fb_div,
					  &ref_div, &post_div);
	else if (ASIC_IS_AVIVO(rdev))
		radeon_compute_pll_avivo(pll, adjusted_clock, &pll_clock, &fb_div, &frac_fb_div,
					 &ref_div, &post_div);
	else
		radeon_compute_pll_legacy(pll, adjusted_clock, &pll_clock, &fb_div, &frac_fb_div,
					  &ref_div, &post_div);

	atombios_crtc_program_ss(rdev, ATOM_DISABLE, radeon_crtc->pll_id, &ss);

	atombios_crtc_program_pll(crtc, radeon_crtc->crtc_id, radeon_crtc->pll_id,
				  encoder_mode, radeon_encoder->encoder_id, mode->clock,
				  ref_div, fb_div, frac_fb_div, post_div, bpc, ss_enabled, &ss);

	if (ss_enabled) {
		/* calculate ss amount and step size */
		if (ASIC_IS_DCE4(rdev)) {
			u32 step_size;
			u32 amount = (((fb_div * 10) + frac_fb_div) * ss.percentage) / 10000;
			ss.amount = (amount / 10) & ATOM_PPLL_SS_AMOUNT_V2_FBDIV_MASK;
			ss.amount |= ((amount - (amount / 10)) << ATOM_PPLL_SS_AMOUNT_V2_NFRAC_SHIFT) &
				ATOM_PPLL_SS_AMOUNT_V2_NFRAC_MASK;
			if (ss.type & ATOM_PPLL_SS_TYPE_V2_CENTRE_SPREAD)
				step_size = (4 * amount * ref_div * (ss.rate * 2048)) /
					(125 * 25 * pll->reference_freq / 100);
			else
				step_size = (2 * amount * ref_div * (ss.rate * 2048)) /
					(125 * 25 * pll->reference_freq / 100);
			ss.step = step_size;
		}

		atombios_crtc_program_ss(rdev, ATOM_ENABLE, radeon_crtc->pll_id, &ss);
	}
}

static int dce4_crtc_do_set_base(struct drm_crtc *crtc,
				 struct drm_framebuffer *fb,
				 int x, int y, int atomic)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_framebuffer *radeon_fb;
	struct drm_framebuffer *target_fb;
	struct drm_gem_object *obj;
	struct radeon_bo *rbo;
	uint64_t fb_location;
	uint32_t fb_format, fb_pitch_pixels, tiling_flags;
	unsigned bankw, bankh, mtaspect, tile_split;
	u32 fb_swap = EVERGREEN_GRPH_ENDIAN_SWAP(EVERGREEN_GRPH_ENDIAN_NONE);
	u32 tmp, viewport_w, viewport_h;
	int r;

	/* no fb bound */
	if (!atomic && !crtc->fb) {
		DRM_DEBUG_KMS("No FB bound\n");
		return 0;
	}

	if (atomic) {
		radeon_fb = to_radeon_framebuffer(fb);
		target_fb = fb;
	}
	else {
		radeon_fb = to_radeon_framebuffer(crtc->fb);
		target_fb = crtc->fb;
	}

	/* If atomic, assume fb object is pinned & idle & fenced and
	 * just update base pointers
	 */
	obj = radeon_fb->obj;
	rbo = gem_to_radeon_bo(obj);
	r = radeon_bo_reserve(rbo, false);
	if (unlikely(r != 0))
		return r;

	if (atomic)
		fb_location = radeon_bo_gpu_offset(rbo);
	else {
		r = radeon_bo_pin(rbo, RADEON_GEM_DOMAIN_VRAM, &fb_location);
		if (unlikely(r != 0)) {
			radeon_bo_unreserve(rbo);
			return -EINVAL;
		}
	}

	radeon_bo_get_tiling_flags(rbo, &tiling_flags, NULL);
	radeon_bo_unreserve(rbo);

	switch (target_fb->bits_per_pixel) {
	case 8:
		fb_format = (EVERGREEN_GRPH_DEPTH(EVERGREEN_GRPH_DEPTH_8BPP) |
			     EVERGREEN_GRPH_FORMAT(EVERGREEN_GRPH_FORMAT_INDEXED));
		break;
	case 15:
		fb_format = (EVERGREEN_GRPH_DEPTH(EVERGREEN_GRPH_DEPTH_16BPP) |
			     EVERGREEN_GRPH_FORMAT(EVERGREEN_GRPH_FORMAT_ARGB1555));
		break;
	case 16:
		fb_format = (EVERGREEN_GRPH_DEPTH(EVERGREEN_GRPH_DEPTH_16BPP) |
			     EVERGREEN_GRPH_FORMAT(EVERGREEN_GRPH_FORMAT_ARGB565));
#ifdef __BIG_ENDIAN
		fb_swap = EVERGREEN_GRPH_ENDIAN_SWAP(EVERGREEN_GRPH_ENDIAN_8IN16);
#endif
		break;
	case 24:
	case 32:
		fb_format = (EVERGREEN_GRPH_DEPTH(EVERGREEN_GRPH_DEPTH_32BPP) |
			     EVERGREEN_GRPH_FORMAT(EVERGREEN_GRPH_FORMAT_ARGB8888));
#ifdef __BIG_ENDIAN
		fb_swap = EVERGREEN_GRPH_ENDIAN_SWAP(EVERGREEN_GRPH_ENDIAN_8IN32);
#endif
		break;
	default:
		DRM_ERROR("Unsupported screen depth %d\n",
			  target_fb->bits_per_pixel);
		return -EINVAL;
	}

	if (tiling_flags & RADEON_TILING_MACRO) {
		if (rdev->family >= CHIP_CAYMAN)
			tmp = rdev->config.cayman.tile_config;
		else
			tmp = rdev->config.evergreen.tile_config;

		switch ((tmp & 0xf0) >> 4) {
		case 0: /* 4 banks */
			fb_format |= EVERGREEN_GRPH_NUM_BANKS(EVERGREEN_ADDR_SURF_4_BANK);
			break;
		case 1: /* 8 banks */
		default:
			fb_format |= EVERGREEN_GRPH_NUM_BANKS(EVERGREEN_ADDR_SURF_8_BANK);
			break;
		case 2: /* 16 banks */
			fb_format |= EVERGREEN_GRPH_NUM_BANKS(EVERGREEN_ADDR_SURF_16_BANK);
			break;
		}

		fb_format |= EVERGREEN_GRPH_ARRAY_MODE(EVERGREEN_GRPH_ARRAY_2D_TILED_THIN1);

		evergreen_tiling_fields(tiling_flags, &bankw, &bankh, &mtaspect, &tile_split);
		fb_format |= EVERGREEN_GRPH_TILE_SPLIT(tile_split);
		fb_format |= EVERGREEN_GRPH_BANK_WIDTH(bankw);
		fb_format |= EVERGREEN_GRPH_BANK_HEIGHT(bankh);
		fb_format |= EVERGREEN_GRPH_MACRO_TILE_ASPECT(mtaspect);
	} else if (tiling_flags & RADEON_TILING_MICRO)
		fb_format |= EVERGREEN_GRPH_ARRAY_MODE(EVERGREEN_GRPH_ARRAY_1D_TILED_THIN1);

	switch (radeon_crtc->crtc_id) {
	case 0:
		WREG32(AVIVO_D1VGA_CONTROL, 0);
		break;
	case 1:
		WREG32(AVIVO_D2VGA_CONTROL, 0);
		break;
	case 2:
		WREG32(EVERGREEN_D3VGA_CONTROL, 0);
		break;
	case 3:
		WREG32(EVERGREEN_D4VGA_CONTROL, 0);
		break;
	case 4:
		WREG32(EVERGREEN_D5VGA_CONTROL, 0);
		break;
	case 5:
		WREG32(EVERGREEN_D6VGA_CONTROL, 0);
		break;
	default:
		break;
	}

	WREG32(EVERGREEN_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH + radeon_crtc->crtc_offset,
	       upper_32_bits(fb_location));
	WREG32(EVERGREEN_GRPH_SECONDARY_SURFACE_ADDRESS_HIGH + radeon_crtc->crtc_offset,
	       upper_32_bits(fb_location));
	WREG32(EVERGREEN_GRPH_PRIMARY_SURFACE_ADDRESS + radeon_crtc->crtc_offset,
	       (u32)fb_location & EVERGREEN_GRPH_SURFACE_ADDRESS_MASK);
	WREG32(EVERGREEN_GRPH_SECONDARY_SURFACE_ADDRESS + radeon_crtc->crtc_offset,
	       (u32) fb_location & EVERGREEN_GRPH_SURFACE_ADDRESS_MASK);
	WREG32(EVERGREEN_GRPH_CONTROL + radeon_crtc->crtc_offset, fb_format);
	WREG32(EVERGREEN_GRPH_SWAP_CONTROL + radeon_crtc->crtc_offset, fb_swap);

	WREG32(EVERGREEN_GRPH_SURFACE_OFFSET_X + radeon_crtc->crtc_offset, 0);
	WREG32(EVERGREEN_GRPH_SURFACE_OFFSET_Y + radeon_crtc->crtc_offset, 0);
	WREG32(EVERGREEN_GRPH_X_START + radeon_crtc->crtc_offset, 0);
	WREG32(EVERGREEN_GRPH_Y_START + radeon_crtc->crtc_offset, 0);
	WREG32(EVERGREEN_GRPH_X_END + radeon_crtc->crtc_offset, target_fb->width);
	WREG32(EVERGREEN_GRPH_Y_END + radeon_crtc->crtc_offset, target_fb->height);

	fb_pitch_pixels = target_fb->pitches[0] / (target_fb->bits_per_pixel / 8);
	WREG32(EVERGREEN_GRPH_PITCH + radeon_crtc->crtc_offset, fb_pitch_pixels);
	WREG32(EVERGREEN_GRPH_ENABLE + radeon_crtc->crtc_offset, 1);

	WREG32(EVERGREEN_DESKTOP_HEIGHT + radeon_crtc->crtc_offset,
	       target_fb->height);
	x &= ~3;
	y &= ~1;
	WREG32(EVERGREEN_VIEWPORT_START + radeon_crtc->crtc_offset,
	       (x << 16) | y);
	viewport_w = crtc->mode.hdisplay;
	viewport_h = (crtc->mode.vdisplay + 1) & ~1;
	WREG32(EVERGREEN_VIEWPORT_SIZE + radeon_crtc->crtc_offset,
	       (viewport_w << 16) | viewport_h);

	/* pageflip setup */
	/* make sure flip is at vb rather than hb */
	tmp = RREG32(EVERGREEN_GRPH_FLIP_CONTROL + radeon_crtc->crtc_offset);
	tmp &= ~EVERGREEN_GRPH_SURFACE_UPDATE_H_RETRACE_EN;
	WREG32(EVERGREEN_GRPH_FLIP_CONTROL + radeon_crtc->crtc_offset, tmp);

	/* set pageflip to happen anywhere in vblank interval */
	WREG32(EVERGREEN_MASTER_UPDATE_MODE + radeon_crtc->crtc_offset, 0);

	if (!atomic && fb && fb != crtc->fb) {
		radeon_fb = to_radeon_framebuffer(fb);
		rbo = gem_to_radeon_bo(radeon_fb->obj);
		r = radeon_bo_reserve(rbo, false);
		if (unlikely(r != 0))
			return r;
		radeon_bo_unpin(rbo);
		radeon_bo_unreserve(rbo);
	}

	/* Bytes per pixel may have changed */
	radeon_bandwidth_update(rdev);

	return 0;
}

static int avivo_crtc_do_set_base(struct drm_crtc *crtc,
				  struct drm_framebuffer *fb,
				  int x, int y, int atomic)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_framebuffer *radeon_fb;
	struct drm_gem_object *obj;
	struct radeon_bo *rbo;
	struct drm_framebuffer *target_fb;
	uint64_t fb_location;
	uint32_t fb_format, fb_pitch_pixels, tiling_flags;
	u32 fb_swap = R600_D1GRPH_SWAP_ENDIAN_NONE;
	u32 tmp, viewport_w, viewport_h;
	int r;

	/* no fb bound */
	if (!atomic && !crtc->fb) {
		DRM_DEBUG_KMS("No FB bound\n");
		return 0;
	}

	if (atomic) {
		radeon_fb = to_radeon_framebuffer(fb);
		target_fb = fb;
	}
	else {
		radeon_fb = to_radeon_framebuffer(crtc->fb);
		target_fb = crtc->fb;
	}

	obj = radeon_fb->obj;
	rbo = gem_to_radeon_bo(obj);
	r = radeon_bo_reserve(rbo, false);
	if (unlikely(r != 0))
		return r;

	/* If atomic, assume fb object is pinned & idle & fenced and
	 * just update base pointers
	 */
	if (atomic)
		fb_location = radeon_bo_gpu_offset(rbo);
	else {
		r = radeon_bo_pin(rbo, RADEON_GEM_DOMAIN_VRAM, &fb_location);
		if (unlikely(r != 0)) {
			radeon_bo_unreserve(rbo);
			return -EINVAL;
		}
	}
	radeon_bo_get_tiling_flags(rbo, &tiling_flags, NULL);
	radeon_bo_unreserve(rbo);

	switch (target_fb->bits_per_pixel) {
	case 8:
		fb_format =
		    AVIVO_D1GRPH_CONTROL_DEPTH_8BPP |
		    AVIVO_D1GRPH_CONTROL_8BPP_INDEXED;
		break;
	case 15:
		fb_format =
		    AVIVO_D1GRPH_CONTROL_DEPTH_16BPP |
		    AVIVO_D1GRPH_CONTROL_16BPP_ARGB1555;
		break;
	case 16:
		fb_format =
		    AVIVO_D1GRPH_CONTROL_DEPTH_16BPP |
		    AVIVO_D1GRPH_CONTROL_16BPP_RGB565;
#ifdef __BIG_ENDIAN
		fb_swap = R600_D1GRPH_SWAP_ENDIAN_16BIT;
#endif
		break;
	case 24:
	case 32:
		fb_format =
		    AVIVO_D1GRPH_CONTROL_DEPTH_32BPP |
		    AVIVO_D1GRPH_CONTROL_32BPP_ARGB8888;
#ifdef __BIG_ENDIAN
		fb_swap = R600_D1GRPH_SWAP_ENDIAN_32BIT;
#endif
		break;
	default:
		DRM_ERROR("Unsupported screen depth %d\n",
			  target_fb->bits_per_pixel);
		return -EINVAL;
	}

	if (rdev->family >= CHIP_R600) {
		if (tiling_flags & RADEON_TILING_MACRO)
			fb_format |= R600_D1GRPH_ARRAY_MODE_2D_TILED_THIN1;
		else if (tiling_flags & RADEON_TILING_MICRO)
			fb_format |= R600_D1GRPH_ARRAY_MODE_1D_TILED_THIN1;
	} else {
		if (tiling_flags & RADEON_TILING_MACRO)
			fb_format |= AVIVO_D1GRPH_MACRO_ADDRESS_MODE;

		if (tiling_flags & RADEON_TILING_MICRO)
			fb_format |= AVIVO_D1GRPH_TILED;
	}

	if (radeon_crtc->crtc_id == 0)
		WREG32(AVIVO_D1VGA_CONTROL, 0);
	else
		WREG32(AVIVO_D2VGA_CONTROL, 0);

	if (rdev->family >= CHIP_RV770) {
		if (radeon_crtc->crtc_id) {
			WREG32(R700_D2GRPH_PRIMARY_SURFACE_ADDRESS_HIGH, upper_32_bits(fb_location));
			WREG32(R700_D2GRPH_SECONDARY_SURFACE_ADDRESS_HIGH, upper_32_bits(fb_location));
		} else {
			WREG32(R700_D1GRPH_PRIMARY_SURFACE_ADDRESS_HIGH, upper_32_bits(fb_location));
			WREG32(R700_D1GRPH_SECONDARY_SURFACE_ADDRESS_HIGH, upper_32_bits(fb_location));
		}
	}
	WREG32(AVIVO_D1GRPH_PRIMARY_SURFACE_ADDRESS + radeon_crtc->crtc_offset,
	       (u32) fb_location);
	WREG32(AVIVO_D1GRPH_SECONDARY_SURFACE_ADDRESS +
	       radeon_crtc->crtc_offset, (u32) fb_location);
	WREG32(AVIVO_D1GRPH_CONTROL + radeon_crtc->crtc_offset, fb_format);
	if (rdev->family >= CHIP_R600)
		WREG32(R600_D1GRPH_SWAP_CONTROL + radeon_crtc->crtc_offset, fb_swap);

	WREG32(AVIVO_D1GRPH_SURFACE_OFFSET_X + radeon_crtc->crtc_offset, 0);
	WREG32(AVIVO_D1GRPH_SURFACE_OFFSET_Y + radeon_crtc->crtc_offset, 0);
	WREG32(AVIVO_D1GRPH_X_START + radeon_crtc->crtc_offset, 0);
	WREG32(AVIVO_D1GRPH_Y_START + radeon_crtc->crtc_offset, 0);
	WREG32(AVIVO_D1GRPH_X_END + radeon_crtc->crtc_offset, target_fb->width);
	WREG32(AVIVO_D1GRPH_Y_END + radeon_crtc->crtc_offset, target_fb->height);

	fb_pitch_pixels = target_fb->pitches[0] / (target_fb->bits_per_pixel / 8);
	WREG32(AVIVO_D1GRPH_PITCH + radeon_crtc->crtc_offset, fb_pitch_pixels);
	WREG32(AVIVO_D1GRPH_ENABLE + radeon_crtc->crtc_offset, 1);

	WREG32(AVIVO_D1MODE_DESKTOP_HEIGHT + radeon_crtc->crtc_offset,
	       target_fb->height);
	x &= ~3;
	y &= ~1;
	WREG32(AVIVO_D1MODE_VIEWPORT_START + radeon_crtc->crtc_offset,
	       (x << 16) | y);
	viewport_w = crtc->mode.hdisplay;
	viewport_h = (crtc->mode.vdisplay + 1) & ~1;
	WREG32(AVIVO_D1MODE_VIEWPORT_SIZE + radeon_crtc->crtc_offset,
	       (viewport_w << 16) | viewport_h);

	/* pageflip setup */
	/* make sure flip is at vb rather than hb */
	tmp = RREG32(AVIVO_D1GRPH_FLIP_CONTROL + radeon_crtc->crtc_offset);
	tmp &= ~AVIVO_D1GRPH_SURFACE_UPDATE_H_RETRACE_EN;
	WREG32(AVIVO_D1GRPH_FLIP_CONTROL + radeon_crtc->crtc_offset, tmp);

	/* set pageflip to happen anywhere in vblank interval */
	WREG32(AVIVO_D1MODE_MASTER_UPDATE_MODE + radeon_crtc->crtc_offset, 0);

	if (!atomic && fb && fb != crtc->fb) {
		radeon_fb = to_radeon_framebuffer(fb);
		rbo = gem_to_radeon_bo(radeon_fb->obj);
		r = radeon_bo_reserve(rbo, false);
		if (unlikely(r != 0))
			return r;
		radeon_bo_unpin(rbo);
		radeon_bo_unreserve(rbo);
	}

	/* Bytes per pixel may have changed */
	radeon_bandwidth_update(rdev);

	return 0;
}

int atombios_crtc_set_base(struct drm_crtc *crtc, int x, int y,
			   struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;

	if (ASIC_IS_DCE4(rdev))
		return dce4_crtc_do_set_base(crtc, old_fb, x, y, 0);
	else if (ASIC_IS_AVIVO(rdev))
		return avivo_crtc_do_set_base(crtc, old_fb, x, y, 0);
	else
		return radeon_crtc_do_set_base(crtc, old_fb, x, y, 0);
}

int atombios_crtc_set_base_atomic(struct drm_crtc *crtc,
                                  struct drm_framebuffer *fb,
				  int x, int y, enum mode_set_atomic state)
{
       struct drm_device *dev = crtc->dev;
       struct radeon_device *rdev = dev->dev_private;

	if (ASIC_IS_DCE4(rdev))
		return dce4_crtc_do_set_base(crtc, fb, x, y, 1);
	else if (ASIC_IS_AVIVO(rdev))
		return avivo_crtc_do_set_base(crtc, fb, x, y, 1);
	else
		return radeon_crtc_do_set_base(crtc, fb, x, y, 1);
}

/* properly set additional regs when using atombios */
static void radeon_legacy_atom_fixup(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	u32 disp_merge_cntl;

	switch (radeon_crtc->crtc_id) {
	case 0:
		disp_merge_cntl = RREG32(RADEON_DISP_MERGE_CNTL);
		disp_merge_cntl &= ~RADEON_DISP_RGB_OFFSET_EN;
		WREG32(RADEON_DISP_MERGE_CNTL, disp_merge_cntl);
		break;
	case 1:
		disp_merge_cntl = RREG32(RADEON_DISP2_MERGE_CNTL);
		disp_merge_cntl &= ~RADEON_DISP2_RGB_OFFSET_EN;
		WREG32(RADEON_DISP2_MERGE_CNTL, disp_merge_cntl);
		WREG32(RADEON_FP_H2_SYNC_STRT_WID,   RREG32(RADEON_CRTC2_H_SYNC_STRT_WID));
		WREG32(RADEON_FP_V2_SYNC_STRT_WID,   RREG32(RADEON_CRTC2_V_SYNC_STRT_WID));
		break;
	}
}

static int radeon_atom_pick_pll(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct drm_encoder *test_encoder;
	struct drm_crtc *test_crtc;
	uint32_t pll_in_use = 0;

	if (ASIC_IS_DCE61(rdev)) {
		list_for_each_entry(test_encoder, &dev->mode_config.encoder_list, head) {
			if (test_encoder->crtc && (test_encoder->crtc == crtc)) {
				struct radeon_encoder *test_radeon_encoder =
					to_radeon_encoder(test_encoder);
				struct radeon_encoder_atom_dig *dig =
					test_radeon_encoder->enc_priv;

				if ((test_radeon_encoder->encoder_id ==
				     ENCODER_OBJECT_ID_INTERNAL_UNIPHY) &&
				    (dig->linkb == false)) /* UNIPHY A uses PPLL2 */
					return ATOM_PPLL2;
			}
		}
		/* UNIPHY B/C/D/E/F */
		list_for_each_entry(test_crtc, &dev->mode_config.crtc_list, head) {
			struct radeon_crtc *radeon_test_crtc;

			if (crtc == test_crtc)
				continue;

			radeon_test_crtc = to_radeon_crtc(test_crtc);
			if ((radeon_test_crtc->pll_id == ATOM_PPLL0) ||
			    (radeon_test_crtc->pll_id == ATOM_PPLL1))
				pll_in_use |= (1 << radeon_test_crtc->pll_id);
		}
		if (!(pll_in_use & 4))
			return ATOM_PPLL0;
		return ATOM_PPLL1;
	} else if (ASIC_IS_DCE4(rdev)) {
		list_for_each_entry(test_encoder, &dev->mode_config.encoder_list, head) {
			if (test_encoder->crtc && (test_encoder->crtc == crtc)) {
				/* in DP mode, the DP ref clock can come from PPLL, DCPLL, or ext clock,
				 * depending on the asic:
				 * DCE4: PPLL or ext clock
				 * DCE5: DCPLL or ext clock
				 *
				 * Setting ATOM_PPLL_INVALID will cause SetPixelClock to skip
				 * PPLL/DCPLL programming and only program the DP DTO for the
				 * crtc virtual pixel clock.
				 */
				if (ENCODER_MODE_IS_DP(atombios_get_encoder_mode(test_encoder))) {
					if (ASIC_IS_DCE5(rdev) || rdev->clock.dp_extclk)
						return ATOM_PPLL_INVALID;
				}
			}
		}

		/* otherwise, pick one of the plls */
		list_for_each_entry(test_crtc, &dev->mode_config.crtc_list, head) {
			struct radeon_crtc *radeon_test_crtc;

			if (crtc == test_crtc)
				continue;

			radeon_test_crtc = to_radeon_crtc(test_crtc);
			if ((radeon_test_crtc->pll_id >= ATOM_PPLL1) &&
			    (radeon_test_crtc->pll_id <= ATOM_PPLL2))
				pll_in_use |= (1 << radeon_test_crtc->pll_id);
		}
		if (!(pll_in_use & 1))
			return ATOM_PPLL1;
		return ATOM_PPLL2;
	} else
		return radeon_crtc->crtc_id;

}

void radeon_atom_disp_eng_pll_init(struct radeon_device *rdev)
{
	/* always set DCPLL */
	if (ASIC_IS_DCE6(rdev))
		atombios_crtc_set_disp_eng_pll(rdev, rdev->clock.default_dispclk);
	else if (ASIC_IS_DCE4(rdev)) {
		struct radeon_atom_ss ss;
		bool ss_enabled = radeon_atombios_get_asic_ss_info(rdev, &ss,
								   ASIC_INTERNAL_SS_ON_DCPLL,
								   rdev->clock.default_dispclk);
		if (ss_enabled)
			atombios_crtc_program_ss(rdev, ATOM_DISABLE, ATOM_DCPLL, &ss);
		/* XXX: DCE5, make sure voltage, dispclk is high enough */
		atombios_crtc_set_disp_eng_pll(rdev, rdev->clock.default_dispclk);
		if (ss_enabled)
			atombios_crtc_program_ss(rdev, ATOM_ENABLE, ATOM_DCPLL, &ss);
	}

}

int atombios_crtc_mode_set(struct drm_crtc *crtc,
			   struct drm_display_mode *mode,
			   struct drm_display_mode *adjusted_mode,
			   int x, int y, struct drm_framebuffer *old_fb)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct drm_encoder *encoder;
	bool is_tvcv = false;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		/* find tv std */
		if (encoder->crtc == crtc) {
			struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
			if (radeon_encoder->active_device &
			    (ATOM_DEVICE_TV_SUPPORT | ATOM_DEVICE_CV_SUPPORT))
				is_tvcv = true;
		}
	}

	atombios_crtc_set_pll(crtc, adjusted_mode);

	if (ASIC_IS_DCE4(rdev))
		atombios_set_crtc_dtd_timing(crtc, adjusted_mode);
	else if (ASIC_IS_AVIVO(rdev)) {
		if (is_tvcv)
			atombios_crtc_set_timing(crtc, adjusted_mode);
		else
			atombios_set_crtc_dtd_timing(crtc, adjusted_mode);
	} else {
		atombios_crtc_set_timing(crtc, adjusted_mode);
		if (radeon_crtc->crtc_id == 0)
			atombios_set_crtc_dtd_timing(crtc, adjusted_mode);
		radeon_legacy_atom_fixup(crtc);
	}
	atombios_crtc_set_base(crtc, x, y, old_fb);
	atombios_overscan_setup(crtc, mode, adjusted_mode);
	atombios_scaler_setup(crtc);
	return 0;
}

static bool atombios_crtc_mode_fixup(struct drm_crtc *crtc,
				     struct drm_display_mode *mode,
				     struct drm_display_mode *adjusted_mode)
{
	if (!radeon_crtc_scaling_mode_fixup(crtc, mode, adjusted_mode))
		return false;
	return true;
}

static void atombios_crtc_prepare(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;

	radeon_crtc->in_mode_set = true;
	/* pick pll */
	radeon_crtc->pll_id = radeon_atom_pick_pll(crtc);

	/* disable crtc pair power gating before programming */
	if (ASIC_IS_DCE6(rdev))
		atombios_powergate_crtc(crtc, ATOM_DISABLE);

	atombios_lock_crtc(crtc, ATOM_ENABLE);
	atombios_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
}

static void atombios_crtc_commit(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);

	atombios_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
	atombios_lock_crtc(crtc, ATOM_DISABLE);
	radeon_crtc->in_mode_set = false;
}

static void atombios_crtc_disable(struct drm_crtc *crtc)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_atom_ss ss;

	atombios_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);

	switch (radeon_crtc->pll_id) {
	case ATOM_PPLL1:
	case ATOM_PPLL2:
		/* disable the ppll */
		atombios_crtc_program_pll(crtc, radeon_crtc->crtc_id, radeon_crtc->pll_id,
					  0, 0, ATOM_DISABLE, 0, 0, 0, 0, 0, false, &ss);
		break;
	case ATOM_PPLL0:
		/* disable the ppll */
		if (ASIC_IS_DCE61(rdev))
			atombios_crtc_program_pll(crtc, radeon_crtc->crtc_id, radeon_crtc->pll_id,
						  0, 0, ATOM_DISABLE, 0, 0, 0, 0, 0, false, &ss);
		break;
	default:
		break;
	}
	radeon_crtc->pll_id = -1;
}

static const struct drm_crtc_helper_funcs atombios_helper_funcs = {
	.dpms = atombios_crtc_dpms,
	.mode_fixup = atombios_crtc_mode_fixup,
	.mode_set = atombios_crtc_mode_set,
	.mode_set_base = atombios_crtc_set_base,
	.mode_set_base_atomic = atombios_crtc_set_base_atomic,
	.prepare = atombios_crtc_prepare,
	.commit = atombios_crtc_commit,
	.load_lut = radeon_crtc_load_lut,
	.disable = atombios_crtc_disable,
};

void radeon_atombios_init_crtc(struct drm_device *dev,
			       struct radeon_crtc *radeon_crtc)
{
	struct radeon_device *rdev = dev->dev_private;

	if (ASIC_IS_DCE4(rdev)) {
		switch (radeon_crtc->crtc_id) {
		case 0:
		default:
			radeon_crtc->crtc_offset = EVERGREEN_CRTC0_REGISTER_OFFSET;
			break;
		case 1:
			radeon_crtc->crtc_offset = EVERGREEN_CRTC1_REGISTER_OFFSET;
			break;
		case 2:
			radeon_crtc->crtc_offset = EVERGREEN_CRTC2_REGISTER_OFFSET;
			break;
		case 3:
			radeon_crtc->crtc_offset = EVERGREEN_CRTC3_REGISTER_OFFSET;
			break;
		case 4:
			radeon_crtc->crtc_offset = EVERGREEN_CRTC4_REGISTER_OFFSET;
			break;
		case 5:
			radeon_crtc->crtc_offset = EVERGREEN_CRTC5_REGISTER_OFFSET;
			break;
		}
	} else {
		if (radeon_crtc->crtc_id == 1)
			radeon_crtc->crtc_offset =
				AVIVO_D2CRTC_H_TOTAL - AVIVO_D1CRTC_H_TOTAL;
		else
			radeon_crtc->crtc_offset = 0;
	}
	radeon_crtc->pll_id = -1;
	drm_crtc_helper_add(&radeon_crtc->base, &atombios_helper_funcs);
}
