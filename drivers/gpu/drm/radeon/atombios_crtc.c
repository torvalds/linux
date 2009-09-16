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
#include "radeon_fixed.h"
#include "radeon.h"
#include "atom.h"
#include "atom-bits.h"

/* evil but including atombios.h is much worse */
bool radeon_atom_get_tv_timings(struct radeon_device *rdev, int index,
				SET_CRTC_TIMING_PARAMETERS_PS_ALLOCATION *crtc_timing,
				int32_t *pixel_clock);
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

	args.usOverscanRight = 0;
	args.usOverscanLeft = 0;
	args.usOverscanBottom = 0;
	args.usOverscanTop = 0;
	args.ucCRTC = radeon_crtc->crtc_id;

	switch (radeon_crtc->rmx_type) {
	case RMX_CENTER:
		args.usOverscanTop = (adjusted_mode->crtc_vdisplay - mode->crtc_vdisplay) / 2;
		args.usOverscanBottom = (adjusted_mode->crtc_vdisplay - mode->crtc_vdisplay) / 2;
		args.usOverscanLeft = (adjusted_mode->crtc_hdisplay - mode->crtc_hdisplay) / 2;
		args.usOverscanRight = (adjusted_mode->crtc_hdisplay - mode->crtc_hdisplay) / 2;
		atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
		break;
	case RMX_ASPECT:
		a1 = mode->crtc_vdisplay * adjusted_mode->crtc_hdisplay;
		a2 = adjusted_mode->crtc_vdisplay * mode->crtc_hdisplay;

		if (a1 > a2) {
			args.usOverscanLeft = (adjusted_mode->crtc_hdisplay - (a2 / mode->crtc_vdisplay)) / 2;
			args.usOverscanRight = (adjusted_mode->crtc_hdisplay - (a2 / mode->crtc_vdisplay)) / 2;
		} else if (a2 > a1) {
			args.usOverscanLeft = (adjusted_mode->crtc_vdisplay - (a1 / mode->crtc_hdisplay)) / 2;
			args.usOverscanRight = (adjusted_mode->crtc_vdisplay - (a1 / mode->crtc_hdisplay)) / 2;
		}
		atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
		break;
	case RMX_FULL:
	default:
		args.usOverscanRight = 0;
		args.usOverscanLeft = 0;
		args.usOverscanBottom = 0;
		args.usOverscanTop = 0;
		atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
		break;
	}
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

void atombios_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		if (ASIC_IS_DCE3(rdev))
			atombios_enable_crtc_memreq(crtc, 1);
		atombios_enable_crtc(crtc, 1);
		atombios_blank_crtc(crtc, 0);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		atombios_blank_crtc(crtc, 1);
		atombios_enable_crtc(crtc, 0);
		if (ASIC_IS_DCE3(rdev))
			atombios_enable_crtc_memreq(crtc, 0);
		break;
	}

	if (mode != DRM_MODE_DPMS_OFF) {
		radeon_crtc_load_lut(crtc);
	}
}

static void
atombios_set_crtc_dtd_timing(struct drm_crtc *crtc,
			     SET_CRTC_USING_DTD_TIMING_PARAMETERS * crtc_param)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	SET_CRTC_USING_DTD_TIMING_PARAMETERS conv_param;
	int index = GetIndexIntoMasterTable(COMMAND, SetCRTC_UsingDTDTiming);

	conv_param.usH_Size = cpu_to_le16(crtc_param->usH_Size);
	conv_param.usH_Blanking_Time =
	    cpu_to_le16(crtc_param->usH_Blanking_Time);
	conv_param.usV_Size = cpu_to_le16(crtc_param->usV_Size);
	conv_param.usV_Blanking_Time =
	    cpu_to_le16(crtc_param->usV_Blanking_Time);
	conv_param.usH_SyncOffset = cpu_to_le16(crtc_param->usH_SyncOffset);
	conv_param.usH_SyncWidth = cpu_to_le16(crtc_param->usH_SyncWidth);
	conv_param.usV_SyncOffset = cpu_to_le16(crtc_param->usV_SyncOffset);
	conv_param.usV_SyncWidth = cpu_to_le16(crtc_param->usV_SyncWidth);
	conv_param.susModeMiscInfo.usAccess =
	    cpu_to_le16(crtc_param->susModeMiscInfo.usAccess);
	conv_param.ucCRTC = crtc_param->ucCRTC;

	printk("executing set crtc dtd timing\n");
	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&conv_param);
}

void atombios_crtc_set_timing(struct drm_crtc *crtc,
			      SET_CRTC_TIMING_PARAMETERS_PS_ALLOCATION *
			      crtc_param)
{
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	SET_CRTC_TIMING_PARAMETERS_PS_ALLOCATION conv_param;
	int index = GetIndexIntoMasterTable(COMMAND, SetCRTC_Timing);

	conv_param.usH_Total = cpu_to_le16(crtc_param->usH_Total);
	conv_param.usH_Disp = cpu_to_le16(crtc_param->usH_Disp);
	conv_param.usH_SyncStart = cpu_to_le16(crtc_param->usH_SyncStart);
	conv_param.usH_SyncWidth = cpu_to_le16(crtc_param->usH_SyncWidth);
	conv_param.usV_Total = cpu_to_le16(crtc_param->usV_Total);
	conv_param.usV_Disp = cpu_to_le16(crtc_param->usV_Disp);
	conv_param.usV_SyncStart = cpu_to_le16(crtc_param->usV_SyncStart);
	conv_param.usV_SyncWidth = cpu_to_le16(crtc_param->usV_SyncWidth);
	conv_param.susModeMiscInfo.usAccess =
	    cpu_to_le16(crtc_param->susModeMiscInfo.usAccess);
	conv_param.ucCRTC = crtc_param->ucCRTC;
	conv_param.ucOverscanRight = crtc_param->ucOverscanRight;
	conv_param.ucOverscanLeft = crtc_param->ucOverscanLeft;
	conv_param.ucOverscanBottom = crtc_param->ucOverscanBottom;
	conv_param.ucOverscanTop = crtc_param->ucOverscanTop;
	conv_param.ucReserved = crtc_param->ucReserved;

	printk("executing set crtc timing\n");
	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&conv_param);
}

void atombios_crtc_set_pll(struct drm_crtc *crtc, struct drm_display_mode *mode)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct drm_encoder *encoder = NULL;
	struct radeon_encoder *radeon_encoder = NULL;
	uint8_t frev, crev;
	int index = GetIndexIntoMasterTable(COMMAND, SetPixelClock);
	SET_PIXEL_CLOCK_PS_ALLOCATION args;
	PIXEL_CLOCK_PARAMETERS *spc1_ptr;
	PIXEL_CLOCK_PARAMETERS_V2 *spc2_ptr;
	PIXEL_CLOCK_PARAMETERS_V3 *spc3_ptr;
	uint32_t sclock = mode->clock;
	uint32_t ref_div = 0, fb_div = 0, frac_fb_div = 0, post_div = 0;
	struct radeon_pll *pll;
	int pll_flags = 0;

	memset(&args, 0, sizeof(args));

	if (ASIC_IS_AVIVO(rdev)) {
		uint32_t ss_cntl;

		if ((rdev->family == CHIP_RS600) ||
		    (rdev->family == CHIP_RS690) ||
		    (rdev->family == CHIP_RS740))
			pll_flags |= (RADEON_PLL_USE_FRAC_FB_DIV |
				      RADEON_PLL_PREFER_CLOSEST_LOWER);

		if (ASIC_IS_DCE32(rdev) && mode->clock > 200000)	/* range limits??? */
			pll_flags |= RADEON_PLL_PREFER_HIGH_FB_DIV;
		else
			pll_flags |= RADEON_PLL_PREFER_LOW_REF_DIV;

		/* disable spread spectrum clocking for now -- thanks Hedy Lamarr */
		if (radeon_crtc->crtc_id == 0) {
			ss_cntl = RREG32(AVIVO_P1PLL_INT_SS_CNTL);
			WREG32(AVIVO_P1PLL_INT_SS_CNTL, ss_cntl & ~1);
		} else {
			ss_cntl = RREG32(AVIVO_P2PLL_INT_SS_CNTL);
			WREG32(AVIVO_P2PLL_INT_SS_CNTL, ss_cntl & ~1);
		}
	} else {
		pll_flags |= RADEON_PLL_LEGACY;

		if (mode->clock > 200000)	/* range limits??? */
			pll_flags |= RADEON_PLL_PREFER_HIGH_FB_DIV;
		else
			pll_flags |= RADEON_PLL_PREFER_LOW_REF_DIV;

	}

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (encoder->crtc == crtc) {
			if (!ASIC_IS_AVIVO(rdev)) {
				if (encoder->encoder_type !=
				    DRM_MODE_ENCODER_DAC)
					pll_flags |= RADEON_PLL_NO_ODD_POST_DIV;
				if (!ASIC_IS_AVIVO(rdev)
				    && (encoder->encoder_type ==
					DRM_MODE_ENCODER_LVDS))
					pll_flags |= RADEON_PLL_USE_REF_DIV;
			}
			radeon_encoder = to_radeon_encoder(encoder);
			break;
		}
	}

	if (radeon_crtc->crtc_id == 0)
		pll = &rdev->clock.p1pll;
	else
		pll = &rdev->clock.p2pll;

	radeon_compute_pll(pll, mode->clock, &sclock, &fb_div, &frac_fb_div,
			   &ref_div, &post_div, pll_flags);

	atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev,
			      &crev);

	switch (frev) {
	case 1:
		switch (crev) {
		case 1:
			spc1_ptr = (PIXEL_CLOCK_PARAMETERS *) & args.sPCLKInput;
			spc1_ptr->usPixelClock = cpu_to_le16(sclock);
			spc1_ptr->usRefDiv = cpu_to_le16(ref_div);
			spc1_ptr->usFbDiv = cpu_to_le16(fb_div);
			spc1_ptr->ucFracFbDiv = frac_fb_div;
			spc1_ptr->ucPostDiv = post_div;
			spc1_ptr->ucPpll =
			    radeon_crtc->crtc_id ? ATOM_PPLL2 : ATOM_PPLL1;
			spc1_ptr->ucCRTC = radeon_crtc->crtc_id;
			spc1_ptr->ucRefDivSrc = 1;
			break;
		case 2:
			spc2_ptr =
			    (PIXEL_CLOCK_PARAMETERS_V2 *) & args.sPCLKInput;
			spc2_ptr->usPixelClock = cpu_to_le16(sclock);
			spc2_ptr->usRefDiv = cpu_to_le16(ref_div);
			spc2_ptr->usFbDiv = cpu_to_le16(fb_div);
			spc2_ptr->ucFracFbDiv = frac_fb_div;
			spc2_ptr->ucPostDiv = post_div;
			spc2_ptr->ucPpll =
			    radeon_crtc->crtc_id ? ATOM_PPLL2 : ATOM_PPLL1;
			spc2_ptr->ucCRTC = radeon_crtc->crtc_id;
			spc2_ptr->ucRefDivSrc = 1;
			break;
		case 3:
			if (!encoder)
				return;
			spc3_ptr =
			    (PIXEL_CLOCK_PARAMETERS_V3 *) & args.sPCLKInput;
			spc3_ptr->usPixelClock = cpu_to_le16(sclock);
			spc3_ptr->usRefDiv = cpu_to_le16(ref_div);
			spc3_ptr->usFbDiv = cpu_to_le16(fb_div);
			spc3_ptr->ucFracFbDiv = frac_fb_div;
			spc3_ptr->ucPostDiv = post_div;
			spc3_ptr->ucPpll =
			    radeon_crtc->crtc_id ? ATOM_PPLL2 : ATOM_PPLL1;
			spc3_ptr->ucMiscInfo = (radeon_crtc->crtc_id << 2);
			spc3_ptr->ucTransmitterId = radeon_encoder->encoder_id;
			spc3_ptr->ucEncoderMode =
			    atombios_get_encoder_mode(encoder);
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

	printk("executing set pll\n");
	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

int atombios_crtc_set_base(struct drm_crtc *crtc, int x, int y,
			   struct drm_framebuffer *old_fb)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_framebuffer *radeon_fb;
	struct drm_gem_object *obj;
	struct drm_radeon_gem_object *obj_priv;
	uint64_t fb_location;
	uint32_t fb_format, fb_pitch_pixels, tiling_flags;

	if (!crtc->fb)
		return -EINVAL;

	radeon_fb = to_radeon_framebuffer(crtc->fb);

	obj = radeon_fb->obj;
	obj_priv = obj->driver_private;

	if (radeon_gem_object_pin(obj, RADEON_GEM_DOMAIN_VRAM, &fb_location)) {
		return -EINVAL;
	}

	switch (crtc->fb->bits_per_pixel) {
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
		break;
	case 24:
	case 32:
		fb_format =
		    AVIVO_D1GRPH_CONTROL_DEPTH_32BPP |
		    AVIVO_D1GRPH_CONTROL_32BPP_ARGB8888;
		break;
	default:
		DRM_ERROR("Unsupported screen depth %d\n",
			  crtc->fb->bits_per_pixel);
		return -EINVAL;
	}

	radeon_object_get_tiling_flags(obj->driver_private,
				       &tiling_flags, NULL);
	if (tiling_flags & RADEON_TILING_MACRO)
		fb_format |= AVIVO_D1GRPH_MACRO_ADDRESS_MODE;

	if (tiling_flags & RADEON_TILING_MICRO)
		fb_format |= AVIVO_D1GRPH_TILED;

	if (radeon_crtc->crtc_id == 0)
		WREG32(AVIVO_D1VGA_CONTROL, 0);
	else
		WREG32(AVIVO_D2VGA_CONTROL, 0);
	WREG32(AVIVO_D1GRPH_PRIMARY_SURFACE_ADDRESS + radeon_crtc->crtc_offset,
	       (u32) fb_location);
	WREG32(AVIVO_D1GRPH_SECONDARY_SURFACE_ADDRESS +
	       radeon_crtc->crtc_offset, (u32) fb_location);
	WREG32(AVIVO_D1GRPH_CONTROL + radeon_crtc->crtc_offset, fb_format);

	WREG32(AVIVO_D1GRPH_SURFACE_OFFSET_X + radeon_crtc->crtc_offset, 0);
	WREG32(AVIVO_D1GRPH_SURFACE_OFFSET_Y + radeon_crtc->crtc_offset, 0);
	WREG32(AVIVO_D1GRPH_X_START + radeon_crtc->crtc_offset, 0);
	WREG32(AVIVO_D1GRPH_Y_START + radeon_crtc->crtc_offset, 0);
	WREG32(AVIVO_D1GRPH_X_END + radeon_crtc->crtc_offset, crtc->fb->width);
	WREG32(AVIVO_D1GRPH_Y_END + radeon_crtc->crtc_offset, crtc->fb->height);

	fb_pitch_pixels = crtc->fb->pitch / (crtc->fb->bits_per_pixel / 8);
	WREG32(AVIVO_D1GRPH_PITCH + radeon_crtc->crtc_offset, fb_pitch_pixels);
	WREG32(AVIVO_D1GRPH_ENABLE + radeon_crtc->crtc_offset, 1);

	WREG32(AVIVO_D1MODE_DESKTOP_HEIGHT + radeon_crtc->crtc_offset,
	       crtc->mode.vdisplay);
	x &= ~3;
	y &= ~1;
	WREG32(AVIVO_D1MODE_VIEWPORT_START + radeon_crtc->crtc_offset,
	       (x << 16) | y);
	WREG32(AVIVO_D1MODE_VIEWPORT_SIZE + radeon_crtc->crtc_offset,
	       (crtc->mode.hdisplay << 16) | crtc->mode.vdisplay);

	if (crtc->mode.flags & DRM_MODE_FLAG_INTERLACE)
		WREG32(AVIVO_D1MODE_DATA_FORMAT + radeon_crtc->crtc_offset,
		       AVIVO_D1MODE_INTERLEAVE_EN);
	else
		WREG32(AVIVO_D1MODE_DATA_FORMAT + radeon_crtc->crtc_offset, 0);

	if (old_fb && old_fb != crtc->fb) {
		radeon_fb = to_radeon_framebuffer(old_fb);
		radeon_gem_object_unpin(radeon_fb->obj);
	}
	return 0;
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
	SET_CRTC_TIMING_PARAMETERS_PS_ALLOCATION crtc_timing;
	int need_tv_timings = 0;
	bool ret;

	/* TODO color tiling */
	memset(&crtc_timing, 0, sizeof(crtc_timing));

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		/* find tv std */
		if (encoder->crtc == crtc) {
			struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);

			if (radeon_encoder->active_device & ATOM_DEVICE_TV_SUPPORT) {
				struct radeon_encoder_atom_dac *tv_dac = radeon_encoder->enc_priv;
				if (tv_dac) {
					if (tv_dac->tv_std == TV_STD_NTSC ||
					    tv_dac->tv_std == TV_STD_NTSC_J ||
					    tv_dac->tv_std == TV_STD_PAL_M)
						need_tv_timings = 1;
					else
						need_tv_timings = 2;
					break;
				}
			}
		}
	}

	crtc_timing.ucCRTC = radeon_crtc->crtc_id;
	if (need_tv_timings) {
		ret = radeon_atom_get_tv_timings(rdev, need_tv_timings - 1,
						 &crtc_timing, &adjusted_mode->clock);
		if (ret == false)
			need_tv_timings = 0;
	}

	if (!need_tv_timings) {
		crtc_timing.usH_Total = adjusted_mode->crtc_htotal;
		crtc_timing.usH_Disp = adjusted_mode->crtc_hdisplay;
		crtc_timing.usH_SyncStart = adjusted_mode->crtc_hsync_start;
		crtc_timing.usH_SyncWidth =
			adjusted_mode->crtc_hsync_end - adjusted_mode->crtc_hsync_start;

		crtc_timing.usV_Total = adjusted_mode->crtc_vtotal;
		crtc_timing.usV_Disp = adjusted_mode->crtc_vdisplay;
		crtc_timing.usV_SyncStart = adjusted_mode->crtc_vsync_start;
		crtc_timing.usV_SyncWidth =
			adjusted_mode->crtc_vsync_end - adjusted_mode->crtc_vsync_start;

		if (adjusted_mode->flags & DRM_MODE_FLAG_NVSYNC)
			crtc_timing.susModeMiscInfo.usAccess |= ATOM_VSYNC_POLARITY;

		if (adjusted_mode->flags & DRM_MODE_FLAG_NHSYNC)
			crtc_timing.susModeMiscInfo.usAccess |= ATOM_HSYNC_POLARITY;

		if (adjusted_mode->flags & DRM_MODE_FLAG_CSYNC)
			crtc_timing.susModeMiscInfo.usAccess |= ATOM_COMPOSITESYNC;

		if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE)
			crtc_timing.susModeMiscInfo.usAccess |= ATOM_INTERLACE;

		if (adjusted_mode->flags & DRM_MODE_FLAG_DBLSCAN)
			crtc_timing.susModeMiscInfo.usAccess |= ATOM_DOUBLE_CLOCK_MODE;
	}

	atombios_crtc_set_pll(crtc, adjusted_mode);
	atombios_crtc_set_timing(crtc, &crtc_timing);

	if (ASIC_IS_AVIVO(rdev))
		atombios_crtc_set_base(crtc, x, y, old_fb);
	else {
		if (radeon_crtc->crtc_id == 0) {
			SET_CRTC_USING_DTD_TIMING_PARAMETERS crtc_dtd_timing;
			memset(&crtc_dtd_timing, 0, sizeof(crtc_dtd_timing));

			/* setup FP shadow regs on R4xx */
			crtc_dtd_timing.ucCRTC = radeon_crtc->crtc_id;
			crtc_dtd_timing.usH_Size = adjusted_mode->crtc_hdisplay;
			crtc_dtd_timing.usV_Size = adjusted_mode->crtc_vdisplay;
			crtc_dtd_timing.usH_Blanking_Time =
			    adjusted_mode->crtc_hblank_end -
			    adjusted_mode->crtc_hdisplay;
			crtc_dtd_timing.usV_Blanking_Time =
			    adjusted_mode->crtc_vblank_end -
			    adjusted_mode->crtc_vdisplay;
			crtc_dtd_timing.usH_SyncOffset =
			    adjusted_mode->crtc_hsync_start -
			    adjusted_mode->crtc_hdisplay;
			crtc_dtd_timing.usV_SyncOffset =
			    adjusted_mode->crtc_vsync_start -
			    adjusted_mode->crtc_vdisplay;
			crtc_dtd_timing.usH_SyncWidth =
			    adjusted_mode->crtc_hsync_end -
			    adjusted_mode->crtc_hsync_start;
			crtc_dtd_timing.usV_SyncWidth =
			    adjusted_mode->crtc_vsync_end -
			    adjusted_mode->crtc_vsync_start;
			/* crtc_dtd_timing.ucH_Border = adjusted_mode->crtc_hborder; */
			/* crtc_dtd_timing.ucV_Border = adjusted_mode->crtc_vborder; */

			if (adjusted_mode->flags & DRM_MODE_FLAG_NVSYNC)
				crtc_dtd_timing.susModeMiscInfo.usAccess |=
				    ATOM_VSYNC_POLARITY;

			if (adjusted_mode->flags & DRM_MODE_FLAG_NHSYNC)
				crtc_dtd_timing.susModeMiscInfo.usAccess |=
				    ATOM_HSYNC_POLARITY;

			if (adjusted_mode->flags & DRM_MODE_FLAG_CSYNC)
				crtc_dtd_timing.susModeMiscInfo.usAccess |=
				    ATOM_COMPOSITESYNC;

			if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE)
				crtc_dtd_timing.susModeMiscInfo.usAccess |=
				    ATOM_INTERLACE;

			if (adjusted_mode->flags & DRM_MODE_FLAG_DBLSCAN)
				crtc_dtd_timing.susModeMiscInfo.usAccess |=
				    ATOM_DOUBLE_CLOCK_MODE;

			atombios_set_crtc_dtd_timing(crtc, &crtc_dtd_timing);
		}
		radeon_crtc_set_base(crtc, x, y, old_fb);
		radeon_legacy_atom_set_surface(crtc);
	}
	atombios_overscan_setup(crtc, mode, adjusted_mode);
	atombios_scaler_setup(crtc);
	radeon_bandwidth_update(rdev);
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
	atombios_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
	atombios_lock_crtc(crtc, 1);
}

static void atombios_crtc_commit(struct drm_crtc *crtc)
{
	atombios_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
	atombios_lock_crtc(crtc, 0);
}

static const struct drm_crtc_helper_funcs atombios_helper_funcs = {
	.dpms = atombios_crtc_dpms,
	.mode_fixup = atombios_crtc_mode_fixup,
	.mode_set = atombios_crtc_mode_set,
	.mode_set_base = atombios_crtc_set_base,
	.prepare = atombios_crtc_prepare,
	.commit = atombios_crtc_commit,
};

void radeon_atombios_init_crtc(struct drm_device *dev,
			       struct radeon_crtc *radeon_crtc)
{
	if (radeon_crtc->crtc_id == 1)
		radeon_crtc->crtc_offset =
		    AVIVO_D2CRTC_H_TOTAL - AVIVO_D1CRTC_H_TOTAL;
	drm_crtc_helper_add(&radeon_crtc->base, &atombios_helper_funcs);
}
